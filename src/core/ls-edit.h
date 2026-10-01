/* SPDX-License-Identifier: AGPL-3.0-or-later */
#pragma once
#include "core/ls-prompts.h"
#include "interfaces/ls-provider.h"
G_BEGIN_DECLS
#define LS_TYPE_EDITOR (ls_editor_get_type())
G_DECLARE_FINAL_TYPE(LsEditor, ls_editor, LS, EDITOR, GObject)
/**
 * ls_editor_new:
 * @provider: (transfer none): transport for the edit call
 * @prompts: (transfer none): catalog with spec_system and an edit stage
 * @settings: (transfer none): provider settings (provider, model, timeout,
 *   retries, max_tokens), as ls_recipe_resolve_ai() returns them
 *
 * An editor changes a delivered composed site from a plain-language request.
 * Its "progress" signal (one string) is emitted on the calling thread.
 * Returns: (transfer full): a new editor
 */
LsEditor *ls_editor_new(LsProvider *provider, LsPrompts *prompts, JsonObject *settings);
/**
 * ls_editor_apply:
 * @self: an editor
 * @project: a published composed site: a Git working tree holding the
 *   site's files and its .libresite/run.json record with the plan and page
 *   specifications
 * @request: the change, in plain words
 * @summary: (out) (optional) (transfer full): the one-line version note
 * @commit: (out) (optional) (transfer full): the new commit's hash
 * @cancellable: (nullable): cancellation token
 * @error: return location for an error
 *
 * Asks the model for only the plan members and page specifications the
 * request touches, checks the merged plan and every page, recomposes the
 * whole site with the current component library, and only then writes the
 * changed files, updates the run record and commits a new version on the
 * site's own history. The site must have no uncommitted changes and its
 * files must still match the run record. Any failure leaves the files, the
 * record and the history as they were.
 * Returns: whether a new version was committed
 */
gboolean ls_editor_apply(LsEditor *self, const gchar *project, const gchar *request, gchar **summary,
	gchar **commit, GCancellable *cancellable, GError **error);
G_END_DECLS
