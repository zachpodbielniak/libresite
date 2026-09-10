/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "steps/ls-step.h"
typedef struct {
	JsonObject *usage;
} LsStepPrivate;
G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE(LsStep, ls_step, G_TYPE_OBJECT)

/* Usage belongs to one execution and is copied for consumers. */
static void
finalize(GObject *object){
	LsStepPrivate *priv = ls_step_get_instance_private(LS_STEP(object));
	g_clear_pointer(&priv->usage, json_object_unref);
	G_OBJECT_CLASS(ls_step_parent_class)->finalize(object);
}

JsonObject *
ls_step_dup_usage(LsStep *self){
	LsStepPrivate *priv = ls_step_get_instance_private(self);
	g_autoptr(JsonObject) copy = json_object_new();
	g_autoptr(GList) members = NULL;
	GList *item;
	if (priv->usage == NULL)
		return NULL;
	/* All record fields are scalars; copy each value independently. */
	members = json_object_get_members(priv->usage);
	for (item = members; item != NULL; item = item->next) {
		const gchar *key = (const gchar *)item->data;
		json_object_set_member(copy, key, json_node_copy(json_object_get_member(priv->usage, key)));
	}
	return (JsonObject *)g_steal_pointer(&copy);
}

/* All provider calls in this synchronous execution contribute, including retries. */
static void
record_call(LsProvider *provider, const gchar *name, const gchar *model,
	gint64 bytes_in, gint64 bytes_out, JsonObject *usage){
	gint64 attempts = json_object_get_int_member(usage, "attempts");
	const gchar *resolved = model != NULL ? model : "(provider default)";
	(void)provider;
	if (name == NULL)
		name = "(unknown)";
	json_object_set_string_member(usage, "provider", attempts == 0 ||
		g_strcmp0(ls_string(usage, "provider"), name) == 0 ? name : "(mixed)");
	json_object_set_string_member(usage, "model", attempts == 0 ||
		g_strcmp0(ls_string(usage, "model"), resolved) == 0 ? resolved : "(mixed)");
	json_object_set_int_member(usage, "attempts", attempts + 1);
	json_object_set_int_member(usage, "retries", attempts);
	json_object_set_int_member(usage, "bytes_in",
		json_object_get_int_member(usage, "bytes_in") + bytes_in);
	json_object_set_int_member(usage, "bytes_out",
		json_object_get_int_member(usage, "bytes_out") + bytes_out);
}
static void
ls_step_class_init(LsStepClass *klass){
	G_OBJECT_CLASS(klass)->finalize = finalize;
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
	LsStepPrivate *priv = ls_step_get_instance_private(self);
	gint64 started = g_get_monotonic_time();
	gulong handler;
	g_clear_pointer(&priv->usage, json_object_unref);
	priv->usage = json_object_new();
	json_object_set_string_member(priv->usage, "step",
		config != NULL && ls_string(config, "id") != NULL ? ls_string(config, "id") : "(anonymous)");
	json_object_set_string_member(priv->usage, "provider", "native");
	json_object_set_string_member(priv->usage, "model", "(none)");
	json_object_set_boolean_member(priv->usage, "success", FALSE);
	json_object_set_int_member(priv->usage, "attempts", 0);
	json_object_set_int_member(priv->usage, "retries", 0);
	json_object_set_int_member(priv->usage, "bytes_in", 0);
	json_object_set_int_member(priv->usage, "bytes_out", 0);
	json_object_set_int_member(priv->usage, "elapsed_us", 0);
	if (g_cancellable_set_error_if_cancelled(cancellable, error))
		return FALSE;
	if (klass->execute == NULL) {
		g_set_error_literal(
			error, LS_ERROR, LS_ERROR_INVALID, "Step has no execute implementation"
		);
		return FALSE;
	}
	handler = provider != NULL
		? g_signal_connect(provider, "usage", G_CALLBACK(record_call), priv->usage) : 0;
	success = klass->execute(self, recipe, config, prompts, context, provider,
		cancellable, &local_error);
	if (handler != 0)
		g_signal_handler_disconnect(provider, handler);
	json_object_set_int_member(priv->usage, "elapsed_us", g_get_monotonic_time() - started);
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
	json_object_set_boolean_member(priv->usage, "success", success);
	return success;
}
