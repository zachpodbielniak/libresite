/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "steps/ls-step.h"
#include <string.h>
struct _LsPruneStep {
	LsStep parent_instance;
};
G_DEFINE_TYPE(LsPruneStep, ls_prune_step, LS_TYPE_STEP)

/* Safe relative path list: 1..4096 unique entries. Prefixes must also end
 * with "/" so a prefix always names a directory, never a file stem. */
static gboolean
path_list(JsonObject *options, const gchar *key, gboolean prefixes){
	g_autoptr(GHashTable) seen = g_hash_table_new(g_str_hash, g_str_equal);
	JsonNode *node = json_object_get_member(options, key);
	JsonArray *paths;
	guint i;
	if (node == NULL || !JSON_NODE_HOLDS_ARRAY(node))
		return FALSE;
	paths = json_node_get_array(node);
	if (json_array_get_length(paths) == 0 || json_array_get_length(paths) > 4096)
		return FALSE;
	for (i = 0; i < json_array_get_length(paths); i++) {
		JsonNode *element = json_array_get_element(paths, i);
		const gchar *path = json_node_get_value_type(element) == G_TYPE_STRING ? json_node_get_string(element) : NULL;
		g_autofree gchar *stem = NULL;
		if (path == NULL || *path == '\0' || !g_hash_table_add(seen, (gpointer)path))
			return FALSE;
		if (prefixes) {
			if (!g_str_has_suffix(path, "/") || strlen(path) < 2)
				return FALSE;
			stem = g_strndup(path, strlen(path) - 1);
		}
		if (!ls_path_valid(stem != NULL ? stem : path))
			return FALSE;
	}
	return TRUE;
}

/* A path list or directory prefixes is required so an empty or misspelled step
 * cannot silently prune nothing; unknown keys fail preflight before any call. */
gboolean
ls_prune_step_check_options(JsonObject *options, GError **error){
	gboolean has_paths = options != NULL && json_object_has_member(options, "paths");
	gboolean has_prefixes = options != NULL && json_object_has_member(options, "prefixes");
	if (options == NULL || !ls_object_keys(options, "|paths|prefixes|", NULL) || (!has_paths && !has_prefixes) ||
		(has_paths && !path_list(options, "paths", FALSE)) ||
		(has_prefixes && !path_list(options, "prefixes", TRUE))) {
		g_set_error_literal(error, LS_ERROR, LS_ERROR_INVALID,
			"prune requires options.paths (1..4096 unique safe relative file paths) and/or "
			"options.prefixes (unique safe directories ending in /), and no other keys");
		return FALSE;
	}
	return TRUE;
}

static gboolean
matches_prefix(JsonArray *prefixes, const gchar *path){
	guint i;
	for (i = 0; prefixes != NULL && i < json_array_get_length(prefixes); i++)
		if (g_str_has_prefix(path, json_array_get_string_element(prefixes, i)))
			return TRUE;
	return FALSE;
}

/* Remove listed files through the ordinary transactional delete path. Absent
 * files are skipped, so the step is repeatable on resume and with seeds. */
static gboolean
execute(LsStep *self, LsRecipe *recipe, JsonObject *config, LsPrompts *prompts,
	LsContext *context, LsProvider *provider, GCancellable *cancellable, GError **error){
	JsonNode *node = json_object_get_member(config, "options");
	JsonObject *options = node != NULL && JSON_NODE_HOLDS_OBJECT(node)
		? json_node_get_object(node) : NULL;
	g_autoptr(JsonBuilder) builder = NULL;
	g_autoptr(JsonNode) root = NULL;
	g_autoptr(JsonNode) snapshot = NULL;
	g_autoptr(GHashTable) removed = g_hash_table_new(g_str_hash, g_str_equal);
	g_autofree gchar *bundle = NULL;
	g_autofree gchar *current = NULL;
	JsonArray *paths, *prefixes, *files;
	guint i;
	(void)self;
	(void)recipe;
	(void)prompts;
	(void)provider;
	if (g_cancellable_set_error_if_cancelled(cancellable, error) ||
		!ls_prune_step_check_options(options, error))
		return FALSE;
	paths = json_object_has_member(options, "paths") ? json_object_get_array_member(options, "paths") : NULL;
	prefixes = json_object_has_member(options, "prefixes") ? json_object_get_array_member(options, "prefixes") : NULL;
	current = ls_context_snapshot(context);
	snapshot = ls_json_parse(current, error);
	if (snapshot == NULL)
		return FALSE;
	files = json_object_get_array_member(json_node_get_object(snapshot), "files");
	builder = json_builder_new();
	json_builder_begin_object(builder);
	json_builder_set_member_name(builder, "files");
	json_builder_begin_array(builder);
	json_builder_end_array(builder);
	json_builder_set_member_name(builder, "delete");
	json_builder_begin_array(builder);
	for (i = 0; paths != NULL && i < json_array_get_length(paths); i++) {
		const gchar *path = json_array_get_string_element(paths, i);
		if (ls_context_lookup(context, path) != NULL && g_hash_table_add(removed, (gpointer)path))
			json_builder_add_string_value(builder, path);
	}
	for (i = 0; i < json_array_get_length(files); i++) {
		const gchar *path = ls_string(json_array_get_object_element(files, i), "path");
		if (matches_prefix(prefixes, path) && g_hash_table_add(removed, (gpointer)path))
			json_builder_add_string_value(builder, path);
	}
	json_builder_end_array(builder);
	json_builder_end_object(builder);
	root = json_builder_get_root(builder);
	bundle = ls_json_encode(root);
	if (g_cancellable_set_error_if_cancelled(cancellable, error))
		return FALSE;
	return ls_context_apply(context, bundle, error);
}
static void
ls_prune_step_class_init(LsPruneStepClass *klass){
	LS_STEP_CLASS(klass)->execute = execute;
}
static void
ls_prune_step_init(LsPruneStep *self){
	(void)self;
}
