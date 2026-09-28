/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "steps/ls-step.h"
struct _LsPruneStep {
	LsStep parent_instance;
};
G_DEFINE_TYPE(LsPruneStep, ls_prune_step, LS_TYPE_STEP)

/* Paths are required so an empty or misspelled step cannot silently prune
 * nothing; unknown keys fail preflight before any provider call. */
gboolean
ls_prune_step_check_options(JsonObject *options, GError **error){
	g_autoptr(GHashTable) seen = NULL;
	JsonNode *node;
	JsonArray *paths;
	guint i;
	if (options == NULL || !ls_object_keys(options, "|paths|", NULL) ||
		(node = json_object_get_member(options, "paths")) == NULL ||
		!JSON_NODE_HOLDS_ARRAY(node) || json_array_get_length(json_node_get_array(node)) == 0 ||
		json_array_get_length(json_node_get_array(node)) > 4096)
		goto invalid;
	paths = json_node_get_array(node);
	seen = g_hash_table_new(g_str_hash, g_str_equal);
	for (i = 0; i < json_array_get_length(paths); i++) {
		const gchar *path;
		node = json_array_get_element(paths, i);
		path = json_node_get_value_type(node) == G_TYPE_STRING ? json_node_get_string(node) : NULL;
		if (path == NULL || *path == '\0' || !ls_path_valid(path) ||
			!g_hash_table_add(seen, (gpointer)path))
			goto invalid;
	}
	return TRUE;
invalid:
	g_set_error_literal(error, LS_ERROR, LS_ERROR_INVALID,
		"prune requires options.paths with 1..4096 unique safe relative file paths and no other keys");
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
	g_autofree gchar *bundle = NULL;
	JsonArray *paths;
	guint i;
	(void)self;
	(void)recipe;
	(void)prompts;
	(void)provider;
	if (g_cancellable_set_error_if_cancelled(cancellable, error) ||
		!ls_prune_step_check_options(options, error))
		return FALSE;
	paths = json_object_get_array_member(options, "paths");
	builder = json_builder_new();
	json_builder_begin_object(builder);
	json_builder_set_member_name(builder, "files");
	json_builder_begin_array(builder);
	json_builder_end_array(builder);
	json_builder_set_member_name(builder, "delete");
	json_builder_begin_array(builder);
	for (i = 0; i < json_array_get_length(paths); i++) {
		const gchar *path = json_array_get_string_element(paths, i);
		if (ls_context_lookup(context, path) != NULL)
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
