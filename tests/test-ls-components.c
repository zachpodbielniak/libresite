/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "core/ls-components.h"
#include "core/ls-util.h"
#include <string.h>

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
	g_assert_cmpuint(g_list_length(axes), ==, 15);
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
	g_test_add_func("/components/schema-kinds", test_schema_kinds);
	return g_test_run();
}
