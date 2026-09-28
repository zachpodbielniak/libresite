/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "core/ls-template.h"
#include "core/ls-util.h"
#include <string.h>

typedef enum { TOKEN_TEXT, TOKEN_VALUE, TOKEN_RAW, TOKEN_OPEN, TOKEN_INVERT, TOKEN_CLOSE } TokenKind;
typedef struct {
	TokenKind kind;
	gchar *text;
	guint end;		/* index of the matching close token for sections */
} Token;

static void
token_free(gpointer data){
	Token *token = data;
	g_free(token->text);
	g_free(token);
}

gchar *
ls_template_escape(const gchar *text){
	GString *out = g_string_new(NULL);
	const gchar *p;
	for (p = text != NULL ? text : ""; *p != '\0'; p++) {
		switch (*p) {
		case '&': g_string_append(out, "&amp;"); break;
		case '<': g_string_append(out, "&lt;"); break;
		case '>': g_string_append(out, "&gt;"); break;
		case '"': g_string_append(out, "&quot;"); break;
		case '\'': g_string_append(out, "&#39;"); break;
		default: g_string_append_c(out, *p);
		}
	}
	return g_string_free(out, FALSE);
}

static gboolean
invalid(GError **error, const gchar *message, const gchar *name){
	g_set_error(error, LS_ERROR, LS_ERROR_INVALID, "Template %s%s%s", message,
		name != NULL ? ": " : "", name != NULL ? name : "");
	return FALSE;
}

/* Split into tokens and pair every section with its close before rendering,
 * so a malformed template can never produce partial output. */
static GPtrArray *
tokenize(const gchar *source, GError **error){
	g_autoptr(GPtrArray) tokens = g_ptr_array_new_with_free_func(token_free);
	g_autoptr(GArray) open = g_array_new(FALSE, FALSE, sizeof(guint));
	const gchar *p = source;
	while (*p != '\0') {
		const gchar *tag = strstr(p, "{{");
		const gchar *close;
		gboolean triple;
		Token *token;
		g_autofree gchar *body = NULL;
		gchar *name;
		if (tag == NULL)
			tag = p + strlen(p);
		if (tag > p) {
			token = g_new0(Token, 1);
			token->kind = TOKEN_TEXT;
			token->text = g_strndup(p, (gsize)(tag - p));
			g_ptr_array_add(tokens, token);
		}
		if (*tag == '\0')
			break;
		triple = tag[2] == '{';
		close = strstr(tag + (triple ? 3 : 2), triple ? "}}}" : "}}");
		if (close == NULL) {
			invalid(error, "has an unterminated tag", NULL);
			return NULL;
		}
		body = g_strndup(tag + (triple ? 3 : 2), (gsize)(close - tag - (triple ? 3 : 2)));
		p = close + (triple ? 3 : 2);
		name = g_strstrip(body);
		if (*name == '!')
			continue;
		token = g_new0(Token, 1);
		if (triple)
			token->kind = TOKEN_RAW;
		else if (*name == '&' || *name == '#' || *name == '^' || *name == '/') {
			token->kind = *name == '&' ? TOKEN_RAW : *name == '#' ? TOKEN_OPEN
				: *name == '^' ? TOKEN_INVERT : TOKEN_CLOSE;
			name = g_strstrip(name + 1);
		} else
			token->kind = TOKEN_VALUE;
		token->text = g_strdup(name);
		if (*token->text == '\0') {
			token_free(token);
			invalid(error, "has an empty tag", NULL);
			return NULL;
		}
		g_ptr_array_add(tokens, token);
		if (token->kind == TOKEN_OPEN || token->kind == TOKEN_INVERT) {
			guint index = tokens->len - 1;
			g_array_append_val(open, index);
		} else if (token->kind == TOKEN_CLOSE) {
			Token *opener;
			if (open->len == 0) {
				invalid(error, "closes a section that is not open", token->text);
				return NULL;
			}
			opener = g_ptr_array_index(tokens, g_array_index(open, guint, open->len - 1));
			if (!g_str_equal(opener->text, token->text)) {
				invalid(error, "closes the wrong section", token->text);
				return NULL;
			}
			opener->end = tokens->len - 1;
			g_array_set_size(open, open->len - 1);
		}
	}
	if (open->len > 0) {
		Token *opener = g_ptr_array_index(tokens, g_array_index(open, guint, open->len - 1));
		invalid(error, "leaves a section open", opener->text);
		return NULL;
	}
	return (GPtrArray *)g_steal_pointer(&tokens);
}

/* Resolve the first segment outward, the rest inside the found value. */
static JsonNode *
lookup(GPtrArray *stack, const gchar *name){
	g_auto(GStrv) parts = NULL;
	JsonNode *node = NULL;
	guint i;
	gint level;
	if (g_str_equal(name, "."))
		return g_ptr_array_index(stack, stack->len - 1);
	parts = g_strsplit(name, ".", -1);
	for (level = (gint)stack->len - 1; level >= 0 && node == NULL; level--) {
		JsonNode *frame = g_ptr_array_index(stack, level);
		if (frame != NULL && JSON_NODE_HOLDS_OBJECT(frame))
			node = json_object_get_member(json_node_get_object(frame), parts[0]);
	}
	for (i = 1; node != NULL && parts[i] != NULL; i++)
		node = JSON_NODE_HOLDS_OBJECT(node)
			? json_object_get_member(json_node_get_object(node), parts[i]) : NULL;
	return node;
}

static gboolean
truthy(JsonNode *node){
	if (node == NULL || JSON_NODE_HOLDS_NULL(node))
		return FALSE;
	if (JSON_NODE_HOLDS_ARRAY(node))
		return json_array_get_length(json_node_get_array(node)) > 0;
	if (JSON_NODE_HOLDS_VALUE(node)) {
		GType type = json_node_get_value_type(node);
		if (type == G_TYPE_BOOLEAN)
			return json_node_get_boolean(node);
		if (type == G_TYPE_STRING)
			return *json_node_get_string(node) != '\0';
	}
	return TRUE;
}

static gchar *
scalar(JsonNode *node){
	GType type;
	gchar buffer[G_ASCII_DTOSTR_BUF_SIZE];
	if (node == NULL || !JSON_NODE_HOLDS_VALUE(node))
		return g_strdup("");
	type = json_node_get_value_type(node);
	if (type == G_TYPE_STRING)
		return g_strdup(json_node_get_string(node));
	if (type == G_TYPE_INT64)
		return g_strdup_printf("%" G_GINT64_FORMAT, json_node_get_int(node));
	if (type == G_TYPE_DOUBLE)
		return g_strdup(g_ascii_dtostr(buffer, sizeof buffer, json_node_get_double(node)));
	if (type == G_TYPE_BOOLEAN)
		return g_strdup(json_node_get_boolean(node) ? "true" : "false");
	return g_strdup("");
}

static void
render_range(GPtrArray *tokens, guint start, guint end, GPtrArray *stack, GString *out){
	guint i;
	for (i = start; i < end; i++) {
		Token *token = g_ptr_array_index(tokens, i);
		JsonNode *node;
		switch (token->kind) {
		case TOKEN_TEXT:
			g_string_append(out, token->text);
			break;
		case TOKEN_VALUE:
		case TOKEN_RAW: {
			g_autofree gchar *text = scalar(lookup(stack, token->text));
			if (token->kind == TOKEN_RAW)
				g_string_append(out, text);
			else {
				g_autofree gchar *escaped = ls_template_escape(text);
				g_string_append(out, escaped);
			}
			break;
		}
		case TOKEN_OPEN:
			node = lookup(stack, token->text);
			if (truthy(node)) {
				if (JSON_NODE_HOLDS_ARRAY(node)) {
					JsonArray *items = json_node_get_array(node);
					guint j;
					for (j = 0; j < json_array_get_length(items); j++) {
						g_ptr_array_add(stack, json_array_get_element(items, j));
						render_range(tokens, i + 1, token->end, stack, out);
						g_ptr_array_set_size(stack, stack->len - 1);
					}
				} else {
					g_ptr_array_add(stack, JSON_NODE_HOLDS_OBJECT(node) ? node
						: g_ptr_array_index(stack, stack->len - 1));
					render_range(tokens, i + 1, token->end, stack, out);
					g_ptr_array_set_size(stack, stack->len - 1);
				}
			}
			i = token->end;
			break;
		case TOKEN_INVERT:
			if (!truthy(lookup(stack, token->text)))
				render_range(tokens, i + 1, token->end, stack, out);
			i = token->end;
			break;
		case TOKEN_CLOSE:
			break;
		}
	}
}

gchar *
ls_template_render(const gchar *source, JsonNode *view, GError **error){
	g_autoptr(GPtrArray) tokens = NULL;
	g_autoptr(GPtrArray) stack = g_ptr_array_new();
	GString *out;
	g_return_val_if_fail(source != NULL && view != NULL, NULL);
	tokens = tokenize(source, error);
	if (tokens == NULL)
		return NULL;
	out = g_string_sized_new(strlen(source) * 2);
	g_ptr_array_add(stack, view);
	render_range(tokens, 0, tokens->len, stack, out);
	return g_string_free(out, FALSE);
}
