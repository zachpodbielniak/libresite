/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* Content and real multi-page sites (docs/components.org, "Pages and
 * sections from the order"): a page for every service, About and Service
 * area pages only with their facts, a header and footer that link every
 * page, and home sections left out when the order has no content for
 * them. */
#include "libresite.h"
#include "core/ls-components.h"
#include "core/ls-design.h"
#include "core/ls-util.h"
#include <string.h>

static LsComponents *
library(void){
	g_autoptr(GError) error = NULL;
	LsComponents *components = ls_components_new(&error);
	g_assert_no_error(error);
	g_assert_nonnull(components);
	return components;
}

static JsonObject *
block(LsComponents *components){
	return json_object_get_object_member(json_node_get_object(ls_components_get_catalog(components)), "library");
}

static JsonNode *
parse(const gchar *text){
	g_autoptr(GError) error = NULL;
	JsonNode *node = ls_json_parse(text, &error);
	g_assert_no_error(error);
	return node;
}

/* The plumber form as the platform flattens it: hours, area, colours and
 * the owner's process in the requirements, the year in the tagline. */
static const gchar rich[] =
	"{\"brand\": {\"name\": \"Hartwell Plumbing\", \"tagline\": \"Family plumbing and drain service in Fairport"
	" and the east side of Rochester since 1998.\", \"colors\": \"navy and white\"},"
	" \"contact\": {\"phone\": \"585-555-0141\", \"email\": \"office@hartwell.example\","
	" \"address\": \"214 Main Street, Fairport, NY 14450\"},"
	" \"booking\": {\"href\": \"/book\"},"
	" \"offers\": [{\"id\": \"a\", \"name\": \"Emergency repairs\", \"description\": \"Burst pipes and leaks.\"},"
	" {\"id\": \"b\", \"name\": \"Drain cleaning\"}, {\"id\": \"c\", \"name\": \"Water heaters\"},"
	" {\"id\": \"d\", \"name\": \"Sump pumps\"}, {\"id\": \"e\", \"name\": \"Bathroom and kitchen plumbing\"}],"
	" \"requirements\": \"Use one shared contact form. Show these supplied business details on the contact page:"
	" Hours: Mon-Fri 7am-6pm, Sat 8am-noon. Emergency calls answered 24/7.. Service area: Fairport, Penfield,"
	" Perinton, Pittsford, Webster and East Rochester. Colours: navy and white. Style: clean and modern,"
	" trustworthy. Apply these visual preferences. The owner's own process, in order: 1. Call or send the form"
	" and tell us what is happening. 2. We give you an arrival window and a price before any work starts."
	" 3. A licensed plumber fixes it and cleans up after.\"}";

/* A thin order: names, a phone and an email, nothing else. */
static const gchar thin[] =
	"{\"brand\": {\"name\": \"Polar Air Heating & Cooling\", \"tagline\": \"Heating and cooling\"},"
	" \"contact\": {\"phone\": \"614-555-0136\", \"email\": \"info@polarair.example\"},"
	" \"booking\": {\"href\": \"/book\"},"
	" \"offers\": [{\"id\": \"a\", \"name\": \"Furnace repair\"}, {\"id\": \"b\", \"name\": \"AC repair\"},"
	" {\"id\": \"c\", \"name\": \"Installation\"}, {\"id\": \"d\", \"name\": \"Maintenance\"}],"
	" \"requirements\": \"Use one shared contact form for enquiries and quote requests.\"}";

static const gchar stock[] =
	"[{\"path\": \"assets/a.jpg\", \"alt\": \"A plumber under a sink.\", \"stock\": true, \"credit\": \"A, CC0\","
	" \"width\": 1920, \"height\": 1440},"
	" {\"path\": \"assets/b.jpg\", \"alt\": \"A water heater.\", \"stock\": true, \"width\": 1920, \"height\": 1080},"
	" {\"path\": \"assets/c.jpg\", \"alt\": \"A faucet.\", \"stock\": true, \"width\": 1280, \"height\": 1707},"
	" {\"path\": \"assets/d.jpg\", \"alt\": \"A shower.\", \"stock\": true, \"width\": 1280, \"height\": 1707}]";

static const gchar own[] =
	"[{\"path\": \"assets/a.jpg\", \"alt\": \"Our van outside a house.\", \"width\": 1920, \"height\": 1440},"
	" {\"path\": \"assets/b.jpg\", \"alt\": \"A new water heater we fitted.\", \"width\": 1920, \"height\": 1080},"
	" {\"path\": \"assets/c.jpg\", \"alt\": \"A finished bathroom.\", \"width\": 1920, \"height\": 1080},"
	" {\"path\": \"assets/d.jpg\", \"alt\": \"A cleared drain.\", \"width\": 1920, \"height\": 1080}]";

static JsonObject *
design_for(LsComponents *components, const gchar *site_text, const gchar *preset, const gchar *photos_text){
	g_autoptr(JsonNode) site = parse(site_text);
	g_autoptr(JsonNode) photos = photos_text != NULL ? parse(photos_text) : NULL;
	g_autoptr(GError) error = NULL;
	JsonObject *design = ls_design_new(block(components), ls_components_get_themes(components),
		json_node_get_object(site), preset, photos != NULL ? json_node_get_array(photos) : NULL, &error);
	g_assert_no_error(error);
	g_assert_nonnull(design);
	return design;
}

static gboolean
home_has(JsonObject *design, const gchar *component){
	JsonArray *home = json_object_get_array_member(design, "home");
	guint i;
	for (i = 0; i < json_array_get_length(home); i++)
		if (g_strcmp0(ls_string(json_array_get_object_element(home, i), "component"), component) == 0)
			return TRUE;
	return FALSE;
}

static JsonObject *
page_of(JsonObject *design, const gchar *path){
	JsonArray *pages = json_object_get_array_member(design, "pages");
	guint i;
	for (i = 0; i < json_array_get_length(pages); i++)
		if (g_strcmp0(ls_string(json_array_get_object_element(pages, i), "path"), path) == 0)
			return json_array_get_object_element(pages, i);
	return NULL;
}

/* Every supplied service gets a page of its own, on a rich order and on a
 * thin one; About and Service area pages come only with their facts. */
static void
test_service_pages(void){
	g_autoptr(LsComponents) components = library();
	g_autoptr(JsonObject) full = design_for(components, rich, "evergreen", stock);
	g_autoptr(JsonObject) names = design_for(components, thin, "lindholm", NULL);
	const gchar *rich_routes[] = {"index.html", "services.html", "emergency-repairs.html", "drain-cleaning.html",
		"water-heaters.html", "sump-pumps.html", "bathroom-and-kitchen-plumbing.html", "about.html",
		"service-area.html", "questions.html", "book.html", "contact.html"};
	const gchar *thin_routes[] = {"index.html", "services.html", "furnace-repair.html", "ac-repair.html",
		"installation.html", "maintenance.html", "questions.html", "book.html", "contact.html"};
	guint i;
	g_assert_cmpuint(json_array_get_length(json_object_get_array_member(full, "pages")), ==, G_N_ELEMENTS(rich_routes));
	for (i = 0; i < G_N_ELEMENTS(rich_routes); i++)
		g_assert_nonnull(page_of(full, rich_routes[i]));
	g_assert_cmpstr(ls_string(page_of(full, "water-heaters.html"), "kind"), ==, "detail");
	g_assert_cmpstr(ls_string(page_of(full, "water-heaters.html"), "offer"), ==, "c");
	g_assert_cmpstr(ls_string(page_of(full, "water-heaters.html"), "title"), ==, "Water heaters");
	g_assert_cmpstr(ls_string(page_of(full, "service-area.html"), "kind"), ==, "visit");
	g_assert_cmpstr(ls_string(page_of(full, "about.html"), "kind"), ==, "about");
	g_assert_cmpuint(json_array_get_length(json_object_get_array_member(names, "pages")), ==, G_N_ELEMENTS(thin_routes));
	for (i = 0; i < G_N_ELEMENTS(thin_routes); i++)
		g_assert_nonnull(page_of(names, thin_routes[i]));
	g_assert_null(page_of(names, "about.html"));
	g_assert_null(page_of(names, "service-area.html"));
	/* A Questions page answers from general truths; a Booking page holds
	 * the supplied booking calendar, so it comes only with a booking page. */
	g_assert_cmpstr(ls_string(page_of(full, "questions.html"), "kind"), ==, "general");
	g_assert_cmpstr(ls_string(page_of(full, "book.html"), "kind"), ==, "form");
	{
		g_autoptr(JsonObject) unbooked = design_for(components, "{\"brand\": {\"name\": \"Quiet Co\"},"
			" \"offers\": [{\"id\": \"a\", \"name\": \"One\"}]}", "evergreen", NULL);
		g_assert_nonnull(page_of(unbooked, "questions.html"));
		g_assert_null(page_of(unbooked, "book.html"));
	}
}

/* The supplied facts the builder reads from the order, word for word. */
static void
test_facts(void){
	g_autoptr(JsonNode) site = parse(rich);
	g_autoptr(JsonObject) facts = ls_design_facts(json_node_get_object(site));
	g_autoptr(JsonNode) bare = parse(thin);
	g_autoptr(JsonObject) none = ls_design_facts(json_node_get_object(bare));
	JsonArray *area = json_object_get_array_member(facts, "area");
	JsonArray *process = json_object_get_array_member(facts, "process");
	g_assert_cmpstr(ls_string(facts, "since"), ==, "1998");
	g_assert_cmpstr(ls_string(facts, "hours"), ==, "Mon-Fri 7am-6pm, Sat 8am-noon. Emergency calls answered 24/7.");
	g_assert_cmpstr(ls_string(facts, "address"), ==, "214 Main Street, Fairport, NY 14450");
	g_assert_cmpuint(json_array_get_length(area), ==, 6);
	g_assert_cmpstr(json_array_get_string_element(area, 0), ==, "Fairport");
	g_assert_cmpstr(json_array_get_string_element(area, 5), ==, "East Rochester");
	g_assert_cmpuint(json_array_get_length(process), ==, 3);
	g_assert_cmpstr(json_array_get_string_element(process, 2), ==, "A licensed plumber fixes it and cleans up after.");
	g_assert_null(ls_string(none, "since"));
	g_assert_null(ls_string(none, "hours"));
	g_assert_false(json_object_has_member(none, "area"));
	g_assert_false(json_object_has_member(none, "process"));
}

/* A home section enters only with its content: the thin order has no
 * trust strip, process, about, area, gallery or reviews, but keeps its
 * services, the general FAQ and the closing call band; the rich order
 * gains the strip (or an opening that carries it), the process, the about
 * and the area; a gallery needs
 * the owner's own photos, never stock ones. */
static void
test_sections_gated(void){
	g_autoptr(LsComponents) components = library();
	g_autoptr(JsonObject) full = design_for(components, rich, "evergreen", stock);
	g_autoptr(JsonObject) names = design_for(components, thin, "evergreen", stock);
	g_autoptr(JsonObject) owned = design_for(components, rich, "evergreen", own);
	const gchar *absent[] = {"facts", "steps", "about", "areas", "gallery", "reviews"};
	const gchar *present[] = {"facts", "services", "steps", "about", "areas", "faq", "cta"};
	guint i;
	for (i = 0; i < G_N_ELEMENTS(absent); i++)
		g_assert_false(home_has(names, absent[i]));
	g_assert_true(home_has(names, "services"));
	g_assert_true(home_has(names, "faq"));
	g_assert_true(home_has(names, "cta"));
	/* The trust facts show as a strip, or in the opening's own stats bar. */
	for (i = 0; i < G_N_ELEMENTS(present); i++)
		if (!home_has(full, present[i]) && !(g_str_equal(present[i], "facts") && g_strcmp0(ls_string(
			json_array_get_object_element(json_object_get_array_member(full, "home"), 0), "variant"), "stats-bar") == 0))
			g_error("the rich home has no %s", present[i]);
	g_assert_false(home_has(full, "gallery"));
	g_assert_false(home_has(full, "reviews"));
	g_assert_true(home_has(owned, "gallery"));
	/* Every pick still keeps the clash rules. */
	{
		JsonObject *designs[3];
		designs[0] = full;
		designs[1] = names;
		designs[2] = owned;
		for (i = 0; i < 3; i++) {
			g_autoptr(GString) why = g_string_new(NULL);
			if (!ls_design_check_home(block(components), json_object_get_array_member(designs[i], "home"),
				ls_string(designs[i], "footer"), why))
				g_error("%s", why->str);
		}
	}
}

/* A minimal writer answer for an interior page. */
static JsonNode *
interior(const gchar *path, const gchar *title){
	g_autofree gchar *text = g_strdup_printf("{\"path\": \"%s\", \"sections\": ["
		"{\"component\": \"page-header\", \"variant\": \"plain\", \"content\": {\"heading\": \"%s\"}},"
		"{\"component\": \"text\", \"variant\": \"narrow\", \"content\": {\"heading\": \"What it involves\","
		" \"body\": \"Plain words about the work.\"}},"
		"{\"component\": \"faq\", \"variant\": \"list\", \"content\": {\"items\": ["
		"{\"question\": \"One?\", \"answer\": \"Yes.\"}, {\"question\": \"Two?\", \"answer\": \"Yes.\"},"
		" {\"question\": \"Three?\", \"answer\": \"Yes.\"}]}},"
		"{\"component\": \"cta\", \"variant\": \"band\", \"content\": {\"heading\": \"Ready?\"}}]}", path, title);
	return parse(text);
}

/* The home as a writer answers it, with every designed section type. */
static const gchar home_page[] =
	"{\"path\": \"index.html\", \"sections\": ["
	"{\"component\": \"hero\", \"variant\": \"photo-start\", \"content\": {\"kicker\": \"Fairport since 1998\","
	" \"heading\": \"Plumbing and drains for Fairport homes\", \"lede\": \"Family plumbers on call.\"}},"
	"{\"component\": \"facts\", \"variant\": \"grid\", \"content\": {\"items\": [{\"label\": \"Made up\","
	" \"value\": \"Invented\"}]}},"
	"{\"component\": \"services\", \"variant\": \"rows\", \"content\": {\"heading\": \"What we fix\","
	" \"items\": [{\"name\": \"Emergency repairs\", \"summary\": \"Burst pipes and leaks.\"},"
	" {\"name\": \"Drain cleaning\", \"summary\": \"Slow drains cleared. Kitchens clog most. Grease is the usual cause.\"}]}},"
	"{\"component\": \"steps\", \"variant\": \"row\", \"content\": {\"heading\": \"How a visit works\","
	" \"items\": [{\"title\": \"Invented one\"}, {\"title\": \"Invented two\"}]}},"
	"{\"component\": \"about\", \"variant\": \"history\", \"content\": {\"heading\": \"A Fairport family business\","
	" \"body\": \"Plumbing on the east side since 1998.\"}},"
	"{\"component\": \"areas\", \"variant\": \"columns\", \"content\": {\"heading\": \"Where we work\"}},"
	"{\"component\": \"faq\", \"variant\": \"list\", \"content\": {\"heading\": \"Questions\","
	" \"items\": [{\"question\": \"Do you answer at night?\", \"answer\": \"Emergency calls are answered 24/7.\"},"
	" {\"question\": \"Question 2?\", \"answer\": \"A general answer.\"}, {\"question\": \"Question 3?\", \"answer\": \"A general answer.\"}, {\"question\": \"Question 4?\", \"answer\": \"A general answer.\"}, {\"question\": \"Question 5?\", \"answer\": \"A general answer.\"}, {\"question\": \"Question 6?\", \"answer\": \"A general answer.\"}, {\"question\": \"Question 7?\", \"answer\": \"A general answer.\"}, {\"question\": \"Question 8?\", \"answer\": \"A general answer.\"}, {\"question\": \"Question 9?\", \"answer\": \"A general answer.\"}, {\"question\": \"Question 10?\", \"answer\": \"A general answer.\"}]}},"
	"{\"component\": \"cta\", \"variant\": \"band\", \"content\": {\"heading\": \"Water where it should not be?\","
	" \"actions\": [{\"href\": \"index.html\", \"label\": \"Somewhere\"}]}}]}";

/* A composed rich site: every service has its page, the header lists
 * every page with the services under one Services menu, the footer lists
 * every page, and the builder's facts replace the writer's in the trust
 * strip, the process and the area. */
static void
test_navigation(void){
	g_autoptr(LsComponents) components = library();
	g_autoptr(GError) error = NULL;
	g_autofree gchar *plan_text = NULL, *bundle = NULL;
	g_autoptr(JsonNode) plan = NULL, composed = NULL, site = parse(rich), photos = parse(stock);
	g_autoptr(JsonObject) pages = json_object_new();
	g_autoptr(JsonObject) design = NULL;
	JsonArray *files, *planned;
	guint i, j;
	g_assert_true(g_file_get_contents("tests/fixtures/composed/plan.json", &plan_text, NULL, NULL));
	plan = parse(plan_text);
	json_object_set_member(json_node_get_object(plan), "photos", json_node_copy(photos));
	json_object_set_member(json_node_get_object(plan), "contact",
		parse("{\"phone\": \"585-555-0141\", \"email\": \"office@hartwell.example\"}"));
	design = design_for(components, rich, "evergreen", stock);
	g_assert_true(ls_components_apply_theme(components, plan, "evergreen", &error));
	g_assert_no_error(error);
	g_assert_true(ls_components_apply_design(components, plan, design));
	ls_components_apply_default(components, plan, TRUE);
	ls_components_check_plan(components, plan, &error);
	g_assert_no_error(error);
	/* The plan's pages are the design's, in its order; the writer's own
	 * extra pages are gone. */
	planned = json_object_get_array_member(json_node_get_object(plan), "pages");
	g_assert_cmpuint(json_array_get_length(planned), ==, 12);
	for (i = 0; i < json_array_get_length(planned); i++) {
		JsonObject *entry = json_array_get_object_element(planned, i);
		const gchar *path = ls_string(entry, "path");
		g_assert_cmpuint(strlen(ls_string(entry, "description")), >=, 50);
		g_assert_cmpuint(strlen(ls_string(entry, "description")), <=, 160);
		g_assert_cmpstr(path, !=, "work.html");
		if (g_str_equal(path, "index.html")) {
			g_autoptr(JsonNode) home = parse(home_page);
			ls_components_apply_opening(components, plan, path, home);
			ls_components_design_home(components, plan, home);
			ls_components_spread_photos(components, plan, home);
			ls_components_check_page(components, plan, path, home, &error);
			g_assert_no_error(error);
			json_object_set_member(pages, path, g_steal_pointer(&home));
		} else {
			JsonNode *page = interior(path, ls_string(entry, "title"));
			ls_components_check_page(components, plan, path, page, &error);
			g_assert_no_error(error);
			json_object_set_member(pages, path, page);
		}
	}
	bundle = ls_components_compose(components, plan, pages, NULL, &error);
	g_assert_no_error(error);
	composed = parse(bundle);
	files = json_object_get_array_member(json_node_get_object(composed), "files");
	for (i = 0; i < json_array_get_length(files); i++) {
		JsonObject *file = json_array_get_object_element(files, i);
		const gchar *path = ls_string(file, "path"), *html = ls_string(file, "content");
		const gchar *header_end, *footer;
		if (!g_str_has_suffix(path, ".html") || g_str_equal(path, "404.html"))
			continue;
		header_end = strstr(html, "</header>");
		footer = strstr(html, "<footer");
		g_assert_nonnull(header_end);
		g_assert_nonnull(footer);
		/* The services sit under one Services menu in the header. */
		g_assert_nonnull(strstr(html, "site-nav__menu"));
		for (j = 0; j < json_array_get_length(planned); j++) {
			const gchar *route = ls_string(json_array_get_object_element(planned, j), "path");
			g_autofree gchar *link = g_strdup_printf("href=\"%s\"", route);
			const gchar *in_header = strstr(html, link);
			g_assert_nonnull(in_header);
			if (!g_str_equal(route, "index.html"))
				g_assert_true(in_header < header_end);
			if (strstr(footer, link) == NULL)
				g_error("%s: the footer does not link %s", path, route);
		}
		/* Every closing call wears the design's band and its tone, whatever
		 * older variant the writer named, so the builder's buttons keep AA. */
		g_assert_null(strstr(html, "cta--band"));
		if (!g_str_equal(path, "index.html") && !g_str_equal(path, "privacy.html") && !g_str_equal(path, "terms.html"))
			g_assert_nonnull(strstr(html, "cta--closing-band tone-brand"));
		if (g_str_equal(path, "index.html")) {
			g_assert_null(strstr(html, "Invented"));
			g_assert_nonnull(strstr(html, "1998"));
			g_assert_nonnull(strstr(html, "East Rochester"));
			g_assert_nonnull(strstr(html, "We give you an arrival window and a price before any work starts."));
			g_assert_nonnull(strstr(html, "cta--"));
			g_assert_null(strstr(html, ">Somewhere<"));
			g_assert_nonnull(strstr(html, "mailto:office@hartwell.example"));
		}
	}
}

/* A designed home needs depth the writer can give honestly: six or more
 * questions and a line for every service. A thinner answer is refused
 * with the reason, so the writer tries again. */
static void
test_home_depth(void){
	g_autoptr(LsComponents) components = library();
	g_autoptr(GError) error = NULL;
	g_autofree gchar *plan_text = NULL;
	g_autoptr(JsonNode) plan = NULL, thin_home = NULL, full_home = NULL;
	g_autoptr(JsonObject) design = design_for(components, thin, "evergreen", NULL);
	GString *faq = g_string_new(NULL);
	guint i;
	g_assert_true(g_file_get_contents("tests/fixtures/composed/plan.json", &plan_text, NULL, NULL));
	plan = parse(plan_text);
	g_assert_true(ls_components_apply_theme(components, plan, "evergreen", &error));
	g_assert_true(ls_components_apply_design(components, plan, design));
	ls_components_apply_default(components, plan, TRUE);
	thin_home = parse("{\"path\": \"index.html\", \"sections\": ["
		"{\"component\": \"hero\", \"variant\": \"centered\", \"content\": {\"heading\": \"Heating and cooling\"}},"
		"{\"component\": \"services\", \"variant\": \"rows\", \"content\": {\"heading\": \"Services\","
		" \"items\": [{\"name\": \"Furnace repair\"}, {\"name\": \"AC repair\"}]}},"
		"{\"component\": \"faq\", \"variant\": \"list\", \"content\": {\"heading\": \"Questions\","
		" \"items\": [{\"question\": \"Why does a furnace short cycle?\", \"answer\": \"Often a dirty filter.\"}]}},"
		"{\"component\": \"cta\", \"variant\": \"band\", \"content\": {\"heading\": \"Cold house?\"}}]}");
	ls_components_apply_opening(components, plan, "index.html", thin_home);
	ls_components_design_home(components, plan, thin_home);
	g_assert_false(ls_components_check_page(components, plan, "index.html", thin_home, &error));
	/* A home of names only, with no facts to show, asks twelve. */
	g_assert_nonnull(strstr(error->message, "12 or more"));
	g_assert_nonnull(strstr(error->message, "Furnace repair"));
	g_clear_error(&error);
	for (i = 0; i < 12; i++)
		g_string_append_printf(faq, "%s{\"question\": \"Question %u?\", \"answer\": \"A general answer.\"}",
			i > 0 ? ", " : "", i + 1);
	{
		g_autofree gchar *text = g_strdup_printf("{\"path\": \"index.html\", \"sections\": ["
			"{\"component\": \"hero\", \"variant\": \"centered\", \"content\": {\"heading\": \"Heating and cooling\"}},"
			"{\"component\": \"services\", \"variant\": \"rows\", \"content\": {\"heading\": \"Services\","
			" \"items\": [{\"name\": \"Furnace repair\", \"summary\": \"Fixing a furnace that will not heat.\"},"
			" {\"name\": \"AC repair\", \"summary\": \"Fixing cooling that stops.\"}]}},"
			"{\"component\": \"faq\", \"variant\": \"list\", \"content\": {\"heading\": \"Questions\","
			" \"items\": [%s]}},"
			"{\"component\": \"cta\", \"variant\": \"band\", \"content\": {\"heading\": \"Cold house?\"}}]}", faq->str);
		full_home = parse(text);
	}
	g_string_free(faq, TRUE);
	ls_components_apply_opening(components, plan, "index.html", full_home);
	ls_components_design_home(components, plan, full_home);
	/* A service with no supplied description gets three or more general
	 * sentences, not a single line. */
	g_assert_false(ls_components_check_page(components, plan, "index.html", full_home, &error));
	g_assert_nonnull(strstr(error->message, "three general"));
	g_clear_error(&error);
	{
		JsonArray *sections = json_object_get_array_member(json_node_get_object(full_home), "sections");
		JsonArray *items = json_object_get_array_member(json_object_get_object_member(
			json_array_get_object_element(sections, 1), "content"), "items");
		json_object_set_string_member(json_array_get_object_element(items, 0), "summary",
			"Fixing a furnace that will not heat. It often starts with the filter and the igniter. Most calls come in the first cold week.");
		json_object_set_string_member(json_array_get_object_element(items, 1), "summary",
			"Fixing cooling that stops. Most calls come on the first hot days! A warm house is the usual sign.");
	}
	ls_components_check_page(components, plan, "index.html", full_home, &error);
	g_assert_no_error(error);
	/* Ten questions are too few on a home of names only. */
	{
		JsonArray *sections = json_object_get_array_member(json_node_get_object(full_home), "sections");
		JsonArray *questions = json_object_get_array_member(json_object_get_object_member(
			json_array_get_object_element(sections, 2), "content"), "items");
		json_array_remove_element(questions, 11);
		json_array_remove_element(questions, 10);
		g_assert_false(ls_components_check_page(components, plan, "index.html", full_home, &error));
		g_assert_nonnull(strstr(error->message, "12 or more"));
		g_clear_error(&error);
	}
	/* The writer is told the same minimum the check holds it to. */
	g_assert_cmpuint(ls_components_home_questions(plan), ==, 12);
	{
		g_autoptr(JsonNode) rich_plan = parse(plan_text);
		g_autoptr(JsonObject) rich_design = design_for(components, rich, "evergreen", NULL);
		g_assert_true(ls_components_apply_theme(components, rich_plan, "evergreen", &error));
		g_assert_true(ls_components_apply_design(components, rich_plan, rich_design));
		g_assert_cmpuint(ls_components_home_questions(rich_plan), ==, 10);
	}
	/* A supplied description is enough on its own. */
	{
		g_autoptr(JsonObject) described = design_for(components, rich, "evergreen", NULL);
		JsonArray *pages = json_object_get_array_member(described, "pages");
		g_assert_true(json_object_get_boolean_member(json_array_get_object_element(pages, 2), "described"));
		g_assert_false(json_object_has_member(json_array_get_object_element(pages, 3), "described"));
	}
}

/* A service page explains the service: a page made of its header and a
 * photo alone is refused; one with the text and three questions passes. */
static void
test_service_depth(void){
	g_autoptr(LsComponents) components = library();
	g_autoptr(GError) error = NULL;
	g_autofree gchar *plan_text = NULL;
	g_autoptr(JsonNode) plan = NULL, bare = NULL, full = NULL;
	g_autoptr(JsonObject) design = design_for(components, thin, "evergreen", NULL);
	g_assert_true(g_file_get_contents("tests/fixtures/composed/plan.json", &plan_text, NULL, NULL));
	plan = parse(plan_text);
	g_assert_true(ls_components_apply_theme(components, plan, "evergreen", &error));
	g_assert_true(ls_components_apply_design(components, plan, design));
	ls_components_apply_default(components, plan, TRUE);
	bare = parse("{\"path\": \"ac-repair.html\", \"sections\": [{\"component\": \"page-header\", \"variant\": \"plain\","
		" \"content\": {\"heading\": \"AC repair\"}}]}");
	g_assert_false(ls_components_check_page(components, plan, "ac-repair.html", bare, &error));
	g_assert_nonnull(strstr(error->message, "a text section on what AC repair involves"));
	g_assert_nonnull(strstr(error->message, "three"));
	g_clear_error(&error);
	full = parse("{\"path\": \"ac-repair.html\", \"sections\": [{\"component\": \"page-header\", \"variant\": \"plain\","
		" \"content\": {\"heading\": \"AC repair\"}},"
		" {\"component\": \"text\", \"variant\": \"narrow\", \"content\": {\"heading\": \"What AC repair involves\","
		" \"body\": \"Most repairs start with a check of the filter, the thermostat and the outdoor unit.\"}},"
		" {\"component\": \"faq\", \"variant\": \"list\", \"content\": {\"items\": ["
		"{\"question\": \"One?\", \"answer\": \"Yes.\"}, {\"question\": \"Two?\", \"answer\": \"Yes.\"},"
		" {\"question\": \"Three?\", \"answer\": \"Yes.\"}]}}]}");
	ls_components_check_page(components, plan, "ac-repair.html", full, &error);
	g_assert_no_error(error);
	/* A heading that promises a process lists its steps, as the site gate
	 * asks of a published page. */
	{
		g_autoptr(JsonNode) about = parse("{\"path\": \"contact.html\", \"sections\": [{\"component\":"
			" \"page-header\", \"variant\": \"plain\", \"content\": {\"heading\": \"Contact\"}},"
			" {\"component\": \"text\", \"variant\": \"narrow\", \"content\": {\"heading\": \"Where we are and how we work\","
			" \"body\": \"We work from one shop.\"}}]}");
		g_assert_false(ls_components_check_page(components, plan, "contact.html", about, &error));
		g_assert_nonnull(strstr(error->message, "steps"));
		g_clear_error(&error);
	}
	/* Questions are not steps: an FAQ headed as a process is sent back,
	 * as the site gate counts only listed steps (slice 4's law firm). */
	{
		g_autoptr(JsonNode) asked = parse("{\"path\": \"questions.html\", \"sections\": [{\"component\":"
			" \"page-header\", \"variant\": \"plain\", \"content\": {\"heading\": \"Questions\"}},"
			" {\"component\": \"faq\", \"variant\": \"list\", \"content\": {\"heading\": \"How we work\", \"items\": ["
			"{\"question\": \"One?\", \"answer\": \"Yes.\"}, {\"question\": \"Two?\", \"answer\": \"Yes.\"}]}}]}");
		g_assert_false(ls_components_check_page(components, plan, "questions.html", asked, &error));
		g_assert_nonnull(strstr(error->message, "steps"));
		g_clear_error(&error);
	}
	/* A heading that promises opening hours gives them in its own section
	 * (slice 4's dentist: "Where we are and when we're open" over an
	 * opening with no hours). */
	{
		g_autoptr(JsonNode) hours = parse("{\"path\": \"contact.html\", \"sections\": [{\"component\":"
			" \"page-header\", \"variant\": \"plain\", \"content\": {\"heading\": \"Where we are and when we're open\","
			" \"lede\": \"We serve Madison and Dane County.\"}}]}");
		g_autoptr(JsonNode) given = parse("{\"path\": \"contact.html\", \"sections\": [{\"component\":"
			" \"page-header\", \"variant\": \"plain\", \"content\": {\"heading\": \"Our hours\","
			" \"lede\": \"Open Mon-Thu 8am-5pm, Fri 8am-1pm.\"}}]}");
		g_assert_false(ls_components_check_page(components, plan, "contact.html", hours, &error));
		g_assert_nonnull(strstr(error->message, "hours"));
		g_clear_error(&error);
		ls_components_check_page(components, plan, "contact.html", given, &error);
		g_assert_no_error(error);
	}
	/* The site gate's banned filler is refused before publication. */
	{
		g_autoptr(JsonNode) filler = parse("{\"path\": \"contact.html\", \"sections\": [{\"component\":"
			" \"page-header\", \"variant\": \"plain\", \"content\": {\"heading\": \"Contact\","
			" \"lede\": \"AC repair is one of the four services we list.\"}}]}");
		g_assert_false(ls_components_check_page(components, plan, "contact.html", filler, &error));
		g_assert_nonnull(strstr(error->message, "one of the four services"));
		g_clear_error(&error);
	}
	/* Pages that are not a service's keep their freedom. */
	{
		g_autoptr(JsonNode) contact = parse("{\"path\": \"contact.html\", \"sections\": [{\"component\":"
			" \"page-header\", \"variant\": \"plain\", \"content\": {\"heading\": \"Contact\"}}]}");
		ls_components_check_page(components, plan, "contact.html", contact, &error);
		g_assert_no_error(error);
	}
}

int
main(int argc, char **argv){
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/content/service-pages", test_service_pages);
	g_test_add_func("/content/facts", test_facts);
	g_test_add_func("/content/sections-gated", test_sections_gated);
	g_test_add_func("/content/navigation", test_navigation);
	g_test_add_func("/content/home-depth", test_home_depth);
	g_test_add_func("/content/service-depth", test_service_depth);
	return g_test_run();
}
