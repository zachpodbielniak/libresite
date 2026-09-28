/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "steps/ls-step.h"

GResource *ls_resources_get_resource(void);
struct _LsScaffoldStep {
	LsStep parent_instance;
};
G_DEFINE_TYPE(LsScaffoldStep, ls_scaffold_step, LS_TYPE_STEP)

/* Only the static profile has a build contract we can emit without guessing
 * the generated application's framework. Reject misspellings before AI work. */
gboolean
ls_scaffold_step_check_options(JsonObject *options, GError **error){
	if (options == NULL)
		return TRUE;
	if (!ls_object_keys(options, "|profile|", error))
		return FALSE;
	if (json_object_has_member(options, "profile") &&
		g_strcmp0(ls_string(options, "profile"), "static") != 0) {
		g_set_error_literal(error, LS_ERROR, LS_ERROR_INVALID,
			"scaffold profile must be 'static'");
		return FALSE;
	}
	return TRUE;
}

/* Render missing files as one bundle. The context boundary checks all path,
 * size and directory conflicts transactionally; seeds always retain ownership. */
gchar *
ls_scaffold_step_render(JsonObject *options, LsContext *context, GError **error){
	/* Visitor-facing utility pages only: a static site ships no build, test or
	 * agent files, so no model tokens are spent maintaining them either. */
	const gchar *paths[] = {"404.html", "privacy.html", "terms.html"};
	g_autoptr(JsonBuilder) builder = json_builder_new();
	g_autoptr(JsonNode) root = NULL;
	guint i;
	if (!ls_scaffold_step_check_options(options, error))
		return NULL;
	json_builder_begin_object(builder);
	json_builder_set_member_name(builder, "files");
	json_builder_begin_array(builder);
	for (i = 0; i < G_N_ELEMENTS(paths); i++) {
		g_autofree gchar *resource = NULL;
		g_autoptr(GBytes) bytes = NULL;
		g_autofree gchar *content = NULL;
		gsize size;
		gconstpointer data;
		if (ls_context_lookup(context, paths[i]) != NULL)
			continue;
		resource = g_strconcat("/org/libresite/scaffold/", paths[i], NULL);
		bytes = g_resource_lookup_data(ls_resources_get_resource(), resource,
			G_RESOURCE_LOOKUP_FLAGS_NONE, error);
		if (bytes == NULL)
			return NULL;
		data = g_bytes_get_data(bytes, &size);
		content = g_strndup(data, size);
		json_builder_begin_object(builder);
		json_builder_set_member_name(builder, "path");
		json_builder_add_string_value(builder, paths[i]);
		json_builder_set_member_name(builder, "content");
		json_builder_add_string_value(builder, content);
		json_builder_end_object(builder);
	}
	json_builder_end_array(builder);
	json_builder_end_object(builder);
	root = json_builder_get_root(builder);
	return ls_json_encode(root);
}

/* Native steps use the same atomic acceptance and cancellation boundary as
 * providers, but never call the transport or execute their emitted sources. */
static gboolean
execute(LsStep *self, LsRecipe *recipe, JsonObject *config, LsPrompts *prompts,
	LsContext *context, LsProvider *provider, GCancellable *cancellable, GError **error){
	JsonNode *node = json_object_get_member(config, "options");
	JsonObject *options = node != NULL && JSON_NODE_HOLDS_OBJECT(node)
		? json_node_get_object(node) : NULL;
	g_autofree gchar *bundle = NULL;
	(void)self;
	(void)prompts;
	(void)provider;
	if (g_cancellable_set_error_if_cancelled(cancellable, error))
		return FALSE;
	bundle = ls_scaffold_step_render_recipe(recipe, options, context, error);
	if (bundle == NULL || g_cancellable_set_error_if_cancelled(cancellable, error))
		return FALSE;
	return ls_context_apply(context, bundle, error);
}
static void
ls_scaffold_step_class_init(LsScaffoldStepClass *klass){
	LS_STEP_CLASS(klass)->execute = execute;
}
static void
ls_scaffold_step_init(LsScaffoldStep *self){
	(void)self;
}
/* A sealed native snapshot replaces embedded templates, including their path
 * inventory. Filter only existing paths, preserving the normal seed contract. */
gchar *
ls_scaffold_step_render_recipe(
	LsRecipe *recipe, JsonObject *options, LsContext *context, GError **error
){
	const gchar *source = ls_recipe_get_scaffold_source(recipe);
	g_autoptr(JsonNode) node = NULL;
	JsonArray *files;
	guint i;
	if (source == NULL)
		return ls_scaffold_step_render(options, context, error);
	if (!ls_scaffold_step_check_options(options, error))
		return NULL;
	node = ls_json_parse(source, error);
	if (node == NULL)
		return NULL;
	/* The recipe setter already validated this immutable bundle. Iterate
	 * backwards so removing entries preserves the archived order. */
	files = json_object_get_array_member(json_node_get_object(node), "files");
	for (i = json_array_get_length(files); i > 0; i--) {
		JsonObject *file = json_array_get_object_element(files, i - 1);
		if (ls_context_lookup(context, ls_string(file, "path")) != NULL)
			json_array_remove_element(files, i - 1);
	}
	return ls_json_encode(node);
}
