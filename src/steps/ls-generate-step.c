/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "steps/ls-step.h"
struct _LsGenerateStep {
	LsStep parent_instance;
};
G_DEFINE_TYPE(LsGenerateStep, ls_generate_step, LS_TYPE_STEP)
/* Retry boundedly with corrective feedback. A rejected bundle never modifies
 * the project, so retries are safe even after malformed model output. */
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
	g_autoptr(JsonObject) settings = ls_recipe_resolve_ai(recipe, config);
	g_autofree gchar *prompt = ls_prompts_render(prompts, recipe, config, context, error);
	g_autofree gchar *retry_prompt = NULL;
	gint64 retries = json_object_get_int_member(settings, "retries");
	gint64 attempt;
	(void)self;
	if (prompt == NULL)
		return FALSE;
	for (attempt = 0; attempt <= retries; attempt++) {
		g_autoptr(GError) local_error = NULL;
		g_autofree gchar *response = ls_provider_generate(
			provider,
			settings,
			ls_prompts_get_system(prompts),
			retry_prompt != NULL ? retry_prompt : prompt,
			cancellable,
			&local_error
		);
		if (response != NULL && ls_context_apply(context, response, &local_error))
			return TRUE;
		if (local_error == NULL)
			g_set_error_literal(
				&local_error, LS_ERROR, LS_ERROR_PROVIDER, "Provider failed without an error"
			);
		if (attempt == retries || g_error_matches(local_error, G_IO_ERROR, G_IO_ERROR_CANCELLED) ||
			g_cancellable_is_cancelled(cancellable)) {
			g_propagate_error(error, g_steal_pointer(&local_error));
			return FALSE;
		}
		g_free(retry_prompt);
		retry_prompt = g_strdup_printf(
			"%s\nPREVIOUS ATTEMPT REJECTED\n%.512s\nReturn a corrected complete JSON file bundle, "
			"without markdown fences.\n",
			prompt,
			local_error->message
		);
	}
	return FALSE;
}
static void
ls_generate_step_class_init(LsGenerateStepClass *klass){
	LS_STEP_CLASS(klass)->execute = execute;
}
static void
ls_generate_step_init(LsGenerateStep *self){
	(void)self;
}
