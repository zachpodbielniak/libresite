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
 * ls_prompts_new_from_yaml:
 * @yaml: complete UTF-8 catalog text
 * @error: return location for an error
 * Returns: (transfer full) (nullable): validated catalog owning a copy of @yaml
 */
LsPrompts *ls_prompts_new_from_yaml(const gchar *yaml, GError **error);
/**
 * ls_prompts_get_source:
 * @self: a catalog
 * Returns: (transfer none): exact original YAML bytes used for its digest
 */
const gchar *ls_prompts_get_source(LsPrompts *self);
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
 * ls_prompts_get_spec_system:
 * @self: a catalog
 * Returns: (transfer none) (nullable): shared system prompt for component
 *   steps (plan, pages), or NULL when the catalog has no spec_system
 */
const gchar *ls_prompts_get_spec_system(LsPrompts *self);
/**
 * ls_prompts_render_stage:
 * @self: a catalog with spec_system
 * @stage: stages entry holding the stage contract
 * @reference: (nullable): reference text such as the component catalog
 * @task: JSON task data for this call
 * @error: return location for an error
 *
 * Builds a prompt that needs no recipe, such as an edit of a delivered
 * site: stage contract, reference and task, in that order.
 * Returns: (transfer full) (nullable): prompt text
 */
gchar *ls_prompts_render_stage(LsPrompts *self, const gchar *stage, const gchar *reference, const gchar *task,
	GError **error);
/**
 * ls_prompts_render_spec:
 * @self: a catalog with spec_system
 * @recipe: recipe and site brief
 * @step: step configuration; its instructions are included verbatim
 * @stage: stages entry holding the stage contract
 * @reference: (nullable): reference text such as the component catalog
 * @task: JSON task data for this call
 * @error: return location for an error
 *
 * Builds a compact prompt for a component step: stage contract, design
 * direction, site data, step instructions, reference and task, in that
 * order. Site text and task data are serialized as data, never templates.
 * Returns: (transfer full) (nullable): prompt text
 */
gchar *ls_prompts_render_spec(LsPrompts *self, LsRecipe *recipe, JsonObject *step,
	const gchar *stage, const gchar *reference, const gchar *task, GError **error);
/**
 * ls_prompts_get_digest:
 * @self: a catalog
 * Returns: (transfer none): SHA-256 of the catalog
 */
const gchar *ls_prompts_get_digest(LsPrompts *self);
G_END_DECLS
