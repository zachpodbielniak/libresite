/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "core/ls-context.h"
#include <string.h>
struct _LsContext {
	GObject parent_instance;
	GHashTable *files;
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
	if (!ls_object_keys(object, "|files|summary|", error))
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
	for (i = 0; i < json_array_get_length(files); i++) {
		LsArtifact *artifact;
		const gchar *path;
		node = json_array_get_element(files, i);
		if (!JSON_NODE_HOLDS_OBJECT(node))
			goto invalid;
		object = json_node_get_object(node);
		if (!ls_object_keys(object, "|path|content|", error))
			return FALSE;
		path = ls_string(object, "path");
		artifact = ls_artifact_new(path, ls_string(object, "content"), error);
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
		"Expected JSON {files: [{path: string, content: string}], summary?: string}"
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
	}
	return TRUE;
}
const LsArtifact *
ls_context_lookup(LsContext *self, const gchar *path){
	return (const LsArtifact *)g_hash_table_lookup(self->files, path);
}
