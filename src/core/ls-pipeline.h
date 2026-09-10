/* SPDX-License-Identifier: AGPL-3.0-or-later */
#pragma once
#include "steps/ls-step.h"
G_BEGIN_DECLS
#define LS_TYPE_PIPELINE (ls_pipeline_get_type())
G_DECLARE_FINAL_TYPE(LsPipeline, ls_pipeline, LS, PIPELINE, GObject)
/**
 * ls_pipeline_new:
 * @provider: transport used by generation steps
 * @prompts: prompt catalog
 * Returns: (transfer full): pipeline with built-in modules registered
 */
LsPipeline *ls_pipeline_new(LsProvider *provider, LsPrompts *prompts);
/**
 * ls_pipeline_register:
 * @self: pipeline
 * @name: recipe type name
 * @step_type: concrete #LsStep subclass with a zero-argument constructor
 * @error: return location for an error
 * Returns: whether registration succeeded; duplicates are errors
 */
gboolean ls_pipeline_register(LsPipeline *self, const gchar *name, GType step_type, GError **error);
/**
 * ls_pipeline_validate:
 * @self: pipeline
 * @recipe: recipe
 * @error: return location for an error
 * Returns: whether types, styles and prompts resolve without calling AI
 */
gboolean ls_pipeline_validate(LsPipeline *self, LsRecipe *recipe, GError **error);
/**
 * ls_pipeline_run:
 * @self: pipeline; use only from one thread at a time
 * @recipe: recipe
 * @output: nonexistent destination directory
 * @resume: reuse an existing matching checkpoint
 * @cancellable: (nullable): cancellation token
 * @error: return location for an error
 *
 * Emits step-started (id, index), step-completed (id, index), artifact-written
 * (#LsArtifact), and completed (output). Indices are zero-based. Failed work is
 * retained in OUTPUT.libresite-work; a successful run publishes OUTPUT.
 * Returns: whether the complete project was published
 */
gboolean ls_pipeline_run(
	LsPipeline *self,
	LsRecipe *recipe,
	const gchar *output,
	gboolean resume,
	GCancellable *cancellable,
	GError **error
);
G_END_DECLS
