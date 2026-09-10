/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "interfaces/ls-provider.h"
#include "core/ls-util.h"
G_DEFINE_INTERFACE(LsProvider, ls_provider, G_TYPE_OBJECT)
/* Implementations supply the single transport operation. */
static void
ls_provider_default_init(LsProviderInterface *iface){
	(void)iface;
}
/* Keep cancellation behavior identical for production and injected transports. */
gchar *
ls_provider_generate(
	LsProvider *self,
	JsonObject *settings,
	const gchar *system,
	const gchar *prompt,
	GCancellable *cancellable,
	GError **error
){
	LsProviderInterface *iface;
	g_autofree gchar *response = NULL;
	g_autoptr(GError) local_error = NULL;
	g_return_val_if_fail(LS_IS_PROVIDER(self), NULL);
	if (g_cancellable_set_error_if_cancelled(cancellable, error))
		return NULL;
	iface = LS_PROVIDER_GET_IFACE(self);
	if (iface->generate == NULL) {
		g_set_error_literal(
			error, LS_ERROR, LS_ERROR_PROVIDER, "Provider has no generate implementation"
		);
		return NULL;
	}
	response = iface->generate(self, settings, system, prompt, cancellable, &local_error);
	/* ai-glib adapters may use their own cancellation domain. Normalize at
	 * the interface boundary so all consumers see the same GIO contract. */
	if (g_cancellable_is_cancelled(cancellable)) {
		g_cancellable_set_error_if_cancelled(cancellable, error);
		return NULL;
	}
	/* Isolate a broken implementation's error even when the caller ignores
	 * diagnostics. A response accompanied by an error must never be accepted. */
	if (response == NULL && local_error == NULL)
		g_set_error_literal(&local_error, LS_ERROR, LS_ERROR_PROVIDER,
			"Provider failed without an error");
	if (local_error != NULL) {
		g_propagate_error(error, g_steal_pointer(&local_error));
		return NULL;
	}
	return (gchar *)g_steal_pointer(&response);
}
