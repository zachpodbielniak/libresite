/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "core/ls-components.h"
#include "core/ls-template.h"
#include "core/ls-util.h"
#include <math.h>
#include <stdarg.h>
#include <string.h>

GResource *ls_resources_get_resource(void);

#define RESOURCE "/org/libresite/components/"
#define MAX_PROBLEMS 30
#define MAX_PAGES 24
#define MAX_SECTIONS 16

static const gchar *const utility_routes[] = {"404.html", "privacy.html", "terms.html"};

struct _LsComponents {
	GObject parent_instance;
	JsonNode *catalog;
	JsonObject *components;
	JsonObject *kinds;
	JsonObject *looks;
	JsonNode *theme_catalog;
	JsonObject *themes;
	JsonNode *identity_catalog;
	JsonObject *sectors;	/* sector name -> description */
	JsonObject *palettes;
	JsonObject *typefaces;
	JsonObject *homes;
	GPtrArray *order;
	GHashTable *files;	/* resource-relative name -> contents */
	JsonNode *utility[G_N_ELEMENTS(utility_routes)];
	gchar *plan_schema;	/* plan.schema.json with catalog enums filled in */
	gchar *themed_plan_schema;	/* the same without the parts a theme supplies */
	gchar *digest;
};
G_DEFINE_TYPE(LsComponents, ls_components, G_TYPE_OBJECT)

static void
ls_components_finalize(GObject *object){
	LsComponents *self = LS_COMPONENTS(object);
	guint i;
	g_clear_pointer(&self->catalog, json_node_unref);
	g_clear_pointer(&self->order, g_ptr_array_unref);
	g_clear_pointer(&self->files, g_hash_table_unref);
	for (i = 0; i < G_N_ELEMENTS(self->utility); i++)
		g_clear_pointer(&self->utility[i], json_node_unref);
	g_free(self->plan_schema);
	g_free(self->themed_plan_schema);
	g_clear_pointer(&self->theme_catalog, json_node_unref);
	g_clear_pointer(&self->identity_catalog, json_node_unref);
	g_free(self->digest);
	G_OBJECT_CLASS(ls_components_parent_class)->finalize(object);
}

static void
ls_components_class_init(LsComponentsClass *klass){
	G_OBJECT_CLASS(klass)->finalize = ls_components_finalize;
}

static void
ls_components_init(LsComponents *self){
	self->files = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
}

/* ---- Problem collection ------------------------------------------------ */

typedef struct {
	GPtrArray *messages;
} Problems;

static void problem(Problems *problems, const gchar *format, ...) G_GNUC_PRINTF(2, 3);
static void
problem(Problems *problems, const gchar *format, ...){
	va_list args;
	if (problems->messages->len >= MAX_PROBLEMS)
		return;
	va_start(args, format);
	g_ptr_array_add(problems->messages, g_strdup_vprintf(format, args));
	va_end(args);
}

/* Report every collected problem at once so a retry can fix all of them. */
static gboolean
finish(Problems *problems, const gchar *subject, GError **error){
	GString *text;
	guint i;
	if (problems->messages->len == 0)
		return TRUE;
	text = g_string_new(NULL);
	g_string_append_printf(text, "%s has %u problem%s:", subject, problems->messages->len,
		problems->messages->len == 1 ? "" : "s");
	for (i = 0; i < problems->messages->len; i++)
		g_string_append_printf(text, "\n- %s", (const gchar *)g_ptr_array_index(problems->messages, i));
	if (problems->messages->len >= MAX_PROBLEMS)
		g_string_append(text, "\n- (further problems omitted)");
	g_set_error_literal(error, LS_ERROR, LS_ERROR_INVALID, text->str);
	g_string_free(text, TRUE);
	return FALSE;
}

/* ---- Small helpers ------------------------------------------------------ */

static JsonObject *
member_object(JsonObject *object, const gchar *key){
	JsonNode *node = object != NULL ? json_object_get_member(object, key) : NULL;
	return node != NULL && JSON_NODE_HOLDS_OBJECT(node) ? json_node_get_object(node) : NULL;
}

static JsonArray *
member_array(JsonObject *object, const gchar *key){
	JsonNode *node = object != NULL ? json_object_get_member(object, key) : NULL;
	return node != NULL && JSON_NODE_HOLDS_ARRAY(node) ? json_node_get_array(node) : NULL;
}

static gboolean
is_string(JsonNode *node){
	return node != NULL && JSON_NODE_HOLDS_VALUE(node) && json_node_get_value_type(node) == G_TYPE_STRING;
}

static gboolean
is_number(JsonNode *node){
	return node != NULL && JSON_NODE_HOLDS_VALUE(node) &&
		(json_node_get_value_type(node) == G_TYPE_INT64 || json_node_get_value_type(node) == G_TYPE_DOUBLE);
}

static gboolean
blank(const gchar *text){
	const gchar *p;
	for (p = text; p != NULL && *p != '\0'; p++)
		if (!g_ascii_isspace(*p))
			return FALSE;
	return TRUE;
}

/* ASCII route/anchor slug; the same derivation is used by checks and output. */
static gchar *
slugify(const gchar *text, gsize limit){
	g_autofree gchar *ascii = g_str_to_ascii(text != NULL ? text : "", "C");
	GString *out = g_string_new(NULL);
	const gchar *p;
	for (p = ascii; *p != '\0' && out->len < limit; p++) {
		if (g_ascii_isalnum(*p))
			g_string_append_c(out, g_ascii_tolower(*p));
		else if (out->len > 0 && out->str[out->len - 1] != '-')
			g_string_append_c(out, '-');
	}
	while (out->len > 0 && out->str[out->len - 1] == '-')
		g_string_truncate(out, out->len - 1);
	return g_string_free(out, FALSE);
}

static gboolean
anchor_valid(const gchar *id){
	const gchar *p;
	if (id == NULL || !g_ascii_isalpha(*id) || strlen(id) > 64)
		return FALSE;
	for (p = id; *p != '\0'; p++)
		if (!g_ascii_isalnum(*p) && *p != '-' && *p != '_')
			return FALSE;
	return TRUE;
}

/* Flat lowercase routes keep every relative link valid from every page. */
static gboolean
route_valid(const gchar *path){
	const gchar *p;
	if (path == NULL || !g_str_has_suffix(path, ".html") || strlen(path) < 6 || strlen(path) > 64)
		return FALSE;
	for (p = path; p < path + strlen(path) - 5; p++)
		if (!(g_ascii_islower(*p) || g_ascii_isdigit(*p) || (*p == '-' && p != path)))
			return FALSE;
	return TRUE;
}

static gboolean
art_path_valid(const gchar *path){
	const gchar *p;
	if (path == NULL || !g_str_has_prefix(path, "assets/") || !g_str_has_suffix(path, ".svg") ||
		strlen(path) < 12 || strlen(path) > 72)
		return FALSE;
	for (p = path + 7; p < path + strlen(path) - 4; p++)
		if (!(g_ascii_islower(*p) || g_ascii_isdigit(*p) || (*p == '-' && p != path + 7)))
			return FALSE;
	return TRUE;
}

static gboolean
is_utility(const gchar *path){
	guint i;
	for (i = 0; i < G_N_ELEMENTS(utility_routes); i++)
		if (g_strcmp0(path, utility_routes[i]) == 0)
			return TRUE;
	return FALSE;
}

/* ---- Colors ------------------------------------------------------------- */

static gboolean
parse_color(const gchar *text, gdouble rgb[3]){
	guint i, len;
	gchar pair[3] = {0, 0, 0};
	if (text == NULL || text[0] != '#')
		return FALSE;
	len = (guint)strlen(text + 1);
	if (len != 3 && len != 6)
		return FALSE;
	for (i = 1; i <= len; i++)
		if (!g_ascii_isxdigit(text[i]))
			return FALSE;
	for (i = 0; i < 3; i++) {
		if (len == 3) {
			pair[0] = text[1 + i];
			pair[1] = text[1 + i];
		} else {
			pair[0] = text[1 + i * 2];
			pair[1] = text[2 + i * 2];
		}
		rgb[i] = (gdouble)g_ascii_strtoull(pair, NULL, 16) / 255.0;
	}
	return TRUE;
}

static gdouble
luminance(const gdouble rgb[3]){
	gdouble linear[3];
	guint i;
	for (i = 0; i < 3; i++)
		linear[i] = rgb[i] <= 0.04045 ? rgb[i] / 12.92 : pow((rgb[i] + 0.055) / 1.055, 2.4);
	return 0.2126 * linear[0] + 0.7152 * linear[1] + 0.0722 * linear[2];
}

static gdouble
contrast(const gchar *a, const gchar *b){
	gdouble x[3], y[3], la, lb;
	if (!parse_color(a, x) || !parse_color(b, y))
		return 0;
	la = luminance(x);
	lb = luminance(y);
	return (MAX(la, lb) + 0.05) / (MIN(la, lb) + 0.05);
}

/* ---- Links -------------------------------------------------------------- */

typedef struct {
	GHashTable *routes;	/* planned and utility routes */
	GHashTable *art;	/* planned art paths */
	GHashTable *anchors;	/* ids on the page being checked; NULL skips anchor checks */
} LinkScope;

/* Returns NULL when @href is acceptable, otherwise a reason for the model. */
static gchar *
href_problem(LinkScope *scope, const gchar *href){
	const gchar *hash;
	g_autofree gchar *route = NULL;
	if (href == NULL || *href == '\0')
		return g_strdup("href is empty");
	if (strpbrk(href, " \t\n\"'<>`\\") != NULL)
		return g_strdup_printf("href '%s' contains spaces, quotes or angle brackets", href);
	if (href[0] == '#') {
		if (!anchor_valid(href + 1))
			return g_strdup_printf("anchor '%s' is not a valid id", href);
		if (scope != NULL && scope->anchors != NULL && !g_hash_table_contains(scope->anchors, href + 1))
			return g_strdup_printf("anchor '%s' matches no section or part id on this page", href);
		return NULL;
	}
	if (g_str_has_prefix(href, "mailto:"))
		return strchr(href, '@') != NULL && strlen(href) > 10 ? NULL
			: g_strdup_printf("'%s' is not a mailto: address", href);
	if (g_str_has_prefix(href, "tel:")) {
		const gchar *p;
		for (p = href + 4; *p != '\0'; p++)
			if (!g_ascii_isdigit(*p) && strchr("+-.() ", *p) == NULL)
				return g_strdup_printf("'%s' is not a tel: number", href);
		return strlen(href) > 6 ? NULL : g_strdup_printf("'%s' is not a tel: number", href);
	}
	if (g_str_has_prefix(href, "https://"))
		return strlen(href) > 9 && href[8] != '/' && href[8] != '.' ? NULL
			: g_strdup_printf("'%s' is not an https:// URL", href);
	if (g_str_has_prefix(href, "http://"))
		return g_strdup_printf("'%s' uses http://; link with https://", href);
	hash = strchr(href, '#');
	route = hash != NULL ? g_strndup(href, (gsize)(hash - href)) : g_strdup(href);
	if (hash != NULL && !anchor_valid(hash + 1))
		return g_strdup_printf("anchor in '%s' is not a valid id", href);
	if (art_path_valid(route))
		return scope == NULL || g_hash_table_contains(scope->art, route) ? NULL
			: g_strdup_printf("'%s' is not planned art", route);
	if (!route_valid(route))
		return g_strdup_printf("'%s' is not a link libresite can publish: use a planned page such as "
			"work.html, #anchor, mailto:, tel: or https://", href);
	if (scope != NULL && !g_hash_table_contains(scope->routes, route))
		return g_strdup_printf("'%s' links to a page that is not in the plan", route);
	return NULL;
}

/* ---- Rich text ---------------------------------------------------------- */

typedef struct {
	LinkScope *scope;
	GString *why;	/* first link problem */
} RichState;

static void
append_escaped(GString *out, const gchar *text, gsize length){
	gsize i;
	for (i = 0; i < length; i++) {
		switch (text[i]) {
		case '&': g_string_append(out, "&amp;"); break;
		case '<': g_string_append(out, "&lt;"); break;
		case '>': g_string_append(out, "&gt;"); break;
		case '"': g_string_append(out, "&quot;"); break;
		case '\'': g_string_append(out, "&#39;"); break;
		default: g_string_append_c(out, text[i]);
		}
	}
}

static const gchar *
find(const gchar *start, const gchar *end, const gchar *needle){
	gsize n = strlen(needle);
	const gchar *p;
	for (p = start; p + n <= end; p++)
		if (memcmp(p, needle, n) == 0)
			return p;
	return NULL;
}

static void
rich_inline(RichState *state, const gchar *s, const gchar *end, GString *out){
	while (s < end) {
		const gchar *close;
		if (*s == '`' && (close = find(s + 1, end, "`")) != NULL && close > s + 1) {
			g_string_append(out, "<code>");
			append_escaped(out, s + 1, (gsize)(close - s - 1));
			g_string_append(out, "</code>");
			s = close + 1;
		} else if (*s == '*' && s + 1 < end && s[1] == '*' &&
			(close = find(s + 2, end, "**")) != NULL && close > s + 2) {
			g_string_append(out, "<strong>");
			rich_inline(state, s + 2, close, out);
			g_string_append(out, "</strong>");
			s = close + 2;
		} else if (*s == '*' && s + 1 < end && !g_ascii_isspace(s[1]) &&
			(close = find(s + 1, end, "*")) != NULL && close > s + 1 && close[-1] != ' ') {
			g_string_append(out, "<em>");
			rich_inline(state, s + 1, close, out);
			g_string_append(out, "</em>");
			s = close + 1;
		} else if (*s == '[') {
			const gchar *middle = find(s + 1, end, "](");
			const gchar *stop = middle != NULL ? find(middle + 2, end, ")") : NULL;
			if (middle != NULL && stop != NULL && middle > s + 1 && stop > middle + 2) {
				g_autofree gchar *href = g_strndup(middle + 2, (gsize)(stop - middle - 2));
				g_autofree gchar *why = href_problem(state->scope, href);
				if (why != NULL && state->why->len == 0)
					g_string_assign(state->why, why);
				g_string_append(out, "<a href=\"");
				append_escaped(out, href, strlen(href));
				g_string_append(out, g_str_has_prefix(href, "https://") ? "\" rel=\"noopener\">" : "\">");
				rich_inline(state, s + 1, middle, out);
				g_string_append(out, "</a>");
				s = stop + 1;
			} else {
				g_string_append_c(out, '[');
				s++;
			}
		} else {
			append_escaped(out, s, 1);
			s++;
		}
	}
}

typedef enum { LINE_TEXT, LINE_BULLET, LINE_NUMBER } LineKind;

static LineKind
line_kind(const gchar *line, const gchar **content){
	const gchar *p = line;
	if ((line[0] == '-' || line[0] == '*') && line[1] == ' ') {
		*content = line + 2;
		return LINE_BULLET;
	}
	while (g_ascii_isdigit(*p))
		p++;
	if (p > line && p[0] == '.' && p[1] == ' ') {
		*content = p + 2;
		return LINE_NUMBER;
	}
	*content = line;
	return LINE_TEXT;
}

/* Paragraphs, bullet and numbered lists; everything else is escaped text. */
static gchar *
rich_render(RichState *state, const gchar *text){
	g_auto(GStrv) lines = NULL;
	GString *out = g_string_new(NULL);
	GString *paragraph = g_string_new(NULL);
	LineKind open = LINE_TEXT;
	gboolean in_list = FALSE;
	guint i;
	g_autofree gchar *normal = g_strdup(text);
	gchar *p;
	for (p = normal; *p != '\0'; p++)
		if (*p == '\r')
			*p = '\n';
	lines = g_strsplit(normal, "\n", -1);
	for (i = 0; lines[i] != NULL; i++) {
		const gchar *content;
		gchar *line = g_strstrip(lines[i]);
		LineKind kind = line_kind(line, &content);
		if (*line == '\0' || kind != LINE_TEXT) {
			if (paragraph->len > 0) {
				g_string_append(out, "<p>");
				rich_inline(state, paragraph->str, paragraph->str + paragraph->len, out);
				g_string_append(out, "</p>\n");
				g_string_truncate(paragraph, 0);
			}
		}
		if (in_list && (*line == '\0' || kind != open)) {
			g_string_append(out, open == LINE_BULLET ? "</ul>\n" : "</ol>\n");
			in_list = FALSE;
		}
		if (*line == '\0')
			continue;
		if (kind == LINE_TEXT) {
			if (paragraph->len > 0)
				g_string_append_c(paragraph, ' ');
			g_string_append(paragraph, line);
			continue;
		}
		if (!in_list) {
			g_string_append(out, kind == LINE_BULLET ? "<ul>" : "<ol>");
			in_list = TRUE;
			open = kind;
		}
		g_string_append(out, "<li>");
		rich_inline(state, content, content + strlen(content), out);
		g_string_append(out, "</li>");
	}
	if (paragraph->len > 0) {
		g_string_append(out, "<p>");
		rich_inline(state, paragraph->str, paragraph->str + paragraph->len, out);
		g_string_append(out, "</p>\n");
	}
	if (in_list)
		g_string_append(out, open == LINE_BULLET ? "</ul>\n" : "</ol>\n");
	g_string_free(paragraph, TRUE);
	return g_string_free(out, FALSE);
}

gchar *
ls_components_render_rich(const gchar *text, GError **error){
	RichState state = {NULL, g_string_new(NULL)};
	gchar *html;
	g_return_val_if_fail(text != NULL, NULL);
	html = rich_render(&state, text);
	if (state.why->len > 0) {
		g_set_error_literal(error, LS_ERROR, LS_ERROR_INVALID, state.why->str);
		g_free(html);
		html = NULL;
	}
	g_string_free(state.why, TRUE);
	return html;
}

/* ---- Markup and CSS safety --------------------------------------------- */

typedef struct {
	LinkScope *scope;
	GString *why;
	gboolean svg;		/* standalone artwork rather than a page fragment */
	guint depth;
	gboolean root_ok;
} MarkupCheck;

static const gchar *const forbidden_elements[] = {"script", "style", "iframe", "object", "embed",
	"form", "input", "button", "textarea", "select", "link", "meta", "base", "foreignobject",
	"frame", "frameset", "applet", "audio", "video", "source", "track", "image", "template",
	"slot", "portal", "noscript", "h1", "main", "dialog", "a", NULL};

static void
markup_start(GMarkupParseContext *context, const gchar *element, const gchar **names,
	const gchar **values, gpointer data, GError **error){
	MarkupCheck *check = data;
	g_autofree gchar *lower = g_ascii_strdown(element, -1);
	const gchar *local = strchr(lower, ':') != NULL ? strchr(lower, ':') + 1 : lower;
	guint i;
	(void)context;
	(void)error;
	if (check->why->len > 0)
		return;
	if (check->depth == 0 && check->svg) {
		gboolean xmlns = FALSE, viewbox = FALSE;
		for (i = 0; names[i] != NULL; i++) {
			xmlns |= g_str_equal(names[i], "xmlns") && g_str_equal(values[i], "http://www.w3.org/2000/svg");
			viewbox |= g_str_equal(names[i], "viewBox");
		}
		if (!g_str_equal(lower, "svg"))
			g_string_assign(check->why, "the root element must be <svg>");
		else if (!xmlns)
			g_string_assign(check->why, "the root <svg> needs xmlns=\"http://www.w3.org/2000/svg\"");
		else if (!viewbox)
			g_string_assign(check->why, "the root <svg> needs a viewBox");
		check->root_ok = check->why->len == 0;
	}
	check->depth++;
	for (i = 0; forbidden_elements[i] != NULL; i++) {
		/* Page fragments may link with <a>; standalone art may not link out. */
		if (g_str_equal(forbidden_elements[i], "a") && !check->svg)
			continue;
		if (g_str_equal(local, forbidden_elements[i])) {
			g_string_printf(check->why, "<%s> is not allowed", element);
			return;
		}
	}
	for (i = 0; names[i] != NULL; i++) {
		g_autofree gchar *name = g_ascii_strdown(names[i], -1);
		const gchar *value = values[i];
		if (g_str_has_prefix(name, "on")) {
			g_string_printf(check->why, "event handler attribute %s is not allowed", names[i]);
			return;
		}
		if (g_str_equal(name, "style") && !check->svg) {
			g_string_assign(check->why, "style attributes are not allowed; use the page css");
			return;
		}
		if (g_str_equal(name, "href") || g_str_equal(name, "xlink:href") || g_str_equal(name, "src")) {
			g_autofree gchar *why = NULL;
			if (check->svg) {
				if (value[0] != '#')
					g_string_printf(check->why, "%s=\"%s\" must reference an id inside the artwork", names[i], value);
			} else if ((why = href_problem(check->scope, value)) != NULL)
				g_string_assign(check->why, why);
			if (check->why->len > 0)
				return;
		}
		if (g_str_equal(name, "srcset") || g_str_equal(name, "action") || g_str_equal(name, "formaction") ||
			g_str_equal(name, "poster") || g_str_equal(name, "background")) {
			g_string_printf(check->why, "attribute %s is not allowed", names[i]);
			return;
		}
		if (strstr(value, "javascript:") != NULL || strstr(value, "url(") != NULL) {
			const gchar *url = strstr(value, "url(");
			if (strstr(value, "javascript:") != NULL || (url != NULL && url[4] != '#' &&
				!(url[4] == '\'' && url[5] == '#') && !(url[4] == '"' && url[5] == '#'))) {
				g_string_printf(check->why, "attribute %s references an external or script URL", names[i]);
				return;
			}
		}
	}
}

static void
markup_end(GMarkupParseContext *context, const gchar *element, gpointer data, GError **error){
	MarkupCheck *check = data;
	(void)context;
	(void)element;
	(void)error;
	check->depth--;
}

static gboolean
check_markup(const gchar *text, gboolean svg, LinkScope *scope, GString *why){
	const GMarkupParser parser = {markup_start, markup_end, NULL, NULL, NULL};
	MarkupCheck check = {scope, why, svg, 0, FALSE};
	g_autoptr(GMarkupParseContext) context = g_markup_parse_context_new(&parser,
		G_MARKUP_TREAT_CDATA_AS_TEXT, &check, NULL);
	g_autoptr(GError) parse_error = NULL;
	g_autofree gchar *document = svg ? g_strdup(text) : g_strconcat("<div>", text, "</div>", NULL);
	const gchar *start = document;
	/* GMarkup rejects prologs; artwork may legitimately begin with one. */
	if (svg && g_str_has_prefix(start, "<?xml")) {
		const gchar *stop = strstr(start, "?>");
		start = stop != NULL ? stop + 2 : start;
	}
	if (svg && strstr(start, "<!DOCTYPE") != NULL) {
		g_string_assign(why, "a DOCTYPE is not allowed");
		return FALSE;
	}
	if (!g_markup_parse_context_parse(context, start, -1, &parse_error) ||
		!g_markup_parse_context_end_parse(context, &parse_error)) {
		if (why->len == 0)
			g_string_printf(why, "not well-formed XML (%s); close every element and use only &amp;, "
				"&lt;, &gt;, &quot;, &apos; or numeric entities", parse_error->message);
		return FALSE;
	}
	if (svg && !check.root_ok && why->len == 0)
		g_string_assign(why, "the root element must be <svg>");
	return why->len == 0;
}

gboolean
ls_components_check_art(const gchar *svg, GError **error){
	g_autoptr(GString) why = g_string_new(NULL);
	g_return_val_if_fail(svg != NULL, FALSE);
	if (strlen(svg) > 256 * 1024)
		g_string_assign(why, "artwork exceeds 256 KiB");
	else if (!g_utf8_validate(svg, -1, NULL))
		g_string_assign(why, "artwork is not UTF-8");
	else if (strstr(svg, "@import") != NULL || strstr(svg, "<style") != NULL)
		g_string_assign(why, "artwork must not embed stylesheets");
	else
		check_markup(svg, TRUE, NULL, why);
	if (why->len == 0)
		return TRUE;
	g_set_error(error, LS_ERROR, LS_ERROR_INVALID, "Artwork rejected: %s", why->str);
	return FALSE;
}

/* Site and page CSS is model output inserted into a stylesheet or <style>. */
static gchar *
css_problem(const gchar *css, gsize limit){
	const gchar *banned[] = {"</", "<!--", "@import", "expression(", "javascript:", "behavior:",
		"-moz-binding", "@charset", "@namespace"};
	const gchar *p;
	gint depth = 0;
	guint i;
	if (strlen(css) > limit)
		return g_strdup_printf("css exceeds %" G_GSIZE_FORMAT " bytes", limit);
	for (i = 0; i < G_N_ELEMENTS(banned); i++)
		if (strstr(css, banned[i]) != NULL)
			return g_strdup_printf("css must not contain '%s'", banned[i]);
	for (p = strstr(css, "url("); p != NULL; p = strstr(p + 4, "url(")) {
		const gchar *target = p + 4;
		while (*target == ' ' || *target == '"' || *target == '\'')
			target++;
		if (!g_str_has_prefix(target, "assets/") && *target != '#')
			return g_strdup("css url() may only reference assets/ or #ids");
	}
	for (p = css; *p != '\0'; p++) {
		if (*p == '{')
			depth++;
		else if (*p == '}' && --depth < 0)
			break;
	}
	if (depth != 0)
		return g_strdup("css has unbalanced braces");
	return NULL;
}

/* ---- Catalog loading ----------------------------------------------------- */

static const gchar *const slot_types[] = {"text", "rich", "link", "links", "image", "items",
	"fields", "choice", "flag", "html", NULL};

static gboolean
catalog_invalid(GError **error, const gchar *component, const gchar *detail){
	g_set_error(error, LS_ERROR, LS_ERROR_INVALID, "Component catalog: %s: %s", component, detail);
	return FALSE;
}

static gboolean
check_slot_definitions(JsonObject *slots, const gchar *component, GError **error){
	g_autoptr(GList) names = json_object_get_members(slots);
	GList *item;
	for (item = names; item != NULL; item = item->next) {
		JsonObject *slot = member_object(slots, item->data);
		const gchar *type = slot != NULL ? ls_string(slot, "type") : NULL;
		if (slot == NULL || type == NULL || !g_strv_contains(slot_types, type) ||
			!ls_object_keys(slot, "|type|required|max|min_items|max_items|values|fields|note|", NULL))
			return catalog_invalid(error, component, "each slot needs a known type and known keys");
		if (g_str_equal(type, "items") && (member_object(slot, "fields") == NULL ||
			!check_slot_definitions(member_object(slot, "fields"), component, error)))
			return error == NULL || *error != NULL ? FALSE
				: catalog_invalid(error, component, "items slots need fields");
		if (g_str_equal(type, "choice") && member_array(slot, "values") == NULL)
			return catalog_invalid(error, component, "choice slots need values");
	}
	return TRUE;
}

static gchar *
load_resource(LsComponents *self, GChecksum *digest, const gchar *name, gboolean required, GError **error){
	g_autofree gchar *path = g_strconcat(RESOURCE, name, NULL);
	g_autoptr(GError) local_error = NULL;
	g_autoptr(GBytes) bytes = g_resource_lookup_data(ls_resources_get_resource(), path, 0, &local_error);
	gchar *text;
	if (bytes == NULL) {
		if (required)
			g_propagate_error(error, g_steal_pointer(&local_error));
		return NULL;
	}
	text = g_strndup(g_bytes_get_data(bytes, NULL), g_bytes_get_size(bytes));
	g_checksum_update(digest, (const guchar *)name, -1);
	g_checksum_update(digest, (const guchar *)"\n", 1);
	g_checksum_update(digest, g_bytes_get_data(bytes, NULL), (gssize)g_bytes_get_size(bytes));
	g_hash_table_replace(self->files, g_strdup(name), g_strdup(text));
	return text;
}

static const gchar *
file_text(LsComponents *self, const gchar *name){
	return g_hash_table_lookup(self->files, name);
}

/* Replace the design half of @plan (tokens, fonts, look, css, restyle) with @theme's. */
static void
copy_theme(JsonObject *theme, JsonObject *plan){
	const gchar *parts[] = {"tokens", "fonts", "look", "css", "restyle"};
	guint i;
	for (i = 0; i < G_N_ELEMENTS(parts); i++) {
		JsonNode *value = json_object_get_member(theme, parts[i]);
		if (json_object_has_member(plan, parts[i]))
			json_object_remove_member(plan, parts[i]);
		if (value != NULL && !JSON_NODE_HOLDS_NULL(value)) {
			/* Deep copy: repair edits plan tokens and must never edit the theme. */
			g_autofree gchar *encoded = ls_json_encode(value);
			json_object_set_member(plan, parts[i], ls_json_parse(encoded, NULL));
		}
	}
}

static void check_plan_into(Problems *problems, LsComponents *self, JsonNode *node);
static gboolean load_themes(LsComponents *self, GChecksum *digest, GError **error);
static gboolean load_identities(LsComponents *self, GChecksum *digest, GError **error);

/* Every look axis lists named values with a description; the first is the default. */
static gboolean
check_look_definitions(JsonObject *looks, GError **error){
	g_autoptr(GList) axes = json_object_get_members(looks);
	GList *axis;
	if (axes == NULL)
		return catalog_invalid(error, "looks", "needs at least one axis");
	for (axis = axes; axis != NULL; axis = axis->next) {
		JsonObject *definition = member_object(looks, axis->data);
		JsonObject *values = member_object(definition, "values");
		g_autoptr(GList) names = NULL;
		GList *name;
		if (definition == NULL || !ls_object_keys(definition, "|summary|values|", NULL) ||
			ls_string(definition, "summary") == NULL || values == NULL || json_object_get_size(values) < 2)
			return catalog_invalid(error, axis->data, "look axes need a summary and two or more values");
		names = json_object_get_members(values);
		for (name = names; name != NULL; name = name->next) {
			const gchar *p;
			if (ls_string(values, name->data) == NULL)
				return catalog_invalid(error, axis->data, "look values need a description");
			for (p = name->data; *p != '\0'; p++)
				if (!g_ascii_islower(*p) && *p != '-')
					return catalog_invalid(error, axis->data, "look values are lowercase words");
		}
	}
	return TRUE;
}

static const gchar *
look_default(LsComponents *self, const gchar *axis){
	JsonObject *values = member_object(member_object(self->looks, axis), "values");
	g_autoptr(GList) names = json_object_get_members(values);
	return names != NULL ? names->data : NULL;
}

/* The plan schema offers exactly the catalog's plannable kinds and look values. */
static gchar *
build_plan_schema(LsComponents *self, GError **error){
	g_autoptr(JsonNode) schema = ls_json_parse(file_text(self, "plan.schema.json"), error);
	JsonObject *properties, *page_properties, *look;
	JsonArray *kinds = json_array_new();
	JsonArray *required = json_array_new();
	JsonObject *look_properties = json_object_new();
	g_autoptr(GList) names = NULL;
	GList *item;
	if (schema == NULL || !JSON_NODE_HOLDS_OBJECT(schema)) {
		json_array_unref(kinds);
		json_array_unref(required);
		json_object_unref(look_properties);
		if (error != NULL && *error == NULL)
			catalog_invalid(error, "plan.schema.json", "must be a JSON object");
		return NULL;
	}
	properties = member_object(json_node_get_object(schema), "properties");
	page_properties = member_object(member_object(member_object(properties, "pages"), "items"), "properties");
	names = json_object_get_members(self->kinds);
	for (item = names; item != NULL; item = item->next)
		if (!json_object_has_member(member_object(self->kinds, item->data), "builtin"))
			json_array_add_string_element(kinds, item->data);
	json_object_set_array_member(member_object(page_properties, "kind"), "enum", kinds);
	g_list_free(g_steal_pointer(&names));
	names = json_object_get_members(self->looks);
	for (item = names; item != NULL; item = item->next) {
		JsonObject *axis = json_object_new();
		JsonArray *values = json_array_new();
		g_autoptr(GList) value_names = json_object_get_members(member_object(member_object(self->looks, item->data), "values"));
		GList *value;
		for (value = value_names; value != NULL; value = value->next)
			json_array_add_string_element(values, value->data);
		json_object_set_string_member(axis, "type", "string");
		json_object_set_array_member(axis, "enum", values);
		json_object_set_object_member(look_properties, item->data, axis);
		json_array_add_string_element(required, item->data);
	}
	look = member_object(properties, "look");
	if (look == NULL) {
		json_array_unref(required);
		json_object_unref(look_properties);
		return catalog_invalid(error, "plan.schema.json", "needs a look property"), NULL;
	}
	json_object_set_object_member(look, "properties", look_properties);
	json_object_set_array_member(look, "required", required);
	{
		/* A themed plan leaves the design system to the theme. */
		g_autofree gchar *encoded = ls_json_encode(schema);
		g_autoptr(JsonNode) themed = ls_json_parse(encoded, NULL);	/* deep: json_node_copy shares objects */
		JsonObject *root = json_node_get_object(themed);
		JsonObject *themed_properties = member_object(root, "properties");
		JsonArray *old_required = member_array(root, "required");
		JsonArray *new_required = json_array_new();
		const gchar *supplied[] = {"tokens", "fonts", "look", "css", "restyle"};
		guint i, j;
		for (i = 0; i < G_N_ELEMENTS(supplied); i++)
			json_object_remove_member(themed_properties, supplied[i]);
		for (i = 0; old_required != NULL && i < json_array_get_length(old_required); i++) {
			const gchar *key = json_array_get_string_element(old_required, i);
			gboolean keep = TRUE;
			for (j = 0; j < G_N_ELEMENTS(supplied); j++)
				keep &= g_strcmp0(key, supplied[j]) != 0;
			if (keep)
				json_array_add_string_element(new_required, key);
		}
		json_object_set_array_member(root, "required", new_required);
		self->themed_plan_schema = ls_json_encode(themed);
	}
	return ls_json_encode(schema);
}

LsComponents *
ls_components_new(GError **error){
	g_autoptr(LsComponents) self = g_object_new(LS_TYPE_COMPONENTS, NULL);
	g_autoptr(GChecksum) digest = g_checksum_new(G_CHECKSUM_SHA256);
	g_autofree gchar *catalog = NULL;
	const gchar *shared[] = {"layout.html", "section.html", "base.css", "look.css", "nav.js", "plan.schema.json"};
	g_autoptr(GList) names = NULL;
	JsonObject *root;
	GList *item;
	guint i;
	catalog = load_resource(self, digest, "catalog.yaml", TRUE, error);
	if (catalog == NULL)
		return NULL;
	self->catalog = ls_yaml_parse(catalog, error);
	if (self->catalog == NULL)
		return NULL;
	root = JSON_NODE_HOLDS_OBJECT(self->catalog) ? json_node_get_object(self->catalog) : NULL;
	if (root == NULL || !ls_object_keys(root, "|version|looks|kinds|components|", error) ||
		(self->components = member_object(root, "components")) == NULL ||
		(self->kinds = member_object(root, "kinds")) == NULL ||
		(self->looks = member_object(root, "looks")) == NULL) {
		if (error != NULL && *error == NULL)
			catalog_invalid(error, "catalog.yaml", "needs version, looks, kinds and components");
		return NULL;
	}
	if (!check_look_definitions(self->looks, error))
		return NULL;
	for (i = 0; i < G_N_ELEMENTS(shared); i++) {
		g_autofree gchar *text = load_resource(self, digest, shared[i], TRUE, error);
		if (text == NULL)
			return NULL;
	}
	self->order = g_ptr_array_new_with_free_func(g_free);
	names = json_object_get_members(self->components);
	for (item = names; item != NULL; item = item->next) {
		const gchar *name = item->data;
		JsonObject *component = member_object(self->components, name);
		g_autofree gchar *file = g_strconcat(name, ".html", NULL);
		g_autofree gchar *css = g_strconcat(name, ".css", NULL);
		g_autofree gchar *js = g_strconcat(name, ".js", NULL);
		g_autofree gchar *text = NULL, *style = NULL, *script = NULL;
		if (component == NULL || !ls_object_keys(component, "|summary|variants|slots|", NULL) ||
			ls_string(component, "summary") == NULL || member_object(component, "variants") == NULL ||
			json_object_get_size(member_object(component, "variants")) == 0 ||
			member_object(component, "slots") == NULL)
			return catalog_invalid(error, name, "needs summary, variants and slots"), NULL;
		if (!check_slot_definitions(member_object(component, "slots"), name, error))
			return NULL;
		text = load_resource(self, digest, file, TRUE, error);
		if (text == NULL)
			return NULL;
		style = load_resource(self, digest, css, FALSE, NULL);
		script = load_resource(self, digest, js, FALSE, NULL);
		g_ptr_array_add(self->order, g_strdup(name));
	}
	/* Page kinds name only real components; home is the one hero kind. */
	{
		g_autoptr(GList) kind_names = json_object_get_members(self->kinds);
		for (item = kind_names; item != NULL; item = item->next) {
			JsonObject *kind = member_object(self->kinds, item->data);
			JsonArray *lists[2];
			guint l, j;
			if (kind == NULL || !ls_object_keys(kind, "|summary|openings|suggested|sidebar|builtin|", NULL) ||
				ls_string(kind, "summary") == NULL || member_array(kind, "openings") == NULL ||
				json_array_get_length(member_array(kind, "openings")) == 0)
				return catalog_invalid(error, item->data, "kinds need a summary and openings"), NULL;
			lists[0] = member_array(kind, "openings");
			lists[1] = member_array(kind, "suggested");
			for (l = 0; l < 2; l++)
				for (j = 0; lists[l] != NULL && j < json_array_get_length(lists[l]); j++)
					if (member_object(self->components, json_array_get_string_element(lists[l], j)) == NULL)
						return catalog_invalid(error, item->data, "kinds may only name catalog components"), NULL;
		}
		if (member_object(self->kinds, "home") == NULL || member_object(self->kinds, "utility") == NULL)
			return catalog_invalid(error, "kinds", "home and utility kinds are required"), NULL;
	}
	for (i = 0; i < G_N_ELEMENTS(utility_routes); i++) {
		g_autofree gchar *file = g_strconcat("pages/", utility_routes[i], NULL);
		g_autofree gchar *json = NULL;
		file[strlen(file) - 5] = '\0';
		{
			g_autofree gchar *name = g_strconcat(file, ".json", NULL);
			json = load_resource(self, digest, name, TRUE, error);
		}
		if (json == NULL || (self->utility[i] = ls_json_parse(json, error)) == NULL)
			return NULL;
	}
	self->plan_schema = build_plan_schema(self, error);
	if (self->plan_schema == NULL || !load_themes(self, digest, error) ||
		!load_identities(self, digest, error))
		return NULL;
	self->digest = g_strdup(g_checksum_get_string(digest));
	return (LsComponents *)g_steal_pointer(&self);
}

const gchar *
ls_components_get_digest(LsComponents *self){
	g_return_val_if_fail(LS_IS_COMPONENTS(self), NULL);
	return self->digest;
}


/* Hash @seed with @salt into [0, n). */
static guint
seed_pick(const gchar *seed, const gchar *salt, guint n){
	g_autoptr(GChecksum) checksum = g_checksum_new(G_CHECKSUM_SHA256);
	guint8 bytes[32];
	gsize length = sizeof bytes;
	g_checksum_update(checksum, (const guchar *)seed, -1);
	g_checksum_update(checksum, (const guchar *)"\n", 1);
	g_checksum_update(checksum, (const guchar *)salt, -1);
	g_checksum_get_digest(checksum, bytes, &length);
	return n == 0 ? 0 : ((guint32)bytes[0] << 24 | (guint32)bytes[1] << 16 | (guint32)bytes[2] << 8 | bytes[3]) % n;
}

gchar *
ls_components_suggest_opening(LsComponents *self, const gchar *seed){
	JsonArray *openings;
	g_autoptr(GPtrArray) choices = g_ptr_array_new_with_free_func(g_free);
	guint i;
	g_return_val_if_fail(LS_IS_COMPONENTS(self), NULL);
	g_return_val_if_fail(seed != NULL, NULL);
	openings = member_array(member_object(self->kinds, "home"), "openings");
	for (i = 0; openings != NULL && i < json_array_get_length(openings); i++) {
		const gchar *name = json_array_get_string_element(openings, i);
		g_autoptr(GList) variants = NULL;
		GList *variant;
		if (g_str_equal(name, "custom"))
			continue;
		variants = json_object_get_members(member_object(member_object(self->components, name), "variants"));
		for (variant = variants; variant != NULL; variant = variant->next)
			g_ptr_array_add(choices, g_strdup_printf("%s/%s", name, (const gchar *)variant->data));
	}
	if (choices->len == 0)
		return NULL;
	return g_strdup(g_ptr_array_index(choices, seed_pick(seed, "opening", choices->len)));
}

JsonObject *
ls_components_suggest_look(LsComponents *self, const gchar *seed){
	JsonObject *look;
	g_autoptr(GList) axes = NULL;
	GList *axis;
	g_return_val_if_fail(LS_IS_COMPONENTS(self), NULL);
	g_return_val_if_fail(seed != NULL, NULL);
	look = json_object_new();
	axes = json_object_get_members(self->looks);
	for (axis = axes; axis != NULL; axis = axis->next) {
		JsonObject *values = member_object(member_object(self->looks, axis->data), "values");
		g_autoptr(GList) names = json_object_get_members(values);
		g_autoptr(GChecksum) checksum = g_checksum_new(G_CHECKSUM_SHA256);
		guint8 bytes[32];
		gsize length = sizeof bytes;
		guint32 pick;
		g_checksum_update(checksum, (const guchar *)seed, -1);
		g_checksum_update(checksum, (const guchar *)"\n", 1);
		g_checksum_update(checksum, (const guchar *)axis->data, -1);
		g_checksum_get_digest(checksum, bytes, &length);
		pick = ((guint32)bytes[0] << 24 | (guint32)bytes[1] << 16 | (guint32)bytes[2] << 8 | bytes[3]) %
			g_list_length(names);
		json_object_set_string_member(look, axis->data, g_list_nth_data(names, pick));
	}
	return look;
}

const gchar *
ls_components_get_plan_schema(LsComponents *self){
	g_return_val_if_fail(LS_IS_COMPONENTS(self), NULL);
	return self->plan_schema;
}

/* Content stays a free object: per-slot rules are checked natively with
 * precise messages, which keeps provider-side schema rejections rare. */
gchar *
ls_components_dup_page_schema(LsComponents *self){
	GString *names = g_string_new(NULL);
	gchar *schema;
	guint i;
	g_return_val_if_fail(LS_IS_COMPONENTS(self), NULL);
	for (i = 0; i < self->order->len; i++)
		g_string_append_printf(names, "%s\"%s\"", i == 0 ? "" : ", ", (const gchar *)g_ptr_array_index(self->order, i));
	schema = g_strdup_printf(
		"{\"type\": \"object\", \"required\": [\"path\", \"sections\"], \"additionalProperties\": false,\n"
		" \"properties\": {\"path\": {\"type\": \"string\"}, \"css\": {\"type\": \"string\"},\n"
		"  \"sections\": {\"type\": \"array\", \"minItems\": 1, \"maxItems\": %d, \"items\": {\"type\": \"object\",\n"
		"   \"required\": [\"component\", \"content\"], \"additionalProperties\": false,\n"
		"   \"properties\": {\"component\": {\"type\": \"string\", \"enum\": [%s]}, \"variant\": {\"type\": \"string\"},\n"
		"    \"id\": {\"type\": \"string\"}, \"content\": {\"type\": \"object\"}}}}, \"aside\": {\"type\": \"string\"}}}\n", MAX_SECTIONS, names->str);
	g_string_free(names, TRUE);
	return schema;
}

gchar *
ls_components_dup_art_schema(LsComponents *self){
	g_return_val_if_fail(LS_IS_COMPONENTS(self), NULL);
	return g_strdup("{\"type\": \"object\", \"required\": [\"svg\"], \"additionalProperties\": false,"
		" \"properties\": {\"svg\": {\"type\": \"string\"}}}\n");
}

/* ---- Model-facing reference ------------------------------------------- */

static void
describe_slots(GString *out, JsonObject *slots, guint indent){
	g_autoptr(GList) names = json_object_get_members(slots);
	GList *item;
	for (item = names; item != NULL; item = item->next) {
		JsonObject *slot = member_object(slots, item->data);
		const gchar *type = ls_string(slot, "type");
		g_string_append_printf(out, "%*s%s: %s", indent, "", (const gchar *)item->data, type);
		if (json_object_has_member(slot, "max"))
			g_string_append_printf(out, " <=%" G_GINT64_FORMAT " chars", json_object_get_int_member(slot, "max"));
		if (json_object_has_member(slot, "min_items") || json_object_has_member(slot, "max_items"))
			g_string_append_printf(out, " (%" G_GINT64_FORMAT "-%" G_GINT64_FORMAT " items)",
				json_object_has_member(slot, "min_items") ? json_object_get_int_member(slot, "min_items") : 0,
				json_object_has_member(slot, "max_items") ? json_object_get_int_member(slot, "max_items") : 99);
		if (json_object_has_member(slot, "values")) {
			JsonArray *values = member_array(slot, "values");
			guint i;
			g_string_append(out, " one of");
			for (i = 0; i < json_array_get_length(values); i++)
				g_string_append_printf(out, "%s%s", i == 0 ? " " : "|", json_array_get_string_element(values, i));
		}
		if (json_object_has_member(slot, "required") && json_object_get_boolean_member(slot, "required"))
			g_string_append(out, ", required");
		if (ls_string(slot, "note") != NULL)
			g_string_append_printf(out, ". %s", ls_string(slot, "note"));
		g_string_append_c(out, '\n');
		if (g_str_equal(type, "items"))
			describe_slots(out, member_object(slot, "fields"), indent + 4);
	}
}

gchar *
ls_components_describe(LsComponents *self){
	GString *out = g_string_new(NULL);
	guint i;
	g_return_val_if_fail(LS_IS_COMPONENTS(self), NULL);
	g_string_append(out,
		"PAGE SPECIFICATION\n"
		"{\"path\": planned route, \"sections\": [section, ...] (1-16), \"css\": optional page CSS, "
		"\"aside\": optional rich text shown under the contents on reading pages}\n"
		"Section: {\"component\": name, \"variant\": name (default: first listed), \"id\": optional "
		"anchor id, \"content\": {slot: value}}. The first section's heading becomes the page h1, "
		"so the first section must have a heading. Section ids default to a slug of the heading.\n\n"
		"SLOT TYPES\n"
		"text: plain text; never markup. Exception: a section heading may mark one or two key words "
		"as *emphasis*, drawn by the site's look; never a whole heading.\n"
		"Rich text: paragraphs separated by a blank line; lines starting \"- \" form a list and "
		"\"1. \" a numbered list; inline **strong**, *emphasis*, `code` and [label](href). No HTML.\n"
		"link: {\"href\": target, \"label\": text}. links: a list of link objects.\n"
		"Targets (href): a planned page such as work.html or work.html#part-id, #id on the same page, "
		"mailto:, tel: or https:// URLs. Nothing else.\n"
		"image: {\"src\": a planned art path, \"alt\": text, or \"\" when purely decorative}.\n"
		"items: a list of objects with the listed fields. choice: one listed value. flag: true or false.\n"
		"fields (forms): [{\"name\": id-like, \"label\": text, \"type\": text|email|tel|url|number|"
		"date|textarea|select|checkbox, \"required\": bool, \"hint\": text, \"options\": [text] "
		"for select, \"maxlength\": number, \"autocomplete\": token}].\n"
		"html (custom only): well-formed XHTML fragment; no h1, scripts, forms, style attributes, "
		"event handlers or remote URLs.\n"
		"Page css: rules for this page only, prefixed with .page--SLUG where SLUG is the route "
		"without .html; url() may only reference assets/ or #ids; no @import.\n\n"
		"PAGE KINDS\n"
		"Every planned page has a kind. The first section must be one of the kind's openings, and "
		"hero is allowed only on the home page. Reading kinds (detail, article) get an in-page "
		"contents column from their section headings.\n");
	{
		g_autoptr(GList) kind_names = json_object_get_members(self->kinds);
		GList *kind_item;
		for (kind_item = kind_names; kind_item != NULL; kind_item = kind_item->next) {
			JsonObject *kind = member_object(self->kinds, kind_item->data);
			JsonArray *lists[2] = {member_array(kind, "openings"), member_array(kind, "suggested")};
			guint l, j;
			if (json_object_has_member(kind, "builtin"))
				continue;
			g_string_append_printf(out, "%s: %s", (const gchar *)kind_item->data, ls_string(kind, "summary"));
			for (l = 0; l < 2; l++) {
				g_string_append(out, l == 0 ? " Opens with: " : " Suits: ");
				for (j = 0; lists[l] != NULL && j < json_array_get_length(lists[l]); j++)
					g_string_append_printf(out, "%s%s", j == 0 ? "" : ", ", json_array_get_string_element(lists[l], j));
				g_string_append_c(out, '.');
			}
			g_string_append_c(out, '\n');
		}
	}
	g_string_append(out, "\nLOOK\n"
		"The plan's look picks one value per axis. Each axis reshapes every component on the site, "
		"so choose them from the subject; two sites should rarely share more than a few values.\n");
	{
		g_autoptr(GList) axes = json_object_get_members(self->looks);
		GList *axis;
		for (axis = axes; axis != NULL; axis = axis->next) {
			JsonObject *definition = member_object(self->looks, axis->data);
			JsonObject *values = member_object(definition, "values");
			g_autoptr(GList) value_names = json_object_get_members(values);
			GList *value;
			g_string_append_printf(out, "%s: %s", (const gchar *)axis->data, ls_string(definition, "summary"));
			for (value = value_names; value != NULL; value = value->next)
				g_string_append_printf(out, " %s (%s)%s", (const gchar *)value->data, ls_string(values, value->data),
					value->next != NULL ? ";" : ".");
			g_string_append_c(out, '\n');
		}
	}
	g_string_append(out, "\nOVERRIDES\n"
		"Every component can be restyled. Library styles sit in cascade layers, so the plan's css, "
		"its restyle entries and page css always win without extra specificity. restyle maps a "
		"component name to CSS included only where that component is used; target its classes "
		"(.NAME, .NAME--VARIANT, .NAME__PART). Sections carry id attributes for page css.\n");
	g_string_append(out, "\nCOMPONENTS\n");
	for (i = 0; i < self->order->len; i++) {
		const gchar *name = g_ptr_array_index(self->order, i);
		JsonObject *component = member_object(self->components, name);
		JsonObject *variants = member_object(component, "variants");
		g_autoptr(GList) variant_names = json_object_get_members(variants);
		GList *item;
		g_string_append_printf(out, "\n%s: %s\n  variants:", name, ls_string(component, "summary"));
		for (item = variant_names; item != NULL; item = item->next)
			g_string_append_printf(out, "\n    %s: %s", (const gchar *)item->data,
				ls_string(variants, item->data));
		g_string_append(out, "\n  content:\n");
		describe_slots(out, member_object(component, "slots"), 4);
	}
	return g_string_free(out, FALSE);
}

/* ---- Plan checks ---------------------------------------------------------- */

typedef struct {
	const gchar *name;
	gdouble min, max, fallback;
} NumberToken;

static const NumberToken number_tokens[] = {
	{"base_size", 0.875, 1.375, 1.0625}, {"ratio", 1.1, 1.6, 1.25}, {"measure", 45, 90, 66},
	{"radius", 0, 32, 2}, {"border", 0, 4, 1}, {"space", 0.75, 1.5, 1}, {"heading_weight", 300, 900, 700},
	{"heading_tracking", -0.08, 0.2, -0.01}, {"heading_leading", 0.9, 1.4, 1.1}, {"max_width", 56, 100, 76}
};
static const gchar *const color_tokens[] = {"paper", "ink", "muted", "surface", "line", "accent", "accent_ink"};
static const gchar *const font_tokens[] = {"font_display", "font_body", "font_mono"};
static const gchar *const ui_keys[] = {"skip", "menu", "nav", "footer_nav", "contents", "filter",
	"count_one", "count_many", "required", "reset", "preview", "download", "error_required",
	"error_invalid", "error_summary", "breadcrumb", "pager", "previous", "next", "letters", "scroll",
	"checklist_yes", "checklist_no"};
static const gchar *const ui_defaults[] = {"Skip to content", "Menu", "Primary", "Footer", "Contents",
	"Filter entries", "{n} entry shown", "{n} entries shown", "required", "Clear", "Preview",
	"Download text file", "Fill in this field.", "Check this entry.", "{n} fields need attention.",
	"Breadcrumb", "More pages", "Previous", "Next", "Jump to letter", "Scrollable list", "Included",
	"Not included"};
static const gchar *const art_ratios[] = {"16:9", "4:3", "3:2", "1:1", "3:4", "2:1", "3:1", "21:9", NULL};

static gdouble
token_number(JsonObject *tokens, const NumberToken *token){
	JsonNode *node = tokens != NULL ? json_object_get_member(tokens, token->name) : NULL;
	return is_number(node) ? json_node_get_double(node) : token->fallback;
}

static void
check_text(Problems *problems, const gchar *where, JsonNode *node, gboolean required, gsize max){
	const gchar *text;
	if (node == NULL || JSON_NODE_HOLDS_NULL(node)) {
		if (required)
			problem(problems, "%s is required", where);
		return;
	}
	if (!is_string(node)) {
		problem(problems, "%s must be a string", where);
		return;
	}
	text = json_node_get_string(node);
	if (blank(text))
		problem(problems, "%s is empty; omit it or write it", where);
	else if (max > 0 && (gsize)g_utf8_strlen(text, -1) > max)
		problem(problems, "%s has %ld characters; the limit is %" G_GSIZE_FORMAT, where,
			g_utf8_strlen(text, -1), max);
}

static gboolean
font_valid(const gchar *font){
	const gchar *p;
	if (font == NULL || blank(font) || strlen(font) > 240)
		return FALSE;
	for (p = font; *p != '\0'; p++)
		if (!g_ascii_isalnum(*p) && strchr(" ,-'\"._", *p) == NULL)
			return FALSE;
	return TRUE;
}

static void
check_link_object(Problems *problems, LinkScope *scope, const gchar *where, JsonNode *node){
	JsonObject *link;
	g_autofree gchar *why = NULL;
	g_autofree gchar *label_where = NULL;
	if (node == NULL || !JSON_NODE_HOLDS_OBJECT(node)) {
		problem(problems, "%s must be {\"href\", \"label\"}", where);
		return;
	}
	link = json_node_get_object(node);
	if (!ls_object_keys(link, "|href|label|", NULL))
		problem(problems, "%s may only have href and label", where);
	label_where = g_strconcat(where, ".label", NULL);
	check_text(problems, label_where, json_object_get_member(link, "label"), TRUE, 80);
	if (!is_string(json_object_get_member(link, "href")))
		problem(problems, "%s.href is required", where);
	else if ((why = href_problem(scope, ls_string(link, "href"))) != NULL)
		problem(problems, "%s: %s", where, why);
}

/* Collect planned routes and art for link and image checks. */
static void
plan_scope(JsonObject *plan, LinkScope *scope){
	JsonArray *pages = member_array(plan, "pages");
	JsonArray *art = member_array(plan, "art");
	guint i;
	scope->routes = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	scope->art = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	scope->anchors = NULL;
	for (i = 0; i < G_N_ELEMENTS(utility_routes); i++)
		g_hash_table_add(scope->routes, g_strdup(utility_routes[i]));
	for (i = 0; pages != NULL && i < json_array_get_length(pages); i++) {
		JsonNode *node = json_array_get_element(pages, i);
		const gchar *path = JSON_NODE_HOLDS_OBJECT(node) ? ls_string(json_node_get_object(node), "path") : NULL;
		if (path != NULL)
			g_hash_table_add(scope->routes, g_strdup(path));
	}
	for (i = 0; art != NULL && i < json_array_get_length(art); i++) {
		JsonNode *node = json_array_get_element(art, i);
		const gchar *path = JSON_NODE_HOLDS_OBJECT(node) ? ls_string(json_node_get_object(node), "path") : NULL;
		if (path != NULL)
			g_hash_table_add(scope->art, g_strdup(path));
	}
}

static void
scope_clear(LinkScope *scope){
	g_clear_pointer(&scope->routes, g_hash_table_unref);
	g_clear_pointer(&scope->art, g_hash_table_unref);
	g_clear_pointer(&scope->anchors, g_hash_table_unref);
}

static void
check_tokens(Problems *problems, JsonObject *tokens){
	guint i;
	const gchar *density, *heading_case;
	if (tokens == NULL) {
		problem(problems, "tokens is required");
		return;
	}
	if (!ls_object_keys(tokens, "|paper|ink|muted|surface|line|accent|accent_ink|font_display|font_body|"
		"font_mono|base_size|ratio|measure|radius|border|space|density|heading_weight|heading_tracking|"
		"heading_leading|heading_case|max_width|", NULL))
		problem(problems, "tokens has an unknown key");
	for (i = 0; i < G_N_ELEMENTS(color_tokens); i++) {
		gdouble rgb[3];
		if (!parse_color(ls_string(tokens, color_tokens[i]), rgb))
			problem(problems, "tokens.%s must be a #rrggbb color", color_tokens[i]);
	}
	for (i = 0; i < G_N_ELEMENTS(font_tokens); i++)
		if (!font_valid(ls_string(tokens, font_tokens[i])))
			problem(problems, "tokens.%s must be a CSS font-family list of installed or generic "
				"families (no URLs, no web fonts)", font_tokens[i]);
	for (i = 0; i < G_N_ELEMENTS(number_tokens); i++) {
		JsonNode *node = json_object_get_member(tokens, number_tokens[i].name);
		gdouble value;
		if (node == NULL)
			continue;
		value = is_number(node) ? json_node_get_double(node) : NAN;
		if (!is_number(node) || value < number_tokens[i].min || value > number_tokens[i].max)
			problem(problems, "tokens.%s must be a number from %g to %g", number_tokens[i].name,
				number_tokens[i].min, number_tokens[i].max);
	}
	density = ls_string(tokens, "density");
	if (json_object_has_member(tokens, "density") && g_strcmp0(density, "compact") != 0 &&
		g_strcmp0(density, "regular") != 0 && g_strcmp0(density, "airy") != 0)
		problem(problems, "tokens.density must be compact, regular or airy");
	heading_case = ls_string(tokens, "heading_case");
	if (json_object_has_member(tokens, "heading_case") && g_strcmp0(heading_case, "none") != 0 &&
		g_strcmp0(heading_case, "uppercase") != 0)
		problem(problems, "tokens.heading_case must be none or uppercase");
	{
		const struct { const gchar *fg, *bg; gdouble min; } pairs[] = {
			{"ink", "paper", 4.5}, {"muted", "paper", 4.5}, {"ink", "surface", 4.5},
			{"muted", "surface", 4.5}, {"accent_ink", "accent", 4.5}
		};
		for (i = 0; i < G_N_ELEMENTS(pairs); i++) {
			gdouble ratio = contrast(ls_string(tokens, pairs[i].fg), ls_string(tokens, pairs[i].bg));
			if (ratio > 0 && ratio < pairs[i].min)
				problem(problems, "tokens.%s on tokens.%s has contrast %.2f:1; WCAG AA text needs %.1f:1",
					pairs[i].fg, pairs[i].bg, ratio, pairs[i].min);
		}
	}
}

/* Open-source families served by Bunny Fonts; the builder writes the URL. */
static void
check_fonts(Problems *problems, JsonObject *plan){
	JsonArray *fonts = member_array(plan, "fonts");
	guint i, j;
	if (!json_object_has_member(plan, "fonts"))
		return;
	if (fonts == NULL || json_array_get_length(fonts) > 3) {
		problem(problems, "fonts must list at most 3 families");
		return;
	}
	for (i = 0; i < json_array_get_length(fonts); i++) {
		JsonNode *node = json_array_get_element(fonts, i);
		JsonObject *font = JSON_NODE_HOLDS_OBJECT(node) ? json_node_get_object(node) : NULL;
		const gchar *family = font != NULL ? ls_string(font, "family") : NULL;
		JsonArray *weights = font != NULL ? member_array(font, "weights") : NULL;
		if (font == NULL || !ls_object_keys(font, "|family|weights|italic|", NULL)) {
			problem(problems, "fonts[%u] must be {family, weights, italic}", i);
			continue;
		}
		if (family == NULL || blank(family) || strlen(family) > 40 ||
			strspn(family, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 ") != strlen(family))
			problem(problems, "fonts[%u].family must be a Bunny Fonts family name of letters, digits and spaces", i);
		if (weights == NULL || json_array_get_length(weights) == 0 || json_array_get_length(weights) > 6)
			problem(problems, "fonts[%u].weights must list 1 to 6 weights", i);
		for (j = 0; weights != NULL && j < json_array_get_length(weights); j++) {
			JsonNode *weight = json_array_get_element(weights, j);
			if (!JSON_NODE_HOLDS_VALUE(weight) || json_node_get_value_type(weight) != G_TYPE_INT64 ||
				json_node_get_int(weight) < 100 || json_node_get_int(weight) > 900 || json_node_get_int(weight) % 100 != 0)
				problem(problems, "fonts[%u].weights[%u] must be 100, 200, ... 900", i, j);
		}
		if (json_object_has_member(font, "italic") &&
			json_node_get_value_type(json_object_get_member(font, "italic")) != G_TYPE_BOOLEAN)
			problem(problems, "fonts[%u].italic must be true or false", i);
	}
}

static gchar *
font_href(JsonObject *plan){
	JsonArray *fonts = member_array(plan, "fonts");
	GString *out;
	guint i, j;
	if (fonts == NULL || json_array_get_length(fonts) == 0)
		return NULL;
	out = g_string_new("https://fonts.bunny.net/css?family=");
	for (i = 0; i < json_array_get_length(fonts); i++) {
		JsonObject *font = json_array_get_object_element(fonts, i);
		JsonArray *weights = member_array(font, "weights");
		gboolean italic = json_object_has_member(font, "italic") && json_object_get_boolean_member(font, "italic");
		g_autofree gchar *family = slugify(ls_string(font, "family"), 40);
		g_string_append_printf(out, "%s%s:", i == 0 ? "" : "|", family);
		for (j = 0; j < json_array_get_length(weights); j++) {
			gint64 weight = json_array_get_int_element(weights, j);
			g_string_append_printf(out, "%s%" G_GINT64_FORMAT, j == 0 ? "" : ",", weight);
			if (italic)
				g_string_append_printf(out, ",%" G_GINT64_FORMAT "i", weight);
		}
	}
	g_string_append(out, "&display=swap");
	return g_string_free(out, FALSE);
}

static void
check_plan_links(Problems *problems, LinkScope *scope, const gchar *where, JsonArray *links, guint max){
	guint i;
	if (json_array_get_length(links) > max)
		problem(problems, "%s allows at most %u links", where, max);
	for (i = 0; i < json_array_get_length(links); i++) {
		g_autofree gchar *item = g_strdup_printf("%s[%u]", where, i);
		check_link_object(problems, scope, item, json_array_get_element(links, i));
	}
}

/* One listed value per look axis; the builder repairs missing axes to defaults. */
static void
check_look(Problems *problems, LsComponents *self, JsonObject *plan){
	JsonObject *look = member_object(plan, "look");
	g_autoptr(GList) axes = NULL;
	GList *axis;
	if (!json_object_has_member(plan, "look"))
		return;
	if (look == NULL) {
		problem(problems, "look must be an object with one value per axis");
		return;
	}
	axes = json_object_get_members(look);
	for (axis = axes; axis != NULL; axis = axis->next) {
		JsonObject *definition = member_object(self->looks, axis->data);
		const gchar *value = ls_string(look, axis->data);
		if (definition == NULL)
			problem(problems, "look.%s is not a look axis", (const gchar *)axis->data);
		else if (value == NULL || ls_string(member_object(definition, "values"), value) == NULL)
			problem(problems, "look.%s must be one of the listed values", (const gchar *)axis->data);
	}
}

/* restyle maps a component name to CSS for that component on this site. */
static void
check_restyle(Problems *problems, LsComponents *self, JsonObject *plan){
	JsonObject *restyle = member_object(plan, "restyle");
	g_autoptr(GList) names = NULL;
	GList *name;
	gsize total = 0;
	if (!json_object_has_member(plan, "restyle"))
		return;
	if (restyle == NULL) {
		problem(problems, "restyle must map component names to CSS");
		return;
	}
	names = json_object_get_members(restyle);
	for (name = names; name != NULL; name = name->next) {
		JsonNode *value = json_object_get_member(restyle, name->data);
		g_autofree gchar *why = NULL;
		if (member_object(self->components, name->data) == NULL) {
			problem(problems, "restyle.%s is not a component", (const gchar *)name->data);
			continue;
		}
		why = is_string(value) ? css_problem(json_node_get_string(value), 8000) : g_strdup("css must be a string");
		if (why != NULL)
			problem(problems, "restyle.%s: %s", (const gchar *)name->data, why);
		else
			total += strlen(json_node_get_string(value));
	}
	if (total > 24000)
		problem(problems, "restyle CSS must total at most 24000 bytes");
}

static void
check_plan_into(Problems *problems, LsComponents *self, JsonNode *node){
	JsonObject *kinds = self->kinds;
	JsonObject *plan = node != NULL && JSON_NODE_HOLDS_OBJECT(node) ? json_node_get_object(node) : NULL;
	JsonArray *pages, *art, *nav;
	JsonObject *brand, *footer, *ui;
	LinkScope scope = {NULL, NULL, NULL};
	g_autoptr(GHashTable) seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	gboolean home = FALSE;
	guint i;
	const gchar *lang;
	if (plan == NULL) {
		problem(problems, "the plan must be a JSON object");
		return;
	}
	if (!ls_object_keys(plan, "|name|tagline|lang|character|tokens|fonts|brand|nav|action|footer|voice|look|art|"
		"pages|css|restyle|ui|", NULL))
		problem(problems, "unknown top-level key; allowed: name, tagline, lang, character, tokens, fonts, brand, "
			"nav, action, footer, voice, look, art, pages, css, restyle, ui");
	check_look(problems, self, plan);
	check_restyle(problems, self, plan);
	check_fonts(problems, plan);
	plan_scope(plan, &scope);
	check_text(problems, "name", json_object_get_member(plan, "name"), TRUE, 80);
	check_text(problems, "tagline", json_object_get_member(plan, "tagline"), FALSE, 160);
	check_text(problems, "voice", json_object_get_member(plan, "voice"), FALSE, 1500);
	check_text(problems, "character", json_object_get_member(plan, "character"), FALSE, 600);
	lang = ls_string(plan, "lang");
	if (json_object_has_member(plan, "lang") && (lang == NULL || strlen(lang) < 2 || strlen(lang) > 35 ||
		!g_ascii_isalpha(lang[0]) || strspn(lang, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-") != strlen(lang)))
		problem(problems, "lang must be a language tag such as en or fr-CA");
	check_tokens(problems, member_object(plan, "tokens"));
	pages = member_array(plan, "pages");
	if (pages == NULL || json_array_get_length(pages) == 0 || json_array_get_length(pages) > MAX_PAGES)
		problem(problems, "pages must list 1 to %d routes", MAX_PAGES);
	for (i = 0; pages != NULL && i < json_array_get_length(pages); i++) {
		JsonNode *entry = json_array_get_element(pages, i);
		JsonObject *page = JSON_NODE_HOLDS_OBJECT(entry) ? json_node_get_object(entry) : NULL;
		const gchar *path = page != NULL ? ls_string(page, "path") : NULL;
		g_autofree gchar *where = g_strdup_printf("pages[%u]", i);
		g_autofree gchar *title = g_strconcat(where, ".title", NULL);
		g_autofree gchar *description = g_strconcat(where, ".description", NULL);
		g_autofree gchar *brief = g_strconcat(where, ".brief", NULL);
		if (page == NULL) {
			problem(problems, "%s must be an object", where);
			continue;
		}
		if (!ls_object_keys(page, "|path|kind|title|description|brief|", NULL))
			problem(problems, "%s may only have path, kind, title, description and brief", where);
		{
			const gchar *kind = ls_string(page, "kind");
			JsonObject *definition = kind != NULL ? member_object(kinds, kind) : NULL;
			if (definition == NULL || json_object_has_member(definition, "builtin")) {
				GString *known = g_string_new(NULL);
				g_autoptr(GList) names = json_object_get_members(kinds);
				GList *name;
				for (name = names; name != NULL; name = name->next)
					if (!json_object_has_member(member_object(kinds, name->data), "builtin"))
						g_string_append_printf(known, "%s%s", known->len > 0 ? ", " : "", (const gchar *)name->data);
				problem(problems, "%s.kind must be one of %s", where, known->str);
				g_string_free(known, TRUE);
			} else if (g_strcmp0(path, "index.html") == 0 && !g_str_equal(kind, "home"))
				problem(problems, "%s: index.html is the home page, so its kind must be home", where);
			else if (g_strcmp0(path, "index.html") != 0 && g_str_equal(kind, "home"))
				problem(problems, "%s: only index.html may be kind home; choose index, detail, article, "
					"menu, visit, about, form or general", where);
		}
		if (is_utility(path))
			problem(problems, "%s.path %s is built in; do not plan 404.html, privacy.html or terms.html", where, path);
		else if (!route_valid(path))
			problem(problems, "%s.path must be a flat lowercase route such as about.html", where);
		else if (!g_hash_table_add(seen, g_strdup(path)))
			problem(problems, "%s.path %s is listed twice", where, path);
		home |= g_strcmp0(path, "index.html") == 0;
		check_text(problems, title, json_object_get_member(page, "title"), TRUE, 120);
		check_text(problems, description, json_object_get_member(page, "description"), TRUE, 200);
		check_text(problems, brief, json_object_get_member(page, "brief"), TRUE, 3000);
	}
	if (!home)
		problem(problems, "pages must include index.html");
	art = member_array(plan, "art");
	if (json_object_has_member(plan, "art") && (art == NULL || json_array_get_length(art) > 12))
		problem(problems, "art must list at most 12 artworks");
	g_hash_table_remove_all(seen);
	for (i = 0; art != NULL && i < json_array_get_length(art); i++) {
		JsonNode *entry = json_array_get_element(art, i);
		JsonObject *work = JSON_NODE_HOLDS_OBJECT(entry) ? json_node_get_object(entry) : NULL;
		const gchar *path = work != NULL ? ls_string(work, "path") : NULL;
		g_autofree gchar *brief = g_strdup_printf("art[%u].brief", i);
		if (work == NULL || !ls_object_keys(work, "|path|brief|ratio|", NULL)) {
			problem(problems, "art[%u] must be {path, brief, ratio}", i);
			continue;
		}
		if (!art_path_valid(path))
			problem(problems, "art[%u].path %s must look like assets/name.svg", i, path != NULL ? path : "(missing)");
		else if (!g_hash_table_add(seen, g_strdup(path)))
			problem(problems, "art[%u].path %s is listed twice", i, path);
		check_text(problems, brief, json_object_get_member(work, "brief"), TRUE, 1500);
		if (!g_strv_contains(art_ratios, ls_string(work, "ratio") != NULL ? ls_string(work, "ratio") : ""))
			problem(problems, "art[%u].ratio must be one of 16:9, 4:3, 3:2, 1:1, 3:4, 2:1, 3:1, 21:9", i);
	}
	nav = member_array(plan, "nav");
	if (nav == NULL || json_array_get_length(nav) == 0)
		problem(problems, "nav must list 1 to 8 links to planned pages");
	else {
		check_plan_links(problems, &scope, "nav", nav, 8);
		for (i = 0; i < json_array_get_length(nav); i++) {
			JsonNode *entry = json_array_get_element(nav, i);
			const gchar *href = JSON_NODE_HOLDS_OBJECT(entry) ? ls_string(json_node_get_object(entry), "href") : NULL;
			if (href != NULL && !g_hash_table_contains(scope.routes, href))
				problem(problems, "nav[%u].href must be a planned page without an anchor", i);
		}
	}
	if (json_object_has_member(plan, "action"))
		check_link_object(problems, &scope, "action", json_object_get_member(plan, "action"));
	brand = member_object(plan, "brand");
	if (json_object_has_member(plan, "brand") && (brand == NULL || !ls_object_keys(brand, "|wordmark|mark|", NULL)))
		problem(problems, "brand may only have wordmark and mark");
	if (brand != NULL) {
		check_text(problems, "brand.wordmark", json_object_get_member(brand, "wordmark"), FALSE, 60);
		if (json_object_has_member(brand, "mark") && !g_hash_table_contains(scope.art,
			ls_string(brand, "mark") != NULL ? ls_string(brand, "mark") : ""))
			problem(problems, "brand.mark must be a planned art path");
	}
	footer = member_object(plan, "footer");
	if (json_object_has_member(plan, "footer") && (footer == NULL || !ls_object_keys(footer, "|note|links|", NULL)))
		problem(problems, "footer may only have note and links");
	if (footer != NULL) {
		RichState state = {&scope, g_string_new(NULL)};
		g_autofree gchar *html = NULL;
		check_text(problems, "footer.note", json_object_get_member(footer, "note"), FALSE, 800);
		if (ls_string(footer, "note") != NULL)
			html = rich_render(&state, ls_string(footer, "note"));
		if (state.why->len > 0)
			problem(problems, "footer.note: %s", state.why->str);
		g_string_free(state.why, TRUE);
		if (member_array(footer, "links") != NULL)
			check_plan_links(problems, &scope, "footer.links", member_array(footer, "links"), 8);
	}
	ui = member_object(plan, "ui");
	if (json_object_has_member(plan, "ui")) {
		if (ui == NULL)
			problem(problems, "ui must be an object of interface strings");
		for (i = 0; ui != NULL && i < G_N_ELEMENTS(ui_keys); i++) {
			g_autofree gchar *where = g_strconcat("ui.", ui_keys[i], NULL);
			check_text(problems, where, json_object_get_member(ui, ui_keys[i]), FALSE, 80);
		}
		if (ui != NULL && json_object_get_size(ui) > 0) {
			g_autoptr(GList) keys = json_object_get_members(ui);
			GList *item;
			for (item = keys; item != NULL; item = item->next) {
				gboolean known = FALSE;
				for (i = 0; i < G_N_ELEMENTS(ui_keys); i++)
					known |= g_str_equal(item->data, ui_keys[i]);
				if (!known)
					problem(problems, "ui.%s is not a known interface string", (const gchar *)item->data);
			}
		}
	}
	if (json_object_has_member(plan, "css")) {
		g_autofree gchar *why = is_string(json_object_get_member(plan, "css"))
			? css_problem(ls_string(plan, "css"), 24000) : g_strdup("css must be a string");
		if (why != NULL)
			problem(problems, "%s", why);
	}
	scope_clear(&scope);
}

static gchar *
format_color(const gdouble rgb[3]){
	return g_strdup_printf("#%02x%02x%02x", (guint)(CLAMP(rgb[0], 0, 1) * 255 + 0.5),
		(guint)(CLAMP(rgb[1], 0, 1) * 255 + 0.5), (guint)(CLAMP(rgb[2], 0, 1) * 255 + 0.5));
}

/* Move @key toward black or white, whichever contrasts with @background, in
 * the smallest steps that reach @minimum. Returns whether it now passes. */
static gboolean
nudge(JsonObject *tokens, const gchar *key, const gchar *background, gdouble minimum){
	gdouble fg[3], bg[3], target, t;
	guint i;
	if (!parse_color(ls_string(tokens, key), fg) || !parse_color(background, bg))
		return FALSE;
	target = luminance(bg) > 0.18 ? 0.0 : 1.0;
	for (t = 0.02; t <= 1.0001; t += 0.02) {
		gdouble mixed[3];
		g_autofree gchar *color = NULL;
		for (i = 0; i < 3; i++)
			mixed[i] = fg[i] + (target - fg[i]) * t;
		color = format_color(mixed);
		if (contrast(color, background) >= minimum + 0.01) {
			json_object_set_string_member(tokens, key, color);
			return TRUE;
		}
	}
	return FALSE;
}


/* Themes are whole designs. Each is checked as the design half of a plan,
 * so applying one can never produce a plan that fails its own checks. */
static gboolean
load_themes(LsComponents *self, GChecksum *digest, GError **error){
	g_autofree gchar *text = load_resource(self, digest, "themes.yaml", TRUE, error);
	JsonObject *root;
	g_autoptr(GList) names = NULL;
	GList *name;
	if (text == NULL || (self->theme_catalog = ls_yaml_parse(text, error)) == NULL)
		return FALSE;
	root = JSON_NODE_HOLDS_OBJECT(self->theme_catalog) ? json_node_get_object(self->theme_catalog) : NULL;
	if (root == NULL || !ls_object_keys(root, "|version|themes|", NULL) ||
		(self->themes = member_object(root, "themes")) == NULL || json_object_get_size(self->themes) == 0)
		return catalog_invalid(error, "themes.yaml", "needs version and at least one theme");
	names = json_object_get_members(self->themes);
	for (name = names; name != NULL; name = name->next) {
		JsonObject *theme = member_object(self->themes, name->data);
		g_autoptr(JsonNode) plan = NULL;
		g_autoptr(GList) axes = NULL;
		Problems problems = {g_ptr_array_new_with_free_func(g_free)};
		GList *axis;
		const gchar *p;
		for (p = name->data; *p != '\0'; p++)
			if (!g_ascii_islower(*p) && !g_ascii_isdigit(*p) && *p != '-') {
				g_ptr_array_unref(problems.messages);
				return catalog_invalid(error, name->data, "theme names are lowercase words");
			}
		if (theme == NULL || !ls_object_keys(theme, "|summary|tokens|fonts|look|css|restyle|", NULL) ||
			ls_string(theme, "summary") == NULL || member_object(theme, "tokens") == NULL ||
			member_object(theme, "look") == NULL) {
			g_ptr_array_unref(problems.messages);
			return catalog_invalid(error, name->data, "themes need a summary, tokens and look");
		}
		axes = json_object_get_members(self->looks);
		for (axis = axes; axis != NULL; axis = axis->next)
			if (ls_string(member_object(theme, "look"), axis->data) == NULL)
				problem(&problems, "look.%s is missing", (const gchar *)axis->data);
		plan = ls_json_parse("{\"name\": \"Theme\", \"nav\": [{\"href\": \"index.html\", \"label\": \"Home\"}], "
			"\"art\": [], \"pages\": [{\"path\": \"index.html\", \"kind\": \"home\", \"title\": \"Home\", "
			"\"description\": \"A theme check page of about sixty characters in length.\", \"brief\": \"b\"}]}", NULL);
		copy_theme(theme, json_node_get_object(plan));
		check_plan_into(&problems, self, plan);
		if (problems.messages->len > 0) {
			g_autofree gchar *why = NULL;
			g_ptr_array_add(problems.messages, NULL);
			why = g_strjoinv("; ", (gchar **)problems.messages->pdata);
			g_ptr_array_unref(problems.messages);
			catalog_invalid(error, name->data, why);
			return FALSE;
		}
		g_ptr_array_unref(problems.messages);
	}
	return TRUE;
}

gchar **
ls_components_list_themes(LsComponents *self){
	g_autoptr(GList) names = NULL;
	GPtrArray *out = g_ptr_array_new();
	GList *name;
	g_return_val_if_fail(LS_IS_COMPONENTS(self), NULL);
	names = json_object_get_members(self->themes);
	for (name = names; name != NULL; name = name->next)
		g_ptr_array_add(out, g_strdup(name->data));
	g_ptr_array_add(out, NULL);
	return (gchar **)g_ptr_array_free(out, FALSE);
}

const gchar *
ls_components_get_theme_summary(LsComponents *self, const gchar *name){
	JsonObject *theme;
	g_return_val_if_fail(LS_IS_COMPONENTS(self), NULL);
	g_return_val_if_fail(name != NULL, NULL);
	theme = member_object(self->themes, name);
	return theme != NULL ? ls_string(theme, "summary") : NULL;
}

const gchar *
ls_components_get_themed_plan_schema(LsComponents *self){
	g_return_val_if_fail(LS_IS_COMPONENTS(self), NULL);
	return self->themed_plan_schema;
}

gboolean
ls_components_apply_theme(LsComponents *self, JsonNode *plan, const gchar *name, GError **error){
	JsonObject *theme;
	g_return_val_if_fail(LS_IS_COMPONENTS(self), FALSE);
	g_return_val_if_fail(plan != NULL, FALSE);
	g_return_val_if_fail(name != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);
	theme = member_object(self->themes, name);
	if (theme == NULL) {
		g_set_error(error, LS_ERROR, LS_ERROR_INVALID, "Unknown theme '%s'", name);
		return FALSE;
	}
	if (!JSON_NODE_HOLDS_OBJECT(plan)) {
		g_set_error_literal(error, LS_ERROR, LS_ERROR_INVALID, "The plan must be a JSON object");
		return FALSE;
	}
	copy_theme(theme, json_node_get_object(plan));
	return TRUE;
}

/* ---- Identities ---------------------------------------------------------- */

/* Hue in degrees, saturation and lightness in 0..1 (HSL). */
static gboolean
color_hsl(const gchar *text, gdouble *hue, gdouble *saturation, gdouble *lightness){
	gdouble rgb[3], high, low, delta;
	if (!parse_color(text, rgb))
		return FALSE;
	high = MAX(rgb[0], MAX(rgb[1], rgb[2]));
	low = MIN(rgb[0], MIN(rgb[1], rgb[2]));
	delta = high - low;
	*lightness = (high + low) / 2;
	*saturation = delta == 0 ? 0 : delta / (1 - fabs(2 * *lightness - 1));
	if (delta == 0)
		*hue = 0;
	else if (high == rgb[0])
		*hue = 60 * fmod((rgb[1] - rgb[2]) / delta + 6, 6);
	else if (high == rgb[1])
		*hue = 60 * ((rgb[2] - rgb[0]) / delta + 2);
	else
		*hue = 60 * ((rgb[0] - rgb[1]) / delta + 4);
	return TRUE;
}

/* The design a model reaches for when nothing ties it to the business: warm
 * cream paper with a rust or orange accent. */
static gboolean
house_default_colors(JsonObject *tokens){
	gdouble ph, ps, pl, ah, as, al;
	if (tokens == NULL || !color_hsl(ls_string(tokens, "paper"), &ph, &ps, &pl) ||
		!color_hsl(ls_string(tokens, "accent"), &ah, &as, &al))
		return FALSE;
	return pl >= 0.88 && ps >= 0.2 && ph >= 20 && ph <= 60 && as >= 0.4 && (ah <= 35 || ah >= 350);
}

/* The default pairing: Fraunces headlines over Source Sans 3. */
static gboolean
house_default_fonts(JsonObject *tokens){
	const gchar *display = tokens != NULL ? ls_string(tokens, "font_display") : NULL;
	const gchar *body = tokens != NULL ? ls_string(tokens, "font_body") : NULL;
	return display != NULL && body != NULL && strstr(display, "Fraunces") != NULL &&
		strstr(body, "Source Sans") != NULL && strstr(display, "Fraunces") - display < 3;
}

/* Every fits list names known sectors, at least one. */
static gboolean
check_fits(LsComponents *self, JsonObject *entry, const gchar *name, GError **error){
	JsonArray *fits = member_array(entry, "fits");
	guint i;
	if (fits == NULL || json_array_get_length(fits) == 0)
		return catalog_invalid(error, name, "needs fits: one or more sectors");
	for (i = 0; i < json_array_get_length(fits); i++) {
		JsonNode *node = json_array_get_element(fits, i);
		if (!JSON_NODE_HOLDS_VALUE(node) || json_node_get_value_type(node) != G_TYPE_STRING ||
			!json_object_has_member(self->sectors, json_node_get_string(node)))
			return catalog_invalid(error, name, "fits may only name sectors");
	}
	return TRUE;
}

/* A palette or typeface is checked as the design half of a plan: first theme
 * as the base, then the palette colors and the typeface over it. */
static gboolean
check_identity_pair(LsComponents *self, JsonObject *palette, JsonObject *typeface, const gchar *name,
	GError **error){
	g_autoptr(GList) themes = json_object_get_members(self->themes);
	g_autoptr(JsonNode) plan = ls_json_parse("{\"name\": \"Identity\", \"nav\": [{\"href\": \"index.html\", "
		"\"label\": \"Home\"}], \"art\": [], \"pages\": [{\"path\": \"index.html\", \"kind\": \"home\", "
		"\"title\": \"Home\", \"description\": \"An identity check page of about sixty characters long.\", "
		"\"brief\": \"b\"}]}", NULL);
	JsonObject *tokens;
	g_autoptr(GList) keys = NULL;
	GList *key;
	Problems problems = {g_ptr_array_new_with_free_func(g_free)};
	gboolean okay;
	copy_theme(member_object(self->themes, themes->data), json_node_get_object(plan));
	tokens = member_object(json_node_get_object(plan), "tokens");
	keys = json_object_get_members(member_object(palette, "tokens"));
	for (key = keys; key != NULL; key = key->next)
		json_object_set_member(tokens, key->data,
			json_node_copy(json_object_get_member(member_object(palette, "tokens"), key->data)));
	g_list_free(keys);
	keys = json_object_get_members(member_object(typeface, "tokens"));
	for (key = keys; key != NULL; key = key->next)
		json_object_set_member(tokens, key->data,
			json_node_copy(json_object_get_member(member_object(typeface, "tokens"), key->data)));
	if (json_object_has_member(json_node_get_object(plan), "fonts"))
		json_object_remove_member(json_node_get_object(plan), "fonts");
	if (member_array(typeface, "fonts") != NULL) {
		g_autofree gchar *fonts = ls_json_encode(json_object_get_member(typeface, "fonts"));
		json_object_set_member(json_node_get_object(plan), "fonts", ls_json_parse(fonts, NULL));
	}
	check_plan_into(&problems, self, plan);
	okay = problems.messages->len == 0;
	if (!okay) {
		g_autofree gchar *why = NULL;
		g_ptr_array_add(problems.messages, NULL);
		why = g_strjoinv("; ", (gchar **)problems.messages->pdata);
		catalog_invalid(error, name, why);
	}
	g_ptr_array_unref(problems.messages);
	return okay;
}

static gboolean
load_identities(LsComponents *self, GChecksum *digest, GError **error){
	const gchar *colors = "|paper|ink|muted|surface|line|accent|accent_ink|";
	const gchar *type = "|font_display|font_body|font_mono|heading_weight|heading_tracking|heading_leading|heading_case|";
	g_autofree gchar *text = load_resource(self, digest, "identities.yaml", TRUE, error);
	JsonObject *root, *first_palette = NULL, *first_typeface = NULL;
	g_autoptr(GList) names = NULL;
	GList *name;
	if (text == NULL || (self->identity_catalog = ls_yaml_parse(text, error)) == NULL)
		return FALSE;
	root = JSON_NODE_HOLDS_OBJECT(self->identity_catalog) ? json_node_get_object(self->identity_catalog) : NULL;
	if (root == NULL || !ls_object_keys(root, "|version|sectors|palettes|typefaces|homes|", NULL) ||
		(self->sectors = member_object(root, "sectors")) == NULL || json_object_get_size(self->sectors) == 0 ||
		(self->palettes = member_object(root, "palettes")) == NULL || json_object_get_size(self->palettes) == 0 ||
		(self->typefaces = member_object(root, "typefaces")) == NULL || json_object_get_size(self->typefaces) == 0 ||
		(self->homes = member_object(root, "homes")) == NULL || json_object_get_size(self->homes) == 0)
		return catalog_invalid(error, "identities.yaml", "needs version, sectors, palettes, typefaces and homes");
	names = json_object_get_members(self->palettes);
	for (name = names; name != NULL; name = name->next) {
		JsonObject *palette = member_object(self->palettes, name->data);
		JsonObject *tokens = member_object(palette, "tokens");
		if (palette == NULL || !ls_object_keys(palette, "|summary|fits|tokens|", NULL) ||
			ls_string(palette, "summary") == NULL || tokens == NULL || !ls_object_keys(tokens, colors, NULL) ||
			json_object_get_size(tokens) != 7)
			return catalog_invalid(error, name->data, "palettes need a summary, fits and the seven color tokens");
		if (!check_fits(self, palette, name->data, error))
			return FALSE;
		if (house_default_colors(tokens))
			return catalog_invalid(error, name->data, "a palette may not be the house default it replaces");
		if (first_palette == NULL)
			first_palette = palette;
	}
	g_list_free(names);
	names = json_object_get_members(self->typefaces);
	for (name = names; name != NULL; name = name->next) {
		JsonObject *typeface = member_object(self->typefaces, name->data);
		JsonObject *tokens = member_object(typeface, "tokens");
		if (typeface == NULL || !ls_object_keys(typeface, "|summary|fits|tokens|fonts|", NULL) ||
			ls_string(typeface, "summary") == NULL || tokens == NULL || !ls_object_keys(tokens, type, NULL) ||
			json_object_get_size(tokens) != 7)
			return catalog_invalid(error, name->data, "typefaces need a summary, fits, fonts and the seven type tokens");
		if (!check_fits(self, typeface, name->data, error))
			return FALSE;
		if (house_default_fonts(tokens))
			return catalog_invalid(error, name->data, "a typeface may not be the house default it replaces");
		if (first_typeface == NULL)
			first_typeface = typeface;
		if (!check_identity_pair(self, first_palette, typeface, name->data, error))
			return FALSE;
	}
	g_list_free(names);
	names = json_object_get_members(self->palettes);
	for (name = names; name != NULL; name = name->next)
		if (!check_identity_pair(self, member_object(self->palettes, name->data), first_typeface, name->data, error))
			return FALSE;
	g_list_free(names);
	names = json_object_get_members(self->homes);
	for (name = names; name != NULL; name = name->next) {
		JsonObject *home = member_object(self->homes, name->data);
		JsonArray *sections = member_array(home, "sections");
		guint i;
		if (home == NULL || !ls_object_keys(home, "|sections|fits|", NULL) || sections == NULL ||
			json_array_get_length(sections) < 2 || json_array_get_length(sections) > 8)
			return catalog_invalid(error, name->data, "homes need fits and 2 to 8 sections");
		if (!check_fits(self, home, name->data, error))
			return FALSE;
		for (i = 0; i < json_array_get_length(sections); i++) {
			const gchar *component = json_array_get_string_element(sections, i);
			if (component == NULL || member_object(self->components, component) == NULL)
				return catalog_invalid(error, name->data, "home sections may only name catalog components");
		}
	}
	return TRUE;
}

gchar **
ls_components_list_sectors(LsComponents *self){
	g_autoptr(GList) names = NULL;
	GPtrArray *out = g_ptr_array_new();
	GList *name;
	g_return_val_if_fail(LS_IS_COMPONENTS(self), NULL);
	names = json_object_get_members(self->sectors);
	for (name = names; name != NULL; name = name->next)
		g_ptr_array_add(out, g_strdup(name->data));
	g_ptr_array_add(out, NULL);
	return (gchar **)g_ptr_array_free(out, FALSE);
}

/* Pick one entry of @group that fits @sector (any entry without a sector). */
static const gchar *
deal(JsonObject *group, const gchar *seed, const gchar *salt, const gchar *sector){
	g_autoptr(GList) names = json_object_get_members(group);
	g_autoptr(GPtrArray) fitting = g_ptr_array_new();
	GList *name;
	for (name = names; name != NULL; name = name->next) {
		JsonArray *fits = member_array(member_object(group, name->data), "fits");
		guint i;
		gboolean fit = sector == NULL;
		for (i = 0; !fit && fits != NULL && i < json_array_get_length(fits); i++)
			fit = g_strcmp0(json_array_get_string_element(fits, i), sector) == 0;
		if (fit)
			g_ptr_array_add(fitting, name->data);
	}
	if (fitting->len == 0)
		return NULL;
	return g_ptr_array_index(fitting, seed_pick(seed, salt, fitting->len));
}

/* {name, summary, ...members} as a fresh object: a deep copy of the entry. */
static JsonObject *
named_copy(JsonObject *group, const gchar *name){
	g_autoptr(JsonNode) source = json_node_new(JSON_NODE_OBJECT);
	g_autofree gchar *text = NULL;
	JsonNode *copy;
	JsonObject *out;
	json_node_set_object(source, member_object(group, name));
	text = ls_json_encode(source);
	copy = ls_json_parse(text, NULL);
	out = json_object_ref(json_node_get_object(copy));
	json_node_unref(copy);
	json_object_remove_member(out, "fits");
	json_object_set_string_member(out, "name", name);
	return out;
}

JsonObject *
ls_components_suggest_identity(LsComponents *self, const gchar *seed, const gchar *sector){
	JsonObject *identity;
	const gchar *palette, *typeface, *home;
	g_return_val_if_fail(LS_IS_COMPONENTS(self), NULL);
	g_return_val_if_fail(seed != NULL, NULL);
	if (sector != NULL && !json_object_has_member(self->sectors, sector))
		sector = NULL;
	palette = deal(self->palettes, seed, "palette", sector);
	typeface = deal(self->typefaces, seed, "typeface", sector);
	home = deal(self->homes, seed, "home", sector);
	identity = json_object_new();
	if (palette != NULL)
		json_object_set_object_member(identity, "palette", named_copy(self->palettes, palette));
	if (typeface != NULL)
		json_object_set_object_member(identity, "type", named_copy(self->typefaces, typeface));
	if (home != NULL)
		json_object_set_object_member(identity, "home", named_copy(self->homes, home));
	return identity;
}

gboolean
ls_components_check_identity(LsComponents *self, JsonNode *plan, JsonObject *identity, GError **error){
	JsonObject *tokens;
	JsonObject *palette, *typeface;
	g_return_val_if_fail(LS_IS_COMPONENTS(self), FALSE);
	g_return_val_if_fail(plan != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);
	tokens = JSON_NODE_HOLDS_OBJECT(plan) ? member_object(json_node_get_object(plan), "tokens") : NULL;
	palette = identity != NULL ? member_object(member_object(identity, "palette"), "tokens") : NULL;
	typeface = identity != NULL ? member_object(member_object(identity, "type"), "tokens") : NULL;
	if (house_default_colors(tokens) && (palette == NULL || !house_default_colors(palette))) {
		g_set_error_literal(error, LS_ERROR, LS_ERROR_VALIDATION,
			"tokens fall back on the house default (warm cream paper with a rust or orange accent), which "
			"makes every site look alike; derive the colors from the business's character, starting from "
			"TASK suggested_palette");
		return FALSE;
	}
	if (house_default_fonts(tokens) && (typeface == NULL || !house_default_fonts(typeface))) {
		g_set_error_literal(error, LS_ERROR, LS_ERROR_VALIDATION,
			"fonts fall back on the house default (Fraunces over Source Sans), which makes every site look "
			"alike; choose type that fits the business, starting from TASK suggested_type");
		return FALSE;
	}
	return TRUE;
}

gchar *
ls_components_repair_plan(LsComponents *self, JsonNode *plan){
	JsonObject *tokens = plan != NULL && JSON_NODE_HOLDS_OBJECT(plan)
		? member_object(json_node_get_object(plan), "tokens") : NULL;
	const struct { const gchar *fg, *bg; } pairs[] = {
		{"ink", "paper"}, {"ink", "surface"}, {"muted", "paper"}, {"muted", "surface"}, {"accent_ink", "accent"}
	};
	GString *notes = g_string_new(NULL);
	guint i, round;
	g_return_val_if_fail(LS_IS_COMPONENTS(self), NULL);
	/* Missing look axes take the catalog default. */
	if (plan != NULL && JSON_NODE_HOLDS_OBJECT(plan) &&
		(!json_object_has_member(json_node_get_object(plan), "look") ||
		member_object(json_node_get_object(plan), "look") != NULL)) {
		JsonObject *root = json_node_get_object(plan);
		JsonObject *look = member_object(root, "look");
		g_autoptr(GList) axes = json_object_get_members(self->looks);
		GList *axis;
		if (look == NULL) {
			look = json_object_new();
			json_object_set_object_member(root, "look", look);
		}
		for (axis = axes; axis != NULL; axis = axis->next)
			if (!json_object_has_member(look, axis->data)) {
				json_object_set_string_member(look, axis->data, look_default(self, axis->data));
				g_string_append_printf(notes, "%slook.%s -> %s", notes->len > 0 ? "; " : "",
					(const gchar *)axis->data, look_default(self, axis->data));
			}
	}
	/* A missing or misplaced kind has one mechanical answer. */
	if (plan != NULL && JSON_NODE_HOLDS_OBJECT(plan)) {
		JsonArray *pages = member_array(json_node_get_object(plan), "pages");
		for (i = 0; pages != NULL && i < json_array_get_length(pages); i++) {
			JsonNode *entry = json_array_get_element(pages, i);
			JsonObject *page = JSON_NODE_HOLDS_OBJECT(entry) ? json_node_get_object(entry) : NULL;
			const gchar *path = page != NULL ? ls_string(page, "path") : NULL;
			const gchar *kind = page != NULL ? ls_string(page, "kind") : NULL;
			const gchar *fixed = NULL;
			if (path == NULL)
				continue;
			if (g_str_equal(path, "index.html") && g_strcmp0(kind, "home") != 0)
				fixed = "home";
			else if (!g_str_equal(path, "index.html") && (kind == NULL || g_str_equal(kind, "home")))
				fixed = "general";
			if (fixed != NULL) {
				g_string_append_printf(notes, "%s%s kind -> %s", notes->len > 0 ? "; " : "", path, fixed);
				json_object_set_string_member(page, "kind", fixed);
			}
		}
	}
	for (i = 0; tokens != NULL && i < G_N_ELEMENTS(number_tokens); i++) {
		JsonNode *node = json_object_get_member(tokens, number_tokens[i].name);
		gdouble value;
		if (!is_number(node))
			continue;
		value = json_node_get_double(node);
		if (value < number_tokens[i].min || value > number_tokens[i].max) {
			gdouble fixed = CLAMP(value, number_tokens[i].min, number_tokens[i].max);
			g_string_append_printf(notes, "%stokens.%s %g -> %g", notes->len > 0 ? "; " : "",
				number_tokens[i].name, value, fixed);
			json_object_set_double_member(tokens, number_tokens[i].name, fixed);
		}
	}
	/* Two rounds: a color shared by two pairs must satisfy both. */
	for (round = 0; tokens != NULL && round < 2; round++) {
		for (i = 0; i < G_N_ELEMENTS(pairs); i++) {
			const gchar *fg = ls_string(tokens, pairs[i].fg), *bg = ls_string(tokens, pairs[i].bg);
			g_autofree gchar *before = g_strdup(fg);
			gdouble ratio = contrast(fg, bg);
			if (ratio == 0 || ratio >= 4.5)
				continue;
			if (!nudge(tokens, pairs[i].fg, bg, 4.5) && g_str_equal(pairs[i].fg, "accent_ink")) {
				g_autofree gchar *ink = g_strdup(fg);
				/* A mid-tone accent cannot hold either extreme: deepen the accent instead. */
				nudge(tokens, "accent", ink, 4.5);
			}
			if (g_strcmp0(before, ls_string(tokens, pairs[i].fg)) != 0)
				g_string_append_printf(notes, "%stokens.%s %s -> %s for %.1f:1 on %s", notes->len > 0 ? "; " : "",
					pairs[i].fg, before, ls_string(tokens, pairs[i].fg), contrast(ls_string(tokens, pairs[i].fg),
					ls_string(tokens, pairs[i].bg)), pairs[i].bg);
		}
	}
	if (notes->len == 0) {
		g_string_free(notes, TRUE);
		return NULL;
	}
	return g_string_free(notes, FALSE);
}

gboolean
ls_components_check_plan(LsComponents *self, JsonNode *plan, GError **error){
	Problems problems = {g_ptr_array_new_with_free_func(g_free)};
	gboolean okay;
	g_return_val_if_fail(LS_IS_COMPONENTS(self), FALSE);
	check_plan_into(&problems, self, plan);
	okay = finish(&problems, "The site plan", error);
	g_ptr_array_unref(problems.messages);
	return okay;
}

/* ---- Page checks ---------------------------------------------------------- */

static const gchar *
section_heading(JsonObject *section){
	JsonObject *content = member_object(section, "content");
	return content != NULL ? ls_string(content, "heading") : NULL;
}

/* Section ids: explicit, else a heading slug, else component-N; then parts. */
static GPtrArray *
section_ids(JsonArray *sections, GHashTable *anchors){
	GPtrArray *ids = g_ptr_array_new_with_free_func(g_free);
	guint i;
	g_hash_table_add(anchors, g_strdup("main"));
	for (i = 0; sections != NULL && i < json_array_get_length(sections); i++) {
		JsonNode *node = json_array_get_element(sections, i);
		JsonObject *section = JSON_NODE_HOLDS_OBJECT(node) ? json_node_get_object(node) : NULL;
		const gchar *explicit = section != NULL ? ls_string(section, "id") : NULL;
		g_autofree gchar *base = explicit != NULL ? g_strdup(explicit)
			: slugify(section != NULL ? section_heading(section) : NULL, 40);
		gchar *id;
		guint n = 2;
		if (base == NULL || *base == '\0' || !g_ascii_isalpha(*base)) {
			g_free(base);
			base = g_strdup_printf("%s-%u", section != NULL && ls_string(section, "component") != NULL
				? ls_string(section, "component") : "section", i + 1);
		}
		id = g_strdup(base);
		while (g_hash_table_contains(anchors, id)) {
			g_free(id);
			id = g_strdup_printf("%s-%u", base, n++);
		}
		g_hash_table_add(anchors, g_strdup(id));
		g_ptr_array_add(ids, id);
	}
	return ids;
}

static gchar *
part_id(GHashTable *anchors, const gchar *heading){
	g_autofree gchar *base = slugify(heading, 40);
	gchar *id;
	guint n = 2;
	if (*base == '\0' || !g_ascii_isalpha(*base)) {
		g_free(base);
		base = g_strdup("part");
	}
	id = g_strdup(base);
	while (g_hash_table_contains(anchors, id)) {
		g_free(id);
		id = g_strdup_printf("%s-%u", base, n++);
	}
	g_hash_table_add(anchors, g_strdup(id));
	return id;
}

/* Article part ids join the page's anchor set before any link is checked. */
static void
collect_part_ids(JsonArray *sections, GHashTable *anchors){
	guint i, j;
	for (i = 0; sections != NULL && i < json_array_get_length(sections); i++) {
		JsonNode *node = json_array_get_element(sections, i);
		JsonObject *section = JSON_NODE_HOLDS_OBJECT(node) ? json_node_get_object(node) : NULL;
		JsonArray *parts = section != NULL && g_strcmp0(ls_string(section, "component"), "article") == 0
			? member_array(member_object(section, "content"), "parts") : NULL;
		for (j = 0; parts != NULL && j < json_array_get_length(parts); j++) {
			JsonNode *part = json_array_get_element(parts, j);
			if (JSON_NODE_HOLDS_OBJECT(part)) {
				g_autofree gchar *id = part_id(anchors, ls_string(json_node_get_object(part), "heading"));
			}
		}
	}
}

typedef struct {
	Problems *problems;
	LinkScope *scope;
} PageCheck;

static void check_value(PageCheck *check, const gchar *where, const gchar *name, JsonObject *slot, JsonNode *value);

static void
check_fields(PageCheck *check, const gchar *where, JsonObject *definitions, JsonNode *node){
	JsonObject *object = node != NULL && JSON_NODE_HOLDS_OBJECT(node) ? json_node_get_object(node) : NULL;
	g_autoptr(GList) names = NULL;
	GList *item;
	if (object == NULL) {
		problem(check->problems, "%s must be an object", where);
		return;
	}
	names = json_object_get_members(object);
	for (item = names; item != NULL; item = item->next)
		if (member_object(definitions, item->data) == NULL)
			problem(check->problems, "%s: unknown field '%s'", where, (const gchar *)item->data);
	g_list_free(g_steal_pointer(&names));
	names = json_object_get_members(definitions);
	for (item = names; item != NULL; item = item->next) {
		g_autofree gchar *field = g_strdup_printf("%s.%s", where, (const gchar *)item->data);
		check_value(check, field, item->data, member_object(definitions, item->data),
			json_object_get_member(object, item->data));
	}
}

static void
check_form_fields(PageCheck *check, const gchar *where, JsonArray *fields){
	const gchar *types[] = {"text", "email", "tel", "url", "number", "date", "textarea", "select", "checkbox", NULL};
	g_autoptr(GHashTable) names = g_hash_table_new(g_str_hash, g_str_equal);
	guint i;
	for (i = 0; i < json_array_get_length(fields); i++) {
		JsonNode *node = json_array_get_element(fields, i);
		JsonObject *field = JSON_NODE_HOLDS_OBJECT(node) ? json_node_get_object(node) : NULL;
		const gchar *name = field != NULL ? ls_string(field, "name") : NULL;
		const gchar *type = field != NULL && ls_string(field, "type") != NULL ? ls_string(field, "type") : "text";
		g_autofree gchar *here = g_strdup_printf("%s[%u]", where, i);
		g_autofree gchar *label = g_strconcat(here, ".label", NULL);
		g_autofree gchar *hint = g_strconcat(here, ".hint", NULL);
		JsonNode *maxlength;
		if (field == NULL || !ls_object_keys(field, "|name|label|type|required|hint|options|maxlength|autocomplete|", NULL)) {
			problem(check->problems, "%s must be a field object with known keys", here);
			continue;
		}
		if (name == NULL || !anchor_valid(name) || strlen(name) > 40)
			problem(check->problems, "%s.name must be an id-like name such as project_name", here);
		else if (!g_hash_table_add(names, (gpointer)name))
			problem(check->problems, "%s.name %s is used twice", here, name);
		check_text(check->problems, label, json_object_get_member(field, "label"), TRUE, 120);
		check_text(check->problems, hint, json_object_get_member(field, "hint"), FALSE, 240);
		if (!g_strv_contains(types, type))
			problem(check->problems, "%s.type must be text, email, tel, url, number, date, textarea, select or checkbox", here);
		if (json_object_has_member(field, "required") &&
			json_node_get_value_type(json_object_get_member(field, "required")) != G_TYPE_BOOLEAN)
			problem(check->problems, "%s.required must be true or false", here);
		if (g_str_equal(type, "select")) {
			JsonArray *options = member_array(field, "options");
			guint j;
			if (options == NULL || json_array_get_length(options) == 0 || json_array_get_length(options) > 30)
				problem(check->problems, "%s.options must list 1 to 30 choices for a select", here);
			for (j = 0; options != NULL && j < json_array_get_length(options); j++)
				if (!is_string(json_array_get_element(options, j)) ||
					blank(json_array_get_string_element(options, j)))
					problem(check->problems, "%s.options[%u] must be text", here, j);
		} else if (json_object_has_member(field, "options"))
			problem(check->problems, "%s.options is only for select fields", here);
		maxlength = json_object_get_member(field, "maxlength");
		if (maxlength != NULL && (json_node_get_value_type(maxlength) != G_TYPE_INT64 ||
			json_node_get_int(maxlength) < 1 || json_node_get_int(maxlength) > 10000))
			problem(check->problems, "%s.maxlength must be a whole number from 1 to 10000", here);
		if (json_object_has_member(field, "autocomplete")) {
			const gchar *token = ls_string(field, "autocomplete");
			if (token == NULL || strlen(token) > 60 || strspn(token, "abcdefghijklmnopqrstuvwxyz- ") != strlen(token))
				problem(check->problems, "%s.autocomplete must be an autocomplete token such as email", here);
		}
	}
}

static void
check_value(PageCheck *check, const gchar *where, const gchar *name, JsonObject *slot, JsonNode *value){
	const gchar *type = ls_string(slot, "type");
	gboolean required = json_object_has_member(slot, "required") && json_object_get_boolean_member(slot, "required");
	gsize max = json_object_has_member(slot, "max") ? (gsize)json_object_get_int_member(slot, "max") : 0;
	(void)name;
	if (value == NULL || JSON_NODE_HOLDS_NULL(value)) {
		if (required)
			problem(check->problems, "%s is required", where);
		return;
	}
	if (g_str_equal(type, "text") || g_str_equal(type, "html")) {
		check_text(check->problems, where, value, required, max);
		if (g_str_equal(type, "html") && is_string(value)) {
			g_autoptr(GString) why = g_string_new(NULL);
			if (!check_markup(json_node_get_string(value), FALSE, check->scope, why))
				problem(check->problems, "%s: %s", where, why->str);
		}
	} else if (g_str_equal(type, "rich")) {
		check_text(check->problems, where, value, required, max);
		if (is_string(value)) {
			RichState state = {check->scope, g_string_new(NULL)};
			g_autofree gchar *html = rich_render(&state, json_node_get_string(value));
			if (state.why->len > 0)
				problem(check->problems, "%s: %s", where, state.why->str);
			g_string_free(state.why, TRUE);
		}
	} else if (g_str_equal(type, "link"))
		check_link_object(check->problems, check->scope, where, value);
	else if (g_str_equal(type, "image")) {
		JsonObject *image = JSON_NODE_HOLDS_OBJECT(value) ? json_node_get_object(value) : NULL;
		const gchar *src = image != NULL ? ls_string(image, "src") : NULL;
		JsonNode *alt = image != NULL ? json_object_get_member(image, "alt") : NULL;
		if (image == NULL || !ls_object_keys(image, "|src|alt|", NULL))
			problem(check->problems, "%s must be {\"src\", \"alt\"}", where);
		else if (src == NULL || !g_hash_table_contains(check->scope->art, src))
			problem(check->problems, "%s.src %s is not planned art", where, src != NULL ? src : "(missing)");
		if (image != NULL && (!is_string(alt) || g_utf8_strlen(json_node_get_string(alt), -1) > 300))
			problem(check->problems, "%s.alt must be text up to 300 characters, or \"\" when decorative", where);
	} else if (g_str_equal(type, "choice")) {
		JsonArray *values = member_array(slot, "values");
		gboolean found = FALSE;
		guint i;
		for (i = 0; is_string(value) && i < json_array_get_length(values); i++)
			found |= g_str_equal(json_array_get_string_element(values, i), json_node_get_string(value));
		if (!found)
			problem(check->problems, "%s must be one of the listed values", where);
	} else if (g_str_equal(type, "flag")) {
		if (!JSON_NODE_HOLDS_VALUE(value) || json_node_get_value_type(value) != G_TYPE_BOOLEAN)
			problem(check->problems, "%s must be true or false", where);
	} else {
		JsonArray *items = JSON_NODE_HOLDS_ARRAY(value) ? json_node_get_array(value) : NULL;
		gint64 min = json_object_has_member(slot, "min_items") ? json_object_get_int_member(slot, "min_items") : 0;
		gint64 most = json_object_has_member(slot, "max_items") ? json_object_get_int_member(slot, "max_items") : 99;
		guint i;
		if (items == NULL) {
			problem(check->problems, "%s must be a list", where);
			return;
		}
		if ((gint64)json_array_get_length(items) < MAX(min, required ? 1 : 0) ||
			(gint64)json_array_get_length(items) > most)
			problem(check->problems, "%s must have %" G_GINT64_FORMAT " to %" G_GINT64_FORMAT " items",
				where, MAX(min, required ? 1 : 0), most);
		if (g_str_equal(type, "fields")) {
			check_form_fields(check, where, items);
			return;
		}
		for (i = 0; i < json_array_get_length(items); i++) {
			g_autofree gchar *item = g_strdup_printf("%s[%u]", where, i);
			if (g_str_equal(type, "links"))
				check_link_object(check->problems, check->scope, item, json_array_get_element(items, i));
			else
				check_fields(check, item, member_object(slot, "fields"), json_array_get_element(items, i));
		}
	}
}

/* Rules that span slots; the catalog cannot express them as slot types. */
static void
check_component_rules(PageCheck *check, const gchar *where, const gchar *component,
	const gchar *variant, JsonObject *content){
	if (g_str_equal(component, "hero") && (g_strcmp0(variant, "split") == 0 || g_strcmp0(variant, "banner") == 0) &&
		!json_object_has_member(content, "art"))
		problem(check->problems, "%s: art is required by the %s variant", where, variant);
	if (g_str_equal(component, "filter")) {
		JsonArray *categories = member_array(content, "categories");
		JsonArray *items = member_array(content, "items");
		g_autoptr(GHashTable) labels = g_hash_table_new(g_str_hash, g_str_equal);
		guint i;
		for (i = 0; categories != NULL && i < json_array_get_length(categories); i++) {
			JsonNode *node = json_array_get_element(categories, i);
			const gchar *label = JSON_NODE_HOLDS_OBJECT(node) ? ls_string(json_node_get_object(node), "label") : NULL;
			if (label != NULL && !g_hash_table_add(labels, (gpointer)label))
				problem(check->problems, "%s.categories: '%s' is listed twice", where, label);
		}
		for (i = 0; items != NULL && i < json_array_get_length(items); i++) {
			JsonNode *node = json_array_get_element(items, i);
			const gchar *category = JSON_NODE_HOLDS_OBJECT(node) ? ls_string(json_node_get_object(node), "category") : NULL;
			if (category != NULL && !g_hash_table_contains(labels, category))
				problem(check->problems, "%s.items[%u]: category '%s' is not one of the categories", where, i, category);
		}
	}
	if (g_str_equal(component, "form")) {
		const gchar *mode = ls_string(content, "mode");
		const gchar *target = ls_string(content, "target");
		const gchar *download = ls_string(content, "download_name");
		if (g_strcmp0(mode, "local") == 0) {
			if (download == NULL || !g_str_has_suffix(download, ".txt") || strlen(download) < 5 ||
				strspn(download, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-") != strlen(download))
				problem(check->problems, "%s.download_name must be a simple file name ending in .txt for local mode", where);
			if (target != NULL)
				problem(check->problems, "%s.target is not used in local mode", where);
		} else if (g_strcmp0(mode, "mailto") == 0) {
			if (target == NULL || strchr(target, '@') == NULL || strpbrk(target, " <>\"'") != NULL)
				problem(check->problems, "%s.target must be the supplied email address for mailto mode", where);
		} else if (g_strcmp0(mode, "endpoint") == 0) {
			g_autofree gchar *why = target != NULL ? href_problem(NULL, target) : g_strdup("missing");
			if (target == NULL || !g_str_has_prefix(target, "https://") || why != NULL)
				problem(check->problems, "%s.target must be the supplied https:// endpoint for endpoint mode", where);
		}
	}
}

/* The kind a page is checked and rendered as: planned, or declared by a built-in page. */
static const gchar *
page_kind(JsonObject *plan, const gchar *path, JsonObject *page){
	JsonArray *pages = member_array(plan, "pages");
	guint i;
	if (page != NULL && ls_string(page, "kind") != NULL)
		return ls_string(page, "kind");
	for (i = 0; pages != NULL && i < json_array_get_length(pages); i++) {
		JsonNode *entry = json_array_get_element(pages, i);
		JsonObject *object = JSON_NODE_HOLDS_OBJECT(entry) ? json_node_get_object(entry) : NULL;
		if (object != NULL && g_strcmp0(ls_string(object, "path"), path) == 0)
			return ls_string(object, "kind") != NULL ? ls_string(object, "kind") : "general";
	}
	return "general";
}

static gboolean
kind_allows(JsonObject *kind, const gchar *component){
	JsonArray *openings = member_array(kind, "openings");
	guint i;
	for (i = 0; openings != NULL && i < json_array_get_length(openings); i++)
		if (g_strcmp0(json_array_get_string_element(openings, i), component) == 0)
			return TRUE;
	return FALSE;
}

static void
check_page_into(LsComponents *self, Problems *problems, JsonNode *plan_node, const gchar *path,
	JsonNode *page_node, gboolean builtin){
	JsonObject *plan = JSON_NODE_HOLDS_OBJECT(plan_node) ? json_node_get_object(plan_node) : NULL;
	JsonObject *page = page_node != NULL && JSON_NODE_HOLDS_OBJECT(page_node) ? json_node_get_object(page_node) : NULL;
	JsonArray *sections;
	LinkScope scope = {NULL, NULL, NULL};
	PageCheck check = {problems, &scope};
	g_autoptr(GPtrArray) ids = NULL;
	const gchar *kind_name;
	JsonObject *kind;
	guint i;
	if (plan == NULL || page == NULL) {
		problem(problems, "the page specification must be a JSON object");
		return;
	}
	plan_scope(plan, &scope);
	scope.anchors = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	if (!ls_object_keys(page, builtin ? "|path|kind|title|description|noindex|sections|" : "|path|sections|css|aside|", NULL))
		problem(problems, "unknown top-level key; allowed: path, sections, css, aside");
	kind_name = page_kind(plan, path, builtin ? page : NULL);
	kind = member_object(self->kinds, kind_name);
	if (json_object_has_member(page, "aside")) {
		JsonObject *slot = json_object_new();
		json_object_set_string_member(slot, "type", "rich");
		json_object_set_int_member(slot, "max", 1200);
		check_value(&check, "aside", "aside", slot, json_object_get_member(page, "aside"));
		json_object_unref(slot);
	}
	if (g_strcmp0(ls_string(page, "path"), path) != 0)
		problem(problems, "path must be %s", path);
	if (!builtin && !g_hash_table_contains(scope.routes, path))
		problem(problems, "%s is not a planned page", path);
	sections = member_array(page, "sections");
	if (sections == NULL || json_array_get_length(sections) == 0 || json_array_get_length(sections) > MAX_SECTIONS)
		problem(problems, "sections must list 1 to %d sections", MAX_SECTIONS);
	ids = section_ids(sections, scope.anchors);
	collect_part_ids(sections, scope.anchors);
	for (i = 0; sections != NULL && i < json_array_get_length(sections); i++) {
		JsonNode *node = json_array_get_element(sections, i);
		JsonObject *section = JSON_NODE_HOLDS_OBJECT(node) ? json_node_get_object(node) : NULL;
		const gchar *name = section != NULL ? ls_string(section, "component") : NULL;
		JsonObject *component = name != NULL ? member_object(self->components, name) : NULL;
		JsonObject *content = section != NULL ? member_object(section, "content") : NULL;
		const gchar *variant = section != NULL ? ls_string(section, "variant") : NULL;
		g_autofree gchar *where = g_strdup_printf("section %u (%s)", i + 1, name != NULL ? name : "?");
		JsonObject *slots;
		g_autoptr(GList) keys = NULL;
		GList *item;
		if (section == NULL || !ls_object_keys(section, "|component|variant|id|content|", NULL)) {
			problem(problems, "%s must be {component, variant, id, content}", where);
			continue;
		}
		if (component == NULL) {
			GString *known = g_string_new(NULL);
			guint j;
			for (j = 0; j < self->order->len; j++)
				g_string_append_printf(known, "%s%s", j == 0 ? "" : ", ", (const gchar *)g_ptr_array_index(self->order, j));
			problem(problems, "%s: unknown component '%s'; use one of %s", where, name != NULL ? name : "", known->str);
			g_string_free(known, TRUE);
			continue;
		}
		if (variant != NULL && member_object(component, "variants") != NULL &&
			!json_object_has_member(member_object(component, "variants"), variant))
			problem(problems, "%s: variant '%s' does not exist", where, variant);
		if (json_object_has_member(section, "id") && !anchor_valid(ls_string(section, "id")))
			problem(problems, "%s: id must start with a letter and use letters, digits, - or _", where);
		if (content == NULL) {
			problem(problems, "%s: content must be an object", where);
			continue;
		}
		if (i == 0 && blank(ls_string(content, "heading")))
			problem(problems, "the first section must have a heading; it becomes the page h1");
		/* Structure follows the page kind: only home may open like a landing page. */
		if (i == 0 && kind != NULL && !kind_allows(kind, name)) {
			GString *openings = g_string_new(NULL);
			JsonArray *allowed = member_array(kind, "openings");
			guint j;
			for (j = 0; j < json_array_get_length(allowed); j++)
				g_string_append_printf(openings, "%s%s", j == 0 ? "" : " or ", json_array_get_string_element(allowed, j));
			problem(problems, "%s: %s %s page opens with %s", where,
				strchr("aeiou", kind_name[0]) != NULL ? "an" : "a", kind_name, openings->str);
			g_string_free(openings, TRUE);
		}
		if ((g_str_equal(name, "hero") || g_str_equal(name, "cover") || g_str_equal(name, "poster")) && g_strcmp0(kind_name, "home") != 0)
			problem(problems, "%s: %s belongs only on the home page; open this %s page with page-header",
				where, name, kind_name);
		slots = member_object(component, "slots");
		keys = json_object_get_members(content);
		for (item = keys; item != NULL; item = item->next)
			if (member_object(slots, item->data) == NULL)
				problem(problems, "%s: unknown slot '%s'", where, (const gchar *)item->data);
		g_list_free(g_steal_pointer(&keys));
		keys = json_object_get_members(slots);
		for (item = keys; item != NULL; item = item->next) {
			g_autofree gchar *slot_where = g_strdup_printf("%s %s", where, (const gchar *)item->data);
			check_value(&check, slot_where, item->data, member_object(slots, item->data),
				json_object_get_member(content, item->data));
		}
		check_component_rules(&check, where, name, variant != NULL ? variant : "", content);
	}
	if (json_object_has_member(page, "css")) {
		g_autofree gchar *why = is_string(json_object_get_member(page, "css"))
			? css_problem(ls_string(page, "css"), 12000) : g_strdup("css must be a string");
		if (why != NULL)
			problem(problems, "%s", why);
	}
	scope_clear(&scope);
}

gboolean
ls_components_check_page(LsComponents *self, JsonNode *plan, const gchar *path, JsonNode *page, GError **error){
	Problems problems = {g_ptr_array_new_with_free_func(g_free)};
	g_autofree gchar *subject = NULL;
	gboolean okay;
	g_return_val_if_fail(LS_IS_COMPONENTS(self) && plan != NULL && path != NULL, FALSE);
	check_page_into(self, &problems, plan, path, page, FALSE);
	subject = g_strdup_printf("The %s specification", path);
	okay = finish(&problems, subject, error);
	g_ptr_array_unref(problems.messages);
	return okay;
}

/* ---- Rendering ------------------------------------------------------------ */

typedef struct {
	LsComponents *self;
	JsonObject *plan;
	JsonObject *ui;			/* resolved interface strings */
	GHashTable *used;		/* component names used anywhere */
	GError *error;
	const gchar *page_path;		/* page being rendered */
	const gchar *page_title;
	const gchar *wordmark;
} Render;

static const gchar *
ui_text(Render *render, const gchar *key){
	return ls_string(render->ui, key);
}

static void
art_size(JsonObject *plan, const gchar *src, gint64 *width, gint64 *height){
	JsonArray *art = member_array(plan, "art");
	const gchar *ratio = "16:9";
	guint i;
	for (i = 0; art != NULL && i < json_array_get_length(art); i++) {
		JsonObject *work = json_array_get_object_element(art, i);
		if (g_strcmp0(ls_string(work, "path"), src) == 0 && ls_string(work, "ratio") != NULL)
			ratio = ls_string(work, "ratio");
	}
	*width = 1600;
	*height = 900;
	if (g_str_equal(ratio, "4:3")) { *width = 1200; *height = 900; }
	else if (g_str_equal(ratio, "3:2")) { *width = 1500; *height = 1000; }
	else if (g_str_equal(ratio, "1:1")) { *width = 1200; *height = 1200; }
	else if (g_str_equal(ratio, "3:4")) { *width = 900; *height = 1200; }
	else if (g_str_equal(ratio, "2:1")) { *width = 1600; *height = 800; }
	else if (g_str_equal(ratio, "3:1")) { *width = 1800; *height = 600; }
	else if (g_str_equal(ratio, "21:9")) { *width = 2100; *height = 900; }
}

static JsonNode *
string_node(const gchar *text){
	JsonNode *node = json_node_new(JSON_NODE_VALUE);
	json_node_set_string(node, text);
	return node;
}

static JsonNode *
rich_node(Render *render, const gchar *text){
	RichState state = {NULL, g_string_new(NULL)};
	g_autofree gchar *html = rich_render(&state, text);
	g_string_free(state.why, TRUE);
	(void)render;
	return string_node(html);
}

static JsonObject *
link_view(JsonObject *link, gboolean primary){
	JsonObject *view = json_object_new();
	json_object_set_string_member(view, "href", ls_string(link, "href"));
	json_object_set_string_member(view, "label", ls_string(link, "label"));
	json_object_set_boolean_member(view, "external", g_str_has_prefix(ls_string(link, "href"), "https://"));
	json_object_set_boolean_member(view, "primary", primary);
	return view;
}

static JsonNode *convert_value(Render *render, JsonObject *slot, JsonNode *value, gboolean eager);

/* Convert every field of an item and add position helpers for templates. */
static JsonObject *
item_view(Render *render, JsonObject *fields, JsonObject *item, guint index, gboolean eager){
	JsonObject *view = json_object_new();
	g_autoptr(GList) names = json_object_get_members(fields);
	GList *name;
	for (name = names; name != NULL; name = name->next) {
		JsonNode *value = json_object_get_member(item, name->data);
		JsonObject *slot = member_object(fields, name->data);
		/* Shadow absent fields so templates never read an outer value of the same name. */
		if (value == NULL || JSON_NODE_HOLDS_NULL(value)) {
			const gchar *type = ls_string(slot, "type");
			json_object_set_null_member(view, name->data);
			if (g_strcmp0(type, "items") == 0 || g_strcmp0(type, "links") == 0) {
				g_autofree gchar *has = g_strconcat("has_", (const gchar *)name->data, NULL);
				json_object_set_boolean_member(view, has, FALSE);
			}
		} else {
			json_object_set_member(view, name->data, convert_value(render, slot, value, eager));
			if (JSON_NODE_HOLDS_ARRAY(value)) {
				g_autofree gchar *has = g_strconcat("has_", (const gchar *)name->data, NULL);
				json_object_set_boolean_member(view, has, json_array_get_length(json_node_get_array(value)) > 0);
			}
		}
	}
	json_object_set_int_member(view, "n", index + 1);
	{
		g_autofree gchar *padded = g_strdup_printf("%02u", index + 1);
		json_object_set_string_member(view, "n2", padded);
	}
	return view;
}

static JsonNode *
convert_value(Render *render, JsonObject *slot, JsonNode *value, gboolean eager){
	const gchar *type = ls_string(slot, "type");
	JsonNode *node;
	if (g_str_equal(type, "rich"))
		return rich_node(render, json_node_get_string(value));
	if (g_str_equal(type, "link")) {
		node = json_node_new(JSON_NODE_OBJECT);
		json_node_take_object(node, link_view(json_node_get_object(value), TRUE));
		return node;
	}
	if (g_str_equal(type, "image")) {
		JsonObject *image = json_node_get_object(value);
		JsonObject *view = json_object_new();
		gint64 width, height;
		art_size(render->plan, ls_string(image, "src"), &width, &height);
		json_object_set_string_member(view, "src", ls_string(image, "src"));
		json_object_set_string_member(view, "alt", ls_string(image, "alt"));
		json_object_set_int_member(view, "width", width);
		json_object_set_int_member(view, "height", height);
		json_object_set_boolean_member(view, "eager", eager);
		node = json_node_new(JSON_NODE_OBJECT);
		json_node_take_object(node, view);
		return node;
	}
	if (g_str_equal(type, "links") || g_str_equal(type, "items")) {
		JsonArray *items = json_node_get_array(value);
		JsonArray *views = json_array_new();
		guint i;
		for (i = 0; i < json_array_get_length(items); i++) {
			JsonObject *item = json_array_get_object_element(items, i);
			json_array_add_object_element(views, g_str_equal(type, "links") ? link_view(item, i == 0)
				: item_view(render, member_object(slot, "fields"), item, i, eager));
		}
		node = json_node_new(JSON_NODE_ARRAY);
		json_node_take_array(node, views);
		return node;
	}
	return json_node_copy(value);
}

static void
form_view(Render *render, JsonObject *view, JsonObject *content, const gchar *id){
	g_autofree gchar *form_id = g_strconcat(id, "-form", NULL);
	const gchar *mode = ls_string(content, "mode");
	JsonArray *fields = member_array(content, "fields");
	JsonArray *views = json_array_new();
	guint i;
	json_object_set_string_member(view, "form_id", form_id);
	json_object_set_boolean_member(view, "local", g_strcmp0(mode, "local") == 0);
	json_object_set_boolean_member(view, "mailto", g_strcmp0(mode, "mailto") == 0);
	json_object_set_boolean_member(view, "endpoint", g_strcmp0(mode, "endpoint") == 0);
	if (g_strcmp0(mode, "mailto") == 0 && !g_str_has_prefix(ls_string(content, "target"), "mailto:")) {
		g_autofree gchar *target = g_strconcat("mailto:", ls_string(content, "target"), NULL);
		json_object_set_string_member(view, "target", target);
	}
	for (i = 0; fields != NULL && i < json_array_get_length(fields); i++) {
		JsonObject *field = json_array_get_object_element(fields, i);
		JsonObject *out = json_object_new();
		const gchar *type = ls_string(field, "type") != NULL ? ls_string(field, "type") : "text";
		g_autofree gchar *fid = g_strdup_printf("%s-%s", form_id, ls_string(field, "name"));
		g_autofree gchar *described = ls_string(field, "hint") != NULL
			? g_strdup_printf("%s-hint %s-error", fid, fid) : g_strdup_printf("%s-error", fid);
		JsonArray *options = member_array(field, "options");
		json_object_set_string_member(out, "fid", fid);
		json_object_set_string_member(out, "name", ls_string(field, "name"));
		json_object_set_string_member(out, "label", ls_string(field, "label"));
		json_object_set_string_member(out, "type", type);
		json_object_set_string_member(out, "describedby", described);
		json_object_set_string_member(out, "required_label", ui_text(render, "required"));
		if (ls_string(field, "hint") != NULL)
			json_object_set_string_member(out, "hint", ls_string(field, "hint"));
		if (ls_string(field, "autocomplete") != NULL)
			json_object_set_string_member(out, "autocomplete", ls_string(field, "autocomplete"));
		if (json_object_has_member(field, "maxlength"))
			json_object_set_int_member(out, "maxlength", json_object_get_int_member(field, "maxlength"));
		json_object_set_boolean_member(out, "required", json_object_has_member(field, "required") &&
			json_object_get_boolean_member(field, "required"));
		json_object_set_boolean_member(out, "is_textarea", g_str_equal(type, "textarea"));
		json_object_set_boolean_member(out, "is_select", g_str_equal(type, "select"));
		json_object_set_boolean_member(out, "is_checkbox", g_str_equal(type, "checkbox"));
		json_object_set_boolean_member(out, "is_input", !g_str_equal(type, "textarea") &&
			!g_str_equal(type, "select") && !g_str_equal(type, "checkbox"));
		if (options != NULL) {
			JsonArray *choices = json_array_new();
			guint j;
			for (j = 0; j < json_array_get_length(options); j++) {
				JsonObject *choice = json_object_new();
				json_object_set_string_member(choice, "value", json_array_get_string_element(options, j));
				json_object_set_string_member(choice, "label", json_array_get_string_element(options, j));
				json_array_add_object_element(choices, choice);
			}
			json_object_set_array_member(out, "options", choices);
		}
		json_array_add_object_element(views, out);
	}
	json_object_set_array_member(view, "fields", views);
}

static gchar *
render_template(Render *render, const gchar *name, JsonObject *view){
	g_autoptr(JsonNode) node = json_node_new(JSON_NODE_OBJECT);
	gchar *text;
	json_node_set_object(node, view);
	text = ls_template_render(file_text(render->self, name), node, render->error != NULL ? NULL : &render->error);
	return text != NULL ? text : g_strdup("");
}

/* Renders each section into @pieces and collects in-page contents entries
 * (headed sections after the opening, and article parts) into @toc. */
static gint
compare_letters(gconstpointer a, gconstpointer b){
	const gchar *left = *(const gchar *const *)a;
	const gchar *right = *(const gchar *const *)b;
	if (g_str_equal(left, "#") != g_str_equal(right, "#"))
		return g_str_equal(left, "#") ? 1 : -1;
	return g_utf8_collate(left, right);
}

/* Group directory entries under their initial letter, keeping the given order
 * within a letter; titles that start with a non-letter gather under '#'. */
static void
directory_view(JsonObject *view, const gchar *id){
	JsonArray *items = json_object_get_array_member(view, "items");
	g_autoptr(GHashTable) by_letter = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
		(GDestroyNotify)json_array_unref);
	g_autoptr(GPtrArray) letters = g_ptr_array_new();
	JsonArray *groups = json_array_new();
	guint i;
	for (i = 0; i < json_array_get_length(items); i++) {
		JsonNode *item = json_array_get_element(items, i);
		const gchar *title = ls_string(json_node_get_object(item), "title");
		gunichar first = title != NULL ? g_utf8_get_char(title) : 0;
		gchar letter[8] = {0};
		JsonArray *entries;
		if (g_unichar_isalpha(first))
			g_unichar_to_utf8(g_unichar_toupper(first), letter);
		else
			letter[0] = '#';
		entries = g_hash_table_lookup(by_letter, letter);
		if (entries == NULL) {
			gchar *key = g_strdup(letter);
			entries = json_array_new();
			g_hash_table_insert(by_letter, key, entries);
			g_ptr_array_add(letters, key);
		}
		json_array_add_element(entries, json_node_copy(item));
	}
	g_ptr_array_sort(letters, compare_letters);
	for (i = 0; i < letters->len; i++) {
		JsonObject *group = json_object_new();
		g_autofree gchar *group_id = g_strdup_printf("%s-g%u", id, i + 1);
		json_object_set_string_member(group, "letter", letters->pdata[i]);
		json_object_set_string_member(group, "group_id", group_id);
		json_object_set_array_member(group, "entries",
			json_array_ref(g_hash_table_lookup(by_letter, letters->pdata[i])));
		json_array_add_object_element(groups, group);
	}
	json_object_set_array_member(view, "groups", groups);
}

/* Headings may mark one or more words as *emphasis*; the look decides how
 * emphasis is drawn. Unpaired or empty markers stay literal text. */
static gchar *
heading_markup(const gchar *text, gboolean html){
	GString *out = g_string_new(NULL);
	const gchar *p = text != NULL ? text : "";
	while (*p != '\0') {
		const gchar *open = strchr(p, '*');
		const gchar *close = open != NULL ? strchr(open + 1, '*') : NULL;
		g_autofree gchar *before = NULL, *inner = NULL;
		if (open == NULL || close == NULL || close == open + 1 || g_ascii_isspace(open[1]) ||
			g_ascii_isspace(close[-1])) {
			g_autofree gchar *rest = html ? ls_template_escape(p) : g_strdup(p);
			g_string_append(out, rest);
			break;
		}
		before = g_strndup(p, (gsize)(open - p));
		inner = g_strndup(open + 1, (gsize)(close - open - 1));
		if (html) {
			g_autofree gchar *escaped_before = ls_template_escape(before);
			g_autofree gchar *escaped_inner = ls_template_escape(inner);
			g_string_append_printf(out, "%s<em class=\"em\">%s</em>", escaped_before, escaped_inner);
		} else
			g_string_append_printf(out, "%s%s", before, inner);
		p = close + 1;
	}
	return g_string_free(out, FALSE);
}

static void
render_sections(Render *render, JsonArray *sections, GPtrArray *pieces, JsonArray *toc){
	g_autoptr(GHashTable) anchors = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	g_autoptr(GPtrArray) ids = section_ids(sections, anchors);
	guint i;
	for (i = 0; i < json_array_get_length(sections); i++) {
		JsonObject *section = json_array_get_object_element(sections, i);
		const gchar *name = ls_string(section, "component");
		JsonObject *component = member_object(render->self->components, name);
		JsonObject *slots = member_object(component, "slots");
		JsonObject *content = member_object(section, "content");
		g_autoptr(GList) variants = json_object_get_members(member_object(component, "variants"));
		const gchar *variant = ls_string(section, "variant") != NULL ? ls_string(section, "variant")
			: (const gchar *)variants->data;
		const gchar *id = g_ptr_array_index(ids, i);
		g_autoptr(JsonObject) view = json_object_new();
		g_autoptr(JsonObject) wrapper = json_object_new();
		g_autoptr(GList) keys = json_object_get_members(slots);
		g_autofree gchar *file = g_strconcat(name, ".html", NULL);
		g_autofree gchar *flag = g_strconcat("v_", variant, NULL);
		g_autofree gchar *inner = NULL, *html = NULL;
		GList *key;
		guint k;
		g_hash_table_add(render->used, g_strdup(name));
		for (key = keys; key != NULL; key = key->next) {
			JsonNode *value = json_object_get_member(content, key->data);
			if (value == NULL || JSON_NODE_HOLDS_NULL(value))
				continue;
			json_object_set_member(view, key->data, convert_value(render, member_object(slots, key->data), value, i == 0));
			if (JSON_NODE_HOLDS_ARRAY(value)) {
				g_autofree gchar *has = g_strconcat("has_", (const gchar *)key->data, NULL);
				json_object_set_boolean_member(view, has, json_array_get_length(json_node_get_array(value)) > 0);
			}
		}
		if (ls_string(content, "heading") != NULL) {
			g_autofree gchar *plain = heading_markup(ls_string(content, "heading"), FALSE);
			g_autofree gchar *marked = heading_markup(ls_string(content, "heading"), TRUE);
			json_object_set_string_member(view, "heading", plain);
			json_object_set_string_member(view, "heading_html", marked);
		}
		for (k = 0; k < G_N_ELEMENTS(ui_keys); k++) {
			g_autofree gchar *ui_name = g_strconcat(ui_keys[k], "_label", NULL);
			json_object_set_string_member(view, ui_name, ui_text(render, ui_keys[k]));
		}
		json_object_set_string_member(view, "contents_label", ui_text(render, "contents"));
		json_object_set_string_member(view, "filter_label", ui_text(render, "filter"));
		json_object_set_string_member(view, "count_one", ui_text(render, "count_one"));
		json_object_set_string_member(view, "count_many", ui_text(render, "count_many"));
		json_object_set_string_member(view, "error_required", ui_text(render, "error_required"));
		json_object_set_string_member(view, "error_invalid", ui_text(render, "error_invalid"));
		json_object_set_string_member(view, "error_summary", ui_text(render, "error_summary"));
		json_object_set_string_member(view, "component", name);
		json_object_set_string_member(view, "variant", variant);
		json_object_set_boolean_member(view, flag, TRUE);
		json_object_set_string_member(view, "id", id);
		json_object_set_string_member(view, "htag", i == 0 ? "h1" : "h2");
		json_object_set_string_member(view, "subtag", i == 0 ? "h2" : "h3");
		/* Display sizes suit short headlines; long ones step down natively. */
		json_object_set_boolean_member(view, "long_heading", ls_string(view, "heading") != NULL &&
			g_utf8_strlen(ls_string(view, "heading"), -1) > 38);
		json_object_set_boolean_member(view, "has_text", json_object_has_member(content, "heading") ||
			json_object_has_member(content, "body"));
		json_object_set_boolean_member(view, "has_caption", json_object_has_member(content, "caption") ||
			json_object_has_member(content, "credit"));
		json_object_set_string_member(view, "crumbs_label", ui_text(render, "breadcrumb"));
		json_object_set_string_member(view, "pager_label", ui_text(render, "pager"));
		json_object_set_string_member(view, "previous_label", ui_text(render, "previous"));
		json_object_set_string_member(view, "next_label", ui_text(render, "next"));
		/* Interior openings carry a breadcrumb back home. */
		if (i == 0 && g_str_equal(name, "page-header") && g_strcmp0(render->page_path, "index.html") != 0) {
			JsonArray *crumbs = json_array_new();
			JsonObject *home = json_object_new();
			JsonObject *here = json_object_new();
			json_object_set_string_member(home, "href", "index.html");
			json_object_set_string_member(home, "label", render->wordmark);
			json_object_set_string_member(here, "label", render->page_title);
			json_array_add_object_element(crumbs, home);
			json_array_add_object_element(crumbs, here);
			json_object_set_array_member(view, "crumbs", crumbs);
			json_object_set_boolean_member(view, "has_crumbs", TRUE);
		}
		if (i > 0 && !blank(ls_string(content, "heading")) && !g_str_equal(name, "article")) {
			JsonObject *entry = json_object_new();
			json_object_set_string_member(entry, "id", id);
			json_object_set_string_member(entry, "label", ls_string(view, "heading"));
			json_array_add_object_element(toc, entry);
		}
		if (g_str_equal(name, "menu")) {
			JsonArray *menu_sections = json_object_get_array_member(view, "sections");
			guint j;
			for (j = 0; j < json_array_get_length(menu_sections); j++) {
				JsonObject *menu_section = json_array_get_object_element(menu_sections, j);
				g_autofree gchar *slug = slugify(ls_string(menu_section, "name"), 32);
				g_autofree gchar *section_id = g_strdup_printf("%s-%u-%s", id, j + 1, slug);
				json_object_set_string_member(menu_section, "section_id", section_id);
				json_object_set_string_member(menu_section, "section_key", section_id);
			}
			/* Section buttons reuse the filter component's script and styles. */
			if (!blank(ls_string(content, "all_label"))) {
				json_object_set_boolean_member(view, "filtered", TRUE);
				g_hash_table_add(render->used, g_strdup("filter"));
			}
		}
		if (g_str_equal(name, "article")) {
			JsonArray *parts = json_object_get_array_member(view, "parts");
			g_autoptr(GHashTable) part_anchors = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
			g_autoptr(GPtrArray) all = NULL;
			guint j;
			/* Recompute part ids exactly as the checker did, across the whole page. */
			all = section_ids(sections, part_anchors);
			for (j = 0; j < i; j++) {
				JsonObject *earlier = json_array_get_object_element(sections, j);
				if (g_strcmp0(ls_string(earlier, "component"), "article") == 0) {
					JsonArray *earlier_parts = member_array(member_object(earlier, "content"), "parts");
					guint m;
					for (m = 0; earlier_parts != NULL && m < json_array_get_length(earlier_parts); m++) {
						g_autofree gchar *skip = part_id(part_anchors, ls_string(
							json_array_get_object_element(earlier_parts, m), "heading"));
					}
				}
			}
			for (j = 0; parts != NULL && j < json_array_get_length(parts); j++) {
				JsonObject *part = json_array_get_object_element(parts, j);
				g_autofree gchar *pid = part_id(part_anchors, ls_string(part, "heading"));
				JsonObject *entry = json_object_new();
				json_object_set_string_member(part, "part_id", pid);
				json_object_set_string_member(entry, "id", pid);
				json_object_set_string_member(entry, "label", ls_string(part, "heading"));
				json_array_add_object_element(toc, entry);
			}
		}
		if (g_str_equal(name, "filter")) {
			JsonArray *categories = json_object_get_array_member(view, "categories");
			JsonArray *items = json_object_get_array_member(view, "items");
			guint j;
			for (j = 0; j < json_array_get_length(categories); j++) {
				JsonObject *category = json_array_get_object_element(categories, j);
				g_autofree gchar *key_text = slugify(ls_string(category, "label"), 40);
				json_object_set_string_member(category, "key", key_text);
			}
			for (j = 0; j < json_array_get_length(items); j++) {
				JsonObject *item = json_array_get_object_element(items, j);
				g_autofree gchar *key_text = slugify(ls_string(item, "category"), 40);
				json_object_set_string_member(item, "category_key", key_text);
			}
		}
		if (g_str_equal(name, "form"))
			form_view(render, view, content, id);
		if (g_str_equal(name, "directory"))
			directory_view(view, id);
		inner = render_template(render, file, view);
		json_object_set_string_member(wrapper, "component", name);
		json_object_set_string_member(wrapper, "variant", variant);
		json_object_set_string_member(wrapper, "id", id);
		json_object_set_string_member(wrapper, "inner", inner);
		json_object_set_boolean_member(wrapper, "labelled", !blank(ls_string(content, "heading")) &&
			!g_str_equal(name, "custom"));
		if (g_str_equal(name, "custom")) {
			json_object_set_string_member(wrapper, "label", ls_string(content, "label"));
			json_object_set_boolean_member(wrapper, "bleed", g_strcmp0(variant, "bleed") == 0);
		}
		html = render_template(render, "section.html", wrapper);
		g_ptr_array_add(pieces, g_steal_pointer(&html));
	}
}

static void
append_link_views(JsonArray *out, JsonArray *links, const gchar *path, GHashTable *seen){
	guint i;
	for (i = 0; links != NULL && i < json_array_get_length(links); i++) {
		JsonObject *link = json_array_get_object_element(links, i);
		JsonObject *view;
		if (!g_hash_table_add(seen, g_strdup(ls_string(link, "href"))))
			continue;
		view = link_view(link, FALSE);
		json_object_set_boolean_member(view, "current", g_strcmp0(ls_string(link, "href"), path) == 0);
		json_array_add_object_element(out, view);
	}
}

/* One body class per look axis, falling back to the catalog default. */
static gchar *
look_classes(LsComponents *self, JsonObject *look){
	GString *out = g_string_new(NULL);
	g_autoptr(GList) axes = json_object_get_members(self->looks);
	GList *axis;
	for (axis = axes; axis != NULL; axis = axis->next) {
		const gchar *value = look != NULL ? ls_string(look, axis->data) : NULL;
		if (value == NULL || ls_string(member_object(member_object(self->looks, axis->data), "values"), value) == NULL)
			value = look_default(self, axis->data);
		g_string_append_printf(out, "%slook-%s-%s", out->len > 0 ? " " : "", (const gchar *)axis->data, value);
	}
	return g_string_free(out, FALSE);
}

static gchar *
render_page(Render *render, const gchar *path, const gchar *title, const gchar *description,
	gboolean noindex, JsonObject *page, const gchar *base_url){
	g_autoptr(JsonObject) view = json_object_new();
	JsonObject *plan = render->plan;
	JsonObject *brand = member_object(plan, "brand");
	JsonObject *footer = member_object(plan, "footer");
	JsonObject *tokens = member_object(plan, "tokens");
	JsonArray *nav = json_array_new();
	JsonArray *footer_links = json_array_new();
	g_autoptr(GHashTable) seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	g_autoptr(JsonArray) legal = json_array_new();
	g_autoptr(GPtrArray) pieces = g_ptr_array_new_with_free_func(g_free);
	g_autoptr(JsonArray) toc = json_array_new();
	g_autofree gchar *slug = g_strndup(path, strlen(path) - 5);
	const gchar *kind_name = page_kind(plan, path, noindex ? page : NULL);
	JsonObject *kind = member_object(render->self->kinds, kind_name);
	GString *main = g_string_new(NULL);
	gboolean sidebar;
	const gchar *name = ls_string(plan, "name");
	g_autofree gchar *full = g_strcmp0(title, name) == 0 ? g_strdup(name) : g_strdup_printf("%s | %s", title, name);
	gdouble paper[3];
	guint i;
	json_object_set_string_member(view, "lang", ls_string(plan, "lang") != NULL ? ls_string(plan, "lang") : "en");
	json_object_set_string_member(view, "title", title);
	json_object_set_string_member(view, "full_title", full);
	json_object_set_string_member(view, "description", description);
	json_object_set_boolean_member(view, "noindex", noindex);
	if (base_url != NULL && !noindex) {
		g_autofree gchar *canonical = g_str_equal(path, "index.html") ? g_strdup(base_url)
			: g_strconcat(base_url, path, NULL);
		json_object_set_string_member(view, "canonical", canonical);
	}
	json_object_set_string_member(view, "site", name);
	json_object_set_string_member(view, "slug", slug);
	{
		g_autofree gchar *classes = look_classes(render->self, member_object(plan, "look"));
		json_object_set_string_member(view, "look_classes", classes);
	}
	json_object_set_boolean_member(view, "home", g_str_equal(path, "index.html"));
	parse_color(ls_string(tokens, "paper"), paper);
	json_object_set_string_member(view, "color_scheme", luminance(paper) < 0.2 ? "dark" : "light");
	json_object_set_string_member(view, "theme_color", ls_string(tokens, "paper"));
	json_object_set_string_member(view, "skip_label", ui_text(render, "skip"));
	json_object_set_string_member(view, "menu_label", ui_text(render, "menu"));
	json_object_set_string_member(view, "nav_label", ui_text(render, "nav"));
	json_object_set_string_member(view, "footer_label", ui_text(render, "footer_nav"));
	json_object_set_string_member(view, "wordmark", brand != NULL && ls_string(brand, "wordmark") != NULL
		? ls_string(brand, "wordmark") : name);
	if (brand != NULL && ls_string(brand, "mark") != NULL) {
		JsonObject *mark = json_object_new();
		json_object_set_string_member(mark, "src", ls_string(brand, "mark"));
		json_object_set_object_member(view, "mark", mark);
	}
	append_link_views(nav, member_array(plan, "nav"), path, seen);
	json_object_set_array_member(view, "nav", nav);
	if (member_object(plan, "action") != NULL) {
		JsonObject *action = link_view(member_object(plan, "action"), TRUE);
		json_object_set_object_member(view, "action", action);
	}
	render->page_path = path;
	render->page_title = title;
	render->wordmark = ls_string(view, "wordmark");
	render_sections(render, member_array(page, "sections"), pieces, toc);
	/* Reading kinds get an in-page contents column once there is enough to navigate. */
	sidebar = kind != NULL && json_object_has_member(kind, "sidebar") &&
		json_object_get_boolean_member(kind, "sidebar") && json_array_get_length(toc) >= 2 && pieces->len > 1;
	for (i = sidebar ? 1 : 0; i < pieces->len; i++)
		g_string_append(main, g_ptr_array_index(pieces, i));
	json_object_set_string_member(view, "kind", kind_name);
	json_object_set_boolean_member(view, "sidebar", sidebar);
	if (sidebar) {
		json_object_set_string_member(view, "opening", g_ptr_array_index(pieces, 0));
		json_object_set_array_member(view, "toc", json_array_ref(toc));
		json_object_set_string_member(view, "contents_label", ui_text(render, "contents"));
		if (ls_string(page, "aside") != NULL)
			json_object_set_member(view, "aside", rich_node(render, ls_string(page, "aside")));
	}
	json_object_set_string_member(view, "main", main->str);
	g_string_free(main, TRUE);
	if (footer != NULL && ls_string(footer, "note") != NULL) {
		JsonNode *note = rich_node(render, ls_string(footer, "note"));
		json_object_set_member(view, "footer_note", note);
	}
	g_hash_table_remove_all(seen);
	append_link_views(footer_links, footer != NULL ? member_array(footer, "links") : NULL, path, seen);
	for (i = 1; i < G_N_ELEMENTS(utility_routes); i++) {
		JsonObject *link = json_object_new();
		JsonObject *spec = json_node_get_object(render->self->utility[i]);
		const gchar *spec_title = ls_string(spec, "title");
		g_autofree gchar *label = g_strndup(spec_title, strcspn(spec_title, "("));
		json_object_set_string_member(link, "href", utility_routes[i]);
		json_object_set_string_member(link, "label", g_strstrip(label));
		json_array_add_object_element(legal, link);
	}
	append_link_views(footer_links, legal, path, seen);
	json_object_set_array_member(view, "footer_links", footer_links);
	if (ls_string(page, "css") != NULL)
		json_object_set_string_member(view, "page_css", ls_string(page, "css"));
	{
		g_autofree gchar *fonts = font_href(plan);
		if (fonts != NULL)
			json_object_set_string_member(view, "font_href", fonts);
	}
	return render_template(render, "layout.html", view);
}

static void
append_number(GString *out, const gchar *name, gdouble value, const gchar *unit){
	gchar buffer[G_ASCII_DTOSTR_BUF_SIZE];
	g_string_append_printf(out, "\t--%s: %s%s;\n", name,
		g_ascii_formatd(buffer, sizeof buffer, "%.4g", value), unit);
}

/* Tokens become custom properties; derived colors keep contrast guarantees. */
static gchar *
token_css(JsonObject *tokens){
	GString *out = g_string_new(":root {\n");
	const gchar *paper = ls_string(tokens, "paper");
	const gchar *accent = ls_string(tokens, "accent");
	const gchar *density = ls_string(tokens, "density") != NULL ? ls_string(tokens, "density") : "regular";
	const gdouble multipliers[] = {0.25, 0.5, 0.75, 1, 1.5, 2, 3, 4.5};
	gdouble base = token_number(tokens, &number_tokens[0]);
	gdouble ratio = token_number(tokens, &number_tokens[1]);
	gdouble space = token_number(tokens, &number_tokens[5]);
	gdouble border = token_number(tokens, &number_tokens[4]);
	gdouble rgb[3];
	gint step;
	guint i;
	for (i = 0; i < G_N_ELEMENTS(color_tokens); i++) {
		g_autofree gchar *name = g_strdelimit(g_strdup(color_tokens[i]), "_", '-');
		g_string_append_printf(out, "\t--%s: %s;\n", name, ls_string(tokens, color_tokens[i]));
	}
	g_string_append_printf(out, "\t--accent-text: %s;\n", contrast(accent, paper) >= 4.5 ? accent : ls_string(tokens, "ink"));
	g_string_append_printf(out, "\t--focus: %s;\n", contrast(accent, paper) >= 3 ? accent : ls_string(tokens, "ink"));
	g_string_append_printf(out, "\t--line-strong: %s;\n", contrast(ls_string(tokens, "line"), paper) >= 3
		? ls_string(tokens, "line") : ls_string(tokens, "muted"));
	parse_color(paper, rgb);
	g_string_append_printf(out, "\t--danger: %s;\n", luminance(rgb) > 0.4
		? (contrast("#a4161a", paper) >= 4.5 ? "#a4161a" : ls_string(tokens, "ink"))
		: (contrast("#ff8a80", paper) >= 4.5 ? "#ff8a80" : ls_string(tokens, "ink")));
	g_string_append(out, "\t--field: var(--paper);\n");
	for (i = 0; i < G_N_ELEMENTS(font_tokens); i++) {
		g_autofree gchar *name = g_strdelimit(g_strdup(font_tokens[i]), "_", '-');
		g_string_append_printf(out, "\t--%s: %s;\n", name, ls_string(tokens, font_tokens[i]));
	}
	for (step = -1; step <= 7; step++) {
		gdouble size = base * pow(ratio, step);
		g_autofree gchar *name = step < 0 ? g_strdup("step--1") : g_strdup_printf("step-%d", step);
		if (step <= 1)
			append_number(out, name, size, "rem");
		else {
			gdouble low = MAX(base * ratio, base * pow(ratio, step * 0.55));
			gdouble slope = (size - low) / 57.5;
			gchar a[G_ASCII_DTOSTR_BUF_SIZE], b[G_ASCII_DTOSTR_BUF_SIZE], c[G_ASCII_DTOSTR_BUF_SIZE], d[G_ASCII_DTOSTR_BUF_SIZE];
			g_string_append_printf(out, "\t--%s: clamp(%srem, calc(%srem + %svw), %srem);\n", name,
				g_ascii_formatd(a, sizeof a, "%.4g", low),
				g_ascii_formatd(b, sizeof b, "%.4g", low - 22.5 * slope),
				g_ascii_formatd(c, sizeof c, "%.4g", slope * 100),
				g_ascii_formatd(d, sizeof d, "%.4g", size));
		}
	}
	for (i = 0; i < G_N_ELEMENTS(multipliers); i++) {
		g_autofree gchar *name = g_strdup_printf("space-%u", i + 1);
		append_number(out, name, space * multipliers[i], "rem");
	}
	append_number(out, "measure", token_number(tokens, &number_tokens[2]), "ch");
	append_number(out, "radius", token_number(tokens, &number_tokens[3]), "px");
	append_number(out, "border", border, "px");
	append_number(out, "border-strong", MAX(2, border), "px");
	append_number(out, "max-width", token_number(tokens, &number_tokens[9]), "rem");
	g_string_append(out, "\t--gutter: clamp(1rem, 4vw, 2.5rem);\n");
	g_string_append_printf(out, "\t--section-space: %s;\n", g_str_equal(density, "compact")
		? "clamp(2.5rem, 5vw, 4rem)" : g_str_equal(density, "airy") ? "clamp(4rem, 10vw, 8.5rem)"
		: "clamp(3rem, 7vw, 6rem)");
	append_number(out, "heading-weight", token_number(tokens, &number_tokens[6]), "");
	append_number(out, "heading-tracking", token_number(tokens, &number_tokens[7]), "em");
	append_number(out, "heading-leading", token_number(tokens, &number_tokens[8]), "");
	g_string_append_printf(out, "\t--heading-case: %s;\n",
		g_strcmp0(ls_string(tokens, "heading_case"), "uppercase") == 0 ? "uppercase" : "none");
	g_string_append(out, "}\n");
	return g_string_free(out, FALSE);
}

static void
add_file(JsonArray *files, const gchar *path, const gchar *content){
	JsonObject *file = json_object_new();
	json_object_set_string_member(file, "path", path);
	json_object_set_string_member(file, "content", content);
	json_array_add_object_element(files, file);
}

static gchar *
normalize_base(const gchar *base_url, GError **error){
	const gchar *host;
	if (base_url == NULL)
		return NULL;
	host = g_str_has_prefix(base_url, "https://") ? base_url + 8 : NULL;
	if (host == NULL || *host == '\0' || *host == '/' || strpbrk(base_url, " ?#@\"'<>\\") != NULL) {
		g_set_error(error, LS_ERROR, LS_ERROR_INVALID,
			"Site address '%s' must be an absolute https:// URL without credentials, query or fragment", base_url);
		return NULL;
	}
	return g_str_has_suffix(base_url, "/") ? g_strdup(base_url) : g_strconcat(base_url, "/", NULL);
}

gchar *
ls_components_compose(LsComponents *self, JsonNode *plan_node, JsonObject *pages, const gchar *base_url, GError **error){
	Problems problems = {g_ptr_array_new_with_free_func(g_free)};
	Render render = {self, NULL, NULL, NULL, NULL, NULL, NULL, NULL};
	g_autoptr(JsonObject) ui = json_object_new();
	g_autoptr(GHashTable) used = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	g_autoptr(JsonNode) root = json_node_new(JSON_NODE_OBJECT);
	g_autoptr(JsonObject) bundle = json_object_new();
	g_autofree gchar *base = NULL;
	JsonArray *files = json_array_new();
	JsonArray *planned;
	JsonObject *plan;
	GString *css, *js, *sitemap = NULL;
	GString *robots = g_string_new("User-agent: *\nAllow: /\n");
	guint i;
	g_return_val_if_fail(LS_IS_COMPONENTS(self) && plan_node != NULL && pages != NULL, NULL);
	json_object_set_array_member(bundle, "files", files);
	check_plan_into(&problems, self, plan_node);
	if (!finish(&problems, "The site plan", error))
		goto fail;
	base = normalize_base(base_url, error);
	if (base_url != NULL && base == NULL)
		goto fail;
	plan = json_node_get_object(plan_node);
	planned = member_array(plan, "pages");
	for (i = 0; i < json_array_get_length(planned); i++) {
		const gchar *path = ls_string(json_array_get_object_element(planned, i), "path");
		JsonNode *page = json_object_get_member(pages, path);
		if (page == NULL) {
			problem(&problems, "%s has no page specification", path);
			continue;
		}
		check_page_into(self, &problems, plan_node, path, page, FALSE);
	}
	for (i = 0; i < G_N_ELEMENTS(utility_routes); i++)
		check_page_into(self, &problems, plan_node, utility_routes[i], self->utility[i], TRUE);
	if (!finish(&problems, "The composed site", error))
		goto fail;
	for (i = 0; i < G_N_ELEMENTS(ui_keys); i++) {
		JsonObject *custom = member_object(plan, "ui");
		json_object_set_string_member(ui, ui_keys[i], custom != NULL && ls_string(custom, ui_keys[i]) != NULL
			? ls_string(custom, ui_keys[i]) : ui_defaults[i]);
	}
	render.plan = plan;
	render.ui = ui;
	render.used = used;
	if (base != NULL) {
		sitemap = g_string_new("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
			"<urlset xmlns=\"http://www.sitemaps.org/schemas/sitemap/0.9\">\n");
		g_string_append_printf(robots, "Sitemap: %ssitemap.xml\n", base);
	}
	for (i = 0; i < json_array_get_length(planned); i++) {
		JsonObject *entry = json_array_get_object_element(planned, i);
		const gchar *path = ls_string(entry, "path");
		g_autofree gchar *html = render_page(&render, path, ls_string(entry, "title"),
			ls_string(entry, "description"), FALSE, json_node_get_object(json_object_get_member(pages, path)), base);
		add_file(files, path, html);
		if (sitemap != NULL) {
			g_autofree gchar *loc = g_str_equal(path, "index.html") ? g_strdup(base) : g_strconcat(base, path, NULL);
			g_autofree gchar *escaped = ls_template_escape(loc);
			g_string_append_printf(sitemap, "  <url><loc>%s</loc></url>\n", escaped);
		}
	}
	for (i = 0; i < G_N_ELEMENTS(utility_routes); i++) {
		JsonObject *spec = json_node_get_object(self->utility[i]);
		g_autofree gchar *html = render_page(&render, utility_routes[i], ls_string(spec, "title"),
			ls_string(spec, "description"), TRUE, spec, base);
		add_file(files, utility_routes[i], html);
	}
	if (render.error != NULL) {
		g_propagate_error(error, g_steal_pointer(&render.error));
		if (sitemap != NULL)
			g_string_free(sitemap, TRUE);
		goto fail;
	}
	/* Library styles sit in cascade layers; the plan's restyles and CSS are
	 * unlayered, so any rule a site writes overrides any library rule. */
	css = g_string_new("/* Generated by libresite from the site plan and component library. */\n"
		"@layer ls-base, ls-components, ls-look;\n");
	{
		g_autofree gchar *tokens = token_css(member_object(plan, "tokens"));
		g_string_append(css, tokens);
	}
	g_string_append_printf(css, "@layer ls-base {\n%s}\n", file_text(self, "base.css"));
	js = g_string_new("/* Generated by libresite from the component library. */\n");
	g_string_append(js, file_text(self, "nav.js"));
	for (i = 0; i < self->order->len; i++) {
		const gchar *name = g_ptr_array_index(self->order, i);
		g_autofree gchar *style = g_strconcat(name, ".css", NULL);
		g_autofree gchar *script = g_strconcat(name, ".js", NULL);
		if (!g_hash_table_contains(used, name))
			continue;
		if (file_text(self, style) != NULL)
			g_string_append_printf(css, "\n/* %s */\n@layer ls-components {\n%s}\n", name, file_text(self, style));
		if (file_text(self, script) != NULL)
			g_string_append_printf(js, "\n%s", file_text(self, script));
	}
	g_string_append_printf(css, "\n/* look */\n@layer ls-look {\n%s}\n", file_text(self, "look.css"));
	for (i = 0; member_object(plan, "restyle") != NULL && i < self->order->len; i++) {
		const gchar *name = g_ptr_array_index(self->order, i);
		const gchar *restyle = ls_string(member_object(plan, "restyle"), name);
		if (restyle != NULL && g_hash_table_contains(used, name))
			g_string_append_printf(css, "\n/* Site restyle: %s */\n%s\n", name, restyle);
	}
	if (ls_string(plan, "css") != NULL)
		g_string_append_printf(css, "\n/* Site plan */\n%s\n", ls_string(plan, "css"));
	add_file(files, "styles.css", css->str);
	add_file(files, "site.js", js->str);
	add_file(files, "robots.txt", robots->str);
	g_string_free(css, TRUE);
	g_string_free(js, TRUE);
	if (sitemap != NULL) {
		g_string_append(sitemap, "</urlset>\n");
		add_file(files, "sitemap.xml", sitemap->str);
		g_string_free(sitemap, TRUE);
	}
	json_object_set_string_member(bundle, "summary", "Composed site from the component library");
	json_node_set_object(root, bundle);
	g_string_free(robots, TRUE);
	g_ptr_array_unref(problems.messages);
	return ls_json_encode(root);
fail:
	g_string_free(robots, TRUE);
	g_ptr_array_unref(problems.messages);
	return NULL;
}
