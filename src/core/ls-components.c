/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "core/ls-components.h"
#include "core/ls-design.h"
#include "core/ls-module.h"
#include "core/ls-visual.h"
#include "core/ls-taste.h"
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
	JsonNode *pairing_catalog;	/* pairings.yaml, merged into typefaces */
	JsonObject *sectors;	/* sector name -> description */
	JsonObject *palettes;
	JsonObject *typefaces;
	JsonObject *homes;
	JsonObject *pictures;	/* bold opening -> needs, shows, photo or hides; NULL without */
	JsonObject *taste;	/* openings and variants an unthemed site never shows; NULL without */
	JsonObject *fallback;	/* the default block: the one design unthemed sites wear */
	JsonObject *library;	/* the library block the design picker reads; NULL without */
	JsonNode *network_catalog;
	JsonObject *networks;	/* social network -> {label, hosts, path}; NULL without networks.yaml */
	GPtrArray *order;
	GPtrArray *extras;	/* NAME--VARIANT of each extra variant, with its own styles */
	GHashTable *files;	/* resource-relative name -> contents */
	GHashTable *snapshot;	/* archived name -> contents; NULL reads the GResource */
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
	g_clear_pointer(&self->network_catalog, json_node_unref);
	g_clear_pointer(&self->order, g_ptr_array_unref);
	g_clear_pointer(&self->files, g_hash_table_unref);
	g_clear_pointer(&self->snapshot, g_hash_table_unref);
	for (i = 0; i < G_N_ELEMENTS(self->utility); i++)
		g_clear_pointer(&self->utility[i], json_node_unref);
	g_free(self->plan_schema);
	g_free(self->themed_plan_schema);
	g_clear_pointer(&self->theme_catalog, json_node_unref);
	g_clear_pointer(&self->identity_catalog, json_node_unref);
	g_clear_pointer(&self->extras, g_ptr_array_unref);
	g_clear_pointer(&self->pairing_catalog, json_node_unref);
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

static gchar *accent_text(const gchar *accent, const gchar *paper, const gchar *surface, const gchar *ink);
static void color_css(GString *out, JsonObject *tokens);
static const gchar *plan_scheme(JsonObject *plan);

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

/* Escape text, adding soft break points inside long email addresses and web
 * addresses (after @ and /, before dots) so a narrow column breaks them at a
 * seam instead of mid-word. Other words are left alone. */
static void
append_breakable(GString *out, const gchar *text, gsize length){
	gsize i = 0;
	while (i < length) {
		gsize end = i, k;
		gboolean address = FALSE;
		while (end < length && !g_ascii_isspace(text[end]))
			end++;
		for (k = i; k < end; k++)
			if (text[k] == '@' || (text[k] == '/' && k + 1 < end && text[k + 1] == '/'))
				address = TRUE;
		address = address && end - i >= 16;
		for (k = i; k < end; k++) {
			if (address && text[k] == '.' && k > i && k + 1 < end && g_ascii_isalnum(text[k + 1]))
				g_string_append(out, "<wbr>");
			append_escaped(out, text + k, 1);
			if (address && (text[k] == '@' || text[k] == '/') && k + 1 < end && text[k + 1] != '/')
				g_string_append(out, "<wbr>");
		}
		while (end < length && g_ascii_isspace(text[end]))
			append_escaped(out, text + end++, 1);
		i = end;
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
			const gchar *run = s + 1;
			while (run < end && *run != '`' && *run != '*' && *run != '[')
				run++;
			append_breakable(out, s, (gsize)(run - s));
			s = run;
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

/* Whether @s to @end is one link and nothing else: such a line stands
 * alone, so its paragraph or list item is marked as a link line and the
 * link becomes a full-size tap target instead of a link in running text. */
static gboolean
link_only(const gchar *s, const gchar *end){
	const gchar *middle = s < end && *s == '[' ? find(s + 1, end, "](") : NULL;
	const gchar *stop = middle != NULL ? find(middle + 2, end, ")") : NULL;
	return middle != NULL && stop != NULL && middle > s + 1 && stop > middle + 2 && stop + 1 == end;
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
				g_string_append(out, link_only(paragraph->str, paragraph->str + paragraph->len) ? "<p class=\"link-line\">" : "<p>");
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
		g_string_append(out, link_only(content, content + strlen(content)) ? "<li class=\"link-line\">" : "<li>");
		rich_inline(state, content, content + strlen(content), out);
		g_string_append(out, "</li>");
	}
	if (paragraph->len > 0) {
		g_string_append(out, link_only(paragraph->str, paragraph->str + paragraph->len) ? "<p class=\"link-line\">" : "<p>");
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
	/* Standalone art is shown through <img>, where the site's fonts never
	 * load: lettering falls back to stray system glyphs. Words belong in the
	 * page, where they are also readable and translatable. */
	if (check->svg && (g_str_equal(local, "text") || g_str_equal(local, "tspan") ||
		g_str_equal(local, "textpath"))) {
		g_string_printf(check->why, "<%s> is not allowed: artwork has no lettering; put words in the page", element);
		return;
	}
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

/* The signature layer: plain CSS in the site's own tokens. Colours come only
 * from the custom properties, so dark mode and the contrast guarantees hold;
 * nothing may hide, pin or force content. Shapes are percent-encoded SVG data
 * images, used as masks or backgrounds. */
static gchar *
signature_problem(const gchar *css){
	const gchar *banned[] = {"!important", "display:none", "display: none", "visibility:hidden",
		"visibility: hidden", "position:fixed", "position: fixed", "rgb(", "rgba(", "hsl(", "hsla(",
		"hwb(", "lab(", "lch(", "color-mix(", "font-size:0", "font-size: 0", "content: attr", "content:attr"};
	g_autofree gchar *lower = NULL;
	GString *plain;
	const gchar *p;
	gchar *why;
	guint i;
	if (css == NULL || strlen(css) > 8000)
		return g_strdup("the signature must be CSS of at most 8000 bytes");
	lower = g_ascii_strdown(css, -1);
	for (i = 0; i < G_N_ELEMENTS(banned); i++)
		if (strstr(lower, banned[i]) != NULL)
			return g_strdup_printf("the signature must not contain '%s'", banned[i]);
	/* url() may hold only a percent-encoded SVG data image. */
	plain = g_string_new(NULL);
	for (p = lower; *p != '\0'; p++) {
		if (g_str_has_prefix(p, "url(")) {
			const gchar *target = p + 4, *end;
			while (*target == ' ' || *target == '"' || *target == '\'')
				target++;
			if (!g_str_has_prefix(target, "data:image/svg+xml,") || strchr(target, '<') != NULL ||
				(end = strchr(target, ')')) == NULL || memchr(target, '<', (gsize)(end - target)) != NULL) {
				g_string_free(plain, TRUE);
				return g_strdup("signature url() may only hold a percent-encoded data:image/svg+xml image");
			}
			if (strstr(target, "script") != NULL || strstr(target, "href") != NULL) {
				g_string_free(plain, TRUE);
				return g_strdup("signature SVG images may not hold scripts or links");
			}
			p = end;
			continue;
		}
		g_string_append_c(plain, *p);
	}
	/* Outside images, colours are tokens: no hex literals. */
	for (p = plain->str; *p != '\0'; p++)
		if (*p == '#' && g_ascii_isxdigit(p[1]) && g_ascii_isxdigit(p[2]) && g_ascii_isxdigit(p[3]) &&
			(p == plain->str || strchr(" :,(", p[-1]) != NULL)) {
			g_string_free(plain, TRUE);
			return g_strdup("signature colours must be var(--paper), var(--ink), var(--accent) and the other tokens, not literals");
		}
	g_string_free(plain, TRUE);
	why = NULL;
	{
		gint depth = 0;
		for (p = css; *p != '\0'; p++) {
			if (*p == '{')
				depth++;
			else if (*p == '}' && --depth < 0)
				break;
		}
		if (depth != 0)
			why = g_strdup("the signature has unbalanced braces");
	}
	if (why == NULL && (strstr(lower, "</") != NULL || strstr(lower, "@import") != NULL ||
		strstr(lower, "expression(") != NULL || strstr(lower, "javascript:") != NULL || strstr(lower, "@charset") != NULL))
		why = g_strdup("the signature must not contain markup, imports, scripts or expressions");
	return why;
}

gboolean
ls_components_check_signature(const gchar *css, GError **error){
	g_autofree gchar *why = signature_problem(css);
	if (why != NULL) {
		g_set_error_literal(error, LS_ERROR, LS_ERROR_INVALID, why);
		return FALSE;
	}
	return TRUE;
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
	g_autoptr(GBytes) bytes = NULL;
	const gchar *archived;
	gchar *text;
	/* An archived library never falls back to newer embedded bytes. */
	if (self->snapshot != NULL) {
		archived = g_hash_table_lookup(self->snapshot, name);
		if (archived != NULL)
			bytes = g_bytes_new(archived, strlen(archived));
		else
			g_set_error(&local_error, LS_ERROR, LS_ERROR_INVALID,
				"Archived component library lacks %s", name);
	} else
		bytes = g_resource_lookup_data(ls_resources_get_resource(), path, 0, &local_error);
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
static void check_photos(Problems *problems, JsonObject *plan);
static gboolean load_themes(LsComponents *self, GChecksum *digest, GError **error);
static gboolean load_identities(LsComponents *self, GChecksum *digest, GError **error);
static gboolean load_networks(LsComponents *self, GChecksum *digest, GError **error);

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
		JsonNode *plain = definition != NULL ? json_object_get_member(definition, "plain") : NULL;
		JsonNode *builder = definition != NULL ? json_object_get_member(definition, "builder") : NULL;
		if (definition == NULL || !ls_object_keys(definition, "|summary|values|plain|builder|", NULL) ||
			ls_string(definition, "summary") == NULL || values == NULL || json_object_get_size(values) < 2)
			return catalog_invalid(error, axis->data, "look axes need a summary and two or more values");
		if (plain != NULL && (!JSON_NODE_HOLDS_VALUE(plain) || json_node_get_value_type(plain) != G_TYPE_INT64 ||
			json_node_get_int(plain) < 0 || json_node_get_int(plain) > 100))
			return catalog_invalid(error, axis->data, "plain is a percentage from 0 to 100");
		if (builder != NULL && (!JSON_NODE_HOLDS_VALUE(builder) || json_node_get_value_type(builder) != G_TYPE_BOOLEAN))
			return catalog_invalid(error, axis->data, "builder is true or false");
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

/* Read an archived {version: 1, files: {name: text}} library into @self. */
static gboolean
read_snapshot(LsComponents *self, const gchar *snapshot, GError **error){
	g_autoptr(JsonNode) node = ls_json_parse(snapshot, error);
	JsonObject *root, *files;
	g_autoptr(GList) names = NULL;
	GList *item;
	JsonNode *version;
	if (node == NULL)
		return FALSE;
	root = JSON_NODE_HOLDS_OBJECT(node) ? json_node_get_object(node) : NULL;
	version = root != NULL ? json_object_get_member(root, "version") : NULL;
	files = root != NULL ? member_object(root, "files") : NULL;
	if (root == NULL || !ls_object_keys(root, "|version|files|", NULL) || files == NULL ||
		version == NULL || !JSON_NODE_HOLDS_VALUE(version) ||
		json_node_get_value_type(version) != G_TYPE_INT64 || json_node_get_int(version) != 1)
		goto invalid;
	self->snapshot = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
	names = json_object_get_members(files);
	for (item = names; item != NULL; item = item->next) {
		JsonNode *value = json_object_get_member(files, item->data);
		if (!JSON_NODE_HOLDS_VALUE(value) || json_node_get_value_type(value) != G_TYPE_STRING)
			goto invalid;
		g_hash_table_replace(self->snapshot, g_strdup(item->data), g_strdup(json_node_get_string(value)));
	}
	return TRUE;
invalid:
	g_set_error_literal(error, LS_ERROR, LS_ERROR_INVALID,
		"Archived component library must be {version: 1, files: {name: text}}");
	return FALSE;
}

LsComponents *
ls_components_new(GError **error){
	return ls_components_new_from_snapshot(NULL, error);
}

gchar *
ls_components_get_snapshot(LsComponents *self){
	g_autoptr(JsonBuilder) builder = json_builder_new();
	g_autoptr(JsonNode) node = NULL;
	g_autoptr(GList) names = NULL;
	GList *item;
	g_return_val_if_fail(LS_IS_COMPONENTS(self), NULL);
	/* Sorted names make equal libraries archive to equal bytes. */
	names = g_list_sort(g_hash_table_get_keys(self->files), (GCompareFunc)strcmp);
	json_builder_begin_object(builder);
	json_builder_set_member_name(builder, "version");
	json_builder_add_int_value(builder, 1);
	json_builder_set_member_name(builder, "files");
	json_builder_begin_object(builder);
	for (item = names; item != NULL; item = item->next) {
		json_builder_set_member_name(builder, item->data);
		json_builder_add_string_value(builder, g_hash_table_lookup(self->files, item->data));
	}
	json_builder_end_object(builder);
	json_builder_end_object(builder);
	node = json_builder_get_root(builder);
	return ls_json_encode(node);
}

static gboolean is_opening(LsComponents *self, const gchar *opening);
static gboolean in_list(const gchar *const *list, const gchar *name);

static gboolean
json_list_has(JsonArray *list, const gchar *name){
	guint i;
	for (i = 0; list != NULL && i < json_array_get_length(list); i++)
		if (JSON_NODE_HOLDS_VALUE(json_array_get_element(list, i)) &&
			g_strcmp0(json_array_get_string_element(list, i), name) == 0)
			return TRUE;
	return FALSE;
}


static gboolean json_list_has(JsonArray *list, const gchar *name);

/* ---- Section forms and extras ------------------------------------------------ */

/* Joins @extra, a map of look axes to new values with descriptions, to the
 * catalog's axes, each value with its look-AXIS-VALUE.css. @owner holds
 * @name; without the member nothing joins. */
static gboolean
join_looks(LsComponents *self, GChecksum *digest, JsonObject *owner, const gchar *name, JsonObject *extra, GError **error){
	g_autoptr(GList) axes = NULL;
	GList *axis;
	if (owner == NULL || !json_object_has_member(owner, name))
		return TRUE;
	if (extra == NULL)
		return catalog_invalid(error, name, "must map look axes to new values"), FALSE;
	axes = json_object_get_members(extra);
	for (axis = axes; axis != NULL; axis = axis->next) {
		JsonObject *values = member_object(member_object(self->looks, axis->data), "values");
		JsonObject *added = member_object(extra, axis->data);
		g_autoptr(GList) fresh = added != NULL ? json_object_get_members(added) : NULL;
		GList *value;
		if (values == NULL || added == NULL)
			return catalog_invalid(error, axis->data, "extra looks extend catalog look axes"), FALSE;
		for (value = fresh; value != NULL; value = value->next) {
			g_autofree gchar *file = g_strdup_printf("look-%s-%s.css", (const gchar *)axis->data, (const gchar *)value->data);
			g_autofree gchar *style = NULL;
			if (json_object_has_member(values, value->data) || ls_string(added, value->data) == NULL)
				return catalog_invalid(error, axis->data, "an extra look value is new and has a description"), FALSE;
			style = load_resource(self, digest, file, TRUE, error);
			if (style == NULL)
				return FALSE;
			json_object_set_string_member(values, value->data, ls_string(added, value->data));
		}
	}
	return TRUE;
}

/* The wider block's openings: new variants of the home opening components,
 * each restyling the component's own markup in COMPONENT--VARIANT.css. */
static gboolean
join_openings(LsComponents *self, GChecksum *digest, JsonObject *root, const gchar *block, GError **error){
	JsonObject *wider = member_object(root, block);
	JsonObject *openings = member_object(wider, "openings");
	JsonArray *home = member_array(member_object(self->kinds, "home"), "openings");
	g_autoptr(GList) owners = NULL;
	GList *owner;
	if (wider == NULL || !json_object_has_member(wider, "openings"))
		return TRUE;
	if (openings == NULL)
		return catalog_invalid(error, "openings", "must map home openings to new variants"), FALSE;
	owners = json_object_get_members(openings);
	for (owner = owners; owner != NULL; owner = owner->next) {
		JsonObject *variants = member_object(member_object(self->components, owner->data), "variants");
		JsonObject *added = member_object(openings, owner->data);
		g_autoptr(GList) values = added != NULL ? json_object_get_members(added) : NULL;
		GList *value;
		if (variants == NULL || added == NULL || !json_list_has(home, owner->data) || g_str_equal(owner->data, "custom"))
			return catalog_invalid(error, owner->data, "wider openings extend the home opening components"), FALSE;
		for (value = values; value != NULL; value = value->next) {
			g_autofree gchar *file = g_strdup_printf("%s--%s.css", (const gchar *)owner->data, (const gchar *)value->data);
			g_autofree gchar *style = NULL;
			if (json_object_has_member(variants, value->data) || ls_string(added, value->data) == NULL)
				return catalog_invalid(error, owner->data, "a wider opening is new and has a description"), FALSE;
			style = load_resource(self, digest, file, TRUE, error);
			if (style == NULL)
				return FALSE;
			json_object_set_string_member(variants, value->data, ls_string(added, value->data));
			g_ptr_array_add(self->extras, g_strdup_printf("%s--%s", (const gchar *)owner->data, (const gchar *)value->data));
		}
	}
	return TRUE;
}

/* ---- Bold openings and forms ------------------------------------------------ */

/* The wider and bold blocks (docs/components.org, "Look") hold only the
 * members documented for them. */
static gboolean
check_block_keys(JsonObject *root, const gchar *block, const gchar *keys, GError **error){
	if (!json_object_has_member(root, block))
		return TRUE;
	if (member_object(root, block) == NULL || !ls_object_keys(member_object(root, block), keys, NULL))
		return catalog_invalid(error, block, "holds only the members documented for it"), FALSE;
	return TRUE;
}

/* The bold block's components join the catalog's before anything checks
 * them: each loads NAME.html and its styles like the rest. */
static gboolean
embolden(JsonObject *root, GError **error){
	JsonObject *bold = member_object(root, "bold");
	JsonObject *components = member_object(root, "components");
	JsonObject *added = member_object(bold, "components");
	g_autoptr(GList) names = NULL;
	GList *name;
	if (bold == NULL || !json_object_has_member(bold, "components"))
		return TRUE;
	if (added == NULL || components == NULL)
		return catalog_invalid(error, "components", "bold components map new names to components"), FALSE;
	names = json_object_get_members(added);
	for (name = names; name != NULL; name = name->next) {
		if (json_object_has_member(components, name->data) || member_object(added, name->data) == NULL)
			return catalog_invalid(error, name->data, "a bold component has a new name"), FALSE;
		json_object_set_member(components, name->data, json_node_copy(json_object_get_member(added, name->data)));
	}
	return TRUE;
}

static gboolean is_opening(LsComponents *self, const gchar *opening);

/* The bold block's pictures: what each of its openings does with one. It
 * needs one, shows one when given, or hides it. */
static gboolean
load_pictures(LsComponents *self, JsonObject *root, GError **error){
	JsonObject *bold = member_object(root, "bold");
	JsonObject *pictures = member_object(bold, "pictures");
	const gchar *rules[] = {"needs", "shows", "hides", NULL};
	g_autoptr(GList) names = NULL;
	GList *name;
	if (bold == NULL || !json_object_has_member(bold, "pictures"))
		return TRUE;
	if (pictures == NULL)
		return catalog_invalid(error, "pictures", "maps home openings to needs, shows or hides"), FALSE;
	names = json_object_get_members(pictures);
	for (name = names; name != NULL; name = name->next) {
		const gchar *rule = ls_string(pictures, name->data);
		if (!is_opening(self, name->data) || rule == NULL || !g_strv_contains(rules, rule))
			return catalog_invalid(error, name->data, "pictures map home openings to needs, shows or hides"), FALSE;
	}
	self->pictures = pictures;
	return TRUE;
}

/* Whether @pair (component/variant) names a real variant of a component. */
static gboolean
real_variant(LsComponents *self, const gchar *pair){
	const gchar *slash = pair != NULL ? strchr(pair, '/') : NULL;
	g_autofree gchar *component = slash != NULL ? g_strndup(pair, (gsize)(slash - pair)) : NULL;
	JsonObject *variants = component != NULL
		? member_object(member_object(self->components, component), "variants") : NULL;
	return variants != NULL && json_object_has_member(variants, slash + 1);
}

static gboolean taste_gimmick(LsComponents *self, const gchar *pair);

/* The taste block: openings and variants an unthemed site never shows, and
 * the colour limit. */
static gboolean
load_taste(LsComponents *self, JsonObject *root, GError **error){
	JsonObject *taste = member_object(root, "taste");
	const gchar *lists[] = {"openings", "variants"};
	guint l, i;
	if (!json_object_has_member(root, "taste"))
		return TRUE;
	if (taste == NULL || !ls_object_keys(taste, "|openings|variants|chroma|", NULL) ||
		!JSON_NODE_HOLDS_VALUE(json_object_get_member(taste, "chroma")) ||
		json_object_get_double_member(taste, "chroma") <= 0 || json_object_get_double_member(taste, "chroma") > 1)
		return catalog_invalid(error, "taste", "needs openings, variants and a chroma from 0 to 1"), FALSE;
	for (l = 0; l < G_N_ELEMENTS(lists); l++)
		if (member_array(taste, lists[l]) == NULL)
			return catalog_invalid(error, "taste", "lists openings and variants"), FALSE;
	for (i = 0; i < json_array_get_length(member_array(taste, "openings")); i++)
		if (!is_opening(self, json_array_get_string_element(member_array(taste, "openings"), i)))
			return catalog_invalid(error, "taste", "openings are home openings as component/variant"), FALSE;
	for (i = 0; i < json_array_get_length(member_array(taste, "variants")); i++)
		if (!real_variant(self, json_array_get_string_element(member_array(taste, "variants"), i)))
			return catalog_invalid(error, "taste", "variants are component/variant pairs"), FALSE;
	self->taste = taste;
	return TRUE;
}

/* The default block: the one calm design every unthemed site wears. Its
 * openings are home openings the taste block allows, its scene a scene
 * variant; load_identities() checks its palette, typeface and home. */
static gboolean
load_default(LsComponents *self, JsonObject *root, GError **error){
	JsonObject *block = member_object(root, "default");
	const gchar *openings[] = {"opening", "photo_opening"};
	guint i;
	if (block == NULL || !ls_object_keys(block, "|opening|photo_opening|scene|palette|typeface|home|", NULL))
		return catalog_invalid(error, "default", "needs opening, photo_opening, scene, palette, typeface and home"), FALSE;
	for (i = 0; i < G_N_ELEMENTS(openings); i++)
		if (!is_opening(self, ls_string(block, openings[i])) || taste_gimmick(self, ls_string(block, openings[i])))
			return catalog_invalid(error, "default", "openings are calm home openings as component/variant"), FALSE;
	{
		g_autofree gchar *scene = g_strconcat("scene/", ls_string(block, "scene") != NULL ? ls_string(block, "scene") : "",
			NULL);
		if (!real_variant(self, scene))
			return catalog_invalid(error, "default", "scene is a scene variant"), FALSE;
	}
	if (ls_string(block, "palette") == NULL || ls_string(block, "typeface") == NULL || ls_string(block, "home") == NULL)
		return catalog_invalid(error, "default", "names a palette, a typeface and a home"), FALSE;
	self->fallback = block;
	return TRUE;
}

/* Whether @pair is on the taste block's banned @list (openings, variants). */
static gboolean
taste_banned(LsComponents *self, const gchar *list, const gchar *pair){
	return self->taste != NULL && pair != NULL && json_list_has(member_array(self->taste, list), pair);
}

/* Whether @pair may not open an unthemed site: a banned opening, or a gimmick
 * variant, which the site check refuses on any page. */
static gboolean
taste_gimmick(LsComponents *self, const gchar *pair){
	return taste_banned(self, "openings", pair) || taste_banned(self, "variants", pair);
}

/* The page skeletons, motion levels and overlays of the library block
 * (docs/components.org, "Skeletons, motion and overlays"): each names its
 * source, its owner words and the sectors it fits, and brings its styles;
 * a placement overlay names a library section variant instead. Optional,
 * so archived libraries load as before. */
static gboolean
load_layout_blocks(LsComponents *self, GChecksum *digest, JsonObject *library, GError **error){
	const gchar *blocks[] = {"skeletons", "motion", "overlays"};
	guint b;
	for (b = 0; b < G_N_ELEMENTS(blocks); b++) {
		JsonObject *entries = member_object(library, blocks[b]);
		g_autoptr(GList) names = NULL;
		GList *item;
		if (!json_object_has_member(library, blocks[b]))
			continue;
		if (entries == NULL)
			return catalog_invalid(error, blocks[b], "library layout blocks are maps of named entries");
		names = json_object_get_members(entries);
		for (item = names; item != NULL; item = item->next) {
			JsonObject *entry = member_object(entries, item->data);
			const gchar *variant = ls_string(entry, "variant");
			g_autofree gchar *style = NULL, *text = NULL;
			if (entry == NULL || !ls_object_keys(entry, "|source|words|fits|needs|share|variant|not_headers|", NULL) ||
				ls_string(entry, "source") == NULL || strlen(ls_string(entry, "source")) < 20 ||
				(json_object_has_member(entry, "needs") && member_object(entry, "needs") == NULL) ||
				(json_object_has_member(entry, "words") && member_array(entry, "words") == NULL) ||
				(json_object_has_member(entry, "fits") && member_array(entry, "fits") == NULL) ||
				(json_object_has_member(entry, "not_headers") && member_array(entry, "not_headers") == NULL))
				return catalog_invalid(error, item->data, "library layouts need a source, and words, fits and not_headers as lists");
			if (g_str_equal(blocks[b], "motion") && !g_str_equal(item->data, "none") && !g_str_equal(item->data, "calm") &&
				!g_str_equal(item->data, "lively"))
				return catalog_invalid(error, item->data, "motion levels are none, calm and lively");
			if (variant != NULL) {
				if (!g_str_equal(blocks[b], "overlays") || member_object(member_object(library, "variants"), variant) == NULL)
					return catalog_invalid(error, item->data, "a placement overlay names a library section variant");
				continue;
			}
			if (g_str_equal(blocks[b], "motion"))
				continue;
			style = g_strdup_printf("%s--%s.css", g_str_equal(blocks[b], "skeletons") ? "skeleton" : "overlay",
				(const gchar *)item->data);
			text = load_resource(self, digest, style, TRUE, error);
			if (text == NULL)
				return FALSE;
		}
		/* Motion brings one stylesheet and one script for every level. */
		if (g_str_equal(blocks[b], "motion")) {
			const gchar *files[] = {"motion.css", "motion.js"};
			guint f;
			for (f = 0; f < G_N_ELEMENTS(files); f++) {
				g_autofree gchar *text = load_resource(self, digest, files[f], TRUE, error);
				if (text == NULL)
					return FALSE;
			}
		}
	}
	/* The scripts of the headers and overlays that move or open. */
	if (json_object_has_member(library, "overlays")) {
		const gchar *files[] = {"header.js", "overlay.js", "quote-form.html"};
		guint f;
		for (f = 0; f < G_N_ELEMENTS(files); f++) {
			g_autofree gchar *text = load_resource(self, digest, files[f], TRUE, error);
			if (text == NULL)
				return FALSE;
		}
	}
	return TRUE;
}

/* The library block (docs/components.org, "Presets and the picker"): every
 * variant names its source and brings its own markup and styles, sectors
 * list real section types and one primary action, named colours are
 * #rrggbb. Optional, so archived libraries load as before. */
static gboolean
load_library(LsComponents *self, GChecksum *digest, JsonObject *root, GError **error){
	JsonObject *library = member_object(root, "library");
	JsonObject *variants, *sectors, *colors;
	g_autoptr(GList) pairs = NULL, names = NULL;
	GList *item;
	if (!json_object_has_member(root, "library"))
		return TRUE;
	if (library == NULL || !ls_object_keys(library,
		"|weights|fallback_opening|sectors|variants|neutral|colors|skeletons|motion|overlays|", NULL) ||
		(variants = member_object(library, "variants")) == NULL || (sectors = member_object(library, "sectors")) == NULL ||
		(colors = member_object(library, "colors")) == NULL)
		return catalog_invalid(error, "library", "needs weights, fallback_opening, sectors, variants, neutral and colors");
	if (!is_opening(self, ls_string(library, "fallback_opening")))
		return catalog_invalid(error, "library", "fallback_opening is a home opening as component/variant");
	pairs = json_object_get_members(variants);
	for (item = pairs; item != NULL; item = item->next) {
		const gchar *pair = item->data;
		const gchar *slash = strchr(pair, '/');
		JsonObject *entry = member_object(variants, pair);
		g_autofree gchar *component = slash != NULL ? g_strndup(pair, (gsize)(slash - pair)) : NULL;
		if (component == NULL || entry == NULL || !ls_object_keys(entry, "|source|family|needs|tone|words|fits|", NULL) ||
			ls_string(entry, "source") == NULL || strlen(ls_string(entry, "source")) < 12 ||
			(json_object_has_member(entry, "needs") && member_object(entry, "needs") == NULL))
			return catalog_invalid(error, pair, "library variants are component/variant with a source and needs");
		if (g_str_equal(component, "header") || g_str_equal(component, "footer"))
			continue;
		{
			g_autofree gchar *markup = g_strdup_printf("%s--%s.html", component, slash + 1);
			g_autofree gchar *style = g_strdup_printf("%s--%s.css", component, slash + 1);
			g_autofree gchar *html = NULL, *css = NULL;
			if (!json_object_has_member(member_object(member_object(self->components, component), "variants"), slash + 1))
				return catalog_invalid(error, pair, "library variants extend catalog components");
			html = load_resource(self, digest, markup, TRUE, error);
			css = html != NULL ? load_resource(self, digest, style, TRUE, error) : NULL;
			if (css == NULL)
				return FALSE;
			g_ptr_array_add(self->extras, g_strdup_printf("%s--%s", component, slash + 1));
		}
	}
	names = json_object_get_members(sectors);
	for (item = names; item != NULL; item = item->next) {
		JsonObject *sector = member_object(sectors, item->data);
		JsonArray *home = member_array(sector, "home");
		const gchar *action = ls_string(sector, "action");
		guint i;
		JsonArray *orders = member_array(sector, "orders");
		guint o;
		if (sector == NULL || !ls_object_keys(sector, "|words|action|home|orders|", NULL) || home == NULL ||
			action == NULL || (!g_str_equal(action, "call") && !g_str_equal(action, "book") && !g_str_equal(action, "quote")) ||
			(json_object_has_member(sector, "orders") && orders == NULL))
			return catalog_invalid(error, item->data, "library sectors need words, an action (call, book or quote), home "
				"and optional orders");
		for (i = 0; i < json_array_get_length(home); i++)
			if (member_object(self->components, json_array_get_string_element(home, i)) == NULL)
				return catalog_invalid(error, item->data, "library homes list catalog components");
		/* Another order holds the same section types, hero first. */
		for (o = 0; orders != NULL && o < json_array_get_length(orders); o++) {
			JsonNode *node = json_array_get_element(orders, o);
			JsonArray *order = JSON_NODE_HOLDS_ARRAY(node) ? json_node_get_array(node) : NULL;
			guint k, m;
			if (order == NULL || json_array_get_length(order) == 0 ||
				g_strcmp0(json_array_get_string_element(order, 0), "hero") != 0)
				return catalog_invalid(error, item->data, "library orders are lists of home sections, hero first");
			for (k = 0; k < json_array_get_length(order); k++) {
				gboolean known = FALSE;
				for (m = 0; m < json_array_get_length(home); m++)
					known = known || g_strcmp0(json_array_get_string_element(order, k),
						json_array_get_string_element(home, m)) == 0;
				if (!known)
					return catalog_invalid(error, item->data, "library orders hold only the sector's home sections");
			}
		}
	}
	if (member_object(sectors, "any") == NULL)
		return catalog_invalid(error, "library", "sectors need an any sector");
	{
		g_autoptr(GList) words = json_object_get_members(colors);
		for (item = words; item != NULL; item = item->next) {
			gdouble rgb[3];
			if (!parse_color(ls_string(colors, item->data), rgb))
				return catalog_invalid(error, item->data, "library colors are #rrggbb");
		}
	}
	{
		g_autofree gchar *kit = load_resource(self, digest, "design.css", TRUE, error);
		if (kit == NULL)
			return FALSE;
	}
	if (!load_layout_blocks(self, digest, library, error))
		return FALSE;
	self->library = library;
	return TRUE;
}

LsComponents *
ls_components_new_from_snapshot(const gchar *snapshot, GError **error){
	g_autoptr(LsComponents) self = g_object_new(LS_TYPE_COMPONENTS, NULL);
	g_autoptr(GChecksum) digest = g_checksum_new(G_CHECKSUM_SHA256);
	g_autofree gchar *catalog = NULL;
	const gchar *shared[] = {"layout.html", "section.html", "base.css", "look.css", "nav.js", "plan.schema.json"};
	g_autoptr(GList) names = NULL;
	JsonObject *root;
	GList *item;
	guint i;
	if (snapshot != NULL && !read_snapshot(self, snapshot, error))
		return NULL;
	catalog = load_resource(self, digest, "catalog.yaml", TRUE, error);
	if (catalog == NULL)
		return NULL;
	self->catalog = ls_yaml_parse(catalog, error);
	if (self->catalog == NULL)
		return NULL;
	root = JSON_NODE_HOLDS_OBJECT(self->catalog) ? json_node_get_object(self->catalog) : NULL;
	if (root == NULL || !ls_object_keys(root, "|version|looks|kinds|components|extra_variants|extra_looks|wider|bold|bar|taste|default|library|", error) ||
		(self->components = member_object(root, "components")) == NULL ||
		(self->kinds = member_object(root, "kinds")) == NULL ||
		(self->looks = member_object(root, "looks")) == NULL) {
		if (error != NULL && *error == NULL)
			catalog_invalid(error, "catalog.yaml", "needs version, looks, kinds and components");
		return NULL;
	}
	if (!check_look_definitions(self->looks, error))
		return NULL;
	/* Extra look values join their axes, each with its own styles; the
	 * blocks are optional so archived libraries load as before. */
	if (!join_looks(self, digest, root, "extra_looks", member_object(root, "extra_looks"), error) ||
		!join_looks(self, digest, member_object(root, "wider"), "looks",
			member_object(member_object(root, "wider"), "looks"), error) ||
		!check_block_keys(root, "wider", "|looks|openings|", error) ||
		!join_looks(self, digest, member_object(root, "bold"), "looks",
			member_object(member_object(root, "bold"), "looks"), error) ||
		!check_block_keys(root, "bold", "|looks|openings|components|pictures|", error) || !embolden(root, error))
		return NULL;
	/* Each later collection owns an append-only catalog block. */
	{
		JsonObject *bar = member_object(root, "bar");
		JsonObject *added = member_object(bar, "components");
		g_autoptr(GList) extra = added != NULL ? json_object_get_members(added) : NULL;
		GList *entry;
		for (entry = extra; entry != NULL; entry = entry->next) {
			if (json_object_has_member(self->components, entry->data))
				return catalog_invalid(error, entry->data, "bar components must have new names"), NULL;
			json_object_set_member(self->components, entry->data, json_node_copy(json_object_get_member(added, entry->data)));
		}
		if (bar != NULL) {
			g_autofree gchar *style = load_resource(self, digest, "visual.css", TRUE, error);
			if (style == NULL)
				return NULL;
		}
	}
	for (i = 0; i < G_N_ELEMENTS(shared); i++) {
		g_autofree gchar *text = load_resource(self, digest, shared[i], TRUE, error);
		if (text == NULL)
			return NULL;
	}
	/* Older sealed libraries keep their original photo treatment, form
	 * fields and single colour scheme. */
	{
		const gchar *later[] = {"supplied-photos.css", "controls.css", "scheme.js", "fields.html"};
		guint k;
		for (k = 0; k < G_N_ELEMENTS(later); k++) {
			g_autofree gchar *text = load_resource(self, digest, later[k], snapshot == NULL, error);
			if (text == NULL && snapshot == NULL)
				return NULL;
		}
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
	/* Extra variants join their components, each with its own styles;
	 * the block is optional so archived libraries load as before. */
	self->extras = g_ptr_array_new_with_free_func(g_free);
	if (json_object_has_member(root, "extra_variants")) {
		JsonObject *extra = member_object(root, "extra_variants");
		g_autoptr(GList) owners = extra != NULL ? json_object_get_members(extra) : NULL;
		GList *owner;
		if (extra == NULL)
			return catalog_invalid(error, "extra_variants", "must map components to new variants"), NULL;
		for (owner = owners; owner != NULL; owner = owner->next) {
			JsonObject *variants = member_object(member_object(self->components, owner->data), "variants");
			JsonObject *added = member_object(extra, owner->data);
			g_autoptr(GList) values = added != NULL ? json_object_get_members(added) : NULL;
			GList *value;
			if (variants == NULL || added == NULL)
				return catalog_invalid(error, owner->data, "extra variants extend catalog components"), NULL;
			for (value = values; value != NULL; value = value->next) {
				g_autofree gchar *file = g_strdup_printf("%s--%s.css", (const gchar *)owner->data, (const gchar *)value->data);
				g_autofree gchar *style = NULL;
				if (json_object_has_member(variants, value->data) || ls_string(added, value->data) == NULL)
					return catalog_invalid(error, owner->data, "an extra variant is new and has a description"), NULL;
				style = load_resource(self, digest, file, TRUE, error);
				if (style == NULL)
					return NULL;
				json_object_set_string_member(variants, value->data, ls_string(added, value->data));
				g_ptr_array_add(self->extras, g_strdup_printf("%s--%s", (const gchar *)owner->data, (const gchar *)value->data));
			}
		}
	}
	if (!join_openings(self, digest, root, "wider", error) || !join_openings(self, digest, root, "bold", error) ||
		!load_pictures(self, root, error) || !load_library(self, digest, root, error))
		return NULL;
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
	if (!load_taste(self, root, error) || !load_default(self, root, error))
		return NULL;
	self->plan_schema = build_plan_schema(self, error);
	if (self->plan_schema == NULL || !load_themes(self, digest, error) ||
		!load_identities(self, digest, error) || !load_networks(self, digest, error))
		return NULL;
	self->digest = g_strdup(g_checksum_get_string(digest));
	return (LsComponents *)g_steal_pointer(&self);
}

JsonNode *
ls_components_get_catalog(LsComponents *self){
	g_return_val_if_fail(LS_IS_COMPONENTS(self), NULL);
	return self->catalog;
}

const gchar *
ls_components_get_digest(LsComponents *self){
	g_return_val_if_fail(LS_IS_COMPONENTS(self), NULL);
	return self->digest;
}


/* ---- Structure ------------------------------------------------------------ */

/* What older plans carry in structure from when the builder dealt the page
 * structure from a seed. Existing sites keep them; the builder ignores them. */
static const gchar *const retired_structure[] = {"width", "gutter", "pace", "pad", "grid", "scale", "narrow",
	"bleed", "band", "offset", "loose", "tight", "variants", "composition", "kit", "direction", "forms", "extras",
	"angle", "subject", NULL};

static gboolean
in_list(const gchar *const *list, const gchar *name){
	guint i;
	for (i = 0; list[i] != NULL; i++)
		if (g_strcmp0(list[i], name) == 0)
			return TRUE;
	return FALSE;
}

static gdouble taste_lightness(const gchar *hex);

/* Whether @opening is a home opening as component/variant. */
static gboolean
is_opening(LsComponents *self, const gchar *opening){
	JsonArray *openings = member_array(member_object(self->kinds, "home"), "openings");
	const gchar *slash = opening != NULL ? strchr(opening, '/') : NULL;
	g_autofree gchar *component = slash != NULL ? g_strndup(opening, (gsize)(slash - opening)) : NULL;
	guint i;
	if (component == NULL || g_str_equal(component, "custom"))
		return FALSE;
	for (i = 0; openings != NULL && i < json_array_get_length(openings); i++)
		if (g_strcmp0(json_array_get_string_element(openings, i), component) == 0)
			return json_object_has_member(member_object(member_object(self->components, component), "variants"),
				slash + 1);
	return FALSE;
}

/* The builder's structure: the home opening, plus what older plans carry. */
static void
check_structure(Problems *problems, LsComponents *self, JsonObject *plan){
	JsonObject *structure = member_object(plan, "structure");
	g_autoptr(GList) keys = NULL;
	GList *key;
	if (!json_object_has_member(plan, "structure"))
		return;
	if (structure == NULL) {
		problem(problems, "structure must be an object");
		return;
	}
	keys = json_object_get_members(structure);
	for (key = keys; key != NULL; key = key->next) {
		if (g_str_equal(key->data, "opening")) {
			if (!is_opening(self, ls_string(structure, "opening")))
				problem(problems, "structure.opening must be a home opening as component/variant, such as hero/split");
		} else if (!in_list(retired_structure, key->data))
			problem(problems, "structure.%s is not part of the structure", (const gchar *)key->data);
	}
}

/* ---- The default design ---------------------------------------------------- */

/* A private deep copy of @node. */
static JsonNode *
node_copy(JsonNode *node){
	g_autofree gchar *text = ls_json_encode(node);
	return ls_json_parse(text, NULL);
}

/* A neon colour: chroma over this and HSL lightness over that. */
#define TASTE_NEON_CHROMA 0.35
#define TASTE_NEON_LIGHTNESS 0.45

/* Keeps the page and surface colours calm: each is moved toward its own
 * grey until under the taste block's chroma limit. */
static void
taste_calm_tokens(LsComponents *self, JsonObject *tokens){
	const gchar *keys[] = {"paper", "surface"};
	guint i;
	if (self->taste == NULL || tokens == NULL)
		return;
	for (i = 0; i < G_N_ELEMENTS(keys); i++) {
		const gchar *value = ls_string(tokens, keys[i]);
		/* Deep colours stay calm however saturated; only a light, coloured
		 * ground is loud. */
		if (value != NULL && taste_lightness(value) >= 0.45) {
			g_autofree gchar *calm = ls_taste_calm(value, json_object_get_double_member(self->taste, "chroma"));
			json_object_set_string_member(tokens, keys[i], calm);
		}
	}
	/* The accent fills bands too, so a vivid, light one (neon) is toned
	 * down to a strong colour; its ink is then kept readable on it. */
	{
		const gchar *accent = ls_string(tokens, "accent");
		gdouble rgb[3] = {0, 0, 0};
		if (accent != NULL && parse_color(accent, rgb) && ls_taste_chroma(accent) > TASTE_NEON_CHROMA &&
			(MAX(MAX(rgb[0], rgb[1]), rgb[2]) + MIN(MIN(rgb[0], rgb[1]), rgb[2])) / 2 > TASTE_NEON_LIGHTNESS) {
			g_autofree gchar *calm = ls_taste_calm(accent, TASTE_NEON_CHROMA);
			/* Its ink: the current one when it still reads, else the
			 * page's ink or paper, else plain black or white, whichever
			 * reads best. */
			const gchar *inks[] = {ls_string(tokens, "accent_ink"), ls_string(tokens, "ink"), ls_string(tokens, "paper"),
				"#111111", "#ffffff"};
			const gchar *best = NULL;
			g_autofree gchar *ink = NULL;
			guint n;
			for (n = 0; n < G_N_ELEMENTS(inks) && (best == NULL || contrast(best, calm) < 4.6); n++)
				if (inks[n] != NULL && (best == NULL || contrast(inks[n], calm) > contrast(best, calm)))
					best = inks[n];
			ink = g_strdup(best != NULL ? best : "#ffffff");
			/* A mid-tone accent that neither reads on is shaded away from
			 * its ink, a tenth at a time. */
			for (n = 0; n < 10 && contrast(ink, calm) < 4.6; n++) {
				gdouble c[3];
				gchar *moved;
				parse_color(ink, c);
				moved = ls_taste_mix(calm, luminance(c) > 0.18 ? "#000000" : "#ffffff", 0.1);
				g_free(calm);
				calm = moved;
			}
			json_object_set_string_member(tokens, "accent", calm);
			json_object_set_string_member(tokens, "accent_ink", ink);
		}
	}
	/* Calmer grounds shift the text's contrast: ink and muted text move
	 * away from the page, a tenth at a time, until both grounds pass AA
	 * with a little room. */
	{
		const gchar *text[] = {"ink", "muted"};
		const gchar *paper = ls_string(tokens, "paper"), *surface = ls_string(tokens, "surface");
		gdouble rgb[3] = {0, 0, 0};
		const gchar *away;
		guint step;
		if (paper == NULL || !parse_color(paper, rgb))
			return;
		away = luminance(rgb) > 0.18 ? "#000000" : "#ffffff";
		for (i = 0; i < G_N_ELEMENTS(text); i++) {
			g_autofree gchar *colour = g_strdup(ls_string(tokens, text[i]));
			if (colour == NULL)
				continue;
			for (step = 0; step < 10 && (contrast(colour, paper) < 4.6 ||
				(surface != NULL && contrast(colour, surface) < 4.6)); step++) {
				gchar *moved = ls_taste_mix(colour, away, 0.2);
				g_free(colour);
				colour = moved;
			}
			json_object_set_string_member(tokens, text[i], colour);
		}
	}
}

/* Only the brand mark is drawn: picture slots take supplied photos, so the
 * plan keeps no other artwork. */
static void
keep_only_mark(JsonObject *plan){
	JsonArray *art = member_array(plan, "art"), *kept;
	JsonObject *brand = member_object(plan, "brand");
	const gchar *mark = brand != NULL ? ls_string(brand, "mark") : NULL;
	guint i;
	if (art == NULL)
		return;
	kept = json_array_new();
	for (i = 0; i < json_array_get_length(art); i++) {
		JsonNode *entry = json_array_get_element(art, i);
		if (mark != NULL && JSON_NODE_HOLDS_OBJECT(entry) &&
			g_strcmp0(ls_string(json_node_get_object(entry), "path"), mark) == 0)
			json_array_add_element(kept, json_node_copy(entry));
	}
	json_object_set_array_member(plan, "art", kept);
}

gboolean
ls_components_apply_default(LsComponents *self, JsonNode *plan_node, gboolean themed){
	JsonObject *plan, *look, *tokens, *structure, *palette, *type;
	g_autoptr(JsonObject) identity = NULL;
	g_autoptr(GList) axes = NULL;
	GList *axis;
	gboolean photos;
	g_return_val_if_fail(LS_IS_COMPONENTS(self), FALSE);
	if (plan_node == NULL || !JSON_NODE_HOLDS_OBJECT(plan_node))
		return FALSE;
	plan = json_node_get_object(plan_node);
	photos = member_array(plan, "photos") != NULL && json_array_get_length(member_array(plan, "photos")) > 0;
	/* One home opening; a site with supplied photos opens on one. */
	structure = json_object_new();
	json_object_set_string_member(structure, "opening",
		ls_string(self->fallback, photos ? "photo_opening" : "opening"));
	json_object_set_object_member(plan, "structure", structure);
	keep_only_mark(plan);
	look = member_object(plan, "look");
	if (look == NULL) {
		look = json_object_new();
		json_object_set_object_member(plan, "look", look);
	}
	axes = json_object_get_members(self->looks);
	/* A theme is a whole design: its look, colours and type stay, and only
	 * the builder axes it leaves open take their defaults. */
	if (themed) {
		for (axis = axes; axis != NULL; axis = axis->next) {
			JsonObject *definition = member_object(self->looks, axis->data);
			if (json_object_has_member(definition, "builder") && json_object_get_boolean_member(definition, "builder") &&
				ls_string(look, axis->data) == NULL)
				json_object_set_string_member(look, axis->data, look_default(self, axis->data));
		}
		return TRUE;
	}
	for (axis = axes; axis != NULL; axis = axis->next)
		json_object_set_string_member(look, axis->data, look_default(self, axis->data));
	identity = ls_components_get_default_identity(self);
	tokens = member_object(plan, "tokens");
	if (tokens == NULL) {
		tokens = json_object_new();
		json_object_set_object_member(plan, "tokens", tokens);
	}
	palette = member_object(member_object(identity, "palette"), "tokens");
	type = member_object(identity, "type");
	if (palette != NULL) {
		g_autoptr(GList) keys = json_object_get_members(palette);
		GList *key;
		for (key = keys; key != NULL; key = key->next)
			json_object_set_string_member(tokens, key->data, ls_string(palette, key->data));
	}
	/* Type needs its web fonts. A plan that asked for none (an order that
	 * forbids remote fonts) keeps its own installed stacks. */
	if (type != NULL && member_array(plan, "fonts") != NULL && json_array_get_length(member_array(plan, "fonts")) > 0) {
		JsonObject *type_tokens = member_object(type, "tokens");
		g_autoptr(GList) keys = type_tokens != NULL ? json_object_get_members(type_tokens) : NULL;
		GList *key;
		for (key = keys; key != NULL; key = key->next)
			json_object_set_member(tokens, key->data, json_node_copy(json_object_get_member(type_tokens, key->data)));
		if (json_object_has_member(type, "fonts"))
			json_object_set_member(plan, "fonts", node_copy(json_object_get_member(type, "fonts")));
	}
	/* A visual sequence opens on the default scene. */
	if (member_object(plan, "visual") != NULL)
		json_object_set_string_member(member_object(plan, "visual"), "layout", ls_string(self->fallback, "scene"));
	/* The visual sequence sets its world's colours first, so the calm
	 * rules cover them too. */
	ls_visual_apply_plan(plan);
	taste_calm_tokens(self, tokens);
	return TRUE;
}

/* What @component/@variant does with a picture: "needs" one, "shows" one
 * when given or "hides" it. The bold block names its own openings; the
 * stock ones are fixed here. */
static const gchar *
opening_picture(LsComponents *self, const gchar *component, const gchar *variant){
	g_autofree gchar *pair = g_strdup_printf("%s/%s", component, variant);
	if (self->pictures != NULL && ls_string(self->pictures, pair) != NULL)
		return ls_string(self->pictures, pair);
	{
		JsonObject *needs = member_object(member_object(member_object(self->library, "variants"), pair), "needs");
		if (needs != NULL && json_object_has_member(needs, "photos"))
			return "needs";
	}
	if (g_strcmp0(component, "cover") == 0 ||
		(g_strcmp0(component, "hero") == 0 && (g_strcmp0(variant, "split") == 0 || g_strcmp0(variant, "banner") == 0)))
		return "needs";
	if (g_strcmp0(component, "hero") == 0 && g_strcmp0(variant, "minimal") != 0)
		return "shows";
	return "hides";
}

/* Openings that cannot draw without a picture. */
static gboolean
opening_needs_art(LsComponents *self, const gchar *component, const gchar *variant){
	return g_str_equal(opening_picture(self, component, variant), "needs");
}

/* Openings that show their picture; the others hide it or have none. */
static gboolean
opening_shows_art(LsComponents *self, const gchar *component, const gchar *variant){
	return !g_str_equal(opening_picture(self, component, variant), "hides");
}

static JsonObject *plan_photo(JsonObject *plan, const gchar *src);

/* The supplied photo an opening leads with, as an image slot: the first
 * landscape one, else the first; NULL without photos. */
static JsonNode *
lead_photo(JsonObject *plan){
	JsonArray *photos = member_array(plan, "photos");
	JsonObject *pick = NULL, *image;
	JsonNode *node;
	guint i;
	for (i = 0; photos != NULL && i < json_array_get_length(photos); i++) {
		JsonObject *photo = JSON_NODE_HOLDS_OBJECT(json_array_get_element(photos, i))
			? json_array_get_object_element(photos, i) : NULL;
		if (photo == NULL || ls_string(photo, "path") == NULL)
			continue;
		if (pick == NULL)
			pick = photo;
		if (json_object_get_int_member_with_default(photo, "width", 0) >= json_object_get_int_member_with_default(photo, "height", 1)) {
			pick = photo;
			break;
		}
	}
	if (pick == NULL)
		return NULL;
	node = json_node_new(JSON_NODE_OBJECT);
	image = json_object_new();
	json_object_set_string_member(image, "src", ls_string(pick, "path"));
	json_object_set_string_member(image, "alt", ls_string(pick, "alt") != NULL ? ls_string(pick, "alt") : "");
	json_node_take_object(node, image);
	return node;
}

/* A supplied photo leads any opening that shows a picture, ahead of a
 * drawing: @content's art becomes the lead photo unless it already is a
 * photo. Returns whether @content changed. */
static gboolean
photo_first(LsComponents *self, JsonObject *plan, JsonObject *content, const gchar *component, const gchar *variant){
	JsonObject *slots = member_object(member_object(self->components, component), "slots");
	JsonObject *art = member_object(content, "art");
	JsonNode *photo;
	if (content == NULL || slots == NULL || !json_object_has_member(slots, "art") ||
		!opening_shows_art(self, component, variant) ||
		(art != NULL && plan_photo(plan, ls_string(art, "src")) != NULL))
		return FALSE;
	photo = lead_photo(plan);
	if (photo == NULL)
		return FALSE;
	json_object_set_member(content, "art", photo);
	return TRUE;
}

/* @source's words in @component/@variant, or NULL when they do not fit its
 * shape. Only slots the target declares are carried. */
static JsonNode *
convert_opening(LsComponents *self, JsonObject *plan, JsonObject *source, const gchar *component,
	const gchar *variant){
	JsonObject *from = member_object(source, "content");
	JsonObject *slots = member_object(member_object(self->components, component), "slots");
	JsonObject *content = json_object_new(), *section = json_object_new();
	JsonNode *out = json_node_new(JSON_NODE_OBJECT);
	const gchar *keep[] = {"kicker", "heading", "lede", "actions", "art"};
	guint i;
	json_node_take_object(out, section);
	json_object_set_object_member(section, "content", content);
	if (from == NULL || slots == NULL) {
		json_node_unref(out);
		return NULL;
	}
	json_object_set_string_member(section, "component", component);
	json_object_set_string_member(section, "variant", variant);
	if (ls_string(source, "id") != NULL)
		json_object_set_string_member(section, "id", ls_string(source, "id"));
	for (i = 0; i < G_N_ELEMENTS(keep); i++)
		if (json_object_has_member(slots, keep[i]) && json_object_has_member(from, keep[i]))
			json_object_set_member(content, keep[i], node_copy(json_object_get_member(from, keep[i])));
	/* A picture the writer put in the opening (often a supplied photo) is
	 * never dropped for an opening that would hide it. */
	if (json_object_has_member(from, "art") && !opening_shows_art(self, component, variant)) {
		json_node_unref(out);
		return NULL;
	}
	photo_first(self, plan, content, component, variant);
	if (opening_needs_art(self, component, variant) && !json_object_has_member(content, "art")) {
		json_node_unref(out);
		return NULL;
	}
	return out;
}

gboolean
ls_components_apply_opening(LsComponents *self, JsonNode *plan_node, const gchar *path, JsonNode *page){
	JsonObject *plan, *first;
	JsonArray *sections;
	g_autoptr(GPtrArray) choices = g_ptr_array_new_with_free_func(g_free);
	const gchar *defaults[] = {"photo_opening", "opening"};
	const gchar *opening, *component;
	guint i;
	g_return_val_if_fail(LS_IS_COMPONENTS(self), FALSE);
	if (plan_node == NULL || !JSON_NODE_HOLDS_OBJECT(plan_node) || page == NULL || !JSON_NODE_HOLDS_OBJECT(page))
		return FALSE;
	plan = json_node_get_object(plan_node);
	if (member_object(plan, "structure") == NULL)
		return FALSE;
	opening = ls_string(member_object(plan, "structure"), "opening");
	sections = member_array(json_node_get_object(page), "sections");
	if (!is_opening(self, opening) || sections == NULL || json_array_get_length(sections) == 0 ||
		!JSON_NODE_HOLDS_OBJECT(json_array_get_element(sections, 0)))
		return FALSE;
	first = json_array_get_object_element(sections, 0);
	component = ls_string(first, "component");
	/* Only the stock openings are rewritten; a custom moment stays. */
	if (component == NULL || (!g_str_equal(component, "hero") && !g_str_equal(component, "cover")))
		return FALSE;
	{
		g_autofree gchar *current = g_strdup_printf("%s/%s", component, ls_string(first, "variant") != NULL
			? ls_string(first, "variant") : "");
		/* The writer already opens as planned; a supplied photo still leads. */
		if (g_str_equal(current, opening))
			return photo_first(self, plan, member_object(first, "content"), component,
				ls_string(first, "variant") != NULL ? ls_string(first, "variant") : "");
	}
	/* The planned opening first, then the catalog's default openings. */
	g_ptr_array_add(choices, g_strdup(opening));
	for (i = 0; i < G_N_ELEMENTS(defaults); i++) {
		const gchar *pair = ls_string(self->fallback, defaults[i]);
		if (pair != NULL && g_strcmp0(pair, opening) != 0)
			g_ptr_array_add(choices, g_strdup(pair));
	}
	for (i = 0; i < choices->len; i++) {
		const gchar *choice = g_ptr_array_index(choices, i);
		g_autofree gchar *name = g_strndup(choice, (gsize)(strchr(choice, '/') - choice));
		g_autoptr(JsonNode) converted = convert_opening(self, plan, first, name, strchr(choice, '/') + 1);
		g_autoptr(JsonNode) candidate = NULL;
		JsonArray *candidate_sections;
		if (converted == NULL)
			continue;
		candidate = node_copy(page);
		candidate_sections = member_array(json_node_get_object(candidate), "sections");
		json_array_remove_element(candidate_sections, 0);
		/* json-glib has no insert: rebuild the list with the opening first. */
		{
			JsonArray *rebuilt = json_array_new();
			guint j;
			json_array_add_element(rebuilt, json_node_copy(converted));
			for (j = 0; j < json_array_get_length(candidate_sections); j++)
				json_array_add_element(rebuilt, json_node_copy(json_array_get_element(candidate_sections, j)));
			json_object_set_array_member(json_node_get_object(candidate), "sections", rebuilt);
		}
		if (!ls_components_check_page(self, plan_node, path, candidate, NULL))
			continue;
		json_object_set_member(json_node_get_object(page), "sections",
			node_copy(json_object_get_member(json_node_get_object(candidate), "sections")));
		return TRUE;
	}
	return FALSE;
}


static const gchar *plan_ui(JsonObject *plan, const gchar *key);

/* The writer's components that may stand in for a designed section type. */
static gboolean
stands_for(const gchar *type, const gchar *component){
	if (g_strcmp0(type, component) == 0)
		return TRUE;
	if (g_strcmp0(type, "hero") == 0)
		return g_strcmp0(component, "cover") == 0;
	if (g_strcmp0(type, "reviews") == 0)
		return g_strcmp0(component, "voices") == 0;
	if (g_strcmp0(type, "contact") == 0)
		return g_strcmp0(component, "contact-card") == 0 || g_strcmp0(component, "call") == 0;
	return FALSE;
}

/* The supplied photos the page's opening does not show, in order. */
static GPtrArray *
free_photos(JsonObject *plan, JsonArray *sections){
	GPtrArray *out = g_ptr_array_new();
	JsonArray *photos = member_array(plan, "photos");
	const gchar *opening = NULL;
	guint i;
	if (sections != NULL && json_array_get_length(sections) > 0 && JSON_NODE_HOLDS_OBJECT(json_array_get_element(sections, 0))) {
		JsonObject *art = member_object(member_object(json_array_get_object_element(sections, 0), "content"), "art");
		opening = art != NULL ? ls_string(art, "src") : NULL;
	}
	for (i = 0; photos != NULL && i < json_array_get_length(photos); i++) {
		JsonObject *photo = json_array_get_object_element(photos, i);
		if (g_strcmp0(ls_string(photo, "path"), opening) != 0)
			g_ptr_array_add(out, photo);
	}
	return out;
}

/* Whether @node is a link to an anchor that @ids does not hold. */
static gboolean
dangling_link(JsonNode *node, GHashTable *ids){
	JsonObject *link = node != NULL && JSON_NODE_HOLDS_OBJECT(node) ? json_node_get_object(node) : NULL;
	const gchar *href = link != NULL && json_object_has_member(link, "href") ? ls_string(link, "href") : NULL;
	return href != NULL && href[0] == '#' && !g_hash_table_contains(ids, href + 1);
}

/* Drops every link in @node to a section the design left out. */
static void
drop_dangling(JsonNode *node, GHashTable *ids){
	if (node == NULL)
		return;
	if (JSON_NODE_HOLDS_ARRAY(node)) {
		JsonArray *array = json_node_get_array(node);
		guint i = json_array_get_length(array);
		while (i-- > 0) {
			if (dangling_link(json_array_get_element(array, i), ids))
				json_array_remove_element(array, i);
			else
				drop_dangling(json_array_get_element(array, i), ids);
		}
	} else if (JSON_NODE_HOLDS_OBJECT(node)) {
		JsonObject *object = json_node_get_object(node);
		g_autoptr(GList) keys = json_object_get_members(object);
		GList *key;
		for (key = keys; key != NULL; key = key->next) {
			JsonNode *value = json_object_get_member(object, key->data);
			if (dangling_link(value, ids))
				json_object_remove_member(object, key->data);
			else
				drop_dangling(value, ids);
		}
	}
}

/* Home section types the builder fills from the order's facts and photos. */
static gboolean
builder_filled(const gchar *type){
	return g_strcmp0(type, "facts") == 0 || g_strcmp0(type, "steps") == 0 || g_strcmp0(type, "areas") == 0 ||
		g_strcmp0(type, "gallery") == 0;
}

static void
add_fact(JsonArray *items, const gchar *label, const gchar *value){
	JsonObject *item = json_object_new();
	json_object_set_string_member(item, "label", label);
	json_object_set_string_member(item, "value", value);
	json_array_add_object_element(items, item);
}

/* The supplied photo after @used that is the owner's own, or NULL. */
static JsonObject *
own_photo(JsonObject *plan, GHashTable *used){
	JsonArray *photos = member_array(plan, "photos");
	guint i;
	for (i = 0; photos != NULL && i < json_array_get_length(photos); i++) {
		JsonObject *photo = json_array_get_object_element(photos, i);
		JsonNode *flag = json_object_get_member(photo, "stock");
		if ((flag != NULL && JSON_NODE_HOLDS_VALUE(flag) && json_node_get_boolean(flag)) ||
			g_hash_table_contains(used, ls_string(photo, "path")))
			continue;
		g_hash_table_add(used, (gpointer)ls_string(photo, "path"));
		return photo;
	}
	return NULL;
}

static JsonObject *photo_art(JsonObject *photo);

/* The next of @photos from *@at that no section shows yet, marked used. */
static JsonObject *
next_free(GPtrArray *photos, guint *at, GHashTable *used){
	while (*at < photos->len) {
		JsonObject *photo = g_ptr_array_index(photos, (*at)++);
		if (!g_hash_table_contains(used, ls_string(photo, "path"))) {
			g_hash_table_add(used, (gpointer)ls_string(photo, "path"));
			return photo;
		}
	}
	return NULL;
}

static JsonObject *
photo_art(JsonObject *photo){
	JsonObject *art = json_object_new();
	json_object_set_string_member(art, "src", ls_string(photo, "path"));
	json_object_set_string_member(art, "alt", ls_string(photo, "alt") != NULL ? ls_string(photo, "alt") : "");
	return art;
}

static gchar *replace_all(const gchar *text, const gchar *needle, const gchar *value);

/* One supplied place for the area section: its name, address, hours and
 * the call. */
static JsonObject *
place_view(const gchar *name, const gchar *address, const gchar *hours, const gchar *phone){
	JsonObject *place = json_object_new();
	json_object_set_string_member(place, "name", name != NULL ? name : "");
	if (address != NULL)
		json_object_set_string_member(place, "address", address);
	if (hours != NULL)
		json_object_set_string_member(place, "hours", hours);
	{
		g_autofree gchar *tel = phone != NULL ? ls_phone_tel(phone) : NULL;
		if (tel != NULL) {
			JsonObject *link = json_object_new();
			json_object_set_string_member(place, "phone", phone);
			json_object_set_string_member(link, "href", tel);
			json_object_set_string_member(link, "label", phone);
			json_object_set_object_member(place, "link", link);
		}
	}
	return place;
}

/* The trust strip's facts, word for word: the starting year, the number
 * of towns served, the first sentence of the hours and the town of the
 * address. */
static JsonArray *
strip_facts(JsonObject *plan){
	JsonObject *facts = member_object(member_object(plan, "design"), "facts");
	JsonArray *area = member_array(facts, "area");
	JsonArray *items = json_array_new();
	if (ls_string(facts, "since") != NULL)
		add_fact(items, plan_ui(plan, "since"), ls_string(facts, "since"));
	if (area != NULL) {
		g_autofree gchar *count = g_strdup_printf("%u", json_array_get_length(area));
		add_fact(items, plan_ui(plan, "towns"), count);
	}
	if (ls_string(facts, "hours") != NULL) {
		/* The first sentence of the hours fits the strip. */
		const gchar *hours = ls_string(facts, "hours");
		const gchar *stop = strstr(hours, ". ");
		g_autofree gchar *first = stop != NULL ? g_strndup(hours, (gsize)(stop - hours)) : g_strdup(hours);
		add_fact(items, plan_ui(plan, "hours"), first);
	}
	if (ls_string(facts, "address") != NULL) {
		/* "Street, [Suite,] Town, State Zip": the town is the part
		 * before the state. */
		g_auto(GStrv) parts = g_strsplit(ls_string(facts, "address"), ",", -1);
		guint n = g_strv_length(parts);
		g_autofree gchar *town = n >= 3 ? g_strstrip(g_strdup(parts[n - 2]))
			: g_strdup(ls_string(facts, "address"));
		add_fact(items, plan_ui(plan, "based_in"), town);
	}
	return items;
}

/* The order's facts and own photos in a designed home section, word for
 * word, in place of anything the writer put there. FALSE when the section
 * has too little left to show. */
static gboolean
fill_from_facts(JsonObject *plan, const gchar *type, const gchar *variant, JsonObject *content, GHashTable *used){
	JsonObject *facts = member_object(member_object(plan, "design"), "facts");
	JsonArray *area = member_array(facts, "area"), *process = member_array(facts, "process");
	guint i;
	if (g_strcmp0(type, "facts") == 0 && g_strcmp0(variant, "band") == 0) {
		/* The year set large, the services as ticks. */
		JsonArray *items = json_array_new(), *ticks = json_array_new();
		JsonArray *pages = member_array(member_object(plan, "design"), "pages");
		if (ls_string(facts, "since") != NULL)
			add_fact(items, plan_ui(plan, "since"), ls_string(facts, "since"));
		for (i = 0; pages != NULL && i < json_array_get_length(pages); i++) {
			JsonObject *page = json_array_get_object_element(pages, i);
			if (g_strcmp0(ls_string(page, "kind"), "detail") == 0 && json_array_get_length(ticks) < 16) {
				JsonObject *tick = json_object_new();
				json_object_set_string_member(tick, "name", ls_string(page, "title"));
				json_array_add_object_element(ticks, tick);
			}
		}
		json_object_set_array_member(content, "items", items);
		json_object_set_array_member(content, "ticks", ticks);
		if (json_object_has_member(content, "intro"))
			json_object_remove_member(content, "intro");
		return json_array_get_length(items) >= 1 && json_array_get_length(ticks) >= 2;
	}
	if (g_strcmp0(type, "facts") == 0 && g_strcmp0(variant, "logos") == 0) {
		/* The supplied press and certification names, as written. */
		JsonArray *items = json_array_new(), *press = member_array(facts, "press");
		for (i = 0; press != NULL && i < json_array_get_length(press) && i < 8; i++)
			add_fact(items, plan_ui(plan, "mentioned_in"), json_array_get_string_element(press, i));
		json_object_set_array_member(content, "items", items);
		json_object_set_string_member(content, "heading", plan_ui(plan, "mentioned_in"));
		if (json_object_has_member(content, "intro"))
			json_object_remove_member(content, "intro");
		return json_array_get_length(items) >= 1;
	}
	if (g_strcmp0(type, "facts") == 0 && g_strcmp0(variant, "rating") == 0) {
		/* The supplied rating, its count and its source, nothing more. */
		JsonObject *rating = member_object(facts, "rating");
		JsonArray *items = json_array_new();
		if (rating != NULL && ls_string(rating, "source") != NULL) {
			gchar score[G_ASCII_DTOSTR_BUF_SIZE];
			g_autofree gchar *count = g_strdup_printf("%" G_GINT64_FORMAT, json_object_get_int_member(rating, "count"));
			g_autofree gchar *with_count = replace_all(plan_ui(plan, "reviews_on"), "{n}", count);
			g_autofree gchar *label = replace_all(with_count, "{source}", ls_string(rating, "source"));
			add_fact(items, label, g_ascii_formatd(score, sizeof score, "%.1f",
				json_object_get_double_member(rating, "score")));
		}
		json_object_set_array_member(content, "items", items);
		if (json_object_has_member(content, "intro"))
			json_object_remove_member(content, "intro");
		return json_array_get_length(items) == 1;
	}
	if (g_strcmp0(type, "facts") == 0) {
		JsonArray *items = strip_facts(plan);
		json_object_set_array_member(content, "items", items);
		if (json_object_has_member(content, "intro"))
			json_object_remove_member(content, "intro");
		return json_array_get_length(items) >= 2;
	}
	if (g_strcmp0(type, "steps") == 0) {
		JsonArray *items = json_array_new();
		for (i = 0; process != NULL && i < json_array_get_length(process); i++) {
			JsonObject *item = json_object_new();
			json_object_set_string_member(item, "title", json_array_get_string_element(process, i));
			json_array_add_object_element(items, item);
		}
		json_object_set_array_member(content, "items", items);
		if (ls_string(content, "heading") == NULL)
			json_object_set_string_member(content, "heading", plan_ui(plan, "how_it_works"));
		return json_array_get_length(items) >= 2;
	}
	if (g_strcmp0(type, "areas") == 0) {
		JsonArray *items = json_array_new();
		const gchar *drop[] = {"art", "note", "hours", "address"};
		for (i = 0; i < G_N_ELEMENTS(drop); i++)
			if (json_object_has_member(content, drop[i]))
				json_object_remove_member(content, drop[i]);
		for (i = 0; area != NULL && i < json_array_get_length(area); i++) {
			JsonObject *item = json_object_new();
			json_object_set_string_member(item, "name", json_array_get_string_element(area, i));
			json_array_add_object_element(items, item);
		}
		if (json_array_get_length(items) > 0)
			json_object_set_array_member(content, "items", items);
		else {
			json_array_unref(items);
			if (json_object_has_member(content, "items"))
				json_object_remove_member(content, "items");
		}
		if (ls_string(facts, "hours") != NULL)
			json_object_set_string_member(content, "hours", ls_string(facts, "hours"));
		if (ls_string(facts, "address") != NULL)
			json_object_set_string_member(content, "address", ls_string(facts, "address"));
		if (json_object_has_member(content, "places"))
			json_object_remove_member(content, "places");
		/* One block per supplied shop, else one for the business itself. */
		if (g_strcmp0(variant, "location-blocks") == 0 || g_strcmp0(variant, "shop-chooser") == 0) {
			JsonArray *places = json_array_new(), *shops = member_array(facts, "locations");
			JsonObject *contact = member_object(plan, "contact");
			for (i = 0; shops != NULL && i < json_array_get_length(shops) && i < 4; i++) {
				JsonObject *shop = json_array_get_object_element(shops, i);
				json_array_add_object_element(places, place_view(ls_string(shop, "name"), ls_string(shop, "address"),
					ls_string(shop, "hours"), ls_string(shop, "phone") != NULL ? ls_string(shop, "phone")
					: ls_string(contact, "phone")));
			}
			if (json_array_get_length(places) == 0 && (ls_string(facts, "address") != NULL ||
				ls_string(facts, "hours") != NULL))
				json_array_add_object_element(places, place_view(ls_string(plan, "name"), ls_string(facts, "address"),
					ls_string(facts, "hours"), ls_string(contact, "phone")));
			json_object_set_array_member(content, "places", places);
			if (ls_string(content, "heading") == NULL)
				json_object_set_string_member(content, "heading", plan_ui(plan, "our_places"));
			return json_array_get_length(places) >= (g_strcmp0(variant, "shop-chooser") == 0 ? 2u : 1u);
		}
		if (ls_string(content, "heading") == NULL)
			json_object_set_string_member(content, "heading", plan_ui(plan, "where_we_work"));
		return json_object_has_member(content, "items") || json_object_has_member(content, "hours") ||
			json_object_has_member(content, "address");
	}
	if (g_strcmp0(type, "gallery") == 0) {
		JsonArray *items = json_array_new();
		JsonObject *photo;
		while (json_array_get_length(items) < 4 && (photo = own_photo(plan, used)) != NULL) {
			JsonObject *item = json_object_new();
			json_object_set_object_member(item, "art", photo_art(photo));
			if (ls_string(photo, "alt") != NULL)
				json_object_set_string_member(item, "caption", ls_string(photo, "alt"));
			json_array_add_object_element(items, item);
		}
		json_object_set_array_member(content, "items", items);
		if (ls_string(content, "heading") == NULL)
			json_object_set_string_member(content, "heading", plan_ui(plan, "own_work"));
		return json_array_get_length(items) >= 2;
	}
	if (g_strcmp0(type, "about") == 0) {
		if (json_object_has_member(content, "art"))
			json_object_remove_member(content, "art");
		if (json_object_has_member(content, "since"))
			json_object_remove_member(content, "since");
		if (ls_string(facts, "since") != NULL)
			json_object_set_string_member(content, "since", ls_string(facts, "since"));
		if (json_object_has_member(content, "signature"))
			json_object_remove_member(content, "signature");
		if (g_strcmp0(variant, "photo-story") == 0 || g_strcmp0(variant, "owner-letter") == 0) {
			JsonObject *photo = own_photo(plan, used);
			if (photo != NULL)
				json_object_set_object_member(content, "art", photo_art(photo));
		}
		/* The letter is signed with the business's own name. */
		if (g_strcmp0(variant, "owner-letter") == 0 && ls_string(plan, "name") != NULL)
			json_object_set_string_member(content, "signature", ls_string(plan, "name"));
		return ls_string(content, "heading") != NULL && ls_string(content, "body") != NULL;
	}
	return TRUE;
}

gboolean
ls_components_design_home(LsComponents *self, JsonNode *plan_node, JsonNode *page){
	JsonObject *plan, *design;
	JsonArray *home, *sections, *rebuilt, *supplied;
	g_autofree gboolean *taken = NULL;
	g_autoptr(GPtrArray) photos = NULL;
	g_autoptr(GHashTable) used = g_hash_table_new(g_str_hash, g_str_equal);
	guint i, j, next_photo = 0;
	g_return_val_if_fail(LS_IS_COMPONENTS(self), FALSE);
	if (plan_node == NULL || !JSON_NODE_HOLDS_OBJECT(plan_node) || page == NULL || !JSON_NODE_HOLDS_OBJECT(page))
		return FALSE;
	plan = json_node_get_object(plan_node);
	design = member_object(plan, "design");
	home = member_array(design, "home");
	sections = member_array(json_node_get_object(page), "sections");
	if (home == NULL || sections == NULL)
		return FALSE;
	supplied = member_array(plan, "reviews");
	taken = g_new0(gboolean, json_array_get_length(sections) + 1);
	photos = free_photos(plan, sections);
	{
		JsonObject *art = json_array_get_length(sections) > 0 && JSON_NODE_HOLDS_OBJECT(json_array_get_element(sections, 0))
			? member_object(member_object(json_array_get_object_element(sections, 0), "content"), "art") : NULL;
		if (art != NULL && ls_string(art, "src") != NULL)
			g_hash_table_add(used, (gpointer)ls_string(art, "src"));
	}
	rebuilt = json_array_new();
	for (i = 0; i < json_array_get_length(home); i++) {
		JsonObject *entry = json_array_get_object_element(home, i);
		const gchar *type = ls_string(entry, "component");
		JsonObject *slots = member_object(member_object(self->components, type), "slots");
		JsonObject *from = NULL, *source, *section, *content;
		g_autoptr(GList) keys = slots != NULL ? json_object_get_members(slots) : NULL;
		GList *key;
		for (j = 0; from == NULL && j < json_array_get_length(sections); j++) {
			JsonNode *node = json_array_get_element(sections, j);
			if (!taken[j] && JSON_NODE_HOLDS_OBJECT(node) && stands_for(type, ls_string(json_node_get_object(node), "component"))) {
				from = json_node_get_object(node);
				taken[j] = TRUE;
			}
		}
		/* Reviews are the order's quotes; the writer may only head them.
		 * The trust strip, the process, the area and the gallery are the
		 * order's facts and photos, so they stand without the writer. */
		if (from == NULL && !(g_strcmp0(type, "reviews") == 0 && supplied != NULL) && !builder_filled(type))
			continue;
		section = json_object_new();
		content = json_object_new();
		json_object_set_string_member(section, "component", type);
		json_object_set_string_member(section, "variant", ls_string(entry, "variant"));
		if (from != NULL && ls_string(from, "id") != NULL)
			json_object_set_string_member(section, "id", ls_string(from, "id"));
		source = from != NULL ? member_object(from, "content") : NULL;
		for (key = keys; source != NULL && key != NULL; key = key->next)
			if (json_object_has_member(source, key->data) && !JSON_NODE_HOLDS_NULL(json_object_get_member(source, key->data)))
				json_object_set_member(content, key->data, node_copy(json_object_get_member(source, key->data)));
		if (g_strcmp0(type, "reviews") == 0 && supplied != NULL) {
			JsonArray *items = json_array_new();
			const gchar *fields[] = {"quote", "name", "source", "headline"};
			guint k, f;
			for (k = 0; k < json_array_get_length(supplied); k++) {
				JsonObject *review = json_array_get_object_element(supplied, k), *item = json_object_new();
				for (f = 0; f < G_N_ELEMENTS(fields); f++)
					if (ls_string(review, fields[f]) != NULL)
						json_object_set_string_member(item, fields[f], ls_string(review, fields[f]));
				json_array_add_object_element(items, item);
			}
			json_object_set_array_member(content, "items", items);
		}
		/* Photo cards and service groups: each a supplied photo the opening
		 * does not show, while there are photos left. */
		if (g_strcmp0(type, "services") == 0) {
			JsonArray *items = member_array(content, "items");
			for (j = 0; items != NULL && j < json_array_get_length(items); j++) {
				JsonObject *item = json_array_get_object_element(items, j);
				if (json_object_has_member(item, "art"))
					json_object_remove_member(item, "art");
				/* A service with a page of its own links to it. */
				{
					JsonArray *designed = member_array(design, "pages");
					guint d;
					for (d = 0; designed != NULL && d < json_array_get_length(designed); d++) {
						JsonObject *target = json_array_get_object_element(designed, d);
						if (g_strcmp0(ls_string(target, "kind"), "detail") == 0 && ls_string(item, "name") != NULL &&
							g_ascii_strcasecmp(ls_string(target, "title"), ls_string(item, "name")) == 0) {
							JsonObject *link = json_object_new();
							json_object_set_string_member(link, "href", ls_string(target, "path"));
							json_object_set_string_member(link, "label", ls_string(item, "name"));
							json_object_set_object_member(item, "link", link);
						}
					}
				}
				if (g_strcmp0(ls_string(entry, "variant"), "photo-cards") == 0 ||
					g_strcmp0(ls_string(entry, "variant"), "sublinks") == 0 ||
					g_strcmp0(ls_string(entry, "variant"), "round-photos") == 0 ||
					g_strcmp0(ls_string(entry, "variant"), "feature-rows") == 0) {
					JsonObject *photo = next_free(photos, &next_photo, used);
					if (photo != NULL)
						json_object_set_object_member(item, "art", photo_art(photo));
				}
			}
		}
		/* The opening's second photo and the closing band's photo: supplied
		 * photos no earlier section shows. */
		{
			const gchar *slot = g_strcmp0(type, "hero") == 0 && g_strcmp0(ls_string(entry, "variant"), "photo-pair") == 0
				? "pair" : g_strcmp0(type, "cta") == 0 && g_strcmp0(ls_string(entry, "variant"), "photo-band") == 0
				? "art" : NULL;
			if (slot != NULL) {
				JsonObject *photo = next_free(photos, &next_photo, used);
				if (json_object_has_member(content, slot) && g_str_equal(slot, "pair"))
					json_object_remove_member(content, slot);
				if (photo != NULL)
					json_object_set_object_member(content, slot, photo_art(photo));
			}
		}
		/* The collage's and the strip's further photos and the stats bar's
		 * facts: supplied photos and facts, from the builder. */
		if (g_strcmp0(type, "hero") == 0 && (g_strcmp0(ls_string(entry, "variant"), "collage") == 0 ||
			g_strcmp0(ls_string(entry, "variant"), "thumb-strip") == 0)) {
			JsonArray *more = json_array_new();
			guint want = g_strcmp0(ls_string(entry, "variant"), "collage") == 0 ? 2 : 3;
			JsonObject *photo;
			while (json_array_get_length(more) < want && (photo = next_free(photos, &next_photo, used)) != NULL) {
				JsonObject *item = json_object_new();
				json_object_set_object_member(item, "art", photo_art(photo));
				json_array_add_object_element(more, item);
			}
			json_object_set_array_member(content, "photos", more);
		}
		if (g_strcmp0(type, "hero") == 0 && g_strcmp0(ls_string(entry, "variant"), "stats-bar") == 0) {
			JsonArray *stats = strip_facts(plan);
			while (json_array_get_length(stats) > 4)
				json_array_remove_element(stats, json_array_get_length(stats) - 1);
			json_object_set_array_member(content, "stats", stats);
		}
		/* The builder draws the phone and email; a writer's row repeating
		 * either is dropped. */
		if (g_strcmp0(type, "contact") == 0 && member_array(content, "items") != NULL) {
			JsonArray *items = member_array(content, "items");
			JsonObject *contact = member_object(plan, "contact");
			g_autofree gchar *tel = contact != NULL && ls_string(contact, "phone") != NULL
				? ls_phone_tel(ls_string(contact, "phone")) : NULL;
			const gchar *email = contact != NULL ? ls_string(contact, "email") : NULL;
			j = json_array_get_length(items);
			while (j-- > 0) {
				JsonObject *item = json_array_get_object_element(items, j);
				const gchar *value = item != NULL ? ls_string(item, "value") : NULL;
				g_autofree gchar *as_tel = value != NULL ? ls_phone_tel(value) : NULL;
				if (value != NULL && ((tel != NULL && g_strcmp0(as_tel, tel) == 0) ||
					(email != NULL && g_ascii_strcasecmp(value, email) == 0)))
					json_array_remove_element(items, j);
			}
		}
		if (!fill_from_facts(plan, type, ls_string(entry, "variant"), content, used)) {
			json_object_unref(content);
			json_object_unref(section);
			continue;
		}
		/* The photo the area card lies over. */
		if (g_strcmp0(type, "areas") == 0 && g_strcmp0(ls_string(entry, "variant"), "card-over-photo") == 0) {
			JsonObject *photo = next_free(photos, &next_photo, used);
			if (photo != NULL)
				json_object_set_object_member(content, "art", photo_art(photo));
		}
		/* The builder draws the opening's, the contact's and the closing
		 * call's actions. */
		if ((g_strcmp0(type, "hero") == 0 || g_strcmp0(type, "contact") == 0 || g_strcmp0(type, "cta") == 0) &&
			json_object_has_member(content, "actions"))
			json_object_remove_member(content, "actions");
		json_object_set_object_member(section, "content", content);
		json_array_add_object_element(rebuilt, section);
	}
	json_object_set_array_member(json_node_get_object(page), "sections", rebuilt);
	{
		g_autoptr(GHashTable) ids = g_hash_table_new(g_str_hash, g_str_equal);
		JsonNode *node = json_object_get_member(json_node_get_object(page), "sections");
		for (i = 0; i < json_array_get_length(rebuilt); i++)
			if (ls_string(json_array_get_object_element(rebuilt, i), "id") != NULL)
				g_hash_table_add(ids, (gpointer)ls_string(json_array_get_object_element(rebuilt, i), "id"));
		drop_dangling(node, ids);
	}
	return TRUE;
}

/* The role a component plays on a home page; anything unlisted reads as story. */

typedef struct {
	JsonNode *node;
	const gchar *component;
	guint rank;
	guint tie;
	guint index;
} Placed;


const gchar *
ls_components_get_look_description(LsComponents *self, const gchar *axis, const gchar *value){
	g_return_val_if_fail(LS_IS_COMPONENTS(self), NULL);
	g_return_val_if_fail(axis != NULL && value != NULL, NULL);
	return ls_string(member_object(member_object(self->looks, axis), "values"), value);
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
		"image: {\"src\": a planned art path or a plan photos path, \"alt\": text (a photo's own alt), or \"\" when purely decorative}.\n"
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
	{"base_size", 1.0, 1.375, 1.0625}, {"ratio", 1.1, 1.6, 1.25}, {"measure", 45, 90, 66},
	{"radius", 0, 32, 2}, {"border", 0, 4, 1}, {"space", 0.75, 1.5, 1}, {"heading_weight", 300, 900, 700},
	{"heading_tracking", -0.08, 0.2, -0.01}, {"heading_leading", 0.9, 1.4, 1.1}, {"max_width", 56, 100, 76}
};
static const gchar *const color_tokens[] = {"paper", "ink", "muted", "surface", "line", "accent", "accent_ink"};
static const gchar *const font_tokens[] = {"font_display", "font_body", "font_mono"};
static const gchar *const ui_keys[] = {"skip", "menu", "nav", "footer_nav", "contents", "filter",
	"count_one", "count_many", "required", "reset", "preview", "download", "error_required",
	"error_invalid", "error_summary", "breadcrumb", "pager", "previous", "next", "letters", "scroll",
	"checklist_yes", "checklist_no", "close", "ingredients", "method", "social", "share", "copy_link", "copied",
	"sending", "sent", "send_failed", "call", "quote", "quick_actions", "send_another", "book", "since", "hours",
	"address", "towns", "all_services", "based_in", "how_it_works", "where_we_work", "own_work", "reviews_on",
	"mentioned_in", "our_places", "announcement", "dismiss", "directions", "scheme", "scheme_system", "scheme_light",
	"scheme_dark", "contact_by", "by_phone", "by_email", "preferred_date", "preferred_time", "urgent", "agree",
	"photos", "photos_hint", "service"};
static const gchar *const ui_defaults[] = {"Skip to content", "Menu", "Primary", "Footer", "Contents",
	"Filter entries", "{n} entry shown", "{n} entries shown", "required", "Clear", "Preview",
	"Download text file", "Fill in this field.", "Check this entry.", "{n} fields need attention.",
	"Breadcrumb", "More pages", "Previous", "Next", "Jump to letter", "Scrollable list", "Included",
	"Not included", "Close", "Ingredients", "Method", "Follow us", "Share this page", "Copy link", "Link copied",
	"Sending your message.", "Thank you. Your message was sent.", "Your message was not sent. Please try again.",
	"Call", "Request a quote", "Get in touch", "Send another message", "Book online", "In business since", "Hours",
	"Address", "Towns served", "All services", "Based in", "How it works", "Where we work", "Our work",
	"{n} reviews on {source}", "Mentioned in", "Where to find us", "Notice", "Dismiss", "Get directions",
	"Colours", "Auto", "Light", "Dark", "Contact me by", "Phone", "Email", "Preferred date", "Preferred time",
	"This is urgent", "You may contact me about this request.", "Photos of the job", "Optional. Pictures help us see the job.",
	"Service"};
/* An interface string for @plan: its own ui text, else the default. */
static const gchar *
plan_ui(JsonObject *plan, const gchar *key){
	JsonObject *ui = member_object(plan, "ui");
	guint i;
	if (ui != NULL && ls_string(ui, key) != NULL)
		return ls_string(ui, key);
	for (i = 0; i < G_N_ELEMENTS(ui_keys); i++)
		if (g_str_equal(ui_keys[i], key))
			return ui_defaults[i];
	return key;
}

static const gchar *const art_ratios[] = {"16:9", "4:3", "3:2", "1:1", "3:4", "2:1", "3:1", "21:9", NULL};

static gdouble
token_number(JsonObject *tokens, const NumberToken *token){
	JsonNode *node = tokens != NULL ? json_object_get_member(tokens, token->name) : NULL;
	return is_number(node) ? json_node_get_double(node) : token->fallback;
}

/* Talk about inputs nobody supplied, which visitors must never read: the
 * matched phrase, or NULL. Whole words only, case-insensitive. */
static const gchar *
missing_talk(const gchar *text){
	static const gchar *const phrases[] = {"not been supplied", "not supplied", "not been provided",
		"were not provided", "was not provided", "not been given", "to be confirmed", "to be announced",
		"details to come", "will be posted", "not published yet", "not yet published", "tbd", "tbc", "tba", NULL};
	g_autofree gchar *lower = g_ascii_strdown(text, -1);
	guint i;
	for (i = 0; phrases[i] != NULL; i++) {
		const gchar *at;
		for (at = strstr(lower, phrases[i]); at != NULL; at = strstr(at + 1, phrases[i])) {
			gchar after = at[strlen(phrases[i])];
			if ((at == lower || !g_ascii_isalnum(at[-1])) && !g_ascii_isalnum(after))
				return phrases[i];
		}
	}
	return NULL;
}

/* Plan fields that brief the writers and never reach a visitor. */
static gboolean
instruction_field(const gchar *where){
	return g_str_equal(where, "voice") || g_str_equal(where, "character") || g_str_has_prefix(where, "missing") ||
		g_str_has_suffix(where, ".brief");
}

/* Whether @plan is an explicit demo build (site.demo), the only kind of site
 * that may say its business is an example. */
static gboolean
plan_is_demo(JsonObject *plan){
	JsonNode *demo = plan != NULL ? json_object_get_member(plan, "demo") : NULL;
	return demo != NULL && JSON_NODE_HOLDS_VALUE(demo) && json_node_get_value_type(demo) == G_TYPE_BOOLEAN &&
		json_node_get_boolean(demo);
}

static void
check_text(Problems *problems, const gchar *where, JsonNode *node, gboolean required, gsize max){
	const gchar *text;
	const gchar *talk;
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
	else if ((talk = missing_talk(text)) != NULL)
		problem(problems, "%s tells visitors about facts that were not supplied (\"%s\"); leave anything missing "
			"out silently and write about what is true", where, talk);
	else if (!instruction_field(where) && (talk = ls_taste_disclosure(text, g_str_equal(where, "footer.note"))) != NULL)
		problem(problems, "%s talks about the site itself (\"%s\"); visible copy never says illustrative, and any "
			"note that the business or its photos are examples belongs only in footer.note, in other words", where, talk);
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
	JsonArray *photos = member_array(plan, "photos");
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
	/* Supplied photographs are images the pages may use, like planned art. */
	for (i = 0; photos != NULL && i < json_array_get_length(photos); i++) {
		JsonNode *node = json_array_get_element(photos, i);
		const gchar *path = JSON_NODE_HOLDS_OBJECT(node) ? ls_string(json_node_get_object(node), "path") : NULL;
		if (path != NULL)
			g_hash_table_add(scope->art, g_strdup(path));
	}
}

/* A supplied profile address: https, one line, no markup characters. */
static gboolean
profile_url_valid(const gchar *url){
	return url != NULL && g_str_has_prefix(url, "https://") && strlen(url) > 8 && strlen(url) <= 300 &&
		strpbrk(url, " \t\r\n\"'<>\\`") == NULL && g_uri_is_valid(url, G_URI_FLAGS_NONE, NULL);
}

/* The network a profile address belongs to: a listed host or one of its
 * subdomains, a federated @name path, or "web". */
static const gchar *
network_of(LsComponents *self, const gchar *url){
	g_autoptr(GUri) uri = g_uri_parse(url, G_URI_FLAGS_NONE, NULL);
	g_autofree gchar *host = uri != NULL && g_uri_get_host(uri) != NULL ? g_ascii_strdown(g_uri_get_host(uri), -1) : NULL;
	g_autoptr(GList) names = NULL;
	GList *name;
	const gchar *bare;
	if (host == NULL || self->networks == NULL)
		return "web";
	bare = g_str_has_prefix(host, "www.") ? host + 4 : g_str_has_prefix(host, "m.") ? host + 2 : host;
	names = json_object_get_members(self->networks);
	for (name = names; name != NULL; name = name->next) {
		JsonArray *hosts = member_array(member_object(self->networks, name->data), "hosts");
		guint i;
		for (i = 0; hosts != NULL && i < json_array_get_length(hosts); i++) {
			const gchar *known = json_array_get_string_element(hosts, i);
			gsize length = strlen(bare), size = strlen(known);
			if (g_str_equal(bare, known) || (length > size && g_str_has_suffix(bare, known) && bare[length - size - 1] == '.'))
				return name->data;
		}
	}
	if (g_str_has_prefix(g_uri_get_path(uri), "/@") && member_object(self->networks, "mastodon") != NULL)
		return "mastodon";
	return "web";
}

/* The handle a visitor recognises: the last path segment, or the host. */
static gchar *
profile_handle(const gchar *url, const gchar *network){
	g_autoptr(GUri) uri = g_uri_parse(url, G_URI_FLAGS_NONE, NULL);
	const gchar *path = uri != NULL ? g_uri_get_path(uri) : NULL;
	g_auto(GStrv) parts = path != NULL ? g_strsplit(path, "/", -1) : NULL;
	const gchar *const at[] = {"instagram", "x", "tiktok", "threads", "twitch", "github", "snapchat", "pinterest", NULL};
	const gchar *last = NULL;
	guint i;
	for (i = 0; parts != NULL && parts[i] != NULL; i++)
		if (parts[i][0] != '\0')
			last = parts[i];
	if (last == NULL)
		return g_strdup(uri != NULL && g_uri_get_host(uri) != NULL ? g_uri_get_host(uri) : url);
	return g_strv_contains(at, network) && last[0] != '@' ? g_strconcat("@", last, NULL) : g_strdup(last);
}

JsonArray *
ls_components_social(LsComponents *self, JsonArray *links){
	JsonArray *out;
	guint i;
	g_return_val_if_fail(LS_IS_COMPONENTS(self), NULL);
	out = json_array_new();
	for (i = 0; links != NULL && i < json_array_get_length(links); i++) {
		JsonNode *node = json_array_get_element(links, i);
		JsonObject *link = JSON_NODE_HOLDS_OBJECT(node) ? json_node_get_object(node) : NULL;
		const gchar *url = link != NULL ? ls_string(link, "url") : NULL;
		const gchar *network;
		JsonObject *entry;
		g_autofree gchar *handle = NULL;
		if (!profile_url_valid(url))
			continue;
		network = network_of(self, url);
		handle = profile_handle(url, network);
		entry = json_object_new();
		json_object_set_string_member(entry, "url", url);
		json_object_set_string_member(entry, "network", network);
		json_object_set_string_member(entry, "label", ls_string(link, "label") != NULL ? ls_string(link, "label")
			: self->networks != NULL && ls_string(member_object(self->networks, network), "label") != NULL
			? ls_string(member_object(self->networks, network), "label") : "Website");
		json_object_set_string_member(entry, "handle", handle);
		json_array_add_object_element(out, entry);
	}
	return out;
}

/* social is written by the builder from the recipe's supplied profiles:
 * [{url, network, label, handle}]. */
/* plan.missing: the facts a visitor would expect that the order did not
 * supply, as short questions for whoever gathers them from the owner. */
static void
check_missing(Problems *problems, JsonObject *plan){
	JsonArray *missing = member_array(plan, "missing");
	guint i;
	if (!json_object_has_member(plan, "missing"))
		return;
	if (missing == NULL || json_array_get_length(missing) > 12) {
		problem(problems, "missing must be an array of at most 12 short questions");
		return;
	}
	for (i = 0; i < json_array_get_length(missing); i++) {
		g_autofree gchar *here = g_strdup_printf("missing[%u]", i);
		check_text(problems, here, json_array_get_element(missing, i), TRUE, 200);
	}
}

static void
check_social(Problems *problems, LsComponents *self, JsonObject *plan){
	JsonArray *social = member_array(plan, "social");
	guint i;
	if (!json_object_has_member(plan, "social"))
		return;
	if (social == NULL || json_array_get_length(social) > 12) {
		problem(problems, "social must list at most 12 supplied profiles");
		return;
	}
	for (i = 0; i < json_array_get_length(social); i++) {
		JsonNode *node = json_array_get_element(social, i);
		JsonObject *entry = JSON_NODE_HOLDS_OBJECT(node) ? json_node_get_object(node) : NULL;
		const gchar *network = entry != NULL ? ls_string(entry, "network") : NULL;
		if (entry == NULL || !ls_object_keys(entry, "|url|network|label|handle|", NULL) ||
			!profile_url_valid(ls_string(entry, "url")) || network == NULL ||
			(!g_str_equal(network, "web") && (self->networks == NULL || member_object(self->networks, network) == NULL)) ||
			ls_string(entry, "label") == NULL || g_utf8_strlen(ls_string(entry, "label"), -1) > 60 ||
			(ls_string(entry, "handle") != NULL && g_utf8_strlen(ls_string(entry, "handle"), -1) > 120))
			problem(problems, "social[%u] must be {url: https address, network: a known network or web, label, handle}", i);
	}
}

/* form is written by the builder from site.form, already validated there;
 * a plan restored from outlines keeps the same shape. */
static void
check_supplied_form(Problems *problems, JsonObject *plan){
	JsonObject *form = member_object(plan, "form");
	JsonArray *fields = form != NULL ? member_array(form, "fields") : NULL;
	const gchar *action = form != NULL ? ls_string(form, "action") : NULL;
	guint i;
	if (!json_object_has_member(plan, "form"))
		return;
	if (form == NULL || !ls_object_keys(form, "|action|fields|honeypot|", NULL) || action == NULL ||
		!(g_str_has_prefix(action, "https://") || (action[0] == '/' && action[1] != '/')) ||
		strpbrk(action, " \t\r\n\"'<>\\`") != NULL || fields == NULL ||
		json_array_get_length(fields) == 0 || json_array_get_length(fields) > 12) {
		problem(problems, "form must be the supplied {action, fields, honeypot}");
		return;
	}
	for (i = 0; i < json_array_get_length(fields); i++) {
		JsonNode *node = json_array_get_element(fields, i);
		JsonObject *field = JSON_NODE_HOLDS_OBJECT(node) ? json_node_get_object(node) : NULL;
		if (field == NULL || blank(ls_string(field, "name")) || blank(ls_string(field, "label")))
			problem(problems, "form.fields[%u] must have a name and a label", i);
	}
}

/* contact is written by the builder from site.contact: the phone and email
 * it links, each already checked there and checked again here. */
static void
check_contact(Problems *problems, JsonObject *plan){
	JsonObject *contact = member_object(plan, "contact");
	const gchar *phone = contact != NULL ? ls_string(contact, "phone") : NULL;
	const gchar *email = contact != NULL ? ls_string(contact, "email") : NULL;
	g_autofree gchar *tel = phone != NULL ? ls_phone_tel(phone) : NULL;
	if (!json_object_has_member(plan, "contact"))
		return;
	if (contact == NULL || !ls_object_keys(contact, "|phone|email|", NULL) || json_object_get_size(contact) == 0 ||
		(json_object_has_member(contact, "phone") && tel == NULL) ||
		(json_object_has_member(contact, "email") && (email == NULL || !ls_email_valid(email))))
		problem(problems, "contact must be the supplied {phone, email}: a phone number of digits, spaces, "
			"+ . - ( ) and a plain email address");
}

/* modules is written by the builder from site.modules: each supplied podbay
 * module's id, title, snippet and stylesheet, checked again here because a
 * plan restored from outlines draws them into pages. */
static void
check_modules(Problems *problems, JsonObject *plan){
	JsonArray *modules = member_array(plan, "modules");
	g_autoptr(GHashTable) seen = g_hash_table_new(g_str_hash, g_str_equal);
	guint i;
	if (!json_object_has_member(plan, "modules"))
		return;
	if (modules == NULL || json_array_get_length(modules) == 0 || json_array_get_length(modules) > 8) {
		problem(problems, "modules must list the 1 to 8 supplied podbay modules");
		return;
	}
	for (i = 0; i < json_array_get_length(modules); i++) {
		JsonNode *node = json_array_get_element(modules, i);
		JsonObject *module = JSON_NODE_HOLDS_OBJECT(node) ? json_node_get_object(node) : NULL;
		const gchar *id = module != NULL ? ls_string(module, "id") : NULL;
		g_autoptr(GError) why = NULL;
		if (module == NULL || !ls_object_keys(module, "|id|title|snippet|css|", NULL) || !ls_module_id_valid(id) ||
			!g_hash_table_add(seen, (gpointer)id)) {
			problem(problems, "modules[%u] must be a supplied {id, title, snippet, css} with a unique id", i);
			continue;
		}
		if (!ls_module_check_snippet(id, ls_string(module, "snippet"), &why) ||
			!ls_module_check_css(id, ls_string(module, "css"), &why))
			problem(problems, "modules[%u]: %s", i, why->message);
	}
}

/* The supplied module @id in the plan, or NULL. */
static JsonObject *
plan_module(JsonObject *plan, const gchar *id){
	JsonArray *modules = plan != NULL ? member_array(plan, "modules") : NULL;
	guint i;
	for (i = 0; id != NULL && modules != NULL && i < json_array_get_length(modules); i++) {
		JsonNode *node = json_array_get_element(modules, i);
		if (JSON_NODE_HOLDS_OBJECT(node) && g_strcmp0(ls_string(json_node_get_object(node), "id"), id) == 0)
			return json_node_get_object(node);
	}
	return NULL;
}

/* The supplied phone as the builder links it: {phone, tel}, or NULL. */
static JsonObject *
call_view(JsonObject *plan){
	JsonObject *contact = plan != NULL ? member_object(plan, "contact") : NULL;
	const gchar *phone = contact != NULL ? ls_string(contact, "phone") : NULL;
	g_autofree gchar *tel = phone != NULL ? ls_phone_tel(phone) : NULL;
	JsonObject *view;
	if (tel == NULL)
		return NULL;
	view = json_object_new();
	json_object_set_string_member(view, "phone", phone);
	json_object_set_string_member(view, "tel", tel);
	if (ls_string(contact, "email") != NULL && ls_email_valid(ls_string(contact, "email")))
		json_object_set_string_member(view, "email", ls_string(contact, "email"));
	return view;
}

/* A form section on a site with a supplied form takes its target and fields
 * from it; the model writes only the words around them. Returns a new copy of
 * @content, or NULL when the site has no supplied form. */
static JsonObject *
supplied_form_content(JsonObject *plan, JsonObject *content){
	JsonObject *form = plan != NULL ? member_object(plan, "form") : NULL;
	g_autoptr(JsonNode) wrapped = NULL;
	g_autoptr(JsonNode) copy = NULL;
	g_autofree gchar *text = NULL;
	JsonObject *out;
	if (form == NULL || content == NULL || member_array(form, "fields") == NULL)
		return NULL;
	wrapped = json_node_new(JSON_NODE_OBJECT);
	json_node_set_object(wrapped, content);
	text = ls_json_encode(wrapped);
	copy = ls_json_parse(text, NULL);
	if (copy == NULL || !JSON_NODE_HOLDS_OBJECT(copy))
		return NULL;
	out = json_object_ref(json_node_get_object(copy));
	json_object_set_string_member(out, "mode", "endpoint");
	json_object_set_string_member(out, "target", ls_string(form, "action"));
	json_object_set_array_member(out, "fields", json_array_ref(member_array(form, "fields")));
	return out;
}

/* A sized photo's variants: 1..9 safe assets/ WebP files by ascending width,
 * the last one at the photo's own (capped) size. See docs/images.org. */
static gboolean
variants_valid(JsonNode *node, gint64 width, gint64 height){
	JsonArray *variants = JSON_NODE_HOLDS_ARRAY(node) ? json_node_get_array(node) : NULL;
	gint64 previous = 0;
	guint i, length;
	if (variants == NULL || (length = json_array_get_length(variants)) < 1 || length > 9)
		return FALSE;
	for (i = 0; i < length; i++) {
		JsonNode *element = json_array_get_element(variants, i);
		JsonObject *variant = JSON_NODE_HOLDS_OBJECT(element) ? json_node_get_object(element) : NULL;
		const gchar *path = variant != NULL ? ls_string(variant, "path") : NULL;
		JsonNode *w = variant != NULL ? json_object_get_member(variant, "width") : NULL;
		JsonNode *h = variant != NULL ? json_object_get_member(variant, "height") : NULL;
		if (variant == NULL || !ls_object_keys(variant, "|path|width|height|", NULL) || path == NULL ||
			!g_str_has_prefix(path, "assets/") || !g_str_has_suffix(path, ".webp") || strstr(path, "..") != NULL ||
			strpbrk(path, " ,\"'<>\\") != NULL || w == NULL || h == NULL ||
			json_node_get_value_type(w) != G_TYPE_INT64 || json_node_get_value_type(h) != G_TYPE_INT64 ||
			json_node_get_int(w) <= previous || json_node_get_int(w) > 20000 ||
			json_node_get_int(h) < 1 || json_node_get_int(h) > 20000)
			return FALSE;
		previous = json_node_get_int(w);
		if (i + 1 == length && (json_node_get_int(w) != width || json_node_get_int(h) != height))
			return FALSE;
	}
	return TRUE;
}

/* photos is written by the builder from the recipe and the seeded images:
 * {path, alt, credit, width, height, variants}. */
static void
check_photos(Problems *problems, JsonObject *plan){
	JsonArray *photos = member_array(plan, "photos");
	guint i;
	if (!json_object_has_member(plan, "photos"))
		return;
	if (photos == NULL || json_array_get_length(photos) > 48) {
		problem(problems, "photos must list at most 48 supplied photographs");
		return;
	}
	for (i = 0; i < json_array_get_length(photos); i++) {
		JsonNode *node = json_array_get_element(photos, i);
		JsonObject *photo = JSON_NODE_HOLDS_OBJECT(node) ? json_node_get_object(node) : NULL;
		const gchar *path = photo != NULL ? ls_string(photo, "path") : NULL;
		JsonNode *width = photo != NULL ? json_object_get_member(photo, "width") : NULL;
		JsonNode *height = photo != NULL ? json_object_get_member(photo, "height") : NULL;
		if (photo == NULL || !ls_object_keys(photo, "|path|alt|credit|stock|focus|width|height|variants|", NULL) || path == NULL ||
			(json_object_has_member(photo, "stock") && (!JSON_NODE_HOLDS_VALUE(json_object_get_member(photo, "stock")) ||
			json_node_get_value_type(json_object_get_member(photo, "stock")) != G_TYPE_BOOLEAN)) ||
			(json_object_has_member(photo, "focus") && !ls_focus_valid(ls_string(photo, "focus"))) ||
			!g_str_has_prefix(path, "assets/") || strstr(path, "..") != NULL || strpbrk(path, " \"'<>\\") != NULL ||
			ls_string(photo, "alt") == NULL || width == NULL || height == NULL ||
			json_node_get_value_type(width) != G_TYPE_INT64 || json_node_get_value_type(height) != G_TYPE_INT64 ||
			json_node_get_int(width) < 1 || json_node_get_int(height) < 1 ||
			json_node_get_int(width) > 20000 || json_node_get_int(height) > 20000)
			problem(problems, "photos[%u] must be {path under assets/, alt, credit, width, height}", i);
		else if (json_object_has_member(photo, "variants") && !variants_valid(json_object_get_member(photo, "variants"),
			json_node_get_int(width), json_node_get_int(height)))
			problem(problems, "photos[%u].variants must be 1 to 9 {path, width, height} assets/ WebP files by "
				"ascending width, the last at the photo's width and height", i);
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
	/* Optional display: a web font that arrives after the page is first
	 * drawn is kept for the next page instead of swapping in and moving
	 * the header and opening under the visitor. The preloads below fetch
	 * the files alongside the stylesheet, so they are normally in time. */
	g_string_append(out, "&display=optional");
	return g_string_free(out, FALSE);
}

/* The most font files a page preloads. */
#define FONT_PRELOADS 8

/* The Latin file of each family's weight (and its italic) on Bunny Fonts,
 * at the address the stylesheet asks for, so the page head can start
 * fetching them with the stylesheet. */
static JsonArray *
font_preloads(JsonObject *plan){
	JsonArray *fonts = member_array(plan, "fonts");
	JsonArray *out = json_array_new();
	guint i, j, k;
	for (i = 0; fonts != NULL && i < json_array_get_length(fonts); i++) {
		JsonObject *font = json_array_get_object_element(fonts, i);
		JsonArray *weights = member_array(font, "weights");
		gboolean italic = json_object_has_member(font, "italic") && json_object_get_boolean_member(font, "italic");
		g_autofree gchar *family = slugify(ls_string(font, "family"), 40);
		for (j = 0; weights != NULL && j < json_array_get_length(weights); j++)
			for (k = 0; k < (italic ? 2u : 1u) && json_array_get_length(out) < FONT_PRELOADS; k++) {
				JsonObject *preload = json_object_new();
				g_autofree gchar *href = g_strdup_printf("https://fonts.bunny.net/%s/files/%s-latin-%" G_GINT64_FORMAT "-%s.woff2",
					family, family, json_array_get_int_element(weights, j), k == 0 ? "normal" : "italic");
				json_object_set_string_member(preload, "href", href);
				json_array_add_object_element(out, preload);
			}
	}
	return out;
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

/* nav: up to 8 links, each optionally with up to 8 children for a drop-down
 * menu. Every href, child or not, is a planned route. */
static void
check_nav(Problems *problems, LinkScope *scope, JsonArray *nav){
	guint i, j;
	if (json_array_get_length(nav) > 8)
		problem(problems, "nav allows at most 8 links");
	for (i = 0; i < json_array_get_length(nav); i++) {
		JsonNode *entry = json_array_get_element(nav, i);
		JsonObject *link = JSON_NODE_HOLDS_OBJECT(entry) ? json_node_get_object(entry) : NULL;
		g_autofree gchar *where = g_strdup_printf("nav[%u]", i);
		JsonArray *children = link != NULL ? member_array(link, "children") : NULL;
		if (link != NULL && json_object_has_member(link, "children")) {
			g_autoptr(JsonNode) bare = json_node_new(JSON_NODE_OBJECT);
			JsonObject *copy = json_object_new();
			if (json_object_has_member(link, "href"))
				json_object_set_member(copy, "href", json_node_copy(json_object_get_member(link, "href")));
			if (json_object_has_member(link, "label"))
				json_object_set_member(copy, "label", json_node_copy(json_object_get_member(link, "label")));
			json_node_take_object(bare, copy);
			check_link_object(problems, scope, where, bare);
			if (!ls_object_keys(link, "|href|label|children|", NULL))
				problem(problems, "%s may only have href, label and children", where);
			if (children == NULL || json_array_get_length(children) == 0) {
				problem(problems, "%s.children must list 1 to 8 links", where);
				continue;
			}
			{
				g_autofree gchar *under = g_strconcat(where, ".children", NULL);
				check_plan_links(problems, scope, under, children, 8);
			}
			for (j = 0; j < json_array_get_length(children); j++) {
				JsonNode *child = json_array_get_element(children, j);
				const gchar *href = JSON_NODE_HOLDS_OBJECT(child) ? ls_string(json_node_get_object(child), "href") : NULL;
				if (href != NULL && !g_hash_table_contains(scope->routes, href))
					problem(problems, "%s.children[%u] must link to a planned page", where, j);
			}
		} else
			check_link_object(problems, scope, where, entry);
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
	if (!json_object_has_member(plan, "restyle") || JSON_NODE_HOLDS_NULL(json_object_get_member(plan, "restyle")))
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

/* design is the builder's (the picker's), reviews the order's supplied
 * quotes; a model answer never sets either. */
static void
check_design(Problems *problems, LsComponents *self, JsonObject *plan){
	JsonArray *reviews = member_array(plan, "reviews");
	guint i;
	if (json_object_has_member(plan, "design")) {
		g_autoptr(GString) why = g_string_new(NULL);
		if (self->library == NULL || member_object(plan, "design") == NULL)
			problem(problems, "design needs a library with presets");
		else if (!ls_design_check(self->library, member_object(plan, "design"), why))
			problem(problems, "%s", g_strstrip(why->str));
	}
	if (!json_object_has_member(plan, "reviews"))
		return;
	if (reviews == NULL || json_array_get_length(reviews) == 0 || json_array_get_length(reviews) > 9) {
		problem(problems, "reviews must list 1 to 9 supplied quotes");
		return;
	}
	for (i = 0; i < json_array_get_length(reviews); i++) {
		JsonNode *node = json_array_get_element(reviews, i);
		JsonObject *review = JSON_NODE_HOLDS_OBJECT(node) ? json_node_get_object(node) : NULL;
		if (review == NULL || !ls_object_keys(review, "|quote|name|source|headline|", NULL) ||
			blank(ls_string(review, "quote")) || blank(ls_string(review, "name")) ||
			g_utf8_strlen(ls_string(review, "quote"), -1) > 400 || g_utf8_strlen(ls_string(review, "name"), -1) > 80 ||
			(ls_string(review, "source") != NULL && g_utf8_strlen(ls_string(review, "source"), -1) > 80) ||
			(ls_string(review, "headline") != NULL && g_utf8_strlen(ls_string(review, "headline"), -1) > 80))
			problem(problems, "reviews[%u] must be {quote up to 400 characters, name up to 80, optional source and "
				"headline up to 80}", i);
	}
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
	if (!ls_object_keys(plan, "|name|tagline|lang|character|missing|tokens|fonts|brand|nav|action|footer|voice|look|"
		"art|photos|media|visual|social|form|contact|modules|credit|booking|demo|pages|css|restyle|ui|structure|signature|"
		"design|reviews|", NULL))
		problem(problems, "unknown top-level key; allowed: name, tagline, lang, character, missing, tokens, fonts, "
			"brand, nav, action, footer, voice, look, art, photos, pages, css, restyle, ui");
	check_missing(problems, plan);
	check_photos(problems, plan);
	{
		g_autoptr(GError) visual_error = NULL;
		if (!ls_visual_check_plan(plan, &visual_error))
			problem(problems, "%s", visual_error->message);
	}
	check_social(problems, self, plan);
	check_supplied_form(problems, plan);
	check_contact(problems, plan);
	check_modules(problems, plan);
	/* credit is the caller's footer line, copied by the builder from site.credit. */
	if (json_object_has_member(plan, "credit") && !ls_credit_valid(member_object(plan, "credit")))
		problem(problems, "credit must be the supplied {text, url}: one line of up to 80 characters and an https:// address");
	/* demo marks an explicit demo build, copied by the builder from site.demo. */
	if (json_object_has_member(plan, "demo") && (!JSON_NODE_HOLDS_VALUE(json_object_get_member(plan, "demo")) ||
		json_node_get_value_type(json_object_get_member(plan, "demo")) != G_TYPE_BOOLEAN))
		problem(problems, "demo must be the supplied true or false");
	/* booking is the caller's page for the "Book online" action, copied from site.booking. */
	if (json_object_has_member(plan, "booking") && !ls_booking_valid(member_object(plan, "booking")))
		problem(problems, "booking must be the supplied {href}: a same-site path such as /book or an https:// address");
	check_structure(problems, self, plan);
	check_look(problems, self, plan);
	check_design(problems, self, plan);
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
		check_nav(problems, &scope, nav);
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
		/* A customer's site never says it is an example; only a demo build does. */
		if (!plan_is_demo(plan) && ls_taste_example(ls_string(footer, "note")) != NULL)
			problem(problems, "footer.note says the business is an example (\"%s\"); this is a customer's site, so "
				"write it as the real business", ls_taste_example(ls_string(footer, "note")));
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
	/* A null css is no css: strict structured output fills omitted fields. */
	if (json_object_has_member(plan, "css") && !JSON_NODE_HOLDS_NULL(json_object_get_member(plan, "css"))) {
		g_autofree gchar *why = is_string(json_object_get_member(plan, "css"))
			? css_problem(ls_string(plan, "css"), 24000) : g_strdup("css must be a string");
		if (why != NULL)
			problem(problems, "%s", why);
	}
	/* signature is the builder's: the checked layer written beside the pages. */
	if (json_object_has_member(plan, "signature")) {
		g_autofree gchar *why = is_string(json_object_get_member(plan, "signature"))
			? signature_problem(ls_string(plan, "signature")) : g_strdup("signature must be a string");
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


/* A preset is a theme with a design block: a source, the sectors it fits,
 * its words, its colour roles, primitives, shape and preferred variants.
 * The design it builds for an empty order must pass ls_design_check(). */
static void
check_preset(Problems *problems, LsComponents *self, JsonObject *theme, const gchar *name){
	JsonObject *design = member_object(theme, "design");
	JsonObject *prefer = member_object(design, "prefer");
	g_autoptr(GList) types = prefer != NULL ? json_object_get_members(prefer) : NULL;
	g_autoptr(JsonObject) built = NULL;
	g_autoptr(GError) local_error = NULL;
	g_autoptr(GString) why = g_string_new(NULL);
	GList *type;
	if (self->library == NULL) {
		problem(problems, "presets need the catalog's library block");
		return;
	}
	if (design == NULL || !ls_object_keys(design, "|source|fits|words|roles|kicker|accent|font_accent|font_label|shape|prefer|dark|",
		NULL) ||
		ls_string(design, "source") == NULL || member_array(design, "fits") == NULL ||
		member_array(design, "words") == NULL || member_object(design, "roles") == NULL || prefer == NULL) {
		problem(problems, "a preset's design needs source, fits, words, roles, shape and prefer");
		return;
	}
	for (type = types; type != NULL; type = type->next) {
		JsonNode *wish = json_object_get_member(prefer, type->data);
		JsonArray *list = JSON_NODE_HOLDS_ARRAY(wish) ? json_node_get_array(wish) : NULL;
		guint i, n = list != NULL ? json_array_get_length(list) : 1;
		for (i = 0; i < n; i++) {
			JsonNode *one = list != NULL ? json_array_get_element(list, i) : wish;
			const gchar *variant = JSON_NODE_HOLDS_VALUE(one) && json_node_get_value_type(one) == G_TYPE_STRING
				? json_node_get_string(one) : "";
			g_autofree gchar *pair = g_strdup_printf("%s/%s", (const gchar *)type->data, variant);
			if (member_object(member_object(self->library, "variants"), pair) == NULL)
				problem(problems, "design.prefer.%s is not a library variant or a list of them", (const gchar *)type->data);
		}
	}
	built = ls_design_new(self->library, self->themes, NULL, name, NULL, &local_error);
	if (built == NULL) {
		problem(problems, "%s", local_error->message);
		return;
	}
	if (!ls_design_check(self->library, built, why))
		problem(problems, "%s", g_strstrip(why->str));
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
		if (theme == NULL || !ls_object_keys(theme, "|summary|tokens|fonts|look|css|restyle|design|", NULL) ||
			ls_string(theme, "summary") == NULL || member_object(theme, "tokens") == NULL ||
			member_object(theme, "look") == NULL) {
			g_ptr_array_unref(problems.messages);
			return catalog_invalid(error, name->data, "themes need a summary, tokens and look");
		}
		axes = json_object_get_members(self->looks);
		/* Structural axes may be left to the builder's defaults. */
		for (axis = axes; axis != NULL; axis = axis->next)
			if (ls_string(member_object(theme, "look"), axis->data) == NULL &&
				!json_object_has_member(member_object(self->looks, axis->data), "builder"))
				problem(&problems, "look.%s is missing", (const gchar *)axis->data);
		plan = ls_json_parse("{\"name\": \"Theme\", \"nav\": [{\"href\": \"index.html\", \"label\": \"Home\"}], "
			"\"art\": [], \"pages\": [{\"path\": \"index.html\", \"kind\": \"home\", \"title\": \"Home\", "
			"\"description\": \"A theme check page of about sixty characters in length.\", \"brief\": \"b\"}]}", NULL);
		copy_theme(theme, json_node_get_object(plan));
		check_plan_into(&problems, self, plan);
		/* A preset's design must build and pass its own checks. */
		if (json_object_has_member(theme, "design"))
			check_preset(&problems, self, theme, name->data);
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
	/* A design belongs to its preset; another theme leaves it behind. */
	if (json_object_has_member(json_node_get_object(plan), "design") &&
		(member_object(json_node_get_object(plan), "design") == NULL ||
		g_strcmp0(ls_string(member_object(json_node_get_object(plan), "design"), "preset"), name) != 0))
		json_object_remove_member(json_node_get_object(plan), "design");
	return TRUE;
}

JsonObject *
ls_components_get_themes(LsComponents *self){
	g_return_val_if_fail(LS_IS_COMPONENTS(self), NULL);
	return self->themes;
}

JsonObject *
ls_components_get_library(LsComponents *self){
	g_return_val_if_fail(LS_IS_COMPONENTS(self), NULL);
	return self->library;
}

/* A meta description of 50 to 160 characters for a page the builder
 * planned itself. */
static gchar *
page_description(JsonObject *plan, JsonObject *entry){
	const gchar *name = ls_string(plan, "name") != NULL ? ls_string(plan, "name") : "us";
	const gchar *kind = ls_string(entry, "kind"), *title = ls_string(entry, "title");
	GString *out = g_string_new(NULL);
	if (g_strcmp0(kind, "detail") == 0)
		g_string_printf(out, "%s from %s: what the work involves, when it is needed and how to arrange it.", title, name);
	else if (g_strcmp0(kind, "services") == 0)
		g_string_printf(out, "Every service %s offers, each with a page of its own and a clear way to ask for it.", name);
	else if (g_strcmp0(kind, "about") == 0)
		g_string_printf(out, "About %s: the business, where it works and how to get in touch.", name);
	else if (g_strcmp0(kind, "visit") == 0)
		g_string_printf(out, "The towns %s serves, its hours and how to arrange a visit.", name);
	else if (g_strcmp0(kind, "contact") == 0)
		g_string_printf(out, "How to reach %s and ask about any of its services.", name);
	else if (g_strcmp0(kind, "form") == 0)
		g_string_printf(out, "Book a time with %s online: choose an open day and time, then send your details.", name);
	else if (g_strcmp0(kind, "general") == 0)
		g_string_printf(out, "Common questions about the services of %s, with plain, general answers.", name);
	else
		g_string_printf(out, "%s: its services, where it works and how to get in touch.", name);
	if (out->len < 50)
		g_string_append(out, " Plain answers and a clear next step.");
	if (g_utf8_strlen(out->str, -1) > 160) {
		gchar *cut = g_utf8_offset_to_pointer(out->str, 157);
		g_string_truncate(out, (gsize)(cut - out->str));
		g_string_append(out, "...");
	}
	return g_string_free(out, FALSE);
}

/* The design's pages become the plan's, in the design's order: a page the
 * writer planned keeps its brief and description, a service page keeps
 * the service's own name, and pages the design does not list are gone.
 * The navigation and the footer are the builder's: the services under one
 * Services menu, then the other pages; the footer lists every page. */
static void
design_pages(JsonObject *plan){
	JsonArray *wanted = member_array(member_object(plan, "design"), "pages");
	JsonArray *planned = member_array(plan, "pages");
	JsonArray *pages, *nav, *children = NULL;
	JsonObject *services = NULL;
	guint i, j;
	if (wanted == NULL)
		return;
	pages = json_array_new();
	nav = json_array_new();
	for (i = 0; i < json_array_get_length(wanted); i++) {
		JsonObject *entry = json_array_get_object_element(wanted, i);
		const gchar *path = ls_string(entry, "path"), *kind = ls_string(entry, "kind");
		JsonObject *mine = NULL, *page = json_object_new();
		for (j = 0; planned != NULL && mine == NULL && j < json_array_get_length(planned); j++) {
			JsonNode *node = json_array_get_element(planned, j);
			if (JSON_NODE_HOLDS_OBJECT(node) && g_strcmp0(ls_string(json_node_get_object(node), "path"), path) == 0)
				mine = json_node_get_object(node);
		}
		json_object_set_string_member(page, "path", path);
		json_object_set_string_member(page, "kind", kind);
		json_object_set_string_member(page, "title", mine != NULL && !blank(ls_string(mine, "title")) &&
			!g_str_equal(kind, "detail") ? ls_string(mine, "title") : ls_string(entry, "title"));
		{
			const gchar *given = mine != NULL ? ls_string(mine, "description") : NULL;
			glong length = given != NULL ? g_utf8_strlen(given, -1) : 0;
			g_autofree gchar *made = length >= 50 && length <= 160 ? NULL : page_description(plan, entry);
			json_object_set_string_member(page, "description", made != NULL ? made : given);
		}
		if (mine != NULL && !blank(ls_string(mine, "brief")))
			json_object_set_string_member(page, "brief", ls_string(mine, "brief"));
		else {
			g_autofree gchar *brief = g_strdup_printf("The %s page: %s.", kind, ls_string(entry, "title"));
			json_object_set_string_member(page, "brief", brief);
		}
		json_array_add_object_element(pages, page);
		/* The navigation: Services with every service under it, then the rest. */
		if (g_str_equal(path, "index.html"))
			continue;
		if (g_str_equal(kind, "detail")) {
			if (children != NULL && json_array_get_length(children) < 8) {
				JsonObject *child = json_object_new();
				json_object_set_string_member(child, "href", path);
				json_object_set_string_member(child, "label", ls_string(entry, "title"));
				json_array_add_object_element(children, child);
			}
			continue;
		}
		{
			JsonObject *link = json_object_new();
			json_object_set_string_member(link, "href", path);
			json_object_set_string_member(link, "label", ls_string(entry, "title"));
			if (g_str_equal(kind, "services")) {
				JsonObject *all = json_object_new();
				services = link;
				children = json_array_new();
				json_object_set_string_member(all, "href", path);
				json_object_set_string_member(all, "label", plan_ui(plan, "all_services"));
				json_array_add_object_element(children, all);
				json_object_set_array_member(link, "children", children);
			}
			json_array_add_object_element(nav, link);
		}
	}
	/* A Services link with no service pages is a plain link. */
	if (services != NULL && json_array_get_length(children) < 2)
		json_object_remove_member(services, "children");
	json_object_set_array_member(plan, "pages", pages);
	json_object_set_array_member(plan, "nav", nav);
	if (member_object(plan, "footer") != NULL && json_object_has_member(member_object(plan, "footer"), "links"))
		json_object_remove_member(member_object(plan, "footer"), "links");
	/* The one primary action is in the header already. */
	if (json_object_has_member(plan, "action"))
		json_object_remove_member(plan, "action");
}

gboolean
ls_components_apply_design(LsComponents *self, JsonNode *plan_node, JsonObject *design){
	JsonObject *plan, *tokens, *roles, *structure, *first;
	JsonArray *home;
	const gchar *map[][2] = {{"paper", "bg"}, {"ink", "fg"}, {"muted", "muted_fg"}, {"surface", "surface"},
		{"line", "line"}, {"accent", "primary"}, {"accent_ink", "primary_fg"}};
	guint i;
	g_return_val_if_fail(LS_IS_COMPONENTS(self), FALSE);
	if (plan_node == NULL || !JSON_NODE_HOLDS_OBJECT(plan_node) || design == NULL)
		return FALSE;
	plan = json_node_get_object(plan_node);
	{
		g_autoptr(JsonNode) holder = json_node_new(JSON_NODE_OBJECT);
		g_autofree gchar *text = NULL;
		json_node_set_object(holder, design);
		text = ls_json_encode(holder);
		json_object_set_member(plan, "design", ls_json_parse(text, NULL));
	}
	roles = member_object(member_object(plan, "design"), "roles");
	tokens = member_object(plan, "tokens");
	if (tokens == NULL) {
		tokens = json_object_new();
		json_object_set_object_member(plan, "tokens", tokens);
	}
	/* The plan's working colours are the design's roles, so every older
	 * component on every page wears the same repaired pairs. */
	for (i = 0; i < G_N_ELEMENTS(map); i++)
		if (ls_string(roles, map[i][1]) != NULL)
			json_object_set_string_member(tokens, map[i][0], ls_string(roles, map[i][1]));
	design_pages(plan);
	home = member_array(member_object(plan, "design"), "home");
	first = home != NULL && json_array_get_length(home) > 0 ? json_array_get_object_element(home, 0) : NULL;
	if (first != NULL && g_strcmp0(ls_string(first, "component"), "hero") == 0) {
		g_autofree gchar *opening = g_strdup_printf("hero/%s", ls_string(first, "variant"));
		structure = member_object(plan, "structure");
		if (structure == NULL) {
			structure = json_object_new();
			json_object_set_object_member(plan, "structure", structure);
		}
		json_object_set_string_member(structure, "opening", opening);
	}
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

/* Recognised social networks for supplied profile links. The file is
 * optional so libraries archived before it existed still load; without it
 * every profile uses the generic link glyph. */
static gboolean
load_networks(LsComponents *self, GChecksum *digest, GError **error){
	g_autofree gchar *text = load_resource(self, digest, "networks.yaml", FALSE, NULL);
	JsonObject *root;
	g_autoptr(GList) names = NULL;
	GList *name;
	if (text == NULL)
		return TRUE;
	if ((self->network_catalog = ls_yaml_parse(text, error)) == NULL)
		return FALSE;
	root = JSON_NODE_HOLDS_OBJECT(self->network_catalog) ? json_node_get_object(self->network_catalog) : NULL;
	if (root == NULL || !ls_object_keys(root, "|version|networks|", NULL) ||
		(self->networks = member_object(root, "networks")) == NULL || member_object(self->networks, "web") == NULL)
		return catalog_invalid(error, "networks.yaml", "needs version and networks, including web");
	names = json_object_get_members(self->networks);
	for (name = names; name != NULL; name = name->next) {
		JsonObject *network = member_object(self->networks, name->data);
		JsonArray *hosts = network != NULL ? member_array(network, "hosts") : NULL;
		const gchar *path = network != NULL ? ls_string(network, "path") : NULL;
		guint i;
		if (network == NULL || !ls_object_keys(network, "|label|hosts|path|", NULL) ||
			ls_string(network, "label") == NULL || hosts == NULL ||
			(path != NULL && strspn(path, "0123456789.-, MmLlHhVvCcSsQqTtAaZz") != strlen(path)))
			return catalog_invalid(error, name->data, "networks need a label, hosts and an optional plain SVG path");
		for (i = 0; i < json_array_get_length(hosts); i++)
			if (!JSON_NODE_HOLDS_VALUE(json_array_get_element(hosts, i)) ||
				json_node_get_value_type(json_array_get_element(hosts, i)) != G_TYPE_STRING)
				return catalog_invalid(error, name->data, "network hosts must be strings");
	}
	return TRUE;
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
	/* The curated pairing library joins the typefaces and is checked with
	 * them; an archived library without it keeps its own. */
	{
		g_autofree gchar *pairings = load_resource(self, digest, "pairings.yaml", FALSE, NULL);
		JsonObject *list;
		if (pairings != NULL) {
			g_autoptr(GList) added = NULL;
			GList *item;
			if ((self->pairing_catalog = ls_yaml_parse(pairings, error)) == NULL)
				return FALSE;
			list = JSON_NODE_HOLDS_OBJECT(self->pairing_catalog)
				? member_object(json_node_get_object(self->pairing_catalog), "pairings") : NULL;
			if (list == NULL || !ls_object_keys(json_node_get_object(self->pairing_catalog), "|version|pairings|", NULL))
				return catalog_invalid(error, "pairings.yaml", "needs version and pairings");
			added = json_object_get_members(list);
			for (item = added; item != NULL; item = item->next) {
				if (json_object_has_member(self->typefaces, item->data))
					return catalog_invalid(error, item->data, "a pairing repeats a typeface name");
				json_object_set_member(self->typefaces, item->data, json_node_copy(json_object_get_member(list, item->data)));
			}
		}
	}
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
	if (member_object(self->palettes, ls_string(self->fallback, "palette")) == NULL ||
		member_object(self->typefaces, ls_string(self->fallback, "typeface")) == NULL ||
		member_object(self->homes, ls_string(self->fallback, "home")) == NULL)
		return catalog_invalid(error, "default", "names a palette, a typeface and a home from identities.yaml");
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

/* ---- Generated palettes ------------------------------------------------------ */

JsonObject *
ls_components_get_default_identity(LsComponents *self){
	JsonObject *identity;
	g_return_val_if_fail(LS_IS_COMPONENTS(self), NULL);
	identity = json_object_new();
	json_object_set_object_member(identity, "palette", named_copy(self->palettes, ls_string(self->fallback, "palette")));
	json_object_set_object_member(identity, "type", named_copy(self->typefaces, ls_string(self->fallback, "typeface")));
	json_object_set_object_member(identity, "home", named_copy(self->homes, ls_string(self->fallback, "home")));
	return identity;
}

/* ---- Clone check ----------------------------------------------------------- */


/* The first family of a CSS font stack, unquoted and lowercased. */
static gchar *
first_family(const gchar *stack){
	const gchar *end;
	g_autofree gchar *family = NULL;
	if (stack == NULL)
		return NULL;
	end = strchr(stack, ',');
	family = g_strndup(stack, end != NULL ? (gsize)(end - stack) : strlen(stack));
	g_strdelimit(family, "'\"", ' ');
	g_strstrip(family);
	return *family != '\0' ? g_ascii_strdown(family, -1) : NULL;
}

/* Hashed declarations of a signature layer that carry its shape: numbers
 * are flattened so a redrawn size still matches, and short generic values
 * such as display: block are skipped because every layer has them. */
static JsonArray *
signature_shingles(const gchar *css){
	JsonArray *out = json_array_new();
	g_autoptr(GHashTable) seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	GString *normal = g_string_new(NULL);
	g_auto(GStrv) pieces = NULL;
	const gchar *p;
	guint i;
	for (p = css != NULL ? css : ""; *p != '\0'; p++) {
		if (p[0] == '/' && p[1] == '*') {
			const gchar *close = strstr(p + 2, "*/");
			if (close == NULL)
				break;
			p = close + 1;
		} else if (g_ascii_isdigit(*p) || (*p == '.' && g_ascii_isdigit(p[1]))) {
			while (g_ascii_isdigit(p[1]) || p[1] == '.')
				p++;
			g_string_append_c(normal, '0');
		} else if (g_ascii_isspace(*p)) {
			if (normal->len > 0 && normal->str[normal->len - 1] != ' ')
				g_string_append_c(normal, ' ');
		} else
			g_string_append_c(normal, g_ascii_tolower(*p));
	}
	pieces = g_strsplit_set(normal->str, ";{}", -1);
	g_string_free(normal, TRUE);
	for (i = 0; pieces[i] != NULL && json_array_get_length(out) < 300; i++) {
		gchar *piece = g_strstrip(pieces[i]);
		const gchar *colon = strchr(piece, ':');
		gchar *hash;
		if (colon == NULL || strlen(colon + 1) < 10 || strchr(piece, '&') != NULL)
			continue;
		hash = g_compute_checksum_for_string(G_CHECKSUM_SHA256, piece, -1);
		hash[12] = '\0';
		if (g_hash_table_add(seen, hash))
			json_array_add_string_element(out, hash);
	}
	return out;
}

/* A fingerprint of a finished plan's look, colours, type and home page. */
static JsonObject *
fingerprint_new(JsonObject *look, JsonObject *colors, JsonObject *type_tokens, const gchar *opening,
	JsonArray *sections){
	JsonObject *fp = json_object_new();
	JsonObject *out_colors = json_object_new(), *fonts = json_object_new();
	JsonArray *home = json_array_new();
	const gchar *hues[] = {"paper", "ink", "accent"};
	guint i;
	json_object_set_int_member(fp, "v", 1);
	if (look != NULL) {
		g_autoptr(GList) axes = json_object_get_members(look);
		GList *axis;
		JsonObject *copy = json_object_new();
		/* The retired stock flourishes are plain on every unthemed site; the
		 * signature layer carries that difference instead. */
		const gchar *const flourishes[] = {"ornament", "emphasis", "kicker", "backdrop", "edges", "texture",
			"links", NULL};
		for (axis = axes; axis != NULL; axis = axis->next)
			if (ls_string(look, axis->data) != NULL && !in_list(flourishes, axis->data))
				json_object_set_string_member(copy, axis->data, ls_string(look, axis->data));
		json_object_set_object_member(fp, "look", copy);
	}
	for (i = 0; colors != NULL && i < G_N_ELEMENTS(hues); i++) {
		gdouble rgb[3];
		if (parse_color(ls_string(colors, hues[i]), rgb))
			json_object_set_string_member(out_colors, hues[i], ls_string(colors, hues[i]));
	}
	json_object_set_object_member(fp, "colors", out_colors);
	if (type_tokens != NULL) {
		g_autofree gchar *display = first_family(ls_string(type_tokens, "font_display"));
		g_autofree gchar *body = first_family(ls_string(type_tokens, "font_body"));
		if (display != NULL)
			json_object_set_string_member(fonts, "display", display);
		if (body != NULL)
			json_object_set_string_member(fonts, "body", body);
	}
	json_object_set_object_member(fp, "fonts", fonts);
	if (opening != NULL)
		json_array_add_string_element(home, opening);
	for (i = 0; sections != NULL && i < json_array_get_length(sections); i++)
		if (JSON_NODE_HOLDS_VALUE(json_array_get_element(sections, i)))
			json_array_add_string_element(home, json_array_get_string_element(sections, i));
	json_object_set_array_member(fp, "home", home);
	return fp;
}

JsonObject *
ls_components_fingerprint(LsComponents *self, JsonNode *plan, JsonNode *home){
	JsonObject *root, *tokens, *fp;
	JsonArray *specs = NULL;
	g_autoptr(JsonArray) rest = json_array_new();
	g_autofree gchar *opening = NULL;
	guint i;
	g_return_val_if_fail(LS_IS_COMPONENTS(self), NULL);
	g_return_val_if_fail(plan != NULL, NULL);
	root = JSON_NODE_HOLDS_OBJECT(plan) ? json_node_get_object(plan) : NULL;
	tokens = member_object(root, "tokens");
	if (home != NULL && JSON_NODE_HOLDS_OBJECT(home))
		specs = member_array(json_node_get_object(home), "sections");
	for (i = 0; specs != NULL && i < json_array_get_length(specs); i++) {
		JsonNode *node = json_array_get_element(specs, i);
		JsonObject *section = JSON_NODE_HOLDS_OBJECT(node) ? json_node_get_object(node) : NULL;
		const gchar *component = ls_string(section, "component");
		if (component == NULL)
			continue;
		if (opening == NULL)
			opening = ls_string(section, "variant") != NULL
				? g_strdup_printf("%s/%s", component, ls_string(section, "variant")) : g_strdup(component);
		else
			json_array_add_string_element(rest, component);
	}
	fp = fingerprint_new(member_object(root, "look"), tokens, tokens, opening, rest);
	/* The preset, the page skeleton and the header, so a nearby order can
	 * avoid them (site.avoid); the footer and motion level for the record. */
	{
		const gchar *keys[] = {"preset", "skeleton", "header", "footer", "motion"};
		guint k;
		for (k = 0; k < G_N_ELEMENTS(keys); k++)
			if (member_object(root, "design") != NULL && ls_string(member_object(root, "design"), keys[k]) != NULL)
				json_object_set_string_member(fp, keys[k], ls_string(member_object(root, "design"), keys[k]));
	}
	if (ls_string(root, "signature") != NULL)
		json_object_set_array_member(fp, "signature", signature_shingles(ls_string(root, "signature")));
	return fp;
}

static gdouble
string_set_overlap(JsonArray *a, JsonArray *b){
	g_autoptr(GHashTable) left = g_hash_table_new(g_str_hash, g_str_equal);
	g_autoptr(GHashTable) both = g_hash_table_new(g_str_hash, g_str_equal);
	guint i, shared = 0, total;
	for (i = 0; a != NULL && i < json_array_get_length(a); i++)
		if (JSON_NODE_HOLDS_VALUE(json_array_get_element(a, i)) && json_array_get_string_element(a, i) != NULL)
			g_hash_table_add(left, (gpointer)json_array_get_string_element(a, i));
	total = g_hash_table_size(left);
	for (i = 0; b != NULL && i < json_array_get_length(b); i++) {
		const gchar *item = JSON_NODE_HOLDS_VALUE(json_array_get_element(b, i))
			? json_array_get_string_element(b, i) : NULL;
		if (item == NULL || !g_hash_table_add(both, (gpointer)item))
			continue;
		if (g_hash_table_contains(left, item))
			shared++;
		else
			total++;
	}
	return total == 0 ? 0 : (gdouble)shared / total;
}

gdouble
ls_components_signature_overlap(JsonObject *a, JsonObject *b){
	JsonArray *sa = member_array(a, "signature"), *sb = member_array(b, "signature");
	g_return_val_if_fail(a != NULL && b != NULL, 0);
	if (sa == NULL || sb == NULL || json_array_get_length(sa) == 0 || json_array_get_length(sb) == 0)
		return 0;
	return string_set_overlap(sa, sb);
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
			"tokens fall back on the house default (warm cream paper with a rust or orange accent); use the "
			"colors in TASK palette");
		return FALSE;
	}
	if (house_default_fonts(tokens) && (typeface == NULL || !house_default_fonts(typeface))) {
		g_set_error_literal(error, LS_ERROR, LS_ERROR_VALIDATION,
			"fonts fall back on the house default (Fraunces over Source Sans); use the type in TASK type");
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
	/* Missing look axes, and values the catalog no longer lists (such as
	 * the numbered section heads an existing site may still wear), take
	 * the catalog default. */
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
			if (!json_object_has_member(look, axis->data) || (ls_string(look, axis->data) != NULL &&
				ls_string(member_object(member_object(self->looks, axis->data), "values"), ls_string(look, axis->data)) == NULL)) {
				json_object_set_string_member(look, axis->data, look_default(self, axis->data));
				g_string_append_printf(notes, "%slook.%s -> %s", notes->len > 0 ? "; " : "",
					(const gchar *)axis->data, look_default(self, axis->data));
			}
	}
	/* A telephone link has one mechanical form: tel: with the digits and a leading +. */
	if (plan != NULL && JSON_NODE_HOLDS_OBJECT(plan)) {
		JsonObject *root = json_node_get_object(plan);
		JsonArray *lists[2];
		guint l;
		lists[0] = member_array(root, "nav");
		lists[1] = member_object(root, "footer") != NULL ? member_array(member_object(root, "footer"), "links") : NULL;
		for (l = 0; l < G_N_ELEMENTS(lists) + 1; l++) {
			JsonArray *list = l < G_N_ELEMENTS(lists) ? lists[l] : NULL;
			guint count = l < G_N_ELEMENTS(lists) ? (list != NULL ? json_array_get_length(list) : 0) : 1;
			for (i = 0; i < count; i++) {
				JsonNode *entry = l < G_N_ELEMENTS(lists) ? json_array_get_element(list, i) : json_object_get_member(root, "action");
				JsonObject *link = entry != NULL && JSON_NODE_HOLDS_OBJECT(entry) ? json_node_get_object(entry) : NULL;
				const gchar *href = link != NULL ? ls_string(link, "href") : NULL;
				if (href != NULL && g_str_has_prefix(href, "tel:") && strpbrk(href + 4, " ()-.") != NULL) {
					GString *clean = g_string_new("tel:");
					const gchar *c;
					for (c = href + 4; *c != '\0'; c++)
						if (g_ascii_isdigit(*c) || (*c == '+' && clean->len == 4))
							g_string_append_c(clean, *c);
					g_string_append_printf(notes, "%s%s -> %s", notes->len > 0 ? "; " : "", href, clean->str);
					json_object_set_string_member(link, "href", clean->str);
					g_string_free(clean, TRUE);
				}
			}
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
	JsonObject *plan;	/* for the supplied photos' own alt text */
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
	const gchar *types[] = {"text", "email", "tel", "url", "number", "date", "time", "textarea", "select", "radio",
		"toggle", "checkbox", "switch", "range", "file", NULL};
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
		if (field == NULL || !ls_object_keys(field, "|name|label|type|required|hint|options|maxlength|autocomplete|min|max|step|",
			NULL)) {
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
			problem(check->problems, "%s.type must be text, email, tel, url, number, date, time, textarea, select, radio, "
				"toggle, checkbox, switch, range or file", here);
		if (json_object_has_member(field, "required") &&
			json_node_get_value_type(json_object_get_member(field, "required")) != G_TYPE_BOOLEAN)
			problem(check->problems, "%s.required must be true or false", here);
		if (!ls_form_field_shape_valid(field))
			problem(check->problems, "%s needs %s", here, ls_form_field_shape_rule());
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
		/* A supplied photo's own alt is the caller's fact; the site check
		 * reports it, the writer cannot change it. */
		else if (image != NULL && ls_taste_disclosure(json_node_get_string(alt), FALSE) != NULL &&
			g_strcmp0(json_node_get_string(alt), ls_string(plan_photo(check->plan, src), "alt")) != 0)
			problem(check->problems, "%s.alt talks about the photo itself (\"%s\"); describe what it shows", where,
				ls_taste_disclosure(json_node_get_string(alt), FALSE));
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
	if (g_str_equal(component, "hours") && member_array(content, "schedule") != NULL &&
		json_array_get_length(member_array(content, "schedule")) > 0) {
		JsonArray *schedule = member_array(content, "schedule");
		const gchar *zone = ls_string(content, "timezone");
		guint i;
		if (zone == NULL || strchr(zone, '/') == NULL ||
			strspn(zone, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789/_+-") != strlen(zone))
			problem(check->problems, "%s.timezone must be an IANA zone such as America/New_York when schedule is set", where);
		if (blank(ls_string(content, "open_label")) || blank(ls_string(content, "closed_label")))
			problem(check->problems, "%s: schedule needs open_label and closed_label", where);
		for (i = 0; i < json_array_get_length(schedule); i++) {
			JsonNode *node = json_array_get_element(schedule, i);
			JsonObject *row = JSON_NODE_HOLDS_OBJECT(node) ? json_node_get_object(node) : NULL;
			const gchar *keys[] = {"opens", "closes"};
			guint k;
			for (k = 0; row != NULL && k < G_N_ELEMENTS(keys); k++) {
				const gchar *t = ls_string(row, keys[k]);
				if (t == NULL || strlen(t) != 5 || !g_ascii_isdigit(t[0]) || !g_ascii_isdigit(t[1]) || t[2] != ':' ||
					!g_ascii_isdigit(t[3]) || !g_ascii_isdigit(t[4]) || (t[0] - '0') * 10 + (t[1] - '0') > 23 ||
					(t[3] - '0') * 10 + (t[4] - '0') > 59)
					problem(check->problems, "%s.schedule[%u].%s must be 24-hour HH:MM", where, i, keys[k]);
			}
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
			g_autofree gchar *why = target != NULL && g_str_has_prefix(target, "https://")
				? href_problem(NULL, target) : NULL;
			gboolean same_site = target != NULL && target[0] == '/' && target[1] != '/' &&
				strstr(target, "..") == NULL && strpbrk(target, " \t\r\n\"'<>\\`#?") == NULL;
			if (target == NULL || (!same_site && (!g_str_has_prefix(target, "https://") || why != NULL)))
				problem(check->problems, "%s.target must be the supplied https:// address or same-site path for endpoint mode", where);
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
	PageCheck check = {problems, &scope, NULL};
	g_autoptr(GPtrArray) ids = NULL;
	g_autoptr(GHashTable) modules_placed = g_hash_table_new(g_str_hash, g_str_equal);
	const gchar *kind_name;
	JsonObject *kind;
	guint i;
	if (plan == NULL || page == NULL) {
		problem(problems, "the page specification must be a JSON object");
		return;
	}
	plan_scope(plan, &scope);
	check.plan = plan;
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
	/* The page headline states what the business does; a question leaves
	 * the visitor to guess. */
	if (!builtin && sections != NULL && json_array_get_length(sections) > 0 &&
		JSON_NODE_HOLDS_OBJECT(json_array_get_element(sections, 0))) {
		JsonObject *opening = member_object(json_array_get_object_element(sections, 0), "content");
		const gchar *heading = ls_string(opening, "heading");
		g_autofree gchar *trimmed = heading != NULL ? g_strstrip(g_strdup(heading)) : NULL;
		if (trimmed != NULL && g_str_has_suffix(trimmed, "?"))
			problem(problems, "section 1 heading \"%s\" is a question; the page headline states what the business "
				"does (and where, when a place is supplied) from the brief's facts", heading);
	}
	for (i = 0; sections != NULL && i < json_array_get_length(sections); i++) {
		JsonNode *node = json_array_get_element(sections, i);
		JsonObject *section = JSON_NODE_HOLDS_OBJECT(node) ? json_node_get_object(node) : NULL;
		const gchar *name = section != NULL ? ls_string(section, "component") : NULL;
		JsonObject *component = name != NULL ? member_object(self->components, name) : NULL;
		JsonObject *content = section != NULL ? member_object(section, "content") : NULL;
		const gchar *variant = section != NULL ? ls_string(section, "variant") : NULL;
		g_autofree gchar *where = g_strdup_printf("section %u (%s)", i + 1, name != NULL ? name : "?");
		g_autoptr(JsonObject) supplied = g_strcmp0(name, "form") == 0 ? supplied_form_content(plan, content) : NULL;
		JsonObject *slots;
		g_autoptr(GList) keys = NULL;
		GList *item;
		if (supplied != NULL)
			content = supplied;
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
		if (i == 0 && kind != NULL && !kind_allows(kind, name) &&
			!(g_str_equal(name, "scene") && g_strcmp0(kind_name, "home") == 0 && member_object(plan, "visual") != NULL)) {
			GString *openings = g_string_new(NULL);
			JsonArray *allowed = member_array(kind, "openings");
			guint j;
			for (j = 0; j < json_array_get_length(allowed); j++)
				g_string_append_printf(openings, "%s%s", j == 0 ? "" : " or ", json_array_get_string_element(allowed, j));
			problem(problems, "%s: %s %s page opens with %s", where,
				strchr("aeiou", kind_name[0]) != NULL ? "an" : "a", kind_name, openings->str);
			g_string_free(openings, TRUE);
		}
		/* Profiles are supplied facts: no profiles, no social section. */
		if (g_str_equal(name, "social") && (plan == NULL || member_array(plan, "social") == NULL ||
			json_array_get_length(member_array(plan, "social")) == 0))
			problem(problems, "%s: social lists the site's supplied profiles, and this site has none; remove it", where);
		/* A module section draws a supplied podbay module, once per page so
		 * its element ids stay unique. */
		if (g_str_equal(name, "module")) {
			const gchar *module = content != NULL ? ls_string(content, "module") : NULL;
			if (plan_module(plan, module) == NULL)
				problem(problems, "%s: module '%s' is not one of the site's supplied podbay modules; use one the plan "
					"lists in modules, or remove the section", where, module != NULL ? module : "");
			else if (!g_hash_table_add(modules_placed, (gpointer)module))
				problem(problems, "%s: module '%s' may appear only once on a page", where, module);
		}
		/* So is the phone: no phone, no call section. */
		if (g_str_equal(name, "call")) {
			g_autoptr(JsonObject) call = call_view(plan);
			if (call == NULL)
				problem(problems, "%s: call shows the site's supplied phone number, and this site has none; remove it", where);
		}
		if (g_str_equal(name, "booking-calendar") &&
			(plan == NULL || !ls_booking_valid(member_object(plan, "booking"))))
			problem(problems, "%s: booking-calendar needs the site's supplied booking page", where);
		if ((g_str_equal(name, "hero") || g_str_equal(name, "cover")) && g_strcmp0(kind_name, "home") != 0)
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

#define HOME_QUESTIONS 10
#define BARE_HOME_QUESTIONS 12

/* Sentences in @text: full stops, question and exclamation marks that end
 * the text or come before a space. */
static guint
count_sentences(const gchar *text){
	const gchar *p;
	guint count = 0;
	for (p = text != NULL ? text : ""; *p != '\0'; p++)
		if ((*p == '.' || *p == '!' || *p == '?') && (p[1] == '\0' || g_ascii_isspace(p[1])))
			count++;
	return count;
}

/* Whether the designed service page titled @name has no supplied description. */
static gboolean
undescribed(JsonObject *plan, const gchar *name){
	JsonArray *designed = member_array(member_object(plan, "design"), "pages");
	guint i;
	for (i = 0; name != NULL && designed != NULL && i < json_array_get_length(designed); i++) {
		JsonObject *entry = json_array_get_object_element(designed, i);
		if (ls_string(entry, "offer") != NULL && g_ascii_strcasecmp(ls_string(entry, "title"), name) == 0)
			return !json_object_has_member(entry, "described");
	}
	return FALSE;
}

guint
ls_components_home_questions(JsonNode *plan_node){
	JsonObject *plan = plan_node != NULL && JSON_NODE_HOLDS_OBJECT(plan_node) ? json_node_get_object(plan_node) : NULL;
	JsonArray *designed = member_array(member_object(plan, "design"), "home");
	const gchar *factual[] = {"facts", "steps", "about", "areas", "gallery", "reviews"};
	guint i, k;
	if (designed == NULL || member_array(member_object(plan, "design"), "pages") == NULL)
		return 0;
	/* A home with no facts of the order's own leans on its questions. */
	for (i = 0; i < json_array_get_length(designed); i++)
		for (k = 0; k < G_N_ELEMENTS(factual); k++)
			if (g_strcmp0(ls_string(json_array_get_object_element(designed, i), "component"), factual[k]) == 0)
				return HOME_QUESTIONS;
	return BARE_HOME_QUESTIONS;
}

/* A designed home carries the depth a writer can give honestly: six or
 * more questions answered from facts and general truths, and a line for
 * every service or service group. */
static void
check_home_depth(Problems *problems, JsonNode *plan_node, const gchar *path, JsonNode *page){
	JsonObject *plan = plan_node != NULL && JSON_NODE_HOLDS_OBJECT(plan_node) ? json_node_get_object(plan_node) : NULL;
	JsonArray *designed = member_array(member_object(plan, "design"), "home");
	JsonArray *sections = page != NULL && JSON_NODE_HOLDS_OBJECT(page)
		? member_array(json_node_get_object(page), "sections") : NULL;
	gboolean wants_faq = FALSE, has_faq = FALSE;
	guint i, j, least = ls_components_home_questions(plan_node);
	if (designed == NULL || sections == NULL || g_strcmp0(path, "index.html") != 0 ||
		member_array(member_object(plan, "design"), "pages") == NULL)
		return;
	for (i = 0; i < json_array_get_length(designed); i++)
		wants_faq |= g_strcmp0(ls_string(json_array_get_object_element(designed, i), "component"), "faq") == 0;
	for (i = 0; i < json_array_get_length(sections); i++) {
		JsonObject *section = JSON_NODE_HOLDS_OBJECT(json_array_get_element(sections, i))
			? json_array_get_object_element(sections, i) : NULL;
		JsonObject *content = member_object(section, "content");
		JsonArray *items = member_array(content, "items");
		const gchar *name = ls_string(section, "component");
		if (g_strcmp0(name, "faq") == 0) {
			has_faq = TRUE;
			/* A home with no facts of the order's own leans on its questions. */
			if (items == NULL || json_array_get_length(items) < least)
				problem(problems, "section %u (faq) needs %u or more questions a visitor would ask about these "
					"services (it has %u), each answered in two to four sentences from supplied facts and general, "
					"true information about the work", i + 1, least, items != NULL ? json_array_get_length(items) : 0);
		}
		if (g_strcmp0(name, "services") == 0) {
			GString *bare = g_string_new(NULL);
			for (j = 0; items != NULL && j < json_array_get_length(items); j++) {
				JsonObject *item = json_array_get_object_element(items, j);
				if (blank(ls_string(item, "summary")) || (undescribed(plan, ls_string(item, "name")) &&
					count_sentences(ls_string(item, "summary")) < 3))
					g_string_append_printf(bare, "%s%s", bare->len > 0 ? ", " : "",
						ls_string(item, "name") != NULL ? ls_string(item, "name") : "(unnamed)");
			}
			if (bare->len > 0)
				problem(problems, "section %u (services) needs a summary for %s: the supplied description, or, "
					"when none was supplied, three general, true sentences on what that service covers, who "
					"it suits and when people book it", i + 1, bare->str);
			g_string_free(bare, TRUE);
		}
	}
	if (wants_faq && !has_faq)
		problem(problems, "the home page needs its faq section with six to eight questions");
}

#define SERVICE_QUESTIONS 3

/* The first string under @node that @filler matches, or NULL. */
static const gchar *
filler_in(JsonNode *node, GRegex *filler){
	if (node == NULL)
		return NULL;
	if (JSON_NODE_HOLDS_VALUE(node) && json_node_get_value_type(node) == G_TYPE_STRING)
		return g_regex_match(filler, json_node_get_string(node), 0, NULL) ? json_node_get_string(node) : NULL;
	if (JSON_NODE_HOLDS_ARRAY(node)) {
		JsonArray *array = json_node_get_array(node);
		guint i;
		for (i = 0; i < json_array_get_length(array); i++) {
			const gchar *hit = filler_in(json_array_get_element(array, i), filler);
			if (hit != NULL)
				return hit;
		}
	} else if (JSON_NODE_HOLDS_OBJECT(node)) {
		g_autoptr(GList) keys = json_object_get_members(json_node_get_object(node));
		GList *key;
		for (key = keys; key != NULL; key = key->next) {
			const gchar *hit = filler_in(json_object_get_member(json_node_get_object(node), key->data), filler);
			if (hit != NULL)
				return hit;
		}
	}
	return NULL;
}

/* On a preset site the lines the site gate bans as filler ("one of the
 * four services", "ask us about a ...") are refused before publication. */
static void
check_filler(Problems *problems, JsonNode *plan_node, JsonNode *page){
	JsonObject *plan = plan_node != NULL && JSON_NODE_HOLDS_OBJECT(plan_node) ? json_node_get_object(plan_node) : NULL;
	g_autoptr(GRegex) filler = NULL;
	const gchar *hit;
	if (member_object(plan, "design") == NULL || page == NULL)
		return;
	filler = g_regex_new("example business|for demonstration|\\bends here\\b|\\bone of the (?:\\d+|[a-z]+) services\\b|"
		"\\bask us about an? [a-z]", G_REGEX_CASELESS, 0, NULL);
	hit = filler_in(page, filler);
	if (hit != NULL)
		problem(problems, "\"%.120s\" is filler; say something specific and true about the service instead", hit);
}

/* On a preset site a heading that promises a process ("how it works",
 * "how a job goes", "our process") heads a list of two or more steps, as
 * the site gate asks of every published page. */
static void
check_promised_steps(LsComponents *self, Problems *problems, JsonNode *plan_node, JsonNode *page){
	JsonObject *plan = plan_node != NULL && JSON_NODE_HOLDS_OBJECT(plan_node) ? json_node_get_object(plan_node) : NULL;
	JsonArray *sections = page != NULL && JSON_NODE_HOLDS_OBJECT(page)
		? member_array(json_node_get_object(page), "sections") : NULL;
	g_autoptr(GRegex) promise = NULL;
	guint i;
	if (member_object(plan, "design") == NULL || sections == NULL)
		return;
	promise = g_regex_new("\\bhow\\b.*\\b(?:works?|runs?|goes)\\b|\\bprocess\\b|\\bstep by step\\b",
		G_REGEX_CASELESS, 0, NULL);
	for (i = 0; i < json_array_get_length(sections); i++) {
		JsonObject *section = JSON_NODE_HOLDS_OBJECT(json_array_get_element(sections, i))
			? json_array_get_object_element(sections, i) : NULL;
		JsonObject *content = member_object(section, "content");
		JsonArray *items = member_array(content, "items");
		const gchar *heading = ls_string(content, "heading");
		const gchar *component = ls_string(section, "component"), *variant = ls_string(section, "variant");
		g_autofree gchar *own = g_strdup_printf("%s--%s.html", component != NULL ? component : "", variant != NULL ? variant : "");
		g_autofree gchar *base = g_strconcat(component != NULL ? component : "", ".html", NULL);
		const gchar *markup = file_text(self, own) != NULL ? file_text(self, own) : file_text(self, base);
		/* Steps count only where the layout lists its items; questions and
		 * label-and-value rows are not steps, as the gate counts listed
		 * steps only. */
		gboolean listed = markup != NULL && strstr(markup, "<li") != NULL && g_strcmp0(component, "faq") != 0;
		if (heading == NULL || !g_regex_match(promise, heading, 0, NULL) ||
			(items != NULL && json_array_get_length(items) >= 2 && listed))
			continue;
		problem(problems, "section %u heading \"%s\" promises a process, so the section lists two or more steps "
			"(use the steps component), or the heading says what the section holds instead", i + 1, heading);
	}
}

/* On a preset site a heading that promises opening hours gives them in its
 * own section, as the site gate asks of a published page. */
static void
check_promised_hours(Problems *problems, JsonNode *plan_node, JsonNode *page){
	JsonObject *plan = plan_node != NULL && JSON_NODE_HOLDS_OBJECT(plan_node) ? json_node_get_object(plan_node) : NULL;
	JsonArray *sections = page != NULL && JSON_NODE_HOLDS_OBJECT(page)
		? member_array(json_node_get_object(page), "sections") : NULL;
	g_autoptr(GRegex) promise = NULL, given = NULL;
	guint i;
	if (member_object(plan, "design") == NULL || sections == NULL)
		return;
	promise = g_regex_new("\\bhours\\b|\\bopening times\\b|\\bwhen we(?: are|'re) open\\b", G_REGEX_CASELESS, 0, NULL);
	given = g_regex_new("\\b\\d{1,2}(?::\\d{2})?\\s*(?:am|pm|a\\.m\\.|p\\.m\\.)|\\b\\d{1,2}:\\d{2}\\b|24/7|24 hours|\\bclosed\\b",
		G_REGEX_CASELESS, 0, NULL);
	for (i = 0; i < json_array_get_length(sections); i++) {
		JsonNode *node = json_array_get_element(sections, i);
		JsonObject *section = JSON_NODE_HOLDS_OBJECT(node) ? json_node_get_object(node) : NULL;
		const gchar *heading = ls_string(member_object(section, "content"), "heading");
		g_autofree gchar *text = NULL;
		if (heading == NULL || !g_regex_match(promise, heading, 0, NULL))
			continue;
		text = ls_json_encode(json_object_get_member(section, "content"));
		if (!g_regex_match(given, text, 0, NULL))
			problem(problems, "section %u heading \"%s\" promises opening hours, so the section gives the supplied "
				"hours, or the heading says what the section holds instead", i + 1, heading);
	}
}

/* A service page explains its one service: a text or article on what the
 * work involves and three or more questions. */
static void
check_service_depth(Problems *problems, JsonNode *plan_node, const gchar *path, JsonNode *page){
	JsonObject *plan = plan_node != NULL && JSON_NODE_HOLDS_OBJECT(plan_node) ? json_node_get_object(plan_node) : NULL;
	JsonArray *designed = member_array(member_object(plan, "design"), "pages");
	JsonArray *sections = page != NULL && JSON_NODE_HOLDS_OBJECT(page)
		? member_array(json_node_get_object(page), "sections") : NULL;
	const gchar *title = NULL;
	gboolean explained = FALSE;
	guint i, questions = 0;
	for (i = 0; designed != NULL && i < json_array_get_length(designed); i++) {
		JsonObject *entry = json_array_get_object_element(designed, i);
		if (g_strcmp0(ls_string(entry, "path"), path) == 0 && ls_string(entry, "offer") != NULL)
			title = ls_string(entry, "title");
	}
	if (title == NULL)
		return;
	for (i = 0; sections != NULL && i < json_array_get_length(sections); i++) {
		JsonObject *section = JSON_NODE_HOLDS_OBJECT(json_array_get_element(sections, i))
			? json_array_get_object_element(sections, i) : NULL;
		JsonObject *content = member_object(section, "content");
		const gchar *name = ls_string(section, "component");
		if ((g_strcmp0(name, "text") == 0 && !blank(ls_string(content, "body"))) ||
			(g_strcmp0(name, "article") == 0 && member_array(content, "parts") != NULL))
			explained = TRUE;
		if (g_strcmp0(name, "faq") == 0 && member_array(content, "items") != NULL)
			questions += json_array_get_length(member_array(content, "items"));
	}
	if (!explained)
		problem(problems, "this service page needs a text section on what %s involves (its heading in sentence "
			"case), of two or three short paragraphs of general, true information about that kind of work", title);
	if (questions < SERVICE_QUESTIONS)
		problem(problems, "this service page needs a section with component faq holding three or four questions "
			"about %s (it has %u), answered from supplied facts and general truths", title, questions);
}

gboolean
ls_components_check_page(LsComponents *self, JsonNode *plan, const gchar *path, JsonNode *page, GError **error){
	Problems problems = {g_ptr_array_new_with_free_func(g_free)};
	g_autofree gchar *subject = NULL;
	gboolean okay;
	g_return_val_if_fail(LS_IS_COMPONENTS(self) && plan != NULL && path != NULL, FALSE);
	check_page_into(self, &problems, plan, path, page, FALSE);
	check_home_depth(&problems, plan, path, page);
	check_service_depth(&problems, plan, path, page);
	check_promised_steps(self, &problems, plan, page);
	check_promised_hours(&problems, plan, page);
	check_filler(&problems, plan, page);
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
	const gchar *base;		/* normalized https:// site address, or NULL */
	const gchar *quote;		/* the live form on its page, for the quote action, or NULL */
	const gchar *book;		/* the supplied booking page, for the book action, or NULL */
	GHashTable *modules;		/* supplied podbay module ids drawn anywhere */
} Render;

static void derived_filter(Render *render, JsonObject *view, const gchar *field);
static gboolean design_overlay(JsonObject *plan, const gchar *name);
static gchar *quote_form_html(Render *render, const gchar *id, const gchar *labelledby, gboolean quiet);
static JsonObject *badge_view(Render *render);

static const gchar *
ui_text(Render *render, const gchar *key){
	return ls_string(render->ui, key);
}

/* The supplied photograph at @src, or %NULL. */
static JsonObject *
plan_photo(JsonObject *plan, const gchar *src){
	JsonArray *photos = member_array(plan, "photos");
	guint i;
	for (i = 0; photos != NULL && i < json_array_get_length(photos); i++) {
		JsonObject *photo = json_array_get_object_element(photos, i);
		if (g_strcmp0(ls_string(photo, "path"), src) == 0)
			return photo;
	}
	return NULL;
}

/* One image slot of a page showing a supplied photo: the {src, alt}
 * object, the section it sits in, the items element holding it (with its
 * array) or else the section content member it is, and the order it
 * claims a photo in. */
typedef struct {
	JsonObject *image;
	guint section;
	JsonArray *items;
	JsonObject *item;
	const gchar *member;
	guint rank;
	guint order;
	gboolean dead;
} PhotoSlot;

/* Sections whose image is the whole point: they give up their photo
 * first, and go when they cannot keep one. */
static const gchar *const photo_only_components[] = {"media", "gallery", "carousel", "strip", "figure",
	"showcase", "compare", NULL};

static gint
photo_slot_order(gconstpointer a, gconstpointer b){
	const PhotoSlot *x = *(PhotoSlot *const *)a, *y = *(PhotoSlot *const *)b;
	if (x->rank != y->rank)
		return x->rank < y->rank ? -1 : 1;
	return x->order < y->order ? -1 : x->order > y->order;
}

/* Gathers the photo slots of section @index's @content: its own image
 * members and those of its items. A scene's phone crop of its own photo
 * is the same picture, not a second slot. */
static void
collect_photo_slots(JsonObject *plan, JsonObject *content, guint index, guint rank, GPtrArray *slots){
	g_autoptr(GList) members = json_object_get_members(content);
	GList *m;
	for (m = members; m != NULL; m = m->next) {
		JsonNode *node = json_object_get_member(content, m->data);
		if (JSON_NODE_HOLDS_OBJECT(node) && plan_photo(plan, ls_string(json_node_get_object(node), "src")) != NULL) {
			PhotoSlot *slot;
			if (g_str_equal(m->data, "portrait") && member_object(content, "art") != NULL &&
				g_strcmp0(ls_string(json_node_get_object(node), "src"), ls_string(member_object(content, "art"), "src")) == 0)
				continue;
			slot = g_new0(PhotoSlot, 1);
			slot->image = json_node_get_object(node);
			slot->section = index;
			slot->member = m->data;
			slot->rank = rank;
			slot->order = slots->len;
			g_ptr_array_add(slots, slot);
		} else if (JSON_NODE_HOLDS_ARRAY(node)) {
			JsonArray *items = json_node_get_array(node);
			guint i;
			for (i = 0; i < json_array_get_length(items); i++) {
				JsonNode *element = json_array_get_element(items, i);
				g_autoptr(GList) fields = NULL;
				GList *f;
				if (!JSON_NODE_HOLDS_OBJECT(element))
					continue;
				fields = json_object_get_members(json_node_get_object(element));
				for (f = fields; f != NULL; f = f->next) {
					JsonNode *field = json_object_get_member(json_node_get_object(element), f->data);
					PhotoSlot *slot;
					if (!JSON_NODE_HOLDS_OBJECT(field) ||
						plan_photo(plan, ls_string(json_node_get_object(field), "src")) == NULL)
						continue;
					slot = g_new0(PhotoSlot, 1);
					slot->image = json_node_get_object(field);
					slot->section = index;
					slot->items = items;
					slot->item = json_node_get_object(element);
					slot->rank = rank;
					slot->order = slots->len;
					g_ptr_array_add(slots, slot);
				}
			}
		}
	}
}

/* Whether @content, after its repeats went, still has what @component
 * needs: every required slot present and every items list at its
 * minimum. */
static gboolean
section_stands(LsComponents *self, const gchar *component, const gchar *variant, JsonObject *content){
	JsonObject *slots = member_object(member_object(self->components, component), "slots");
	g_autoptr(GList) names = slots != NULL ? json_object_get_members(slots) : NULL;
	GList *name;
	for (name = names; name != NULL; name = name->next) {
		JsonObject *slot = member_object(slots, name->data);
		JsonNode *value = json_object_get_member(content, name->data);
		if (json_object_has_member(slot, "required") && json_object_get_boolean_member(slot, "required") && value == NULL)
			return FALSE;
		if (g_strcmp0(ls_string(slot, "type"), "items") == 0 && value != NULL && JSON_NODE_HOLDS_ARRAY(value) &&
			json_object_has_member(slot, "min_items") &&
			json_array_get_length(json_node_get_array(value)) < (guint)json_object_get_int_member(slot, "min_items"))
			return FALSE;
	}
	/* Split and banner heroes and split page headers show their picture. */
	if (json_object_get_member(content, "art") == NULL && (g_strcmp0(variant, "split") == 0 ||
		g_strcmp0(variant, "banner") == 0) && (g_strcmp0(component, "hero") == 0 || g_strcmp0(component, "page-header") == 0))
		return FALSE;
	return TRUE;
}

/* Where @route stands in @plan's pages (0 for the first or an unknown one). */
static guint
page_place(JsonObject *plan, const gchar *route){
	JsonArray *pages = member_array(plan, "pages");
	guint i;
	for (i = 0; route != NULL && pages != NULL && i < json_array_get_length(pages); i++)
		if (JSON_NODE_HOLDS_OBJECT(json_array_get_element(pages, i)) &&
			g_strcmp0(ls_string(json_array_get_object_element(pages, i), "path"), route) == 0)
			return i;
	return 0;
}

gboolean
ls_components_spread_photos(LsComponents *self, JsonNode *plan_node, JsonNode *page){
	g_autoptr(GHashTable) shown = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	g_autoptr(GHashTable) wanted = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	g_autoptr(GHashTable) sequence = g_hash_table_new(g_str_hash, g_str_equal);
	g_autoptr(GPtrArray) spares = g_ptr_array_new();
	guint place;
	g_autoptr(GPtrArray) slots = g_ptr_array_new_with_free_func(g_free);
	g_autoptr(GArray) dropped = NULL;
	JsonObject *plan;
	JsonArray *photos, *sections;
	gboolean changed = FALSE;
	guint i, j;
	g_return_val_if_fail(LS_IS_COMPONENTS(self), FALSE);
	if (plan_node == NULL || !JSON_NODE_HOLDS_OBJECT(plan_node) || page == NULL || !JSON_NODE_HOLDS_OBJECT(page))
		return FALSE;
	plan = json_node_get_object(plan_node);
	photos = member_array(plan, "photos");
	sections = member_array(json_node_get_object(page), "sections");
	if (photos == NULL || json_array_get_length(photos) == 0 || sections == NULL)
		return FALSE;
	for (i = 0; i < json_array_get_length(sections); i++) {
		JsonObject *section = JSON_NODE_HOLDS_OBJECT(json_array_get_element(sections, i))
			? json_array_get_object_element(sections, i) : NULL;
		JsonObject *content = section != NULL ? member_object(section, "content") : NULL;
		const gchar *name = section != NULL ? ls_string(section, "component") : NULL;
		if (content != NULL)
			collect_photo_slots(plan, content, i, i == 0 ? 0 : in_list(photo_only_components, name) ? 2 : 1, slots);
	}
	g_ptr_array_sort(slots, photo_slot_order);
	/* A page without the visual sequence's opening (an inner page) would
	 * rather not repeat the sequence's photos either: it takes a spare
	 * one while any is left, and keeps its own otherwise. */
	{
		JsonObject *media = member_object(plan, "media");
		const gchar *roles[] = {"hero", "portrait", "detail", "proof"};
		gboolean opening = FALSE;
		for (i = 0; i < json_array_get_length(sections); i++)
			opening |= JSON_NODE_HOLDS_OBJECT(json_array_get_element(sections, i)) &&
				g_strcmp0(ls_string(json_array_get_object_element(sections, i), "component"), "scene") == 0;
		for (i = 0; media != NULL && !opening && member_object(plan, "visual") != NULL && i < G_N_ELEMENTS(roles); i++)
			if (ls_string(media, roles[i]) != NULL)
				g_hash_table_add(sequence, (gpointer)ls_string(media, roles[i]));
	}
	/* The photos a repeat may take, outside the sequence, tried from the
	 * page's place in the plan, so neighbouring inner pages start on
	 * different ones. */
	for (i = 0; i < json_array_get_length(photos); i++)
		if (!g_hash_table_contains(sequence, ls_string(json_array_get_object_element(photos, i), "path")))
			g_ptr_array_add(spares, json_array_get_object_element(photos, i));
	place = page_place(plan, ls_string(json_node_get_object(page), "path"));
	/* Claim in turn; a repeat takes a photo nobody has shown, one no later
	 * slot asks for if there is one. */
	for (i = 0; i < slots->len; i++)
		g_hash_table_add(wanted, g_strdup(ls_string(((PhotoSlot *)g_ptr_array_index(slots, i))->image, "src")));
	for (i = 0; i < slots->len; i++) {
		PhotoSlot *slot = g_ptr_array_index(slots, i);
		const gchar *src = ls_string(slot->image, "src");
		JsonObject *fresh = NULL;
		if (!g_hash_table_contains(shown, src) && !g_hash_table_contains(sequence, src)) {
			g_hash_table_add(shown, g_strdup(src));
			continue;
		}
		for (j = 0; j < spares->len; j++) {
			JsonObject *photo = g_ptr_array_index(spares, (place + j) % spares->len);
			const gchar *path = ls_string(photo, "path");
			if (path == NULL || g_hash_table_contains(shown, path))
				continue;
			if (!g_hash_table_contains(wanted, path)) {
				fresh = photo;
				break;
			}
			if (fresh == NULL)
				fresh = photo;
		}
		if (fresh == NULL && !g_hash_table_contains(shown, src)) {
			g_hash_table_add(shown, g_strdup(src));
			continue;
		}
		changed = TRUE;
		if (fresh == NULL) {
			slot->dead = TRUE;
			continue;
		}
		json_object_set_string_member(slot->image, "src", ls_string(fresh, "path"));
		json_object_set_string_member(slot->image, "alt", ls_string(fresh, "alt") != NULL ? ls_string(fresh, "alt") : "");
		g_hash_table_add(shown, g_strdup(ls_string(fresh, "path")));
	}
	/* Repeats with no photo left go: their item, else the optional image.
	 * A section that cannot stand without them goes as well. */
	dropped = g_array_new(FALSE, TRUE, sizeof(gboolean));
	g_array_set_size(dropped, json_array_get_length(sections));
	for (i = 0; i < slots->len; i++) {
		PhotoSlot *slot = g_ptr_array_index(slots, i);
		if (!slot->dead)
			continue;
		if (slot->items != NULL) {
			for (j = 0; j < json_array_get_length(slot->items); j++)
				if (JSON_NODE_HOLDS_OBJECT(json_array_get_element(slot->items, j)) &&
					json_array_get_object_element(slot->items, j) == slot->item) {
					json_array_remove_element(slot->items, j);
					break;
				}
		} else if (slot->member != NULL) {
			json_object_remove_member(member_object(json_array_get_object_element(sections, slot->section), "content"),
				slot->member);
		}
	}
	for (i = 1; i < json_array_get_length(sections); i++) {
		JsonObject *section = JSON_NODE_HOLDS_OBJECT(json_array_get_element(sections, i))
			? json_array_get_object_element(sections, i) : NULL;
		gboolean touched = FALSE;
		for (j = 0; j < slots->len; j++)
			touched |= ((PhotoSlot *)g_ptr_array_index(slots, j))->dead && ((PhotoSlot *)g_ptr_array_index(slots, j))->section == i;
		if (touched && section != NULL && member_object(section, "content") != NULL &&
			!section_stands(self, ls_string(section, "component"), ls_string(section, "variant"), member_object(section, "content")))
			g_array_index(dropped, gboolean, i) = TRUE;
	}
	for (i = json_array_get_length(sections); i > 1; i--)
		if (g_array_index(dropped, gboolean, i - 1))
			json_array_remove_element(sections, i - 1);
	return changed;
}

/* The file a page shows for @src: a sized photo's largest variant, otherwise
 * @src itself. Pages, link previews and focus rules all use this name. */
static const gchar *
shown_src(JsonObject *plan, const gchar *src){
	JsonObject *photo = plan_photo(plan, src);
	JsonArray *variants = photo != NULL ? member_array(photo, "variants") : NULL;
	if (variants == NULL || json_array_get_length(variants) == 0)
		return src;
	return ls_string(json_array_get_object_element(variants, json_array_get_length(variants) - 1), "path");
}

/* "path 480w, path 800w, ..." for a sized photo, or %NULL. */
static gchar *
photo_srcset(JsonObject *plan, const gchar *src){
	JsonObject *photo = plan_photo(plan, src);
	JsonArray *variants = photo != NULL ? member_array(photo, "variants") : NULL;
	GString *out;
	guint i;
	if (variants == NULL || json_array_get_length(variants) == 0)
		return NULL;
	out = g_string_new(NULL);
	for (i = 0; i < json_array_get_length(variants); i++) {
		JsonObject *variant = json_array_get_object_element(variants, i);
		g_string_append_printf(out, "%s%s %" G_GINT64_FORMAT "w", i > 0 ? ", " : "", ls_string(variant, "path"),
			json_object_get_int_member(variant, "width"));
	}
	return g_string_free(out, FALSE);
}

static void
art_size(JsonObject *plan, const gchar *src, gint64 *width, gint64 *height){
	JsonArray *art = member_array(plan, "art");
	JsonObject *photo = plan_photo(plan, src);
	const gchar *ratio = "16:9";
	guint i;
	/* A photograph keeps its own (capped) pixel size. */
	if (photo != NULL) {
		*width = json_object_get_int_member(photo, "width");
		*height = json_object_get_int_member(photo, "height");
		return;
	}
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

static gchar *link_key(const gchar *href);

/* The site's one primary action, as the action bar draws it: the supplied
 * booking page, else the supplied phone, else the live form; NULL without
 * any. Other buttons are primary only when they lead to the same place. */
/* A string member of the plan's design, or NULL without a design. */
static const gchar *
design_string(JsonObject *plan, const gchar *key){
	JsonObject *design = plan != NULL ? member_object(plan, "design") : NULL;
	return design != NULL ? ls_string(design, key) : NULL;
}

/* The first planned page of the contact kind, or NULL. */
static const gchar *
contact_route(JsonObject *plan){
	JsonArray *pages = member_array(plan, "pages");
	guint i;
	for (i = 0; pages != NULL && i < json_array_get_length(pages); i++) {
		JsonObject *entry = json_array_get_object_element(pages, i);
		if (g_strcmp0(ls_string(entry, "kind"), "contact") == 0)
			return ls_string(entry, "path");
	}
	return NULL;
}

/* A design's one primary action as {href, label}: the call, the booking
 * page or the quote page, as design.action says; NULL without a design or
 * without what the action needs. */
static JsonObject *
design_primary(Render *render){
	JsonObject *design = member_object(render->plan, "design");
	const gchar *action = design_string(render->plan, "action");
	g_autoptr(JsonObject) call = NULL;
	JsonObject *link;
	if (design == NULL || action == NULL)
		return NULL;
	call = call_view(render->plan);
	link = json_object_new();
	if (g_str_equal(action, "call") && call != NULL) {
		g_autofree gchar *label = g_strdup_printf("%s %s", ui_text(render, "call"), ls_string(call, "phone"));
		json_object_set_string_member(link, "href", ls_string(call, "tel"));
		json_object_set_string_member(link, "label", label);
	} else if (g_str_equal(action, "book") && render->book != NULL) {
		json_object_set_string_member(link, "href", render->book);
		json_object_set_string_member(link, "label", ui_text(render, "book"));
	} else if (render->quote != NULL || contact_route(render->plan) != NULL) {
		json_object_set_string_member(link, "href", render->quote != NULL ? render->quote : contact_route(render->plan));
		json_object_set_string_member(link, "label", ui_text(render, "quote"));
	} else {
		json_object_unref(link);
		return NULL;
	}
	return link;
}

/* The quiet second action beside a call: the quote page, as an outline. */
static JsonObject *
design_secondary(Render *render){
	JsonObject *design = member_object(render->plan, "design");
	const gchar *href = render->quote != NULL ? render->quote : contact_route(render->plan);
	g_autoptr(JsonObject) call = call_view(render->plan);
	JsonObject *link;
	if (design == NULL || g_strcmp0(design_string(render->plan, "action"), "call") != 0 || call == NULL || href == NULL ||
		g_strcmp0(href, render->page_path) == 0)
		return NULL;
	link = json_object_new();
	json_object_set_string_member(link, "href", href);
	json_object_set_string_member(link, "label", ui_text(render, "quote"));
	return link;
}

/* The supplied photos' credit line: every photo with a credit, stock
 * photos named as stock so none passes for the owner's own work. */
static gchar *
photo_credits(JsonObject *plan){
	JsonArray *photos = member_array(plan, "photos");
	GString *stock = g_string_new(NULL), *own = g_string_new(NULL);
	gchar *out;
	guint i;
	for (i = 0; photos != NULL && i < json_array_get_length(photos); i++) {
		JsonObject *photo = json_array_get_object_element(photos, i);
		JsonNode *flag = json_object_get_member(photo, "stock");
		GString *into = flag != NULL && JSON_NODE_HOLDS_VALUE(flag) && json_node_get_boolean(flag) ? stock : own;
		if (blank(ls_string(photo, "credit")))
			continue;
		g_string_append_printf(into, "%s%s", into->len > 0 ? "; " : "", ls_string(photo, "credit"));
	}
	if (stock->len == 0 && own->len == 0)
		out = NULL;
	else if (own->len == 0)
		out = g_strdup_printf("Stock photos, not of this business: %s.", stock->str);
	else if (stock->len == 0)
		out = g_strdup_printf("Photos: %s.", own->str);
	else
		out = g_strdup_printf("Photos: %s. Stock photos, not of this business: %s.", own->str, stock->str);
	g_string_free(stock, TRUE);
	g_string_free(own, TRUE);
	return out;
}

static gchar *
supplied_action(Render *render){
	JsonObject *contact = member_object(render->plan, "contact");
	const gchar *phone = contact != NULL ? ls_string(contact, "phone") : NULL;
	g_autofree gchar *tel = phone != NULL ? ls_phone_tel(phone) : NULL;
	/* A design names the action that fits the trade. */
	{
		g_autoptr(JsonObject) primary = design_primary(render);
		if (primary != NULL)
			return g_strdup(ls_string(primary, "href"));
	}
	if (render->book != NULL)
		return g_strdup(render->book);
	if (tel != NULL)
		return g_steal_pointer(&tel);
	return g_strdup(render->quote);
}

/* Whether a button to @href may be filled: always without a supplied
 * action, else only when it goes where that action goes. */
static gboolean
may_lead(Render *render, const gchar *href){
	g_autofree gchar *action = supplied_action(render);
	g_autofree gchar *mine = NULL, *theirs = NULL;
	if (action == NULL)
		return TRUE;
	mine = link_key(href);
	theirs = link_key(action);
	return g_str_equal(mine, theirs);
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
			/* Plain text fields also carry an escaped copy with soft break
			 * points, for templates that show addresses in narrow boxes. */
			if (g_strcmp0(ls_string(slot, "type"), "text") == 0 && JSON_NODE_HOLDS_VALUE(value) &&
				json_node_get_value_type(value) == G_TYPE_STRING) {
				const gchar *text = json_node_get_string(value);
				g_autofree gchar *html_name = g_strconcat((const gchar *)name->data, "_html", NULL);
				GString *html = g_string_new(NULL);
				append_breakable(html, text, strlen(text));
				json_object_set_string_member(view, html_name, html->str);
				g_string_free(html, TRUE);
			}
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
		json_node_take_object(node, link_view(json_node_get_object(value),
			may_lead(render, ls_string(json_node_get_object(value), "href"))));
		return node;
	}
	if (g_str_equal(type, "image")) {
		JsonObject *image = json_node_get_object(value);
		JsonObject *view = json_object_new();
		gint64 width, height;
		g_autofree gchar *srcset = photo_srcset(render->plan, ls_string(image, "src"));
		art_size(render->plan, ls_string(image, "src"), &width, &height);
		json_object_set_string_member(view, "src", shown_src(render->plan, ls_string(image, "src")));
		/* Templates write srcset with their own sizes for the layout. */
		if (srcset != NULL)
			json_object_set_string_member(view, "srcset", srcset);
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
			json_array_add_object_element(views, g_str_equal(type, "links")
				? link_view(item, i == 0 && may_lead(render, ls_string(item, "href")))
				: item_view(render, member_object(slot, "fields"), item, i, eager));
		}
		node = json_node_new(JSON_NODE_ARRAY);
		json_node_take_array(node, views);
		return node;
	}
	return json_node_copy(value);
}

/* A new field {name, label, type} for the builder's own form questions. */
static JsonObject *
new_field(const gchar *name, const gchar *label, const gchar *type){
	JsonObject *field = json_object_new();
	json_object_set_string_member(field, "name", name);
	json_object_set_string_member(field, "label", label);
	json_object_set_string_member(field, "type", type);
	return field;
}

static JsonObject *
copy_object(JsonObject *object){
	g_autoptr(JsonNode) node = json_node_new(JSON_NODE_OBJECT);
	g_autoptr(JsonNode) copy = NULL;
	json_node_set_object(node, object);
	copy = json_node_copy(node);
	return json_object_ref(json_node_get_object(copy));
}

/* The fields of an enquiry form the writer wrote on a preset site, in the
 * controls (docs/components.org, "Controls"): a short list of choices
 * becomes buttons or radio buttons; how to reply, a preferred date and
 * time, an urgent switch where the order offers urgent work and a photo
 * upload where the trade benefits and the form can carry files join
 * before the message; a box to agree to be contacted ends the form. A
 * form without an email field and a message, and a supplied form, keep
 * their fields as written. Returns a new reference. */
static JsonArray *
enquiry_fields(Render *render, JsonArray *fields, const gchar *mode){
	JsonObject *design = member_object(render->plan, "design");
	JsonObject *supplied = member_object(render->plan, "form");
	JsonObject *controls = design != NULL ? member_object(design, "controls") : NULL;
	JsonArray *out;
	gboolean email = FALSE, tel = FALSE, date = FALSE, time = FALSE, check = FALSE, toggle = FALSE, file = FALSE;
	gboolean reply = FALSE, added = FALSE, message = FALSE;
	guint i;
	if (fields == NULL || design == NULL || controls == NULL || (supplied != NULL && member_array(supplied, "fields") == fields))
		return fields != NULL ? json_array_ref(fields) : NULL;
	for (i = 0; i < json_array_get_length(fields); i++) {
		JsonObject *field = json_array_get_object_element(fields, i);
		const gchar *type = ls_string(field, "type") != NULL ? ls_string(field, "type") : "text";
		email = email || g_str_equal(type, "email");
		tel = tel || g_str_equal(type, "tel");
		date = date || g_str_equal(type, "date");
		time = time || g_str_equal(type, "time");
		check = check || g_str_equal(type, "checkbox");
		toggle = toggle || g_str_equal(type, "switch");
		file = file || g_str_equal(type, "file");
		reply = reply || g_strcmp0(ls_string(field, "name"), "contact_by") == 0;
		message = message || g_str_equal(type, "textarea");
	}
	if (!email || !message)
		return json_array_ref(fields);
	out = json_array_new();
	for (i = 0; i < json_array_get_length(fields); i++) {
		JsonObject *field = copy_object(json_array_get_object_element(fields, i));
		const gchar *type = ls_string(field, "type") != NULL ? ls_string(field, "type") : "text";
		JsonArray *options = member_array(field, "options");
		if (g_str_equal(type, "select") && options != NULL && json_array_get_length(options) <= 7)
			json_object_set_string_member(field, "type", json_array_get_length(options) >= 2 &&
				json_array_get_length(options) <= 3 ? "toggle" : "radio");
		if (g_str_equal(type, "textarea") && !added) {
			added = TRUE;
			if (tel && !reply) {
				JsonObject *by = new_field("contact_by", ui_text(render, "contact_by"), "radio");
				JsonArray *ways = json_array_new();
				json_array_add_string_element(ways, ui_text(render, "by_phone"));
				json_array_add_string_element(ways, ui_text(render, "by_email"));
				json_object_set_array_member(by, "options", ways);
				json_array_add_object_element(out, by);
			}
			if (!date)
				json_array_add_object_element(out, new_field("preferred_date", ui_text(render, "preferred_date"), "date"));
			if (!time)
				json_array_add_object_element(out, new_field("preferred_time", ui_text(render, "preferred_time"), "time"));
			if (!toggle && json_object_get_boolean_member(controls, "urgent"))
				json_array_add_object_element(out, new_field("urgent", ui_text(render, "urgent"), "switch"));
			/* A mail link carries no file; only a posted form gets the upload. */
			if (!file && json_object_get_boolean_member(controls, "photos") && g_strcmp0(mode, "endpoint") == 0) {
				JsonObject *photos = new_field("photos", ui_text(render, "photos"), "file");
				json_object_set_string_member(photos, "hint", ui_text(render, "photos_hint"));
				json_array_add_object_element(out, photos);
			}
		}
		json_array_add_object_element(out, field);
	}
	if (!check) {
		JsonObject *agree = new_field("agree", ui_text(render, "agree"), "checkbox");
		json_object_set_boolean_member(agree, "required", TRUE);
		json_array_add_object_element(out, agree);
	}
	return out;
}

/* A choice field's buttons: the first carries the field's id, so the
 * error summary and the label lead to the group. */
static JsonArray *
choice_views(const gchar *fid, JsonArray *options){
	JsonArray *choices = json_array_new();
	guint j;
	for (j = 0; options != NULL && j < json_array_get_length(options); j++) {
		JsonObject *choice = json_object_new();
		g_autofree gchar *cid = j == 0 ? g_strdup(fid) : g_strdup_printf("%s-%u", fid, j + 1);
		json_object_set_string_member(choice, "cid", cid);
		json_object_set_string_member(choice, "value", json_array_get_string_element(options, j));
		json_object_set_string_member(choice, "label", json_array_get_string_element(options, j));
		json_array_add_object_element(choices, choice);
	}
	return choices;
}

/* The view of one field for fields.html, and whether it carries files. */
static JsonObject *
field_view(Render *render, JsonObject *field, const gchar *form_id, guint rows, gboolean *files){
	JsonObject *out = json_object_new();
	const gchar *type = ls_string(field, "type") != NULL ? ls_string(field, "type") : "text";
	g_autofree gchar *fid = g_strdup_printf("%s-%s", form_id, ls_string(field, "name"));
	g_autofree gchar *described = ls_string(field, "hint") != NULL
		? g_strdup_printf("%s-hint %s-error", fid, fid) : g_strdup_printf("%s-error", fid);
	JsonArray *options = member_array(field, "options");
	gboolean choice = g_str_equal(type, "radio") || g_str_equal(type, "toggle");
	gboolean checkbox = g_str_equal(type, "checkbox") || g_str_equal(type, "switch");
	json_object_set_string_member(out, "fid", fid);
	json_object_set_string_member(out, "name", ls_string(field, "name"));
	json_object_set_string_member(out, "label", ls_string(field, "label"));
	json_object_set_string_member(out, "type", type);
	json_object_set_string_member(out, "describedby", described);
	json_object_set_string_member(out, "required_label", ui_text(render, "required"));
	json_object_set_int_member(out, "rows", rows);
	if (ls_string(field, "hint") != NULL)
		json_object_set_string_member(out, "hint", ls_string(field, "hint"));
	if (ls_string(field, "autocomplete") != NULL)
		json_object_set_string_member(out, "autocomplete", ls_string(field, "autocomplete"));
	if (json_object_has_member(field, "maxlength"))
		json_object_set_int_member(out, "maxlength", json_object_get_int_member(field, "maxlength"));
	json_object_set_boolean_member(out, "required", json_object_has_member(field, "required") &&
		json_object_get_boolean_member(field, "required"));
	json_object_set_boolean_member(out, "is_choice", choice);
	json_object_set_boolean_member(out, "is_toggle", g_str_equal(type, "toggle"));
	json_object_set_boolean_member(out, "is_checkbox", checkbox);
	json_object_set_boolean_member(out, "is_switch", g_str_equal(type, "switch"));
	json_object_set_boolean_member(out, "is_other", !choice && !checkbox);
	json_object_set_boolean_member(out, "is_textarea", g_str_equal(type, "textarea"));
	json_object_set_boolean_member(out, "is_select", g_str_equal(type, "select"));
	json_object_set_boolean_member(out, "is_range", g_str_equal(type, "range"));
	json_object_set_boolean_member(out, "is_file", g_str_equal(type, "file"));
	json_object_set_boolean_member(out, "is_input", !choice && !checkbox && !g_str_equal(type, "textarea") &&
		!g_str_equal(type, "select") && !g_str_equal(type, "range") && !g_str_equal(type, "file"));
	*files = *files || g_str_equal(type, "file");
	if (json_object_has_member(field, "min") && json_object_has_member(field, "max")) {
		gint64 min = json_object_get_int_member(field, "min"), max = json_object_get_int_member(field, "max");
		gint64 step = json_object_has_member(field, "step") ? MAX(1, json_object_get_int_member(field, "step")) : 1;
		JsonObject *bounds = json_object_new();
		json_object_set_int_member(bounds, "min", min);
		json_object_set_int_member(bounds, "max", max);
		json_object_set_object_member(out, "bounds", bounds);
		json_object_set_int_member(out, "min", min);
		json_object_set_int_member(out, "max", max);
		json_object_set_int_member(out, "step", step);
		/* A slider starts at its middle, on a step. */
		json_object_set_int_member(out, "value", min + ((max - min) / 2 / step) * step);
	}
	if (choice)
		json_object_set_array_member(out, "choices", choice_views(fid, options));
	else if (options != NULL) {
		JsonArray *choices = json_array_new();
		guint j;
		for (j = 0; j < json_array_get_length(options); j++) {
			JsonObject *option = json_object_new();
			json_object_set_string_member(option, "value", json_array_get_string_element(options, j));
			json_object_set_string_member(option, "label", json_array_get_string_element(options, j));
			json_array_add_object_element(choices, option);
		}
		json_object_set_array_member(out, "options", choices);
	}
	return out;
}

/* fields.html for @views: every field of one form, as HTML. */
static gchar *render_template(Render *render, const gchar *name, JsonObject *view);

static void
fields_html(Render *render, JsonObject *view, JsonArray *views){
	g_autoptr(JsonObject) holder = json_object_new();
	g_autofree gchar *html = NULL;
	if (file_text(render->self, "fields.html") == NULL)
		return;
	json_object_set_array_member(holder, "fields", json_array_ref(views));
	html = render_template(render, "fields.html", holder);
	json_object_set_string_member(view, "fields_html", html);
}

static void
form_view(Render *render, JsonObject *view, JsonObject *content, const gchar *id){
	g_autofree gchar *form_id = g_strconcat(id, "-form", NULL);
	const gchar *mode = ls_string(content, "mode");
	g_autoptr(JsonArray) fields = enquiry_fields(render, member_array(content, "fields"), mode);
	JsonArray *views = json_array_new();
	gboolean files = FALSE;
	guint i;
	json_object_set_string_member(view, "form_id", form_id);
	json_object_set_boolean_member(view, "local", g_strcmp0(mode, "local") == 0);
	json_object_set_boolean_member(view, "mailto", g_strcmp0(mode, "mailto") == 0);
	json_object_set_boolean_member(view, "endpoint", g_strcmp0(mode, "endpoint") == 0);
	json_object_set_string_member(view, "error_required", ui_text(render, "error_required"));
	json_object_set_string_member(view, "error_invalid", ui_text(render, "error_invalid"));
	json_object_set_string_member(view, "error_summary", ui_text(render, "error_summary"));
	if (g_strcmp0(mode, "endpoint") == 0) {
		JsonObject *form = member_object(render->plan, "form");
		const gchar *trap = form != NULL ? ls_string(form, "honeypot") : NULL;
		if (trap != NULL)
			json_object_set_string_member(view, "honeypot", trap);
		/* After a send the visitor stays on the page and sees a confirmation
		 * panel in place of the form, repeating the supplied phone. Only the
		 * script shows it; it needs no id and reserves none. */
		{
			JsonObject *call = call_view(render->plan);
			if (call != NULL)
				json_object_set_object_member(view, "sent_call", call);
		}
		json_object_set_string_member(view, "sending", ui_text(render, "sending"));
		json_object_set_string_member(view, "sent", ui_text(render, "sent"));
		json_object_set_string_member(view, "send_failed", ui_text(render, "send_failed"));
	}
	if (g_strcmp0(mode, "mailto") == 0 && !g_str_has_prefix(ls_string(content, "target"), "mailto:")) {
		g_autofree gchar *target = g_strconcat("mailto:", ls_string(content, "target"), NULL);
		json_object_set_string_member(view, "target", target);
	}
	for (i = 0; fields != NULL && i < json_array_get_length(fields); i++) {
		JsonObject *field = json_array_get_object_element(fields, i);
		/* A file never travels by mail link or in a local text file: such a
		 * field is left out there. */
		if (g_strcmp0(ls_string(field, "type"), "file") == 0 && g_strcmp0(mode, "endpoint") != 0)
			continue;
		json_array_add_object_element(views, field_view(render, field, form_id, 5, &files));
	}
	json_object_set_boolean_member(view, "multipart", files);
	json_object_set_array_member(view, "fields", views);
	fields_html(render, view, views);
}

static gchar *
render_template(Render *render, const gchar *name, JsonObject *view){
	g_autoptr(JsonNode) node = json_node_new(JSON_NODE_OBJECT);
	gchar *text;
	json_node_set_object(node, view);
	text = ls_template_render(file_text(render->self, name), node, render->error != NULL ? NULL : &render->error);
	return text != NULL ? text : g_strdup("");
}

/* Whether the plan's design wears the overlay @name. */
static gboolean
design_overlay(JsonObject *plan, const gchar *name){
	JsonArray *overlays = member_array(member_object(plan, "design"), "overlays");
	guint i;
	for (i = 0; overlays != NULL && i < json_array_get_length(overlays); i++)
		if (g_strcmp0(json_array_get_string_element(overlays, i), name) == 0)
			return TRUE;
	return FALSE;
}

/* The supplied live form as a compact quote form with the form script's
 * hooks, labelled by @labelledby; a @quiet submit is an outline, so the
 * first screen keeps one solid action. NULL without a supplied form. */
static gchar *
quote_form_html(Render *render, const gchar *id, const gchar *labelledby, gboolean quiet){
	g_autoptr(JsonObject) empty = json_object_new();
	g_autoptr(JsonObject) content = supplied_form_content(render->plan, empty);
	g_autoptr(JsonObject) view = NULL;
	guint k;
	if (content == NULL)
		return NULL;
	view = json_object_new();
	form_view(render, view, content, id);
	for (k = 0; k < G_N_ELEMENTS(ui_keys); k++) {
		g_autofree gchar *ui_name = g_strconcat(ui_keys[k], "_label", NULL);
		json_object_set_string_member(view, ui_name, ui_text(render, ui_keys[k]));
	}
	json_object_set_string_member(view, "target", ls_string(content, "target"));
	json_object_set_string_member(view, "labelledby", labelledby);
	json_object_set_string_member(view, "submit", ui_text(render, "quote"));
	json_object_set_string_member(view, "error_invalid", ui_text(render, "error_invalid"));
	json_object_set_boolean_member(view, "quiet", quiet);
	/* The form's styles and script come with it. */
	g_hash_table_add(render->used, g_strdup("form"));
	return render_template(render, "quote-form.html", view);
}

/* The badge on the opening photo: the supplied rating, else the year the
 * business started; NULL with neither. */
static JsonObject *
badge_view(Render *render){
	JsonObject *facts = member_object(member_object(render->plan, "design"), "facts");
	JsonObject *rating = member_object(facts, "rating"), *badge;
	if (rating != NULL && ls_string(rating, "source") != NULL) {
		gchar score[G_ASCII_DTOSTR_BUF_SIZE];
		g_autofree gchar *count = g_strdup_printf("%" G_GINT64_FORMAT, json_object_get_int_member(rating, "count"));
		g_autofree gchar *with_count = replace_all(plan_ui(render->plan, "reviews_on"), "{n}", count);
		g_autofree gchar *label = replace_all(with_count, "{source}", ls_string(rating, "source"));
		badge = json_object_new();
		json_object_set_string_member(badge, "value", g_ascii_formatd(score, sizeof score, "%.1f",
			json_object_get_double_member(rating, "score")));
		json_object_set_string_member(badge, "label", label);
		return badge;
	}
	if (ls_string(facts, "since") == NULL)
		return NULL;
	badge = json_object_new();
	json_object_set_string_member(badge, "value", ls_string(facts, "since"));
	json_object_set_string_member(badge, "label", plan_ui(render->plan, "since"));
	return badge;
}

/* The planned page whose path is @path, or NULL. */
static JsonObject *
planned_page(JsonObject *plan, const gchar *path){
	JsonArray *pages = member_array(plan, "pages");
	guint i;
	for (i = 0; pages != NULL && i < json_array_get_length(pages); i++)
		if (g_strcmp0(ls_string(json_array_get_object_element(pages, i), "path"), path) == 0)
			return json_array_get_object_element(pages, i);
	return NULL;
}

/* The announcement bar: one supplied fact, word for word, and the page
 * that tells more; the hours first, then the towns, then the year. */
static JsonObject *
announcement_view(Render *render){
	JsonObject *facts = member_object(member_object(render->plan, "design"), "facts");
	JsonArray *area = member_array(facts, "area");
	JsonObject *view = json_object_new(), *target = NULL;
	g_autofree gchar *text = NULL;
	const gchar *label = NULL;
	if (ls_string(facts, "hours") != NULL) {
		const gchar *hours = ls_string(facts, "hours"), *stop = strstr(hours, ". ");
		text = stop != NULL ? g_strndup(hours, (gsize)(stop - hours)) : g_strdup(hours);
		label = ui_text(render, "hours");
		target = planned_page(render->plan, "contact.html");
	} else if (area != NULL && json_array_get_length(area) > 0) {
		GString *towns = g_string_new(NULL);
		guint i, n = MIN(json_array_get_length(area), 4);
		for (i = 0; i < n; i++)
			g_string_append_printf(towns, "%s%s", i == 0 ? "" : i + 1 == n && n == json_array_get_length(area) ? " and " : ", ",
				json_array_get_string_element(area, i));
		text = g_string_free(towns, FALSE);
		label = ui_text(render, "towns");
		target = planned_page(render->plan, "service-area.html");
	} else if (ls_string(facts, "since") != NULL) {
		text = g_strdup(ls_string(facts, "since"));
		label = ui_text(render, "since");
		target = planned_page(render->plan, "about.html");
	}
	if (text == NULL) {
		json_object_unref(view);
		return NULL;
	}
	json_object_set_string_member(view, "label", label);
	json_object_set_string_member(view, "text", text);
	json_object_set_string_member(view, "region_label", ui_text(render, "announcement"));
	json_object_set_string_member(view, "dismiss_label", ui_text(render, "dismiss"));
	if (target != NULL && g_strcmp0(ls_string(target, "path"), render->page_path) != 0) {
		json_object_set_string_member(view, "href", ls_string(target, "path"));
		json_object_set_string_member(view, "link_label", ls_string(target, "title"));
	}
	return view;
}

/* The header, footer and overlay views of a design's layouts on one page. */
static void
design_layout_views(Render *render, JsonObject *view, JsonArray *nav){
	JsonObject *design = member_object(render->plan, "design");
	JsonObject *facts = member_object(design, "facts");
	const gchar *header = ls_string(design, "header"), *footer = ls_string(design, "footer");
	/* Drawer and overlay menus close from inside, as well as by Escape. */
	if (g_strcmp0(header, "drawer") == 0 || g_strcmp0(header, "overlay") == 0)
		json_object_set_string_member(view, "nav_close", ui_text(render, "close"));
	/* The centred name sits after the first half of the links. */
	if (g_strcmp0(header, "centered-split") == 0 && json_array_get_length(nav) >= 2)
		json_object_set_boolean_member(json_array_get_object_element(nav, (json_array_get_length(nav) + 1) / 2 - 1),
			"split", TRUE);
	if (g_strcmp0(footer, "big-cta") == 0) {
		JsonObject *big = call_view(render->plan);
		if (big != NULL) {
			json_object_set_string_member(big, "lead", ui_text(render, "quick_actions"));
			json_object_set_object_member(view, "footer_big", big);
		}
	}
	if (g_strcmp0(footer, "mega") == 0 && (ls_string(facts, "address") != NULL || ls_string(facts, "hours") != NULL)) {
		JsonObject *place = json_object_new();
		if (ls_string(facts, "address") != NULL) {
			g_autofree gchar *query = g_uri_escape_string(ls_string(facts, "address"), NULL, FALSE);
			g_autofree gchar *href = g_strconcat("https://www.openstreetmap.org/search?query=", query, NULL);
			JsonObject *directions = json_object_new();
			json_object_set_string_member(place, "address", ls_string(facts, "address"));
			json_object_set_string_member(directions, "href", href);
			json_object_set_string_member(directions, "label", ui_text(render, "directions"));
			json_object_set_object_member(place, "directions", directions);
		}
		if (ls_string(facts, "hours") != NULL)
			json_object_set_string_member(place, "hours", ls_string(facts, "hours"));
		json_object_set_object_member(view, "footer_place", place);
	}
	if (g_strcmp0(footer, "wordmark") == 0)
		json_object_set_boolean_member(view, "footer_wordmark", TRUE);
	if (g_strcmp0(footer, "split-form") == 0) {
		g_autofree gchar *html = quote_form_html(render, "footer", "footer-form-h", FALSE);
		if (html != NULL) {
			JsonObject *form = json_object_new();
			json_object_set_string_member(form, "title", ui_text(render, "quote"));
			json_object_set_string_member(form, "html", html);
			json_object_set_object_member(view, "footer_form", form);
		}
	}
	if (design_overlay(render->plan, "announcement")) {
		JsonObject *announcement = announcement_view(render);
		if (announcement != NULL)
			json_object_set_object_member(view, "announcement", announcement);
	}
	if (design_overlay(render->plan, "lightbox")) {
		JsonObject *box = json_object_new();
		json_object_set_string_member(box, "label", ui_text(render, "own_work"));
		json_object_set_string_member(box, "previous", ui_text(render, "previous"));
		json_object_set_string_member(box, "next", ui_text(render, "next"));
		json_object_set_string_member(box, "close", ui_text(render, "close"));
		json_object_set_object_member(view, "lightbox", box);
	}
	/* The quote form opens over the page from every link to it; without
	 * the script those links still lead to the form's own page. */
	if (design_overlay(render->plan, "modal-form") && render->quote != NULL) {
		g_autofree gchar *html = quote_form_html(render, "dialog", "quote-dialog-h", FALSE);
		if (html != NULL) {
			JsonObject *dialog = json_object_new();
			json_object_set_string_member(dialog, "title", ui_text(render, "quote"));
			json_object_set_string_member(dialog, "close_label", ui_text(render, "close"));
			json_object_set_string_member(dialog, "form_html", html);
			json_object_set_object_member(view, "quote_dialog", dialog);
		}
	}
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

/* The fewest initial letters that earn a directory its letter index. */
#define DIRECTORY_INDEX_LETTERS 6

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
	/* A letter index helps only a long list; over a few entries its
	 * letters read as chips standing in for content, so a short directory
	 * is one plain list in the writer's order. */
	if (letters->len < DIRECTORY_INDEX_LETTERS) {
		JsonObject *group = json_object_new();
		g_autofree gchar *group_id = g_strdup_printf("%s-g1", id);
		json_object_set_string_member(group, "group_id", group_id);
		json_object_set_array_member(group, "entries", json_array_ref(items));
		json_array_add_object_element(groups, group);
		json_object_set_array_member(view, "groups", groups);
		return;
	}
	g_ptr_array_sort(letters, compare_letters);
	json_object_set_boolean_member(view, "letter_index", TRUE);
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

/* @variant of component @name, or, when the taste block calls it a gimmick
 * or a banned opening that a writer named, the component's first calm
 * variant. */
static const gchar *
calm_variant(LsComponents *self, JsonObject *plan, JsonObject *component, const gchar *name, const gchar *variant){
	g_autofree gchar *pair = g_strdup_printf("%s/%s", name, variant != NULL ? variant : "");
	JsonObject *structure = member_object(plan, "structure");
	g_autoptr(GList) values = NULL;
	GList *value;
	if (!taste_banned(self, "variants", pair) && !taste_banned(self, "openings", pair))
		return variant;
	/* A structure that names it chose it on purpose (a theme or an
	 * explicit option). */
	if (structure != NULL && g_strcmp0(ls_string(structure, "opening"), pair) == 0)
		return variant;
	values = json_object_get_members(member_object(component, "variants"));
	for (value = values; value != NULL; value = value->next) {
		g_autofree gchar *other = g_strdup_printf("%s/%s", name, (const gchar *)value->data);
		/* Member names belong to the catalog, so they outlive the list. */
		if (!taste_banned(self, "variants", other) && !taste_banned(self, "openings", other))
			return value->data;
	}
	return variant;
}

/* A link address reduced for comparison: no scheme, no www., no trailing slash. */
static gchar *
link_key(const gchar *href){
	g_autofree gchar *lower = g_ascii_strdown(href != NULL ? href : "", -1);
	const gchar *start = lower;
	gsize length;
	if (g_str_has_prefix(start, "https://"))
		start += 8;
	else if (g_str_has_prefix(start, "http://"))
		start += 7;
	if (g_str_has_prefix(start, "www."))
		start += 4;
	length = strlen(start);
	while (length > 0 && start[length - 1] == '/')
		length--;
	return g_strndup(start, length);
}

/* @text with every @needle replaced by @value. */
static gchar *
replace_all(const gchar *text, const gchar *needle, const gchar *value){
	g_auto(GStrv) parts = g_strsplit(text, needle, -1);
	return g_strjoinv(value, parts);
}

/* Profile views for templates: the supplied profiles with their icons. */
static JsonArray *
profile_views(Render *render){
	JsonArray *social = member_array(render->plan, "social");
	JsonArray *out = json_array_new();
	guint i;
	for (i = 0; social != NULL && i < json_array_get_length(social); i++) {
		JsonObject *entry = json_array_get_object_element(social, i);
		JsonObject *network = render->self->networks != NULL
			? member_object(render->self->networks, ls_string(entry, "network")) : NULL;
		JsonObject *web = render->self->networks != NULL ? member_object(render->self->networks, "web") : NULL;
		const gchar *icon = network != NULL && ls_string(network, "path") != NULL ? ls_string(network, "path")
			: web != NULL ? ls_string(web, "path") : NULL;
		JsonObject *view = json_object_new();
		json_object_set_string_member(view, "url", ls_string(entry, "url"));
		json_object_set_string_member(view, "label", ls_string(entry, "label"));
		json_object_set_string_member(view, "network", ls_string(entry, "network"));
		if (ls_string(entry, "handle") != NULL)
			json_object_set_string_member(view, "handle", ls_string(entry, "handle"));
		if (icon != NULL)
			json_object_set_string_member(view, "icon", icon);
		json_array_add_object_element(out, view);
	}
	return out;
}

/* Share targets for the page. With a site address the links are complete;
 * without one site.js fills them from the visitor's address. Nothing loads
 * from a network until the visitor follows a link. */
static JsonArray *
share_views(Render *render){
	static const gchar *const targets[][3] = {
		{"email", "Email", "mailto:?subject={title}&body={url}"},
		{"facebook", "Facebook", "https://www.facebook.com/sharer/sharer.php?u={url}"},
		{"x", "X", "https://x.com/intent/post?url={url}&text={title}"},
		{"linkedin", "LinkedIn", "https://www.linkedin.com/sharing/share-offsite/?url={url}"},
		{"whatsapp", "WhatsApp", "https://wa.me/?text={title}%20{url}"},
		{"bluesky", "Bluesky", "https://bsky.app/intent/compose?text={title}%20{url}"},
	};
	JsonArray *out = json_array_new();
	g_autofree gchar *url = render->base == NULL ? NULL : g_str_equal(render->page_path, "index.html")
		? g_strdup(render->base) : g_strconcat(render->base, render->page_path, NULL);
	g_autofree gchar *title = g_strcmp0(render->page_title, ls_string(render->plan, "name")) == 0
		? g_strdup(render->page_title) : g_strdup_printf("%s | %s", render->page_title, ls_string(render->plan, "name"));
	g_autofree gchar *url_q = url != NULL ? g_uri_escape_string(url, NULL, FALSE) : NULL;
	g_autofree gchar *title_q = g_uri_escape_string(title, NULL, FALSE);
	guint i;
	for (i = 0; i < G_N_ELEMENTS(targets); i++) {
		JsonObject *view = json_object_new();
		JsonObject *network = render->self->networks != NULL ? member_object(render->self->networks, targets[i][0]) : NULL;
		json_object_set_string_member(view, "network", targets[i][0]);
		json_object_set_string_member(view, "label", targets[i][1]);
		json_object_set_string_member(view, "pattern", targets[i][2]);
		if (network != NULL && ls_string(network, "path") != NULL)
			json_object_set_string_member(view, "icon", ls_string(network, "path"));
		if (url_q != NULL) {
			g_autofree gchar *with_url = replace_all(targets[i][2], "{url}", url_q);
			g_autofree gchar *href = replace_all(with_url, "{title}", title_q);
			json_object_set_string_member(view, "href", href);
		}
		json_array_add_object_element(out, view);
	}
	return out;
}

static gboolean few_words(const gchar *text);
static guint count_words(const gchar *text);

/* On a preset site every closing call is one of the design's, so the
 * builder's buttons sit on a tone made for them. */
static const gchar *
design_variant(JsonObject *plan, const gchar *name, const gchar *variant, JsonObject *content){
	/* The photo band only with the photo the builder gave it. */
	if (member_object(plan, "design") != NULL && g_strcmp0(name, "cta") == 0 &&
		g_strcmp0(variant, "photo-band") == 0 && member_object(content, "art") != NULL)
		return variant;
	if (member_object(plan, "design") != NULL && g_strcmp0(name, "cta") == 0 &&
		g_strcmp0(variant, "closing-band") != 0 && g_strcmp0(variant, "promise") != 0)
		return "closing-band";
	return variant;
}

static void
render_sections(Render *render, JsonArray *sections, GPtrArray *pieces, JsonArray *toc){
	g_autoptr(GHashTable) anchors = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	g_autoptr(GPtrArray) ids = section_ids(sections, anchors);
	JsonArray *designed = g_strcmp0(render->page_path, "index.html") == 0
		? member_array(member_object(render->plan, "design"), "home") : NULL;
	guint i, cursor = 0;
	for (i = 0; i < json_array_get_length(sections); i++) {
		JsonObject *section = json_array_get_object_element(sections, i);
		const gchar *name = ls_string(section, "component");
		JsonObject *component = member_object(render->self->components, name);
		JsonObject *slots = member_object(component, "slots");
		g_autoptr(JsonObject) supplied = g_str_equal(name, "form")
			? supplied_form_content(render->plan, member_object(section, "content")) : NULL;
		JsonObject *content = supplied != NULL ? supplied : member_object(section, "content");
		g_autoptr(GList) variants = json_object_get_members(member_object(component, "variants"));
		const gchar *variant = design_variant(render->plan, name, calm_variant(render->self, render->plan, component, name,
			ls_string(section, "variant") != NULL ? ls_string(section, "variant") : (const gchar *)variants->data), content);
		const gchar *id = g_ptr_array_index(ids, i);
		g_autoptr(JsonObject) view = json_object_new();
		g_autoptr(JsonObject) wrapper = json_object_new();
		g_autoptr(GList) keys = json_object_get_members(slots);
		g_autofree gchar *own = g_strdup_printf("%s--%s.html", name, variant);
		g_autofree gchar *file = file_text(render->self, own) != NULL ? g_strdup(own) : g_strconcat(name, ".html", NULL);
		g_autofree gchar *flag = g_strconcat("v_", variant, NULL);
		const gchar *tone = NULL;
		g_autofree gchar *inner = NULL, *html = NULL;
		GList *key;
		guint k;
		g_hash_table_add(render->used, g_strdup(name));
		g_hash_table_add(render->used, g_strdup_printf("%s--%s", name, variant));
		for (key = keys; key != NULL; key = key->next) {
			JsonNode *value = json_object_get_member(content, key->data);
			if (value == NULL || JSON_NODE_HOLDS_NULL(value))
				continue;
			json_object_set_member(view, key->data, convert_value(render, member_object(slots, key->data), value, i == 0));
			/* Lines of three words or fewer may be set in outline. */
			if (JSON_NODE_HOLDS_ARRAY(json_object_get_member(view, key->data))) {
				JsonArray *converted = json_object_get_array_member(view, key->data);
				guint e;
				for (e = 0; e < json_array_get_length(converted); e++) {
					JsonNode *entry = json_array_get_element(converted, e);
					if (JSON_NODE_HOLDS_OBJECT(entry) && ls_string(json_node_get_object(entry), "text") != NULL)
						json_object_set_boolean_member(json_node_get_object(entry), "short_text",
							few_words(ls_string(json_node_get_object(entry), "text")));
				}
			}
			if (JSON_NODE_HOLDS_ARRAY(value)) {
				g_autofree gchar *has = g_strconcat("has_", (const gchar *)key->data, NULL);
				json_object_set_boolean_member(view, has, json_array_get_length(json_node_get_array(value)) > 0);
			}
		}
		if (g_str_equal(name, "scene") || g_str_equal(name, "material"))
			ls_visual_view(render->plan, view, i);
		/* A service with nothing but its name is a fact, not a link: a bare
		 * link reads as navigation, not as the work the business does. */
		if (g_str_equal(name, "services") && member_array(view, "items") != NULL) {
			JsonArray *items = member_array(view, "items");
			guint e;
			for (e = 0; e < json_array_get_length(items); e++) {
				JsonObject *item = json_array_get_object_element(items, e);
				gboolean bare = TRUE;
				const gchar *detail[] = {"summary", "price", "duration"};
				guint d;
				for (d = 0; d < G_N_ELEMENTS(detail); d++)
					bare = bare && (!json_object_has_member(item, detail[d]) ||
						JSON_NODE_HOLDS_NULL(json_object_get_member(item, detail[d])));
				/* With a page for every service, the name is that page's link. */
				if (bare && json_object_has_member(item, "link") &&
					member_array(member_object(render->plan, "design"), "pages") == NULL)
					json_object_set_null_member(item, "link");
			}
		}
		/* Builder facts the model never writes: supplied profiles and share targets. */
		if (g_str_equal(name, "social")) {
			JsonArray *profiles = profile_views(render);
			json_object_set_boolean_member(view, "has_profiles", json_array_get_length(profiles) > 0);
			json_object_set_array_member(view, "profiles", profiles);
		} else if (g_str_equal(name, "share")) {
			json_object_set_array_member(view, "targets", share_views(render));
			json_object_set_string_member(view, "share_title", render->page_title);
		} else if (g_str_equal(name, "call") || (g_str_equal(name, "contact") && member_object(render->plan, "design") != NULL)) {
			JsonObject *call = call_view(render->plan);
			if (call != NULL)
				json_object_set_object_member(view, "call", call);
			json_object_set_string_member(view, "call_label", ui_text(render, "call"));
		} else if (g_str_equal(name, "booking-calendar") && render->book != NULL) {
			JsonArray *pages = member_array(member_object(render->plan, "design"), "pages");
			g_autoptr(JsonArray) names = json_array_new();
			guint p;
			json_object_set_string_member(view, "booking_url", render->book);
			json_object_set_string_member(view, "agree", ui_text(render, "agree"));
			/* The order's services as a choice (docs/components.org, "Controls"). */
			for (p = 0; pages != NULL && p < json_array_get_length(pages); p++)
				if (g_strcmp0(ls_string(json_array_get_object_element(pages, p), "kind"), "detail") == 0)
					json_array_add_string_element(names, ls_string(json_array_get_object_element(pages, p), "title"));
			if (json_array_get_length(names) > 0) {
				JsonObject *services = json_object_new();
				g_autofree gchar *fid = g_strdup_printf("%s-service", id);
				json_object_set_string_member(services, "legend", ui_text(render, "service"));
				json_object_set_boolean_member(services, "toggle", json_array_get_length(names) >= 2 &&
					json_array_get_length(names) <= 3);
				json_object_set_array_member(services, "choices", choice_views(fid, names));
				json_object_set_object_member(view, "services", services);
			}
		} else if (g_str_equal(name, "module")) {
			/* The supplied snippet, as podbay drew it and checked again with
			 * the plan; it also posts without scripts. */
			JsonObject *module = plan_module(render->plan, ls_string(content, "module"));
			g_autofree gchar *snippet = module != NULL
				? ls_module_snippet_html(ls_string(module, "snippet"), ui_text(render, "send_failed")) : NULL;
			if (snippet != NULL) {
				json_object_set_string_member(view, "snippet", snippet);
				g_hash_table_add(render->modules, (gpointer)ls_string(module, "id"));
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
			(g_utf8_strlen(ls_string(view, "heading"), -1) > 38 || count_words(ls_string(view, "heading")) > 5));
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
		/* Products and news build their filter buttons from the items' own
		 * categories or tags, reusing the filter script and styles. */
		if ((g_str_equal(name, "products") || g_str_equal(name, "news")) &&
			!blank(ls_string(content, "all_label")))
			derived_filter(render, view, g_str_equal(name, "products") ? "category" : "tag");
		if (g_str_equal(name, "hours"))
			json_object_set_boolean_member(view, "live", member_array(content, "schedule") != NULL &&
				json_array_get_length(member_array(content, "schedule")) > 0);
		if (g_str_equal(name, "form"))
			form_view(render, view, content, id);
		if (g_str_equal(name, "directory"))
			directory_view(view, id);
		/* A design draws the opening's and the contact's actions: the one
		 * primary action that fits the trade, and the quote as an outline
		 * beside a call; the phone as a line when the action is not the call. */
		if (member_object(render->plan, "design") != NULL && ((i == 0 && g_str_equal(name, "hero")) ||
			g_str_equal(name, "contact") || g_str_equal(name, "cta"))) {
			g_autoptr(JsonObject) primary = design_primary(render);
			g_autoptr(JsonObject) secondary = design_secondary(render);
			JsonArray *actions = json_array_new();
			JsonObject *call = call_view(render->plan);
			if (primary != NULL)
				json_array_add_object_element(actions, link_view(primary, TRUE));
			if (secondary != NULL)
				json_array_add_object_element(actions, link_view(secondary, FALSE));
			json_object_set_array_member(view, "actions", actions);
			json_object_set_boolean_member(view, "has_actions", json_array_get_length(actions) > 0);
			/* The closing call adds the phone, unless the button is the
			 * call, and the email. */
			if (call != NULL && g_str_equal(name, "cta")) {
				json_object_set_boolean_member(call, "show_phone", g_strcmp0(design_string(render->plan, "action"), "call") != 0);
				json_object_set_object_member(view, "call", call);
			} else if (call != NULL && g_strcmp0(design_string(render->plan, "action"), "call") != 0)
				json_object_set_object_member(view, "phone_line", call);
			else if (call != NULL)
				json_object_unref(call);
		}
		if (g_str_equal(name, "areas"))
			json_object_set_boolean_member(view, "has_place", ls_string(content, "hours") != NULL ||
				ls_string(content, "address") != NULL);
		/* Home sections sit on the tone the design gave them. */
		for (; designed != NULL && cursor < json_array_get_length(designed); cursor++) {
			JsonObject *entry = json_array_get_object_element(designed, cursor);
			if (g_strcmp0(ls_string(entry, "component"), name) == 0) {
				tone = ls_string(entry, "tone");
				cursor++;
				break;
			}
		}
		/* The design's band carries its own tone on every page. */
		if (tone == NULL && member_object(render->plan, "design") != NULL && g_str_equal(name, "cta") &&
			g_str_equal(variant, "closing-band"))
			tone = "brand";
		/* The hero's quote form; the gallery's photos open larger. */
		if (member_object(render->plan, "design") != NULL && i == 0 && g_str_equal(name, "hero") &&
			g_str_equal(variant, "form")) {
			g_autofree gchar *labelled_by = g_strconcat(id, "-h", NULL);
			g_autofree gchar *form_html = quote_form_html(render, id, labelled_by, TRUE);
			if (form_html != NULL)
				json_object_set_string_member(view, "form_html", form_html);
		}
		if (g_str_equal(name, "gallery") && design_overlay(render->plan, "lightbox"))
			json_object_set_boolean_member(view, "lightbox", TRUE);
		inner = render_template(render, file, view);
		/* A badge of one supplied fact on the home's opening photo. */
		if (i == 0 && g_str_equal(name, "hero") && design_overlay(render->plan, "badge") &&
			g_strcmp0(render->page_path, "index.html") == 0) {
			JsonObject *badge = badge_view(render);
			if (badge != NULL)
				json_object_set_object_member(wrapper, "badge", badge);
		}
		if (tone != NULL)
			json_object_set_string_member(wrapper, "tone", tone);
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

/* Filter buttons from the distinct values of @field across the view's items,
 * in first-seen order; items without one only show under the all button. */
static void
derived_filter(Render *render, JsonObject *view, const gchar *field){
	JsonArray *items = json_object_get_array_member(view, "items");
	JsonArray *categories = json_array_new();
	g_autoptr(GHashTable) seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	guint j;
	for (j = 0; items != NULL && j < json_array_get_length(items); j++) {
		JsonObject *item = json_array_get_object_element(items, j);
		const gchar *value = ls_string(item, field);
		g_autofree gchar *key = NULL;
		if (blank(value)) {
			json_object_set_string_member(item, "category_key", "");
			continue;
		}
		key = slugify(value, 40);
		json_object_set_string_member(item, "category_key", key);
		if (g_hash_table_add(seen, g_strdup(key))) {
			JsonObject *category = json_object_new();
			json_object_set_string_member(category, "label", value);
			json_object_set_string_member(category, "key", key);
			json_array_add_object_element(categories, category);
		}
	}
	json_object_set_array_member(view, "categories", categories);
	json_object_set_boolean_member(view, "filtered", json_array_get_length(categories) > 1);
	if (json_array_get_length(categories) > 1)
		g_hash_table_add(render->used, g_strdup("filter"));
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
		/* A drop-down menu: its children, and whether this page is one of them. */
		if (member_array(link, "children") != NULL) {
			JsonArray *children = member_array(link, "children");
			JsonArray *views = json_array_new();
			gboolean inside = FALSE;
			guint j;
			for (j = 0; j < json_array_get_length(children); j++) {
				JsonObject *child = json_array_get_object_element(children, j);
				JsonObject *child_view = link_view(child, FALSE);
				gboolean here = g_strcmp0(ls_string(child, "href"), path) == 0;
				json_object_set_boolean_member(child_view, "current", here);
				inside |= here;
				json_array_add_object_element(views, child_view);
			}
			json_object_set_array_member(view, "children", views);
			json_object_set_boolean_member(view, "has_children", TRUE);
			json_object_set_boolean_member(view, "in_section", inside);
		}
		json_array_add_object_element(out, view);
	}
}

/* Short words still take separate lines in a display heading. */
static guint
count_words(const gchar *text){
	g_auto(GStrv) words = NULL;
	guint i, count = 0;
	if (text == NULL)
		return 0;
	words = g_strsplit_set(text, " \t\n", -1);
	for (i = 0; words[i] != NULL; i++)
		count += words[i][0] != '\0';
	return count;
}

/* At most three words: short enough to set in outline at display size. */
static gboolean
few_words(const gchar *text){
	guint count = count_words(text);
	return count > 0 && count <= 3;
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
	gboolean sidebar, reading;
	const gchar *name = ls_string(plan, "name");
	g_autofree gchar *full = g_strcmp0(title, name) == 0 ? g_strdup(name) : g_strdup_printf("%s | %s", title, name);
	gdouble paper[3];
	guint i;
	json_object_set_string_member(view, "lang", ls_string(plan, "lang") != NULL ? ls_string(plan, "lang") : "en");
	json_object_set_string_member(view, "title", title);
	/* The caller's credit line, in every footer, exactly as supplied. */
	if (ls_credit_valid(member_object(plan, "credit")))
		json_object_set_object_member(view, "credit", json_object_ref(member_object(plan, "credit")));
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
		/* A main heading of three words or fewer may be set in outline. */
		{
			JsonArray *sections = member_array(page, "sections");
			JsonObject *first = sections != NULL && json_array_get_length(sections) > 0 &&
				JSON_NODE_HOLDS_OBJECT(json_array_get_element(sections, 0)) ? json_array_get_object_element(sections, 0) : NULL;
			JsonObject *content = member_object(first, "content");
			if (content != NULL && few_words(ls_string(content, "heading"))) {
				gchar *more = g_strconcat(classes, " h1-short", NULL);
				g_free(classes);
				classes = more;
			}
		}
		if (member_object(plan, "visual") != NULL) {
			gchar *more = g_strconcat(classes, " visual-sequence", NULL);
			g_free(classes);
			classes = more;
		}
		/* A design names its preset, its header and footer layouts and its
		 * primitives on the body. */
		if (member_object(plan, "design") != NULL) {
			JsonObject *design = member_object(plan, "design");
			JsonArray *overlays = member_array(design, "overlays");
			GString *extra = g_string_new(NULL);
			gchar *more;
			guint o;
			for (o = 0; overlays != NULL && o < json_array_get_length(overlays); o++)
				g_string_append_printf(extra, " overlay--%s", json_array_get_string_element(overlays, o));
			more = g_strdup_printf("%s design design--%s header--%s footer--%s kicker--%s accent--%s skeleton--%s "
				"motion--%s%s", classes, ls_string(design, "preset"), ls_string(design, "header"),
				ls_string(design, "footer"), ls_string(design, "kicker"), ls_string(design, "accent"),
				ls_string(design, "skeleton") != NULL ? ls_string(design, "skeleton") : "stack",
				ls_string(design, "motion") != NULL ? ls_string(design, "motion") : "none", extra->str);
			g_string_free(extra, TRUE);
			g_free(classes);
			classes = more;
		}
		json_object_set_string_member(view, "look_classes", classes);
	}
	json_object_set_boolean_member(view, "home", g_str_equal(path, "index.html"));
	parse_color(ls_string(tokens, "paper"), paper);
	if (member_object(member_object(plan, "design"), "dark") != NULL)
		json_object_set_string_member(view, "color_scheme", g_str_equal(plan_scheme(plan), "auto") ? "light dark"
			: plan_scheme(plan));
	else
		json_object_set_string_member(view, "color_scheme",
			g_strcmp0(ls_string(member_object(plan, "look"), "scheme"), "auto") == 0
			? (luminance(paper) < 0.2 ? "dark light" : "light dark") : luminance(paper) < 0.2 ? "dark" : "light");
	/* A footer sitemap lists every planned page and the built-in ones. */
	if (g_strcmp0(ls_string(member_object(plan, "look"), "footer"), "sitemap") == 0) {
		JsonArray *map = json_array_new();
		JsonArray *planned = member_array(plan, "pages");
		guint m;
		for (m = 0; planned != NULL && m < json_array_get_length(planned); m++) {
			JsonObject *entry = json_array_get_object_element(planned, m);
			JsonObject *link = json_object_new();
			json_object_set_string_member(link, "href", ls_string(entry, "path"));
			json_object_set_string_member(link, "label", ls_string(entry, "title"));
			json_object_set_boolean_member(link, "current", g_strcmp0(ls_string(entry, "path"), path) == 0);
			json_array_add_object_element(map, link);
		}
		json_object_set_array_member(view, "sitemap", map);
		json_object_set_boolean_member(view, "has_sitemap", json_array_get_length(map) > 0);
	}
	json_object_set_string_member(view, "theme_color", ls_string(tokens, "paper"));
	/* Light and dark: the dark page colour for the browser's own bar, and
	 * the toggle with its words when the visitor may choose. */
	{
		JsonObject *dark = member_object(member_object(member_object(plan, "design"), "dark"), "tokens");
		if (dark != NULL && g_str_equal(plan_scheme(plan), "dark"))
			json_object_set_string_member(view, "theme_color", ls_string(dark, "paper"));
		else if (dark != NULL && g_str_equal(plan_scheme(plan), "auto")) {
			JsonObject *toggle = json_object_new();
			json_object_set_string_member(view, "theme_color_dark", ls_string(dark, "paper"));
			json_object_set_string_member(toggle, "label", ui_text(render, "scheme"));
			json_object_set_string_member(toggle, "system", ui_text(render, "scheme_system"));
			json_object_set_string_member(toggle, "light", ui_text(render, "scheme_light"));
			json_object_set_string_member(toggle, "dark", ui_text(render, "scheme_dark"));
			json_object_set_object_member(view, "scheme_toggle", toggle);
		}
	}
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
	if (ls_string(plan, "tagline") != NULL)
		json_object_set_string_member(view, "tagline", ls_string(plan, "tagline"));
	append_link_views(nav, member_array(plan, "nav"), path, seen);
	json_object_set_array_member(view, "nav", nav);
	if (member_object(plan, "design") != NULL) {
		/* The header's button is the one primary action; the utility row
		 * adds the phone and the quote beside it. */
		g_autoptr(JsonObject) primary = design_primary(render);
		g_autoptr(JsonObject) secondary = design_secondary(render);
		JsonObject *design = member_object(plan, "design");
		if (primary != NULL)
			json_object_set_object_member(view, "action", link_view(primary, TRUE));
		if (g_strcmp0(ls_string(design, "header"), "utility") == 0) {
			JsonObject *utility = json_object_new();
			JsonObject *call = call_view(plan);
			if (call != NULL)
				json_object_set_object_member(utility, "call", call);
			if (secondary != NULL)
				json_object_set_object_member(utility, "secondary", link_view(secondary, FALSE));
			json_object_set_object_member(view, "header_utility", utility);
		}
		{
			JsonObject *contact = call_view(plan);
			if (contact != NULL)
				json_object_set_object_member(view, "footer_contact", contact);
		}
		design_layout_views(render, view, nav);
		/* The centered footer adds the supplied address and hours. */
		if (g_strcmp0(ls_string(design, "footer"), "centered") == 0) {
			JsonObject *facts = member_object(design, "facts");
			if (ls_string(facts, "address") != NULL || ls_string(facts, "hours") != NULL) {
				JsonObject *place = json_object_new();
				if (ls_string(facts, "address") != NULL)
					json_object_set_string_member(place, "address", ls_string(facts, "address"));
				if (ls_string(facts, "hours") != NULL)
					json_object_set_string_member(place, "hours", ls_string(facts, "hours"));
				json_object_set_object_member(view, "footer_place", place);
			}
		}
	} else if (member_object(plan, "action") != NULL) {
		JsonObject *action = link_view(member_object(plan, "action"),
			may_lead(render, ls_string(member_object(plan, "action"), "href")));
		json_object_set_object_member(view, "action", action);
	}
	/* A preset site credits every supplied photo; stock never passes for
	 * the owner's own work. */
	if (member_object(plan, "design") != NULL) {
		g_autofree gchar *credits = photo_credits(plan);
		if (credits != NULL)
			json_object_set_string_member(view, "photo_credits", credits);
	}
	/* One primary action stays in reach on every page: the supplied booking
	 * page, else the supplied phone, else the live form. */
	{
		const gchar *chosen = design_string(plan, "action");
		const gchar *book = chosen == NULL || g_str_equal(chosen, "book") ? render->book : NULL;
		JsonObject *call = book == NULL && (chosen == NULL || !g_str_equal(chosen, "quote")) ? call_view(plan) : NULL;
		gboolean quote = book == NULL && call == NULL && render->quote != NULL;
		if (call != NULL || quote || book != NULL) {
			JsonObject *bar = json_object_new();
			json_object_set_string_member(bar, "label", ui_text(render, "quick_actions"));
			json_object_set_string_member(bar, "call_label", ui_text(render, "call"));
			if (call != NULL)
				json_object_set_object_member(bar, "call", call);
			if (book != NULL) {
				JsonObject *link = json_object_new();
				json_object_set_string_member(link, "href", book);
				json_object_set_string_member(link, "label", ui_text(render, "book"));
				json_object_set_object_member(bar, "book", link);
			}
			if (quote) {
				JsonObject *link = json_object_new();
				json_object_set_string_member(link, "href", render->quote);
				json_object_set_string_member(link, "label", ui_text(render, "quote"));
				json_object_set_object_member(bar, "quote", link);
			}
			/* The wide sticky bar adds the phone as a line beside a button
			 * that is not the call. */
			if (call == NULL && design_overlay(plan, "sticky-bar")) {
				JsonObject *line = call_view(plan);
				if (line != NULL)
					json_object_set_object_member(bar, "bar_phone", line);
			}
			json_object_set_object_member(view, "actions", bar);
			g_hash_table_add(render->used, g_strdup("call"));
		}
	}
	render->page_path = path;
	render->page_title = title;
	render->wordmark = ls_string(view, "wordmark");
	/* Reading kinds keep a contents column. */
	reading = kind != NULL && json_object_has_member(kind, "sidebar") && json_object_get_boolean_member(kind, "sidebar");
	render_sections(render, member_array(page, "sections"), pieces, toc);
	/* They get an in-page contents column once there is enough to navigate. */
	sidebar = reading && json_array_get_length(toc) >= 2 && pieces->len > 1;
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
	/* Link previews use the page's first picture, when the site has an address to make it absolute. */
	if (render->base != NULL && !noindex) {
		/* The first image, whatever its attribute order: some templates
		 * put the class before src. */
		const gchar *tag = strstr(main->str, "<img ");
		const gchar *end = tag != NULL ? strchr(tag, '>') : NULL;
		g_autofree gchar *head = tag != NULL && end != NULL ? g_strndup(tag, (gsize)(end - tag)) : NULL;
		const gchar *img = head != NULL ? strstr(head, " src=\"") : NULL;
		if (img != NULL) {
			g_autofree gchar *src = g_strndup(img + 6, strcspn(img + 6, "\""));
			g_autofree gchar *lower = g_ascii_strdown(src, -1);
			/* Link previews need a raster picture; drawings stay out. */
			if (strstr(src, "://") == NULL && src[0] != '/' && src[0] != '\0' && (g_str_has_suffix(lower, ".jpg") ||
				g_str_has_suffix(lower, ".jpeg") || g_str_has_suffix(lower, ".png") || g_str_has_suffix(lower, ".webp"))) {
				g_autofree gchar *absolute = g_strconcat(render->base, src, NULL);
				json_object_set_string_member(view, "og_image", absolute);
			}
		}
	}
	json_object_set_string_member(view, "twitter_card", json_object_has_member(view, "og_image") ? "summary_large_image" : "summary");
	g_string_free(main, TRUE);
	/* Supplied profiles: footer links on every page, and the business's
	 * sameAs links for search engines on the home page. */
	{
		JsonArray *profiles = profile_views(render);
		json_object_set_boolean_member(view, "has_social", json_array_get_length(profiles) > 0);
		json_object_set_string_member(view, "social_label", ui_text(render, "social"));
		if (g_str_equal(path, "index.html") && json_array_get_length(profiles) > 0) {
			g_autoptr(JsonBuilder) builder = json_builder_new();
			g_autoptr(JsonNode) node = NULL;
			g_autofree gchar *text = NULL, *safe = NULL;
			guint k;
			json_builder_begin_object(builder);
			json_builder_set_member_name(builder, "@context");
			json_builder_add_string_value(builder, "https://schema.org");
			json_builder_set_member_name(builder, "@type");
			json_builder_add_string_value(builder, "Organization");
			json_builder_set_member_name(builder, "name");
			json_builder_add_string_value(builder, name);
			if (render->base != NULL) {
				json_builder_set_member_name(builder, "url");
				json_builder_add_string_value(builder, render->base);
			}
			json_builder_set_member_name(builder, "sameAs");
			json_builder_begin_array(builder);
			for (k = 0; k < json_array_get_length(profiles); k++)
				json_builder_add_string_value(builder, ls_string(json_array_get_object_element(profiles, k), "url"));
			json_builder_end_array(builder);
			json_builder_end_object(builder);
			node = json_builder_get_root(builder);
			text = ls_json_encode(node);
			/* Inside <script>, "</" could end the element early. */
			safe = replace_all(text, "</", "<\\/");
			json_object_set_string_member(view, "jsonld", safe);
		}
		json_object_set_array_member(view, "social", profiles);
	}
	if (footer != NULL && ls_string(footer, "note") != NULL) {
		JsonNode *note = rich_node(render, ls_string(footer, "note"));
		json_object_set_member(view, "footer_note", note);
	}
	g_hash_table_remove_all(seen);
	/* Supplied profiles already have footer icons; a text link to the same
	 * profile would list it twice. */
	{
		g_autoptr(JsonArray) kept = json_array_new();
		g_autoptr(GHashTable) profiles = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
		JsonArray *social = member_array(plan, "social");
		JsonArray *links = footer != NULL ? member_array(footer, "links") : NULL;
		for (i = 0; social != NULL && i < json_array_get_length(social); i++)
			g_hash_table_add(profiles, link_key(ls_string(json_array_get_object_element(social, i), "url")));
		for (i = 0; links != NULL && i < json_array_get_length(links); i++) {
			JsonObject *link = json_array_get_object_element(links, i);
			g_autofree gchar *key = link_key(ls_string(link, "href"));
			if (!g_hash_table_contains(profiles, key))
				json_array_add_element(kept, json_node_copy(json_array_get_element(links, i)));
		}
		/* A design's footer lists every page. */
		if (member_array(member_object(plan, "design"), "pages") != NULL) {
			JsonArray *planned = member_array(member_object(plan, "design"), "pages");
			for (i = 0; planned != NULL && i < json_array_get_length(planned); i++) {
				JsonObject *entry = json_array_get_object_element(planned, i);
				JsonObject *link = json_object_new();
				json_object_set_string_member(link, "href", ls_string(entry, "path"));
				json_object_set_string_member(link, "label", ls_string(entry, "title"));
				json_array_add_object_element(kept, link);
			}
		}
		append_link_views(footer_links, kept, path, seen);
	}
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
		if (fonts != NULL) {
			json_object_set_string_member(view, "font_href", fonts);
			json_object_set_array_member(view, "font_preloads", font_preloads(plan));
		}
	}
	/* Every link to the live form opens the quote dialog, when there is one. */
	if (json_object_has_member(view, "quote_dialog") && render->quote != NULL) {
		g_autofree gchar *html = render_template(render, "layout.html", view);
		g_autofree gchar *escaped = ls_template_escape(render->quote);
		g_autofree gchar *needle = g_strdup_printf("href=\"%s\"", escaped);
		g_autofree gchar *with = g_strdup_printf("href=\"%s\" data-dialog=\"quote-dialog\"", escaped);
		return replace_all(html, needle, with);
	}
	return render_template(render, "layout.html", view);
}

static void
append_number(GString *out, const gchar *name, gdouble value, const gchar *unit){
	gchar buffer[G_ASCII_DTOSTR_BUF_SIZE];
	g_string_append_printf(out, "\t--%s: %s%s;\n", name,
		g_ascii_formatd(buffer, sizeof buffer, "%.4g", value), unit);
}

/* A structure gene, or @fallback when the plan predates structure. */
static gdouble
gene(JsonObject *structure, const gchar *name, gdouble fallback){
	JsonNode *value = structure != NULL ? json_object_get_member(structure, name) : NULL;
	if (value == NULL || !JSON_NODE_HOLDS_VALUE(value) || (json_node_get_value_type(value) != G_TYPE_DOUBLE &&
		json_node_get_value_type(value) != G_TYPE_INT64))
		return fallback;
	return json_node_get_value_type(value) == G_TYPE_INT64 ? (gdouble)json_node_get_int(value)
		: json_node_get_double(value);
}

/* The warning red: deep on a light paper, soft on a dark one, and readable
 * on the paper and on the surface panels it also marks (a warning
 * callout's title); otherwise the ink. */
static const gchar *
danger_color(const gchar *paper, const gchar *surface, const gchar *ink){
	gdouble rgb[3];
	const gchar *red;
	parse_color(paper, rgb);
	red = luminance(rgb) > 0.4 ? "#a4161a" : "#ff8a80";
	return contrast(red, paper) >= 4.5 && contrast(red, surface) >= 4.5 ? red : ink;
}

static gchar *accent_surface(const gchar *accent, const gchar *accent_ink);

/* Tokens become custom properties; derived colors keep contrast guarantees. */
static gchar *
token_css(JsonObject *tokens, JsonObject *structure){
	GString *out = g_string_new(":root {\n");
	const gchar *density = ls_string(tokens, "density") != NULL ? ls_string(tokens, "density") : "regular";
	const gdouble multipliers[] = {0.25, 0.5, 0.75, 1, 1.5, 2, 3, 4.5};
	gdouble base = token_number(tokens, &number_tokens[0]);
	gdouble ratio = token_number(tokens, &number_tokens[1]);
	gdouble space = token_number(tokens, &number_tokens[5]);
	gdouble border = token_number(tokens, &number_tokens[4]);
	gint step;
	guint i;
	color_css(out, tokens);
	g_string_append(out, "\t--field: var(--paper);\n");
	for (i = 0; i < G_N_ELEMENTS(font_tokens); i++) {
		g_autofree gchar *name = g_strdelimit(g_strdup(font_tokens[i]), "_", '-');
		g_string_append_printf(out, "\t--%s: %s;\n", name, ls_string(tokens, font_tokens[i]));
	}
	for (step = -1; step <= 7; step++) {
		gdouble size = base * pow(ratio, step) * (step >= 4 && structure != NULL ? gene(structure, "scale", 1) : 1);
		g_autofree gchar *name = step < 0 ? g_strdup("step--1") : g_strdup_printf("step-%d", step);
		if (step <= 1)
			append_number(out, name, size, "rem");
		else {
			gdouble low = MAX(base * ratio, base * pow(ratio, step * 0.55));
			gdouble slope = (size - low) / 57.5;
			gchar a[G_ASCII_DTOSTR_BUF_SIZE], b[G_ASCII_DTOSTR_BUF_SIZE], c[G_ASCII_DTOSTR_BUF_SIZE], d[G_ASCII_DTOSTR_BUF_SIZE];
			/* Container units: type scales with the content column (main is a
			 * container), so it fits beside a side menu too; outside main they
			 * fall back to the viewport. */
			g_string_append_printf(out, "\t--%s: clamp(%srem, calc(%srem + %scqi), %srem);\n", name,
				g_ascii_formatd(a, sizeof a, "%.4g", low),
				g_ascii_formatd(b, sizeof b, "%.4g", low - 22.5 * slope),
				g_ascii_formatd(c, sizeof c, "%.4g", slope * 100),
				g_ascii_formatd(d, sizeof d, "%.4g", size));
		}
	}
	/* Structure genes scale what the tokens chose; a plan without them keeps
	 * the tokens exactly. */
	if (structure != NULL)
		space *= gene(structure, "pad", 1);
	for (i = 0; i < G_N_ELEMENTS(multipliers); i++) {
		g_autofree gchar *name = g_strdup_printf("space-%u", i + 1);
		append_number(out, name, space * multipliers[i], "rem");
	}
	append_number(out, "measure", token_number(tokens, &number_tokens[2]), "ch");
	append_number(out, "radius", token_number(tokens, &number_tokens[3]), "px");
	append_number(out, "border", border, "px");
	append_number(out, "border-strong", MAX(2, border), "px");
	if (structure == NULL) {
		append_number(out, "frame-width", token_number(tokens, &number_tokens[9]), "rem");
		g_string_append(out, "\t--gutter: clamp(1rem, 4vw, 2.5rem);\n");
		g_string_append_printf(out, "\t--section-space: %s;\n", g_str_equal(density, "compact")
			? "clamp(2.5rem, 5vw, 4rem)" : g_str_equal(density, "airy") ? "clamp(4rem, 10vw, 8.5rem)"
			: "clamp(3rem, 7vw, 6rem)");
	} else {
		gdouble pace = gene(structure, "pace", 1) * (g_str_equal(density, "compact") ? 0.8
			: g_str_equal(density, "airy") ? 1.3 : 1);
		gdouble gutter = gene(structure, "gutter", 2.5);
		gchar a[G_ASCII_DTOSTR_BUF_SIZE], b[G_ASCII_DTOSTR_BUF_SIZE], c[G_ASCII_DTOSTR_BUF_SIZE];
		append_number(out, "frame-width", gene(structure, "width", 76), "rem");
		g_string_append_printf(out, "\t--gutter: clamp(1rem, %svw, %srem);\n",
			g_ascii_formatd(a, sizeof a, "%.3g", gutter * 1.6), g_ascii_formatd(b, sizeof b, "%.3g", MAX(1, gutter)));
		/* Slow paces breathe, but sections never drift more than eight rem
		 * apart: beyond that a wide screen shows a screenful of nothing. */
		g_string_append_printf(out, "\t--section-space: clamp(%srem, %svw, %srem);\n",
			g_ascii_formatd(a, sizeof a, "%.3g", MIN(1.5 + pace, 4)), g_ascii_formatd(b, sizeof b, "%.3g", 6 * pace),
			g_ascii_formatd(c, sizeof c, "%.3g", MIN(5 * pace + 1, 8)));
		append_number(out, "grid-scale", gene(structure, "grid", 1), "");
	}
	g_string_append(out, "\t--max-width: var(--frame-width);\n");
	append_number(out, "heading-weight", token_number(tokens, &number_tokens[6]), "");
	append_number(out, "heading-tracking", token_number(tokens, &number_tokens[7]), "em");
	append_number(out, "heading-leading", token_number(tokens, &number_tokens[8]), "");
	g_string_append_printf(out, "\t--heading-case: %s;\n",
		g_strcmp0(ls_string(tokens, "heading_case"), "uppercase") == 0 ? "uppercase" : "none");
	g_string_append(out, "}\n");
	return g_string_free(out, FALSE);
}

/* @a moved toward @b by @t (0..1), as #rrggbb. */
static gchar *
mix_color(const gchar *a, const gchar *b, gdouble t){
	gdouble x[3], y[3], out[3];
	guint i;
	if (!parse_color(a, x) || !parse_color(b, y))
		return g_strdup(a);
	for (i = 0; i < 3; i++)
		out[i] = x[i] + (y[i] - x[i]) * t;
	return format_color(out);
}

/* Text in the accent colour must read on the paper and on the surface tint
 * the bands use: the accent itself when it does, else the accent deepened
 * toward the ink in the smallest step that does, else the ink. */
static gchar *
accent_text(const gchar *accent, const gchar *paper, const gchar *surface, const gchar *ink){
	gdouble t;
	if (contrast(accent, paper) >= 4.5 && (surface == NULL || contrast(accent, surface) >= 4.5))
		return g_strdup(accent);
	for (t = 0.05; t < 1; t += 0.05) {
		g_autofree gchar *mixed = mix_color(accent, ink, t);
		if (contrast(mixed, paper) >= 4.5 && (surface == NULL || contrast(mixed, surface) >= 4.5))
			return g_steal_pointer(&mixed);
	}
	return g_strdup(ink);
}

/* The panel colour on an accent band: the accent moved a little toward its
 * text, as the band's cards and boxes show it, unless that costs the text
 * its AA contrast (a mid accent under white text); then moved the other
 * way, and failing both the accent itself. */
static gchar *
accent_surface(const gchar *accent, const gchar *accent_ink){
	gdouble rgb[3];
	g_autofree gchar *toward = mix_color(accent, accent_ink, 0.12);
	g_autofree gchar *away = NULL;
	if (contrast(accent_ink, toward) >= 4.5)
		return g_steal_pointer(&toward);
	if (!parse_color(accent_ink, rgb))
		return g_strdup(accent);
	away = mix_color(accent, luminance(rgb) > 0.4 ? "#000000" : "#ffffff", 0.12);
	return contrast(accent_ink, away) >= 4.5 ? g_steal_pointer(&away) : g_strdup(accent);
}


/* The colour tokens of one scheme, with the accent text, accent panel,
 * focus ring, firm line and warning red derived from them. */
static void
color_css(GString *out, JsonObject *tokens){
	const gchar *paper = ls_string(tokens, "paper");
	const gchar *accent = ls_string(tokens, "accent");
	guint i;
	for (i = 0; i < G_N_ELEMENTS(color_tokens); i++) {
		g_autofree gchar *name = g_strdelimit(g_strdup(color_tokens[i]), "_", '-');
		g_string_append_printf(out, "\t--%s: %s;\n", name, ls_string(tokens, color_tokens[i]));
	}
	{
		g_autofree gchar *text = accent_text(accent, paper, ls_string(tokens, "surface"), ls_string(tokens, "ink"));
		g_string_append_printf(out, "\t--accent-text: %s;\n", text);
	}
	{
		g_autofree gchar *panel = accent_surface(accent, ls_string(tokens, "accent_ink"));
		g_string_append_printf(out, "\t--accent-surface: %s;\n", panel);
	}
	g_string_append_printf(out, "\t--focus: %s;\n", contrast(accent, paper) >= 3 ? accent : ls_string(tokens, "ink"));
	g_string_append_printf(out, "\t--line-strong: %s;\n", contrast(ls_string(tokens, "line"), paper) >= 3
		? ls_string(tokens, "line") : ls_string(tokens, "muted"));
	g_string_append_printf(out, "\t--danger: %s;\n", danger_color(paper, ls_string(tokens, "surface"), ls_string(tokens, "ink")));
}

/* A preset's dark scheme as declarations: its colour tokens over the plan's,
 * then the design's dark roles. NULL without a dark scheme. */
static gchar *
dark_declarations(JsonObject *plan){
	JsonObject *design = member_object(plan, "design");
	JsonObject *dark = design != NULL ? member_object(design, "dark") : NULL;
	JsonObject *colors = dark != NULL ? member_object(dark, "tokens") : NULL;
	g_autoptr(JsonObject) tokens = NULL;
	g_autofree gchar *roles = NULL;
	GString *out;
	guint i;
	if (colors == NULL)
		return NULL;
	tokens = json_object_new();
	for (i = 0; i < G_N_ELEMENTS(color_tokens); i++) {
		const gchar *value = ls_string(colors, color_tokens[i]) != NULL ? ls_string(colors, color_tokens[i])
			: ls_string(member_object(plan, "tokens"), color_tokens[i]);
		if (value != NULL)
			json_object_set_string_member(tokens, color_tokens[i], value);
	}
	out = g_string_new("\tcolor-scheme: dark;\n");
	color_css(out, tokens);
	roles = ls_design_dark_css(design);
	g_string_append(out, roles);
	return g_string_free(out, FALSE);
}

/* The plan's colour scheme: auto (light, with dark for visitors who ask for
 * it), light or dark. Only a design with its own dark scheme offers dark. */
static const gchar *
plan_scheme(JsonObject *plan){
	JsonObject *design = member_object(plan, "design");
	const gchar *scheme = design != NULL ? ls_string(design, "scheme") : NULL;
	if (design == NULL || member_object(design, "dark") == NULL)
		return "light";
	return scheme != NULL ? scheme : "auto";
}

/* look.scheme auto: the other scheme's working colors, derived from the
 * tokens and repaired to the same AA pairs, for visitors whose device asks
 * for it. A light site gains a dark version and a dark site a light one. */
static gchar *
scheme_css(JsonObject *tokens){
	const gchar *paper = ls_string(tokens, "paper"), *ink = ls_string(tokens, "ink");
	const gchar *accent = ls_string(tokens, "accent");
	g_autoptr(JsonObject) alt = json_object_new();
	gdouble rgb[3];
	gboolean light;
	GString *out;
	guint round;
	if (paper == NULL || ink == NULL || accent == NULL || !parse_color(paper, rgb))
		return g_strdup("");
	light = luminance(rgb) >= 0.2;
	{
		g_autofree gchar *mixed = mix_color(ink, light ? "#000000" : "#ffffff", light ? 0.2 : 0.04);
		/* The other scheme's ground is as calm as the default one (docs/components.org, "Taste"). */
		g_autofree gchar *base = ls_taste_calm(mixed, 0.15);
		g_autofree gchar *surface = mix_color(base, paper, 0.07);
		g_autofree gchar *line = mix_color(base, paper, 0.22);
		g_autofree gchar *muted = mix_color(paper, base, 0.3);
		json_object_set_string_member(alt, "paper", base);
		json_object_set_string_member(alt, "ink", paper);
		json_object_set_string_member(alt, "surface", surface);
		json_object_set_string_member(alt, "line", line);
		json_object_set_string_member(alt, "muted", muted);
	}
	for (round = 0; round < 2; round++) {
		const gchar *keys[] = {"ink", "muted"};
		const gchar *grounds[] = {"paper", "surface"};
		guint k, g;
		for (k = 0; k < G_N_ELEMENTS(keys); k++)
			for (g = 0; g < G_N_ELEMENTS(grounds); g++)
				if (contrast(ls_string(alt, keys[k]), ls_string(alt, grounds[g])) < 4.5) {
					g_autofree gchar *ground = g_strdup(ls_string(alt, grounds[g]));
					nudge(alt, keys[k], ground, 4.5);
				}
	}
	out = g_string_new(NULL);
	g_string_append_printf(out, "@media (prefers-color-scheme: %s) {\n:root {\n", light ? "dark" : "light");
	g_string_append_printf(out, "\t--paper: %s;\n\t--ink: %s;\n\t--muted: %s;\n\t--surface: %s;\n\t--line: %s;\n",
		ls_string(alt, "paper"), ls_string(alt, "ink"), ls_string(alt, "muted"), ls_string(alt, "surface"),
		ls_string(alt, "line"));
	{
		g_autofree gchar *text = accent_text(accent, ls_string(alt, "paper"), ls_string(alt, "surface"), ls_string(alt, "ink"));
		g_string_append_printf(out, "\t--accent-text: %s;\n", text);
	}
	g_string_append_printf(out, "\t--focus: %s;\n", contrast(accent, ls_string(alt, "paper")) >= 3
		? accent : ls_string(alt, "ink"));
	g_string_append_printf(out, "\t--line-strong: %s;\n", ls_string(alt, "muted"));
	/* The warning red follows the alternate paper, as token_css() does. */
	g_string_append_printf(out, "\t--danger: %s;\n", danger_color(ls_string(alt, "paper"), ls_string(alt, "surface"), ls_string(alt, "ink")));
	g_string_append(out, "}\n}\n");
	return g_string_free(out, FALSE);
}

/* Supplied photographs keep their subject in frame when a box crops them. */
static gchar *
focus_css(JsonObject *plan){
	JsonArray *photos = member_array(plan, "photos");
	GString *out = g_string_new(NULL);
	guint i;
	for (i = 0; photos != NULL && i < json_array_get_length(photos); i++) {
		JsonObject *photo = json_array_get_object_element(photos, i);
		if (ls_string(photo, "focus") != NULL)
			g_string_append_printf(out, "img[src=\"%s\"] { object-position: %s; }\n",
				shown_src(plan, ls_string(photo, "path")), ls_string(photo, "focus"));
	}
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

/* The quote action leads to the first planned page showing a form section,
 * at that section: "visit.html#quote-form". NULL when no page has one. */
static gchar *
quote_href(JsonObject *plan, JsonObject *pages){
	JsonArray *planned = member_array(plan, "pages");
	guint i;
	for (i = 0; planned != NULL && i < json_array_get_length(planned); i++) {
		const gchar *path = ls_string(json_array_get_object_element(planned, i), "path");
		JsonNode *node = path != NULL ? json_object_get_member(pages, path) : NULL;
		JsonArray *sections = node != NULL && JSON_NODE_HOLDS_OBJECT(node)
			? member_array(json_node_get_object(node), "sections") : NULL;
		guint s;
		for (s = 0; sections != NULL && s < json_array_get_length(sections); s++) {
			JsonNode *section = json_array_get_element(sections, s);
			if (JSON_NODE_HOLDS_OBJECT(section) &&
				g_strcmp0(ls_string(json_node_get_object(section), "component"), "form") == 0) {
				g_autoptr(GHashTable) anchors = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
				g_autoptr(GPtrArray) ids = section_ids(sections, anchors);
				return g_strdup_printf("%s#%s", path, (const gchar *)g_ptr_array_index(ids, s));
			}
		}
	}
	return NULL;
}

/* A booking, contact or visit page outranks a name that merely contains
 * those letters. Zero means leave the page alone. */
static gint
booking_calendar_score(JsonObject *entry){
	const gchar *path = ls_string(entry, "path");
	const gchar *kind = ls_string(entry, "kind");
	if (path != NULL && (g_str_has_prefix(path, "book.") || g_str_has_prefix(path, "book-") ||
		g_str_has_prefix(path, "booking.") || g_str_has_prefix(path, "booking-") ||
		g_str_has_prefix(path, "reserv")))
		return 5;
	if ((path != NULL && g_str_has_prefix(path, "contact")) || g_strcmp0(kind, "contact") == 0)
		return 4;
	if ((path != NULL && g_str_has_prefix(path, "visit")) || g_strcmp0(kind, "visit") == 0)
		return 3;
	if (g_strcmp0(kind, "form") == 0)
		return 2;
	return 0;
}

/* Give a bookable site one calendar on its booking, contact or visit page
 * when that page still has room. Copy the specifications only when inserting:
 * the caller and the checkpoint still own their originals. */
static JsonObject *
pages_with_booking_calendar(JsonObject *plan, JsonObject *pages){
	JsonArray *planned = member_array(plan, "pages");
	JsonObject *copy;
	GList *names, *item;
	const gchar *target = NULL;
	gint best = 0;
	guint i;
	if (pages == NULL || !ls_booking_valid(member_object(plan, "booking")))
		return json_object_ref(pages);
	for (i = 0; planned != NULL && i < json_array_get_length(planned); i++) {
		JsonObject *entry = json_array_get_object_element(planned, i);
		const gchar *path;
		JsonNode *node;
		JsonArray *sections;
		gint score;
		guint j;
		if (entry == NULL)
			continue;
		path = ls_string(entry, "path");
		node = path != NULL ? json_object_get_member(pages, path) : NULL;
		sections = node != NULL && JSON_NODE_HOLDS_OBJECT(node)
			? member_array(json_node_get_object(node), "sections") : NULL;
		for (j = 0; sections != NULL && j < json_array_get_length(sections); j++) {
			JsonObject *section = json_array_get_object_element(sections, j);
			if (section != NULL && g_strcmp0(ls_string(section, "component"), "booking-calendar") == 0)
				return json_object_ref(pages);
		}
		/* A full page cannot take another section; try the next candidate. */
		if (sections == NULL || json_array_get_length(sections) >= MAX_SECTIONS)
			continue;
		score = booking_calendar_score(entry);
		if (score > best) {
			best = score;
			target = path;
		}
	}
	if (target == NULL)
		return json_object_ref(pages);
	copy = json_object_new();
	names = json_object_get_members(pages);
	for (item = names; item != NULL; item = item->next) {
		JsonNode *node = json_object_get_member(pages, item->data);
		/* json_node_copy() shares the object, so copy the text. */
		if (node != NULL)
			json_object_set_member(copy, item->data, node_copy(node));
	}
	g_list_free(names);
	{
		JsonNode *node = json_object_get_member(copy, target);
		JsonObject *page = node != NULL && JSON_NODE_HOLDS_OBJECT(node) ? json_node_get_object(node) : NULL;
		JsonArray *sections = member_array(page, "sections");
		JsonObject *section;
		JsonObject *content;
		if (sections == NULL)
			return copy;
		section = json_object_new();
		content = json_object_new();
		json_object_set_string_member(section, "component", "booking-calendar");
		json_object_set_string_member(section, "variant", "month");
		json_object_set_string_member(content, "heading", "Choose a time");
		json_object_set_object_member(section, "content", content);
		json_array_add_object_element(sections, section);
	}
	return copy;
}

/* Whether @extra (COMPONENT--VARIANT) is one of the library's variants. */
static gboolean
library_variant(LsComponents *self, const gchar *extra){
	const gchar *dash = strstr(extra, "--");
	g_autofree gchar *pair = dash != NULL ? g_strdup_printf("%.*s/%s", (gint)(dash - extra), extra, dash + 2) : NULL;
	return pair != NULL && member_object(member_object(self->library, "variants"), pair) != NULL;
}

gchar *
ls_components_compose(LsComponents *self, JsonNode *plan_node, JsonObject *pages, const gchar *base_url, GError **error){
	Problems problems = {g_ptr_array_new_with_free_func(g_free)};
	Render render = {self, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};
	g_autoptr(JsonObject) ui = json_object_new();
	g_autoptr(JsonObject) booking_pages = NULL;
	g_autoptr(GHashTable) used = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	g_autoptr(GHashTable) modules = g_hash_table_new(g_str_hash, g_str_equal);
	g_autoptr(JsonNode) root = json_node_new(JSON_NODE_OBJECT);
	g_autoptr(JsonObject) bundle = json_object_new();
	g_autofree gchar *base = NULL;
	g_autofree gchar *quote = NULL;
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
	booking_pages = pages_with_booking_calendar(plan, pages);
	pages = booking_pages;
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
	/* A site with a live form offers the quote action that leads to it. */
	if (member_object(plan, "form") != NULL)
		quote = quote_href(plan, pages);
	for (i = 0; i < G_N_ELEMENTS(ui_keys); i++) {
		JsonObject *custom = member_object(plan, "ui");
		json_object_set_string_member(ui, ui_keys[i], custom != NULL && ls_string(custom, ui_keys[i]) != NULL
			? ls_string(custom, ui_keys[i]) : ui_defaults[i]);
	}
	render.plan = plan;
	render.ui = ui;
	render.used = used;
	render.modules = modules;
	render.base = base;
	render.quote = quote;
	/* A supplied booking page offers the book action that leads to it. */
	if (ls_booking_valid(member_object(plan, "booking")))
		render.book = ls_string(member_object(plan, "booking"), "href");
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
		"@layer ls-base, ls-modules, ls-components, ls-look, ls-signature;\n"
		"@media (prefers-reduced-motion: no-preference) { @view-transition { navigation: auto; } }\n");
	{
		g_autofree gchar *tokens = token_css(member_object(plan, "tokens"), member_object(plan, "structure"));
		g_autofree gchar *focus = focus_css(plan);
		g_autofree gchar *design = member_object(plan, "design") != NULL ? ls_design_css(member_object(plan, "design")) : NULL;
		g_autofree gchar *dark = dark_declarations(plan);
		g_string_append(css, tokens);
		if (design != NULL)
			g_string_append(css, design);
		/* A preset's own dark scheme (docs/components.org, "Light and
		 * dark"): the visitor's system asks for it unless the toggle chose
		 * light, the toggle chooses it, or the owner locks the site to it.
		 * Nothing animates the change. */
		if (dark != NULL && g_str_equal(plan_scheme(plan), "auto"))
			g_string_append_printf(css, ":root { color-scheme: light; }\n"
				"@media (prefers-color-scheme: dark) {\n:root:not([data-scheme=\"light\"]) {\n%s}\n}\n"
				":root[data-scheme=\"dark\"] {\n%s}\n", dark, dark);
		else if (dark != NULL && g_str_equal(plan_scheme(plan), "dark"))
			g_string_append_printf(css, ":root {\n%s}\n", dark);
		else if (dark != NULL)
			g_string_append(css, ":root { color-scheme: light; }\n");
		else if (g_strcmp0(ls_string(member_object(plan, "look"), "scheme"), "auto") == 0) {
			g_autofree gchar *scheme = scheme_css(member_object(plan, "tokens"));
			g_string_append(css, scheme);
		}
		g_string_append(css, focus);
	}
	g_string_append_printf(css, "@layer ls-base {\n%s}\n", file_text(self, "base.css"));
	/* Every form control, in every form (docs/components.org, "Controls"). */
	if (file_text(self, "controls.css") != NULL)
		g_string_append_printf(css, "/* controls */\n@layer ls-base {\n%s}\n", file_text(self, "controls.css"));
	/* Supplied podbay module styles sit under the library, whose module
	 * component retunes them to the site's tokens. */
	if (g_hash_table_size(modules) > 0) {
		JsonArray *supplied = member_array(plan, "modules");
		g_string_append(css, "@layer ls-modules {\n");
		for (i = 0; i < json_array_get_length(supplied); i++) {
			JsonObject *module = json_array_get_object_element(supplied, i);
			if (g_hash_table_contains(modules, ls_string(module, "id")))
				g_string_append_printf(css, "/* podbay module: %s */\n%s\n", ls_string(module, "id"),
					ls_string(module, "css"));
		}
		g_string_append(css, "}\n");
	}
	js = g_string_new("/* Generated by libresite from the component library. */\n");
	g_string_append(js, file_text(self, "nav.js"));
	if (g_str_equal(plan_scheme(plan), "auto") && file_text(self, "scheme.js") != NULL)
		g_string_append_printf(js, "\n%s", file_text(self, "scheme.js"));
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
	/* Extra variants a page rendered bring their own styles. */
	for (i = 0; i < self->extras->len; i++) {
		const gchar *extra = g_ptr_array_index(self->extras, i);
		g_autofree gchar *file = g_strconcat(extra, ".css", NULL);
		if (g_hash_table_contains(used, extra) && !library_variant(self, extra))
			g_string_append_printf(css, "\n/* %s */\n@layer ls-components {\n%s}\n", extra, file_text(self, file));
	}
	g_string_append_printf(css, "\n/* look */\n@layer ls-look {\n%s}\n", file_text(self, "look.css"));
	/* A design's primitives and section shells lead over the stock look. */
	if (member_object(plan, "design") != NULL && file_text(self, "design.css") != NULL)
		g_string_append_printf(css, "\n/* design */\n@layer ls-look {\n%s}\n", file_text(self, "design.css"));
	/* The design's page skeleton, overlays and motion, with their scripts. */
	if (member_object(plan, "design") != NULL) {
		JsonObject *design = member_object(plan, "design");
		JsonArray *overlays = member_array(design, "overlays");
		const gchar *header = ls_string(design, "header"), *motion = ls_string(design, "motion");
		g_autofree gchar *skeleton = g_strdup_printf("skeleton--%s.css", ls_string(design, "skeleton") != NULL
			? ls_string(design, "skeleton") : "stack");
		if (file_text(self, skeleton) != NULL)
			g_string_append_printf(css, "\n/* %s */\n@layer ls-look {\n%s}\n", skeleton, file_text(self, skeleton));
		for (i = 0; overlays != NULL && i < json_array_get_length(overlays); i++) {
			g_autofree gchar *overlay = g_strdup_printf("overlay--%s.css", json_array_get_string_element(overlays, i));
			if (file_text(self, overlay) != NULL)
				g_string_append_printf(css, "\n/* %s */\n@layer ls-look {\n%s}\n", overlay, file_text(self, overlay));
		}
		if (motion != NULL && !g_str_equal(motion, "none") && file_text(self, "motion.css") != NULL) {
			g_string_append_printf(css, "\n/* motion */\n@layer ls-look {\n%s}\n", file_text(self, "motion.css"));
			g_string_append_printf(js, "\n%s", file_text(self, "motion.js"));
		}
		if ((g_strcmp0(header, "drawer") == 0 || g_strcmp0(header, "overlay") == 0 ||
			g_strcmp0(header, "transparent-solid") == 0 || g_strcmp0(header, "shrink") == 0) &&
			file_text(self, "header.js") != NULL)
			g_string_append_printf(js, "\n%s", file_text(self, "header.js"));
		if (overlays != NULL && json_array_get_length(overlays) > 0 && file_text(self, "overlay.js") != NULL)
			g_string_append_printf(js, "\n%s", file_text(self, "overlay.js"));
	}
	/* Library variants own layout and lead over the primitives they arrange. */
	for (i = 0; i < self->extras->len; i++) {
		const gchar *extra = g_ptr_array_index(self->extras, i);
		g_autofree gchar *file = g_strconcat(extra, ".css", NULL);
		if (g_hash_table_contains(used, extra) && library_variant(self, extra))
			g_string_append_printf(css, "\n/* %s */\n@layer ls-look {\n%s}\n", extra, file_text(self, file));
	}
	/* Extra look values the site wears bring their own styles. */
	{
		JsonObject *look = member_object(plan, "look");
		g_autoptr(GList) axes = look != NULL ? json_object_get_members(look) : NULL;
		GList *axis;
		for (axis = axes; axis != NULL; axis = axis->next) {
			g_autofree gchar *file = ls_string(look, axis->data) != NULL
				? g_strdup_printf("look-%s-%s.css", (const gchar *)axis->data, ls_string(look, axis->data)) : NULL;
			if (file != NULL && file_text(self, file) != NULL)
				g_string_append_printf(css, "\n/* look %s */\n@layer ls-look {\n%s}\n", file, file_text(self, file));
		}
	}
	if (member_array(plan, "photos") != NULL && file_text(self, "supplied-photos.css") != NULL)
		g_string_append_printf(css, "\n@layer ls-look {\n%s}\n", file_text(self, "supplied-photos.css"));
	for (i = 0; member_object(plan, "restyle") != NULL && i < self->order->len; i++) {
		const gchar *name = g_ptr_array_index(self->order, i);
		const gchar *restyle = ls_string(member_object(plan, "restyle"), name);
		if (restyle != NULL && g_hash_table_contains(used, name))
			g_string_append_printf(css, "\n/* Site restyle: %s */\n%s\n", name, restyle);
	}
	if (ls_string(plan, "css") != NULL)
		g_string_append_printf(css, "\n/* Site plan */\n%s\n", ls_string(plan, "css"));
	/* The site's signature sits in the last layer: it leads over the library,
	 * the site's own CSS still leads over it, and the library's few
	 * !important guards (focus rings, the index label) hold against it. */
	/* The signature's divider punctuates the page at every third gap and
	 * keeps to its own small space, so it never fences off every section. */
	if (ls_string(plan, "signature") != NULL && member_object(plan, "visual") == NULL)
		g_string_append_printf(css, "\n/* Site signature */\n@layer ls-signature {\n%s\n"
			"main > .block + .block::before { margin-block: 0 var(--space-5); }\n"
			"main > .block + .block:not(:nth-child(3n+1))::before { display: none; }\n}\n", ls_string(plan, "signature"));
	if (member_object(plan, "visual") != NULL && file_text(self, "visual.css") != NULL)
		g_string_append_printf(css, "\n@layer ls-signature {\n%s\n}\n", file_text(self, "visual.css"));
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

/* ---- Taste ---------------------------------------------------------------- */

/* Pages that carry no photograph by design. */
static gboolean
photoless_route(const gchar *path){
	return g_strcmp0(path, "404.html") == 0 || g_strcmp0(path, "privacy.html") == 0 || g_strcmp0(path, "terms.html") == 0;
}

/* Whether @node, anywhere inside, shows one of @plan's supplied photos. A
 * photo counts only under a slot that @markup draws ({{#slot}}), so a
 * writer's photo in a layout without pictures does not count; NULL markup
 * counts every photo. @slot is the member that holds @node. */
static gboolean
shows_photo(JsonObject *plan, JsonNode *node, const gchar *markup, const gchar *slot){
	if (node == NULL)
		return FALSE;
	if (JSON_NODE_HOLDS_ARRAY(node)) {
		JsonArray *list = json_node_get_array(node);
		guint i;
		for (i = 0; i < json_array_get_length(list); i++)
			if (shows_photo(plan, json_array_get_element(list, i), markup, slot))
				return TRUE;
	} else if (JSON_NODE_HOLDS_OBJECT(node)) {
		JsonObject *object = json_node_get_object(node);
		g_autoptr(GList) members = NULL;
		GList *member;
		if (ls_string(object, "src") != NULL && plan_photo(plan, ls_string(object, "src")) != NULL) {
			g_autofree gchar *open = slot != NULL ? g_strdup_printf("{{#%s}}", slot) : NULL;
			if (markup == NULL || (open != NULL && strstr(markup, open) != NULL))
				return TRUE;
		}
		members = json_object_get_members(object);
		for (member = members; member != NULL; member = member->next)
			if (shows_photo(plan, json_object_get_member(object, member->data), markup, member->data))
				return TRUE;
	}
	return FALSE;
}

/* Whether a page's @sections show a supplied photo where their markup
 * (the variant's own template, else the component's) draws it. */
static gboolean
sections_show_photo(LsComponents *self, JsonObject *plan, JsonArray *sections){
	guint i;
	for (i = 0; sections != NULL && i < json_array_get_length(sections); i++) {
		JsonNode *node = json_array_get_element(sections, i);
		JsonObject *section = JSON_NODE_HOLDS_OBJECT(node) ? json_node_get_object(node) : NULL;
		const gchar *component = ls_string(section, "component"), *variant = ls_string(section, "variant");
		g_autofree gchar *own = component != NULL && variant != NULL ? g_strdup_printf("%s--%s.html", component, variant) : NULL;
		g_autofree gchar *base = component != NULL ? g_strconcat(component, ".html", NULL) : NULL;
		const gchar *markup = own != NULL && file_text(self, own) != NULL ? file_text(self, own)
			: base != NULL ? file_text(self, base) : NULL;
		if (section != NULL && shows_photo(plan, json_object_get_member(section, "content"), markup, NULL))
			return TRUE;
	}
	return FALSE;
}

gboolean
ls_components_ensure_photo(LsComponents *self, JsonNode *plan_node, const gchar *path, JsonNode *page){
	JsonObject *plan, *section, *content, *art, *photo;
	JsonArray *photos, *sections;
	g_return_val_if_fail(LS_IS_COMPONENTS(self), FALSE);
	if (plan_node == NULL || !JSON_NODE_HOLDS_OBJECT(plan_node) || page == NULL || !JSON_NODE_HOLDS_OBJECT(page) ||
		path == NULL || photoless_route(path))
		return FALSE;
	plan = json_node_get_object(plan_node);
	photos = member_array(plan, "photos");
	sections = member_array(json_node_get_object(page), "sections");
	if (photos == NULL || json_array_get_length(photos) == 0 || sections == NULL ||
		json_array_get_length(sections) == 0 || json_array_get_length(sections) >= MAX_SECTIONS ||
		sections_show_photo(self, plan, sections))
		return FALSE;
	/* The page's place in the plan picks the photo, so neighbouring inner
	 * pages differ, from those the home page's visual sequence does not
	 * already show. */
	{
		JsonObject *media = member_object(plan, "media");
		g_autoptr(GPtrArray) spare = g_ptr_array_new();
		guint i;
		g_autoptr(GPtrArray) unshown = g_ptr_array_new();
		g_autofree gchar *spec = ls_json_encode(json_object_get_member(json_node_get_object(page), "sections"));
		for (i = 0; i < json_array_get_length(photos); i++) {
			const gchar *candidate = ls_string(json_array_get_object_element(photos, i), "path");
			g_autofree gchar *quoted = g_strdup_printf("\"%s\"", candidate != NULL ? candidate : "");
			if (media == NULL || (g_strcmp0(candidate, ls_string(media, "hero")) != 0 &&
				g_strcmp0(candidate, ls_string(media, "detail")) != 0 && g_strcmp0(candidate, ls_string(media, "proof")) != 0))
				g_ptr_array_add(spare, json_array_get_object_element(photos, i));
			/* A photo the page names in a slot it does not draw stays
			 * out, so it can never show twice. */
			if (strstr(spec, quoted) == NULL)
				g_ptr_array_add(unshown, json_array_get_object_element(photos, i));
		}
		if (unshown->len > 0 && unshown->len < json_array_get_length(photos)) {
			g_ptr_array_set_size(spare, 0);
			g_ptr_array_extend(spare, unshown, NULL, NULL);
		}
		photo = spare->len > 0 ? g_ptr_array_index(spare, page_place(plan, path) % spare->len)
			: json_array_get_object_element(photos, page_place(plan, path) % json_array_get_length(photos));
	}
	art = json_object_new();
	json_object_set_string_member(art, "src", ls_string(photo, "path"));
	json_object_set_string_member(art, "alt", ls_string(photo, "alt") != NULL ? ls_string(photo, "alt") : "");
	content = json_object_new();
	json_object_set_object_member(content, "art", art);
	section = json_object_new();
	json_object_set_string_member(section, "component", "media");
	json_object_set_string_member(section, "variant", "full");
	json_object_set_object_member(section, "content", content);
	{
		JsonArray *rebuilt = json_array_new();
		guint i;
		for (i = 0; i < json_array_get_length(sections); i++) {
			json_array_add_element(rebuilt, json_node_copy(json_array_get_element(sections, i)));
			if (i == 0)
				json_array_add_object_element(rebuilt, json_object_ref(section));
		}
		json_object_set_array_member(json_node_get_object(page), "sections", rebuilt);
	}
	json_object_unref(section);
	return TRUE;
}

/* Visible words of @html between @start and @end: tags, scripts and styles
 * dropped, alt text kept. */
static gchar *
visible_text(const gchar *start, const gchar *end){
	GString *out = g_string_new(NULL);
	const gchar *p = start;
	while (p < end) {
		if (*p != '<') {
			g_string_append_c(out, *p++);
			continue;
		}
		if (g_ascii_strncasecmp(p, "<script", 7) == 0 || g_ascii_strncasecmp(p, "<style", 6) == 0) {
			const gchar *close = strstr(p, p[1] == 's' && p[2] == 'c' ? "</script>" : "</style>");
			p = close != NULL && close < end ? close + 8 : end;
			continue;
		}
		{
			const gchar *close = strchr(p, '>');
			const gchar *alt = g_strstr_len(p, close != NULL ? close - p : end - p, " alt=\"");
			if (alt != NULL) {
				const gchar *q = strchr(alt + 6, '"');
				if (q != NULL) {
					g_string_append_c(out, ' ');
					g_string_append_len(out, alt + 6, q - alt - 6);
				}
			}
			g_string_append_c(out, ' ');
			p = close != NULL && close < end ? close + 1 : end;
		}
	}
	return g_string_free(out, FALSE);
}

/* Whether @html carries the class @name: whole class names only. */
static gboolean
has_class(const gchar *html, const gchar *name){
	const gchar *at;
	gsize length = strlen(name);
	for (at = strstr(html, name); at != NULL; at = strstr(at + 1, name))
		if ((at[-1] == '"' || at[-1] == ' ') && (at[length] == '"' || at[length] == ' '))
			return TRUE;
	return FALSE;
}

/* The supplied photo whose path or sized variant is @name, or %NULL. */
static const gchar *
photo_named(JsonObject *plan, const gchar *name){
	JsonArray *photos = member_array(plan, "photos");
	guint i, j;
	for (i = 0; photos != NULL && i < json_array_get_length(photos); i++) {
		JsonObject *photo = json_array_get_object_element(photos, i);
		JsonArray *sized = member_array(photo, "variants");
		if (g_strcmp0(ls_string(photo, "path"), name) == 0)
			return ls_string(photo, "path");
		for (j = 0; sized != NULL && j < json_array_get_length(sized); j++)
			if (g_strcmp0(ls_string(json_array_get_object_element(sized, j), "path"), name) == 0)
				return ls_string(photo, "path");
	}
	return NULL;
}

/* The first URL of the @attribute (src or srcset) in the tag at @tag. */
static gchar *
tag_url(const gchar *tag, const gchar *attribute){
	const gchar *close = strchr(tag, '>');
	g_autofree gchar *needle = g_strdup_printf(" %s=\"", attribute);
	const gchar *at = strstr(tag, needle);
	if (at == NULL || close == NULL || at > close)
		return NULL;
	at += strlen(needle);
	return g_strndup(at, strcspn(at, "\" ,"));
}

/* Counts each supplied photo @html shows into @page: an img once, and a
 * phone source of its picture only when it shows another photo. */
static void
count_page_photos(JsonObject *plan, const gchar *html, GHashTable *page){
	g_autoptr(GPtrArray) pending = g_ptr_array_new();
	const gchar *p;
	for (p = strchr(html, '<'); p != NULL; p = strchr(p + 1, '<')) {
		gboolean source = g_str_has_prefix(p, "<source "), img = g_str_has_prefix(p, "<img ");
		g_autofree gchar *url = source ? tag_url(p, "srcset") : img ? tag_url(p, "src") : NULL;
		const gchar *photo = url != NULL ? photo_named(plan, url) : NULL;
		guint i;
		if (g_str_has_prefix(p, "<picture"))
			g_ptr_array_set_size(pending, 0);
		if (photo == NULL)
			continue;
		if (source) {
			g_ptr_array_add(pending, (gpointer)photo);
			continue;
		}
		g_hash_table_insert(page, (gpointer)photo, GUINT_TO_POINTER(GPOINTER_TO_UINT(g_hash_table_lookup(page, photo)) + 1));
		for (i = 0; i < pending->len; i++)
			if (g_strcmp0(g_ptr_array_index(pending, i), photo) != 0) {
				const gchar *other = g_ptr_array_index(pending, i);
				g_hash_table_insert(page, (gpointer)other, GUINT_TO_POINTER(GPOINTER_TO_UINT(g_hash_table_lookup(page, other)) + 1));
				break;
			}
		g_ptr_array_set_size(pending, 0);
	}
}

static void
taste_page(LsComponents *self, JsonObject *plan, const gchar *path, const gchar *html, GString *problems){
	const gchar *body = strstr(html, "<body class=\"");
	const gchar *footer = strstr(html, "<footer");
	const gchar *footer_end = footer != NULL ? strstr(footer, "</footer>") : NULL;
	const gchar *h1 = strstr(html, "<h1");
	const gchar *end = html + strlen(html);
	const gchar *talk;
	g_autofree gchar *main_text = visible_text(html, footer != NULL ? footer : end);
	g_autofree gchar *footer_text = footer != NULL ? visible_text(footer, footer_end != NULL ? footer_end : end) : NULL;
	g_autofree gchar *after_text = footer_end != NULL ? visible_text(footer_end, end) : NULL;
	if ((talk = ls_taste_disclosure(main_text, FALSE)) != NULL || (talk = ls_taste_disclosure(after_text, FALSE)) != NULL)
		g_string_append_printf(problems, "%s: visible copy talks about the site itself (\"%s\")\n", path, talk);
	if ((talk = ls_taste_disclosure(footer_text, TRUE)) != NULL ||
		(!plan_is_demo(plan) && (talk = ls_taste_example(footer_text)) != NULL))
		g_string_append_printf(problems, "%s: the footer says \"%s\"\n", path, talk);
	if (h1 != NULL && strstr(h1, "</h1>") != NULL) {
		const gchar *open_end = strchr(h1, '>');
		g_autofree gchar *words = visible_text(open_end + 1, strstr(h1, "</h1>"));
		g_strstrip(words);
		if (g_str_has_suffix(words, "?"))
			g_string_append_printf(problems, "%s: the headline \"%s\" is a question\n", path, words);
	}
	/* Letter chips: a letter index over fewer letters than earn one. */
	{
		const gchar *index;
		for (index = strstr(html, "class=\"directory__letters\""); index != NULL;
			index = strstr(index + 1, "class=\"directory__letters\"")) {
			const gchar *close = strstr(index, "</nav>");
			guint count = 0;
			const gchar *link;
			for (link = strstr(index, "<a "); link != NULL && close != NULL && link < close; link = strstr(link + 1, "<a "))
				count++;
			if (count < DIRECTORY_INDEX_LETTERS)
				g_string_append_printf(problems, "%s: letter chips (%u letters standing in for content)\n", path, count);
		}
	}
	if (self->taste != NULL) {
		JsonArray *variants = member_array(self->taste, "variants");
		g_autofree gchar *classes = NULL;
		g_auto(GStrv) names = NULL;
		guint i;
		if (body != NULL) {
			const gchar *close = strchr(body + 13, '"');
			classes = close != NULL ? g_strndup(body + 13, close - body - 13) : NULL;
		}
		names = classes != NULL ? g_strsplit(classes, " ", -1) : NULL;
		for (i = 0; names != NULL && names[i] != NULL; i++) {
			/* A visual sequence wears its world's own frame, and a preset's
			 * look is part of its reviewed design. */
			g_autoptr(GList) axes = member_object(plan, "visual") == NULL && member_object(plan, "design") == NULL
				? json_object_get_members(self->looks) : NULL;
			GList *axis;
			for (axis = axes; axis != NULL; axis = axis->next) {
				g_autofree gchar *prefix = g_strdup_printf("look-%s-", (const gchar *)axis->data);
				if (g_str_has_prefix(names[i], prefix) &&
					g_strcmp0(names[i] + strlen(prefix), look_default(self, axis->data)) != 0)
					g_string_append_printf(problems, "%s: look %s %s is not the default design\n", path,
						(const gchar *)axis->data, names[i] + strlen(prefix));
			}
		}
		for (i = 0; variants != NULL && i < json_array_get_length(variants); i++) {
			const gchar *pair = json_array_get_string_element(variants, i);
			g_autofree gchar *component = g_strndup(pair, (gsize)(strchr(pair, '/') - pair));
			g_autofree gchar *name = g_strdup_printf("%s--%s", component, strchr(pair, '/') + 1);
			if (has_class(html, name))
				g_string_append_printf(problems, "%s: variant %s is a gimmick\n", path, pair);
		}
	}
	/* A photo site shows a photograph on every page. */
	if (member_array(plan, "photos") != NULL && json_array_get_length(member_array(plan, "photos")) > 0 &&
		!photoless_route(path)) {
		JsonArray *photos = member_array(plan, "photos");
		gboolean found = FALSE;
		guint i, j;
		for (i = 0; !found && i < json_array_get_length(photos); i++) {
			JsonObject *photo = json_array_get_object_element(photos, i);
			JsonArray *sized = member_array(photo, "variants");
			g_autofree gchar *src = g_strdup_printf("src=\"%s\"", ls_string(photo, "path"));
			found = strstr(html, src) != NULL;
			for (j = 0; !found && sized != NULL && j < json_array_get_length(sized); j++) {
				g_autofree gchar *variant = g_strdup_printf("src=\"%s\"",
					ls_string(json_array_get_object_element(sized, j), "path"));
				found = strstr(html, variant) != NULL;
			}
		}
		if (!found)
			g_string_append_printf(problems, "%s: no photograph on the page\n", path);
	}
	/* No page shows a supplied photo twice. */
	{
		g_autoptr(GHashTable) page = g_hash_table_new(g_str_hash, g_str_equal);
		GHashTableIter iter;
		gpointer photo, count;
		count_page_photos(plan, html, page);
		g_hash_table_iter_init(&iter, page);
		while (g_hash_table_iter_next(&iter, &photo, &count))
			if (GPOINTER_TO_UINT(count) > 1)
				g_string_append_printf(problems, "%s: shows the photo %s %u times; each photo once per page "
					"(shown twice or more)\n", path, (const gchar *)photo, GPOINTER_TO_UINT(count));
	}
}

/* The brightest channel of a #rrggbb colour, 0 to 1. */
static gdouble
taste_lightness(const gchar *hex){
	gdouble rgb[3];
	if (!parse_color(hex, rgb))
		return 0;
	return MAX(MAX(rgb[0], rgb[1]), rgb[2]);
}

gboolean
ls_components_check_taste(LsComponents *self, JsonNode *plan_node, JsonNode *bundle, GString *problems){
	JsonObject *plan;
	JsonArray *files;
	gsize before;
	guint i;
	g_return_val_if_fail(LS_IS_COMPONENTS(self), FALSE);
	g_return_val_if_fail(problems != NULL, FALSE);
	before = problems->len;
	if (plan_node == NULL || !JSON_NODE_HOLDS_OBJECT(plan_node) || bundle == NULL || !JSON_NODE_HOLDS_OBJECT(bundle)) {
		g_string_append(problems, "the plan and bundle must be JSON objects\n");
		return FALSE;
	}
	plan = json_node_get_object(plan_node);
	files = member_array(json_node_get_object(bundle), "files");
	for (i = 0; files != NULL && i < json_array_get_length(files); i++) {
		JsonObject *file = json_array_get_object_element(files, i);
		const gchar *path = ls_string(file, "path"), *content = ls_string(file, "content");
		if (path == NULL || content == NULL || ls_string(file, "encoding") != NULL)
			continue;
		if (g_str_has_suffix(path, ".html"))
			taste_page(self, plan, path, content, problems);
		else if (g_str_equal(path, "styles.css") && self->taste != NULL) {
			const gchar *keys[] = {"--paper:", "--surface:"};
			guint k;
			for (k = 0; k < G_N_ELEMENTS(keys); k++) {
				const gchar *at;
				for (at = strstr(content, keys[k]); at != NULL; at = strstr(at + 1, keys[k])) {
					const gchar *value = at + strlen(keys[k]);
					g_autofree gchar *hex = NULL;
					while (*value == ' ')
						value++;
					if (*value != '#')
						continue;
					hex = g_strndup(value, 7);
					/* Deep colours stay calm however saturated; only a light,
					 * coloured ground is loud. */
					if (ls_taste_chroma(hex) > json_object_get_double_member(self->taste, "chroma") &&
						taste_lightness(hex) >= 0.45)
						g_string_append_printf(problems, "styles.css: %s %s is a loud page colour\n", keys[k], hex);
				}
			}
		}
	}
	return problems->len == before;
}

guint
ls_components_count_photos(LsComponents *self, JsonNode *plan_node, JsonNode *bundle){
	g_autoptr(GHashTable) shown = g_hash_table_new(g_str_hash, g_str_equal);
	JsonArray *files;
	guint i;
	g_return_val_if_fail(LS_IS_COMPONENTS(self), 0);
	if (plan_node == NULL || !JSON_NODE_HOLDS_OBJECT(plan_node) || bundle == NULL || !JSON_NODE_HOLDS_OBJECT(bundle))
		return 0;
	files = member_array(json_node_get_object(bundle), "files");
	for (i = 0; files != NULL && i < json_array_get_length(files); i++) {
		JsonObject *file = json_array_get_object_element(files, i);
		const gchar *path = ls_string(file, "path"), *content = ls_string(file, "content");
		if (path != NULL && content != NULL && ls_string(file, "encoding") == NULL && g_str_has_suffix(path, ".html"))
			count_page_photos(json_node_get_object(plan_node), content, shown);
	}
	return g_hash_table_size(shown);
}
