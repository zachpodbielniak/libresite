/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* Supplied podbay module snippets and stylesheets. A snippet is drawn into a
 * page as it is, so it is held to podbay's restricted markup here, with a
 * small strict reader rather than a general HTML parser: anything the reader
 * does not know is refused. See docs/components.org, "Podbay modules". */
#include "core/ls-module.h"
#include "core/ls-template.h"
#include "core/ls-util.h"
#include <string.h>

#define MAX_DEPTH 8
#define MAX_ID 100

gboolean
ls_module_id_valid(const gchar *id){
	const gchar *c;
	if (id == NULL || *id < 'a' || *id > 'z' || strlen(id) > 64)
		return FALSE;
	for (c = id; *c != '\0'; c++) {
		if (*c == '-') {
			if (c[1] == '\0' || c[1] == '-')
				return FALSE;
		} else if (!g_ascii_islower(*c) && !g_ascii_isdigit(*c))
			return FALSE;
	}
	return TRUE;
}

/* ---- Snippets ------------------------------------------------------------ */

typedef struct {
	const gchar *name;
	const gchar *attributes;	/* |allowed|names| */
	gboolean empty;		/* a void element: no closing tag */
} Element;

static const Element elements[] = {
	{"form", "|class|hx-post|hx-target|hx-swap|hx-encoding|", FALSE},
	{"div", "|class|id|data-podbay-outcome|aria-live|", FALSE},
	{"label", "|for|class|", FALSE},
	{"input", "|id|name|type|maxlength|min|max|step|value|required|class|autocomplete|placeholder|accept|multiple|",
		TRUE},
	{"textarea", "|id|name|maxlength|required|class|placeholder|rows|", FALSE},
	{"select", "|id|name|required|class|multiple|", FALSE},
	{"option", "|value|selected|", FALSE},
	{"button", "|type|class|", FALSE},
};
static const gchar *const flags[] = {"required", "multiple", "selected", NULL};
static const gchar *const swaps[] = {"innerHTML", "outerHTML", "beforebegin", "afterbegin", "beforeend", "afterend",
	"delete", "none", NULL};
static const gchar *const input_types[] = {"text", "email", "tel", "url", "number", "checkbox", "date", "time", "file",
	NULL};

typedef struct {
	const gchar *module;
	GHashTable *ids;
	GPtrArray *open;	/* element names, innermost last */
	gchar *target;		/* the form's hx-target id */
	gboolean form_seen;
	gboolean form_closed;
	guint buttons;
	GError **error;
} Reader;

static gboolean
refuse(Reader *reader, const gchar *format, ...) G_GNUC_PRINTF(2, 3);
static gboolean
refuse(Reader *reader, const gchar *format, ...){
	va_list args;
	g_autofree gchar *reason = NULL;
	va_start(args, format);
	reason = g_strdup_vprintf(format, args);
	va_end(args);
	g_set_error(reader->error, LS_ERROR, LS_ERROR_INVALID, "podbay module %s: the snippet %s", reader->module, reason);
	return FALSE;
}

/* Text or a value: & only as &amp; &lt; &gt; &quot; or a numeric reference. */
static gboolean
references_valid(const gchar *text, gsize length){
	gsize i = 0;
	while (i < length) {
		if (text[i] == '&') {
			static const gchar *const named[] = {"&amp;", "&lt;", "&gt;", "&quot;"};
			gboolean known = FALSE;
			guint n;
			for (n = 0; n < G_N_ELEMENTS(named) && !known; n++)
				if (length - i >= strlen(named[n]) && strncmp(text + i, named[n], strlen(named[n])) == 0) {
					i += strlen(named[n]);
					known = TRUE;
				}
			if (!known) {
				gsize start;
				if (length - i < 4 || text[i + 1] != '#')
					return FALSE;
				i += 2;
				if (text[i] == 'x') {
					start = ++i;
					while (i < length && g_ascii_isxdigit(text[i]))
						i++;
				} else {
					start = i;
					while (i < length && g_ascii_isdigit(text[i]))
						i++;
				}
				if (i == start || i - start > 6 || i >= length || text[i] != ';')
					return FALSE;
				i++;
			}
		} else
			i++;
	}
	return TRUE;
}

static gboolean
element_id_valid(const gchar *id){
	return id != NULL && g_ascii_isalpha(*id) && strlen(id) <= MAX_ID &&
		strspn(id, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") == strlen(id);
}

/* One attribute of @element, already known to be on its list. */
static gboolean
attribute_valid(Reader *reader, const gchar *element, const gchar *name, const gchar *value){
	if (g_strv_contains(flags, name))
		return value == NULL || refuse(reader, "gives %s a value; it is a flag", name);
	if (value == NULL)
		return refuse(reader, "has %s without a value", name);
	if (g_str_equal(name, "id")) {
		if (!element_id_valid(value))
			return refuse(reader, "has id \"%s\"; ids are letters, digits, - and _", value);
		if (!g_hash_table_add(reader->ids, g_strdup(value)))
			return refuse(reader, "uses the id \"%s\" twice", value);
	} else if (g_str_equal(name, "for") || g_str_equal(name, "data-podbay-outcome")) {
		if (!element_id_valid(value))
			return refuse(reader, "has %s=\"%s\"", name, value);
	} else if (g_str_equal(name, "hx-post")) {
		if (!ls_form_action_valid(value))
			return refuse(reader, "posts to \"%s\"; hx-post must be a same-site path or an https:// address", value);
	} else if (g_str_equal(name, "hx-target")) {
		if (value[0] != '#' || !element_id_valid(value + 1))
			return refuse(reader, "has hx-target \"%s\"; it must be # and an element id", value);
		reader->target = g_strdup(value + 1);
	} else if (g_str_equal(name, "hx-swap")) {
		if (!g_strv_contains(swaps, value))
			return refuse(reader, "has hx-swap \"%s\"; it must be one of podbay's eight modes", value);
	} else if (g_str_equal(name, "hx-encoding")) {
		if (!g_str_equal(value, "multipart/form-data"))
			return refuse(reader, "has hx-encoding \"%s\"", value);
	} else if (g_str_equal(name, "aria-live")) {
		if (!g_str_equal(value, "polite"))
			return refuse(reader, "has aria-live \"%s\"; outcomes are polite", value);
	} else if (g_str_equal(name, "type")) {
		if (g_str_equal(element, "button") ? !g_str_equal(value, "submit") : !g_strv_contains(input_types, value))
			return refuse(reader, "has a %s of type \"%s\"", element, value);
	}
	return TRUE;
}

/* The start tag at @at ("<name"); advances *@end past its ">". */
static gboolean
read_start(Reader *reader, const gchar *at, const gchar **end){
	const Element *element = NULL;
	const gchar *c = at + 1;
	g_autofree gchar *name = NULL;
	g_autofree gchar *classes = NULL;
	g_autoptr(GHashTable) seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	gboolean post = FALSE, swap = FALSE;
	guint i;
	while (g_ascii_islower(*c))
		c++;
	name = g_strndup(at + 1, c - at - 1);
	for (i = 0; i < G_N_ELEMENTS(elements); i++)
		if (g_str_equal(elements[i].name, name))
			element = &elements[i];
	if (element == NULL)
		return refuse(reader, "contains <%s>; only a form with labelled fields, a button and outcome regions "
			"may be drawn", *name != '\0' ? name : "!");
	if (reader->form_closed || (reader->open->len == 0) != g_str_equal(name, "form") ||
		(g_str_equal(name, "form") && reader->form_seen))
		return refuse(reader, "must be exactly one form, with nothing outside it");
	if (reader->open->len >= MAX_DEPTH)
		return refuse(reader, "nests more than %d elements deep", MAX_DEPTH);
	for (;;) {
		const gchar *start;
		g_autofree gchar *attribute = NULL;
		g_autofree gchar *value = NULL;
		g_autofree gchar *needle = NULL;
		if (*c != ' ' && *c != '\n' && *c != '\t' && *c != '>')
			return refuse(reader, "has a malformed <%s> tag", name);
		while (*c == ' ' || *c == '\n' || *c == '\t')
			c++;
		if (*c == '>')
			break;
		start = c;
		while (g_ascii_islower(*c) || *c == '-')
			c++;
		if (c == start)
			return refuse(reader, "has a malformed <%s> tag", name);
		attribute = g_strndup(start, c - start);
		needle = g_strconcat("|", attribute, "|", NULL);
		if (strstr(element->attributes, needle) == NULL)
			return refuse(reader, "gives <%s> the attribute %s", name, attribute);
		if (!g_hash_table_add(seen, g_strdup(attribute)))
			return refuse(reader, "repeats %s on <%s>", attribute, name);
		if (*c == '=') {
			const gchar *close;
			if (c[1] != '"')
				return refuse(reader, "leaves %s unquoted; quote every value with \"", attribute);
			close = strchr(c + 2, '"');
			if (close == NULL)
				return refuse(reader, "never closes the quote of %s", attribute);
			if (memchr(c + 2, '<', close - c - 2) != NULL || memchr(c + 2, '>', close - c - 2) != NULL ||
				!references_valid(c + 2, close - c - 2))
				return refuse(reader, "has < > or a bare & in the value of %s", attribute);
			value = g_strndup(c + 2, close - c - 2);
			c = close + 1;
		}
		if (!attribute_valid(reader, name, attribute, value))
			return FALSE;
		if (g_str_equal(attribute, "class"))
			classes = g_steal_pointer(&value);
		post = post || g_str_equal(attribute, "hx-post");
		swap = swap || g_str_equal(attribute, "hx-swap");
	}
	if (g_str_equal(name, "form")) {
		g_autofree gchar *own = g_strconcat("podbay-", reader->module, NULL);
		g_auto(GStrv) words = classes != NULL ? g_strsplit(classes, " ", -1) : NULL;
		if (words == NULL || !g_strv_contains((const gchar *const *)words, "podbay") ||
			!g_strv_contains((const gchar *const *)words, own))
			return refuse(reader, "form needs class=\"podbay %s\"", own);
		if (!post || reader->target == NULL || !swap)
			return refuse(reader, "form needs hx-post, hx-target and hx-swap");
		reader->form_seen = TRUE;
	}
	if (g_str_equal(name, "button"))
		reader->buttons++;
	if (!element->empty)
		g_ptr_array_add(reader->open, g_steal_pointer(&name));
	*end = c + 1;
	return TRUE;
}

gboolean
ls_module_check_snippet(const gchar *id, const gchar *snippet, GError **error){
	Reader reader = {id, NULL, NULL, NULL, FALSE, FALSE, 0, error};
	const gchar *c;
	gboolean okay = FALSE;
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);
	if (!ls_module_id_valid(id)) {
		g_set_error(error, LS_ERROR, LS_ERROR_INVALID, "podbay module ids are lowercase words joined by hyphens");
		return FALSE;
	}
	if (snippet == NULL || strlen(snippet) > LS_MODULE_MAX_TEXT || !g_utf8_validate(snippet, -1, NULL))
		return refuse(&reader, "must be UTF-8 text of at most %d bytes", LS_MODULE_MAX_TEXT);
	reader.ids = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	reader.open = g_ptr_array_new_with_free_func(g_free);
	c = snippet;
	while (*c != '\0') {
		if (*c == '<' && c[1] == '/') {
			const gchar *start = c + 2, *end = start;
			while (g_ascii_islower(*end))
				end++;
			if (*end != '>' || reader.open->len == 0 ||
				strncmp(g_ptr_array_index(reader.open, reader.open->len - 1), start, end - start) != 0 ||
				strlen(g_ptr_array_index(reader.open, reader.open->len - 1)) != (gsize)(end - start)) {
				refuse(&reader, "does not close its elements in order");
				goto out;
			}
			g_ptr_array_remove_index(reader.open, reader.open->len - 1);
			if (reader.open->len == 0)
				reader.form_closed = TRUE;
			c = end + 1;
		} else if (*c == '<') {
			if (!read_start(&reader, c, &c))
				goto out;
		} else {
			const gchar *end = strchr(c, '<');
			gsize length = end != NULL ? (gsize)(end - c) : strlen(c);
			if (reader.open->len == 0 && strspn(c, " \t\r\n") < length) {
				refuse(&reader, "must be exactly one form, with nothing outside it");
				goto out;
			}
			if (memchr(c, '>', length) != NULL || !references_valid(c, length)) {
				refuse(&reader, "has > or a bare & in its text");
				goto out;
			}
			c += length;
		}
	}
	if (!reader.form_closed)
		refuse(&reader, "must be exactly one complete form");
	else if (reader.buttons == 0)
		refuse(&reader, "has no submit button");
	else if (!g_hash_table_contains(reader.ids, reader.target))
		refuse(&reader, "names hx-target #%s, which is not in the snippet", reader.target);
	else
		okay = TRUE;
out:
	g_hash_table_unref(reader.ids);
	g_ptr_array_unref(reader.open);
	g_free(reader.target);
	return okay;
}

gchar *
ls_module_snippet_html(const gchar *snippet, const gchar *failed){
	const gchar *start, *c;
	g_autofree gchar *action = NULL, *escaped = NULL;
	g_return_val_if_fail(snippet != NULL && failed != NULL, NULL);
	start = snippet + strspn(snippet, " \t\r\n");
	if (!g_str_has_prefix(start, "<form") || strchr(" \t\n", start[5]) == NULL || start[5] == '\0')
		return NULL;
	/* Walk the form's attributes as the reader does, so a value can never be
	 * mistaken for an attribute and any whitespace may separate them. */
	for (c = start + 5; action == NULL;) {
		const gchar *name;
		gsize length;
		c += strspn(c, " \t\n");
		if (*c == '>' || *c == '\0')
			return NULL;
		name = c;
		while (g_ascii_islower(*c) || *c == '-')
			c++;
		length = (gsize)(c - name);
		if (length == 0)
			return NULL;
		if (*c == '=') {
			const gchar *close;
			if (c[1] != '"' || (close = strchr(c + 2, '"')) == NULL)
				return NULL;
			/* The value was checked as a form action: no quotes, brackets or &. */
			if (length == 7 && strncmp(name, "hx-post", 7) == 0)
				action = g_strndup(c + 2, (gsize)(close - c - 2));
			c = close + 1;
		}
	}
	escaped = ls_template_escape(failed);
	return g_strdup_printf("<form action=\"%s\" method=\"post\" data-podbay-form data-failed=\"%s\"%s", action, escaped,
		start + 5);
}

/* ---- Stylesheets --------------------------------------------------------- */

static const gchar *const css_functions[] = {"var", "calc", "min", "max", "clamp", "rgb", "rgba", "hsl", "hsla", "not",
	"is", "where", "nth-child", "nth-of-type", NULL};

static gboolean
css_refuse(GError **error, const gchar *id, const gchar *reason){
	g_set_error(error, LS_ERROR, LS_ERROR_INVALID, "podbay module %s: the stylesheet %s", id, reason);
	return FALSE;
}

/* Every selector in @list starts with .podbay-ID and does not extend it. */
static gboolean
selectors_valid(const gchar *id, const gchar *list, gsize length){
	g_autofree gchar *text = g_strndup(list, length);
	g_autofree gchar *own = g_strconcat(".podbay-", id, NULL);
	g_auto(GStrv) selectors = g_strsplit(text, ",", -1);
	guint i;
	for (i = 0; selectors[i] != NULL; i++) {
		const gchar *selector = g_strstrip(selectors[i]);
		gchar next;
		if (!g_str_has_prefix(selector, own))
			return FALSE;
		next = selector[strlen(own)];
		if (g_ascii_isalnum(next) || next == '-' || next == '_' || (guchar)next >= 0x80)
			return FALSE;
	}
	return TRUE;
}

gboolean
ls_module_check_css(const gchar *id, const gchar *css, GError **error){
	g_autofree gchar *plain = NULL;
	gchar *c;
	guint rules = 0;
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);
	if (!ls_module_id_valid(id)) {
		g_set_error(error, LS_ERROR, LS_ERROR_INVALID, "podbay module ids are lowercase words joined by hyphens");
		return FALSE;
	}
	if (css == NULL || strlen(css) > LS_MODULE_MAX_TEXT || !g_utf8_validate(css, -1, NULL))
		return css_refuse(error, id, "must be UTF-8 text of at most 65536 bytes");
	/* Comments become spaces, as podbay serves them. */
	plain = g_strdup(css);
	for (c = plain; (c = strstr(c, "/*")) != NULL;) {
		gchar *end = strstr(c + 2, "*/");
		if (end == NULL)
			return css_refuse(error, id, "never closes a comment");
		memset(c, ' ', end + 2 - c);
		c = end + 2;
	}
	if (strpbrk(plain, "@\\<\"'!&") != NULL)
		return css_refuse(error, id, "may not contain @ \\ < \" ' ! or &: no at-rules, escapes, strings or !important");
	for (c = plain; (c = strchr(c, '(')) != NULL; c++) {
		gchar *name = c;
		g_autofree gchar *function = NULL;
		while (name > plain && (g_ascii_isalnum(name[-1]) || name[-1] == '-'))
			name--;
		function = g_ascii_strdown(name, c - name);
		if (!g_strv_contains(css_functions, function))
			return css_refuse(error, id, "may only call var, calc, min, max, clamp, rgb, rgba, hsl, hsla, not, is, "
				"where, nth-child and nth-of-type");
	}
	c = plain;
	for (;;) {
		gchar *open, *close;
		c += strspn(c, " \t\r\n");
		if (*c == '\0')
			break;
		open = strchr(c, '{');
		close = open != NULL ? strchr(open + 1, '}') : NULL;
		if (open == NULL || close == NULL || memchr(c, '}', open - c) != NULL || memchr(c, ';', open - c) != NULL)
			return css_refuse(error, id, "must be flat rules: selectors, {, declarations and }");
		if (memchr(open + 1, '{', close - open - 1) != NULL)
			return css_refuse(error, id, "may not nest rules");
		if (!selectors_valid(id, c, open - c)) {
			g_autofree gchar *reason = g_strdup_printf("must start every selector with .podbay-%s", id);
			return css_refuse(error, id, reason);
		}
		rules++;
		c = close + 1;
	}
	if (rules == 0)
		return css_refuse(error, id, "needs at least one rule");
	return TRUE;
}

void
ls_module_strip_markup(JsonObject *plan){
	JsonNode *node;
	JsonArray *modules;
	guint i;
	g_return_if_fail(plan != NULL);
	node = json_object_get_member(plan, "modules");
	if (node == NULL || !JSON_NODE_HOLDS_ARRAY(node))
		return;
	modules = json_node_get_array(node);
	for (i = 0; i < json_array_get_length(modules); i++) {
		JsonNode *entry = json_array_get_element(modules, i);
		if (!JSON_NODE_HOLDS_OBJECT(entry))
			continue;
		if (json_object_has_member(json_node_get_object(entry), "snippet"))
			json_object_remove_member(json_node_get_object(entry), "snippet");
		if (json_object_has_member(json_node_get_object(entry), "css"))
			json_object_remove_member(json_node_get_object(entry), "css");
	}
}
