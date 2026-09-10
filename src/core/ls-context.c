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
	gboolean revision;
};
G_DEFINE_TYPE(LsContext, ls_context, G_TYPE_OBJECT)
/* All artifact ownership lives in the hash table. */
static void
ls_context_finalize(GObject *object){
	g_hash_table_unref(LS_CONTEXT(object)->files);
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
}
LsContext *
ls_context_new(void){
	return g_object_new(LS_TYPE_CONTEXT, NULL);
}
/* Build a candidate map and detect file/directory conflicts before committing. */
gboolean
ls_context_apply(LsContext *self, const gchar *response, GError **error){
	g_autoptr(JsonNode) parsed = NULL;
	g_autoptr(GHashTable) candidate =
		g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify)ls_artifact_free);
	g_autoptr(GHashTable) seen = g_hash_table_new(g_str_hash, g_str_equal);
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
	if (!ls_object_keys(object, "|files|summary|delete|", error))
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
	/* Deletions and replacements share one transaction and one duplicate set.
	 * Removing a directory means explicitly naming each stored file beneath it. */
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
	for (i = 0; i < json_array_get_length(files); i++) {
		LsArtifact *artifact;
		const LsArtifact *previous;
		JsonNode *mode;
		gboolean executable;
		const gchar *path;
		node = json_array_get_element(files, i);
		if (!JSON_NODE_HOLDS_OBJECT(node))
			goto invalid;
		object = json_node_get_object(node);
		if (!ls_object_keys(object, "|path|content|executable|", error))
			return FALSE;
		path = ls_string(object, "path");
		previous = path != NULL ? ls_context_lookup(self, path) : NULL;
		mode = json_object_get_member(object, "executable");
		if (mode != NULL && json_node_get_value_type(mode) != G_TYPE_BOOLEAN)
			goto invalid;
		executable = mode != NULL ? json_node_get_boolean(mode)
			: previous != NULL && ls_artifact_get_executable(previous);
		artifact = ls_artifact_new_full(path, ls_string(object, "content"), executable, error);
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
		total += strlen(ls_artifact_get_content((const LsArtifact *)value));
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
		"Expected JSON {files: [{path: string, content: string}], delete?: [path], summary?: string}"
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
		json_builder_begin_object(builder);
		json_builder_set_member_name(builder, "path");
		json_builder_add_string_value(builder, ls_artifact_get_path(artifact));
		json_builder_set_member_name(builder, "content");
		json_builder_add_string_value(builder, ls_artifact_get_content(artifact));
		if (ls_artifact_get_executable(artifact)) {
			json_builder_set_member_name(builder, "executable");
			json_builder_add_boolean_value(builder, TRUE);
		}
		json_builder_end_object(builder);
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
		const gchar *content = ls_artifact_get_content((const LsArtifact *)value);
		if (!g_file_make_directory_with_parents(parent, cancellable, &local_error) &&
			!g_error_matches(local_error, G_IO_ERROR, G_IO_ERROR_EXISTS)) {
			g_propagate_error(error, g_steal_pointer(&local_error));
			return FALSE;
		}
		stream = g_file_create(file, G_FILE_CREATE_PRIVATE, cancellable, error);
		if (stream == NULL ||
			!g_output_stream_write_all(
				G_OUTPUT_STREAM(stream), content, strlen(content), NULL, cancellable, error
			) ||
			!g_output_stream_close(G_OUTPUT_STREAM(stream), cancellable, error))
			return FALSE;
		if (ls_artifact_get_executable((const LsArtifact *)value) &&
			!g_file_set_attribute_uint32(file, G_FILE_ATTRIBUTE_UNIX_MODE, 0700,
				G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, cancellable, error))
			return FALSE;
	}
	return TRUE;
}
const LsArtifact *
ls_context_lookup(LsContext *self, const gchar *path){
	return (const LsArtifact *)g_hash_table_lookup(self->files, path);
}

/* Replace a complete snapshot atomically, retaining the context's run mode. */
gboolean
ls_context_restore(LsContext *self, const gchar *snapshot, GError **error){
	g_autoptr(LsContext) candidate = ls_context_new();
	GHashTable *previous;
	if (!ls_context_apply(candidate, snapshot, error))
		return FALSE;
	previous = self->files;
	self->files = candidate->files;
	candidate->files = previous;
	return TRUE;
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
				"Source must contain only regular text files and directories: %s", path);
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
				*total + (gsize)count > LS_MAX_PROJECT ||
				memchr(buffer, 0, (gsize)count) != NULL) {
				(void)close(child_fd);
				g_set_error(error, LS_ERROR, LS_ERROR_LIMIT,
					"Source is binary or exceeds text/project limits: %s", path);
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
		artifact = ls_artifact_new_full(path, contents->str, (info.st_mode & 0111) != 0, error);
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
