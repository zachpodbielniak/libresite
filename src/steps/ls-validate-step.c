/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "steps/ls-step.h"
struct _LsValidateStep {
	LsStep parent_instance;
};
G_DEFINE_TYPE(LsValidateStep, ls_validate_step, LS_TYPE_STEP)
/* Deterministic output contract, distinct from AI review. Full browser or
 * compiler verification belongs to the generated project's documented tests. */
static gboolean
execute(
	LsStep *self,
	LsRecipe *recipe,
	JsonObject *config,
	LsPrompts *prompts,
	LsContext *context,
	LsProvider *provider,
	GCancellable *cancellable,
	GError **error
){
	JsonObject *options = json_object_has_member(config, "options")
							  ? json_object_get_object_member(config, "options")
							  : NULL;
	JsonNode *node = options != NULL ? json_object_get_member(options, "required") : NULL;
	JsonArray *required =
		node != NULL && JSON_NODE_HOLDS_ARRAY(node) ? json_node_get_array(node) : NULL;
	guint i;
	(void)self;
	(void)recipe;
	(void)prompts;
	(void)provider;
	(void)cancellable;
	if (options == NULL || !ls_object_keys(options, "|required|", error)) {
		if (options == NULL)
			g_set_error_literal(
				error, LS_ERROR, LS_ERROR_INVALID, "validate requires options.required"
			);
		return FALSE;
	}
	if (required == NULL || json_array_get_length(required) == 0) {
		g_set_error_literal(
			error, LS_ERROR, LS_ERROR_INVALID, "options.required must be a nonempty array of paths"
		);
		return FALSE;
	}
	for (i = 0; i < json_array_get_length(required); i++) {
		const LsArtifact *artifact;
		const gchar *path;
		node = json_array_get_element(required, i);
		path = json_node_get_value_type(node) == G_TYPE_STRING ? json_node_get_string(node) : NULL;
		if (!ls_path_valid(path)) {
			g_set_error_literal(
				error, LS_ERROR, LS_ERROR_INVALID, "Invalid required artifact path"
			);
			return FALSE;
		}
		artifact = ls_context_lookup(context, path);
		if (artifact == NULL || *ls_artifact_get_content(artifact) == '\0') {
			g_set_error(
				error, LS_ERROR, LS_ERROR_INVALID, "Required artifact is missing or empty: %s", path
			);
			return FALSE;
		}
	}
	return TRUE;
}
static void
ls_validate_step_class_init(LsValidateStepClass *klass){
	LS_STEP_CLASS(klass)->execute = execute;
}
static void
ls_validate_step_init(LsValidateStep *self){
	(void)self;
}
