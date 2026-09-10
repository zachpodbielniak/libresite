/* SPDX-License-Identifier: AGPL-3.0-or-later */
#pragma once
#include <gio/gio.h>
#include <json-glib/json-glib.h>
G_BEGIN_DECLS
#define LS_ERROR (ls_error_quark())
#define LS_MAX_DOCUMENT (16 * 1024 * 1024)
#define LS_MAX_PROJECT (64 * 1024 * 1024)
/**
 * LsError:
 * @LS_ERROR_INVALID: Invalid input or provider output.
 * @LS_ERROR_CONFLICT: Existing output or incompatible checkpoint.
 * @LS_ERROR_PROVIDER: Provider failure.
 * @LS_ERROR_LIMIT: Resource limit exceeded.
 */
typedef enum { LS_ERROR_INVALID, LS_ERROR_CONFLICT, LS_ERROR_PROVIDER, LS_ERROR_LIMIT } LsError;
GQuark ls_error_quark(void);
/* Internal helpers shared by the parser, artifact store and CLI. */
JsonNode *ls_yaml_parse(const gchar *text, GError **error);
gchar *ls_read_text(const gchar *path, GError **error);
gboolean ls_object_keys(JsonObject *object, const gchar *keys, GError **error);
const gchar *ls_string(JsonObject *object, const gchar *key);
gboolean ls_path_valid(const gchar *path);
gchar *ls_json_encode(JsonNode *node);
/* Strict JSON boundary: rejects duplicate members, NUL escapes and deep trees. */
JsonNode *ls_json_parse(const gchar *text, GError **error);
G_END_DECLS
