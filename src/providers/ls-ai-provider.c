/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "providers/ls-ai-provider.h"
#include "core/ls-util.h"
#include <ai-glib.h>
#include <glib/gstdio.h>
#include <stdarg.h>
#include <string.h>
GResource *ls_resources_get_resource(void);
struct _LsAiProvider {
	GObject parent_instance;
	gboolean debug;
};
static void debug_log(const gchar *format, ...) G_GNUC_PRINTF(1, 2);
/* Emit metadata only: never dump prompts, generated text or the environment. */
static void
debug_log(const gchar *format, ...){
	g_autoptr(GDateTime) now = g_date_time_new_now_local();
	g_autofree gchar *timestamp = g_date_time_format(now, "%Y-%m-%dT%H:%M:%S%z");
	g_autofree gchar *message = NULL;
	va_list args;
	va_start(args, format);
	message = g_strdup_vprintf(format, args);
	va_end(args);
	g_printerr("[%s] debug: %s\n", timestamp, message);
}
static void provider_iface_init(LsProviderInterface *iface);
G_DEFINE_TYPE_WITH_CODE(
	LsAiProvider,
	ls_ai_provider,
	G_TYPE_OBJECT,
	G_IMPLEMENT_INTERFACE(LS_TYPE_PROVIDER, provider_iface_init)
)
/* ai-glib owns CLI argv construction, authentication and response parsing. */
static gchar *
generate(
	LsProvider *self,
	JsonObject *settings,
	const gchar *system,
	const gchar *prompt,
	GCancellable *cancellable,
	GError **error
){
	g_autoptr(AiCliClient) client = NULL;
	g_autoptr(AiMessage) message = NULL;
	g_autoptr(AiResponse) response = NULL;
	g_autofree gchar *directory = NULL;
	g_autofree gchar *text = NULL;
	const gchar *provider = ls_string(settings, "provider");
	const gchar *model = ls_string(settings, "model");
	const gchar *effort = ls_string(settings, "effort");
	GList messages = {NULL, NULL, NULL};
	GParamSpec *schema_property;
	gboolean debug = LS_AI_PROVIDER(self)->debug;
	gint64 started;
	if (g_strcmp0(provider, "codex") == 0)
		client = AI_CLI_CLIENT(ai_codex_cli_client_new());
	else if (g_strcmp0(provider, "claude-code") == 0)
		client = AI_CLI_CLIENT(ai_claude_code_client_new());
	else if (g_strcmp0(provider, "cursor") == 0)
		client = AI_CLI_CLIENT(ai_cursor_client_new());
	else if (g_strcmp0(provider, "grok-build") == 0)
		client = AI_CLI_CLIENT(ai_grok_build_client_new());
	else if (g_strcmp0(provider, "antigravity") == 0)
		client = AI_CLI_CLIENT(ai_antigravity_client_new());
	else {
		g_set_error(
			error,
			LS_ERROR,
			LS_ERROR_PROVIDER,
			"Unsupported provider '%s'",
			provider != NULL ? provider : "(missing)"
		);
		return NULL;
	}
	/* Claude Code and Antigravity expose this capability in pinned ai-glib.
	 * Constrain the invocation; never strip narration from provider output. */
	schema_property = g_object_class_find_property(G_OBJECT_GET_CLASS(client), "json-schema");
	if (schema_property != NULL && G_PARAM_SPEC_VALUE_TYPE(schema_property) == G_TYPE_STRING &&
		(schema_property->flags & G_PARAM_WRITABLE) != 0) {
		g_autoptr(GBytes) bytes = g_resource_lookup_data(ls_resources_get_resource(),
			"/org/libresite/file-bundle.schema.json", 0, error);
		g_autofree gchar *schema = NULL;
		if (bytes == NULL)
			return NULL;
		schema = g_strndup(g_bytes_get_data(bytes, NULL), g_bytes_get_size(bytes));
		g_object_set(client, "json-schema", schema, NULL);
	}
	/* Unattended generation cannot answer TTY or stdin approval prompts.
	 * Codex exec rejects --ask-for-approval; read-only plus never-ask still
	 * waits for sandbox-escalation approval and never returns. Bypass is
	 * required for every CLI adapter. skip-permissions conflicts with an
	 * explicit restrictive Codex sandbox, so do not set one. */
	directory = g_dir_make_tmp("libresite-provider-XXXXXX", error);
	if (directory == NULL)
		return NULL;
	g_object_set(client, "skip-permissions", TRUE, NULL);
	ai_cli_client_set_working_directory(client, directory);
	ai_cli_client_set_session_persistence(client, debug);
	ai_cli_client_set_process_timeout_ms(
		client, (gint)json_object_get_int_member(settings, "timeout") * 1000
	);
	ai_cli_client_set_max_tokens(client, (gint)json_object_get_int_member(settings, "max_tokens"));
	ai_cli_client_set_system_prompt(client, system);
	if (model != NULL)
		ai_cli_client_set_model(client, model);
	if (effort != NULL)
		ai_cli_client_set_effort_level(client, effort);
	message = ai_message_new_user(prompt);
	messages.data = message;
	if (debug)
		debug_log("provider=%s model=%s effort=%s timeout=%" G_GINT64_FORMAT
			"s max_tokens=%" G_GINT64_FORMAT " system_bytes=%" G_GSIZE_FORMAT
			" prompt_bytes=%" G_GSIZE_FORMAT " persistence=enabled cwd=%s",
			provider, model != NULL ? model : "(provider default)",
			effort != NULL ? effort : "(provider default)",
			json_object_get_int_member(settings, "timeout"),
			json_object_get_int_member(settings, "max_tokens"),
			strlen(system), strlen(prompt), directory);
	started = g_get_monotonic_time();
	if (debug && g_str_equal(provider, "codex"))
		debug_log("Codex max_tokens is prompt guidance, not an enforced CLI limit; "
			"session files may stay unchanged while response tokens stream");
	response = ai_cli_client_chat_sync(client, &messages, cancellable, error);
	if (debug) {
		const gchar *session = ai_cli_client_get_session_id(client);
		debug_log("provider=%s elapsed_ms=%" G_GINT64_FORMAT
			" result=%s session=%s error_domain=%s error_code=%d",
			provider, (g_get_monotonic_time() - started) / 1000,
			response != NULL ? "response" : "failed",
			session != NULL ? session : "(unavailable; locate session by cwd)",
			error != NULL && *error != NULL ? g_quark_to_string((*error)->domain) : "none",
			error != NULL && *error != NULL ? (*error)->code : 0);
	}
	/* Only remove an empty provider directory; never recursively delete files
	 * created by third-party CLI tools. A nonempty directory remains inspectable. */
	if (!debug)
		(void)g_rmdir(directory);
	if (response == NULL) {
		if (error != NULL && g_error_matches(*error, AI_ERROR, AI_ERROR_TIMEOUT))
			g_prefix_error(error,
				"Total provider deadline exceeded (not an inactivity timeout); "
				"resume with a larger --timeout SECONDS: ");
		return NULL;
	}
	if (ai_response_get_stop_reason(response) == AI_STOP_REASON_ERROR) {
		g_set_error_literal(
			error, LS_ERROR, LS_ERROR_PROVIDER, "Provider reported an unsuccessful completion"
		);
		return NULL;
	}
	text = ai_response_get_text(response);
	if (debug)
		debug_log("provider=%s response_bytes=%" G_GSIZE_FORMAT " stop_reason=%d",
			provider, text != NULL ? strlen(text) : (gsize)0,
			(gint)ai_response_get_stop_reason(response));
	if (text == NULL || *text == '\0') {
		g_set_error_literal(error, LS_ERROR, LS_ERROR_PROVIDER, "Provider returned no text");
		return NULL;
	}
	if (ai_response_get_stop_reason(response) == AI_STOP_REASON_MAX_TOKENS) {
		g_set_error_literal(
			error,
			LS_ERROR,
			LS_ERROR_LIMIT,
			"Provider response was truncated; increase max_tokens or split the step"
		);
		return NULL;
	}
	return (gchar *)g_steal_pointer(&text);
}
static void
provider_iface_init(LsProviderInterface *iface){
	iface->generate = generate;
}
static void
ls_ai_provider_class_init(LsAiProviderClass *klass){
	(void)klass;
}
static void
ls_ai_provider_init(LsAiProvider *self){
	(void)self;
}
LsAiProvider *
ls_ai_provider_new(void){
	return g_object_new(LS_TYPE_AI_PROVIDER, NULL);
}
/* The default remains ephemeral; debug is an explicit per-transport choice. */
void
ls_ai_provider_set_debug(LsAiProvider *self, gboolean enabled){
	g_return_if_fail(LS_IS_AI_PROVIDER(self));
	self->debug = enabled;
}
