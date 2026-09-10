/* SPDX-License-Identifier: AGPL-3.0-or-later */
#pragma once
#include <gio/gio.h>
#include <json-glib/json-glib.h>
G_BEGIN_DECLS
#define LS_TYPE_PROVIDER (ls_provider_get_type())
G_DECLARE_INTERFACE(LsProvider, ls_provider, LS, PROVIDER, GObject)
/**
 * LsProviderInterface:
 * @parent_iface: parent interface
 * @generate: return a UTF-8 file-bundle response; must honor cancellation
 * @reserved: ABI expansion slots
 *
 * A transport boundary: implementations may use an AI CLI, an API or fixtures.
 */
struct _LsProviderInterface {
	GTypeInterface parent_iface;
	gchar *(*generate)(
		LsProvider *self,
		JsonObject *settings,
		const gchar *system,
		const gchar *prompt,
		GCancellable *cancellable,
		GError **error
	);
	gpointer reserved[8];
};
/**
 * ls_provider_generate:
 * @self: a provider
 * @settings: validated per-step settings
 * @system: system instructions
 * @prompt: task and project context
 * @cancellable: (nullable): cancellation token
 * @error: return location for an error
 * Returns: (transfer full) (nullable): provider response
 */
gchar *ls_provider_generate(
	LsProvider *self,
	JsonObject *settings,
	const gchar *system,
	const gchar *prompt,
	GCancellable *cancellable,
	GError **error
);
G_END_DECLS
