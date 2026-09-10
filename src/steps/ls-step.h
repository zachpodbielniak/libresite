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
#define LS_TYPE_GENERATE_STEP (ls_generate_step_get_type())
G_DECLARE_FINAL_TYPE(LsGenerateStep, ls_generate_step, LS, GENERATE_STEP, LsStep)
#define LS_TYPE_VALIDATE_STEP (ls_validate_step_get_type())
G_DECLARE_FINAL_TYPE(LsValidateStep, ls_validate_step, LS, VALIDATE_STEP, LsStep)
/**
 * ls_validate_step_check_options:
 * @options: (nullable) (transfer none): native validation options
 * @error: return location for an error
 *
 * Checks required paths and optional literal-content and byte-size predicates
 * without accessing artifacts or invoking a provider. Does not mutate @options.
 *
 * Returns: whether the options have a valid schema
 */
gboolean ls_validate_step_check_options(JsonObject *options, GError **error);
G_END_DECLS
