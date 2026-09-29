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
 * ls_context_import:
 * @directory: source directory containing regular files
 * @cancellable: (nullable): cancellation token
 * @error: return location for an error
 *
 * Ignores .git, .libresite and *.libresite-work entries. Rejects symlinks,
 * special files, invalid paths and projects exceeding limits. UTF-8 text and
 * binary assets are imported; JSON snapshots encode binary files as base64.
 * Returns: (transfer full) (nullable): complete revision context
 */
LsContext *ls_context_import(const gchar *directory, GCancellable *cancellable, GError **error);
/**
 * ls_context_is_revision:
 * @self: artifact store
 * Returns: whether this context was imported for revision
 */
gboolean ls_context_is_revision(LsContext *self);
/**
 * ls_context_restore:
 * @self: artifact store
 * @snapshot: complete JSON snapshot
 * @error: return location for an error
 * Returns: whether the snapshot replaced all files atomically
 */
gboolean ls_context_restore(LsContext *self, const gchar *snapshot, GError **error);
/**
 * ls_context_apply:
 * @self: an artifact store
 * @response: JSON object with files, optional delete/rename and optional summary
 * @error: return location for an error
 *
 * Validates the whole bundle before changing the store. Existing files are replaced;
 * omitted files survive. Deletions must name unique existing files. Renames move
 * unique existing files onto absent paths. Operations cannot overlap except that a
 * write may recreate a renamed-from path. File/directory conflicts are checked
 * after all operations.
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
 * ls_context_prompt_bundle:
 * @self: an artifact store
 * @step: (nullable): generation step; options.files and options.context focus inlining
 * @error: return location for an error
 *
 * Builds the model-facing project payload: a complete path manifest plus selected
 * UTF-8 file contents. Binary files stay in the manifest unless options.context
 * names them. A focused step inlines its files list, contract docs and context
 * globs instead of every text file.
 * Returns: (transfer full) (nullable): JSON object with manifest and files
 */
gchar *ls_context_prompt_bundle(LsContext *self, JsonObject *step, GError **error);
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
 * ls_context_archive:
 * @self: a context
 * @path: a stored file path
 * @error: return location for an error
 *
 * Moves a file out of the published tree into the context archive. Archived
 * files are never exported, but they travel in snapshots, checkpoints and the
 * published run record, so build sources such as page outlines survive a
 * build without being served. Archiving a path again replaces its content.
 * Only native steps archive; generated responses cannot write the archive.
 * Returns: whether the file was archived; %FALSE when @path is not stored
 */
gboolean ls_context_archive(LsContext *self, const gchar *path, GError **error);
/**
 * ls_context_lookup_archived:
 * @self: a context
 * @path: an archived file path
 *
 * Returns: (transfer none) (nullable): the archived file, or %NULL
 */
const LsArtifact *ls_context_lookup_archived(LsContext *self, const gchar *path);
/**
 * ls_context_lookup:
 * @self: an artifact store
 * @path: relative file path
 * Returns: (transfer none) (nullable): stored artifact
 */
const LsArtifact *ls_context_lookup(LsContext *self, const gchar *path);
G_END_DECLS
