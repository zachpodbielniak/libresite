/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* Presets and the picker (docs/components.org, "Presets and the picker"):
 * the presets and variants load through the catalog, the picker is
 * deterministic, the owner's words come first, colour pairs are repaired
 * to AA, content decides which variants are eligible and the clash rules
 * hold for every pick. */
#include "libresite.h"
#include "core/ls-components.h"
#include "core/ls-design.h"
#include "core/ls-util.h"
#include <stdio.h>
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
	JsonObject *root = json_node_get_object(ls_components_get_catalog(components));
	g_assert_true(json_object_has_member(root, "library"));
	return json_object_get_object_member(root, "library");
}

static JsonNode *
parse(const gchar *text){
	g_autoptr(GError) error = NULL;
	JsonNode *node = ls_json_parse(text, &error);
	g_assert_no_error(error);
	return node;
}

/* A plumber as the platform sends it: five offers, a phone, the owner's
 * colour and style words in the requirements. */
static const gchar plumber[] =
	"{\"brand\": {\"name\": \"Hartwell Plumbing\", \"tagline\": \"Family plumbing and drain service in Fairport.\","
	" \"colors\": \"navy and white\"},"
	" \"contact\": {\"phone\": \"585-555-0141\", \"email\": \"office@hartwell.example\"},"
	" \"booking\": {\"href\": \"/book\"},"
	" \"offers\": [{\"id\": \"a\", \"name\": \"Emergency repairs\"}, {\"id\": \"b\", \"name\": \"Drain cleaning\"},"
	" {\"id\": \"c\", \"name\": \"Water heaters\"}, {\"id\": \"d\", \"name\": \"Sump pumps\"},"
	" {\"id\": \"e\", \"name\": \"Bathroom and kitchen plumbing\"}],"
	" \"requirements\": \"Use one form. Colours: navy and white. Style: clean and modern, trustworthy. "
	"A licensed plumber fixes it and cleans up after.\"}";

/* A salon with only names: no words, no descriptions. */
static const gchar salon[] =
	"{\"brand\": {\"name\": \"Studio Nine Salon\", \"tagline\": \"Hair salon\"},"
	" \"contact\": {\"phone\": \"503-555-0122\"}, \"booking\": {\"href\": \"/book\"},"
	" \"offers\": [{\"id\": \"a\", \"name\": \"Cuts\"}, {\"id\": \"b\", \"name\": \"Colour\"},"
	" {\"id\": \"c\", \"name\": \"Balayage\"}, {\"id\": \"d\", \"name\": \"Blowouts\"}, {\"id\": \"e\", \"name\": \"Bridal\"}],"
	" \"requirements\": \"Use one form.\"}";

static const gchar four_photos[] =
	"[{\"path\": \"assets/a.jpg\", \"alt\": \"A plumber under a sink.\", \"width\": 1920, \"height\": 1440},"
	" {\"path\": \"assets/b.jpg\", \"alt\": \"A water heater.\", \"width\": 1920, \"height\": 1080},"
	" {\"path\": \"assets/c.jpg\", \"alt\": \"A faucet.\", \"width\": 1280, \"height\": 1707},"
	" {\"path\": \"assets/d.jpg\", \"alt\": \"A faucet.\", \"width\": 1280, \"height\": 1707}]";

static JsonObject *
themes_of(LsComponents *components){
	return ls_components_get_themes(components);
}

static JsonObject *
design_for(LsComponents *components, const gchar *site_text, const gchar *preset, const gchar *photos_text){
	g_autoptr(JsonNode) site = parse(site_text);
	g_autoptr(JsonNode) photos = photos_text != NULL ? parse(photos_text) : NULL;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *picked = preset == NULL
		? ls_design_pick_preset(block(components), themes_of(components), json_node_get_object(site)) : NULL;
	JsonObject *design = ls_design_new(block(components), themes_of(components), json_node_get_object(site),
		preset != NULL ? preset : picked, photos != NULL ? json_node_get_array(photos) : NULL, &error);
	g_assert_no_error(error);
	g_assert_nonnull(design);
	return design;
}

static gchar *
encode(JsonObject *object){
	g_autoptr(JsonNode) node = json_node_new(JSON_NODE_OBJECT);
	json_node_set_object(node, object);
	return ls_json_encode(node);
}

/* The variant a design gave @component on the home page, or NULL. */
static const gchar *
variant_of(JsonObject *design, const gchar *component){
	JsonArray *home = json_object_get_array_member(design, "home");
	guint i;
	for (i = 0; i < json_array_get_length(home); i++) {
		JsonObject *section = json_array_get_object_element(home, i);
		if (g_strcmp0(ls_string(section, "component"), component) == 0)
			return ls_string(section, "variant");
	}
	return NULL;
}

/* The three slice presets load as themes through the catalog, each with a
 * design block that names the real site it was read from; every library
 * variant names its source too, and exists in the catalog. */
static void
test_presets_load(void){
	g_autoptr(LsComponents) components = library();
	g_auto(GStrv) themes = ls_components_list_themes(components);
	JsonObject *variants = json_object_get_object_member(block(components), "variants");
	g_autoptr(GList) names = json_object_get_members(variants);
	const gchar *presets[] = {"evergreen", "press", "lindholm"};
	GList *name;
	guint i;
	for (i = 0; i < G_N_ELEMENTS(presets); i++) {
		JsonObject *theme = json_object_get_object_member(themes_of(components), presets[i]);
		JsonObject *design;
		g_assert_true(g_strv_contains((const gchar *const *)themes, presets[i]));
		g_assert_nonnull(theme);
		design = json_object_get_object_member(theme, "design");
		g_assert_nonnull(design);
		g_assert_cmpuint(strlen(ls_string(design, "source")), >, 20);
	}
	g_assert_cmpuint(g_list_length(names), >=, 14);
	for (name = names; name != NULL; name = name->next) {
		JsonObject *entry = json_object_get_object_member(variants, name->data);
		const gchar *pair = name->data;
		const gchar *slash = strchr(pair, '/');
		g_autofree gchar *component = g_strndup(pair, (gsize)(slash - pair));
		g_assert_nonnull(slash);
		g_assert_cmpuint(strlen(ls_string(entry, "source")), >, 10);
		if (!g_str_equal(component, "header") && !g_str_equal(component, "footer")) {
			JsonObject *catalog = json_object_get_object_member(json_node_get_object(
				ls_components_get_catalog(components)), "components");
			JsonObject *owner = json_object_get_object_member(catalog, component);
			g_assert_nonnull(owner);
			g_assert_true(json_object_has_member(json_object_get_object_member(owner, "variants"), slash + 1));
		}
	}
}

/* The hash is FNV-1a over lower case: fixed values, whatever the process. */
static void
test_hash(void){
	g_assert_cmpuint(ls_design_hash(""), ==, 2166136261u);
	g_assert_cmpuint(ls_design_hash("a"), ==, 0xe40c292cu);
	g_assert_cmpuint(ls_design_hash("Hartwell Plumbing"), ==, ls_design_hash("hartwell plumbing"));
	g_assert_cmpuint(ls_design_hash("Hartwell Plumbing"), !=, ls_design_hash("Studio Nine Salon"));
}

/* The same order always gives the same site; a fresh library gives it too. */
static void
test_determinism(void){
	g_autoptr(LsComponents) components = library();
	g_autoptr(LsComponents) again = library();
	g_autoptr(JsonObject) first = design_for(components, plumber, NULL, four_photos);
	g_autoptr(JsonObject) second = design_for(again, plumber, NULL, four_photos);
	g_autoptr(JsonObject) third = design_for(components, salon, NULL, four_photos);
	g_autoptr(JsonObject) fourth = design_for(again, salon, NULL, four_photos);
	g_autofree gchar *a = encode(first), *b = encode(second), *c = encode(third), *d = encode(fourth);
	g_assert_cmpstr(a, ==, b);
	g_assert_cmpstr(c, ==, d);
	g_assert_cmpstr(a, !=, c);
}

/* Owner words first: "clean and modern, trustworthy" is evergreen; "warm"
 * and "calm" words are press; a forced preset is kept; without words the
 * salon's sector weighs press highest. The owner's named colour becomes
 * the brand colour exactly when it passes AA. */
static void
test_owner_words(void){
	g_autoptr(LsComponents) components = library();
	g_autoptr(JsonNode) site = parse(plumber);
	g_autoptr(JsonNode) warm = parse("{\"brand\": {\"name\": \"Hartwell Plumbing\"}, \"style\": \"warm, soft and calm\"}");
	g_autoptr(GPtrArray) words = ls_design_owner_words(json_node_get_object(site));
	g_autofree gchar *picked = ls_design_pick_preset(block(components), themes_of(components), json_node_get_object(site));
	g_autofree gchar *warm_pick = ls_design_pick_preset(block(components), themes_of(components),
		json_node_get_object(warm));
	g_autoptr(JsonObject) forced = design_for(components, plumber, "lindholm", four_photos);
	g_autoptr(JsonObject) evergreen = design_for(components, plumber, NULL, four_photos);
	JsonObject *roles = json_object_get_object_member(evergreen, "roles");
	const gchar *navy = ls_string(json_object_get_object_member(block(components), "colors"), "navy");
	guint i;
	gboolean clean = FALSE, cleans = FALSE;
	for (i = 0; i < words->len; i++) {
		clean = clean || g_str_equal(g_ptr_array_index(words, i), "clean");
		cleans = cleans || g_str_equal(g_ptr_array_index(words, i), "cleans");
	}
	g_assert_true(clean);
	/* Only the owner's colour and style sentences count. */
	g_assert_false(cleans);
	g_assert_cmpstr(picked, ==, "evergreen");
	g_assert_cmpstr(warm_pick, ==, "press");
	g_assert_cmpstr(ls_string(forced, "preset"), ==, "lindholm");
	g_assert_cmpstr(ls_string(evergreen, "preset"), ==, "evergreen");
	g_assert_nonnull(navy);
	g_assert_cmpstr(ls_string(roles, "brand"), ==, navy);
	g_assert_cmpfloat(ls_design_contrast(ls_string(roles, "brand"), ls_string(roles, "brand_fg")), >=, 4.5);
	{
		g_autoptr(JsonObject) plain = design_for(components, salon, NULL, four_photos);
		g_assert_cmpstr(ls_string(plain, "preset"), ==, "press");
	}
}

/* Contrast repair moves lightness only, as little as it takes, and leaves a
 * passing pair alone. Every role pair of every preset passes after repair. */
static void
test_contrast_repair(void){
	g_autoptr(LsComponents) components = library();
	g_autofree gchar *leaf = ls_design_repair("#72ab03", "#ffffff", 4.5);
	g_autofree gchar *kept = ls_design_repair("#163e31", "#ffffff", 4.5);
	g_autofree gchar *pale = ls_design_repair("#f6f4ed", "#ffffff", 4.5);
	const gchar *presets[] = {"evergreen", "press", "lindholm"};
	const gchar *pairs[][2] = {{"bg", "fg"}, {"bg", "muted_fg"}, {"surface", "surface_fg"}, {"surface", "muted_fg"},
		{"primary", "primary_fg"}, {"brand", "brand_fg"}, {"brand", "brand_muted"}, {"inverse", "inverse_fg"},
		{"inverse", "inverse_muted"}, {"photo_primary", "photo_primary_fg"}, {"brand", "brand_accent"}};
	guint p, i;
	g_assert_cmpfloat(ls_design_contrast(leaf, "#ffffff"), >=, 4.5);
	g_assert_cmpfloat(ls_design_contrast(leaf, "#ffffff"), <, 5.2);
	g_assert_cmpstr(kept, ==, "#163e31");
	g_assert_cmpfloat(ls_design_contrast(pale, "#ffffff"), >=, 4.5);
	/* The hue stays: the repaired leaf green is still green. */
	{
		guint r, g, b;
		g_assert_cmpint(sscanf(leaf, "#%02x%02x%02x", &r, &g, &b), ==, 3);
		g_assert_cmpuint(g, >, r);
		g_assert_cmpuint(g, >, b);
	}
	for (p = 0; p < G_N_ELEMENTS(presets); p++) {
		g_autoptr(JsonObject) design = design_for(components, plumber, presets[p], four_photos);
		g_autoptr(GString) why = g_string_new(NULL);
		JsonObject *roles = json_object_get_object_member(design, "roles");
		for (i = 0; i < G_N_ELEMENTS(pairs); i++)
			g_assert_cmpfloat(ls_design_contrast(ls_string(roles, pairs[i][0]), ls_string(roles, pairs[i][1])), >=, 4.5);
		g_assert_true(ls_design_check(block(components), design, why));
		g_assert_cmpstr(why->str, ==, "");
	}
}

/* Content decides which variants may show: photo cards need a photo for
 * every service after the hero's; call-first and the utility header need a
 * phone; reviews show only with supplied quotes; without a photo the home
 * opens on the plain fallback opening. */
static void
test_eligibility(void){
	g_autoptr(LsComponents) components = library();
	g_autoptr(JsonObject) few = design_for(components, plumber, "evergreen", four_photos);
	g_autoptr(JsonObject) none = design_for(components, "{\"brand\": {\"name\": \"Quiet Co\"},"
		" \"offers\": [{\"id\": \"a\", \"name\": \"One\"}, {\"id\": \"b\", \"name\": \"Two\"}]}", "evergreen", NULL);
	g_autoptr(JsonObject) quoted = design_for(components,
		"{\"brand\": {\"name\": \"Hartwell Plumbing\"}, \"contact\": {\"phone\": \"585-555-0141\"},"
		" \"offers\": [{\"id\": \"a\", \"name\": \"One\"}, {\"id\": \"b\", \"name\": \"Two\"}],"
		" \"reviews\": [{\"quote\": \"Fixed the leak the same day.\", \"name\": \"Ann P.\"},"
		" {\"quote\": \"Clean, quick and fair.\", \"name\": \"Raj S.\"},"
		" {\"quote\": \"Came out at midnight.\", \"name\": \"Lee M.\"}]}", "evergreen", four_photos);
	guint i;
	g_assert_cmpstr(variant_of(few, "services"), !=, "photo-cards");
	g_assert_null(variant_of(few, "reviews"));
	g_assert_nonnull(variant_of(quoted, "reviews"));
	g_assert_cmpstr(variant_of(quoted, "services"), ==, "photo-cards");
	g_assert_cmpstr(variant_of(none, "contact"), !=, "call-first");
	g_assert_cmpstr(ls_string(none, "header"), !=, "utility");
	g_assert_cmpstr(variant_of(none, "hero"), ==, "centered");
	g_assert_cmpstr(ls_string(none, "action"), ==, "quote");
	/* The plumber calls; the salon books, whatever booking page is supplied. */
	g_assert_cmpstr(ls_string(few, "action"), ==, "call");
	{
		g_autoptr(JsonObject) booked = design_for(components, salon, NULL, four_photos);
		g_assert_cmpstr(ls_string(booked, "action"), ==, "book");
		g_assert_cmpstr(ls_string(booked, "sector"), ==, "wellness");
		/* A site that books never leads its contact with a call. */
		g_assert_cmpstr(variant_of(booked, "contact"), !=, "call-first");
	}
	/* A preset's preference list falls to its next choice: evergreen's
	 * plumber without a photo per card gets the ruled price list. */
	g_assert_cmpstr(variant_of(few, "services"), ==, "price-list");
	for (i = 0; i < json_array_get_length(json_object_get_array_member(quoted, "home")); i++)
		g_assert_nonnull(ls_string(json_array_get_object_element(json_object_get_array_member(quoted, "home"), i), "tone"));
}

/* The clash rules: a hand-made clash is refused with the rule it breaks,
 * and two hundred businesses' picks all keep every rule. */
static void
test_clash_rules(void){
	g_autoptr(LsComponents) components = library();
	g_autoptr(JsonNode) bad = parse("[{\"component\": \"hero\", \"variant\": \"split-card\", \"tone\": \"default\"},"
		" {\"component\": \"services\", \"variant\": \"photo-cards\", \"tone\": \"surface\"},"
		" {\"component\": \"reviews\", \"variant\": \"card-grid\", \"tone\": \"surface\"}]");
	g_autoptr(GString) why = g_string_new(NULL);
	const gchar *kinds[] = {"Plumbing", "Salon", "Roofing", "Hair Studio", "Drains"};
	guint i;
	g_assert_false(ls_design_check_home(block(components), json_node_get_array(bad), "columns", why));
	g_assert_nonnull(strstr(why->str, "tone"));
	g_assert_nonnull(strstr(why->str, "family"));
	for (i = 0; i < 200; i++) {
		g_autofree gchar *site = g_strdup_printf("{\"brand\": {\"name\": \"Example %u %s\"},"
			" \"contact\": {\"phone\": \"585-555-0141\"},"
			" \"offers\": [{\"id\": \"a\", \"name\": \"One\"}, {\"id\": \"b\", \"name\": \"Two\"},"
			" {\"id\": \"c\", \"name\": \"Three\"}],"
			" \"reviews\": [{\"quote\": \"Good.\", \"name\": \"A\"}, {\"quote\": \"Fine.\", \"name\": \"B\"},"
			" {\"quote\": \"Great.\", \"name\": \"C\"}]}", i, kinds[i % G_N_ELEMENTS(kinds)]);
		g_autoptr(JsonObject) design = design_for(components, site, NULL, four_photos);
		g_autoptr(GString) broken = g_string_new(NULL);
		if (!ls_design_check_home(block(components), json_object_get_array_member(design, "home"),
			ls_string(design, "footer"), broken))
			g_error("%s: %s", site, broken->str);
	}
}

/* site.avoid removes the presets of nearby sites from the hash pick: a
 * second salon in the same street avoiding the first one's preset gets
 * another, and the owner's own words still win over it. */
static void
test_avoid(void){
	g_autoptr(LsComponents) components = library();
	g_autoptr(JsonNode) plain = parse(salon);
	g_autofree gchar *first = ls_design_pick_preset(block(components), themes_of(components), json_node_get_object(plain));
	g_autofree gchar *avoid = g_strdup_printf("[{\"v\": 1, \"preset\": \"%s\"}]", first);
	g_autoptr(JsonNode) avoided = parse(avoid);
	g_autoptr(JsonNode) worded = parse("{\"brand\": {\"name\": \"Studio Nine Salon\"}, \"style\": \"warm and calm\"}");
	g_autofree gchar *second = NULL, *kept = NULL;
	json_object_set_member(json_node_get_object(plain), "avoid", json_node_copy(avoided));
	json_object_set_member(json_node_get_object(worded), "avoid", json_node_copy(avoided));
	second = ls_design_pick_preset(block(components), themes_of(components), json_node_get_object(plain));
	kept = ls_design_pick_preset(block(components), themes_of(components), json_node_get_object(worded));
	g_assert_cmpstr(first, ==, "press");
	g_assert_cmpstr(second, !=, first);
	g_assert_cmpstr(kept, ==, "press");
}

/* A home page as a writer answers TASK's design: the opening, the services
 * and the contact section, in the writer's own words. */
static const gchar home_page[] =
	"{\"index.html\": {\"path\": \"index.html\", \"sections\": ["
	"{\"component\": \"hero\", \"variant\": \"photo-start\", \"content\": {\"kicker\": \"Fairport since 1998\","
	" \"heading\": \"Plumbing and drains for Fairport homes\", \"lede\": \"Family plumbers on call around the clock.\"}},"
	"{\"component\": \"features\", \"variant\": \"numbered\", \"content\": {\"heading\": \"Dropped\","
	" \"items\": [{\"title\": \"One\", \"body\": \"Two\"}]}},"
	"{\"component\": \"services\", \"variant\": \"rows\", \"content\": {\"heading\": \"What we fix\","
	" \"items\": [{\"name\": \"Emergency repairs\", \"summary\": \"Burst pipes and leaks. Calls come at all hours. Water off first.\"},"
	" {\"name\": \"Drain cleaning\", \"summary\": \"Kitchen and main lines. Slow drains first. Roots block old lines.\"},"
	" {\"name\": \"Sump pumps\", \"summary\": \"Pumps that keep a basement dry. Spring is busiest. Test it yearly.\"}]}},"
	"{\"component\": \"faq\", \"variant\": \"list\", \"content\": {\"heading\": \"Questions\","
	" \"items\": [{\"question\": \"Do you clear main lines?\", \"answer\": \"Yes, kitchen and main lines.\"},"
	" {\"question\": \"Question 2?\", \"answer\": \"A general answer.\"}, {\"question\": \"Question 3?\", \"answer\": \"A general answer.\"}, {\"question\": \"Question 4?\", \"answer\": \"A general answer.\"}, {\"question\": \"Question 5?\", \"answer\": \"A general answer.\"}, {\"question\": \"Question 6?\", \"answer\": \"A general answer.\"}, {\"question\": \"Question 7?\", \"answer\": \"A general answer.\"}, {\"question\": \"Question 8?\", \"answer\": \"A general answer.\"}, {\"question\": \"Question 9?\", \"answer\": \"A general answer.\"}, {\"question\": \"Question 10?\", \"answer\": \"A general answer.\"}, {\"question\": \"Question 11?\", \"answer\": \"A general answer.\"}, {\"question\": \"Question 12?\", \"answer\": \"A general answer.\"}]}},"
	"{\"component\": \"cta\", \"variant\": \"band\", \"content\": {\"heading\": \"Call us or write\","
	" \"actions\": [{\"href\": \"index.html\", \"label\": \"Call\"}]}}]}}";

/* A plan wearing a preset composes: the preset's role tokens and tints are
 * in styles.css with no rotation, outline text or texture, the home page
 * renders the picked variants on their tones, the primary action is the
 * call, and every supplied photo with a credit is credited in the footer as
 * a stock photo. */
static void
test_compose(void){
	g_autoptr(LsComponents) components = library();
	g_autoptr(GError) error = NULL;
	g_autofree gchar *plan_text = NULL, *bundle = NULL;
	g_autoptr(JsonNode) plan = NULL, pages = NULL, composed = NULL;
	g_autoptr(JsonNode) site = parse(plumber);
	g_autoptr(JsonNode) photos = parse("[{\"path\": \"assets/photos/sink.jpg\", \"alt\": \"A plumber under a sink.\","
		" \"credit\": \"Plumber at work, flickr user rick, CC BY 2.0\", \"stock\": true, \"width\": 1920, \"height\": 1440}]");
	g_autoptr(JsonObject) design = NULL;
	JsonArray *files;
	const gchar *css = NULL, *home = NULL;
	guint i;
	g_assert_true(g_file_get_contents("tests/fixtures/composed/plan.json", &plan_text, NULL, NULL));
	plan = parse(plan_text);
	pages = parse(home_page);
	json_object_set_member(json_node_get_object(plan), "photos", json_node_copy(photos));
	/* The plan step copies the supplied phone and booking page in. */
	json_object_set_member(json_node_get_object(plan), "contact", parse("{\"phone\": \"585-555-0141\"}"));
	json_object_set_member(json_node_get_object(plan), "booking", parse("{\"href\": \"/book\"}"));
	design = ls_design_new(block(components), themes_of(components), json_node_get_object(site), "evergreen",
		json_node_get_array(photos), &error);
	g_assert_no_error(error);
	g_assert_true(ls_components_apply_theme(components, plan, "evergreen", &error));
	g_assert_no_error(error);
	g_assert_true(ls_components_apply_design(components, plan, design));
	ls_components_apply_default(components, plan, TRUE);
	ls_components_check_plan(components, plan, &error);
	g_assert_no_error(error);
	/* Every other planned page gets a short writer answer. */
	{
		JsonArray *planned = json_object_get_array_member(json_node_get_object(plan), "pages");
		for (i = 0; i < json_array_get_length(planned); i++) {
			const gchar *route = ls_string(json_array_get_object_element(planned, i), "path");
			g_autofree gchar *text = g_strdup_printf("{\"path\": \"%s\", \"sections\": [{\"component\": \"page-header\","
				" \"variant\": \"plain\", \"content\": {\"heading\": \"%s\"}}, {\"component\": \"text\", \"variant\":"
				" \"narrow\", \"content\": {\"body\": \"Words.\"}}, {\"component\": \"faq\", \"variant\": \"list\","
				" \"content\": {\"items\": [{\"question\": \"One?\", \"answer\": \"Yes.\"}, {\"question\": \"Two?\","
				" \"answer\": \"Yes.\"}, {\"question\": \"Three?\", \"answer\": \"Yes.\"}]}}]}", route,
				ls_string(json_array_get_object_element(planned, i), "title"));
			if (!g_str_equal(route, "index.html"))
				json_object_set_member(json_node_get_object(pages), route, parse(text));
		}
	}
	{
		JsonNode *page = json_object_get_member(json_node_get_object(pages), "index.html");
		JsonArray *sections;
		ls_components_apply_opening(components, plan, "index.html", page);
		ls_components_design_home(components, plan, page);
		ls_components_spread_photos(components, plan, page);
		ls_components_check_page(components, plan, "index.html", page, &error);
		g_assert_no_error(error);
		/* The home holds the designed sections only, in their order. */
		sections = json_object_get_array_member(json_node_get_object(page), "sections");
		g_assert_cmpuint(json_array_get_length(sections), ==, json_array_get_length(json_object_get_array_member(
			json_object_get_object_member(json_node_get_object(plan), "design"), "home")));
		g_assert_cmpstr(ls_string(json_array_get_object_element(sections, 1), "component"), ==, "services");
		/* The page closes on the call band, whose actions are the builder's. */
		{
			JsonObject *last = json_array_get_object_element(sections, json_array_get_length(sections) - 1);
			g_assert_cmpstr(ls_string(last, "component"), ==, "cta");
			g_assert_false(json_object_has_member(json_object_get_object_member(last, "content"), "actions"));
		}
	}
	bundle = ls_components_compose(components, plan, json_node_get_object(pages), NULL, &error);
	g_assert_no_error(error);
	composed = parse(bundle);
	files = json_object_get_array_member(json_node_get_object(composed), "files");
	for (i = 0; i < json_array_get_length(files); i++) {
		JsonObject *file = json_array_get_object_element(files, i);
		if (g_str_equal(ls_string(file, "path"), "styles.css"))
			css = ls_string(file, "content");
		if (g_str_equal(ls_string(file, "path"), "index.html"))
			home = ls_string(file, "content");
	}
	g_assert_nonnull(css);
	g_assert_nonnull(home);
	g_assert_nonnull(strstr(css, "--brand:"));
	g_assert_nonnull(strstr(css, "--primary-hover:"));
	g_assert_nonnull(strstr(css, "--radius-control:"));
	/* The design layer and its variants turn, outline and texture nothing. */
	{
		const gchar *layer = strstr(css, "/* design */");
		g_assert_nonnull(layer);
		g_assert_null(strstr(layer, "rotate"));
		g_assert_null(strstr(layer, "-webkit-text-stroke"));
		g_assert_null(strstr(layer, "texture"));
		g_assert_null(strstr(layer, "url("));
	}
	g_assert_nonnull(strstr(home, "design--evergreen"));
	{
		g_autofree gchar *hero = g_strdup_printf("hero--%s", variant_of(design, "hero"));
		g_assert_nonnull(strstr(home, hero));
	}
	g_assert_nonnull(strstr(home, "tone-"));
	g_assert_nonnull(strstr(home, "href=\"tel:5855550141\""));
	g_assert_nonnull(strstr(home, "Stock photo"));
	g_assert_nonnull(strstr(home, "flickr user rick"));
	/* Each service on the home links to its own page. */
	{
		const gchar *main = strstr(home, "<main");
		g_assert_nonnull(main);
		g_assert_nonnull(strstr(main, "href=\"sump-pumps.html\">Sump pumps</a>"));
		g_assert_nonnull(strstr(main, "href=\"drain-cleaning.html\">Drain cleaning</a>"));
	}
	/* The plumber's primary action is the call, not the booking page. */
	g_assert_null(strstr(home, "action-bar__book"));
}

int
main(int argc, char **argv){
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/design/presets-load", test_presets_load);
	g_test_add_func("/design/hash", test_hash);
	g_test_add_func("/design/determinism", test_determinism);
	g_test_add_func("/design/owner-words", test_owner_words);
	g_test_add_func("/design/contrast-repair", test_contrast_repair);
	g_test_add_func("/design/eligibility", test_eligibility);
	g_test_add_func("/design/clash-rules", test_clash_rules);
	g_test_add_func("/design/avoid", test_avoid);
	g_test_add_func("/design/compose", test_compose);
	return g_test_run();
}
