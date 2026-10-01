/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "core/ls-context.h"
#include <string.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
struct _LsContext {
	GObject parent_instance;
	GHashTable *files;
	GHashTable *archive;	/* path -> LsArtifact kept by native steps, never exported */
	gboolean revision;
};
G_DEFINE_TYPE(LsContext, ls_context, G_TYPE_OBJECT)
/* All artifact ownership lives in the hash table. */
static void
ls_context_finalize(GObject *object){
	g_hash_table_unref(LS_CONTEXT(object)->files);
	g_hash_table_unref(LS_CONTEXT(object)->archive);
	G_OBJECT_CLASS(ls_context_parent_class)->finalize(object);
}
static void
ls_context_class_init(LsContextClass *klass){
	G_OBJECT_CLASS(klass)->finalize = ls_context_finalize;
}
static void
ls_context_init(LsContext *self){
	self->files =
		g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify)ls_artifact_free);
	self->archive =
		g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify)ls_artifact_free);
}
LsContext *
ls_context_new(void){
	return g_object_new(LS_TYPE_CONTEXT, NULL);
}

/* Reject whitespace and characters outside the standard base64 alphabet. */
static gboolean
is_base64(const gchar *text){
	const gchar *cursor;
	guint pads = 0;
	gsize length;
	if (text == NULL)
		return FALSE;
	length = strlen(text);
	if (length % 4 != 0)
		return FALSE;
	for (cursor = text; *cursor != '\0'; cursor++) {
		if (*cursor == '=') {
			pads++;
			if (pads > 2)
				return FALSE;
		} else if (pads > 0 ||
			(!g_ascii_isalnum(*cursor) && *cursor != '+' && *cursor != '/'))
			return FALSE;
	}
	return TRUE;
}

/* Decode a files[] object. encoding=base64 stores decoded bytes; valid UTF-8
 * without NUL is stored as text so SVG accidentally sent as base64 stays usable. */
static LsArtifact *
artifact_from_object(JsonObject *object, const LsArtifact *previous, GError **error){
	const gchar *path = ls_string(object, "path");
	const gchar *content = ls_string(object, "content");
	const gchar *encoding = ls_string(object, "encoding");
	JsonNode *mode;
	gboolean executable;
	if (!ls_object_keys(object, "|path|content|executable|encoding|", error))
		return NULL;
	mode = json_object_get_member(object, "executable");
	if (mode != NULL && json_node_get_value_type(mode) != G_TYPE_BOOLEAN) {
		g_set_error_literal(
			error, LS_ERROR, LS_ERROR_INVALID,
			"Expected JSON {files: [{path, content, encoding?: utf8|base64, executable?: bool}]}"
		);
		return NULL;
	}
	executable = mode != NULL ? json_node_get_boolean(mode)
		: previous != NULL && ls_artifact_get_executable(previous);
	if (encoding == NULL || g_str_equal(encoding, "utf8"))
		return ls_artifact_new_full(path, content, executable, error);
	if (!g_str_equal(encoding, "base64") || content == NULL || !is_base64(content)) {
		g_set_error(error, LS_ERROR, LS_ERROR_INVALID,
			"encoding must be utf8 or base64: %s", path != NULL ? path : "(missing)");
		return NULL;
	}
	{
		gsize size = 0;
		g_autofree guchar *bytes = g_base64_decode(content, &size);
		gboolean binary = size == 0 || memchr(bytes, 0, size) != NULL ||
			!g_utf8_validate((const gchar *)bytes, (gssize)size, NULL);
		return ls_artifact_new_bytes(path, bytes, size, binary, executable, error);
	}
}

/* Snapshot and prompt bundles share one object grammar so checkpoints remain
 * loadable as apply payloads. Binary files always travel as base64. */
static void
artifact_to_builder(JsonBuilder *builder, const LsArtifact *artifact){
	gsize size;
	const guint8 *data = ls_artifact_get_data(artifact, &size);
	json_builder_begin_object(builder);
	json_builder_set_member_name(builder, "path");
	json_builder_add_string_value(builder, ls_artifact_get_path(artifact));
	json_builder_set_member_name(builder, "content");
	if (ls_artifact_is_binary(artifact)) {
		g_autofree gchar *encoded = g_base64_encode(data, size);
		json_builder_add_string_value(builder, encoded);
		json_builder_set_member_name(builder, "encoding");
		json_builder_add_string_value(builder, "base64");
	} else
		json_builder_add_string_value(builder, ls_artifact_get_content(artifact));
	if (ls_artifact_get_executable(artifact)) {
		json_builder_set_member_name(builder, "executable");
		json_builder_add_boolean_value(builder, TRUE);
	}
	json_builder_end_object(builder);
}

static void
manifest_to_builder(JsonBuilder *builder, const LsArtifact *artifact){
	gsize size;
	const guint8 *data = ls_artifact_get_data(artifact, &size);
	g_autofree gchar *digest = g_compute_checksum_for_data(G_CHECKSUM_SHA256, data, size);
	json_builder_begin_object(builder);
	json_builder_set_member_name(builder, "path");
	json_builder_add_string_value(builder, ls_artifact_get_path(artifact));
	json_builder_set_member_name(builder, "bytes");
	json_builder_add_int_value(builder, (gint64)size);
	json_builder_set_member_name(builder, "encoding");
	json_builder_add_string_value(builder, ls_artifact_is_binary(artifact) ? "base64" : "utf8");
	json_builder_set_member_name(builder, "sha256");
	json_builder_add_string_value(builder, digest);
	if (ls_artifact_get_executable(artifact)) {
		json_builder_set_member_name(builder, "executable");
		json_builder_add_boolean_value(builder, TRUE);
	}
	json_builder_end_object(builder);
}

/* Contract documents travel with every focused implementation stage. */
static gboolean
is_contract_path(const gchar *path){
	return g_str_has_prefix(path, "docs/") || g_str_has_prefix(path, "config/") ||
		g_str_equal(path, "AGENTS.md") || g_str_equal(path, "README.org");
}

/* Build a candidate map and detect file/directory conflicts before committing. */
gboolean
ls_context_apply(LsContext *self, const gchar *response, GError **error){
	g_autoptr(JsonNode) parsed = NULL;
	g_autoptr(GHashTable) candidate =
		g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify)ls_artifact_free);
	g_autoptr(GHashTable) seen = g_hash_table_new(g_str_hash, g_str_equal);
	g_autoptr(GHashTable) rename_from = g_hash_table_new(g_str_hash, g_str_equal);
	g_autoptr(GHashTable) rename_to = g_hash_table_new(g_str_hash, g_str_equal);
	JsonNode *root, *node;
	JsonObject *object;
	JsonArray *files;
	GHashTableIter iter;
	gpointer key, value;
	gsize total = 0;
	guint i;
	if (response == NULL || strlen(response) > LS_MAX_PROJECT * 2 ||
		!g_utf8_validate(response, -1, NULL))
		goto invalid;
	parsed = ls_json_parse(response, error);
	if (parsed == NULL)
		return FALSE;
	root = parsed;
	if (!JSON_NODE_HOLDS_OBJECT(root))
		goto invalid;
	object = json_node_get_object(root);
	if (!ls_object_keys(object, "|files|summary|delete|rename|", error))
		return FALSE;
	if (json_object_has_member(object, "summary") && ls_string(object, "summary") == NULL)
		goto invalid;
	node = json_object_get_member(object, "files");
	if (node == NULL || !JSON_NODE_HOLDS_ARRAY(node))
		goto invalid;
	files = json_node_get_array(node);
	if (json_array_get_length(files) > 4096)
		goto invalid;
	g_hash_table_iter_init(&iter, self->files);
	while (g_hash_table_iter_next(&iter, &key, &value))
		g_hash_table_insert(
			candidate, g_strdup((const gchar *)key), ls_artifact_copy((const LsArtifact *)value)
		);
	/* Deletions, renames and replacements share one transaction. Removing a
	 * directory means explicitly naming each stored file beneath it. */
	node = json_object_get_member(object, "delete");
	if (node != NULL) {
		JsonArray *removed;
		if (!JSON_NODE_HOLDS_ARRAY(node))
			goto invalid;
		removed = json_node_get_array(node);
		if (json_array_get_length(removed) > 4096)
			goto invalid;
		for (i = 0; i < json_array_get_length(removed); i++) {
			const gchar *path;
			node = json_array_get_element(removed, i);
			if (json_node_get_value_type(node) != G_TYPE_STRING)
				goto invalid;
			path = json_node_get_string(node);
			if (!ls_path_valid(path) || !g_hash_table_add(seen, (gpointer)path) ||
				!g_hash_table_remove(candidate, path)) {
				g_set_error(error, LS_ERROR, LS_ERROR_INVALID,
					"Deletion requires a unique existing file: %s", path);
				return FALSE;
			}
		}
	}
	node = json_object_get_member(json_node_get_object(root), "rename");
	if (node != NULL) {
		JsonArray *renames;
		g_autoptr(GHashTable) pending =
			g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify)ls_artifact_free);
		if (!JSON_NODE_HOLDS_ARRAY(node))
			goto invalid;
		renames = json_node_get_array(node);
		if (json_array_get_length(renames) > 4096)
			goto invalid;
		for (i = 0; i < json_array_get_length(renames); i++) {
			JsonObject *pair;
			const gchar *from;
			const gchar *to;
			node = json_array_get_element(renames, i);
			if (!JSON_NODE_HOLDS_OBJECT(node))
				goto invalid;
			pair = json_node_get_object(node);
			if (!ls_object_keys(pair, "|from|to|", error))
				return FALSE;
			from = ls_string(pair, "from");
			to = ls_string(pair, "to");
			if (!ls_path_valid(from) || !ls_path_valid(to) || g_str_equal(from, to) ||
				g_hash_table_contains(seen, from) || g_hash_table_contains(seen, to) ||
				!g_hash_table_add(rename_from, (gpointer)from) ||
				!g_hash_table_add(rename_to, (gpointer)to) ||
				!g_hash_table_contains(candidate, from)) {
				g_set_error(error, LS_ERROR, LS_ERROR_INVALID,
					"Rename requires unique existing from and unused to paths: %s -> %s",
					from != NULL ? from : "(missing)", to != NULL ? to : "(missing)");
				return FALSE;
			}
		}
		/* Pull every source first so a->b plus b->a is a swap, not a clash. */
		for (i = 0; i < json_array_get_length(renames); i++) {
			JsonObject *pair = json_node_get_object(json_array_get_element(renames, i));
			const gchar *from = ls_string(pair, "from");
			LsArtifact *moved = ls_artifact_copy(g_hash_table_lookup(candidate, from));
			g_hash_table_remove(candidate, from);
			g_hash_table_insert(pending, g_strdup(from), moved);
		}
		for (i = 0; i < json_array_get_length(renames); i++) {
			JsonObject *pair = json_node_get_object(json_array_get_element(renames, i));
			const gchar *from = ls_string(pair, "from");
			const gchar *to = ls_string(pair, "to");
			LsArtifact *moved;
			LsArtifact *renamed;
			if (g_hash_table_contains(candidate, to)) {
				g_set_error(error, LS_ERROR, LS_ERROR_INVALID,
					"Rename destination already exists: %s", to);
				return FALSE;
			}
			moved = g_hash_table_lookup(pending, from);
			renamed = ls_artifact_with_path(moved, to, error);
			if (renamed == NULL)
				return FALSE;
			g_hash_table_insert(candidate, g_strdup(to), renamed);
		}
	}
	for (i = 0; i < json_array_get_length(files); i++) {
		LsArtifact *artifact;
		const LsArtifact *previous;
		const gchar *path;
		node = json_array_get_element(files, i);
		if (!JSON_NODE_HOLDS_OBJECT(node))
			goto invalid;
		object = json_node_get_object(node);
		path = ls_string(object, "path");
		if (path != NULL && (g_hash_table_contains(seen, path) || g_hash_table_contains(rename_to, path))) {
			g_set_error(error, LS_ERROR, LS_ERROR_INVALID,
				"File operation overlaps delete or rename destination: %s", path);
			return FALSE;
		}
		previous = path != NULL ? ls_context_lookup(self, path) : NULL;
		if (path != NULL && g_hash_table_contains(rename_from, path))
			previous = NULL;
		artifact = artifact_from_object(object, previous, error);
		if (artifact == NULL)
			return FALSE;
		if (!g_hash_table_add(seen, (gpointer)path)) {
			ls_artifact_free(artifact);
			g_set_error(error, LS_ERROR, LS_ERROR_INVALID, "Duplicate artifact '%s'", path);
			return FALSE;
		}
		g_hash_table_replace(candidate, g_strdup(path), artifact);
	}
	g_hash_table_iter_init(&iter, candidate);
	while (g_hash_table_iter_next(&iter, &key, &value)) {
		g_autofree gchar *parent = g_path_get_dirname((const gchar *)key);
		total += ls_artifact_get_size((const LsArtifact *)value);
		while (!g_str_equal(parent, ".")) {
			gchar *next;
			if (g_hash_table_contains(candidate, parent)) {
				g_set_error(
					error, LS_ERROR, LS_ERROR_INVALID, "File/directory conflict at '%s'", parent
				);
				return FALSE;
			}
			next = g_path_get_dirname(parent);
			g_free(parent);
			parent = next;
		}
	}
	if (total > LS_MAX_PROJECT || g_hash_table_size(candidate) > 4096) {
		g_set_error_literal(
			error, LS_ERROR, LS_ERROR_LIMIT, "Project exceeds 64 MiB or 4096 files"
		);
		return FALSE;
	}
	g_hash_table_unref(self->files);
	self->files = (GHashTable *)g_steal_pointer(&candidate);
	return TRUE;
invalid:
	g_set_error_literal(
		error,
		LS_ERROR,
		LS_ERROR_INVALID,
		"Expected JSON {files: [{path, content, encoding?: utf8|base64}], "
		"delete?: [path], rename?: [{from, to}], summary?: string}"
	);
	return FALSE;
}
/* Stable path ordering makes checkpoints and prompt previews reproducible. */
gchar *
ls_context_snapshot(LsContext *self){
	g_autoptr(JsonBuilder) builder = json_builder_new();
	g_autoptr(JsonNode) root = NULL;
	g_autoptr(GList) keys =
		g_list_sort(g_hash_table_get_keys(self->files), (GCompareFunc)g_strcmp0);
	GList *item;
	json_builder_begin_object(builder);
	json_builder_set_member_name(builder, "files");
	json_builder_begin_array(builder);
	for (item = keys; item != NULL; item = item->next) {
		const LsArtifact *artifact =
			(const LsArtifact *)g_hash_table_lookup(self->files, item->data);
		artifact_to_builder(builder, artifact);
	}
	json_builder_end_array(builder);
	/* The archive member appears only when used, so older snapshots stay byte-identical. */
	if (g_hash_table_size(self->archive) > 0) {
		g_autoptr(GList) archived =
			g_list_sort(g_hash_table_get_keys(self->archive), (GCompareFunc)g_strcmp0);
		json_builder_set_member_name(builder, "archive");
		json_builder_begin_array(builder);
		for (item = archived; item != NULL; item = item->next)
			artifact_to_builder(builder, g_hash_table_lookup(self->archive, item->data));
		json_builder_end_array(builder);
	}
	json_builder_end_object(builder);
	root = json_builder_get_root(builder);
	return ls_json_encode(root);
}

/* Collect exact paths and glob selectors from open-ended generation options. */
static gboolean
load_selectors(JsonObject *step, GHashTable *exact, GPtrArray *globs, gboolean *focused,
	GError **error){
	JsonNode *options_node;
	JsonObject *options;
	JsonNode *node;
	*focused = FALSE;
	if (step == NULL)
		return TRUE;
	options_node = json_object_get_member(step, "options");
	if (options_node == NULL || !JSON_NODE_HOLDS_OBJECT(options_node))
		return TRUE;
	options = json_node_get_object(options_node);
	node = json_object_get_member(options, "files");
	if (node != NULL && JSON_NODE_HOLDS_ARRAY(node)) {
		JsonArray *files = json_node_get_array(node);
		guint i;
		gboolean strings = json_array_get_length(files) > 0;
		for (i = 0; i < json_array_get_length(files); i++) {
			JsonNode *element = json_array_get_element(files, i);
			if (json_node_get_value_type(element) != G_TYPE_STRING) {
				strings = FALSE;
				break;
			}
		}
		if (strings) {
			*focused = TRUE;
			for (i = 0; i < json_array_get_length(files); i++)
				g_hash_table_add(exact, g_strdup(json_array_get_string_element(files, i)));
		}
	}
	node = json_object_get_member(options, "context");
	if (node == NULL)
		return TRUE;
	if (!JSON_NODE_HOLDS_ARRAY(node) || json_array_get_length(json_node_get_array(node)) == 0 ||
		json_array_get_length(json_node_get_array(node)) > 4096) {
		g_set_error_literal(error, LS_ERROR, LS_ERROR_INVALID,
			"options.context must contain 1..4096 unique nonempty glob strings");
		return FALSE;
	}
	{
		JsonArray *context = json_node_get_array(node);
		g_autoptr(GHashTable) seen = g_hash_table_new(g_str_hash, g_str_equal);
		guint i;
		*focused = TRUE;
		for (i = 0; i < json_array_get_length(context); i++) {
			const gchar *pattern;
			JsonNode *element = json_array_get_element(context, i);
			if (json_node_get_value_type(element) != G_TYPE_STRING)
				goto invalid_context;
			pattern = json_node_get_string(element);
			if (pattern == NULL || *pattern == '\0' || !g_hash_table_add(seen, (gpointer)pattern))
				goto invalid_context;
			if (ls_path_valid(pattern))
				g_hash_table_add(exact, g_strdup(pattern));
			g_ptr_array_add(globs, g_strdup(pattern));
		}
	}
	return TRUE;
invalid_context:
	g_set_error_literal(error, LS_ERROR, LS_ERROR_INVALID,
		"options.context must contain 1..4096 unique nonempty glob strings");
	return FALSE;
}

static gboolean
path_inlined(const gchar *path, gboolean binary, gboolean focused, GHashTable *exact,
	GPtrArray *globs){
	guint i;
	if (!focused)
		return !binary;
	if (binary) {
		if (g_hash_table_contains(exact, path))
			return TRUE;
		for (i = 0; i < globs->len; i++) {
			g_autoptr(GPatternSpec) spec =
				g_pattern_spec_new((const gchar *)g_ptr_array_index(globs, i));
			if (g_pattern_spec_match_string(spec, path))
				return TRUE;
		}
		return FALSE;
	}
	if (is_contract_path(path) || g_hash_table_contains(exact, path))
		return TRUE;
	for (i = 0; i < globs->len; i++) {
		g_autoptr(GPatternSpec) spec =
			g_pattern_spec_new((const gchar *)g_ptr_array_index(globs, i));
		if (g_pattern_spec_match_string(spec, path))
			return TRUE;
	}
	return FALSE;
}

gchar *
ls_context_prompt_bundle(LsContext *self, JsonObject *step, GError **error){
	g_autoptr(JsonBuilder) builder = json_builder_new();
	g_autoptr(JsonNode) root = NULL;
	g_autoptr(GList) keys =
		g_list_sort(g_hash_table_get_keys(self->files), (GCompareFunc)g_strcmp0);
	g_autoptr(GHashTable) exact = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	g_autoptr(GPtrArray) globs = g_ptr_array_new_with_free_func(g_free);
	gboolean focused = FALSE;
	GList *item;
	if (!load_selectors(step, exact, globs, &focused, error))
		return NULL;
	json_builder_begin_object(builder);
	json_builder_set_member_name(builder, "manifest");
	json_builder_begin_array(builder);
	for (item = keys; item != NULL; item = item->next) {
		const LsArtifact *artifact =
			(const LsArtifact *)g_hash_table_lookup(self->files, item->data);
		manifest_to_builder(builder, artifact);
	}
	json_builder_end_array(builder);
	json_builder_set_member_name(builder, "files");
	json_builder_begin_array(builder);
	for (item = keys; item != NULL; item = item->next) {
		const LsArtifact *artifact =
			(const LsArtifact *)g_hash_table_lookup(self->files, item->data);
		if (!path_inlined(ls_artifact_get_path(artifact), ls_artifact_is_binary(artifact),
				focused, exact, globs))
			continue;
		artifact_to_builder(builder, artifact);
	}
	json_builder_end_array(builder);
	json_builder_end_object(builder);
	root = json_builder_get_root(builder);
	return ls_json_encode(root);
}

/* Export only into a newly created directory. Generated files cannot introduce
 * symlinks; GFile exclusive creation also refuses pre-existing leaf paths. */
gboolean
ls_context_export(
	LsContext *self, const gchar *directory, GCancellable *cancellable, GError **error
){
	g_autoptr(GFile) root = g_file_new_for_path(directory);
	GHashTableIter iter;
	gpointer key, value;
	if (!g_file_make_directory(root, cancellable, error))
		return FALSE;
	g_hash_table_iter_init(&iter, self->files);
	while (g_hash_table_iter_next(&iter, &key, &value)) {
		g_autoptr(GFile) file = g_file_resolve_relative_path(root, (const gchar *)key);
		g_autoptr(GFile) parent = g_file_get_parent(file);
		g_autoptr(GFileOutputStream) stream = NULL;
		g_autoptr(GError) local_error = NULL;
		gsize size;
		const guint8 *data = ls_artifact_get_data((const LsArtifact *)value, &size);
		if (!g_file_make_directory_with_parents(parent, cancellable, &local_error) &&
			!g_error_matches(local_error, G_IO_ERROR, G_IO_ERROR_EXISTS)) {
			g_propagate_error(error, g_steal_pointer(&local_error));
			return FALSE;
		}
		/* Published files are public web content: the process umask decides who
		 * may read them, so a web server running as another user can serve them. */
		stream = g_file_create(file, G_FILE_CREATE_NONE, cancellable, error);
		if (stream == NULL ||
			!g_output_stream_write_all(
				G_OUTPUT_STREAM(stream), data, size, NULL, cancellable, error
			) ||
			!g_output_stream_close(G_OUTPUT_STREAM(stream), cancellable, error))
			return FALSE;
		if (ls_artifact_get_executable((const LsArtifact *)value)) {
			g_autoptr(GFileInfo) info = g_file_query_info(file, G_FILE_ATTRIBUTE_UNIX_MODE,
				G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, cancellable, error);
			guint32 mode;
			if (info == NULL)
				return FALSE;
			mode = g_file_info_get_attribute_uint32(info, G_FILE_ATTRIBUTE_UNIX_MODE) & 0777;
			/* Execute follows read, so the umask governs both. */
			if (!g_file_set_attribute_uint32(file, G_FILE_ATTRIBUTE_UNIX_MODE, mode | ((mode & 0444) >> 2),
				G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, cancellable, error))
				return FALSE;
		}
	}
	return TRUE;
}
const LsArtifact *
ls_context_lookup(LsContext *self, const gchar *path){
	return (const LsArtifact *)g_hash_table_lookup(self->files, path);
}

/* Decode a snapshot archive into @archive with the same path, uniqueness and
 * size rules as published files. */
static gboolean
load_archive(JsonNode *node, GHashTable *archive, GError **error){
	JsonArray *entries;
	gsize total = 0;
	guint i;
	if (!JSON_NODE_HOLDS_ARRAY(node) || json_array_get_length(json_node_get_array(node)) > 4096)
		goto invalid;
	entries = json_node_get_array(node);
	for (i = 0; i < json_array_get_length(entries); i++) {
		JsonNode *element = json_array_get_element(entries, i);
		LsArtifact *artifact;
		gsize size = 0;
		const gchar *path;
		if (!JSON_NODE_HOLDS_OBJECT(element))
			goto invalid;
		path = ls_string(json_node_get_object(element), "path");
		if (path == NULL || !ls_path_valid(path) || ls_string(json_node_get_object(element), "content") == NULL ||
			g_hash_table_contains(archive, path))
			goto invalid;
		artifact = artifact_from_object(json_node_get_object(element), NULL, error);
		if (artifact == NULL)
			return FALSE;
		(void)ls_artifact_get_data(artifact, &size);
		total += size;
		g_hash_table_insert(archive, g_strdup(path), artifact);
		if (total > LS_MAX_PROJECT) {
			g_set_error_literal(error, LS_ERROR, LS_ERROR_LIMIT, "Archive exceeds project size limit");
			return FALSE;
		}
	}
	return TRUE;
invalid:
	g_set_error_literal(error, LS_ERROR, LS_ERROR_INVALID,
		"Snapshot archive must be a list of unique {path, content} files with safe paths");
	return FALSE;
}

/* Replace a complete snapshot atomically, retaining the context's run mode.
 * The archive travels in snapshots only; generated responses cannot set it. */
gboolean
ls_context_restore(LsContext *self, const gchar *snapshot, GError **error){
	g_autoptr(LsContext) candidate = ls_context_new();
	g_autoptr(JsonNode) parsed = NULL;
	g_autofree gchar *files_only = NULL;
	GHashTable *previous;
	JsonNode *archive;
	parsed = snapshot != NULL ? ls_json_parse(snapshot, error) : NULL;
	if (parsed == NULL) {
		if (error != NULL && *error == NULL)
			g_set_error_literal(error, LS_ERROR, LS_ERROR_INVALID, "Snapshot is not JSON");
		return FALSE;
	}
	if (!JSON_NODE_HOLDS_OBJECT(parsed))
		return ls_context_apply(candidate, snapshot, error);
	archive = json_object_get_member(json_node_get_object(parsed), "archive");
	if (archive != NULL) {
		if (!load_archive(archive, candidate->archive, error))
			return FALSE;
		json_object_remove_member(json_node_get_object(parsed), "archive");
	}
	files_only = ls_json_encode(parsed);
	if (!ls_context_apply(candidate, files_only, error))
		return FALSE;
	previous = self->files;
	self->files = candidate->files;
	candidate->files = previous;
	previous = self->archive;
	self->archive = candidate->archive;
	candidate->archive = previous;
	return TRUE;
}

gboolean
ls_context_archive(LsContext *self, const gchar *path, GError **error){
	gpointer key = NULL, value = NULL;
	g_return_val_if_fail(LS_IS_CONTEXT(self), FALSE);
	g_return_val_if_fail(path != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);
	if (!g_hash_table_steal_extended(self->files, path, &key, &value)) {
		g_set_error(error, LS_ERROR, LS_ERROR_INVALID, "Cannot archive %s: no such file", path);
		return FALSE;
	}
	g_hash_table_replace(self->archive, key, value);
	return TRUE;
}

const LsArtifact *
ls_context_lookup_archived(LsContext *self, const gchar *path){
	g_return_val_if_fail(LS_IS_CONTEXT(self), NULL);
	g_return_val_if_fail(path != NULL, NULL);
	return g_hash_table_lookup(self->archive, path);
}

/* Open children relative to pinned directory descriptors. Never follow symlinks,
 * even if another process replaces an entry between enumeration and openat(). */
static gboolean
import_directory(LsContext *self, gint directory_fd, const gchar *prefix,
	gsize *total, guint *entries, GCancellable *cancellable, GError **error){
	DIR *directory;
	struct dirent *entry;
	gboolean okay = FALSE;
	directory = fdopendir(directory_fd);
	if (directory == NULL) {
		gint saved_errno = errno;
		(void)close(directory_fd);
		g_set_error(error, G_IO_ERROR, g_io_error_from_errno(saved_errno),
			"Cannot enumerate source '%s': %s", prefix, g_strerror(saved_errno));
		return FALSE;
	}
	for (;;) {
		g_autofree gchar *path = NULL;
		struct stat info;
		gint child_fd;
		g_autoptr(GString) contents = g_string_new(NULL);
		gchar buffer[8192];
		gssize count;
		LsArtifact *artifact;
		gboolean binary;
		if (g_cancellable_set_error_if_cancelled(cancellable, error))
			goto out;
		errno = 0;
		entry = readdir(directory);
		if (entry == NULL) {
			if (errno != 0)
				goto io_error;
			break;
		}
		/* Repository administration and Libresite workspaces are never artifacts. */
		if (g_str_equal(entry->d_name, ".") || g_str_equal(entry->d_name, "..") ||
			g_str_equal(entry->d_name, ".git") || g_str_equal(entry->d_name, ".libresite") ||
			g_str_has_suffix(entry->d_name, ".libresite-work"))
			continue;
		path = *prefix != '\0' ? g_strconcat(prefix, "/", entry->d_name, NULL)
			: g_strdup(entry->d_name);
		if (!ls_path_valid(path)) {
			g_set_error(error, LS_ERROR, LS_ERROR_INVALID, "Unsafe source path: %s", path);
			goto out;
		}
		/* Bound directory-only trees as well as the artifact count and bytes. */
		if (++(*entries) > 8192) {
			g_set_error_literal(error, LS_ERROR, LS_ERROR_LIMIT,
				"Source exceeds 8192 filesystem entries");
			goto out;
		}
		if (fstatat(dirfd(directory), entry->d_name, &info, AT_SYMLINK_NOFOLLOW) != 0)
			goto io_error;
		if (!S_ISDIR(info.st_mode) && !S_ISREG(info.st_mode)) {
			g_set_error(error, LS_ERROR, LS_ERROR_INVALID,
				"Source must contain only regular files and directories: %s", path);
			goto out;
		}
		child_fd = openat(dirfd(directory), entry->d_name,
			O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK |
			(S_ISDIR(info.st_mode) ? O_DIRECTORY : 0));
		if (child_fd < 0)
			goto io_error;
		if (S_ISDIR(info.st_mode)) {
			if (!import_directory(self, child_fd, path, total, entries, cancellable, error))
				goto out;
			continue;
		}
		/* Recheck the opened object, then bound every read rather than trusting size. */
		if (fstat(child_fd, &info) != 0 || !S_ISREG(info.st_mode)) {
			(void)close(child_fd);
			g_set_error(error, LS_ERROR, LS_ERROR_CONFLICT,
				"Source file changed while opening: %s", path);
			goto out;
		}
		for (;;) {
			if (g_cancellable_set_error_if_cancelled(cancellable, error)) {
				(void)close(child_fd);
				goto out;
			}
			count = read(child_fd, buffer, sizeof(buffer));
			if (count < 0 && errno == EINTR)
				continue;
			if (count <= 0)
				break;
			if (contents->len + (gsize)count > LS_MAX_DOCUMENT ||
				*total + (gsize)count > LS_MAX_PROJECT) {
				(void)close(child_fd);
				g_set_error(error, LS_ERROR, LS_ERROR_LIMIT,
					"Source exceeds file or project limits: %s", path);
				goto out;
			}
			*total += (gsize)count;
			g_string_append_len(contents, buffer, count);
		}
		if (count < 0) {
			gint saved_errno = errno;
			(void)close(child_fd);
			errno = saved_errno;
			goto io_error;
		}
		(void)close(child_fd);
		binary = memchr(contents->str, 0, contents->len) != NULL ||
			!g_utf8_validate(contents->str, (gssize)contents->len, NULL);
		artifact = ls_artifact_new_bytes(path, (const guint8 *)contents->str, contents->len,
			binary, (info.st_mode & 0111) != 0, error);
		if (artifact == NULL)
			goto out;
		g_hash_table_insert(self->files, g_strdup(path), artifact);
		if (g_hash_table_size(self->files) > 4096) {
			g_set_error_literal(error, LS_ERROR, LS_ERROR_LIMIT, "Source exceeds 4096 files");
			goto out;
		}
	}
	okay = TRUE;
	goto out;
io_error:
	g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
		"Cannot read source directory '%s': %s", prefix, g_strerror(errno));
out:
	(void)closedir(directory);
	return okay;
}

/* Build privately; callers never receive a partial import on failure. */
LsContext *
ls_context_import(const gchar *directory, GCancellable *cancellable, GError **error){
	g_autoptr(LsContext) self = ls_context_new();
	gsize total = 0;
	guint entries = 0;
	gint directory_fd;
	if (directory == NULL || *directory == '\0') {
		g_set_error_literal(error, LS_ERROR, LS_ERROR_INVALID, "A source directory is required");
		return NULL;
	}
	if (g_cancellable_set_error_if_cancelled(cancellable, error))
		return NULL;
	directory_fd = open(directory, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
	if (directory_fd < 0) {
		g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
			"Cannot open source directory '%s': %s", directory, g_strerror(errno));
		return NULL;
	}
	if (!import_directory(self, directory_fd, "", &total, &entries, cancellable, error))
		return NULL;
	if (g_hash_table_size(self->files) == 0) {
		g_set_error_literal(error, LS_ERROR, LS_ERROR_INVALID, "Source project contains no files");
		return NULL;
	}
	self->revision = TRUE;
	return (LsContext *)g_steal_pointer(&self);
}

gboolean
ls_context_is_revision(LsContext *self){
	return self->revision;
}
