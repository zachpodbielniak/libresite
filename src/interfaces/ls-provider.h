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
/**
 * ls_provider_dispatch:
 * @self: a provider
 * @settings: validated per-step settings
 * @system: system instructions
 * @prompt: task and project context
 * @bytes_in: (out): system plus prompt UTF-8 bytes passed to the transport
 * @bytes_out: (out): UTF-8 bytes returned, zero when nothing was returned
 * @cancellable: (nullable): cancellation token
 * @error: return location for an error
 *
 * Performs the same call and error normalization as ls_provider_generate()
 * without emitting #LsProvider::usage, so parallel steps may call it from
 * worker threads. Report the measured bytes afterwards with
 * ls_provider_record_usage() on the thread that owns the step. The provider
 * implementation must support concurrent generate calls when a step
 * dispatches in parallel; see docs/components.org.
 * Returns: (transfer full) (nullable): provider response
 */
gchar *ls_provider_dispatch(
	LsProvider *self,
	JsonObject *settings,
	const gchar *system,
	const gchar *prompt,
	gint64 *bytes_in,
	gint64 *bytes_out,
	GCancellable *cancellable,
	GError **error
);
/**
 * ls_provider_record_usage:
 * @self: a provider
 * @settings: the settings passed to the measured dispatch
 * @bytes_in: bytes reported by ls_provider_dispatch()
 * @bytes_out: bytes reported by ls_provider_dispatch()
 *
 * Emits #LsProvider::usage synchronously on the calling thread.
 */
void ls_provider_record_usage(LsProvider *self, JsonObject *settings, gint64 bytes_in, gint64 bytes_out);
G_END_DECLS
