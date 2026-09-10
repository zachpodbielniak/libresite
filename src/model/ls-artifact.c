/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "model/ls-artifact.h"
#include "core/ls-util.h"
#include <string.h>
struct _LsArtifact {
	gchar *path;
	guint8 *data;
	gsize size;
	gboolean binary;
	gboolean executable;
};
G_DEFINE_BOXED_TYPE(LsArtifact, ls_artifact, ls_artifact_copy, ls_artifact_free)

/* Own a trailing NUL so UTF-8 accessors remain C-string compatible. Binary
 * payloads may contain interior NULs; size, not strlen, is authoritative. */
LsArtifact *
ls_artifact_new_bytes(const gchar *path, const guint8 *data, gsize size,
	gboolean binary, gboolean executable, GError **error){
	LsArtifact *self;
	if (!ls_path_valid(path) || data == NULL || size > LS_MAX_DOCUMENT) {
		g_set_error(
			error,
			LS_ERROR,
			LS_ERROR_INVALID,
			"Unsafe artifact path or invalid contents: %s",
			path != NULL ? path : "(missing)"
		);
		return NULL;
	}
	if (!binary && (memchr(data, 0, size) != NULL ||
		!g_utf8_validate((const gchar *)data, (gssize)size, NULL))) {
		g_set_error(
			error,
			LS_ERROR,
			LS_ERROR_INVALID,
			"Unsafe artifact path or invalid contents: %s",
			path
		);
		return NULL;
	}
	self = g_new0(LsArtifact, 1);
	self->path = g_strdup(path);
	self->size = size;
	self->binary = binary;
	self->executable = executable;
	self->data = g_malloc(size + 1);
	if (size > 0)
		memcpy(self->data, data, size);
	self->data[size] = '\0';
	return self;
}

/* Own immutable strings so callers cannot change validated paths afterward. */
LsArtifact *
ls_artifact_new(const gchar *path, const gchar *content, GError **error){
	return ls_artifact_new_full(path, content, FALSE, error);
}

/* Store executable intent alongside immutable contents for revision snapshots. */
LsArtifact *
ls_artifact_new_full(const gchar *path, const gchar *content, gboolean executable, GError **error){
	if (content == NULL) {
		g_set_error(
			error,
			LS_ERROR,
			LS_ERROR_INVALID,
			"Unsafe artifact path or invalid contents: %s",
			path != NULL ? path : "(missing)"
		);
		return NULL;
	}
	return ls_artifact_new_bytes(
		path, (const guint8 *)content, strlen(content), FALSE, executable, error
	);
}
/* Boxed copies have no shared mutable state. */
LsArtifact *
ls_artifact_copy(const LsArtifact *self){
	return ls_artifact_new_bytes(
		self->path, self->data, self->size, self->binary, self->executable, NULL
	);
}

/* Rename keeps bytes, encoding and executable intent; only the path changes. */
LsArtifact *
ls_artifact_with_path(const LsArtifact *self, const gchar *path, GError **error){
	return ls_artifact_new_bytes(
		path, self->data, self->size, self->binary, self->executable, error
	);
}
/* Accept NULL to support automatic scope cleanup. */
void
ls_artifact_free(LsArtifact *self){
	if (self == NULL)
		return;
	g_free(self->path);
	g_free(self->data);
	g_free(self);
}
/* Borrowed strings remain valid for the artifact lifetime. */
const gchar *
ls_artifact_get_path(const LsArtifact *self){
	return self->path;
}
const gchar *
ls_artifact_get_content(const LsArtifact *self){
	return self->binary ? NULL : (const gchar *)self->data;
}

const guint8 *
ls_artifact_get_data(const LsArtifact *self, gsize *size){
	if (size != NULL)
		*size = self->size;
	return self->data;
}

gsize
ls_artifact_get_size(const LsArtifact *self){
	return self->size;
}

gboolean
ls_artifact_is_binary(const LsArtifact *self){
	return self->binary;
}

gboolean
ls_artifact_get_executable(const LsArtifact *self){
	return self->executable;
}
