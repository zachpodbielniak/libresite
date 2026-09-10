/* SPDX-License-Identifier: AGPL-3.0-or-later */
#pragma once
#include <glib-object.h>
G_BEGIN_DECLS
/**
 * LsArtifact:
 *
 * Immutable, owned UTF-8 project file. Copy to retain a signal argument.
 */
typedef struct _LsArtifact LsArtifact;
#define LS_TYPE_ARTIFACT (ls_artifact_get_type())
GType ls_artifact_get_type(void) G_GNUC_CONST;
/**
 * ls_artifact_new:
 * @path: safe relative output path
 * @content: UTF-8 file contents
 * @error: return location for an error
 * Returns: (transfer full) (nullable): a validated artifact
 */
LsArtifact *ls_artifact_new(const gchar *path, const gchar *content, GError **error);
/**
 * ls_artifact_new_full:
 * @path: safe relative output path
 * @content: UTF-8 file contents
 * @executable: whether to export the file with owner execute permission
 * @error: return location for an error
 * Returns: (transfer full) (nullable): a validated immutable artifact
 */
LsArtifact *ls_artifact_new_full(const gchar *path, const gchar *content,
	gboolean executable, GError **error);
/**
 * ls_artifact_get_executable:
 * @self: an artifact
 * Returns: whether this file is executable
 */
gboolean ls_artifact_get_executable(const LsArtifact *self);
/**
 * ls_artifact_copy:
 * @self: an artifact
 * Returns: (transfer full): an independent copy
 */
LsArtifact *ls_artifact_copy(const LsArtifact *self);
/**
 * ls_artifact_free:
 * @self: (nullable): an artifact
 */
void ls_artifact_free(LsArtifact *self);
/**
 * ls_artifact_get_path:
 * @self: an artifact
 * Returns: (transfer none): relative path
 */
const gchar *ls_artifact_get_path(const LsArtifact *self);
/**
 * ls_artifact_get_content:
 * @self: an artifact
 * Returns: (transfer none): file contents
 */
const gchar *ls_artifact_get_content(const LsArtifact *self);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(LsArtifact, ls_artifact_free)
G_END_DECLS
