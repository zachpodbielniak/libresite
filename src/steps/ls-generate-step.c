/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "steps/ls-step.h"
struct _LsGenerateStep {
	LsStep parent_instance;
};
G_DEFINE_TYPE(LsGenerateStep, ls_generate_step, LS_TYPE_STEP)
/* Validate a proposed project against immediately following native checks.
 * Earlier planning stages are allowed to produce an incomplete project. */
static gboolean
check_acceptance(
	LsRecipe *recipe,
	JsonObject *config,
	LsPrompts *prompts,
	LsContext *candidate,
	LsProvider *provider,
	GCancellable *cancellable,
	GError **error
){
	JsonArray *steps = ls_recipe_get_steps(recipe);
	gboolean found = FALSE;
	guint i;
	for (i = 0; i < json_array_get_length(steps); i++) {
		JsonObject *next = json_array_get_object_element(steps, i);
		g_autoptr(LsStep) validator = NULL;
		if (!found) {
			found = g_strcmp0(ls_string(next, "id"), ls_string(config, "id")) == 0;
			continue;
		}
		if (!g_str_equal(ls_string(next, "type"), "validate"))
			break;
		validator = g_object_new(LS_TYPE_VALIDATE_STEP, NULL);
		if (!ls_step_execute(validator, recipe, next, prompts, candidate, provider, cancellable, error))
			return FALSE;
	}
	return TRUE;
}
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
	g_autofree gchar *snapshot = ls_context_snapshot(context);
	gint64 retries = json_object_get_int_member(settings, "retries");
	gint64 attempt;
	if (prompt == NULL)
		return FALSE;
	for (attempt = 0; attempt <= retries; attempt++) {
		g_autoptr(GError) local_error = NULL;
		g_autoptr(LsContext) candidate = ls_context_new();
		g_autofree gchar *response = NULL;
		g_autofree gchar *status = g_strdup_printf(
			"attempt %" G_GINT64_FORMAT "/%" G_GINT64_FORMAT
			"; provider %s; timeout %" G_GINT64_FORMAT "s%s",
			attempt + 1, retries + 1, ls_string(settings, "provider"),
			json_object_get_int_member(settings, "timeout"),
			json_object_get_int_member(settings, "timeout") == 0 ? " (unlimited)" : "");
		g_signal_emit_by_name(self, "progress", status);
		response = ls_provider_generate(
			provider,
			settings,
			ls_prompts_get_system(prompts),
			retry_prompt != NULL ? retry_prompt : prompt,
			cancellable,
			&local_error
		);
		/* Trial output remains isolated until structural and downstream checks
		 * pass. Failed attempts must not contaminate the next retry or checkpoint. */
		if (response != NULL && ls_context_apply(candidate, snapshot, &local_error) &&
			ls_context_apply(candidate, response, &local_error) &&
			check_acceptance(recipe, config, prompts, candidate, provider, cancellable, &local_error)) {
			if (g_cancellable_set_error_if_cancelled(cancellable, error))
				return FALSE;
			return ls_context_apply(context, response, error);
		}
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
		/* Surface the actual rejection before starting another paid request. */
		g_free(status);
		status = g_strdup_printf("attempt %" G_GINT64_FORMAT
			" failed: %.512s; retrying", attempt + 1, local_error->message);
		g_signal_emit_by_name(self, "progress", status);
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
