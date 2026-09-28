/* SPDX-License-Identifier: AGPL-3.0-or-later */
#pragma once
#include "core/ls-prompts.h"
#include "interfaces/ls-provider.h"
G_BEGIN_DECLS
#define LS_TYPE_STEP (ls_step_get_type())
G_DECLARE_DERIVABLE_TYPE(LsStep, ls_step, LS, STEP, GObject)
/**
 * LsStepClass:
 * @parent_class: parent class
 * @execute: synchronous step implementation
 * @reserved: ABI expansion slots
 */
struct _LsStepClass {
	GObjectClass parent_class;
	gboolean (*execute)(
		LsStep *self,
		LsRecipe *recipe,
		JsonObject *config,
		LsPrompts *prompts,
		LsContext *context,
		LsProvider *provider,
		GCancellable *cancellable,
		GError **error
	);
	gpointer reserved[8];
};
/**
 * ls_step_execute:
 * @self: a step
 * @recipe: recipe
 * @config: step configuration
 * @prompts: catalog
 * @context: artifact store
 * @provider: transport
 * @cancellable: (nullable): cancellation token
 * @error: return location for an error
 * Returns: whether the step succeeded
 */
gboolean ls_step_execute(
	LsStep *self,
	LsRecipe *recipe,
	JsonObject *config,
	LsPrompts *prompts,
	LsContext *context,
	LsProvider *provider,
	GCancellable *cancellable,
	GError **error
);
/**
 * ls_step_dup_usage:
 * @self: a step
 *
 * Returns an independent record of the last execution, including failed calls.
 * See docs/usage.org for field units and accounting limits.
 * Returns: (transfer full) (nullable): usage object, or NULL before execution
 */
JsonObject *ls_step_dup_usage(LsStep *self);
#define LS_TYPE_GENERATE_STEP (ls_generate_step_get_type())
G_DECLARE_FINAL_TYPE(LsGenerateStep, ls_generate_step, LS, GENERATE_STEP, LsStep)
#define LS_TYPE_VALIDATE_STEP (ls_validate_step_get_type())
G_DECLARE_FINAL_TYPE(LsValidateStep, ls_validate_step, LS, VALIDATE_STEP, LsStep)
/**
 * ls_validate_step_check_options:
 * @options: (nullable) (transfer none): native validation options
 * @error: return location for an error
 *
 * Checks required paths and optional literal-content, byte-size and ShellCheck
 * predicates without accessing artifacts or invoking a provider. When a rule
 * sets shellcheck, also requires ShellCheck on PATH so a missing host tool
 * fails before paid generation. Does not mutate @options.
 *
 * Returns: whether the options have a valid schema
 */
gboolean ls_validate_step_check_options(JsonObject *options, GError **error);
#define LS_TYPE_SCAFFOLD_STEP (ls_scaffold_step_get_type())
G_DECLARE_FINAL_TYPE(LsScaffoldStep, ls_scaffold_step, LS, SCAFFOLD_STEP, LsStep)
/**
 * ls_scaffold_step_check_options:
 * @options: (nullable) (transfer none): options; profile defaults to static
 * @error: return location for an error
 * Returns: whether native scaffold options are valid
 */
gboolean ls_scaffold_step_check_options(JsonObject *options, GError **error);
/**
 * ls_scaffold_step_render:
 * @options: (nullable) (transfer none): native scaffold options
 * @context: (transfer none): existing project artifacts, left unchanged
 * @error: return location for an error
 *
 * Renders embedded templates for absent paths only. Apply the returned bundle
 * with ls_context_apply() to validate conflicts and commit it atomically.
 * Returns: (transfer full) (nullable): a JSON file bundle, or NULL on error
 */
gchar *ls_scaffold_step_render(JsonObject *options, LsContext *context, GError **error);
/**
 * ls_scaffold_step_render_recipe:
 * @recipe: recipe with an optional archived native snapshot
 * @options: (nullable): static scaffold settings
 * @context: existing artifacts; matching paths are preserved
 * @error: return location for an error
 * Returns: (transfer full) (nullable): missing archived or default native files
 */
gchar *ls_scaffold_step_render_recipe(
	LsRecipe *recipe, JsonObject *options, LsContext *context, GError **error
);
#define LS_TYPE_GATE_STEP (ls_gate_step_get_type())
G_DECLARE_FINAL_TYPE(LsGateStep, ls_gate_step, LS, GATE_STEP, LsStep)
/**
 * ls_gate_step_check_options:
 * @options: (nullable) (transfer none): gate options
 * @error: return location for an error
 * Returns: whether native gate options are valid
 */
gboolean ls_gate_step_check_options(JsonObject *options, GError **error);
/**
 * ls_gate_step_preview:
 * @options: (nullable): gate options
 * @error: return location for an error
 * Returns: (transfer full) (nullable): human-readable gate description
 */
gchar *ls_gate_step_preview(JsonObject *options, GError **error);
#define LS_TYPE_PRUNE_STEP (ls_prune_step_get_type())
G_DECLARE_FINAL_TYPE(LsPruneStep, ls_prune_step, LS, PRUNE_STEP, LsStep)
/**
 * ls_prune_step_check_options:
 * @options: (nullable) (transfer none): prune options
 * @error: return location for an error
 *
 * Requires options.paths (1..4096 unique safe relative file paths),
 * options.prefixes (unique safe directories ending in "/", removing every
 * file below them) or both, and no other keys. Does not access artifacts or
 * mutate @options.
 *
 * Returns: whether native prune options are valid
 */
gboolean ls_prune_step_check_options(JsonObject *options, GError **error);
#define LS_TYPE_PLAN_STEP (ls_plan_step_get_type())
G_DECLARE_FINAL_TYPE(LsPlanStep, ls_plan_step, LS, PLAN_STEP, LsStep)
#define LS_TYPE_PAGES_STEP (ls_pages_step_get_type())
G_DECLARE_FINAL_TYPE(LsPagesStep, ls_pages_step, LS, PAGES_STEP, LsStep)
/**
 * ls_pages_step_check_options:
 * @options: (nullable) (transfer none): pages step options
 * @error: return location for an error
 *
 * Accepts parallel (1..16, default 6), mode (write or review, default write)
 * and base_url (an absolute https:// site address). Does not access
 * artifacts, a provider or the plan.
 * Returns: whether the options are valid
 */
gboolean ls_pages_step_check_options(JsonObject *options, GError **error);
/**
 * ls_spec_step_check:
 * @prompts: catalog
 * @recipe: recipe containing @config
 * @config: a plan or pages step
 * @error: return location for an error
 *
 * Preflight for component steps: a spec_system prompt and the needed stage
 * contracts exist, pages options are valid and a plan step precedes every
 * pages step. Never calls a provider.
 * Returns: whether the step can run
 */
gboolean ls_spec_step_check(LsPrompts *prompts, LsRecipe *recipe, JsonObject *config, GError **error);
/**
 * ls_spec_step_preview:
 * @prompts: catalog
 * @recipe: recipe containing @config
 * @config: a plan or pages step
 * @error: return location for an error
 *
 * The plan step's complete prompt, or for a pages step the stage contract
 * and component reference each page writer receives with its own task.
 * Returns: (transfer full) (nullable): preview text
 */
gchar *ls_spec_step_preview(LsPrompts *prompts, LsRecipe *recipe, JsonObject *config, GError **error);
G_END_DECLS
