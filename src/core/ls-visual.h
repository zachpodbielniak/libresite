/* SPDX-License-Identifier: AGPL-3.0-or-later */
#ifndef LS_VISUAL_H
#define LS_VISUAL_H
#include <json-glib/json-glib.h>
#include "core/ls-context.h"
G_BEGIN_DECLS
/**
 * ls_visual_check_media:
 * @owner: (nullable): a site or plan object that may hold media
 * @error: return location for an error
 *
 * Checks an optional media block: hero, portrait and detail name photos on
 * @owner, proof is optional, kind and source are present, clips are local
 * assets/ MP4 or WebM paths, and facts belong only to a proof project.
 * A NULL owner, or an owner with no media member, is valid.
 * Returns: whether @owner has no media or a valid media block
 */
gboolean ls_visual_check_media(JsonObject *owner, GError **error);
/**
 * ls_visual_check_plan:
 * @plan: (nullable): a site plan
 * @error: return location for an error
 *
 * Checks plan.visual when plan.media is present: world, layout, headings
 * and copy, a portrait crop for a full opening, and no self-talk in the
 * words. A NULL plan, or a plan with neither media nor visual, is valid.
 * Returns: whether the visual sequence is absent or valid
 */
gboolean ls_visual_check_plan(JsonObject *plan, GError **error);
/**
 * ls_visual_check_assets:
 * @owner: (nullable): a site object
 * @context: (nullable): seeded artifacts; NULL skips the byte check
 * @error: return location for an error
 *
 * ls_visual_check_media() plus the seeded clip bytes: a binary MP4 or WebM
 * of at most 2 MiB with a container signature.
 * Returns: whether the media assets are absent or acceptable
 */
gboolean ls_visual_check_assets(JsonObject *owner, LsContext *context, GError **error);
/**
 * ls_visual_apply_plan:
 * @plan: (nullable): a plan with a valid visual sequence
 *
 * Paints the dealt world's colours and type onto the plan and retires the
 * look axes and signature the sequence owns. A plan that fails
 * ls_visual_check_plan() is left alone.
 */
void ls_visual_apply_plan(JsonObject *plan);
/**
 * ls_visual_apply_page:
 * @plan: (nullable): a plan with a valid visual sequence
 * @page: (nullable): the home page specification, changed in place
 *
 * Rebuilds the home opening as the outcome, craft and proof beats, then
 * keeps the writer's later sections. Other pages are left alone.
 */
void ls_visual_apply_page(JsonObject *plan, JsonObject *page);
/**
 * ls_visual_view:
 * @plan: (nullable): a plan with media
 * @view: the section view being rendered
 * @index: which beat is being rendered (0 outcome, 2 proof)
 *
 * Adds the project credit, the film paths on the opening beat, and the
 * proof facts on the proof beat. Stock and illustration media stay
 * uncaptioned.
 */
void ls_visual_view(JsonObject *plan, JsonObject *view, guint index);
G_END_DECLS
#endif
