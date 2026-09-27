/* SPDX-License-Identifier: AGPL-3.0-or-later */
#pragma once
#include "interfaces/ls-provider.h"
G_BEGIN_DECLS
#define LS_TYPE_AI_PROVIDER (ls_ai_provider_get_type())
G_DECLARE_FINAL_TYPE(LsAiProvider, ls_ai_provider, LS, AI_PROVIDER, GObject)
/**
 * ls_ai_provider_new:
 *
 * Claude Code calls run answer-only: the response-schema tool is the only
 * tool, with no MCP servers, hooks, skills or user configuration, and no
 * permission bypass. The other CLI adapters keep their bypass because pinned
 * ai-glib cannot remove their tools.
 *
 * Returns: (transfer full): ai-glib CLI transport
 */
LsAiProvider *ls_ai_provider_new(void);
/**
 * ls_ai_provider_set_debug:
 * @self: ai-glib transport
 * @enabled: whether to persist provider sessions and print diagnostics
 *
 * Configure before generation; do not change this while a call is running.
 * Sessions are stored by the provider CLI and may contain project contents.
 */
void ls_ai_provider_set_debug(LsAiProvider *self, gboolean enabled);
G_END_DECLS
