/* SPDX-License-Identifier: AGPL-3.0-or-later */
#pragma once
#include "core/ls-recipe.h"
#include "core/ls-context.h"
G_BEGIN_DECLS
#define LS_TYPE_PROMPTS (ls_prompts_get_type())
G_DECLARE_FINAL_TYPE(LsPrompts, ls_prompts, LS, PROMPTS, GObject)
/**
 * ls_prompts_new:
 * @path: (nullable): replacement catalog, or embedded defaults
 * @error: return location for an error
 * Returns: (transfer full) (nullable): validated prompt catalog
 */
LsPrompts *ls_prompts_new(const gchar *path, GError **error);
/**
 * ls_prompts_set_max_bytes:
 * @self: a catalog
 * @max_bytes: system plus task byte limit, or zero for unlimited (the default)
 *
 * Configure before rendering or starting a pipeline.
 * This admission limit does not change prompt contents or checkpoint identity.
 */
void ls_prompts_set_max_bytes(LsPrompts *self, gsize max_bytes);
/**
 * ls_prompts_get_system:
 * @self: a catalog
 * Returns: (transfer none): shared system prompt
 */
const gchar *ls_prompts_get_system(LsPrompts *self);
/**
 * ls_prompts_render:
 * @self: a catalog
 * @recipe: recipe and site brief
 * @step: step configuration
 * @context: previously generated files
 * @error: return location for an error
 * Returns: (transfer full) (nullable): complete task prompt
 */
gchar *ls_prompts_render(
	LsPrompts *self, LsRecipe *recipe, JsonObject *step, LsContext *context, GError **error
);
/**
 * ls_prompts_get_digest:
 * @self: a catalog
 * Returns: (transfer none): SHA-256 of the catalog
 */
const gchar *ls_prompts_get_digest(LsPrompts *self);
G_END_DECLS
