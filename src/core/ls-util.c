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

/* yaml-glib infers numeric/boolean/null types even for quoted scalars. Restore
 * explicit string styles from libyaml after conversion, including empty and
 * null-looking strings whose original spelling yaml-glib has discarded.
 * check_yaml already bounds recursion and rejects aliases/duplicate keys. */
static void
restore_yaml_strings(yaml_document_t *document, yaml_node_t *source, JsonNode *target){
	if (source->type == YAML_SCALAR_NODE) {
		if (source->data.scalar.style != YAML_PLAIN_SCALAR_STYLE)
			json_node_init_string(target, (const gchar *)source->data.scalar.value);
	} else if (source->type == YAML_SEQUENCE_NODE) {
		yaml_node_item_t *item;
		guint i = 0;
		for (item = source->data.sequence.items.start;
			item < source->data.sequence.items.top; item++, i++)
			restore_yaml_strings(document, yaml_document_get_node(document, *item),
				json_array_get_element(json_node_get_array(target), i));
	} else if (source->type == YAML_MAPPING_NODE) {
		yaml_node_pair_t *pair;
		for (pair = source->data.mapping.pairs.start;
			pair < source->data.mapping.pairs.top; pair++) {
			yaml_node_t *key = yaml_document_get_node(document, pair->key);
			restore_yaml_strings(document, yaml_document_get_node(document, pair->value),
				json_object_get_member(json_node_get_object(target),
					(const gchar *)key->data.scalar.value));
		}
	}
}

/* Normalize only the converted tree; never modify dependency sources or the
 * borrowed yaml-glib document. Plain scalar resolution remains compatible. */
JsonNode *
ls_yaml_parse(const gchar *text, GError **error){
	g_autoptr(YamlParser) parser = NULL;
	g_autoptr(JsonNode) result = NULL;
	YamlDocument *document;
	yaml_parser_t raw_parser;
	yaml_document_t raw_document;
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
	result = yaml_document_to_json_node(document);
	if (!yaml_parser_initialize(&raw_parser)) {
		g_set_error_literal(error, LS_ERROR, LS_ERROR_LIMIT, "Cannot initialize YAML parser");
		return NULL;
	}
	yaml_parser_set_input_string(&raw_parser, (const unsigned char *)text, strlen(text));
	if (!yaml_parser_load(&raw_parser, &raw_document)) {
		g_set_error_literal(error, LS_ERROR, LS_ERROR_INVALID, "Cannot restore YAML string types");
		yaml_parser_delete(&raw_parser);
		return NULL;
	}
	if (yaml_document_get_root_node(&raw_document) != NULL)
		restore_yaml_strings(&raw_document, yaml_document_get_root_node(&raw_document), result);
	yaml_document_delete(&raw_document);
	yaml_parser_delete(&raw_parser);
	return g_steal_pointer(&result);
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
		/* Delimiters belong to the allowlist syntax, never to a member name. */
		if (strchr((const gchar *)item->data, '|') != NULL || strstr(keys, needle) == NULL) {
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
gboolean
ls_focus_valid(const gchar *focus){
	g_auto(GStrv) parts = NULL;
	guint i, n;
	if (focus == NULL || *focus == '\0' || strlen(focus) > 24)
		return FALSE;
	parts = g_strsplit(focus, " ", -1);
	n = g_strv_length(parts);
	if (n < 1 || n > 2)
		return FALSE;
	if (g_str_has_suffix(parts[0], "%")) {
		for (i = 0; i < n; i++) {
			gchar *end = NULL;
			gdouble value;
			if (!g_str_has_suffix(parts[i], "%"))
				return FALSE;
			value = g_ascii_strtod(parts[i], &end);
			if (end == parts[i] || *end != '%' || end[1] != '\0' || value < 0 || value > 100)
				return FALSE;
		}
		return n == 2;
	}
	for (i = 0; i < n; i++)
		if (!g_str_equal(parts[i], "left") && !g_str_equal(parts[i], "center") && !g_str_equal(parts[i], "right") &&
			!g_str_equal(parts[i], "top") && !g_str_equal(parts[i], "bottom"))
			return FALSE;
	return TRUE;
}

/* A phone number as a person writes it: digits, spaces, + . - ( ) only, at
 * most 40 bytes, 3 to 15 digits and a + only before the first digit. The
 * tel: address keeps the + and the digits. */
gchar *
ls_phone_tel(const gchar *phone){
	GString *tel;
	const gchar *c;
	guint digits = 0;
	if (phone == NULL || strlen(phone) > 40 || strspn(phone, "0123456789 +-().") != strlen(phone))
		return NULL;
	tel = g_string_new("tel:");
	for (c = phone; *c != '\0'; c++) {
		if (g_ascii_isdigit(*c)) {
			g_string_append_c(tel, *c);
			digits++;
		} else if (*c == '+') {
			if (tel->len != 4 || strspn(phone, " ") != (gsize)(c - phone))
				break;
			g_string_append_c(tel, '+');
		}
	}
	if (*c != '\0' || digits < 3 || digits > 15) {
		g_string_free(tel, TRUE);
		return NULL;
	}
	return g_string_free(tel, FALSE);
}

/* One @ between a local part of up to 64 and a dotted domain of letters,
 * digits, dots and hyphens; printable ASCII without spaces, quotes,
 * brackets, separators or the ? # % that would change a mailto: link; at
 * most 254 bytes. */
gboolean
ls_email_valid(const gchar *email){
	const gchar *at = email != NULL ? strchr(email, '@') : NULL;
	const gchar *c;
	if (at == NULL || strlen(email) > 254 || at == email || at - email > 64 || strchr(at + 1, '@') != NULL ||
		strchr(at + 1, '.') == NULL || at[1] == '.' || g_str_has_suffix(email, ".") || strstr(at, "..") != NULL)
		return FALSE;
	for (c = email; *c != '\0'; c++)
		if (*c <= ' ' || *c > '~' || strchr("\"'<>()[]\\,;:`?#%", *c) != NULL)
			return FALSE;
	return strspn(at + 1, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-") == strlen(at + 1);
}
/* {text, url}: one line of 1 to 80 characters and an https:// address with
 * a host, at most 300 bytes, without spaces, quotes or markup characters. */
gboolean
ls_credit_valid(JsonObject *credit){
	const gchar *text = credit != NULL ? ls_string(credit, "text") : NULL;
	const gchar *url = credit != NULL ? ls_string(credit, "url") : NULL;
	const gchar *c;
	if (text == NULL || url == NULL || !ls_object_keys(credit, "|text|url|", NULL) ||
		!g_utf8_validate(text, -1, NULL) || g_utf8_strlen(text, -1) > 80 || strspn(text, " \t") == strlen(text))
		return FALSE;
	for (c = text; *c != '\0'; c++)
		if ((guchar)*c < 0x20 || *c == 0x7f)
			return FALSE;
	return g_str_has_prefix(url, "https://") && strlen(url) > 8 && url[8] != '/' && strlen(url) <= 300 &&
		strpbrk(url, " \t\r\n\"'<>\\`") == NULL && g_uri_is_valid(url, G_URI_FLAGS_NONE, NULL);
}
gboolean
ls_form_action_valid(const gchar *action){
	if (action == NULL || strlen(action) < 2 || strlen(action) > 300 ||
		strpbrk(action, " \t\r\n\"'<>\\`#?") != NULL)
		return FALSE;
	if (g_str_has_prefix(action, "https://"))
		return strlen(action) > 8 && action[8] != '/' && g_uri_is_valid(action, G_URI_FLAGS_NONE, NULL);
	return action[0] == '/' && action[1] != '/' && strstr(action, "..") == NULL &&
		strspn(action, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789/_.-") == strlen(action);
}
/* One site.form field. @seen collects names so a later field, or the
 * honeypot, cannot reuse one. The object is only read. */
static gboolean
form_field_valid(JsonObject *field, GHashTable *seen){
	static const gchar *const types[] = {"text", "email", "tel", "url", "number", "date", "textarea", NULL};
	const gchar *name = ls_string(field, "name");
	const gchar *label = ls_string(field, "label");
	const gchar *type = ls_string(field, "type");
	const gchar *hint = ls_string(field, "hint");
	const gchar *autocomplete = ls_string(field, "autocomplete");
	JsonNode *required = json_object_get_member(field, "required");
	JsonNode *maxlength = json_object_get_member(field, "maxlength");
	if (!ls_object_keys(field, "|name|label|type|required|autocomplete|maxlength|hint|", NULL) ||
		name == NULL || *name == '\0' || strlen(name) > 40 ||
		strspn(name, "abcdefghijklmnopqrstuvwxyz0123456789_-") != strlen(name) ||
		label == NULL || *label == '\0' || g_utf8_strlen(label, -1) > 80 ||
		(json_object_has_member(field, "hint") && (hint == NULL || *hint == '\0' || g_utf8_strlen(hint, -1) > 200)) ||
		(json_object_has_member(field, "autocomplete") && (autocomplete == NULL || *autocomplete == '\0' ||
		strlen(autocomplete) > 40 ||
		strspn(autocomplete, "abcdefghijklmnopqrstuvwxyz- ") != strlen(autocomplete))) ||
		(type != NULL && !g_strv_contains(types, type)) ||
		(required != NULL && json_node_get_value_type(required) != G_TYPE_BOOLEAN) ||
		(maxlength != NULL && (json_node_get_value_type(maxlength) != G_TYPE_INT64 ||
		json_node_get_int(maxlength) < 1 || json_node_get_int(maxlength) > 20000)) ||
		!g_hash_table_add(seen, (gpointer)name))
		return FALSE;
	return TRUE;
}
const gchar *
ls_form_requirements(void){
	return "site.form needs {action, fields, honeypot}: action a same-site path such as /api/forms/contact or an "
		"https:// address, without spaces, quotes, ? or #; fields 1 to 12 unique {name, label, type, required, "
		"autocomplete, maxlength, hint} with names of lowercase letters, digits, - or _ and type text, email, tel, "
		"url, number, date or textarea; honeypot an optional field name not among the fields";
}
gboolean
ls_form_valid(JsonObject *form){
	JsonNode *node;
	JsonArray *fields;
	const gchar *action, *trap;
	g_autoptr(GHashTable) seen = g_hash_table_new(g_str_hash, g_str_equal);
	guint i;
	if (form == NULL || !ls_object_keys(form, "|action|fields|honeypot|", NULL))
		return FALSE;
	action = ls_string(form, "action");
	if (!ls_form_action_valid(action))
		return FALSE;
	node = json_object_get_member(form, "fields");
	fields = node != NULL && JSON_NODE_HOLDS_ARRAY(node) ? json_node_get_array(node) : NULL;
	if (fields == NULL || json_array_get_length(fields) == 0 || json_array_get_length(fields) > 12)
		return FALSE;
	for (i = 0; i < json_array_get_length(fields); i++) {
		JsonNode *entry = json_array_get_element(fields, i);
		if (!JSON_NODE_HOLDS_OBJECT(entry) || !form_field_valid(json_node_get_object(entry), seen))
			return FALSE;
	}
	trap = ls_string(form, "honeypot");
	if (json_object_has_member(form, "honeypot") && (trap == NULL || *trap == '\0' || strlen(trap) > 40 ||
		strspn(trap, "abcdefghijklmnopqrstuvwxyz0123456789_-") != strlen(trap) || g_hash_table_contains(seen, trap)))
		return FALSE;
	return TRUE;
}
/* {href}: a same-site path or https:// address, as a form action is. */
gboolean
ls_booking_valid(JsonObject *booking){
	return booking != NULL && ls_object_keys(booking, "|href|", NULL) && ls_form_action_valid(ls_string(booking, "href"));
}
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
	/* JSON-GLib otherwise accepts JavaScript assignments and comments. */
	json_parser_set_strict(parser, TRUE);
	if (!json_parser_load_from_data(parser, text, -1, error))
		return NULL;
	if (json_parser_get_root(parser) == NULL)
		goto invalid;
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

/* Run Git without inherited repository pointers, user hooks, filters or signing.
 * Cancellation must reap the child before the private export is abandoned. */
gboolean
ls_git_run(const gchar *directory, const gchar *const *arguments, gchar **output,
	GCancellable *cancellable, GError **error){
	g_autoptr(GSubprocessLauncher) launcher = g_subprocess_launcher_new(
		output != NULL ? G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE
		: G_SUBPROCESS_FLAGS_STDOUT_SILENCE | G_SUBPROCESS_FLAGS_STDERR_PIPE);
	g_autoptr(GSubprocess) process = NULL;
	g_auto(GStrv) environment = g_get_environ();
	g_autoptr(GPtrArray) argv = g_ptr_array_new();
	g_autofree gchar *diagnostic = NULL, *captured = NULL;
	guint i;
	if (g_cancellable_set_error_if_cancelled(cancellable, error))
		return FALSE;
	g_subprocess_launcher_set_cwd(launcher, directory);
	for (i = 0; environment[i] != NULL; i++) {
		if (g_str_has_prefix(environment[i], "GIT_")) {
			g_autofree gchar *name = g_strndup(environment[i],
				(gsize)(strchr(environment[i], '=') - environment[i]));
			g_subprocess_launcher_unsetenv(launcher, name);
		}
	}
	g_subprocess_launcher_setenv(launcher, "GIT_CONFIG_NOSYSTEM", "1", TRUE);
	g_subprocess_launcher_setenv(launcher, "GIT_CONFIG_GLOBAL", "/dev/null", TRUE);
	g_subprocess_launcher_setenv(launcher, "GIT_ATTR_NOSYSTEM", "1", TRUE);
	g_ptr_array_add(argv, (gpointer)"git");
	g_ptr_array_add(argv, (gpointer)"-c");
	g_ptr_array_add(argv, (gpointer)"core.hooksPath=/dev/null");
	g_ptr_array_add(argv, (gpointer)"-c");
	g_ptr_array_add(argv, (gpointer)"core.attributesFile=/dev/null");
	g_ptr_array_add(argv, (gpointer)"-c");
	g_ptr_array_add(argv, (gpointer)"core.excludesFile=/dev/null");
	g_ptr_array_add(argv, (gpointer)"-c");
	g_ptr_array_add(argv, (gpointer)"user.name=Libresite");
	g_ptr_array_add(argv, (gpointer)"-c");
	g_ptr_array_add(argv, (gpointer)"user.email=libresite@localhost");
	for (i = 0; arguments[i] != NULL; i++)
		g_ptr_array_add(argv, (gpointer)arguments[i]);
	g_ptr_array_add(argv, NULL);
	process = g_subprocess_launcher_spawnv(launcher, (const gchar *const *)argv->pdata, error);
	if (process == NULL) {
		g_prefix_error(error, "Cannot run git %s: ", arguments[0]);
		return FALSE;
	}
	if (!g_subprocess_communicate_utf8(process, NULL, cancellable, output != NULL ? &captured : NULL,
		&diagnostic, error)) {
		g_subprocess_force_exit(process);
		g_subprocess_wait(process, NULL, NULL);
		return FALSE;
	}
	if (!g_subprocess_get_successful(process)) {
		g_set_error(error, LS_ERROR, LS_ERROR_CONFLICT, "git %s failed: %s",
			arguments[0], diagnostic != NULL ? diagnostic : "no diagnostic");
		return FALSE;
	}
	if (output != NULL)
		*output = captured != NULL ? g_steal_pointer(&captured) : g_strdup("");
	return TRUE;
}
