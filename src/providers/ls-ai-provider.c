/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "providers/ls-ai-provider.h"
#include "core/ls-util.h"
#include <ai-glib.h>
#include <glib/gstdio.h>
struct _LsAiProvider {
	GObject parent_instance;
};
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
	(void)self;
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
	/* No permission bypass. This directory is a working location, not a sandbox;
	 * the account's CLI policy still controls any native tool access. */
	directory = g_dir_make_tmp("libresite-provider-XXXXXX", error);
	if (directory == NULL)
		return NULL;
	if (AI_IS_CODEX_CLI_CLIENT(client))
		ai_codex_cli_client_set_sandbox(AI_CODEX_CLI_CLIENT(client), "read-only");
	ai_cli_client_set_working_directory(client, directory);
	ai_cli_client_set_session_persistence(client, FALSE);
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
	response = ai_cli_client_chat_sync(client, &messages, cancellable, error);
	/* Only remove an empty provider directory; never recursively delete files
	 * created by third-party CLI tools. A nonempty directory remains inspectable. */
	(void)g_rmdir(directory);
	if (response == NULL)
		return NULL;
	if (ai_response_get_stop_reason(response) == AI_STOP_REASON_ERROR) {
		g_set_error_literal(
			error, LS_ERROR, LS_ERROR_PROVIDER, "Provider reported an unsuccessful completion"
		);
		return NULL;
	}
	text = ai_response_get_text(response);
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
