/* SPDX-License-Identifier: AGPL-3.0-or-later */
#pragma once
#include "core/ls-recipe.h"
#include "model/ls-artifact.h"
G_BEGIN_DECLS
#define LS_TYPE_CONTEXT (ls_context_get_type())
G_DECLARE_FINAL_TYPE(LsContext, ls_context, LS, CONTEXT, GObject)
/**
 * ls_context_new:
 * Returns: (transfer full): empty artifact store
 */
LsContext *ls_context_new(void);
/**
 * ls_context_apply:
 * @self: an artifact store
 * @response: JSON object with a files array and optional summary
 * @error: return location for an error
 *
 * Validates the whole bundle before changing the store. Existing files are replaced.
 * Returns: whether every artifact was accepted
 */
gboolean ls_context_apply(LsContext *self, const gchar *response, GError **error);
/**
 * ls_context_snapshot:
 * @self: an artifact store
 * Returns: (transfer full): canonical JSON file bundle, sorted by path
 */
gchar *ls_context_snapshot(LsContext *self);
/**
 * ls_context_export:
 * @self: an artifact store
 * @directory: a new, nonexistent output directory
 * @cancellable: (nullable): cancellation token
 * @error: return location for an error
 * Returns: whether the project was written successfully
 */
gboolean ls_context_export(
	LsContext *self, const gchar *directory, GCancellable *cancellable, GError **error
);
/**
 * ls_context_lookup:
 * @self: an artifact store
 * @path: relative file path
 * Returns: (transfer none) (nullable): stored artifact
 */
const LsArtifact *ls_context_lookup(LsContext *self, const gchar *path);
G_END_DECLS
