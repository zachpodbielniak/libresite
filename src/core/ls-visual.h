/* SPDX-License-Identifier: AGPL-3.0-or-later */
#ifndef LS_VISUAL_H
#define LS_VISUAL_H
#include <json-glib/json-glib.h>
#include "core/ls-context.h"
/* Internal validated JSON contracts; all arguments borrowed, no mutation
 * except the explicitly named apply operations. */
gboolean ls_visual_check_media(JsonObject *owner, GError **error);
gboolean ls_visual_check_plan(JsonObject *plan, GError **error);
gboolean ls_visual_check_assets(JsonObject *owner, LsContext *context, GError **error);
void ls_visual_apply_plan(JsonObject *plan);
void ls_visual_apply_page(JsonObject *plan, JsonObject *page);
void ls_visual_view(JsonObject *plan, JsonObject *view, guint index);
#endif
