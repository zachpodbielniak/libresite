/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "model/ls-artifact.h"
#include "core/ls-util.h"
#include <string.h>
struct _LsArtifact {
	gchar *path;
	gchar *content;
	gboolean executable;
};
G_DEFINE_BOXED_TYPE(LsArtifact, ls_artifact, ls_artifact_copy, ls_artifact_free)

/* Own immutable strings so callers cannot change validated paths afterward. */
LsArtifact *
ls_artifact_new(const gchar *path, const gchar *content, GError **error){
	return ls_artifact_new_full(path, content, FALSE, error);
}

/* Store executable intent alongside immutable contents for revision snapshots. */
LsArtifact *
ls_artifact_new_full(const gchar *path, const gchar *content, gboolean executable, GError **error){
	LsArtifact *self;
	if (!ls_path_valid(path) || content == NULL || !g_utf8_validate(content, -1, NULL) ||
		strlen(content) > LS_MAX_DOCUMENT) {
		g_set_error(
			error,
			LS_ERROR,
			LS_ERROR_INVALID,
			"Unsafe artifact path or invalid contents: %s",
			path != NULL ? path : "(missing)"
		);
		return NULL;
	}
	self = g_new0(LsArtifact, 1);
	self->path = g_strdup(path);
	self->content = g_strdup(content);
	self->executable = executable;
	return self;
}
/* Boxed copies have no shared mutable state. */
LsArtifact *
ls_artifact_copy(const LsArtifact *self){
	return ls_artifact_new_full(self->path, self->content, self->executable, NULL);
}
/* Accept NULL to support automatic scope cleanup. */
void
ls_artifact_free(LsArtifact *self){
	if (self == NULL)
		return;
	g_free(self->path);
	g_free(self->content);
	g_free(self);
}
/* Borrowed strings remain valid for the artifact lifetime. */
const gchar *
ls_artifact_get_path(const LsArtifact *self){
	return self->path;
}
const gchar *
ls_artifact_get_content(const LsArtifact *self){
	return self->content;
}

gboolean
ls_artifact_get_executable(const LsArtifact *self){
	return self->executable;
}
