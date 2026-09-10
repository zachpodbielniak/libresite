/* SPDX-License-Identifier: AGPL-3.0-or-later */
#pragma once
#include "interfaces/ls-provider.h"
G_BEGIN_DECLS
#define LS_TYPE_AI_PROVIDER (ls_ai_provider_get_type())
G_DECLARE_FINAL_TYPE(LsAiProvider, ls_ai_provider, LS, AI_PROVIDER, GObject)
/**
 * ls_ai_provider_new:
 * Returns: (transfer full): ai-glib CLI transport
 */
LsAiProvider *ls_ai_provider_new(void);
G_END_DECLS
