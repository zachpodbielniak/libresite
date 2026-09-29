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
		{"{'path':'work.html','sections':[{'component':'manifesto','content':{'heading':'x','lines':[{'text':'a'},{'text':'b'}]}}]}", "opens with page-header"},
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
		"family=fraunces:400,400i,700,700i|ibm-plex-sans:500&amp;display=swap\">");
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
	names = json_object_get_members(all);
	for (name = names; name != NULL; name = name->next) {
		JsonObject *component = json_object_get_object_member(all, name->data);
		JsonObject *variants = json_object_get_object_member(component, "variants");
		g_autoptr(GList) variant_names = json_object_get_members(variants);
		GList *variant;
		for (variant = variant_names; variant != NULL; variant = variant->next) {
			g_autoptr(JsonObject) pages = fixture_pages();
			g_autoptr(JsonNode) page = json("{'path':'index.html','sections':[{'component':'hero','variant':'statement',"
				"'content':{'heading':'Home'}}]}");
			JsonObject *section = json_object_new();
			g_autoptr(GError) error = NULL;
			g_autofree gchar *text = NULL;
			g_autofree gchar *marker = g_strdup_printf("class=\"block block--%s %s %s--%s",
				(const gchar *)name->data, (const gchar *)name->data, (const gchar *)name->data,
				(const gchar *)variant->data);
			g_autofree gchar *selector = g_strdup_printf(".%s--%s", (const gchar *)name->data,
				(const gchar *)variant->data);
			g_autoptr(JsonNode) bundle = NULL;
			const gchar *html;
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
		"{'title':'elm'},{'title':'Ash'},{'title':'2x4 offcuts'},{'title':'Beech'},{'title':'Alder'}]}},"
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
	g_assert_cmpuint(count(html, "class=\"directory__letter\""), ==, 4);
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
	contains(file_content(bundle, "index.html"), "look-header-centered look-nav-underline look-heads-numbered");
	contains(file_content(bundle, "work.html"), "look-panels-outlined look-rhythm-bands");
	contains(file_content(bundle, "404.html"), "look-footer-big");
	css = file_content(bundle, "styles.css");
	layers = strstr(css, "@layer ls-base, ls-components, ls-look;");
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
		g_autoptr(GList) names = json_object_get_members(values);
		GList *name;
		g_assert_cmpuint(json_array_get_length(listed), ==, g_list_length(names));
		for (name = names; name != NULL; name = name->next) {
			g_autofree gchar *selector = g_strdup_printf(".look-%s-%s", (const gchar *)axis->data,
				(const gchar *)name->data);
			if (name != names && strstr(css, selector) == NULL)
				g_string_append_printf(unstyled, " %s", selector);
		}
	}
	if (unstyled->len > 0)
		g_error("look values without styles:%s", unstyled->str);
}


/* Look suggestions are stable per site, valid, and vary between sites. */
static void
test_suggest_look(void){
	g_autoptr(LsComponents) components = library();
	g_autoptr(JsonObject) first = ls_components_suggest_look(components, "harbor\nA furniture workshop");
	g_autoptr(JsonObject) again = ls_components_suggest_look(components, "harbor\nA furniture workshop");
	g_autoptr(GHashTable) seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	g_autoptr(JsonNode) plan = load("plan.json");
	g_autoptr(GList) axes = json_object_get_members(first);
	GList *axis;
	guint i;
	g_assert_cmpuint(g_list_length(axes), ==, 21);
	for (axis = axes; axis != NULL; axis = axis->next)
		g_assert_cmpstr(ls_string(first, axis->data), ==, ls_string(again, axis->data));
	json_object_set_object_member(json_node_get_object(plan), "look", json_object_ref(first));
	g_assert_true(ls_components_check_plan(components, plan, NULL));
	for (i = 0; i < 12; i++) {
		g_autofree gchar *seed = g_strdup_printf("site-%u", i);
		g_autoptr(JsonObject) look = ls_components_suggest_look(components, seed);
		g_autoptr(JsonNode) node = json_node_new(JSON_NODE_OBJECT);
		json_node_set_object(node, look);
		g_hash_table_add(seen, ls_json_encode(node));
	}
	g_assert_cmpuint(g_hash_table_size(seen), >=, 11);
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

/* Home openings rotate between sites, stay stable per site and are real. */
static void
test_suggest_opening(void){
	g_autoptr(LsComponents) components = library();
	g_autoptr(GHashTable) seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	g_autofree gchar *first = ls_components_suggest_opening(components, "harbor");
	g_autofree gchar *again = ls_components_suggest_opening(components, "harbor");
	guint i;
	g_assert_cmpstr(first, ==, again);
	for (i = 0; i < 40; i++) {
		g_autofree gchar *seed = g_strdup_printf("site-%u", i);
		gchar *opening = ls_components_suggest_opening(components, seed);
		g_autofree gchar *component = g_strndup(opening, strcspn(opening, "/"));
		g_assert_true(g_strv_contains((const gchar *[]){"hero", "cover", "poster", "manifesto", NULL}, component));
		g_hash_table_add(seen, opening);
	}
	g_assert_cmpuint(g_hash_table_size(seen), >=, 8);
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

/* Axes with plain: N deal their default to about N percent of sites, so
 * textures and backdrops stay occasional; other axes spread evenly. */
static void
test_plain_share(void){
	g_autoptr(LsComponents) components = library();
	guint plain_texture = 0, plain_backdrop = 0, i;
	for (i = 0; i < 400; i++) {
		g_autofree gchar *seed = g_strdup_printf("site %u", i);
		g_autoptr(JsonObject) look = ls_components_suggest_look(components, seed);
		plain_texture += g_str_equal(ls_string(look, "texture"), "none");
		plain_backdrop += g_str_equal(ls_string(look, "backdrop"), "none");
	}
	/* 50 percent plain plus an even share of the rest: about 56 percent. */
	g_assert_cmpuint(plain_texture, >=, 180);
	g_assert_cmpuint(plain_texture, <=, 270);
	g_assert_cmpuint(plain_backdrop, >=, 180);
	g_assert_cmpuint(plain_backdrop, <=, 270);
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
	contains(html, "<dialog class=\"gallery__dialog\"");
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

/* Identities fit their sector, stay stable for a site and vary between
 * sites; the house-default check rejects only an undealt default. */
static void
test_identities(void){
	g_autoptr(LsComponents) components = library();
	g_auto(GStrv) sectors = ls_components_list_sectors(components);
	g_autoptr(GHashTable) palettes = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	g_autoptr(JsonNode) plan = load("plan.json");
	g_autoptr(GError) error = NULL;
	JsonObject *tokens = json_object_get_object_member(json_node_get_object(plan), "tokens");
	guint i, s;
	g_assert_true(g_strv_contains((const gchar *const *)sectors, "trades"));
	g_assert_true(g_strv_contains((const gchar *const *)sectors, "software"));
	for (s = 0; sectors[s] != NULL; s++)
		for (i = 0; i < 40; i++) {
			g_autofree gchar *seed = g_strdup_printf("site %u", i);
			g_autoptr(JsonObject) a = ls_components_suggest_identity(components, seed, sectors[s]);
			g_autoptr(JsonObject) b = ls_components_suggest_identity(components, seed, sectors[s]);
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
			/* Olive market fits food, community and wellness, never software. */
			if (g_str_equal(sectors[s], "software"))
				g_assert_cmpstr(ls_string(json_object_get_object_member(a, "palette"), "name"), !=, "olive-market");
			if (g_str_equal(sectors[s], "trades"))
				g_hash_table_add(palettes, g_strdup(ls_string(json_object_get_object_member(a, "palette"), "name")));
		}
	/* Forty trades sites spread over several palettes. */
	g_assert_cmpuint(g_hash_table_size(palettes), >=, 3);
	/* An unknown or absent sector deals from every entry. */
	{
		g_autoptr(JsonObject) any = ls_components_suggest_identity(components, "x", NULL);
		g_autoptr(JsonObject) unknown = ls_components_suggest_identity(components, "x", "nope");
		g_assert_true(json_object_equal(any, unknown));
	}
	/* The fixture plan is its own design. */
	g_assert_true(ls_components_check_identity(components, plan, NULL, &error));
	g_assert_no_error(error);
	/* Cream paper with a rust accent is the house default. */
	json_object_set_string_member(tokens, "paper", "#faf3e7");
	json_object_set_string_member(tokens, "accent", "#b4471f");
	g_assert_false(ls_components_check_identity(components, plan, NULL, &error));
	g_assert_error(error, LS_ERROR, LS_ERROR_VALIDATION);
	g_assert_nonnull(strstr(error->message, "suggested_palette"));
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
	g_assert_nonnull(strstr(error->message, "suggested_type"));
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

int
main(int argc, char **argv){
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/components/catalog", test_catalog);
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
	g_test_add_func("/components/suggest-look", test_suggest_look);
	g_test_add_func("/components/themes", test_themes);
	g_test_add_func("/components/heading-emphasis", test_heading_emphasis);
	g_test_add_func("/components/suggest-opening", test_suggest_opening);
	g_test_add_func("/components/directory-tabs", test_directory_and_tabs);
	g_test_add_func("/components/menu-filter", test_menu_filter);
	g_test_add_func("/components/identities", test_identities);
	g_test_add_func("/components/photos", test_photos);
	g_test_add_func("/components/scheme-sitemap-focus", test_scheme_sitemap_focus);
	g_test_add_func("/components/new-components", test_new_components);
	g_test_add_func("/components/plain-share", test_plain_share);
	g_test_add_func("/components/contrast-panel", test_contrast_panel);
	g_test_add_func("/components/schema-kinds", test_schema_kinds);
	return g_test_run();
}
