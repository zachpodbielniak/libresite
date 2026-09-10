/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "steps/ls-step.h"
G_DEFINE_ABSTRACT_TYPE(LsStep, ls_step, G_TYPE_OBJECT)
static void
ls_step_class_init(LsStepClass *klass){
	/**
	 * LsStep::progress:
	 * @self: the emitting step
	 * @message: (transfer none): human-readable attempt or retry status
	 *
	 * Emitted synchronously on the executing thread. Does not imply that
	 * artifacts have been accepted or checkpointed.
	 */
	g_signal_new("progress", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
		0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_STRING);
}
static void
ls_step_init(LsStep *self){
	(void)self;
}
/* Custom steps share the cancellation and missing-vfunc error contract. */
gboolean
ls_step_execute(
	LsStep *self,
	LsRecipe *recipe,
	JsonObject *config,
	LsPrompts *prompts,
	LsContext *context,
	LsProvider *provider,
	GCancellable *cancellable,
	GError **error
){
	LsStepClass *klass = LS_STEP_GET_CLASS(self);
	g_autoptr(GError) local_error = NULL;
	gboolean success;
	if (g_cancellable_set_error_if_cancelled(cancellable, error))
		return FALSE;
	if (klass->execute == NULL) {
		g_set_error_literal(
			error, LS_ERROR, LS_ERROR_INVALID, "Step has no execute implementation"
		);
		return FALSE;
	}
	success = klass->execute(self, recipe, config, prompts, context, provider,
		cancellable, &local_error);
	/* Cancellation wins even if a plugin returned success or a private error
	 * domain. Failure cannot be checkpointed just because diagnostics are NULL. */
	if (g_cancellable_set_error_if_cancelled(cancellable, error))
		return FALSE;
	if (!success && local_error == NULL)
		g_set_error_literal(&local_error, LS_ERROR, LS_ERROR_INVALID,
			"Step failed without an error");
	if (local_error != NULL) {
		g_propagate_error(error, g_steal_pointer(&local_error));
		return FALSE;
	}
	return success;
}
