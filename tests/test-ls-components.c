/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "core/ls-components.h"
#include "core/ls-util.h"
#include <string.h>
#include <math.h>

#define FIXTURE "tests/fixtures/composed/"

static JsonNode *
load(const gchar *name){
	g_autofree gchar *path = g_strconcat(FIXTURE, name, NULL);
	g_autofree gchar *text = NULL;
	g_assert_true(g_file_get_contents(path, &text, NULL, NULL));
	return ls_json_parse(text, NULL);
}

static void contains(const gchar *text, const gchar *needle);
static void lacks(const gchar *text, const gchar *needle);

static LsComponents *
library(void){
	g_autoptr(GError) error = NULL;
	LsComponents *components = ls_components_new(&error);
	g_assert_no_error(error);
	g_assert_nonnull(components);
	return components;
}

/* Parse a JSON literal written with single quotes for readability. */
static JsonNode *
json(const gchar *text){
	g_autofree gchar *copy = g_strdup(text);
	gchar *p;
	for (p = copy; *p != '\0'; p++)
		if (*p == '\'')
			*p = '"';
	return ls_json_parse(copy, NULL);
}

static JsonObject *
fixture_pages(void){
	JsonObject *pages = json_object_new();
	json_object_set_member(pages, "index.html", load("index.json"));
	json_object_set_member(pages, "work.html", load("work.json"));
	json_object_set_member(pages, "start.html", load("start.json"));
	json_object_set_member(pages, "journal.html", load("journal.json"));
	json_object_set_member(pages, "visit.html", load("visit.json"));
	return pages;
}

/* The embedded catalog loads, is fingerprinted and describes every component. */
static void
test_catalog(void){
	g_autoptr(LsComponents) components = library();
	g_autofree gchar *reference = ls_components_describe(components);
	const gchar *names[] = {"hero", "text", "article", "features", "cards", "filter", "media",
		"quote", "cta", "faq", "facts", "pricing", "gallery", "form", "page-header", "lead", "figure",
		"callout", "table", "menu", "timeline", "entries", "pager", "hours", "location", "team", "steps",
		"columns", "glossary", "schedule", "links", "custom"};
	guint i;
	g_assert_cmpuint(strlen(ls_components_get_digest(components)), ==, 64);
	g_assert_nonnull(strstr(reference, "Rich text"));
	g_assert_nonnull(strstr(reference, "[label](href)"));
	for (i = 0; i < G_N_ELEMENTS(names); i++) {
		g_autofree gchar *heading = g_strdup_printf("\n%s: ", names[i]);
		g_assert_nonnull(strstr(reference, heading));
	}
	g_assert_nonnull(strstr(reference, "statement"));
	g_assert_nonnull(strstr(reference, "PAGE KINDS"));
	g_assert_nonnull(strstr(reference, "\narticle: "));
	g_assert_null(strstr(reference, "\nutility: "));
	g_assert_nonnull(strstr(reference, "required"));
}

/* Response schemas: the plan's is embedded, the page's lists every component. */
static void
test_schemas(void){
	g_autoptr(LsComponents) components = library();
	g_autoptr(JsonNode) plan = ls_json_parse(ls_components_get_plan_schema(components), NULL);
	g_autofree gchar *page_text = ls_components_dup_page_schema(components);
	g_autoptr(JsonNode) page = ls_json_parse(page_text, NULL);
	g_autofree gchar *art_text = ls_components_dup_art_schema(components);
	g_autoptr(JsonNode) art = ls_json_parse(art_text, NULL);
	g_assert_nonnull(plan);
	g_assert_nonnull(page);
	g_assert_nonnull(art);
	g_assert_nonnull(strstr(ls_components_get_plan_schema(components), "\"pages\""));
	g_assert_nonnull(strstr(page_text, "\"hero\""));
	g_assert_nonnull(strstr(page_text, "\"custom\""));
	g_assert_nonnull(strstr(page_text, "\"sections\""));
	g_assert_nonnull(strstr(art_text, "\"svg\""));
}

static void
test_plan_valid(void){
	g_autoptr(LsComponents) components = library();
	g_autoptr(JsonNode) plan = load("plan.json");
	g_autoptr(GError) error = NULL;
	g_assert_true(ls_components_check_plan(components, plan, &error));
	g_assert_no_error(error);
}

static void
expect_plan_error(LsComponents *components, JsonNode *plan, const gchar *const *needles){
	g_autoptr(GError) error = NULL;
	guint i;
	g_assert_false(ls_components_check_plan(components, plan, &error));
	g_assert_error(error, LS_ERROR, LS_ERROR_INVALID);
	for (i = 0; needles[i] != NULL; i++)
		if (strstr(error->message, needles[i]) == NULL)
			g_error("plan error lacks '%s': %s", needles[i], error->message);
}

/* Every problem is reported at once, with the rule the model has to satisfy. */
static void
test_plan_errors(void){
	g_autoptr(LsComponents) components = library();
	g_autoptr(JsonNode) plan = load("plan.json");
	JsonObject *root = json_node_get_object(plan);
	JsonObject *tokens = json_object_get_object_member(root, "tokens");
	JsonArray *pages = json_object_get_array_member(root, "pages");
	JsonArray *art = json_object_get_array_member(root, "art");
	const gchar *contrast[] = {"muted", "4.5", NULL};
	const gchar *several[] = {"about.html", "404.html", "assets/bench.png", "@import", "index.html",
		"accent_ink", NULL};
	json_object_set_string_member(tokens, "muted", "#b8b0a2");
	expect_plan_error(components, plan, contrast);
	json_object_set_string_member(tokens, "muted", "#57524a");
	json_object_set_string_member(tokens, "accent_ink", "#2a5c4e");
	json_object_set_string_member(json_array_get_object_element(
		json_object_get_array_member(root, "nav"), 0), "href", "about.html");
	json_object_set_string_member(json_array_get_object_element(pages, 0), "path", "404.html");
	json_object_set_string_member(json_array_get_object_element(art, 1), "path", "assets/bench.png");
	json_object_set_string_member(root, "css", "@import url(x.css);");
	json_object_set_string_member(json_array_get_object_element(pages, 1), "kind", "home");
	json_object_set_string_member(json_array_get_object_element(pages, 2), "kind", "landing");
	expect_plan_error(components, plan, several);
	{
		const gchar *kinds[] = {"only index.html may be kind home", "kind must be one of home, index", NULL};
		expect_plan_error(components, plan, kinds);
	}
	{
		JsonObject *look = json_object_get_object_member(root, "look");
		JsonObject *restyle = json_object_get_object_member(root, "restyle");
		const gchar *looks[] = {"look.header must be one of the listed values", "look.sparkle is not a look axis",
			"restyle.banner is not a component", "restyle.hero: ", NULL};
		json_object_set_string_member(look, "header", "wobbly");
		json_object_set_string_member(look, "sparkle", "on");
		json_object_set_string_member(restyle, "banner", "a { color: red; }");
		json_object_set_string_member(restyle, "hero", "@import url(x.css);");
		expect_plan_error(components, plan, looks);
	}
}

/* Mechanical misses are repaired natively instead of costing a model retry:
 * colors move minimally until AA holds and numbers clamp into range. */
static void
test_plan_repair(void){
	g_autoptr(LsComponents) components = library();
	g_autoptr(JsonNode) plan = load("plan.json");
	JsonObject *tokens = json_object_get_object_member(json_node_get_object(plan), "tokens");
	g_autoptr(GError) error = NULL;
	g_autofree gchar *notes = NULL;
	g_autofree gchar *again = NULL;
	json_object_set_string_member(tokens, "muted", "#9a9288");
	json_object_set_string_member(tokens, "accent", "#e0562e");
	json_object_set_string_member(tokens, "accent_ink", "#fff8f0");
	json_object_set_double_member(tokens, "ratio", 2.4);
	json_object_set_double_member(tokens, "measure", 120);
	json_object_remove_member(json_array_get_object_element(json_object_get_array_member(
		json_node_get_object(plan), "pages"), 1), "kind");
	g_assert_false(ls_components_check_plan(components, plan, NULL));
	notes = ls_components_repair_plan(components, plan);
	g_assert_nonnull(notes);
	g_assert_nonnull(strstr(notes, "muted"));
	g_assert_nonnull(strstr(notes, "ratio"));
	g_assert_nonnull(strstr(notes, "work.html kind -> general"));
	g_assert_nonnull(strstr(notes, "look.nav -> underline"));
	lacks(notes, "look.header");
	g_assert_true(ls_components_check_plan(components, plan, &error));
	g_assert_no_error(error);
	g_assert_cmpstr(ls_string(tokens, "accent_ink"), !=, NULL);
	/* A valid plan is left exactly as it was. */
	again = ls_components_repair_plan(components, plan);
	g_assert_null(again);
}

static void
test_page_valid(void){
	g_autoptr(LsComponents) components = library();
	g_autoptr(JsonNode) plan = load("plan.json");
	const gchar *names[] = {"index", "work", "start", "journal", "visit"};
	guint i;
	for (i = 0; i < G_N_ELEMENTS(names); i++) {
		g_autoptr(GError) error = NULL;
		g_autofree gchar *file = g_strconcat(names[i], ".json", NULL);
		g_autofree gchar *path = g_strconcat(names[i], ".html", NULL);
		g_autoptr(JsonNode) page = load(file);
		g_assert_true(ls_components_check_page(components, plan, path, page, &error));
		g_assert_no_error(error);
	}
}

typedef struct {
	const gchar *page;
	const gchar *needle;
} PageCase;

/* Each specification error names the section, slot and the allowed values. */
static void
test_page_errors(void){
	g_autoptr(LsComponents) components = library();
	g_autoptr(JsonNode) plan = load("plan.json");
	const PageCase cases[] = {
		{"{'path':'work.html','sections':[{'component':'hero','content':{'heading':'x'}}]}", "index.html"},
		{"{'path':'index.html','sections':[{'component':'banner','content':{}}]}", "unknown component 'banner'"},
		{"{'path':'index.html','sections':[{'component':'hero','variant':'huge','content':{'heading':'x'}}]}", "variant 'huge'"},
		{"{'path':'index.html','sections':[{'component':'hero','content':{'heading':'x','subtitle':'y'}}]}", "unknown slot 'subtitle'"},
		{"{'path':'index.html','sections':[{'component':'hero','content':{'lede':'x'}}]}", "heading is required"},
		{"{'path':'index.html','sections':[{'component':'hero','content':{'heading':'x','actions':[{'href':'about.html','label':'A'}]}}]}", "about.html"},
		{"{'path':'index.html','sections':[{'component':'hero','content':{'heading':'x','lede':'[a](#nope)'}}]}", "#nope"},
		{"{'path':'index.html','sections':[{'component':'hero','variant':'split','content':{'heading':'x'}}]}", "art is required"},
		{"{'path':'index.html','sections':[{'component':'hero','content':{'heading':'x','art':{'src':'assets/other.svg','alt':''}}}]}", "assets/other.svg"},
		{"{'path':'index.html','sections':[{'component':'quote','content':{'quote':'x'}},{'component':'hero','content':{'heading':'x'}}]}", "first section"},
		{"{'path':'index.html','sections':[{'component':'hero','content':{'heading':'x'}},{'component':'custom','content':{'label':'c','html':'<div onclick=\\\"x()\\\"><script>x()</script></div>'}}]}", "onclick"},
		{"{'path':'index.html','sections':[{'component':'hero','content':{'heading':'x'}},{'component':'filter','content':{'all_label':'All','empty':'e','categories':[{'label':'A'},{'label':'B'}],'items':[{'title':'t','category':'C'}]}}]}", "category 'C'"},
		{"{'path':'index.html','sections':[{'component':'hero','content':{'heading':'x'}}],'css':'a{color:red}</style>'}", "css"},
		{"{'path':'index.html','sections':[{'component':'hero','content':{'heading':'x','lede':'[a](javascript:alert(1))'}}]}", "javascript:"},
	};
	const PageCase kind_cases[] = {
		{"{'path':'work.html','sections':[{'component':'hero','content':{'heading':'x'}}]}", "hero belongs only on the home page"},
		{"{'path':'work.html','sections':[{'component':'text','content':{'heading':'x','body':'y'}}]}", "an index page opens with page-header"},
		{"{'path':'work.html','sections':[{'component':'page-header','content':{'heading':'x'}},{'component':'hero','content':{'heading':'y'}}]}", "hero belongs only"},
		{"{'path':'work.html','sections':[{'component':'page-header','content':{'heading':'x'}},{'component':'cover','content':{'heading':'y','art':{'src':'assets/bench.svg','alt':''}}}]}", "cover belongs only on the home page"},
		{"{'path':'work.html','sections':[{'component':'hero','content':{'heading':'x'}}]}", "opens with page-header"},
		{"{'path':'work.html','aside':'[x](nowhere.html)','sections':[{'component':'page-header','content':{'heading':'x'}}]}", "nowhere.html"},
	};
	guint i;
	for (i = 0; i < G_N_ELEMENTS(kind_cases); i++) {
		g_autoptr(GError) error = NULL;
		g_autoptr(JsonNode) page = json(kind_cases[i].page);
		g_assert_false(ls_components_check_page(components, plan, "work.html", page, &error));
		if (strstr(error->message, kind_cases[i].needle) == NULL)
			g_error("kind case %u error lacks '%s': %s", i, kind_cases[i].needle, error->message);
	}
	for (i = 0; i < G_N_ELEMENTS(cases); i++) {
		g_autoptr(GError) error = NULL;
		g_autoptr(JsonNode) page = json(cases[i].page);
		g_assert_nonnull(page);
		g_assert_false(ls_components_check_page(components, plan, "index.html", page, &error));
		g_assert_error(error, LS_ERROR, LS_ERROR_INVALID);
		if (strstr(error->message, cases[i].needle) == NULL)
			g_error("case %u error lacks '%s': %s", i, cases[i].needle, error->message);
	}
}

static void
test_rich(void){
	g_autoptr(GError) error = NULL;
	g_autofree gchar *html = ls_components_render_rich(
		"One **two** *three* `four` [five](work.html) <script>\n\n- a\n- b\n\n1. c\n2. d", &error);
	g_autofree gchar *bad = NULL;
	g_assert_no_error(error);
	g_assert_cmpstr(html, ==, "<p>One <strong>two</strong> <em>three</em> <code>four</code> "
		"<a href=\"work.html\">five</a> &lt;script&gt;</p>\n<ul><li>a</li><li>b</li></ul>\n"
		"<ol><li>c</li><li>d</li></ol>\n");
	bad = ls_components_render_rich("[x](javascript:alert(1))", &error);
	g_assert_null(bad);
	g_assert_error(error, LS_ERROR, LS_ERROR_INVALID);
}

static void
test_art(void){
	const gchar *bad[] = {
		"<svg viewBox=\"0 0 1 1\"></svg>",
		"<svg xmlns=\"http://www.w3.org/2000/svg\"></svg>",
		"<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 1 1\"><script>x</script></svg>",
		"<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 1 1\" onload=\"x()\"></svg>",
		"<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 1 1\"><use href=\"https://x/y.svg#a\"/></svg>",
		"<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 1 1\"><rect style=\"fill:url(https://x)\"/></svg>",
		"<html xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 1 1\"></html>",
		"<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 1 1\"><rect></svg>",
		/* Lettering renders in fallback fonts inside <img>: none at all. */
		"<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 9 9\"><text x=\"1\" y=\"8\">VN</text></svg>",
		"<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 9 9\"><g><TEXT><tspan>A</tspan></TEXT></g></svg>",
		"<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 9 9\"><svg:textPath href=\"#p\">B</svg:textPath></svg>",
	};
	g_autofree gchar *good = NULL;
	g_autoptr(GError) error = NULL;
	guint i;
	g_assert_true(g_file_get_contents(FIXTURE "mark.svg", &good, NULL, NULL));
	g_assert_true(ls_components_check_art(good, &error));
	g_assert_no_error(error);
	g_assert_true(ls_components_check_art("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 9 9\">"
		"<defs><linearGradient id=\"g\"/></defs><rect fill=\"url(#g)\" width=\"9\" height=\"9\"/>"
		"<use href=\"#g\"/></svg>", NULL));
	for (i = 0; i < G_N_ELEMENTS(bad); i++) {
		g_autoptr(GError) art_error = NULL;
		if (ls_components_check_art(bad[i], &art_error))
			g_error("art case %u was accepted", i);
		g_assert_error(art_error, LS_ERROR, LS_ERROR_INVALID);
	}
}

static const gchar *
file_content(JsonNode *bundle, const gchar *path){
	JsonArray *files = json_object_get_array_member(json_node_get_object(bundle), "files");
	guint i;
	for (i = 0; i < json_array_get_length(files); i++) {
		JsonObject *file = json_array_get_object_element(files, i);
		if (g_str_equal(ls_string(file, "path"), path))
			return ls_string(file, "content");
	}
	return NULL;
}

static guint
count(const gchar *haystack, const gchar *needle){
	guint n = 0;
	const gchar *p = haystack;
	while ((p = strstr(p, needle)) != NULL) {
		n++;
		p += strlen(needle);
	}
	return n;
}

static void
contains(const gchar *text, const gchar *needle){
	if (text == NULL || strstr(text, needle) == NULL)
		g_error("missing '%s' in:\n%.2000s", needle, text != NULL ? text : "(null)");
}

static void
lacks(const gchar *text, const gchar *needle){
	g_assert_nonnull(text);
	if (strstr(text, needle) != NULL)
		g_error("unexpected '%s' in:\n%.2000s", needle, text);
}

/* The rendered site is complete, accessible by construction and self-contained. */
static void
test_compose(void){
	g_autoptr(LsComponents) components = library();
	g_autoptr(JsonNode) plan = load("plan.json");
	g_autoptr(JsonObject) pages = fixture_pages();
	g_autoptr(GError) error = NULL;
	g_autofree gchar *text = ls_components_compose(components, plan, pages, "https://example.com/", &error);
	g_autoptr(JsonNode) bundle = NULL;
	const gchar *paths[] = {"index.html", "work.html", "start.html", "journal.html", "visit.html",
		"404.html", "privacy.html", "terms.html", "styles.css", "site.js", "robots.txt", "sitemap.xml"};
	const gchar *index, *work, *start, *privacy, *css, *js;
	guint i;
	g_assert_no_error(error);
	bundle = ls_json_parse(text, &error);
	g_assert_no_error(error);
	for (i = 0; i < G_N_ELEMENTS(paths); i++)
		g_assert_nonnull(file_content(bundle, paths[i]));
	g_assert_cmpuint(json_array_get_length(json_object_get_array_member(
		json_node_get_object(bundle), "files")), ==, G_N_ELEMENTS(paths));
	index = file_content(bundle, "index.html");
	work = file_content(bundle, "work.html");
	start = file_content(bundle, "start.html");
	privacy = file_content(bundle, "privacy.html");
	css = file_content(bundle, "styles.css");
	js = file_content(bundle, "site.js");
	for (i = 0; i < 8; i++) {
		const gchar *page = file_content(bundle, paths[i]);
		g_assert_cmpuint(count(page, "<h1"), ==, 1);
		g_assert_cmpuint(count(page, "<main"), ==, 1);
		contains(page, "class=\"skip-link\" href=\"#main\"");
		contains(page, "<html lang=\"en\">");
		contains(page, "<meta name=\"description\" content=\"");
		contains(page, "<nav class=\"site-nav\" id=\"site-nav\"");
		contains(page, "href=\"work.html\"");
		contains(page, "href=\"privacy.html\"");
		lacks(page, "src=\"http");
		lacks(page, "{{");
	}
	/* The primary navigation is never folded away: no disclosure, visible links. */
	lacks(index, "<details");
	contains(index, "class=\"nav-toggle\" type=\"button\" aria-expanded=\"false\" aria-controls=\"site-nav\" hidden");
	contains(index, "<title>Furniture repair in Harbor County | Harbor &amp; Pine</title>");
	contains(index, "<link rel=\"canonical\" href=\"https://example.com/\">");
	contains(index, "class=\"brand\" href=\"index.html\" aria-current=\"page\"");
	contains(index, "<strong>structure</strong>");
	contains(index, "id=\"how\"");
	contains(index, "href=\"#how\"");
	contains(index, "fetchpriority=\"high\"");
	contains(index, "<ol class=\"features__list\"");
	contains(index, "<em>No real customers are shown.</em>");
	lacks(index, "noindex");
	contains(work, "<a href=\"work.html\" aria-current=\"page\">Work</a>");
	contains(work, "<link rel=\"canonical\" href=\"https://example.com/work.html\">");
	contains(work, "data-filter-item=\"chairs\"");
	contains(work, "class=\"filter__controls\" role=\"group\"");
	contains(work, "<style>\n.filter__list { gap: 2rem; }");
	contains(start, "method=\"dialog\" novalidate data-local-form data-download-name=\"repair-request.txt\"");
	contains(start, "Nothing is sent.");
	contains(start, "data-form-actions hidden");
	contains(start, "aria-describedby=\"repair-request-form-problem-hint repair-request-form-problem-error\"");
	contains(start, "<option value=\"Table\">Table</option>");
	contains(privacy, "<meta name=\"robots\" content=\"noindex\">");
	/* Interior pages open with a page header and a breadcrumb, never a hero. */
	lacks(work, "block--hero");
	contains(work, "block--page-header");
	contains(work, "<nav class=\"crumbs\" aria-label=\"Breadcrumb\"><ol><li><a href=\"index.html\">Harbor &amp; Pine</a></li>"
		"<li><span aria-current=\"page\">Recent work</span></li></ol></nav>");
	contains(work, "page--kind-index");
	lacks(index, "class=\"crumbs\"");
	/* Reading kinds get an in-page contents column built from their structure. */
	{
		const gchar *journal = file_content(bundle, "journal.html");
		const gchar *visit = file_content(bundle, "visit.html");
		contains(journal, "<div class=\"wrap page-grid\">");
		contains(journal, "<li><a href=\"#what-a-repair-saves\">What a repair saves</a></li>");
		contains(journal, "<li><a href=\"#common-repairs\">Common repairs</a></li>");
		contains(journal, "page-aside__note");
		contains(journal, "<figure class=\"article__figure\">");
		contains(journal, "<ol class=\"article__notes\">");
		contains(journal, "<caption>Repair types and what they involve</caption>");
		contains(journal, "<th scope=\"col\">Repair</th>");
		contains(journal, "lead--dropcap");
		contains(journal, "pager__link--next");
		contains(journal, "<dt>Reading</dt><dd>6 minutes</dd>");
		lacks(visit, "page-grid");
		/* An item's missing field never falls through to an outer value. */
		lacks(visit, "&lt;p&gt;");
		g_assert_cmpuint(count(visit, "class=\"menu__note muted\""), ==, 1);
		contains(visit, "<span class=\"menu__dish\">Re-glue</span><span class=\"menu__leader\" aria-hidden=\"true\"></span><span class=\"menu__price\">40</span>");
		contains(visit, "<address class=\"location__address\">");
		contains(visit, "<div class=\"hours__row\"><dt>Saturday</dt><dd>10 am to 2 pm</dd></div>");
		contains(visit, "steps--row");
		contains(visit, "entries--dated");
		contains(visit, "schedule__when");
		contains(css, ".page-grid");
		contains(css, ".menu__leader");
	}
	contains(css, "--paper: #f4efe6;");
	contains(css, ".hero--split");
	contains(css, ".filter__controls");
	contains(css, ".form__form");
	contains(css, ".page--index .hero__title { max-width: 12ch; }");
	lacks(css, ".pricing__list");
	contains(js, "nav-toggle");
	contains(js, "data-filter");
	contains(js, "data-local-form");
	contains(file_content(bundle, "robots.txt"), "Sitemap: https://example.com/sitemap.xml");
	contains(file_content(bundle, "sitemap.xml"), "<loc>https://example.com/work.html</loc>");
	lacks(file_content(bundle, "sitemap.xml"), "privacy.html");
	lacks(file_content(bundle, "sitemap.xml"), "404.html");
}

/* Planned open-source fonts load from Bunny Fonts; the URL is built natively. */
static void
test_fonts(void){
	g_autoptr(LsComponents) components = library();
	g_autoptr(JsonNode) plan = load("plan.json");
	g_autoptr(JsonObject) pages = fixture_pages();
	g_autoptr(GError) error = NULL;
	g_autoptr(JsonNode) bad = json("[{'family':'Evil\\\"><script>','weights':[400]}]");
	g_autofree gchar *text = NULL;
	g_autoptr(JsonNode) bundle = NULL;
	json_object_set_member(json_node_get_object(plan), "fonts",
		json("[{'family':'Fraunces','weights':[400,700],'italic':true},{'family':'IBM Plex Sans','weights':[500]}]"));
	text = ls_components_compose(components, plan, pages, NULL, &error);
	g_assert_no_error(error);
	bundle = ls_json_parse(text, NULL);
	contains(file_content(bundle, "index.html"), "<link rel=\"stylesheet\" href=\"https://fonts.bunny.net/css?"
		"family=fraunces:400,400i,700,700i|ibm-plex-sans:500&amp;display=optional\">");
	json_object_set_member(json_node_get_object(plan), "fonts", json_node_copy(bad));
	g_assert_false(ls_components_check_plan(components, plan, &error));
	g_assert_error(error, LS_ERROR, LS_ERROR_INVALID);
	g_assert_nonnull(strstr(error->message, "fonts"));
}

/* Without a real address there are no canonicals and no sitemap. */
static void
test_compose_preview(void){
	g_autoptr(LsComponents) components = library();
	g_autoptr(JsonNode) plan = load("plan.json");
	g_autoptr(JsonObject) pages = fixture_pages();
	g_autoptr(GError) error = NULL;
	g_autofree gchar *text = ls_components_compose(components, plan, pages, NULL, &error);
	g_autoptr(JsonNode) bundle = NULL;
	g_assert_no_error(error);
	bundle = ls_json_parse(text, NULL);
	lacks(file_content(bundle, "index.html"), "canonical");
	g_assert_null(file_content(bundle, "sitemap.xml"));
	lacks(file_content(bundle, "robots.txt"), "Sitemap");
}

/* Composition refuses a missing or invalid page instead of emitting a partial site. */
static void
test_compose_errors(void){
	g_autoptr(LsComponents) components = library();
	g_autoptr(JsonNode) plan = load("plan.json");
	g_autoptr(JsonObject) pages = fixture_pages();
	g_autoptr(GError) error = NULL;
	g_autofree gchar *missing = NULL, *http = NULL;
	json_object_remove_member(pages, "work.html");
	missing = ls_components_compose(components, plan, pages, NULL, &error);
	g_assert_null(missing);
	g_assert_error(error, LS_ERROR, LS_ERROR_INVALID);
	g_assert_nonnull(strstr(error->message, "work.html"));
	g_clear_error(&error);
	json_object_set_member(pages, "work.html", load("work.json"));
	http = ls_components_compose(components, plan, pages, "http://example.com/", &error);
	g_assert_null(http);
	g_assert_error(error, LS_ERROR, LS_ERROR_INVALID);
}

/* Model text is data: markup in plain slots is escaped, never interpreted. */
static void
test_escaping(void){
	g_autoptr(LsComponents) components = library();
	g_autoptr(JsonNode) plan = load("plan.json");
	g_autoptr(JsonObject) pages = fixture_pages();
	g_autoptr(GError) error = NULL;
	g_autofree gchar *text = NULL;
	g_autoptr(JsonNode) bundle = NULL;
	json_object_set_member(pages, "work.html", json(
		"{'path':'work.html','sections':[{'component':'page-header','content':{'heading':'<img src=x onerror=alert(1)>'}}]}"));
	text = ls_components_compose(components, plan, pages, NULL, &error);
	g_assert_no_error(error);
	bundle = ls_json_parse(text, NULL);
	contains(file_content(bundle, "work.html"), "&lt;img src=x onerror=alert(1)&gt;");
	lacks(file_content(bundle, "work.html"), "<img src=x");
}

/* Long opening headlines step down a size so interior pages stay readable. */
static void
test_long_heading(void){
	g_autoptr(LsComponents) components = library();
	g_autoptr(JsonNode) plan = load("plan.json");
	g_autoptr(JsonObject) pages = fixture_pages();
	g_autoptr(GError) error = NULL;
	g_autofree gchar *text = NULL;
	g_autoptr(JsonNode) bundle = NULL;
	json_object_set_member(pages, "work.html", json(
		"{'path':'work.html','sections':[{'component':'page-header','variant':'plain','content':"
		"{'heading':'Prepare a brief before you write to anyone at all'}}]}"));
	text = ls_components_compose(components, plan, pages, NULL, &error);
	g_assert_no_error(error);
	bundle = ls_json_parse(text, NULL);
	contains(file_content(bundle, "work.html"), "page-header__title page-header__title--long");
	lacks(file_content(bundle, "index.html"), "hero__title--long");
	contains(file_content(bundle, "styles.css"), ".page-header__title--long");
}


/* Build a sample value that exercises every declared field of a slot. */
static JsonNode *
sample_value(JsonObject *slot, guint depth){
	const gchar *type = ls_string(slot, "type");
	JsonNode *node;
	if (g_str_equal(type, "text")) {
		gint64 max = json_object_has_member(slot, "max") ? json_object_get_int_member(slot, "max") : 40;
		g_autofree gchar *text = g_strndup(depth % 2 == 0 ? "Sample words here" : "Other sample words", MAX(max, 1));
		node = json_node_new(JSON_NODE_VALUE);
		json_node_set_string(node, text);
		return node;
	}
	if (g_str_equal(type, "rich"))
		return json("'Sample *rich* text.'");
	if (g_str_equal(type, "link"))
		return json("{'href':'work.html','label':'Work'}");
	if (g_str_equal(type, "links"))
		return json("[{'href':'work.html','label':'Work'},{'href':'visit.html','label':'Visit'}]");
	if (g_str_equal(type, "image"))
		return json("{'src':'assets/bench.svg','alt':'A bench'}");
	if (g_str_equal(type, "choice")) {
		node = json_node_new(JSON_NODE_VALUE);
		json_node_set_string(node, json_array_get_string_element(json_object_get_array_member(slot, "values"), 0));
		return node;
	}
	if (g_str_equal(type, "flag")) {
		node = json_node_new(JSON_NODE_VALUE);
		json_node_set_boolean(node, TRUE);
		return node;
	}
	if (g_str_equal(type, "items")) {
		JsonObject *fields = json_object_get_object_member(slot, "fields");
		gint64 wanted = json_object_has_member(slot, "min_items") ? json_object_get_int_member(slot, "min_items") : 0;
		JsonArray *items = json_array_new();
		gint64 i;
		wanted = MAX(wanted, 2);
		if (json_object_has_member(slot, "max_items"))
			wanted = MIN(wanted, json_object_get_int_member(slot, "max_items"));
		for (i = 0; i < wanted; i++) {
			JsonObject *item = json_object_new();
			g_autoptr(GList) names = json_object_get_members(fields);
			GList *name;
			for (name = names; name != NULL; name = name->next)
				json_object_set_member(item, name->data,
					sample_value(json_object_get_object_member(fields, name->data), depth + (guint)i));
			json_array_add_object_element(items, item);
		}
		node = json_node_new(JSON_NODE_ARRAY);
		json_node_take_array(node, items);
		return node;
	}
	g_error("no sample for slot type %s", type);
	return NULL;
}

/* Content for a component, generated from its catalog slots; a few components
 * have cross-slot rules and use handwritten content instead. */
static JsonNode *
sample_content(const gchar *name, JsonObject *component){
	JsonObject *slots = json_object_get_object_member(component, "slots");
	g_autoptr(GList) names = NULL;
	GList *slot;
	JsonObject *content;
	JsonNode *node;
	if (g_str_equal(name, "form"))
		return json("{'heading':'Request','mode':'local','fields':[{'name':'piece','label':'Piece','required':true}],"
			"'submit':'Prepare','download_name':'request.txt'}");
	if (g_str_equal(name, "filter"))
		return json("{'heading':'Filtered','all_label':'All','categories':[{'label':'Chairs'},{'label':'Tables'}],"
			"'empty':'None yet.','items':[{'title':'Ladderback','category':'Chairs'},{'title':'Drop-leaf','category':'Tables'}]}");
	if (g_str_equal(name, "module"))
		return json("{'heading':'Write to us','module':'contact-form'}");
	if (g_str_equal(name, "custom"))
		return json("{'label':'Custom region','html':'<div><p>Custom sample</p></div>'}");
	if (g_str_equal(name, "hours"))
		return json("{'heading':'Hours','items':[{'days':'Mon to Fri','times':'9:00 to 17:00'}],"
			"'timezone':'Europe/London','open_label':'Open now','closed_label':'Closed now',"
			"'schedule':[{'day':'mon','opens':'09:00','closes':'17:00'},{'day':'fri','opens':'18:00','closes':'02:00'}]}");
	content = json_object_new();
	names = json_object_get_members(slots);
	for (slot = names; slot != NULL; slot = slot->next)
		json_object_set_member(content, slot->data, sample_value(json_object_get_object_member(slots, slot->data), 0));
	node = json_node_new(JSON_NODE_OBJECT);
	json_node_take_object(node, content);
	return node;
}

/* Every component renders in every variant, and every variant but the
 * default one has its own styles. */
static void
test_every_variant(void){
	g_autoptr(LsComponents) components = library();
	g_autoptr(JsonNode) plan = load("plan.json");
	g_autofree gchar *yaml = NULL;
	g_autoptr(JsonNode) catalog = NULL;
	JsonObject *all;
	g_autoptr(GList) names = NULL;
	GList *name;
	guint rendered = 0;
	g_autoptr(GString) unstyled = g_string_new(NULL);
	g_assert_true(g_file_get_contents("data/components/catalog.yaml", &yaml, NULL, NULL));
	catalog = ls_yaml_parse(yaml, NULL);
	g_assert_nonnull(catalog);
	all = json_object_get_object_member(json_node_get_object(catalog), "components");
	/* Builder-fed components need their supplied facts: profiles come with the fixture, a phone here. */
	json_object_set_member(json_node_get_object(plan), "contact", json("{'phone':'+1 555 0100'}"));
	json_object_set_member(json_node_get_object(plan), "booking", json("{'href':'/book'}"));
	/* ...and a supplied podbay module for the module component. */
	{
		JsonArray *modules = json_array_new();
		JsonObject *module = json_object_new();
		gchar *text = NULL;
		json_object_set_string_member(module, "id", "contact-form");
		g_assert_true(g_file_get_contents("tests/fixtures/podbay/contact-form.html", &text, NULL, NULL));
		json_object_set_string_member(module, "snippet", text);
		g_free(text);
		g_assert_true(g_file_get_contents("tests/fixtures/podbay/contact-form.css", &text, NULL, NULL));
		json_object_set_string_member(module, "css", text);
		g_free(text);
		json_array_add_object_element(modules, module);
		json_object_set_array_member(json_node_get_object(plan), "modules", modules);
	}
	names = json_object_get_members(all);
	for (name = names; name != NULL; name = name->next) {
		JsonObject *component = json_object_get_object_member(all, name->data);
		JsonObject *variants = json_object_get_object_member(component, "variants");
		g_autoptr(GList) variant_names = json_object_get_members(variants);
		GList *variant;
		for (variant = variant_names; variant != NULL; variant = variant->next) {
			g_autoptr(JsonObject) pages = fixture_pages();
			g_autoptr(JsonNode) page = NULL;
			JsonObject *section;
			g_autoptr(GError) error = NULL;
			g_autofree gchar *text = NULL;
			g_autofree gchar *marker = g_strdup_printf("class=\"block block--%s %s %s--%s",
				(const gchar *)name->data, (const gchar *)name->data, (const gchar *)name->data,
				(const gchar *)variant->data);
			g_autofree gchar *selector = g_strdup_printf(".%s--%s", (const gchar *)name->data,
				(const gchar *)variant->data);
			g_autoptr(JsonNode) bundle = NULL;
			const gchar *html;
			/* A gimmick a writer names renders as a calm variant (taste);
			 * test-ls-taste covers that. */
			{
				JsonObject *taste = json_object_get_object_member(json_node_get_object(catalog), "taste");
				g_autofree gchar *pair = g_strdup_printf("%s/%s", (const gchar *)name->data, (const gchar *)variant->data);
				const gchar *lists[] = {"variants", "openings"};
				gboolean banned = FALSE;
				guint l, k;
				for (l = 0; taste != NULL && l < G_N_ELEMENTS(lists); l++)
					for (k = 0; k < json_array_get_length(json_object_get_array_member(taste, lists[l])); k++)
						banned |= g_strcmp0(json_array_get_string_element(json_object_get_array_member(taste, lists[l]), k), pair) == 0;
				if (banned)
					continue;
			}
			page = json("{'path':'index.html','sections':[{'component':'hero','variant':'statement',"
				"'content':{'heading':'Home'}}]}");
			section = json_object_new();
			json_object_set_string_member(section, "component", name->data);
			json_object_set_string_member(section, "variant", variant->data);
			json_object_set_member(section, "content", sample_content(name->data, component));
			json_array_add_object_element(json_object_get_array_member(json_node_get_object(page), "sections"), section);
			json_object_set_member(pages, "index.html", json_node_copy(page));
			text = ls_components_compose(components, plan, pages, NULL, &error);
			if (error != NULL)
				g_error("%s/%s: %s", (const gchar *)name->data, (const gchar *)variant->data, error->message);
			bundle = ls_json_parse(text, NULL);
			html = file_content(bundle, "index.html");
			contains(html, marker);
			lacks(html, "{{");
			lacks(html, "Sample *rich*");
			if (variant != variant_names && strstr(file_content(bundle, "styles.css"), selector) == NULL)
				g_string_append_printf(unstyled, " %s", selector);
			rendered++;
		}
	}
	if (unstyled->len > 0)
		g_error("variants without styles:%s", unstyled->str);
	g_assert_cmpuint(rendered, >=, 150);
}

/* The plan schema offers exactly the plannable page kinds of the catalog. */
static void
test_schema_kinds(void){
	g_autoptr(LsComponents) components = library();
	g_autofree gchar *yaml = NULL;
	g_autoptr(JsonNode) catalog = NULL;
	g_autoptr(JsonNode) schema = ls_json_parse(ls_components_get_plan_schema(components), NULL);
	JsonObject *kinds;
	JsonArray *listed;
	g_autoptr(GList) names = NULL;
	GList *name;
	guint plannable = 0, i;
	g_assert_true(g_file_get_contents("data/components/catalog.yaml", &yaml, NULL, NULL));
	catalog = ls_yaml_parse(yaml, NULL);
	kinds = json_object_get_object_member(json_node_get_object(catalog), "kinds");
	listed = json_object_get_array_member(json_object_get_object_member(json_object_get_object_member(
		json_object_get_object_member(json_object_get_object_member(json_object_get_object_member(
		json_node_get_object(schema), "properties"), "pages"), "items"), "properties"), "kind"), "enum");
	names = json_object_get_members(kinds);
	for (name = names; name != NULL; name = name->next) {
		JsonObject *kind = json_object_get_object_member(kinds, name->data);
		gboolean found = FALSE;
		if (json_object_has_member(kind, "builtin") && json_object_get_boolean_member(kind, "builtin"))
			continue;
		plannable++;
		for (i = 0; i < json_array_get_length(listed); i++)
			found |= g_str_equal(json_array_get_string_element(listed, i), name->data);
		if (!found)
			g_error("plan schema lacks kind %s", (const gchar *)name->data);
	}
	g_assert_cmpuint(json_array_get_length(listed), ==, plannable);
}


/* Directory entries group under sorted initial letters; tabs ship their
 * script and every panel stays readable without it. */
static void
test_directory_and_tabs(void){
	g_autoptr(LsComponents) components = library();
	g_autoptr(JsonNode) plan = load("plan.json");
	g_autoptr(JsonObject) pages = fixture_pages();
	g_autoptr(GError) error = NULL;
	g_autofree gchar *text = NULL;
	g_autoptr(JsonNode) bundle = NULL;
	const gchar *html, *a, *b, *e, *hash;
	json_object_set_member(pages, "work.html", json(
		"{'path':'work.html','sections':[{'component':'page-header','content':{'heading':'Woods'}},"
		"{'component':'directory','variant':'list','content':{'heading':'All woods','items':["
		"{'title':'elm'},{'title':'Ash'},{'title':'2x4 offcuts'},{'title':'Beech'},{'title':'Alder'},"
		"{'title':'Cedar'},{'title':'Dogwood'},{'title':'Fir'}]}},"
		"{'component':'tabs','variant':'line','content':{'heading':'Care','items':["
		"{'label':'Oil','body':'Oil twice a year.'},{'label':'Wax','body':'Wax monthly.'}]}}]}"));
	text = ls_components_compose(components, plan, pages, NULL, &error);
	g_assert_no_error(error);
	bundle = ls_json_parse(text, NULL);
	html = file_content(bundle, "work.html");
	a = strstr(html, "<p class=\"directory__letter\" id=\"all-woods-g1-h\">A</p>");
	b = strstr(html, ">B</p>");
	e = strstr(html, ">E</p>");
	hash = strstr(html, ">#</p>");
	if (a == NULL || b == NULL || e == NULL || hash == NULL || !(a < b && b < e && e < hash))
		g_error("directory groups out of order:\n%.3000s", html);
	/* Over a few letters the index would be chips standing in for content:
	 * a short directory is a plain list. */
	{
		g_autoptr(JsonObject) few = fixture_pages();
		g_autoptr(JsonNode) small = NULL;
		g_autofree gchar *small_text = NULL;
		json_object_set_member(few, "work.html", json(
			"{'path':'work.html','sections':[{'component':'page-header','content':{'heading':'Woods'}},"
			"{'component':'directory','variant':'list','content':{'heading':'All woods','items':["
			"{'title':'Nail care'},{'title':'Coat trims'},{'title':'Dental care'}]}}]}"));
		small_text = ls_components_compose(components, plan, few, NULL, &error);
		g_assert_no_error(error);
		small = ls_json_parse(small_text, NULL);
		g_assert_null(strstr(file_content(small, "work.html"), "directory__letters"));
		g_assert_null(strstr(file_content(small, "work.html"), "directory__letter\""));
		g_assert_nonnull(strstr(file_content(small, "work.html"), "Coat trims"));
		/* One list in the writer's order, not a ruled group per letter, and
		 * a group without a letter spans the row instead of sitting in the
		 * letter's narrow column. */
		g_assert_cmpuint(count(file_content(small, "work.html"), "class=\"directory__group\""), ==, 1);
		g_assert_true(strstr(file_content(small, "work.html"), "Nail care") <
			strstr(file_content(small, "work.html"), "Coat trims"));
		contains(file_content(small, "styles.css"), ".directory--list .directory__items:first-child { grid-column: 1 / -1; }");
	}
	g_assert_cmpuint(count(html, "class=\"directory__letter\""), ==, 7);
	g_assert_true(strstr(a, "Ash") < strstr(a, "Alder"));
	contains(html, "<a href=\"#all-woods-g1\">A</a>");
	contains(html, "aria-label=\"Jump to letter\"");
	contains(html, "role=\"tablist\" hidden");
	contains(html, "Oil twice a year.");
	contains(html, "Wax monthly.");
	contains(file_content(bundle, "site.js"), "[data-tabs]");
	lacks(file_content(bundle, "index.html"), "data-tabs");
}

/* A menu with all_label gets one filter button per section, reusing the
 * filter script and styles; without it the menu stays script-free. */
static void
test_menu_filter(void){
	g_autoptr(LsComponents) components = library();
	g_autoptr(JsonNode) plan = load("plan.json");
	g_autoptr(JsonObject) pages = fixture_pages();
	g_autoptr(GError) error = NULL;
	g_autofree gchar *text = NULL;
	g_autoptr(JsonNode) bundle = NULL;
	const gchar *html;
	json_object_set_member(pages, "work.html", json(
		"{'path':'work.html','sections':[{'component':'page-header','content':{'heading':'Menu'}},"
		"{'component':'menu','content':{'heading':'Food','all_label':'Everything','sections':["
		"{'name':'Small plates','items':[{'name':'Olives','price':'4'}]},"
		"{'name':'Mains','items':[{'name':'Stew','price':'14'},{'name':'Pie'}]}]}}]}"));
	text = ls_components_compose(components, plan, pages, NULL, &error);
	g_assert_no_error(error);
	bundle = ls_json_parse(text, NULL);
	html = file_content(bundle, "work.html");
	contains(html, "<div class=\"wrap\" data-filter>");
	contains(html, "class=\"filter__controls\" role=\"group\" aria-label=\"Filter entries\" hidden>");
	contains(html, "data-filter-value=\"\" aria-pressed=\"true\">Everything</button>");
	g_assert_cmpuint(count(html, "class=\"filter__button\""), ==, 3);
	g_assert_cmpuint(count(html, "data-filter-item=\"food-1-small-plates\""), ==, 1);
	contains(html, "data-filter-value=\"food-2-mains\" aria-pressed=\"false\">Mains</button>");
	contains(html, "data-filter-empty hidden");
	/* Every dish is in the page source, so nothing depends on the script. */
	contains(html, ">Olives<");
	contains(html, ">Pie<");
	contains(file_content(bundle, "site.js"), "[data-filter]");
	contains(file_content(bundle, "styles.css"), ".filter__controls");
	/* Without all_label the menu renders no controls and pulls in no filter. */
	json_object_set_member(pages, "work.html", json(
		"{'path':'work.html','sections':[{'component':'page-header','content':{'heading':'Menu'}},"
		"{'component':'menu','content':{'heading':'Food','sections':["
		"{'name':'Mains','items':[{'name':'Stew','price':'14'}]}]}}]}"));
	g_clear_pointer(&text, g_free);
	g_clear_pointer(&bundle, json_node_unref);
	text = ls_components_compose(components, plan, pages, NULL, &error);
	g_assert_no_error(error);
	bundle = ls_json_parse(text, NULL);
	html = file_content(bundle, "work.html");
	lacks(html, "data-filter");
	lacks(file_content(bundle, "site.js"), "[data-filter]");
}

/* The plan's look reaches every page as body classes, library styles sit in
 * cascade layers, and restyles follow them only for components in use. */
static void
test_look_and_restyle(void){
	g_autoptr(LsComponents) components = library();
	g_autoptr(JsonNode) plan = load("plan.json");
	g_autoptr(JsonObject) pages = fixture_pages();
	g_autoptr(GError) error = NULL;
	g_autofree gchar *text = NULL;
	g_autoptr(JsonNode) bundle = NULL;
	const gchar *css, *layers, *look, *restyle, *hero;
	text = ls_components_compose(components, plan, pages, NULL, &error);
	g_assert_no_error(error);
	bundle = ls_json_parse(text, NULL);
	contains(file_content(bundle, "index.html"), "look-header-centered look-nav-underline look-heads-rule");
	contains(file_content(bundle, "work.html"), "look-panels-outlined look-rhythm-bands");
	contains(file_content(bundle, "404.html"), "look-footer-big");
	css = file_content(bundle, "styles.css");
	layers = strstr(css, "@layer ls-base, ls-modules, ls-components, ls-look, ls-signature;");
	hero = strstr(css, "@layer ls-components {\n.hero {");
	look = strstr(css, "@layer ls-look {");
	restyle = strstr(css, "/* Site restyle: hero */\n.hero__title { font-style: italic; }");
	g_assert_nonnull(layers);
	g_assert_nonnull(hero);
	g_assert_nonnull(look);
	g_assert_nonnull(restyle);
	g_assert_true(layers < hero && hero < look && look < restyle);
	g_assert_true(restyle < strstr(css, "/* Site plan */") || strstr(css, "/* Site plan */") == NULL);
	lacks(css, ".tabs__tab { color: red; }");
	contains(css, "@layer ls-base {\n");
}

/* Every look value other than an axis default has styles of its own, and the
 * plan schema offers exactly the catalog's look values. */
static void
test_every_look(void){
	g_autoptr(LsComponents) components = library();
	g_autoptr(JsonNode) plan = load("plan.json");
	g_autoptr(JsonObject) pages = fixture_pages();
	g_autoptr(GError) error = NULL;
	g_autofree gchar *yaml = NULL, *text = NULL;
	g_autoptr(JsonNode) catalog = NULL, bundle = NULL;
	g_autoptr(JsonNode) schema = ls_json_parse(ls_components_get_plan_schema(components), NULL);
	JsonObject *looks, *schema_look;
	g_autoptr(GList) axes = NULL;
	g_autoptr(GString) unstyled = g_string_new(NULL);
	GList *axis;
	const gchar *css;
	g_assert_true(g_file_get_contents("data/components/catalog.yaml", &yaml, NULL, NULL));
	catalog = ls_yaml_parse(yaml, NULL);
	looks = json_object_get_object_member(json_node_get_object(catalog), "looks");
	schema_look = json_object_get_object_member(json_object_get_object_member(json_object_get_object_member(
		json_node_get_object(schema), "properties"), "look"), "properties");
	text = ls_components_compose(components, plan, pages, NULL, &error);
	g_assert_no_error(error);
	bundle = ls_json_parse(text, NULL);
	css = file_content(bundle, "styles.css");
	axes = json_object_get_members(looks);
	g_assert_cmpuint(g_list_length(axes), ==, json_object_get_size(schema_look));
	for (axis = axes; axis != NULL; axis = axis->next) {
		JsonObject *values = json_object_get_object_member(json_object_get_object_member(looks, axis->data), "values");
		JsonArray *listed = json_object_get_array_member(json_object_get_object_member(schema_look, axis->data), "enum");
		JsonObject *extra = json_object_get_object_member(json_node_get_object(catalog), "extra_looks");
		JsonObject *added = extra != NULL && json_object_has_member(extra, axis->data)
			? json_object_get_object_member(extra, axis->data) : NULL;
		JsonObject *wider = json_object_has_member(json_node_get_object(catalog), "wider")
			? json_object_get_object_member(json_node_get_object(catalog), "wider") : NULL;
		JsonObject *wider_looks = wider != NULL && json_object_has_member(wider, "looks")
			? json_object_get_object_member(wider, "looks") : NULL;
		g_autoptr(GList) names = json_object_get_members(values);
		g_autoptr(GList) fresh = added != NULL ? json_object_get_members(added) : NULL;
		GList *name;
		/* Extra values (catalog extra_looks and the wider block's looks) join
		 * the schema, each styled in its own look-AXIS-VALUE.css. */
		if (wider_looks != NULL && json_object_has_member(wider_looks, axis->data))
			fresh = g_list_concat(fresh, json_object_get_members(json_object_get_object_member(wider_looks, axis->data)));
		g_assert_cmpuint(json_array_get_length(listed), ==, g_list_length(names) + g_list_length(fresh));
		for (name = names; name != NULL; name = name->next) {
			g_autofree gchar *selector = g_strdup_printf(".look-%s-%s", (const gchar *)axis->data,
				(const gchar *)name->data);
			if (name != names && strstr(css, selector) == NULL)
				g_string_append_printf(unstyled, " %s", selector);
		}
		for (name = fresh; name != NULL; name = name->next) {
			g_autofree gchar *path = g_strdup_printf("data/components/look-%s-%s.css", (const gchar *)axis->data,
				(const gchar *)name->data);
			g_autofree gchar *selector = g_strdup_printf(".look-%s-%s", (const gchar *)axis->data,
				(const gchar *)name->data);
			g_autofree gchar *own = NULL;
			if (!g_file_get_contents(path, &own, NULL, NULL) || strstr(own, selector) == NULL)
				g_string_append_printf(unstyled, " %s", selector);
		}
	}
	if (unstyled->len > 0)
		g_error("look values without styles:%s", unstyled->str);
}



/* Every theme applies to a whole site: every page, token, look axis and
 * component style changes, while content stays the same, and applying a
 * theme never edits the library's copy of it. */
static void
test_themes(void){
	g_autoptr(LsComponents) components = library();
	g_auto(GStrv) names = ls_components_list_themes(components);
	g_autoptr(GHashTable) sheets = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	g_autoptr(GError) error = NULL;
	guint i;
	g_assert_cmpuint(g_strv_length(names), >=, 15);
	for (i = 0; names[i] != NULL; i++) {
		g_autoptr(JsonNode) plan = load("plan.json");
		g_autoptr(JsonObject) pages = fixture_pages();
		g_autoptr(JsonNode) bundle = NULL;
		g_autofree gchar *text = NULL;
		JsonObject *tokens;
		const gchar *css, *index;
		g_autofree gchar *paper = NULL;
		g_assert_nonnull(ls_components_get_theme_summary(components, names[i]));
		g_assert_true(ls_components_apply_theme(components, plan, names[i], &error));
		g_assert_no_error(error);
		tokens = json_object_get_object_member(json_node_get_object(plan), "tokens");
		paper = g_strdup_printf("--paper: %s;", ls_string(tokens, "paper"));
		/* Mutating the applied copy must not reach the next application. */
		json_object_set_string_member(tokens, "paper", "#123456");
		g_assert_true(ls_components_apply_theme(components, plan, names[i], NULL));
		/* As in the plan step: the theme's look, then the builder's defaults. */
		g_assert_true(ls_components_apply_default(components, plan, TRUE));
		g_assert_true(ls_components_check_plan(components, plan, &error));
		g_assert_no_error(error);
		g_assert_null(ls_components_repair_plan(components, plan));
		text = ls_components_compose(components, plan, pages, NULL, &error);
		g_assert_no_error(error);
		bundle = ls_json_parse(text, NULL);
		css = file_content(bundle, "styles.css");
		index = file_content(bundle, "index.html");
		contains(css, paper);
		lacks(css, ".hero__title { font-style: italic; }");
		contains(index, "Harbor");
		g_assert_true(g_hash_table_add(sheets, g_strdup(css)));
	}
	{
		g_autoptr(JsonNode) plan = load("plan.json");
		g_assert_false(ls_components_apply_theme(components, plan, "nope", &error));
		g_assert_error(error, LS_ERROR, LS_ERROR_INVALID);
	}
	g_assert_null(ls_components_get_theme_summary(components, "nope"));
	g_assert_null(strstr(ls_components_get_themed_plan_schema(components), "\"tokens\""));
	g_assert_nonnull(strstr(ls_components_get_plan_schema(components), "\"tokens\""));
}


/* Heading emphasis renders as markup only where the heading is shown;
 * everywhere else the markers are dropped, and stray markers stay text. */
static void
test_heading_emphasis(void){
	g_autoptr(LsComponents) components = library();
	g_autoptr(JsonNode) plan = load("plan.json");
	g_autoptr(JsonObject) pages = fixture_pages();
	g_autoptr(GError) error = NULL;
	g_autofree gchar *text = NULL;
	g_autoptr(JsonNode) bundle = NULL;
	const gchar *html;
	json_object_set_member(pages, "journal.html", json(
		"{'path':'journal.html','sections':[{'component':'page-header','content':{'heading':'Notes from the *bench*'}},"
		"{'component':'text','content':{'heading':'Glue <b> and *clamps* at 5 * 3','body':'Body.'}},"
		"{'component':'text','content':{'heading':'A * lone and ** empty marker','body':'More.'}}]}"));
	text = ls_components_compose(components, plan, pages, NULL, &error);
	g_assert_no_error(error);
	bundle = ls_json_parse(text, NULL);
	html = file_content(bundle, "journal.html");
	contains(html, ">Notes from the <em class=\"em\">bench</em></h1>");
	contains(html, ">Glue &lt;b&gt; and <em class=\"em\">clamps</em> at 5 * 3</h2>");
	contains(html, ">A * lone and ** empty marker</h2>");
	contains(html, "<a href=\"#glue-b-and-clamps-at-5-3\">Glue &lt;b&gt; and clamps at 5 * 3</a>");
	lacks(html, "*bench*");
	lacks(html, "<b>");
}

/* Section heads are never numbered: the page carries no section numbers
 * and the styles no rule that would draw them. */
static void
test_section_numbers(void){
	g_autoptr(LsComponents) components = library();
	g_autoptr(JsonNode) plan = load("plan.json");
	g_autoptr(JsonObject) pages = fixture_pages();
	g_autoptr(GError) error = NULL;
	g_autofree gchar *text = NULL;
	g_autoptr(JsonNode) bundle = NULL;
	const gchar *html;
	const gchar *css;
	json_object_set_member(pages, "journal.html", json(
		"{'path':'journal.html','sections':[{'component':'page-header','content':{'heading':'Notes'}},"
		"{'component':'text','content':{'heading':'First part','body':'Body.'}},"
		"{'component':'text','content':{'body':'No heading here.'}},"
		"{'component':'text','content':{'heading':'Second part','body':'More.'}}]}"));
	text = ls_components_compose(components, plan, pages, NULL, &error);
	g_assert_no_error(error);
	bundle = ls_json_parse(text, NULL);
	html = file_content(bundle, "journal.html");
	css = file_content(bundle, "styles.css");
	contains(html, "id=\"first-part\"");
	lacks(html, "--ls-n");
	lacks(css, "var(--ls-n)");
	lacks(css, "counter(ls-section");
}

/* Supplied profiles: networks are recognised from the address alone, the
 * footer links them on every page, the home page lists them as sameAs for
 * search engines, and a social section needs profiles to exist. */
static void
test_social(void){
	g_autoptr(LsComponents) components = library();
	g_autoptr(JsonNode) links = json("[{'url':'https://www.instagram.com/harbor.bench/'},"
		"{'url':'https://twitter.com/harbor'},{'url':'https://bsky.app/profile/harbor.bsky.social'},"
		"{'url':'https://fosstodon.org/@harbor'},{'url':'https://www.linkedin.com/company/harbor'},"
		"{'url':'https://harbor.example/','label':'Our blog'},{'url':'http://facebook.com/harbor'},"
		"{'url':'https://notinstagram.com/x'}]");
	g_autoptr(JsonArray) social = ls_components_social(components, json_node_get_array(links));
	const gchar *expected[][3] = {
		{"instagram", "Instagram", "@harbor.bench"}, {"x", "X", "@harbor"},
		{"bluesky", "Bluesky", "harbor.bsky.social"}, {"mastodon", "Mastodon", "@harbor"},
		{"linkedin", "LinkedIn", "harbor"}, {"web", "Our blog", "harbor.example"}, {"web", "Website", "x"}};
	g_autoptr(JsonNode) plan = load("plan.json");
	g_autoptr(JsonObject) pages = fixture_pages();
	g_autoptr(GError) error = NULL;
	g_autofree gchar *text = NULL;
	g_autoptr(JsonNode) bundle = NULL;
	const gchar *home, *work;
	guint i;
	/* The http address is skipped; a look-alike host is not the network. */
	g_assert_cmpuint(json_array_get_length(social), ==, G_N_ELEMENTS(expected));
	for (i = 0; i < G_N_ELEMENTS(expected); i++) {
		JsonObject *entry = json_array_get_object_element(social, i);
		g_assert_cmpstr(json_object_get_string_member(entry, "network"), ==, expected[i][0]);
		g_assert_cmpstr(json_object_get_string_member(entry, "label"), ==, expected[i][1]);
		g_assert_cmpstr(json_object_get_string_member(entry, "handle"), ==, expected[i][2]);
	}
	/* The plan's builder-owned profiles render, with a hostile site name kept inside its script. */
	json_object_set_string_member(json_node_get_object(plan), "name", "Harbor </script><b>");
	/* A footer text link to a supplied profile is dropped: the icons already list it. A spaced
	 * telephone link is repaired to digits. */
	{
		JsonObject *footer = json_object_get_object_member(json_node_get_object(plan), "footer");
		JsonArray *footer_links = json_object_get_array_member(footer, "links");
		g_autofree gchar *notes = NULL;
		json_array_add_element(footer_links, json("{'label':'Instagram','href':'https://instagram.com/harborbench/'}"));
		json_array_add_element(footer_links, json("{'label':'Call','href':'tel:+1 (555) 014-2270'}"));
		notes = ls_components_repair_plan(components, plan);
		g_assert_nonnull(notes);
		g_assert_nonnull(strstr(notes, "-> tel:+15550142270"));
	}
	json_object_set_member(pages, "journal.html", json(
		"{'path':'journal.html','sections':[{'component':'page-header','content':{'heading':'Notes'}},"
		"{'component':'social','variant':'list','content':{'heading':'Follow the bench'}}]}"));
	text = ls_components_compose(components, plan, pages, "https://harbor.example/", &error);
	g_assert_no_error(error);
	bundle = ls_json_parse(text, NULL);
	home = file_content(bundle, "index.html");
	work = file_content(bundle, "journal.html");
	contains(home, "<ul class=\"site-footer__social\" role=\"list\" aria-label=\"Follow us\">");
	contains(home, "<a href=\"https://www.instagram.com/harborbench\" rel=\"me noopener\"><svg viewBox=\"0 0 24 24\"");
	contains(home, "<span class=\"visually-hidden\">Mastodon</span>");
	lacks(home, ">Instagram</a>");
	contains(home, "href=\"tel:+15550142270\"");
	contains(home, "<script type=\"application/ld+json\">");
	contains(home, "\"sameAs\":[\"https://www.instagram.com/harborbench\",\"https://mastodon.social/@harborbench\"]");
	contains(home, "Harbor <\\/script><b>");
	lacks(home, "Harbor </script>");
	lacks(work, "application/ld+json");
	contains(work, "social__item social__item--instagram");
	contains(work, "<span class=\"social__handle\">@harborbench</span>");
	/* A profile the builder cannot vouch for is refused. */
	{
		g_autoptr(JsonNode) bad = load("plan.json");
		g_autoptr(GError) bad_error = NULL;
		g_autofree gchar *none = NULL;
		json_object_set_member(json_node_get_object(bad), "social",
			json("[{'url':'http://instagram.com/x','network':'instagram','label':'Instagram'}]"));
		none = ls_components_compose(components, bad, pages, NULL, &bad_error);
		g_assert_null(none);
		g_assert_nonnull(strstr(bad_error->message, "social[0]"));
	}
	/* A social section without supplied profiles is refused, and no footer links appear. */
	{
		g_autoptr(JsonNode) bare = load("plan.json");
		g_autoptr(GError) bare_error = NULL;
		g_autofree gchar *none = NULL;
		json_object_remove_member(json_node_get_object(bare), "social");
		none = ls_components_compose(components, bare, pages, NULL, &bare_error);
		g_assert_null(none);
		g_assert_nonnull(strstr(bare_error->message, "has none"));
	}
}

/* Share targets are complete links when the site has an address, filled by
 * site.js from the visitor's address when it does not, and the preview
 * image is the page's first picture made absolute. */
static void
test_share(void){
	g_autoptr(LsComponents) components = library();
	g_autoptr(JsonNode) plan = load("plan.json");
	g_autoptr(JsonObject) pages = fixture_pages();
	guint pass;
	json_object_set_member(pages, "journal.html", json(
		"{'path':'journal.html','sections':[{'component':'page-header','content':{'heading':'Notes & news'}},"
		"{'component':'share','content':{'heading':'Pass it on'}}]}"));
	for (pass = 0; pass < 2; pass++) {
		g_autoptr(GError) error = NULL;
		g_autofree gchar *text = ls_components_compose(components, plan, pages,
			pass == 0 ? "https://harbor.example/" : NULL, &error);
		g_autoptr(JsonNode) bundle = NULL;
		const gchar *html, *js;
		g_assert_no_error(error);
		bundle = ls_json_parse(text, NULL);
		html = file_content(bundle, "journal.html");
		js = file_content(bundle, "site.js");
		contains(js, "data-share-native");
		contains(html, "data-share=\"https://www.facebook.com/sharer/sharer.php?u={url}\"");
		contains(html, "data-share-copy data-copied=\"Link copied\">Copy link</button>");
		if (pass == 0) {
			contains(html, "href=\"https://www.facebook.com/sharer/sharer.php?u=https%3A%2F%2Fharbor.example%2Fjournal.html\"");
			contains(html, "href=\"mailto:?subject=Why%20we%20mend%20instead%20of%20replace%20%7C%20Harbor%20%26%20Pine&amp;body=https%3A%2F%2F");
			lacks(html, "<li hidden><a");
		} else {
			contains(html, "<li hidden><a class=\"share__button share__link share__link--facebook\" data-share=");
			lacks(html, "og:image");
			contains(html, "<meta name=\"twitter:card\" content=\"summary\">");
		}
	}
	/* Pages without share load no share script; the home page's first picture previews it. */
	{
		g_autoptr(JsonObject) plain = fixture_pages();
		g_autoptr(GError) error = NULL;
		g_autofree gchar *text = ls_components_compose(components, plan, plain, "https://harbor.example/", &error);
		g_autoptr(JsonNode) bundle = NULL;
		const gchar *home;
		g_assert_no_error(error);
		bundle = ls_json_parse(text, NULL);
		lacks(file_content(bundle, "site.js"), "data-share-native");
		/* A drawing is no link preview. */
		home = file_content(bundle, "index.html");
		contains(home, "<img src=\"assets/");
		lacks(home, "og:image");
	}
	{
		g_autoptr(JsonNode) photo_plan = load("plan.json");
		g_autoptr(JsonObject) photo_pages = fixture_pages();
		g_autoptr(GError) error = NULL;
		g_autofree gchar *text = NULL;
		g_autoptr(JsonNode) bundle = NULL;
		const gchar *html;
		json_object_set_member(json_node_get_object(photo_plan), "photos",
			json("[{'path':'assets/yard.jpg','alt':'Dogs in the yard','width':1600,'height':1067}]"));
		json_object_set_member(photo_pages, "journal.html", json(
			"{'path':'journal.html','sections':[{'component':'page-header','content':{'heading':'Notes'}},"
			"{'component':'media','content':{'art':{'src':'assets/yard.jpg','alt':'Dogs in the yard'}}}]}"));
		text = ls_components_compose(components, photo_plan, photo_pages, "https://harbor.example/", &error);
		g_assert_no_error(error);
		bundle = ls_json_parse(text, NULL);
		html = file_content(bundle, "journal.html");
		contains(html, "<meta property=\"og:image\" content=\"https://harbor.example/assets/yard.jpg\">");
		contains(html, "<meta name=\"twitter:card\" content=\"summary_large_image\">");
	}
}

/* The signature layer keeps to the site's tokens and never hides or pins
 * content; stock flourishes are retired unless a theme names them. */
static void
test_signature_rules(void){
	g_autoptr(LsComponents) components = library();
	const gchar *good[] = {
		".em { color: var(--accent-text); border-bottom: 2px dashed currentColor; }",
		"main > .block + .block::before { content: \"\"; display: block; width: 3rem; height: 1rem; margin-inline: auto; "
		"background-color: var(--accent); mask: url(\"data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 30 10'%3E%3Ccircle cx='5' cy='5' r='4'/%3E%3C/svg%3E\") center / contain no-repeat; }",
		"@media (prefers-reduced-motion: no-preference) { .kicker { transition: color 0.2s; } }"};
	const gchar *bad[] = {
		".em { color: #c0ffee; }", ".em { color: rgb(1, 2, 3); }", ".site-nav { display: none; }",
		".em { color: var(--ink) !important; }", ".kicker { position: fixed; }",
		".em { background: url(https://example.com/x.png); }",
		".em { background: url(\"data:image/svg+xml,<svg></svg>\"); }",
		".em { background: url(\"data:image/svg+xml,%3Csvg%3E%3Cscript%3E%3C/script%3E%3C/svg%3E\"); }",
		".em { color: var(--ink);", "@import url(x.css);"};
	guint i;
	for (i = 0; i < G_N_ELEMENTS(good); i++) {
		g_autoptr(GError) error = NULL;
		g_assert_true(ls_components_check_signature(good[i], &error));
		g_assert_no_error(error);
	}
	for (i = 0; i < G_N_ELEMENTS(bad); i++) {
		g_autoptr(GError) error = NULL;
		g_assert_false(ls_components_check_signature(bad[i], &error));
		g_assert_nonnull(error);
	}
	/* Without a theme the stock flourishes are off; with one they stay. */
	for (i = 0; i < 2; i++) {
		g_autoptr(JsonNode) plan = load("plan.json");
		JsonObject *look = json_object_get_object_member(json_node_get_object(plan), "look");
		json_object_set_string_member(look, "ornament", "rule");
		json_object_set_string_member(look, "emphasis", "highlight");
		g_assert_true(ls_components_apply_default(components, plan, i == 1));
		g_assert_cmpstr(json_object_get_string_member(look, "ornament"), ==, i == 0 ? "none" : "rule");
		g_assert_cmpstr(json_object_get_string_member(look, "emphasis"), ==, i == 0 ? "accent" : "highlight");
	}
	/* A plan's signature is checked and composed last. */
	{
		g_autoptr(JsonNode) plan = load("plan.json");
		g_autoptr(JsonObject) pages = fixture_pages();
		g_autoptr(GError) error = NULL;
		g_autofree gchar *text = NULL;
		g_autoptr(JsonNode) bundle = NULL;
		const gchar *css;
		json_object_set_string_member(json_node_get_object(plan), "signature", good[0]);
		text = ls_components_compose(components, plan, pages, NULL, &error);
		g_assert_no_error(error);
		bundle = ls_json_parse(text, NULL);
		css = file_content(bundle, "styles.css");
		g_assert_nonnull(strstr(css, "/* Site signature */"));
		/* Last, but for the builder's rule that rations its divider. */
		g_assert_nonnull(strstr(css, "border-bottom: 2px dashed currentColor; }\nmain > .block + .block::before"));
		g_assert_true(g_str_has_suffix(css, "::before { display: none; }\n}\n"));
		g_assert_nonnull(strstr(css, "@layer ls-base, ls-modules, ls-components, ls-look, ls-signature;"));
		g_assert_nonnull(strstr(css, "/* Site signature */\n@layer ls-signature {\n"));
		json_object_set_string_member(json_node_get_object(plan), "signature", bad[0]);
		g_clear_pointer(&text, g_free);
		text = ls_components_compose(components, plan, pages, NULL, &error);
		g_assert_null(text);
		g_assert_nonnull(error);
	}
}

/* A variant that paints a panel in the accent keeps it under every panel
 * look: the look layer comes last, so the restoring rule must be in it. */
static void
test_contrast_panel(void){
	g_autoptr(LsComponents) components = library();
	g_autoptr(JsonNode) plan = load("plan.json");
	g_autoptr(JsonObject) pages = fixture_pages();
	g_autoptr(GError) error = NULL;
	g_autofree gchar *text = NULL;
	g_autoptr(JsonNode) bundle = NULL;
	const gchar *css, *look, *panels, *restore;
	JsonObject *look_object = json_object_get_object_member(json_node_get_object(plan), "look");
	if (look_object == NULL) {
		look_object = json_object_new();
		json_object_set_object_member(json_node_get_object(plan), "look", look_object);
	}
	json_object_set_string_member(look_object, "panels", "rules");
	json_object_set_member(pages, "work.html", json(
		"{'path':'work.html','sections':[{'component':'page-header','content':{'heading':'Work'}},"
		"{'component':'split','variant':'contrast','content':{'heading':'Two ways','panels':["
		"{'heading':'One','body':'First.'},{'heading':'Two','body':'Second.'}]}}]}"));
	text = ls_components_compose(components, plan, pages, NULL, &error);
	g_assert_no_error(error);
	bundle = ls_json_parse(text, NULL);
	css = file_content(bundle, "styles.css");
	look = strstr(css, "@layer ls-look {");
	g_assert_nonnull(look);
	panels = strstr(look, ".look-panels-rules :is(");
	restore = strstr(look, "body .split--contrast .split__panel:last-child { background: var(--accent)");
	g_assert_nonnull(panels);
	g_assert_nonnull(restore);
	g_assert_true(panels < restore);
	contains(file_content(bundle, "work.html"), "split--contrast");
}

/* A supplied photograph is an image the pages may use, rendered at its own
 * pixel size; anything else that is not planned art is still rejected. */
static void
test_photos(void){
	g_autoptr(LsComponents) components = library();
	g_autoptr(JsonNode) plan = load("plan.json");
	g_autoptr(JsonObject) pages = fixture_pages();
	g_autoptr(GError) error = NULL;
	g_autofree gchar *text = NULL;
	g_autoptr(JsonNode) bundle = NULL;
	JsonObject *root = json_node_get_object(plan);
	json_object_set_member(root, "photos", json(
		"[{'path':'assets/bench.jpg','alt':'A workbench','credit':'Test','width':1800,'height':1200}]"));
	g_assert_true(ls_components_check_plan(components, plan, &error));
	g_assert_no_error(error);
	json_object_set_member(pages, "work.html", json(
		"{'path':'work.html','sections':[{'component':'page-header','content':{'heading':'Work'}},"
		"{'component':'media','content':{'art':{'src':'assets/bench.jpg','alt':'A workbench'},'caption':'The bench.'}}]}"));
	text = ls_components_compose(components, plan, pages, NULL, &error);
	g_assert_no_error(error);
	bundle = ls_json_parse(text, NULL);
	contains(file_content(bundle, "work.html"), "<img src=\"assets/bench.jpg\" alt=\"A workbench\" width=\"1800\" height=\"1200\"");
	contains(file_content(bundle, "styles.css"), "max-height: min(80vh, 52rem); object-fit: cover;");
	/* An unsupplied raster is neither art nor a photo. */
	g_clear_pointer(&text, g_free);
	json_object_set_member(pages, "work.html", json(
		"{'path':'work.html','sections':[{'component':'page-header','content':{'heading':'Work'}},"
		"{'component':'media','content':{'art':{'src':'assets/other.jpg','alt':'x'}}}]}"));
	text = ls_components_compose(components, plan, pages, NULL, &error);
	g_assert_null(text);
	g_assert_nonnull(error);
	g_assert_nonnull(strstr(error->message, "assets/other.jpg is not planned art"));
	g_clear_error(&error);
	/* The builder writes photos; malformed entries fail the plan check. */
	json_object_set_member(root, "photos", json("[{'path':'x.jpg','alt':'a','width':1,'height':1}]"));
	g_assert_false(ls_components_check_plan(components, plan, &error));
	g_assert_nonnull(strstr(error->message, "photos[0]"));
	g_clear_error(&error);
	json_object_set_member(root, "photos", json("[{'path':'assets/x.jpg','alt':'a','width':0,'height':1}]"));
	g_assert_false(ls_components_check_plan(components, plan, &error));
	g_clear_error(&error);
}

/* A photo sized for phones (docs/images.org) is shown as its largest variant
 * with a srcset of every variant, the layout's sizes and the capped
 * dimensions; its focus rule follows the shown file; malformed variants fail
 * the plan check. */
static void
test_photo_variants(void){
	g_autoptr(LsComponents) components = library();
	g_autoptr(JsonNode) plan = load("plan.json");
	g_autoptr(JsonObject) pages = fixture_pages();
	g_autoptr(GError) error = NULL;
	g_autofree gchar *text = NULL;
	g_autoptr(JsonNode) bundle = NULL;
	JsonObject *root = json_node_get_object(plan);
	const gchar *html;
	static const gchar *const bad[] = {
		"[{'path':'assets/b-480w.webp','width':480,'height':320}]",
		"[{'path':'assets/b-480w.webp','width':480,'height':320},{'path':'assets/b-1600w.webp','width':1600,'height':900}]",
		"[{'path':'assets/b-800w.webp','width':800,'height':533},{'path':'assets/b-480w.webp','width':480,'height':320},"
			"{'path':'assets/b-1600w.webp','width':1600,'height':1067}]",
		"[{'path':'assets/b-1600w.png','width':1600,'height':1067}]",
		"[{'path':'../b-1600w.webp','width':1600,'height':1067}]",
		"[{'path':'assets/b 1600w.webp','width':1600,'height':1067}]",
		"[{'path':'assets/b-1600w.webp','width':1600,'height':1067,'x':1}]",
		"[]", NULL
	};
	guint i;
	json_object_set_member(root, "photos", json(
		"[{'path':'assets/bench.png','alt':'A workbench','focus':'top','width':1600,'height':1067,'variants':["
		"{'path':'assets/bench-480w.webp','width':480,'height':320},"
		"{'path':'assets/bench-800w.webp','width':800,'height':533},"
		"{'path':'assets/bench-1600w.webp','width':1600,'height':1067}]}]"));
	g_assert_true(ls_components_check_plan(components, plan, &error));
	g_assert_no_error(error);
	json_object_set_member(pages, "work.html", json(
		"{'path':'work.html','sections':[{'component':'page-header','content':{'heading':'Work'}},"
		"{'component':'media','content':{'art':{'src':'assets/bench.png','alt':'A workbench'},'caption':'The bench.'}}]}"));
	text = ls_components_compose(components, plan, pages, NULL, &error);
	g_assert_no_error(error);
	bundle = ls_json_parse(text, NULL);
	html = file_content(bundle, "work.html");
	contains(html, "<img src=\"assets/bench-1600w.webp\" alt=\"A workbench\" width=\"1600\" height=\"1067\" "
		"srcset=\"assets/bench-480w.webp 480w, assets/bench-800w.webp 800w, assets/bench-1600w.webp 1600w\" "
		"sizes=\"(max-width: 48rem) 100vw, 50vw\" loading=\"lazy\"");
	lacks(html, "assets/bench.png");
	contains(file_content(bundle, "styles.css"), "img[src=\"assets/bench-1600w.webp\"] { object-position: top; }");
	for (i = 0; bad[i] != NULL; i++) {
		g_autofree gchar *photos = g_strdup_printf("[{'path':'assets/bench.png','alt':'a','width':1600,'height':1067,"
			"'variants':%s}]", bad[i]);
		json_object_set_member(root, "photos", json(photos));
		g_assert_false(ls_components_check_plan(components, plan, &error));
		g_assert_nonnull(strstr(error->message, "photos[0].variants"));
		g_clear_error(&error);
	}
}

/* Every component that shows art offers a sized photo's srcset with sizes
 * for its own layout, so a new component cannot ship full-size photos. */
static void
test_templates_srcset(void){
	g_autoptr(GDir) dir = g_dir_open("data/components", 0, NULL);
	const gchar *name;
	guint seen = 0;
	g_assert_nonnull(dir);
	while ((name = g_dir_read_name(dir)) != NULL) {
		g_autofree gchar *path = NULL, *text = NULL;
		const gchar *at;
		if (!g_str_has_suffix(name, ".html"))
			continue;
		path = g_build_filename("data/components", name, NULL);
		g_assert_true(g_file_get_contents(path, &text, NULL, NULL));
		for (at = strstr(text, "<img "); at != NULL; at = strstr(at + 1, "<img ")) {
			const gchar *end = strchr(at, '>');
			g_autofree gchar *tag = g_strndup(at, (gsize)(end - at));
			/* Art has its planned size; the brand mark is a fixed 32 px drawing. */
			if (strstr(tag, "src=\"{{src}}\"") == NULL || strstr(tag, "height=\"{{height}}\"") == NULL)
				continue;
			if (strstr(tag, "height=\"{{height}}\"{{#srcset}} srcset=\"{{srcset}}\" sizes=\"") == NULL)
				g_error("%s: an art <img> lacks srcset and sizes: %s", name, tag);
			seen++;
		}
	}
	g_assert_cmpuint(seen, >=, 26);
}

/* Shop and news filters come from the items themselves, the open-now badge
 * from a checked schedule, and drop-down menus from checked nav children. */
static void
test_new_components(void){
	g_autoptr(LsComponents) components = library();
	g_autoptr(JsonNode) plan = load("plan.json");
	g_autoptr(JsonObject) pages = fixture_pages();
	g_autoptr(GError) error = NULL;
	g_autofree gchar *text = NULL;
	g_autoptr(JsonNode) bundle = NULL;
	JsonObject *root = json_node_get_object(plan);
	const gchar *html, *home;
	json_object_set_member(root, "nav", json(
		"[{'href':'work.html','label':'Work','children':[{'href':'journal.html','label':'Journal'},{'href':'visit.html','label':'Visit'}]},"
		"{'href':'start.html','label':'Start'}]"));
	json_object_set_member(pages, "work.html", json(
		"{'path':'work.html','sections':[{'component':'page-header','content':{'heading':'Shop'}},"
		"{'component':'products','variant':'grid','content':{'heading':'Chairs and tables','all_label':'Everything','items':["
		"{'name':'Ladderback','price':'$240','category':'Chairs'},{'name':'Drop-leaf','category':'Tables'},"
		"{'name':'Stool','category':'Chairs','badge':'New'}]}},"
		"{'component':'news','content':{'heading':'Notes','all_label':'All','items':[{'title':'Oil','tag':'Care'},{'title':'Wax','tag':'Care'}]}},"
		"{'component':'hours','content':{'heading':'Hours','items':[{'days':'Mon','times':'9 to 5'}],'timezone':'Europe/London',"
		"'open_label':'Open now','closed_label':'Closed now','schedule':[{'day':'mon','opens':'09:00','closes':'17:00'}]}},"
		"{'component':'compare','variant':'slider','content':{'first':[{'art':{'src':'assets/bench.svg','alt':'Before'},'label':'Before'}],"
		"'second':[{'art':{'src':'assets/bench.svg','alt':'After'},'label':'After'}]}},"
		"{'component':'gallery','content':{'items':[{'art':{'src':'assets/bench.svg','alt':'A'}},{'art':{'src':'assets/bench.svg','alt':'B'}}]}}]}"));
	text = ls_components_compose(components, plan, pages, NULL, &error);
	g_assert_no_error(error);
	bundle = ls_json_parse(text, NULL);
	html = file_content(bundle, "work.html");
	/* Two categories, in first-seen order, and every item keyed. */
	contains(html, "data-filter-value=\"chairs\" aria-pressed=\"false\">Chairs</button>");
	contains(html, "data-filter-value=\"tables\" aria-pressed=\"false\">Tables</button>");
	g_assert_cmpuint(count(html, "data-filter-item=\"chairs\""), ==, 2);
	contains(html, "<p class=\"product-card__badge\">New</p>");
	/* One tag only: no buttons for news. */
	g_assert_cmpuint(count(html, "data-filter-item=\"care\""), ==, 0);
	contains(html, "data-hours data-tz=\"Europe/London\" data-open=\"Open now\" data-closed=\"Closed now\"");
	contains(html, "data-day=\"mon\" data-opens=\"09:00\" data-closes=\"17:00\"");
	contains(html, "data-compare");
	contains(html, "<a class=\"gallery__open\" href=\"assets/bench.svg\">");
	/* The dialog is named after its picture at open time, not "Close". */
	contains(html, "<dialog class=\"gallery__dialog\"><form method=\"dialog\">");
	lacks(html, "gallery__dialog\" aria-label");
	contains(file_content(bundle, "site.js"), "dialog.setAttribute(\"aria-label\", big.alt");
	/* The slider's range keeps its own grid row, below both stacked pictures. */
	contains(file_content(bundle, "styles.css"), ".compare--slider .is-ready .compare__range { grid-area: 2 / 1; }");
	/* Products without pictures keep the grid even beside ones with them. */
	contains(file_content(bundle, "styles.css"), ".products__list:has(.product-card__art) .product-card:not(:has(.product-card__art))::before");
	/* Every edge look reserves room below content for the cut it makes. */
	{
		const gchar *edges[] = {"wave", "torn", "slant", "steps"};
		guint e;
		for (e = 0; e < G_N_ELEMENTS(edges); e++) {
			g_autofree gchar *rule = g_strdup_printf(".look-edges-%s main > .block:not(:last-child) { padding-bottom: calc(var(--section-space) + ", edges[e]);
			contains(file_content(bundle, "styles.css"), rule);
		}
	}
	lacks(file_content(bundle, "styles.css"), "compare__side:last-of-type { position: absolute; inset: 0;");
	/* The drop-down: children under Work, and Work marked when a child is open. */
	home = file_content(bundle, "journal.html");
	contains(home, "<li class=\"site-nav__group is-section\"><a href=\"work.html\">Work</a>");
	contains(home, "<li><a href=\"journal.html\" aria-current=\"page\">Journal</a></li>");
	contains(file_content(bundle, "site.js"), "[data-hours]");
	contains(file_content(bundle, "site.js"), "[data-compare]");
	contains(file_content(bundle, "site.js"), "[data-lightbox]");
	/* A schedule without a zone, or a bad time, is rejected. */
	g_clear_pointer(&text, g_free);
	json_object_set_member(pages, "work.html", json(
		"{'path':'work.html','sections':[{'component':'page-header','content':{'heading':'Visit'}},"
		"{'component':'hours','content':{'items':[{'days':'Mon','times':'9 to 5'}],'open_label':'Open','closed_label':'Closed',"
		"'schedule':[{'day':'mon','opens':'9am','closes':'17:00'}]}}]}"));
	text = ls_components_compose(components, plan, pages, NULL, &error);
	g_assert_null(text);
	g_assert_nonnull(strstr(error->message, "timezone must be an IANA zone"));
	g_assert_nonnull(strstr(error->message, "schedule[0].opens must be 24-hour HH:MM"));
	g_clear_error(&error);
	/* Nav children must be planned pages. */
	json_object_set_member(root, "nav", json("[{'href':'work.html','label':'Work','children':[{'href':'nowhere.html','label':'X'}]}]"));
	g_assert_false(ls_components_check_plan(components, plan, &error));
	g_assert_nonnull(strstr(error->message, "nav[0].children"));
	g_clear_error(&error);
}

/* WCAG contrast of two #rrggbb colors, for checking generated tokens. */
static gdouble
test_contrast(const gchar *a, const gchar *b){
	gdouble l[2];
	const gchar *c[2] = {a, b};
	guint i, j;
	for (i = 0; i < 2; i++) {
		gdouble ch[3];
		for (j = 0; j < 3; j++) {
			gchar pair[3] = {c[i][1 + j * 2], c[i][2 + j * 2], 0};
			gdouble v = g_ascii_strtoull(pair, NULL, 16) / 255.0;
			ch[j] = v <= 0.04045 ? v / 12.92 : pow((v + 0.055) / 1.055, 2.4);
		}
		l[i] = 0.2126 * ch[0] + 0.7152 * ch[1] + 0.0722 * ch[2];
	}
	return (MAX(l[0], l[1]) + 0.05) / (MIN(l[0], l[1]) + 0.05);
}

/* Dark mode, a sitemap footer, photo focus and page transitions come from
 * the plan's look and photos, not from anything a page writes. */
static void
test_scheme_sitemap_focus(void){
	g_autoptr(LsComponents) components = library();
	g_autoptr(JsonNode) plan = load("plan.json");
	g_autoptr(JsonObject) pages = fixture_pages();
	g_autoptr(GError) error = NULL;
	g_autofree gchar *text = NULL;
	g_autoptr(JsonNode) bundle = NULL;
	JsonObject *root = json_node_get_object(plan);
	JsonObject *look = json_object_get_object_member(root, "look");
	const gchar *css, *html, *dark;
	json_object_set_string_member(look, "scheme", "auto");
	json_object_set_string_member(look, "footer", "sitemap");
	json_object_set_member(root, "photos", json(
		"[{'path':'assets/bench.jpg','alt':'A bench','focus':'70% 30%','width':1600,'height':900}]"));
	text = ls_components_compose(components, plan, pages, NULL, &error);
	g_assert_no_error(error);
	bundle = ls_json_parse(text, NULL);
	css = file_content(bundle, "styles.css");
	html = file_content(bundle, "index.html");
	contains(css, "@view-transition { navigation: auto; }");
	contains(css, "img[src=\"assets/bench.jpg\"] { object-position: 70% 30%; }");
	/* The fixture is light, so auto adds a dark version whose pairs pass AA. */
	dark = strstr(css, "@media (prefers-color-scheme: dark) {");
	g_assert_nonnull(dark);
	/* The warning red is re-derived for the dark paper, not kept from the light. */
	g_assert_nonnull(strstr(dark, "--danger: "));
	g_assert_null(strstr(dark, "--danger: #a4161a"));
	contains(html, "<meta name=\"color-scheme\" content=\"light dark\">");
	{
		gchar paper[8] = {0}, ink[8] = {0}, muted[8] = {0};
		const gchar *p = strstr(dark, "--paper: #"), *i = strstr(dark, "--ink: #"), *m = strstr(dark, "--muted: #");
		g_assert_true(p != NULL && i != NULL && m != NULL);
		memcpy(paper, p + 9, 7);
		memcpy(ink, i + 7, 7);
		memcpy(muted, m + 9, 7);
		g_assert_true(test_contrast(ink, paper) >= 4.5);
		g_assert_true(test_contrast(muted, paper) >= 4.5);
	}
	/* The footer lists every planned page. */
	contains(html, "<ul class=\"site-footer__map\" role=\"list\">");
	contains(html, "<li><a href=\"journal.html\">");
	/* A fixed scheme adds nothing, and focus rejects anything but a position. */
	json_object_set_string_member(look, "scheme", "fixed");
	g_clear_pointer(&text, g_free);
	g_clear_pointer(&bundle, json_node_unref);
	text = ls_components_compose(components, plan, pages, NULL, &error);
	bundle = ls_json_parse(text, NULL);
	lacks(file_content(bundle, "styles.css"), "prefers-color-scheme");
	g_assert_true(ls_focus_valid("top"));
	g_assert_true(ls_focus_valid("bottom right"));
	g_assert_true(ls_focus_valid("70% 30%"));
	g_assert_false(ls_focus_valid("70%"));
	g_assert_false(ls_focus_valid("120% 5%"));
	g_assert_false(ls_focus_valid("top; color: red"));
	g_assert_false(ls_focus_valid(""));
}

/* The default identity is one curated palette, pairing and home; the
 * house-default check rejects only a default the identity does not name. */
static void
test_identities(void){
	g_autoptr(LsComponents) components = library();
	g_auto(GStrv) sectors = ls_components_list_sectors(components);
	g_autoptr(JsonNode) plan = load("plan.json");
	g_autoptr(GError) error = NULL;
	JsonObject *tokens = json_object_get_object_member(json_node_get_object(plan), "tokens");
	g_assert_true(g_strv_contains((const gchar *const *)sectors, "trades"));
	g_assert_true(g_strv_contains((const gchar *const *)sectors, "software"));
	/* The default identity is one entry of each group, without its fits. */
	{
		g_autoptr(JsonObject) a = ls_components_get_default_identity(components);
		g_autoptr(JsonObject) b = ls_components_get_default_identity(components);
		const gchar *parts[] = {"palette", "type", "home"};
		guint p;
		g_assert_true(json_object_equal(a, b));
		for (p = 0; p < G_N_ELEMENTS(parts); p++) {
			g_assert_nonnull(ls_string(json_object_get_object_member(a, parts[p]), "name"));
			g_assert_false(json_object_has_member(json_object_get_object_member(a, parts[p]), "fits"));
		}
		g_assert_nonnull(json_object_get_object_member(json_object_get_object_member(a, "palette"), "tokens"));
		g_assert_nonnull(json_object_get_array_member(json_object_get_object_member(a, "type"), "fonts"));
		g_assert_cmpuint(json_array_get_length(json_object_get_array_member(
			json_object_get_object_member(a, "home"), "sections")), >=, 2);
	}
	/* The fixture plan is its own design. */
	g_assert_true(ls_components_check_identity(components, plan, NULL, &error));
	g_assert_no_error(error);
	/* Cream paper with a rust accent is the house default. */
	json_object_set_string_member(tokens, "paper", "#faf3e7");
	json_object_set_string_member(tokens, "accent", "#b4471f");
	g_assert_false(ls_components_check_identity(components, plan, NULL, &error));
	g_assert_error(error, LS_ERROR, LS_ERROR_VALIDATION);
	g_assert_nonnull(strstr(error->message, "TASK palette"));
	g_clear_error(&error);
	/* Unless the dealt palette itself is that warm. */
	{
		g_autoptr(JsonObject) identity = json_object_new();
		JsonObject *palette = json_object_new();
		JsonObject *dealt = json_object_new();
		json_object_set_string_member(dealt, "paper", "#f8f0e0");
		json_object_set_string_member(dealt, "accent", "#b04a20");
		json_object_set_object_member(palette, "tokens", dealt);
		json_object_set_object_member(identity, "palette", palette);
		g_assert_true(ls_components_check_identity(components, plan, identity, &error));
		g_assert_no_error(error);
	}
	/* A dark paper with the same accent is not the default. */
	json_object_set_string_member(tokens, "paper", "#1a1410");
	g_assert_true(ls_components_check_identity(components, plan, NULL, &error));
	/* Fraunces over Source Sans is the default pairing. */
	json_object_set_string_member(tokens, "font_display", "Fraunces, Georgia, serif");
	json_object_set_string_member(tokens, "font_body", "'Source Sans 3', system-ui, sans-serif");
	g_assert_false(ls_components_check_identity(components, plan, NULL, &error));
	g_assert_error(error, LS_ERROR, LS_ERROR_VALIDATION);
	g_assert_nonnull(strstr(error->message, "TASK type"));
	g_clear_error(&error);
	/* Fraunces as a fallback deep in a stack is not the pairing. */
	json_object_set_string_member(tokens, "font_display", "Newsreader, Fraunces, serif");
	g_assert_true(ls_components_check_identity(components, plan, NULL, &error));
	/* character is optional natively, bounded when present. */
	{
		g_autoptr(JsonNode) fresh = load("plan.json");
		g_autofree gchar *long_text = g_strnfill(601, 'x');
		json_object_set_string_member(json_node_get_object(fresh), "character", "Copper, gauges and work boots.");
		g_assert_true(ls_components_check_plan(components, fresh, &error));
		g_assert_no_error(error);
		json_object_set_string_member(json_node_get_object(fresh), "character", long_text);
		g_assert_false(ls_components_check_plan(components, fresh, &error));
		g_assert_nonnull(strstr(error->message, "character"));
		g_clear_error(&error);
	}
}

/* Layout lint over every library stylesheet: rules that caused overflow,
 * clipping or unscrollable pages in real browsers must not return. */
static void
test_layout_lint(void){
	g_autoptr(GDir) dir = g_dir_open("data/components", 0, NULL);
	g_autoptr(GRegex) bare_fr = g_regex_new("grid-template-columns:[^;}]*?(?<=[ :])(?<!, )[0-9.]+fr", 0, 0, NULL);
	g_autoptr(GRegex) viewport = g_regex_new("[0-9.]vw", 0, 0, NULL);
	g_autoptr(GRegex) auto_track = g_regex_new("grid-template-columns:[^;}]*minmax\\(0, [0-9.]+fr\\) auto", 0, 0, NULL);
	const gchar *name;
	g_assert_nonnull(dir);
	while ((name = g_dir_read_name(dir)) != NULL) {
		g_autofree gchar *path = NULL, *css = NULL;
		if (!g_str_has_suffix(name, ".css"))
			continue;
		path = g_build_filename("data/components", name, NULL);
		g_assert_true(g_file_get_contents(path, &css, NULL, NULL));
		/* A bare fr track grows to its longest word and pushes items apart. */
		if (g_regex_match(bare_fr, css, 0, NULL))
			g_error("%s: grid track without a minmax(0, ...) floor", name);
		/* A bare auto track beside a zero-floor track takes all the room and
		 * collapses its neighbour to a letter per line. */
		if (g_regex_match(auto_track, css, 0, NULL))
			g_error("%s: bare auto track beside a shrinkable one", name);
		/* Headings never get automatic hyphens. */
		if (strstr(css, "hyphens: auto") != NULL)
			g_error("%s: automatic hyphenation", name);
		/* Component type follows the page column, not the screen. */
		if (!g_str_equal(name, "base.css") && !g_str_equal(name, "look.css") && g_regex_match(viewport, css, 0, NULL))
			g_error("%s: viewport width unit in a component", name);
		if (g_str_equal(name, "base.css") || g_str_equal(name, "look.css"))
			continue;
		/* Prices, names and links wrap rather than push the page sideways. */
		if (strstr(css, "white-space: nowrap") != NULL && !g_str_equal(name, "jumpnav.css") &&
			!g_str_equal(name, "marquee.css"))
			g_error("%s: unwrappable text", name);
	}
	{
		g_autofree gchar *look = NULL;
		g_assert_true(g_file_get_contents("data/components/look.css", &look, NULL, NULL));
		/* Sites fill the screen: no frame draws a border around the page. */
		lacks(look, "look-frame-inset");
		/* Alternating bands never repaint a section that has its own colours. */
		contains(look, ".look-rhythm-bands main > .block:nth-of-type(even):not(.cta--band, .statement--band,");
		/* Side panels never wrap into a second column. */
		contains(look, ":is(.look-frame-rail, .look-frame-split) .site-header__inner { flex-wrap: nowrap;");
		/* Pill links keep a gap even where a side panel zeroes the list gap, and a
		 * centred header centres its links, not only the list box. */
		contains(look, "body.look-nav-pill .site-nav ul { gap: var(--space-2); }");
		contains(look, ".look-header-centered .site-nav, .look-header-centered .site-nav ul { justify-content: center; }");
		/* The offset margin is added beside a dealt column, never taken out of it. */
		contains(look, "var(--max-width) + var(--ls-offset)");
		/* One frame per picture, and turned pictures never widen the page. */
		contains(look, ".look-images-framed main img:not(.cover img, .media--framed img, .brand img)");
		contains(look, ":is(.look-images-taped, .look-photos-print) main { overflow-x: clip; }");
		contains(look, "main > .block:first-child:not(.cover--full, .cover--split) img");
	}
	{
		g_autofree gchar *cover = NULL;
		g_autofree gchar *media = NULL;
		g_autofree gchar *story = NULL;
		g_autofree gchar *gallery = NULL;
		g_assert_true(g_file_get_contents("data/components/cover.css", &cover, NULL, NULL));
		g_assert_true(g_file_get_contents("data/components/media.css", &media, NULL, NULL));
		g_assert_true(g_file_get_contents("data/components/story.css", &story, NULL, NULL));
		g_assert_true(g_file_get_contents("data/components/gallery.css", &gallery, NULL, NULL));
		/* A full cover's picture fills its cell; the text sits on a panel, not a full-width fade. */
		contains(cover, ".cover--full .cover__art img { position: absolute; inset: 0;");
		lacks(cover, "linear-gradient(to top");
		/* Side-by-side layouts follow the page column, not the screen. */
		lacks(media, "@media (min-width: 56rem)");
		lacks(cover, "@media (min-width: 56rem)");
		/* The pinned story never leaves screens of empty page. */
		lacks(story, "30vh");
		contains(gallery, ".gallery:not(.gallery--masonry) .gallery__item img { aspect-ratio: 4 / 3;");
	}
}

/* Long email and web addresses break at a seam in narrow boxes, never mid-word. */
static void
test_address_breaks(void){
	g_autoptr(LsComponents) components = library();
	g_autoptr(JsonNode) plan = load("plan.json");
	g_autoptr(JsonObject) pages = fixture_pages();
	g_autoptr(GError) error = NULL;
	g_autofree gchar *text = NULL;
	g_autoptr(JsonNode) bundle = NULL;
	const gchar *html;
	json_object_set_member(pages, "visit.html", json(
		"{'path':'visit.html','sections':[{'component':'page-header','content':{'heading':'Visit'}},"
		"{'component':'contact-card','content':{'heading':'Write to us','body':'Or mail hello@lanternandleafbooks.com any time.',"
		"'items':[{'label':'Email','value':'hello@lanternandleafbooks.com','link':{'href':'mailto:hello@lanternandleafbooks.com','label':'Email'}},"
		"{'label':'Phone','value':'(555) 013-4408'}]}}]}"));
	text = ls_components_compose(components, plan, pages, NULL, &error);
	g_assert_no_error(error);
	bundle = ls_json_parse(text, NULL);
	html = file_content(bundle, "visit.html");
	contains(html, ">hello@<wbr>lanternandleafbooks<wbr>.com</a>");
	contains(html, "mail hello@<wbr>lanternandleafbooks<wbr>.com any time.");
	/* The link target and short values stay as they are. */
	contains(html, "href=\"mailto:hello@lanternandleafbooks.com\"");
	contains(html, "<dd>(555) 013-4408</dd>");
	lacks(file_content(bundle, "styles.css"), "font-size: var(--step-1); overflow-wrap: anywhere; }");
}

/* Accent text reads on the surface tint too, and accent-painted panels swap
 * their tokens so a restyle using var(--accent) cannot vanish into them. */
static void
test_accent_text(void){
	g_autoptr(LsComponents) components = library();
	g_autoptr(JsonNode) plan = load("plan.json");
	g_autoptr(JsonObject) pages = fixture_pages();
	g_autoptr(GError) error = NULL;
	g_autofree gchar *text = NULL, *repairs = NULL;
	g_autoptr(JsonNode) bundle = NULL;
	JsonObject *tokens = json_object_get_object_member(json_node_get_object(plan), "tokens");
	const gchar *css;
	json_object_set_string_member(tokens, "paper", "#ffffff");
	json_object_set_string_member(tokens, "surface", "#dcdcdc");
	json_object_set_string_member(tokens, "ink", "#111111");
	json_object_set_string_member(tokens, "muted", "#3a3a3a");
	json_object_set_string_member(tokens, "accent", "#737373");
	json_object_set_string_member(tokens, "accent_ink", "#ffffff");
	repairs = ls_components_repair_plan(components, plan);
	text = ls_components_compose(components, plan, pages, NULL, &error);
	g_assert_no_error(error);
	bundle = ls_json_parse(text, NULL);
	css = file_content(bundle, "styles.css");
	/* #737373 passes on white (4.7:1) but not on the #dcdcdc surface. */
	lacks(css, "--accent-text: #737373;");
	contains(css, "--accent-text: #");
	contains(css, "main .offer__stub, main .notice--accent .notice__box, main .split--contrast .split__panel:last-child {");
	/* A tall stacked header never pins over the page. */
	contains(css, ".look-header-glass:is(.look-frame-center, .look-frame-masthead) .site-header { position: relative; }");
}

/* A supplied form is the builder's: whatever the model wrote, the form posts
 * the order's fields to the order's same-site path, with its honeypot. */
static void
test_supplied_form(void){
	g_autoptr(LsComponents) components = library();
	g_autoptr(JsonNode) plan = load("plan.json");
	g_autoptr(JsonObject) pages = fixture_pages();
	g_autoptr(JsonNode) page = json(
		"{'path':'visit.html','sections':[{'component':'page-header','content':{'heading':'Visit'}},"
		"{'component':'form','content':{'heading':'Write to us','mode':'local','download_name':'note.txt',"
		"'submit':'Send','fields':[{'name':'invented','label':'Invented field'}]}}]}");
	g_autoptr(GError) error = NULL;
	g_autofree gchar *text = NULL;
	g_autoptr(JsonNode) bundle = NULL;
	const gchar *html;
	JsonObject *root = json_node_get_object(plan);
	json_object_set_member(root, "form", json(
		"{'action':'/api/v1/sites/harbor/enquiries','honeypot':'website','fields':["
		"{'name':'first-name','label':'First name','required':true,'autocomplete':'given-name'},"
		"{'name':'last-name','label':'Last name','required':true,'autocomplete':'family-name'},"
		"{'name':'email','label':'Email','type':'email','required':true,'autocomplete':'email'},"
		"{'name':'message','label':'Message','type':'textarea','required':true,'maxlength':4000}]}"));
	g_assert_true(ls_components_check_plan(components, plan, &error));
	g_assert_no_error(error);
	/* The model's local mode, download name and invented field give way. */
	g_assert_true(ls_components_check_page(components, plan, "visit.html", page, &error));
	g_assert_no_error(error);
	json_object_set_member(pages, "visit.html", json_node_copy(page));
	text = ls_components_compose(components, plan, pages, NULL, &error);
	g_assert_no_error(error);
	bundle = ls_json_parse(text, NULL);
	html = file_content(bundle, "visit.html");
	contains(html, "action=\"/api/v1/sites/harbor/enquiries\" method=\"post\" data-endpoint-form");
	/* The confirmation is a panel in the page, not a message on the form. */
	lacks(html, "data-sent=");
	contains(html, "<div class=\"form__sent\" role=\"status\" aria-live=\"polite\" tabindex=\"-1\" data-form-sent hidden>");
	/* A field the host refuses is marked in the site's own words. */
	contains(html, "data-failed=\"Your message was not sent. Please try again.\" data-error-invalid=\"Check this entry.\"");
	contains(html, "data-failed=\"Your message was not sent. Please try again.\"");
	contains(html, "role=\"alert\" data-form-summary hidden");
	contains(html, "name=\"first-name\" required autocomplete=\"given-name\"");
	contains(html, "autocomplete=\"given-name\"");
	contains(html, "<textarea id=");
	contains(html, "name=\"message\" rows=\"5\" required maxlength=\"4000\"");
	contains(html, "name=\"website\" tabindex=\"-1\" autocomplete=\"off\"");
	/* The honeypot is out of reach: no focus, no click and nothing for a screen
	 * reader to read out. It stays enabled, so a filled value is still posted. */
	contains(html, "<div class=\"form__trap\" aria-hidden=\"true\" inert><label>Leave this field empty <input type=\"text\" name=\"website\" tabindex=\"-1\" autocomplete=\"off\"></label></div>");
	contains(html, "role=\"status\" data-form-status");
	lacks(html, "invented");
	lacks(html, "data-local-form");
	contains(file_content(bundle, "site.js"), "[data-endpoint-form]");
	/* A plan whose supplied form lost its shape is refused. */
	json_object_set_member(root, "form", json("{'action':'//evil.example/x','fields':[]}"));
	g_assert_false(ls_components_check_plan(components, plan, &error));
	g_assert_nonnull(strstr(error->message, "form must be the supplied"));
	g_clear_error(&error);
	/* Without a supplied form, endpoint mode takes a same-site path, never a
	 * protocol-relative or http address. */
	json_object_remove_member(root, "form");
	{
		const gchar *targets[] = {"/api/forms/contact", "//evil.example/x", "http://example.com/x", "/a?b=1"};
		const gboolean ok[] = {TRUE, FALSE, FALSE, FALSE};
		guint i;
		for (i = 0; i < G_N_ELEMENTS(targets); i++) {
			g_autofree gchar *spec = g_strdup_printf(
				"{'path':'visit.html','sections':[{'component':'page-header','content':{'heading':'Visit'}},"
				"{'component':'form','content':{'heading':'Write','mode':'endpoint','target':'%s','submit':'Send',"
				"'fields':[{'name':'email','label':'Email','type':'email'}]}}]}", targets[i]);
			g_autoptr(JsonNode) candidate = json(spec);
			g_assert_cmpint(ls_components_check_page(components, plan, "visit.html", candidate, NULL), ==, ok[i]);
		}
	}
}

/* Every page of a composed bundle, joined. */
static gchar *
all_pages(JsonNode *bundle){
	JsonArray *files = json_object_get_array_member(json_node_get_object(bundle), "files");
	GString *out = g_string_new(NULL);
	guint i;
	for (i = 0; i < json_array_get_length(files); i++) {
		JsonObject *file = json_array_get_object_element(files, i);
		if (g_str_has_suffix(ls_string(file, "path"), ".html"))
			g_string_append(out, ls_string(file, "content"));
	}
	return g_string_free(out, FALSE);
}

static JsonNode *
compose_bundle(LsComponents *components, JsonNode *plan, JsonObject *pages){
	g_autoptr(GError) error = NULL;
	g_autofree gchar *text = ls_components_compose(components, plan, pages, "https://harbor.example/", &error);
	g_assert_no_error(error);
	g_assert_nonnull(text);
	return ls_json_parse(text, NULL);
}

/* The confirmation panel right before the form @form_id on @html, from its
 * opening tag to its end; it holds no nested div and carries no id. */
static gchar *
sent_panel(const gchar *html, const gchar *form_id){
	static const gchar open[] = "<div class=\"form__sent\" role=\"status\" aria-live=\"polite\" "
		"tabindex=\"-1\" data-form-sent hidden>";
	g_autofree gchar *form = g_strdup_printf("</div>\n<form class=\"form__form\" id=\"%s\"", form_id);
	const gchar *start = html;
	while (start != NULL && (start = strstr(start, open)) != NULL) {
		const gchar *end = strstr(start, "</div>");
		if (end != NULL && g_str_has_prefix(end, form))
			return g_strndup(start, end - start);
		start += sizeof open - 1;
	}
	g_error("no confirmation panel before '%s' in:\n%.4000s", form_id, html != NULL ? html : "(null)");
	return NULL;
}

static guint
occurrences(const gchar *text, const gchar *needle){
	guint n = 0;
	const gchar *at = text;
	while ((at = strstr(at, needle)) != NULL) {
		n++;
		at += strlen(needle);
	}
	return n;
}

/* Every honeypot a template draws is out of reach and still a successful
 * control: inert, so no visitor can focus it, click it or hear its label,
 * and not disabled, so a filled value is still posted. */
static void
test_honeypots_inert(void){
	g_autoptr(GDir) dir = g_dir_open("data/components", 0, NULL);
	const gchar *name;
	guint traps = 0;
	g_assert_nonnull(dir);
	while ((name = g_dir_read_name(dir)) != NULL) {
		g_autofree gchar *path = NULL;
		g_autofree gchar *text = NULL;
		if (!g_str_has_suffix(name, ".html"))
			continue;
		path = g_build_filename("data/components", name, NULL);
		g_assert_true(g_file_get_contents(path, &text, NULL, NULL));
		g_test_message("%s", name);
		traps += occurrences(text, "class=\"form__trap\"");
		g_assert_cmpuint(occurrences(text, "class=\"form__trap\""), ==,
			occurrences(text, "<div class=\"form__trap\" aria-hidden=\"true\" inert>"));
		g_assert_cmpuint(occurrences(text, "class=\"form__trap\""), ==,
			occurrences(text, "name=\"{{honeypot}}\" tabindex=\"-1\" autocomplete=\"off\"></label>"));
	}
	g_assert_cmpuint(traps, >=, 3);
}


/* A supplied booking page puts "Book online" in the action bar on every page,
 * beside the call and quote actions, and alone when neither is supplied. The
 * label is an interface string a plan may replace; an invalid booking is
 * refused like any other supplied fact. */
static void
test_booking_action(void){
	g_autoptr(LsComponents) components = library();
	g_autoptr(JsonNode) plan = load("plan.json");
	g_autoptr(JsonObject) pages = fixture_pages();
	g_autoptr(GError) error = NULL;
	g_autoptr(JsonNode) bundle = NULL;
	g_autofree gchar *every = NULL;
	const gchar *home;
	JsonObject *root = json_node_get_object(plan);
	json_object_set_member(root, "booking", json("{'href':'/book'}"));
	g_assert_true(ls_components_check_plan(components, plan, &error));
	g_assert_no_error(error);
	bundle = compose_bundle(components, plan, pages);
	home = file_content(bundle, "index.html");
	contains(home, "<nav class=\"action-bar\" aria-label=\"Get in touch\">");
	contains(home, "<a class=\"button action-bar__book\" href=\"/book\">Book online</a>");
	contains(file_content(bundle, "visit.html"), "class=\"block block--booking-calendar");
	/* On the page with the calendar, the action leads to it, not to itself. */
	contains(file_content(bundle, "visit.html"), "<a class=\"button action-bar__book\" href=\"#choose-a-time\">");
	lacks(file_content(bundle, "visit.html"), "href=\"/book\"");
	lacks(home, "action-bar__call");
	every = all_pages(bundle);
	g_assert_cmpuint(occurrences(every, "action-bar__book"), >=, 2);
	g_clear_pointer(&every, g_free);
	g_clear_pointer(&bundle, json_node_unref);
	/* With a phone and a live form too, the bar still holds one primary
	 * action: the supplied booking page. */
	json_object_set_member(root, "contact", json("{'phone':'+1 (555) 014-2270'}"));
	json_object_set_member(root, "form", json(
		"{'action':'/api/v1/sites/harbor/enquiries','fields':[{'name':'email','label':'Email','type':'email','required':true}]}"));
	json_object_set_member(root, "ui", json("{'book':'Book a visit'}"));
	bundle = compose_bundle(components, plan, pages);
	home = file_content(bundle, "index.html");
	contains(home, "<a class=\"button action-bar__book\" href=\"/book\">Book a visit</a>");
	lacks(home, "action-bar__call");
	lacks(home, "action-bar__quote");
	g_assert_cmpuint(occurrences(home, "class=\"button action-bar__"), ==, 1);
	g_clear_pointer(&bundle, json_node_unref);
	/* Only the supplied href, nothing else. */
	json_object_set_member(root, "booking", json("{'href':'javascript:alert(1)'}"));
	g_assert_false(ls_components_check_plan(components, plan, &error));
	g_assert_nonnull(strstr(error->message, "booking"));
	g_clear_error(&error);
	json_object_remove_member(root, "booking");
	bundle = compose_bundle(components, plan, pages);
	every = all_pages(bundle);
	lacks(every, "action-bar__book");
}

/* Booking stays on the site's page, using the host's route for times. The
 * page never links to itself as its booking action. Without a way to send
 * there is no request form; with the supplied email, the request form
 * that stands in for unloaded times sends by mail. */
static void
test_booking_calendar(void){
	g_autoptr(LsComponents) components = library();
	g_autoptr(JsonNode) plan = load("plan.json");
	g_autoptr(JsonObject) pages = fixture_pages();
	g_autoptr(JsonNode) bundle = NULL;
	g_autoptr(GError) error = NULL;
	JsonObject *root = json_node_get_object(plan);
	JsonObject *visit = json_node_get_object(json_object_get_member(pages, "visit.html"));
	JsonArray *sections = json_object_get_array_member(visit, "sections");
	const gchar *html, *script, *style, *form, *privacy, *terms, *name;
	json_object_set_member(root, "booking", json("{'href':'/book'}"));
	json_array_add_element(sections, json("{'component':'booking-calendar','variant':'month',"
		"'content':{'heading':'Choose a time'}}"));
	bundle = compose_bundle(components, plan, pages);
	html = file_content(bundle, "visit.html");
	script = file_content(bundle, "site.js");
	style = file_content(bundle, "styles.css");
	contains(html, "data-booking-url=\"/book\"");
	lacks(html, "href=\"/book\"");
	lacks(html, "data-booking-request");
	contains(html, "name=\"start\"");
	contains(html, "name=\"name\"");
	contains(html, "name=\"email\"");
	contains(html, "name=\"notes\"");
	form = strstr(html, "data-booking-form");
	g_assert_nonnull(form);
	privacy = strstr(form, "href=\"privacy.html\"");
	terms = strstr(form, "href=\"terms.html\"");
	name = strstr(form, "name=\"name\"");
	g_assert_nonnull(privacy);
	g_assert_nonnull(terms);
	g_assert_true(privacy < name && terms < name);
	contains(html, "aria-live=\"polite\"");
	contains(script, "format=json");
	contains(script, "if (Array.isArray(data)) return data;");
	contains(script, "Array.isArray(data.slots)");
	contains(style, ".booking-calendar");
	g_clear_pointer(&bundle, json_node_unref);
	json_object_set_member(root, "contact", json("{'email':'hello@bench.example'}"));
	bundle = compose_bundle(components, plan, pages);
	html = file_content(bundle, "visit.html");
	contains(html, "data-booking-request");
	contains(html, "action=\"mailto:hello@bench.example\"");
	lacks(html, "href=\"/book\"");
	g_clear_pointer(&bundle, json_node_unref);
	/* No other form on the page, so the calendar draws its own request form.
	 * That form posts the supplied honeypot, inert and still named. */
	json_object_remove_member(root, "contact");
	json_object_set_member(root, "form", json(
		"{'action':'/api/v1/sites/harbor/enquiries','honeypot':'slot','fields':["
		"{'name':'email','label':'Email','type':'email','required':true}]}"));
	bundle = compose_bundle(components, plan, pages);
	html = file_content(bundle, "visit.html");
	contains(html, "data-booking-request");
	contains(html, "action=\"/api/v1/sites/harbor/enquiries\"");
	contains(html, "<div class=\"form__trap\" aria-hidden=\"true\" inert><label>Leave this field empty <input type=\"text\" name=\"slot\" tabindex=\"-1\" autocomplete=\"off\"></label></div>");
	g_clear_pointer(&bundle, json_node_unref);
	json_object_remove_member(root, "form");
	json_object_remove_member(root, "booking");
	g_assert_false(ls_components_check_page(components, plan, "visit.html",
		json_object_get_member(pages, "visit.html"), &error));
	g_assert_nonnull(strstr(error->message, "booking"));
}

static void
test_booking_calendar_default_page(void){
	g_autoptr(LsComponents) components = library();
	g_autoptr(JsonNode) plan = load("plan.json");
	g_autoptr(JsonObject) pages = fixture_pages();
	g_autoptr(JsonNode) bundle = NULL;
	JsonObject *root = json_node_get_object(plan);
	JsonArray *planned = json_object_get_array_member(root, "pages");
	json_object_set_member(root, "booking", json("{'href':'/book'}"));
	json_array_add_element(planned, json("{'path':'contact.html','kind':'contact','title':'Contact',"
		"'description':'Reach us and choose a time.','brief':'Contact'}"));
	json_object_set_member(pages, "contact.html", json("{'path':'contact.html',"
		"'sections':[{'component':'page-header','content':{'heading':'Contact'}}]}"));
	bundle = compose_bundle(components, plan, pages);
	contains(file_content(bundle, "contact.html"), "class=\"block block--booking-calendar");
	lacks(file_content(bundle, "visit.html"), "class=\"block block--booking-calendar");
	/* Composition did not change the caller's page specification. */
	g_assert_cmpuint(json_array_get_length(json_object_get_array_member(
		json_node_get_object(json_object_get_member(pages, "contact.html")), "sections")), ==, 1);
}

/* A site with a supplied form whose pages left every form out still gets one,
 * on its contact page: the model kept the calendar but dropped the enquiry
 * form, and visitors had no way to send a question. A site that already
 * shows a form gets no second one. */
static void
test_supplied_form_default_page(void){
	g_autoptr(LsComponents) components = library();
	g_autoptr(JsonNode) plan = load("plan.json");
	g_autoptr(JsonObject) pages = fixture_pages();
	g_autoptr(JsonNode) bundle = NULL;
	g_autoptr(JsonNode) kept = NULL;
	JsonObject *root = json_node_get_object(plan);
	JsonArray *planned = json_object_get_array_member(root, "pages");
	const gchar *contact;
	json_object_set_member(root, "form", json(
		"{'action':'/api/v1/sites/harbor/enquiries','honeypot':'website','fields':["
		"{'name':'first-name','label':'First name','type':'text','required':true},"
		"{'name':'message','label':'Message','type':'textarea','required':true}]}"));
	json_object_set_member(root, "booking", json("{'href':'/book'}"));
	json_array_add_element(planned, json("{'path':'contact.html','kind':'contact','title':'Contact',"
		"'description':'Reach us and choose a time.','brief':'Contact'}"));
	/* The model kept the calendar and dropped the enquiry form. */
	json_object_set_member(pages, "contact.html", json("{'path':'contact.html',"
		"'sections':[{'component':'page-header','content':{'heading':'Contact and quote requests'}},"
		"{'component':'booking-calendar','variant':'month','content':{'heading':'Choose a time'}}]}"));
	/* The start page's form is the only one; without it no page has a form. */
	json_object_set_member(pages, "start.html", json("{'path':'start.html',"
		"'sections':[{'component':'page-header','content':{'heading':'Start'}}]}"));
	bundle = compose_bundle(components, plan, pages);
	contact = file_content(bundle, "contact.html");
	contains(contact, "data-endpoint-form");
	contains(contact, "action=\"/api/v1/sites/harbor/enquiries\"");
	contains(contact, ">Send a message");
	contains(contact, ">Send</button>");
	contains(contact, "name=\"website\"");
	contains(contact, "<div class=\"form__trap\" aria-hidden=\"true\" inert>");
	contains(contact, ">First name");
	contains(contact, ">Choose a time");
	g_assert_cmpuint(occurrences(contact, "class=\"block block--booking-calendar"), ==, 1);
	g_assert_true(strstr(contact, "Choose a time") < strstr(contact, "Send a message"));
	lacks(file_content(bundle, "visit.html"), "data-endpoint-form");
	lacks(file_content(bundle, "index.html"), "data-endpoint-form");
	/* Booking is the one primary action while a booking page is supplied. */
	contains(file_content(bundle, "index.html"), "href=\"/book\"");
	/* Composition did not change the caller's page specification. */
	g_assert_cmpuint(json_array_get_length(json_object_get_array_member(
		json_node_get_object(json_object_get_member(pages, "contact.html")), "sections")), ==, 2);
	/* Without a booking page, the calendar is not part of the page, and the
	 * quote action leads to the form composition placed. */
	json_object_remove_member(root, "booking");
	json_object_set_member(pages, "contact.html", json("{'path':'contact.html',"
		"'sections':[{'component':'page-header','content':{'heading':'Contact and quote requests'}}]}"));
	g_clear_pointer(&bundle, json_node_unref);
	bundle = compose_bundle(components, plan, pages);
	contains(file_content(bundle, "contact.html"), "data-endpoint-form");
	contains(file_content(bundle, "index.html"), "href=\"contact.html#");
	/* With the start page's own form back, contact gets no second one. */
	json_object_set_member(pages, "start.html", load("start.json"));
	kept = compose_bundle(components, plan, pages);
	lacks(file_content(kept, "contact.html"), "data-endpoint-form");
	contains(file_content(kept, "start.html"), "data-endpoint-form");
}

/* A full contact page cannot grow, so the form lands on the visit page.
 * When that page is full too, a page of kind form still can. Nothing here
 * changes the caller's specifications. */
static void
test_supplied_form_placement(void){
	g_autoptr(LsComponents) components = library();
	g_autoptr(JsonNode) plan = load("plan.json");
	g_autoptr(JsonObject) pages = fixture_pages();
	g_autoptr(JsonNode) bundle = NULL;
	JsonObject *root = json_node_get_object(plan);
	JsonArray *planned = json_object_get_array_member(root, "pages");
	JsonArray *contact_sections;
	JsonArray *visit_sections;
	guint n;
	json_object_set_member(root, "form", json(
		"{'action':'/api/v1/sites/harbor/enquiries','fields':["
		"{'name':'message','label':'Message','type':'textarea','required':true}]}"));
	json_array_add_element(planned, json("{'path':'contact.html','kind':'contact','title':'Contact',"
		"'description':'Reach us.','brief':'Contact'}"));
	json_object_set_member(pages, "contact.html", json("{'path':'contact.html',"
		"'sections':[{'component':'page-header','content':{'heading':'Contact'}}]}"));
	json_object_set_member(pages, "start.html", json("{'path':'start.html',"
		"'sections':[{'component':'page-header','content':{'heading':'Start'}}]}"));
	contact_sections = json_object_get_array_member(
		json_node_get_object(json_object_get_member(pages, "contact.html")), "sections");
	for (n = json_array_get_length(contact_sections); n < 16; n++)
		json_array_add_element(contact_sections, json("{'component':'text','content':{'heading':'Extra note',"
			"'body':'A short note.'}}"));
	bundle = compose_bundle(components, plan, pages);
	contains(file_content(bundle, "visit.html"), "data-endpoint-form");
	contains(file_content(bundle, "visit.html"), ">Send a message");
	lacks(file_content(bundle, "contact.html"), "data-endpoint-form");
	lacks(file_content(bundle, "start.html"), "data-endpoint-form");
	g_assert_cmpuint(json_array_get_length(contact_sections), ==, 16);
	g_clear_pointer(&bundle, json_node_unref);
	visit_sections = json_object_get_array_member(
		json_node_get_object(json_object_get_member(pages, "visit.html")), "sections");
	for (n = json_array_get_length(visit_sections); n < 16; n++)
		json_array_add_element(visit_sections, json("{'component':'text','content':{'heading':'Extra note',"
			"'body':'A short note.'}}"));
	bundle = compose_bundle(components, plan, pages);
	contains(file_content(bundle, "start.html"), "data-endpoint-form");
	lacks(file_content(bundle, "visit.html"), "data-endpoint-form");
	lacks(file_content(bundle, "contact.html"), "data-endpoint-form");
	g_assert_cmpuint(json_array_get_length(visit_sections), ==, 16);
}

/* "facebook.html" contains the letters book, and a full visit page cannot
 * grow. Neither becomes the calendar; contact still can. */
static void
test_booking_calendar_placement(void){
	g_autoptr(LsComponents) components = library();
	g_autoptr(JsonNode) plan = load("plan.json");
	g_autoptr(JsonObject) pages = fixture_pages();
	g_autoptr(JsonNode) bundle = NULL;
	JsonObject *root = json_node_get_object(plan);
	JsonArray *planned = json_object_get_array_member(root, "pages");
	JsonArray *sections = json_object_get_array_member(
		json_node_get_object(json_object_get_member(pages, "visit.html")), "sections");
	guint n;
	json_object_set_member(root, "booking", json("{'href':'/book'}"));
	json_array_add_element(planned, json("{'path':'facebook.html','kind':'general','title':'Notes',"
		"'description':'A page whose name contains book.','brief':'Notes'}"));
	json_object_set_member(pages, "facebook.html", json("{'path':'facebook.html',"
		"'sections':[{'component':'page-header','content':{'heading':'Notes'}}]}"));
	bundle = compose_bundle(components, plan, pages);
	contains(file_content(bundle, "visit.html"), "class=\"block block--booking-calendar");
	lacks(file_content(bundle, "facebook.html"), "class=\"block block--booking-calendar");
	g_clear_pointer(&bundle, json_node_unref);
	for (n = json_array_get_length(sections); n < 16; n++)
		json_array_add_element(sections, json("{'component':'text','content':{'heading':'Extra note',"
			"'body':'A short note.'}}"));
	bundle = compose_bundle(components, plan, pages);
	contains(file_content(bundle, "index.html"), "action-bar__book");
	lacks(file_content(bundle, "visit.html"), "class=\"block block--booking-calendar");
	lacks(file_content(bundle, "facebook.html"), "class=\"block block--booking-calendar");
}

/* A supplied phone becomes a click-to-call link that stays in reach on every
 * page, and a quote action leads to the live form. After a send the visitor
 * stays on the page: each live form carries a hidden confirmation panel,
 * shown in place only by the script (after a JSON "received", or when the
 * host returns to ?sent=1); it repeats the number when there is one. There
 * is no thank-you page and no :target or #sent anchor. Without a phone
 * there are no call parts. */
static void
test_contact_actions(void){
	g_autoptr(LsComponents) components = library();
	g_autoptr(JsonNode) plan = load("plan.json");
	g_autoptr(JsonObject) pages = fixture_pages();
	g_autoptr(GError) error = NULL;
	g_autoptr(JsonNode) bundle = NULL;
	g_autoptr(JsonNode) call_page = json(
		"{'path':'journal.html','sections':[{'component':'page-header','content':{'heading':'Notes'}},"
		"{'component':'call','variant':'band','content':{'heading':'Talk to the bench'}}]}");
	g_autofree gchar *every = NULL;
	g_autofree gchar *panel = NULL;
	const gchar *home, *visit, *journal, *css, *js;
	JsonObject *root = json_node_get_object(plan);
	json_object_set_member(root, "form", json(
		"{'action':'/api/v1/sites/harbor/enquiries','fields':["
		"{'name':'email','label':'Email','type':'email','required':true},"
		"{'name':'message','label':'Message','type':'textarea','required':true}]}"));
	/* Two forms on one page, and a heading whose slug would be "sent". */
	json_object_set_member(pages, "visit.html", json(
		"{'path':'visit.html','sections':[{'component':'page-header','content':{'heading':'Sent'}},"
		"{'component':'form','id':'quote-form','content':{'heading':'Ask for an estimate','mode':'endpoint',"
		"'target':'/x','submit':'Send','fields':[{'name':'email','label':'Email'}]}},"
		"{'component':'form','id':'second','content':{'heading':'Or write again','mode':'endpoint',"
		"'target':'/x','submit':'Send','fields':[{'name':'email','label':'Email'}]}}]}"));
	json_object_set_member(root, "contact", json("{'phone':'+1 (555) 014-2270','email':'hello@harbor.example'}"));
	g_assert_true(ls_components_check_plan(components, plan, &error));
	g_assert_no_error(error);
	/* The builder draws the number in a call section; the model writes only the words. */
	g_assert_true(ls_components_check_page(components, plan, "journal.html", call_page, &error));
	g_assert_no_error(error);
	json_object_set_member(pages, "journal.html", json_node_copy(call_page));
	bundle = compose_bundle(components, plan, pages);
	home = file_content(bundle, "index.html");
	contains(home, "<nav class=\"action-bar\" aria-label=\"Get in touch\">");
	contains(home, "<a class=\"button action-bar__call\" href=\"tel:+15550142270\">");
	contains(home, "<span class=\"action-bar__number\">+1 (555) 014-2270</span>");
	/* On a phone the action is as wide as its words, so the number does not
	 * break across lines. */
	contains(file_content(bundle, "styles.css"), ".action-bar .button { flex: 1 1 auto; justify-content: center; text-align: center; }");
	/* One primary action: the phone outranks the live form's quote action. */
	lacks(home, "action-bar__quote");
	g_assert_cmpuint(occurrences(home, "class=\"button action-bar__"), ==, 1);
	/* The bar sits in the page column, after the content, so it is in reach without covering the footer. */
	g_assert_true(strstr(home, "action-bar") < strstr(home, "</main>"));
	/* No separate thank-you page anywhere. */
	g_assert_null(file_content(bundle, "thanks.html"));
	every = all_pages(bundle);
	lacks(every, "thanks.html");
	lacks(every, "data-thanks");
	lacks(file_content(bundle, "sitemap.xml"), "thanks.html");
	visit = file_content(bundle, "visit.html");
	contains(visit, "action=\"/api/v1/sites/harbor/enquiries\" method=\"post\" data-endpoint-form");
	/* Panels carry no id and reserve none: a heading whose slug is "sent"
	 * keeps that id, and it is the only one on the page. */
	g_assert_cmpuint(occurrences(visit, "id=\"sent\""), ==, 1);
	lacks(visit, "id=\"sent-2\"");
	lacks(visit, "-sent\"");
	g_assert_cmpuint(occurrences(visit, "data-form-sent hidden>"), ==, 2);
	panel = sent_panel(visit, "quote-form-form");
	contains(panel, "<p class=\"form__sent-message\">Thank you. Your message was sent.</p>");
	contains(panel, "<a class=\"button form__sent-call\" href=\"tel:+15550142270\">Call "
		"<span class=\"form__sent-number\">+1 (555) 014-2270</span></a>");
	contains(panel, "<a class=\"form__sent-again\" href=\"#quote-form-form\" data-form-again>Send another message</a>");
	g_clear_pointer(&panel, g_free);
	panel = sent_panel(visit, "second-form");
	contains(panel, "href=\"tel:+15550142270\"");
	g_clear_pointer(&panel, g_free);
	/* Every supplied form gets a panel, including the quote action's form. */
	panel = sent_panel(file_content(bundle, "start.html"), "repair-request-form");
	contains(panel, "href=\"#repair-request-form\"");
	g_clear_pointer(&panel, g_free);
	journal = file_content(bundle, "journal.html");
	contains(journal, "call--band");
	contains(journal, "href=\"tel:+15550142270\"");
	css = file_content(bundle, "styles.css");
	contains(css, ".action-bar {");
	contains(css, "position: sticky;");
	/* Without scripts the panel stays hidden: nothing in the stylesheet
	 * shows it from the address. A site wanting a no-script confirmation
	 * sets the host's thank-you address. */
	lacks(css, ":target");
	contains(css, ".form__sent:focus { outline: 3px solid var(--focus);");
	js = file_content(bundle, "site.js");
	contains(js, "[data-form-sent]");
	contains(js, "/[?&]sent=1(&|$)/");
	contains(js, "/[?&]error=([^&]*)/");
	lacks(js, "#sent");
	lacks(js, "data-thanks");
	g_clear_pointer(&bundle, json_node_unref);
	g_clear_pointer(&every, g_free);
	/* The plan's contact is the checked one the builder wrote. */
	json_object_set_member(root, "contact", json("{'phone':'call us'}"));
	g_assert_false(ls_components_check_plan(components, plan, &error));
	g_assert_nonnull(strstr(error->message, "contact"));
	g_clear_error(&error);
	json_object_set_member(root, "contact", json("{'phone':'+1 555 0100','address':'1 Quay Street'}"));
	g_assert_false(ls_components_check_plan(components, plan, &error));
	g_clear_error(&error);
	/* No phone: no call link, no call section, nothing invented; the confirmation stays. */
	json_object_set_member(root, "contact", json("{'email':'hello@harbor.example'}"));
	g_assert_false(ls_components_check_page(components, plan, "journal.html", call_page, &error));
	g_assert_nonnull(strstr(error->message, "call"));
	g_clear_error(&error);
	json_object_remove_member(pages, "journal.html");
	json_object_set_member(pages, "journal.html", load("journal.json"));
	bundle = compose_bundle(components, plan, pages);
	every = all_pages(bundle);
	lacks(every, "tel:");
	lacks(every, "action-bar__call");
	lacks(every, "call__");
	lacks(every, "form__sent-call");
	contains(file_content(bundle, "index.html"),
		"<a class=\"button action-bar__quote\" href=\"start.html#repair-request\">Request a quote</a>");
	panel = sent_panel(file_content(bundle, "visit.html"), "quote-form-form");
	contains(panel, "Thank you. Your message was sent.");
	g_assert_null(file_content(bundle, "thanks.html"));
	g_clear_pointer(&panel, g_free);
	g_clear_pointer(&bundle, json_node_unref);
	g_clear_pointer(&every, g_free);
	/* No phone and no live form: no bar, no confirmation panel. */
	json_object_remove_member(root, "contact");
	json_object_remove_member(root, "form");
	json_object_set_member(pages, "visit.html", load("visit.json"));
	bundle = compose_bundle(components, plan, pages);
	every = all_pages(bundle);
	lacks(every, "action-bar");
	lacks(every, "data-form-sent");
	g_assert_null(file_content(bundle, "thanks.html"));
	/* thanks.html is an ordinary route now: the builder no longer reserves it. */
	{
		g_autoptr(JsonNode) planned = load("plan.json");
		JsonArray *list = json_object_get_array_member(json_node_get_object(planned), "pages");
		json_array_add_element(list, json("{'path':'thanks.html','title':'Thanks','description':'Thanks.',"
			"'brief':'Thanks','kind':'general'}"));
		if (!ls_components_check_plan(components, planned, &error)) {
			g_assert_null(strstr(error->message, "thanks.html"));
			g_clear_error(&error);
		}
	}
}

/* A caller's credit shows in the footer of every page, small and in the
 * site's own style, with its text escaped; none is drawn without one, and a
 * plan carrying another than the checked shape is refused. */
static void
test_footer_credit(void){
	g_autoptr(LsComponents) components = library();
	g_autoptr(JsonNode) plan = load("plan.json");
	g_autoptr(JsonObject) pages = fixture_pages();
	g_autoptr(GError) error = NULL;
	g_autoptr(JsonNode) bundle = NULL;
	g_autofree gchar *every = NULL;
	const gchar *credit = "<p class=\"wrap site-footer__credit\"><a href=\"https://example.com/?ref=site\" "
		"rel=\"noopener\">Built with A &amp; B</a></p>";
	JsonObject *root = json_node_get_object(plan);
	guint pages_seen = 0;
	json_object_set_member(root, "credit", json("{'text':'Built with A & B','url':'https://example.com/?ref=site'}"));
	g_assert_true(ls_components_check_plan(components, plan, &error));
	g_assert_no_error(error);
	bundle = compose_bundle(components, plan, pages);
	{
		JsonArray *files = json_object_get_array_member(json_node_get_object(bundle), "files");
		guint i;
		for (i = 0; i < json_array_get_length(files); i++) {
			JsonObject *file = json_array_get_object_element(files, i);
			if (!g_str_has_suffix(ls_string(file, "path"), ".html"))
				continue;
			contains(ls_string(file, "content"), credit);
			g_assert_true(strstr(ls_string(file, "content"), credit) > strstr(ls_string(file, "content"),
				"<footer class=\"site-footer\">"));
			pages_seen++;
		}
	}
	/* The planned pages and the built-in 404, privacy and terms pages. */
	g_assert_cmpuint(pages_seen, >=, 4);
	contains(file_content(bundle, "styles.css"), ".site-footer__credit {");
	g_clear_pointer(&bundle, json_node_unref);
	/* Only the checked {text, url} shape. */
	json_object_set_member(root, "credit", json("{'text':'Built with Example','url':'javascript:alert(1)'}"));
	g_assert_false(ls_components_check_plan(components, plan, &error));
	g_assert_nonnull(strstr(error->message, "credit"));
	g_clear_error(&error);
	/* None passed: no credit anywhere. */
	json_object_remove_member(root, "credit");
	bundle = compose_bundle(components, plan, pages);
	every = all_pages(bundle);
	lacks(every, "site-footer__credit");
}

/* Notes about inputs nobody supplied never reach visitors: a footer note or
 * page text that talks about them is sent back to be left out silently. */
static void
test_missing_talk(void){
	g_autoptr(LsComponents) components = library();
	g_autoptr(JsonNode) plan = load("plan.json");
	g_autoptr(GError) error = NULL;
	JsonObject *root = json_node_get_object(plan);
	const gchar *notes[] = {
		"Prices, opening hours and delivery details have not been supplied for this site, so none are listed.",
		"Opening hours were not provided.", "Prices to be confirmed.", "Hours: TBD", "Menu details to come."};
	const gchar *fine[] = {"All work shown is an example.", "Parking is supplied free on Quay Street.",
		"We confirm every booking by email."};
	guint i;
	for (i = 0; i < G_N_ELEMENTS(notes); i++) {
		g_autoptr(JsonNode) footer = json_node_new(JSON_NODE_OBJECT);
		JsonObject *object = json_object_new();
		json_object_set_string_member(object, "note", notes[i]);
		json_node_take_object(footer, object);
		json_object_set_member(root, "footer", g_steal_pointer(&footer));
		if (ls_components_check_plan(components, plan, &error))
			g_error("'%s' was accepted", notes[i]);
		g_assert_nonnull(strstr(error->message, "footer.note"));
		g_assert_nonnull(strstr(error->message, "not supplied"));
		g_clear_error(&error);
	}
	for (i = 0; i < G_N_ELEMENTS(fine); i++) {
		g_autoptr(JsonNode) footer = json_node_new(JSON_NODE_OBJECT);
		JsonObject *object = json_object_new();
		json_object_set_string_member(object, "note", fine[i]);
		json_node_take_object(footer, object);
		json_object_set_member(root, "footer", g_steal_pointer(&footer));
		if (!ls_components_check_plan(components, plan, &error))
			g_error("'%s' was refused: %s", fine[i], error->message);
	}
	/* Page text too. */
	{
		g_autoptr(JsonNode) page = json(
			"{'path':'journal.html','sections':[{'component':'page-header','content':{'heading':'Opening hours "
			"have not been supplied'}}]}");
		g_assert_false(ls_components_check_page(components, plan, "journal.html", page, &error));
		g_assert_nonnull(strstr(error->message, "not supplied"));
	}
}

/* The fixture snippet and stylesheet podbay drew for its contact form. */
static gchar *
podbay_text(const gchar *name){
	g_autofree gchar *path = g_strconcat("tests/fixtures/podbay/", name, NULL);
	gchar *text = NULL;
	g_assert_true(g_file_get_contents(path, &text, NULL, NULL));
	return text;
}

/* A supplied podbay module is drawn where a page places a module section:
 * its snippet as podbay drew it, posting without scripts too, its stylesheet
 * in its own layer under the site's overrides, and the posting script. Only
 * pages that use one carry any of it. */
static void
test_supplied_modules(void){
	g_autoptr(LsComponents) components = library();
	g_autoptr(JsonNode) plan = load("plan.json");
	g_autoptr(JsonObject) pages = fixture_pages();
	g_autoptr(GError) error = NULL;
	g_autoptr(JsonNode) bundle = NULL;
	g_autofree gchar *snippet = podbay_text("contact-form.html");
	g_autofree gchar *style = podbay_text("contact-form.css");
	g_autoptr(JsonNode) module_page = json(
		"{'path':'visit.html','sections':[{'component':'page-header','content':{'heading':'Visit'}},"
		"{'component':'module','variant':'card','content':{'heading':'Write to the bench','module':'contact-form'}}]}");
	JsonObject *root = json_node_get_object(plan);
	JsonArray *modules = json_array_new();
	JsonObject *entry = json_object_new();
	const gchar *visit, *css, *js, *layers;
	json_object_set_string_member(entry, "id", "contact-form");
	json_object_set_string_member(entry, "title", "Contact us");
	json_object_set_string_member(entry, "snippet", snippet);
	json_object_set_string_member(entry, "css", style);
	json_array_add_object_element(modules, entry);
	json_object_set_array_member(root, "modules", modules);
	g_assert_true(ls_components_check_plan(components, plan, &error));
	g_assert_no_error(error);
	g_assert_true(ls_components_check_page(components, plan, "visit.html", module_page, &error));
	g_assert_no_error(error);
	json_object_set_member(pages, "visit.html", json_node_copy(module_page));
	bundle = compose_bundle(components, plan, pages);
	visit = file_content(bundle, "visit.html");
	contains(visit, "class=\"block block--module module module--card");
	contains(visit, "Write to the bench");
	contains(visit, "<form action=\"/forms/contact\" method=\"post\" data-podbay-form "
		"data-failed=\"Your message was not sent. Please try again.\" class=\"podbay podbay-contact-form\" "
		"hx-post=\"/forms/contact\" hx-target=\"#contact-result\" hx-swap=\"innerHTML\">");
	contains(visit, "<div id=\"contact-result\" class=\"podbay-outcome\" data-podbay-outcome=\"thank-you\" "
		"aria-live=\"polite\"></div>");
	css = file_content(bundle, "styles.css");
	layers = strstr(css, "@layer ls-base, ls-modules, ls-components,");
	g_assert_nonnull(layers);
	contains(css, "@layer ls-modules {\n/* podbay module: contact-form */");
	contains(css, ".podbay-contact-form textarea {");
	/* The site's overrides come after, in a later layer, from its own tokens. */
	g_assert_true(strstr(css, "@layer ls-modules {") < strstr(css, "--podbay-border: var(--line-strong)"));
	contains(css, ".podbay button[type=\"submit\"]");
	js = file_content(bundle, "site.js");
	contains(js, "[data-podbay-form]");
	contains(js, "\"HX-Request\": \"true\"");
	g_clear_pointer(&bundle, json_node_unref);
	/* A site whose pages place no module ships none of it. */
	json_object_set_member(pages, "visit.html", load("visit.json"));
	bundle = compose_bundle(components, plan, pages);
	lacks(file_content(bundle, "styles.css"), "podbay-contact-form");
	lacks(file_content(bundle, "site.js"), "data-podbay-form");
	g_clear_pointer(&bundle, json_node_unref);
	/* A module section names a supplied module, once per page. */
	{
		g_autoptr(JsonNode) unknown = json(
			"{'path':'visit.html','sections':[{'component':'page-header','content':{'heading':'Visit'}},"
			"{'component':'module','content':{'module':'booking'}}]}");
		g_autoptr(JsonNode) twice = json(
			"{'path':'visit.html','sections':[{'component':'page-header','content':{'heading':'Visit'}},"
			"{'component':'module','content':{'module':'contact-form'}},"
			"{'component':'module','content':{'module':'contact-form'}}]}");
		g_assert_false(ls_components_check_page(components, plan, "visit.html", unknown, &error));
		g_assert_nonnull(strstr(error->message, "booking"));
		g_clear_error(&error);
		g_assert_false(ls_components_check_page(components, plan, "visit.html", twice, &error));
		g_assert_nonnull(strstr(error->message, "once"));
		g_clear_error(&error);
	}
	/* The plan's modules are the checked ones the builder wrote. */
	json_object_set_string_member(entry, "css", "body { color: red; }");
	g_assert_false(ls_components_check_plan(components, plan, &error));
	g_assert_nonnull(strstr(error->message, "modules"));
	g_clear_error(&error);
	json_object_remove_member(root, "modules");
	g_assert_false(ls_components_check_page(components, plan, "visit.html", module_page, &error));
	g_assert_nonnull(strstr(error->message, "module"));
	g_clear_error(&error);
}

int
main(int argc, char **argv){
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/components/catalog", test_catalog);
	g_test_add_func("/components/supplied-modules", test_supplied_modules);
	g_test_add_func("/components/schemas", test_schemas);
	g_test_add_func("/components/plan/valid", test_plan_valid);
	g_test_add_func("/components/plan/errors", test_plan_errors);
	g_test_add_func("/components/plan/repair", test_plan_repair);
	g_test_add_func("/components/page/valid", test_page_valid);
	g_test_add_func("/components/page/errors", test_page_errors);
	g_test_add_func("/components/rich", test_rich);
	g_test_add_func("/components/art", test_art);
	g_test_add_func("/components/compose", test_compose);
	g_test_add_func("/components/compose/preview", test_compose_preview);
	g_test_add_func("/components/compose/fonts", test_fonts);
	g_test_add_func("/components/compose/errors", test_compose_errors);
	g_test_add_func("/components/escaping", test_escaping);
	g_test_add_func("/components/long-heading", test_long_heading);
	g_test_add_func("/components/every-variant", test_every_variant);
	g_test_add_func("/components/look-restyle", test_look_and_restyle);
	g_test_add_func("/components/every-look", test_every_look);
	g_test_add_func("/components/themes", test_themes);
	g_test_add_func("/components/heading-emphasis", test_heading_emphasis);
	g_test_add_func("/components/section-numbers", test_section_numbers);
	g_test_add_func("/components/signature-rules", test_signature_rules);
	g_test_add_func("/components/social", test_social);
	g_test_add_func("/components/share", test_share);
	g_test_add_func("/components/directory-tabs", test_directory_and_tabs);
	g_test_add_func("/components/menu-filter", test_menu_filter);
	g_test_add_func("/components/identities", test_identities);
	g_test_add_func("/components/photos", test_photos);
	g_test_add_func("/components/photo-variants", test_photo_variants);
	g_test_add_func("/components/templates-srcset", test_templates_srcset);
	g_test_add_func("/components/scheme-sitemap-focus", test_scheme_sitemap_focus);
	g_test_add_func("/components/new-components", test_new_components);
	g_test_add_func("/components/contrast-panel", test_contrast_panel);
	g_test_add_func("/components/schema-kinds", test_schema_kinds);
	g_test_add_func("/components/layout-lint", test_layout_lint);
	g_test_add_func("/components/address-breaks", test_address_breaks);
	g_test_add_func("/components/accent-text", test_accent_text);
	g_test_add_func("/components/supplied-form", test_supplied_form);
	g_test_add_func("/components/honeypots-inert", test_honeypots_inert);
	g_test_add_func("/components/contact-actions", test_contact_actions);
	g_test_add_func("/components/booking-action", test_booking_action);
	g_test_add_func("/components/booking-calendar", test_booking_calendar);
	g_test_add_func("/components/booking-calendar-default-page", test_booking_calendar_default_page);
	g_test_add_func("/components/booking-calendar-placement", test_booking_calendar_placement);
	g_test_add_func("/components/supplied-form-default-page", test_supplied_form_default_page);
	g_test_add_func("/components/supplied-form-placement", test_supplied_form_placement);
	g_test_add_func("/components/footer-credit", test_footer_credit);
	g_test_add_func("/components/missing-talk", test_missing_talk);
	return g_test_run();
}
