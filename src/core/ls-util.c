/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "core/ls-util.h"
#include <yaml-glib.h>
#include <yaml.h>
#include <string.h>

G_DEFINE_QUARK(ls - error - quark, ls_error)

/* Reject ambiguous YAML before yaml-glib normalizes it: duplicate keys,
 * aliases, complex keys, explicit tags and multiple documents are errors. */
static gboolean
check_yaml(const gchar *text, GError **error){
	yaml_parser_t parser;
	yaml_event_t event;
	GHashTable *keys[32] = {NULL};
	gboolean expecting_key[32] = {FALSE};
	gint depth = 0;
	guint documents = 0, events = 0;
	gboolean okay = TRUE, done = FALSE;
	const gchar *reason = "Invalid YAML";
	if (!yaml_parser_initialize(&parser)) {
		g_set_error_literal(error, LS_ERROR, LS_ERROR_LIMIT, "Cannot initialize YAML parser");
		return FALSE;
	}
	yaml_parser_set_input_string(&parser, (const unsigned char *)text, strlen(text));
	while (okay && !done) {
		if (!yaml_parser_parse(&parser, &event)) {
			g_set_error(
				error,
				LS_ERROR,
				LS_ERROR_INVALID,
				"YAML line %lu: %s",
				(gulong)parser.problem_mark.line + 1,
				parser.problem
			);
			okay = FALSE;
			break;
		}
		if (++events > 100000) {
			okay = FALSE;
			reason = "Too many YAML nodes";
		}
		if (event.type == YAML_DOCUMENT_START_EVENT && ++documents > 1) {
			okay = FALSE;
			reason = "Exactly one YAML document is required";
		}
		if (event.type == YAML_ALIAS_EVENT) {
			okay = FALSE;
			reason = "YAML aliases are not supported";
		}
		if (event.type == YAML_SCALAR_EVENT) {
			if (event.data.scalar.tag != NULL || event.data.scalar.anchor != NULL ||
				strlen((const gchar *)event.data.scalar.value) != event.data.scalar.length) {
				okay = FALSE;
				reason = "Tags, anchors and NUL values are not supported";
			}
			if (depth > 0 && keys[depth - 1] != NULL) {
				if (expecting_key[depth - 1]) {
					if (!g_hash_table_add(
							keys[depth - 1], g_strdup((const gchar *)event.data.scalar.value)
						)) {
						okay = FALSE;
						reason = "Duplicate YAML mapping key";
					}
				}
				expecting_key[depth - 1] = !expecting_key[depth - 1];
			}
		} else if (
			event.type == YAML_MAPPING_START_EVENT || event.type == YAML_SEQUENCE_START_EVENT
		) {
			if ((event.type == YAML_MAPPING_START_EVENT &&
				 (event.data.mapping_start.tag != NULL ||
				  event.data.mapping_start.anchor != NULL)) ||
				(event.type == YAML_SEQUENCE_START_EVENT &&
				 (event.data.sequence_start.tag != NULL ||
				  event.data.sequence_start.anchor != NULL))) {
				okay = FALSE;
				reason = "Collection tags and anchors are not supported";
			}
			if (depth == 32 || (depth > 0 && keys[depth - 1] != NULL && expecting_key[depth - 1])) {
				okay = FALSE;
				reason = "YAML nesting limit or non-scalar mapping key";
			} else {
				if (depth > 0 && keys[depth - 1] != NULL)
					expecting_key[depth - 1] = TRUE;
				keys[depth] = event.type == YAML_MAPPING_START_EVENT
								  ? g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL)
								  : NULL;
				expecting_key[depth++] = TRUE;
			}
		} else if (event.type == YAML_MAPPING_END_EVENT || event.type == YAML_SEQUENCE_END_EVENT) {
			if (depth > 0) {
				--depth;
				g_clear_pointer(&keys[depth], g_hash_table_unref);
			}
		} else if (event.type == YAML_STREAM_END_EVENT)
			done = TRUE;
		yaml_event_delete(&event);
	}
	while (depth > 0) {
		--depth;
		g_clear_pointer(&keys[depth], g_hash_table_unref);
	}
	yaml_parser_delete(&parser);
	if (!okay && (error == NULL || *error == NULL))
		g_set_error_literal(error, LS_ERROR, LS_ERROR_INVALID, reason);
	return okay;
}

/* Convert once at the boundary; JSON-GLib provides precise scalar typing for
 * recipe validation and the same representation used in provider responses. */
JsonNode *
ls_yaml_parse(const gchar *text, GError **error){
	g_autoptr(YamlParser) parser = NULL;
	YamlDocument *document;
	if (text == NULL || strlen(text) > LS_MAX_DOCUMENT || !g_utf8_validate(text, -1, NULL)) {
		g_set_error_literal(
			error, LS_ERROR, LS_ERROR_INVALID, "Expected UTF-8 YAML of at most 16 MiB"
		);
		return NULL;
	}
	if (!check_yaml(text, error))
		return NULL;
	parser = yaml_parser_new();
	if (!yaml_parser_load_from_data(parser, text, -1, error))
		return NULL;
	document = yaml_parser_get_document(parser, 0);
	if (document == NULL) {
		g_set_error_literal(error, LS_ERROR, LS_ERROR_INVALID, "Empty YAML document");
		return NULL;
	}
	return yaml_document_to_json_node(document);
}

/* Stream with an allocation ceiling instead of trusting a racy stat size. */
gchar *
ls_read_text(const gchar *path, GError **error){
	g_autoptr(GFile) file = g_file_new_for_path(path);
	g_autoptr(GFileInputStream) input = g_file_read(file, NULL, error);
	g_autoptr(GString) text = g_string_new(NULL);
	gchar buffer[8192];
	gssize count;
	if (input == NULL)
		return NULL;
	while (
		(count = g_input_stream_read(G_INPUT_STREAM(input), buffer, sizeof(buffer), NULL, error)) >
		0) {
		if (text->len + (gsize)count > LS_MAX_DOCUMENT || memchr(buffer, 0, (gsize)count) != NULL) {
			g_set_error_literal(
				error, LS_ERROR, LS_ERROR_LIMIT, "Text file exceeds 16 MiB or contains NUL bytes"
			);
			return NULL;
		}
		g_string_append_len(text, buffer, count);
	}
	if (count < 0)
		return NULL;
	if (!g_utf8_validate(text->str, (gssize)text->len, NULL)) {
		g_set_error_literal(error, LS_ERROR, LS_ERROR_INVALID, "File is not UTF-8");
		return NULL;
	}
	return g_string_free(g_steal_pointer(&text), FALSE);
}

/* Unknown keys are errors: a misspelled model must not silently spend money. */
gboolean
ls_object_keys(JsonObject *object, const gchar *keys, GError **error){
	g_autoptr(GList) members = json_object_get_members(object);
	GList *item;
	for (item = members; item != NULL; item = item->next) {
		g_autofree gchar *needle = g_strdup_printf("|%s|", (gchar *)item->data);
		if (strstr(keys, needle) == NULL) {
			g_set_error(error, LS_ERROR, LS_ERROR_INVALID, "Unknown key '%s'", (gchar *)item->data);
			return FALSE;
		}
	}
	return TRUE;
}

/* Non-string values intentionally return NULL instead of GLib criticals. */
const gchar *
ls_string(JsonObject *object, const gchar *key){
	JsonNode *node = json_object_get_member(object, key);
	return node != NULL && json_node_get_value_type(node) == G_TYPE_STRING
			   ? json_node_get_string(node)
			   : NULL;
}

/* Ban dot directories and platform-dependent path syntax in generated files. */
gboolean
ls_path_valid(const gchar *path){
	g_auto(GStrv) parts = NULL;
	guint i;
	const gchar *cursor;
	if (path == NULL || *path == '\0' || strlen(path) > 1024 || !g_utf8_validate(path, -1, NULL) ||
		g_path_is_absolute(path))
		return FALSE;
	for (cursor = path; *cursor != '\0'; cursor++) {
		if ((guchar)*cursor < 32 || *cursor == '\\' || *cursor == ':' || (guchar)*cursor == 127)
			return FALSE;
	}
	parts = g_strsplit(path, "/", -1);
	for (i = 0; parts[i] != NULL; i++) {
		if (*parts[i] == '\0' || strlen(parts[i]) > 255 || g_str_equal(parts[i], ".") ||
			g_str_equal(parts[i], "..") || g_str_equal(parts[i], ".git") ||
			g_str_equal(parts[i], ".libresite"))
			return FALSE;
	}
	return TRUE;
}

/* Deterministic serialization used by prompts and checkpoint fingerprints. */
gchar *
ls_json_encode(JsonNode *node){
	g_autoptr(JsonGenerator) generator = json_generator_new();
	json_generator_set_root(generator, node);
	return json_generator_to_data(generator, NULL);
}

/* JsonParser signals expose duplicate members before callers lose that fact. */
typedef struct {
	GHashTable *objects;
	gboolean duplicate;
} JsonValidation;
static void
json_member(JsonParser *parser, JsonObject *object, const gchar *member, gpointer data){
	JsonValidation *validation = (JsonValidation *)data;
	GHashTable *keys = (GHashTable *)g_hash_table_lookup(validation->objects, object);
	(void)parser;
	if (keys == NULL) {
		keys = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
		g_hash_table_insert(validation->objects, object, keys);
	}
	if (!g_hash_table_add(keys, g_strdup(member)))
		validation->duplicate = TRUE;
}

/* A small lexical preflight bounds recursion before JSON-GLib builds the tree.
 * Escaped backslashes are skipped, so literal text "\\u0000" remains valid. */
JsonNode *
ls_json_parse(const gchar *text, GError **error){
	g_autoptr(JsonParser) parser = json_parser_new();
	g_autoptr(GHashTable) objects = g_hash_table_new_full(
		g_direct_hash, g_direct_equal, NULL, (GDestroyNotify)g_hash_table_unref
	);
	JsonValidation validation = {objects, FALSE};
	gboolean quoted = FALSE;
	gint depth = 0;
	const gchar *cursor;
	if (text == NULL || strlen(text) > LS_MAX_PROJECT * 2 || !g_utf8_validate(text, -1, NULL))
		goto invalid;
	for (cursor = text; *cursor != '\0'; cursor++) {
		if (quoted && *cursor == '\\') {
			if (g_str_has_prefix(cursor + 1, "u0000"))
				goto invalid;
			if (cursor[1] != '\0')
				cursor++;
		} else if (*cursor == '"')
			quoted = !quoted;
		else if (!quoted && (*cursor == '{' || *cursor == '[')) {
			if (++depth > 32)
				goto invalid;
		} else if (!quoted && (*cursor == '}' || *cursor == ']'))
			depth--;
	}
	g_signal_connect(parser, "object-member", G_CALLBACK(json_member), &validation);
	if (!json_parser_load_from_data(parser, text, -1, error))
		return NULL;
	if (validation.duplicate) {
		g_set_error_literal(error, LS_ERROR, LS_ERROR_INVALID, "Duplicate JSON object member");
		return NULL;
	}
	return json_node_copy(json_parser_get_root(parser));
invalid:
	g_set_error_literal(
		error,
		LS_ERROR,
		LS_ERROR_INVALID,
		"JSON contains NUL, exceeds size/nesting limits or is not UTF-8"
	);
	return NULL;
}
