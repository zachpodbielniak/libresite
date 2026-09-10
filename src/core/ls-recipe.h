/* SPDX-License-Identifier: AGPL-3.0-or-later */
#pragma once
#include "core/ls-util.h"
G_BEGIN_DECLS
typedef struct _LsContext LsContext;
#define LS_TYPE_RECIPE (ls_recipe_get_type())
G_DECLARE_FINAL_TYPE(LsRecipe, ls_recipe, LS, RECIPE, GObject)
/**
 * ls_recipe_new:
 * @yaml: YAML recipe text
 * @error: return location for an error
 * Returns: (transfer full) (nullable): validated recipe
 */
LsRecipe *ls_recipe_new(const gchar *yaml, GError **error);
/**
 * ls_recipe_get_source:
 * @self: a recipe
 * Returns: (transfer none): original YAML text, excluding execution overrides
 */
const gchar *ls_recipe_get_source(LsRecipe *self);
/**
 * ls_recipe_set_scaffold_source:
 * @self: a recipe
 * @bundle: (nullable): archived native JSON files bundle, or NULL for defaults
 * @error: return location for an error
 *
 * Configure before preview or execution. Validates and copies the snapshot
 * without changing original recipe text. Native bytes bind checkpoint identity;
 * callers must preserve them alongside the recipe. Package loading does this.
 * Returns: whether the pin was set; failed validation preserves the old pin
 */
gboolean ls_recipe_set_scaffold_source(LsRecipe *self, const gchar *bundle, GError **error);
/**
 * ls_recipe_get_scaffold_source:
 * @self: a recipe
 * Returns: (transfer none) (nullable): exact archived native bundle, or NULL
 */
const gchar *ls_recipe_get_scaffold_source(LsRecipe *self);
/**
 * ls_recipe_from_form:
 * @json: UTF-8 JSON form with name, site and optional ai/pipeline/seed_artifacts
 * @error: return location for an error
 *
 * Supplies version 1 and a static-site pipeline when omitted. No provider calls.
 * Catalog and module validation still require ls_pipeline_validate().
 * Returns: (transfer full) (nullable): validated recipe independent of input data
 */
LsRecipe *ls_recipe_from_form(const gchar *json, GError **error);
/**
 * ls_recipe_to_yaml:
 * @self: validated recipe
 * @error: return location for an error
 *
 * Preserves string types and exact text, including copy locks. Does not emit
 * execution-only overrides or modify the recipe or its checkpoint identity.
 * Returns: (transfer full) (nullable): YAML text, freed with g_free()
 */
gchar *ls_recipe_to_yaml(LsRecipe *self, GError **error);
/**
 * ls_recipe_create_context:
 * @self: validated recipe
 * @error: return location for an error
 *
 * Create initial artifacts from the optional seed_artifacts sequence. Use only
 * for fresh runs or previews; resume must restore the checkpoint instead.
 *
 * Returns: (transfer full) (nullable): independently owned initial context
 */
LsContext *ls_recipe_create_context(LsRecipe *self, GError **error);
/**
 * ls_recipe_seed_context:
 * @self: validated recipe
 * @context: initial artifact context, optionally imported for revision
 * @error: return location for an error
 *
 * Apply explicit seed artifacts as one transaction, replacing matching paths.
 * Call only before a fresh run or preview; never overlay a resumed checkpoint.
 * Returns: whether every seed artifact was applied
 */
gboolean ls_recipe_seed_context(LsRecipe *self, LsContext *context, GError **error);
/**
 * ls_recipe_set_timeout:
 * @self: recipe
 * @seconds: -1 to use recipe values, 0 for unlimited, otherwise 1..86400 seconds
 *
 * Set an execution-only deadline override before running. The parsed recipe
 * and checkpoint fingerprint remain unchanged. Do not mutate during a run.
 */
void ls_recipe_set_timeout(LsRecipe *self, gint seconds);
/**
 * ls_recipe_set_unlimited:
 * @self: recipe
 * @unlimited: whether to disable provider time and requested output budgets
 *
 * Configure before execution. Takes precedence over timeout overrides and
 * leaves the parsed recipe unchanged for checkpoint compatibility.
 */
void ls_recipe_set_unlimited(LsRecipe *self, gboolean unlimited);
/**
 * ls_recipe_get_root:
 * @self: a recipe
 * Returns: (transfer none): immutable recipe object; do not modify
 */
JsonObject *ls_recipe_get_root(LsRecipe *self);
/**
 * ls_recipe_get_steps:
 * @self: a recipe
 * Returns: (transfer none): ordered step array; do not modify
 */
JsonArray *ls_recipe_get_steps(LsRecipe *self);
/**
 * ls_recipe_resolve_ai:
 * @self: a recipe
 * @step: step configuration
 * Returns: (transfer full): merged defaults and step AI overrides
 */
JsonObject *ls_recipe_resolve_ai(LsRecipe *self, JsonObject *step);
G_END_DECLS
