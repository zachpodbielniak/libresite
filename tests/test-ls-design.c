/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* Presets and the picker (docs/components.org, "Presets and the picker"):
 * the presets and variants load through the catalog, the picker is
 * deterministic, the owner's words come first, colour pairs are repaired
 * to AA, content decides which variants are eligible and the clash rules
 * hold for every pick. */
#include "libresite.h"
#include "core/ls-components.h"
#include "core/ls-design.h"
#include "core/ls-recipe.h"
#include "core/ls-util.h"
#include <math.h>
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

/* An order whose owner names a pale colour that fails AA as it is. */
static const gchar yellow_order[] =
	"{\"brand\": {\"name\": \"Sunny Side Cafe\", \"tagline\": \"Cafe and bakery\", \"colors\": \"yellow and white\"},"
	" \"contact\": {\"phone\": \"802-555-0147\"},"
	" \"offers\": [{\"id\": \"a\", \"name\": \"Bread\"}, {\"id\": \"b\", \"name\": \"Cakes\"}]}";

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

/* Every preset of the component-library proposal (section 3.2), with the
 * real site its values were read from. */
static const gchar *const presets[][2] = {
	{"evergreen", "Evergreen Home Heating"}, {"lindholm", "Lindholm Roofing"},
	{"veterans", "Veterans Contracting"}, {"highlands", "Highlands Landscaping"}, {"guin", "Guin Service"},
	{"weleave", "We Leave Clean"}, {"electrified", "Electrified Garage"}, {"press", "PRESS Modern Massage"},
	{"glenwood", "Glenwood South Barberstudio"}, {"gleamery", "The Gleamery"}, {"village", "Village Dental"},
	{"harte", "Harte Law"}, {"littlefish", "Little Fish Accounting"}, {"zinc", "Zinc Cafe"},
	{"mission", "Mission Bowling Club"}, {"tulip", "Table & Tulip"}};

/* The sixteen presets load as themes through the catalog, each with a
 * design block that names the real site it was read from; every library
 * variant names its source too, and exists in the catalog. */
static void
test_presets_load(void){
	g_autoptr(LsComponents) components = library();
	g_auto(GStrv) themes = ls_components_list_themes(components);
	JsonObject *variants = json_object_get_object_member(block(components), "variants");
	g_autoptr(GList) names = json_object_get_members(variants);
	GList *name;
	guint i, count = 0;
	for (i = 0; i < G_N_ELEMENTS(presets); i++) {
		JsonObject *theme = json_object_get_object_member(themes_of(components), presets[i][0]);
		JsonObject *design;
		g_assert_true(g_strv_contains((const gchar *const *)themes, presets[i][0]));
		g_assert_nonnull(theme);
		design = json_object_get_object_member(theme, "design");
		g_assert_nonnull(design);
		g_assert_cmpuint(strlen(ls_string(design, "source")), >, 20);
		if (strstr(ls_string(design, "source"), presets[i][1]) == NULL)
			g_error("preset %s does not name %s in its source", presets[i][0], presets[i][1]);
		g_assert_nonnull(json_object_get_array_member(design, "fits"));
	}
	/* No other theme carries a design block. */
	for (i = 0; themes[i] != NULL; i++)
		if (json_object_has_member(json_object_get_object_member(themes_of(components), themes[i]), "design"))
			count++;
	g_assert_cmpuint(count, ==, G_N_ELEMENTS(presets));
	g_assert_cmpuint(g_list_length(names), >=, 40);
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
 * hash picks the salon a preset that fits wellness. The owner's named colour becomes
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
	/* Neutral colour words such as "white" name no preset: an order that
	 * says only that gets the same pick as one with no words. */
	{
		g_autoptr(JsonNode) white = parse("{\"brand\": {\"name\": \"Hartwell Plumbing\", \"colors\": \"white\"}}");
		g_autoptr(JsonNode) bare = parse("{\"brand\": {\"name\": \"Hartwell Plumbing\"}}");
		g_autofree gchar *white_pick = ls_design_pick_preset(block(components), themes_of(components),
			json_node_get_object(white));
		g_autofree gchar *bare_pick = ls_design_pick_preset(block(components), themes_of(components),
			json_node_get_object(bare));
		g_assert_cmpstr(white_pick, ==, bare_pick);
	}
	g_assert_cmpstr(ls_string(forced, "preset"), ==, "lindholm");
	g_assert_cmpstr(ls_string(evergreen, "preset"), ==, "evergreen");
	g_assert_nonnull(navy);
	g_assert_cmpstr(ls_string(roles, "brand"), ==, navy);
	g_assert_cmpfloat(ls_design_contrast(ls_string(roles, "brand"), ls_string(roles, "brand_fg")), >=, 4.5);
	{
		g_autoptr(JsonObject) plain = design_for(components, salon, NULL, four_photos);
		JsonObject *theme = json_object_get_object_member(themes_of(components), ls_string(plain, "preset"));
		JsonArray *fits = json_object_get_array_member(json_object_get_object_member(theme, "design"), "fits");
		gboolean wellness = FALSE;
		for (i = 0; i < json_array_get_length(fits); i++)
			wellness = wellness || g_str_equal(json_array_get_string_element(fits, i), "wellness");
		g_assert_true(wellness);
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
	/* Each preset as it is, and under owner colour words that move its
	 * brand colour, keeps every text pair at AA. */
	for (p = 0; p < G_N_ELEMENTS(presets); p++) {
		const gchar *orders[] = {plumber, salon, yellow_order};
		guint o;
		for (o = 0; o < G_N_ELEMENTS(orders); o++) {
			g_autoptr(JsonObject) design = design_for(components, orders[o], presets[p][0], four_photos);
			g_autoptr(GString) why = g_string_new(NULL);
			JsonObject *roles = json_object_get_object_member(design, "roles");
			for (i = 0; i < G_N_ELEMENTS(pairs); i++)
				if (ls_design_contrast(ls_string(roles, pairs[i][0]), ls_string(roles, pairs[i][1])) < 4.5)
					g_error("%s: %s on %s is under 4.5:1", presets[p][0], pairs[i][1], pairs[i][0]);
			if (!ls_design_check(block(components), design, why))
				g_error("%s: %s", presets[p][0], why->str);
		}
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
	/* Nor does it wear the utility header, whose phone pill would be a
	 * second primary action beside the booking button: across many
	 * booking businesses, none gets it. */
	for (i = 0; i < 60; i++) {
		g_autofree gchar *text = g_strdup_printf("{\"brand\": {\"name\": \"Example %u Dental\", \"tagline\": \"Dentistry\"},"
			" \"contact\": {\"phone\": \"608-555-0163\"}, \"booking\": {\"href\": \"/book\"},"
			" \"offers\": [{\"id\": \"a\", \"name\": \"Cleanings\"}, {\"id\": \"b\", \"name\": \"Fillings\"}]}", i);
		g_autoptr(JsonObject) booking = design_for(components, text, NULL, four_photos);
		g_assert_cmpstr(ls_string(booking, "action"), ==, "book");
		g_assert_cmpstr(ls_string(booking, "header"), !=, "utility");
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
 * another. The owner's own words still win over it when they name one
 * preset best, and choose between presets they fit equally. */
static void
test_avoid(void){
	g_autoptr(LsComponents) components = library();
	g_autoptr(JsonNode) plain = parse(salon);
	g_autofree gchar *first = ls_design_pick_preset(block(components), themes_of(components), json_node_get_object(plain));
	g_autofree gchar *avoid = g_strdup_printf("[{\"v\": 1, \"preset\": \"%s\"}]", first);
	g_autoptr(JsonNode) avoided = parse(avoid);
	g_autoptr(JsonNode) press_near = parse("[{\"v\": 1, \"preset\": \"press\"}]");
	g_autoptr(JsonNode) worded = parse("{\"brand\": {\"name\": \"Studio Nine Salon\"}, \"style\": \"warm, soft and calm\"}");
	g_autoptr(JsonNode) tied = parse("{\"brand\": {\"name\": \"Studio Nine Salon\"}, \"style\": \"navy\"}");
	g_autofree gchar *second = NULL, *kept = NULL, *tie = NULL, *moved = NULL;
	json_object_set_member(json_node_get_object(plain), "avoid", json_node_copy(avoided));
	json_object_set_member(json_node_get_object(worded), "avoid", json_node_copy(press_near));
	second = ls_design_pick_preset(block(components), themes_of(components), json_node_get_object(plain));
	kept = ls_design_pick_preset(block(components), themes_of(components), json_node_get_object(worded));
	g_assert_cmpstr(second, !=, first);
	g_assert_cmpstr(kept, ==, "press");
	/* "navy" fits several presets equally; a nearby site wearing one
	 * leaves another of them. */
	tie = ls_design_pick_preset(block(components), themes_of(components), json_node_get_object(tied));
	{
		g_autofree gchar *near = g_strdup_printf("[{\"v\": 1, \"preset\": \"%s\"}]", tie);
		json_object_set_member(json_node_get_object(tied), "avoid", parse(near));
	}
	moved = ls_design_pick_preset(block(components), themes_of(components), json_node_get_object(tied));
	{
		const gchar *both[] = {tie, moved};
		guint b, w;
		for (b = 0; b < G_N_ELEMENTS(both); b++) {
			JsonArray *words = json_object_get_array_member(json_object_get_object_member(json_object_get_object_member(
				themes_of(components), both[b]), "design"), "words");
			gboolean navy = FALSE;
			for (w = 0; w < json_array_get_length(words); w++)
				navy = navy || g_str_equal(json_array_get_string_element(words, w), "navy");
			g_assert_true(navy);
		}
	}
	g_assert_cmpstr(tie, !=, moved);
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

/* An avoid entry as the builder's fingerprint writes it for @design. */
static gchar *
neighbour_of(JsonObject *design){
	return g_strdup_printf("[{\"v\": 1, \"preset\": \"%s\", \"home\": [\"hero/%s\"]}]", ls_string(design, "preset"),
		variant_of(design, "hero"));
}

/* Same-trade businesses in one town: the second carries the first one's
 * fingerprint in site.avoid, as the platform sends it, and gets another
 * preset and another hero. Owner words still decide the preset when they
 * name one, but the hero moves. */
static void
test_neighbours(void){
	g_autoptr(LsComponents) components = library();
	const gchar *pairs[][2] = {
		{"Hartwell Plumbing", "Bayside Plumbing & Drain"}, {"Summit Roofing Co", "Ridgeline Roofing"},
		{"Studio Nine Salon", "Mane Street Hair Salon"}, {"Lakeview Family Dental", "Northside Dental Care"},
		{"Okafor & Reyes Law", "Bell Family Law"}, {"Crumb & Co Bakery", "Rise Bakery"},
		{"Polar Air Heating & Cooling", "Comfort Pro Heating"}};
	const gchar *trades[] = {"Plumbing", "Roofing", "Hair salon", "Dentistry", "Law office", "Bakery", "Heating"};
	guint i;
	for (i = 0; i < G_N_ELEMENTS(pairs); i++) {
		g_autofree gchar *first_text = g_strdup_printf("{\"brand\": {\"name\": \"%s\", \"tagline\": \"%s in Fairport\"},"
			" \"contact\": {\"phone\": \"585-555-0141\"}, \"booking\": {\"href\": \"/book\"},"
			" \"offers\": [{\"id\": \"a\", \"name\": \"One\"}, {\"id\": \"b\", \"name\": \"Two\"}, {\"id\": \"c\", \"name\": \"Three\"}]}",
			pairs[i][0], trades[i]);
		g_autoptr(JsonObject) first = design_for(components, first_text, NULL, four_photos);
		g_autofree gchar *avoid = neighbour_of(first);
		g_autofree gchar *second_text = g_strdup_printf("{\"brand\": {\"name\": \"%s\", \"tagline\": \"%s in Fairport\"},"
			" \"contact\": {\"phone\": \"585-555-0177\"}, \"booking\": {\"href\": \"/book\"},"
			" \"offers\": [{\"id\": \"a\", \"name\": \"One\"}, {\"id\": \"b\", \"name\": \"Two\"}, {\"id\": \"c\", \"name\": \"Three\"}],"
			" \"avoid\": %s}", pairs[i][1], trades[i], avoid);
		g_autoptr(JsonObject) second = design_for(components, second_text, NULL, four_photos);
		if (g_strcmp0(ls_string(first, "preset"), ls_string(second, "preset")) == 0 ||
			g_strcmp0(variant_of(first, "hero"), variant_of(second, "hero")) == 0)
			g_error("%s and %s share %s/%s and %s/%s", pairs[i][0], pairs[i][1], ls_string(first, "preset"),
				variant_of(first, "hero"), ls_string(second, "preset"), variant_of(second, "hero"));
	}
	/* The same order twice, the second beside the first: the owner's words
	 * keep their preset, and the hero moves. */
	{
		g_autoptr(JsonObject) first = design_for(components, plumber, NULL, four_photos);
		g_autofree gchar *avoid = neighbour_of(first);
		g_autoptr(JsonNode) site = parse(plumber);
		g_autoptr(JsonNode) list = parse(avoid);
		g_autoptr(JsonNode) photos = parse(four_photos);
		g_autoptr(GError) error = NULL;
		g_autofree gchar *picked = NULL;
		g_autoptr(JsonObject) second = NULL;
		json_object_set_member(json_node_get_object(site), "avoid", json_node_copy(list));
		picked = ls_design_pick_preset(block(components), themes_of(components), json_node_get_object(site));
		second = ls_design_new(block(components), themes_of(components), json_node_get_object(site), picked,
			json_node_get_array(photos), &error);
		g_assert_no_error(error);
		g_assert_cmpstr(variant_of(first, "hero"), !=, variant_of(second, "hero"));
	}
}

/* Businesses of one trade spread over the presets and openings that fit
 * it, the presets that fit the trade weigh most, every pick keeps the clash
 * rules, and the home's section order varies where the trade allows. */
static void
test_spread(void){
	g_autoptr(LsComponents) components = library();
	const gchar *trades[][2] = {{"Plumbing", "trades"}, {"Dental", "clinic"}, {"Law", "professional"},
		{"Bakery", "food"}, {"Salon", "wellness"}};
	guint t;
	for (t = 0; t < G_N_ELEMENTS(trades); t++) {
		g_autoptr(GHashTable) presets_seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
		g_autoptr(GHashTable) heroes = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
		g_autoptr(GHashTable) orders = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
		guint i, fitting = 0;
		for (i = 0; i < 80; i++) {
			g_autofree gchar *site = g_strdup_printf("{\"brand\": {\"name\": \"Example %u %s\", \"tagline\":"
				" \"%s since 1998\"}, \"contact\": {\"phone\": \"585-555-0141\", \"address\": \"1 Main Street, Fairport, NY 14450\"},"
				" \"offers\": [{\"id\": \"a\", \"name\": \"One\"}, {\"id\": \"b\", \"name\": \"Two\"}, {\"id\": \"c\", \"name\": \"Three\"}],"
				" \"requirements\": \"Hours: Mon-Fri 9am-5pm. Service area: Fairport and Webster. The owner's own process, in order:"
				" 1. Call. 2. Visit. 3. Done.\"}", i, trades[t][0], trades[t][0]);
			g_autoptr(JsonObject) design = design_for(components, site, NULL, four_photos);
			JsonObject *theme = json_object_get_object_member(themes_of(components), ls_string(design, "preset"));
			JsonArray *home = json_object_get_array_member(design, "home");
			g_autoptr(GString) order = g_string_new(NULL), broken = g_string_new(NULL);
			guint j;
			g_assert_cmpstr(ls_string(design, "sector"), ==, trades[t][1]);
			g_hash_table_add(presets_seen, g_strdup(ls_string(design, "preset")));
			g_hash_table_add(heroes, g_strdup(variant_of(design, "hero")));
			for (j = 0; j < json_array_get_length(home); j++)
				g_string_append_printf(order, "%s ", ls_string(json_array_get_object_element(home, j), "component"));
			g_hash_table_add(orders, g_strdup(order->str));
			for (j = 0; j < json_array_get_length(json_object_get_array_member(json_object_get_object_member(theme,
				"design"), "fits")); j++)
				fitting += g_strcmp0(json_array_get_string_element(json_object_get_array_member(
					json_object_get_object_member(theme, "design"), "fits"), j), trades[t][1]) == 0 ? 1 : 0;
			if (!ls_design_check(block(components), design, broken))
				g_error("%s: %s", site, broken->str);
		}
		if (g_hash_table_size(presets_seen) < 4 || g_hash_table_size(heroes) < 4 || g_hash_table_size(orders) < 2)
			g_error("%s: %u presets, %u heroes, %u orders over 80 businesses", trades[t][0],
				g_hash_table_size(presets_seen), g_hash_table_size(heroes), g_hash_table_size(orders));
		/* The trade weights the pick; it never forbids another preset. */
		g_assert_cmpuint(fitting, >, 40);
		g_assert_cmpuint(fitting, <, 80);
	}
}

/* An order with every fact the new sections can show: the year, hours,
 * towns, address and process, supplied press names, a rating and two
 * shops. */
static const gchar rich[] =
	"{\"brand\": {\"name\": \"Hartwell Plumbing\", \"tagline\": \"Family plumbing in Fairport since 1998.\"},"
	" \"contact\": {\"phone\": \"585-555-0141\", \"email\": \"office@hartwell.example\","
	" \"address\": \"214 Main Street, Suite 2, Fairport, NY 14450\"}, \"booking\": {\"href\": \"/book\"},"
	" \"offers\": [{\"id\": \"a\", \"name\": \"Emergency repairs\", \"description\": \"Burst pipes and leaks.\"},"
	" {\"id\": \"b\", \"name\": \"Drain cleaning\", \"description\": \"Kitchen and main lines.\"},"
	" {\"id\": \"c\", \"name\": \"Water heaters\", \"description\": \"Tank and tankless.\"}],"
	" \"press\": [\"Democrat and Chronicle\", \"WXXI News\"],"
	" \"rating\": {\"score\": 4.9, \"count\": 212, \"source\": \"Google\"},"
	" \"locations\": [{\"name\": \"Fairport shop\", \"address\": \"214 Main Street, Fairport, NY 14450\","
	" \"hours\": \"Mon-Fri 7am-6pm\"}, {\"name\": \"Webster shop\", \"address\": \"9 North Avenue, Webster, NY 14580\"}],"
	" \"requirements\": \"Use one form. Hours: Mon-Fri 7am-6pm. Service area: Fairport, Penfield, Perinton and Webster."
	" The owner's own process, in order: 1. Call or send the form. 2. We give a price first. 3. We fix it and clean up.\"}";

/* Eight photos: six stock, enough for the opening, a photo per service and
 * the closing band, and two of the owner's own for the story. */
static const gchar eight_photos[] =
	"[{\"path\": \"assets/a.jpg\", \"alt\": \"A plumber under a sink.\", \"stock\": true, \"width\": 1920, \"height\": 1440},"
	" {\"path\": \"assets/b.jpg\", \"alt\": \"A water heater.\", \"stock\": true, \"width\": 1920, \"height\": 1080},"
	" {\"path\": \"assets/c.jpg\", \"alt\": \"A faucet.\", \"stock\": true, \"width\": 1280, \"height\": 1707},"
	" {\"path\": \"assets/d.jpg\", \"alt\": \"A drain cable.\", \"stock\": true, \"width\": 1600, \"height\": 1200},"
	" {\"path\": \"assets/e.jpg\", \"alt\": \"A new boiler.\", \"stock\": true, \"width\": 1600, \"height\": 1200},"
	" {\"path\": \"assets/f.jpg\", \"alt\": \"Copper pipes.\", \"stock\": true, \"width\": 1600, \"height\": 1200},"
	" {\"path\": \"assets/g.jpg\", \"alt\": \"The owner at the shop.\", \"width\": 1600, \"height\": 1200},"
	" {\"path\": \"assets/h.jpg\", \"alt\": \"The van.\", \"width\": 1600, \"height\": 1200}]";

/* The rich order's home as a writer answers it. */
static const gchar rich_home[] =
	"{\"path\": \"index.html\", \"sections\": ["
	"{\"component\": \"hero\", \"variant\": \"photo-start\", \"content\": {\"kicker\": \"Fairport since 1998\","
	" \"heading\": \"Plumbing and drains for Fairport homes\", \"lede\": \"Family plumbers on call around the clock.\"}},"
	"{\"component\": \"services\", \"variant\": \"rows\", \"content\": {\"heading\": \"What we fix\","
	" \"items\": [{\"name\": \"Emergency repairs\", \"summary\": \"Burst pipes and leaks.\"},"
	" {\"name\": \"Drain cleaning\", \"summary\": \"Kitchen and main lines.\"},"
	" {\"name\": \"Water heaters\", \"summary\": \"Tank and tankless.\"}]}},"
	"{\"component\": \"steps\", \"variant\": \"row\", \"content\": {\"heading\": \"How a job goes\","
	" \"items\": [{\"title\": \"A\"}, {\"title\": \"B\"}]}},"
	"{\"component\": \"about\", \"variant\": \"history\", \"content\": {\"heading\": \"A Fairport family firm\","
	" \"body\": \"Hartwell Plumbing has served Fairport since 1998.\"}},"
	"{\"component\": \"areas\", \"variant\": \"town-list\", \"content\": {\"heading\": \"Where we work\"}},"
	"{\"component\": \"faq\", \"variant\": \"list\", \"content\": {\"heading\": \"Questions\","
	" \"items\": [{\"question\": \"Q1?\", \"answer\": \"A general answer.\"}, {\"question\": \"Q2?\", \"answer\": \"A general answer.\"},"
	" {\"question\": \"Q3?\", \"answer\": \"A general answer.\"}, {\"question\": \"Q4?\", \"answer\": \"A general answer.\"},"
	" {\"question\": \"Q5?\", \"answer\": \"A general answer.\"}, {\"question\": \"Q6?\", \"answer\": \"A general answer.\"},"
	" {\"question\": \"Q7?\", \"answer\": \"A general answer.\"}, {\"question\": \"Q8?\", \"answer\": \"A general answer.\"},"
	" {\"question\": \"Q9?\", \"answer\": \"A general answer.\"}, {\"question\": \"Q10?\", \"answer\": \"A general answer.\"}]}},"
	"{\"component\": \"cta\", \"variant\": \"band\", \"content\": {\"heading\": \"Call us or write\"}}]}";

/* The component gallery's realistic pages and photos, while it is built. */
static gchar *gallery_pages = NULL;
static gchar *gallery_photos = NULL;
/* The order's colour scheme lock for compose_site(), or NULL for none. */
static const gchar *compose_scheme = NULL;

/* A live form as the platform supplies it, three supplied reviews, and a
 * contact page whose form section the quote action leads to. */
static const gchar supplied_form[] =
	"{\"action\": \"/api/forms/quote\", \"honeypot\": \"website\", \"fields\": ["
	"{\"name\": \"name\", \"label\": \"Your name\", \"required\": true, \"autocomplete\": \"name\"},"
	"{\"name\": \"phone\", \"label\": \"Phone\", \"type\": \"tel\", \"required\": true, \"autocomplete\": \"tel\"},"
	"{\"name\": \"contact_by\", \"label\": \"Contact me by\", \"type\": \"radio\", \"options\": [\"Phone\", \"Email\"]},"
	"{\"name\": \"job\", \"label\": \"What needs doing?\", \"type\": \"textarea\", \"maxlength\": 2000}]}";
static const gchar supplied_reviews[] =
	"[{\"quote\": \"They came the same evening and found the leak behind the dishwasher.\", \"name\": \"Dana R.\", \"source\": \"Google\"},"
	" {\"quote\": \"Clear price before they started, and the basement was cleaner than before.\", \"name\": \"Marcus T.\", \"source\": \"Google\"},"
	" {\"quote\": \"Our water heater was replaced in a morning. Friendly and quick.\", \"name\": \"Priya S.\", \"source\": \"Facebook\"}]";
static const gchar contact_form_page[] =
	"{\"path\": \"contact.html\", \"sections\": [{\"component\": \"page-header\", \"variant\": \"plain\","
	" \"content\": {\"heading\": \"Contact\"}}, {\"component\": \"form\", \"variant\": \"stacked\", \"id\": \"quote\","
	" \"content\": {\"heading\": \"Ask for a quote\", \"mode\": \"endpoint\", \"fields\": [{\"name\": \"name\","
	" \"label\": \"Your name\"}], \"submit\": \"Send\"}}, {\"component\": \"faq\", \"variant\": \"list\","
	" \"content\": {\"items\": [{\"question\": \"One?\", \"answer\": \"Yes.\"}, {\"question\": \"Two?\","
	" \"answer\": \"Yes.\"}, {\"question\": \"Three?\", \"answer\": \"Yes.\"}]}}]}";

/* Composes the rich order on @preset with @type set to @variant (a home
 * section, or the header or footer layout; NULL for the picker's own),
 * with the page specifications of @extra (a JSON object by route, or NULL)
 * in place of the default answers, each page through the pages step's own
 * fixes; returns the bundle and, in @plan_out, the plan. */
static JsonNode *
compose_site(LsComponents *components, const gchar *preset, const gchar *type, const gchar *variant,
	const gchar *extra, JsonNode **plan_out){
	g_autoptr(GError) error = NULL;
	g_autofree gchar *plan_text = NULL, *bundle = NULL;
	g_autoptr(JsonNode) plan = NULL, pages = NULL;
	g_autoptr(JsonNode) more = extra != NULL ? parse(extra) : gallery_pages != NULL ? parse(gallery_pages) : NULL;
	g_autoptr(JsonNode) site = parse(rich);
	g_autoptr(JsonNode) photos = parse(gallery_photos != NULL ? gallery_photos : eight_photos);
	g_autoptr(JsonObject) design = NULL;
	/* Layouts built on a supplied form, supplied reviews or the owner's
	 * own photos get them. */
	gboolean form = gallery_pages != NULL || (g_strcmp0(type, "overlay") == 0 && g_strcmp0(variant, "modal-form") == 0) ||
		(g_strcmp0(type, "hero") == 0 && g_strcmp0(variant, "form") == 0) ||
		(g_strcmp0(type, "footer") == 0 && g_strcmp0(variant, "split-form") == 0);
	gboolean reviews = gallery_pages != NULL || g_strcmp0(type, "reviews") == 0;
	gboolean own = g_strcmp0(type, "overlay") == 0 && g_strcmp0(variant, "lightbox") == 0;
	guint i;
	if (compose_scheme != NULL)
		json_object_set_string_member(json_node_get_object(site), "scheme", compose_scheme);
	g_assert_true(g_file_get_contents("tests/fixtures/composed/plan.json", &plan_text, NULL, NULL));
	plan = parse(plan_text);
	pages = parse("{}");
	/* Gallery sites are written out for people to look at: the fixture's
	 * logo file is not among them, so they draw none, as the orders do. */
	if (gallery_photos != NULL)
		json_object_remove_member(json_object_get_object_member(json_node_get_object(plan), "brand"), "mark");
	if (gallery_pages != NULL) {
		json_object_set_string_member(json_node_get_object(plan), "name", "Hartwell Plumbing");
		json_object_set_string_member(json_node_get_object(plan), "tagline", "Family plumbing in Fairport since 1998");
		json_object_set_string_member(json_object_get_object_member(json_node_get_object(plan), "brand"), "wordmark",
			"Hartwell Plumbing");
	}
	if (form) {
		json_object_set_member(json_node_get_object(site), "form", parse(supplied_form));
		json_object_set_member(json_node_get_object(plan), "form", parse(supplied_form));
	}
	if (reviews) {
		json_object_set_member(json_node_get_object(site), "reviews", parse(supplied_reviews));
		json_object_set_member(json_node_get_object(plan), "reviews", parse(supplied_reviews));
	}
	for (i = 0; own && i < json_array_get_length(json_node_get_array(photos)); i++)
		json_object_remove_member(json_array_get_object_element(json_node_get_array(photos), i), "stock");
	json_object_set_member(json_node_get_object(pages), "index.html", parse(rich_home));
	json_object_set_member(json_node_get_object(plan), "photos", json_node_copy(photos));
	json_object_set_member(json_node_get_object(plan), "contact", parse("{\"phone\": \"585-555-0141\","
		" \"email\": \"office@hartwell.example\"}"));
	json_object_set_member(json_node_get_object(plan), "booking", parse("{\"href\": \"/book\"}"));
	design = ls_design_new(block(components), themes_of(components), json_node_get_object(site), preset,
		json_node_get_array(photos), &error);
	g_assert_no_error(error);
	/* A trust strip asked for under an opening that carries the facts in
	 * its own bar: the opening steps back to a plain photo and the strip
	 * returns under it. */
	if (g_strcmp0(type, "facts") == 0 && variant_of(design, "facts") == NULL) {
		JsonArray *home = json_object_get_array_member(design, "home");
		JsonObject *strip = json_object_new();
		if (g_strcmp0(variant_of(design, "hero"), "stats-bar") == 0)
			json_object_set_string_member(json_array_get_object_element(home, 0), "variant", "photo-start");
		json_object_set_string_member(strip, "component", "facts");
		json_object_set_string_member(strip, "variant", "strip");
		json_object_set_string_member(strip, "tone", "default");
		json_array_add_object_element(home, strip);
		{
			/* After the opening. */
			guint k = json_array_get_length(home) - 1;
			JsonNode *moved = json_node_copy(json_array_get_element(home, k));
			JsonArray *rebuilt = json_array_new();
			guint m;
			for (m = 0; m < k; m++) {
				json_array_add_element(rebuilt, json_node_copy(json_array_get_element(home, m)));
				if (m == 0)
					json_array_add_element(rebuilt, json_node_copy(moved));
			}
			json_node_unref(moved);
			json_object_set_array_member(design, "home", rebuilt);
		}
	}
	/* A gallery page shows its one layout on the plainest of the rest: the
	 * stacked skeleton, calm motion and no other overlay. */
	if (gallery_pages != NULL) {
		json_object_set_string_member(design, "skeleton", "stack");
		json_object_set_string_member(design, "motion", "calm");
		json_object_set_array_member(design, "overlays", json_array_new());
	}
	if (type != NULL && !ls_design_set_layout(block(components), design, type, variant, &error))
		g_error("%s/%s: %s", type, variant, error->message);
	g_assert_true(ls_components_apply_theme(components, plan, preset, &error));
	g_assert_no_error(error);
	g_assert_true(ls_components_apply_design(components, plan, design));
	ls_components_apply_default(components, plan, TRUE);
	if (!ls_components_check_plan(components, plan, &error))
		g_error("%s/%s: %s", type, variant, error->message);
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
			if (more != NULL && json_object_has_member(json_node_get_object(more), route))
				json_object_set_member(json_node_get_object(pages), route,
					json_node_copy(json_object_get_member(json_node_get_object(more), route)));
			else if (form && g_str_equal(route, "contact.html"))
				json_object_set_member(json_node_get_object(pages), route, parse(contact_form_page));
			else if (!g_str_equal(route, "index.html"))
				json_object_set_member(json_node_get_object(pages), route, parse(text));
		}
	}
	/* Every page as the pages step accepts it: the home is the design's
	 * list, each photo shows once, and each page of a photo site shows one. */
	{
		JsonArray *planned = json_object_get_array_member(json_node_get_object(plan), "pages");
		for (i = 0; i < json_array_get_length(planned); i++) {
			const gchar *route = ls_string(json_array_get_object_element(planned, i), "path");
			JsonNode *page = json_object_get_member(json_node_get_object(pages), route);
			if (g_str_equal(route, "index.html")) {
				ls_components_apply_opening(components, plan, route, page);
				ls_components_design_home(components, plan, page);
			}
			ls_components_spread_photos(components, plan, page);
			ls_components_ensure_photo(components, plan, route, page);
			if (!ls_components_check_page(components, plan, route, page, &error))
				g_error("%s/%s %s: %s", type, variant, route, error->message);
		}
	}
	bundle = ls_components_compose(components, plan, json_node_get_object(pages), NULL, &error);
	if (bundle == NULL)
		g_error("%s/%s: %s", type, variant, error->message);
	if (plan_out != NULL)
		*plan_out = g_steal_pointer(&plan);
	return parse(bundle);
}

/* The file at @path in a composed bundle. */
static const gchar *
file_in(JsonNode *bundle, const gchar *path){
	JsonArray *files = json_object_get_array_member(json_node_get_object(bundle), "files");
	guint i;
	for (i = 0; i < json_array_get_length(files); i++)
		if (g_str_equal(ls_string(json_array_get_object_element(files, i), "path"), path))
			return ls_string(json_array_get_object_element(files, i), "content");
	return NULL;
}

/* Composes the rich order on @preset with @type set to @variant (a home
 * section, or the header or footer layout); returns the home page and,
 * in @css, the stylesheet. */
static gchar *
compose_with(LsComponents *components, const gchar *preset, const gchar *type, const gchar *variant, gchar **css){
	g_autoptr(JsonNode) composed = compose_site(components, preset, type, variant, NULL, NULL);
	JsonArray *files;
	g_autofree gchar *home = NULL;
	guint i;
	files = json_object_get_array_member(json_node_get_object(composed), "files");
	for (i = 0; i < json_array_get_length(files); i++) {
		JsonObject *file = json_array_get_object_element(files, i);
		if (g_str_equal(ls_string(file, "path"), "styles.css"))
			*css = g_strdup(ls_string(file, "content"));
		if (g_str_equal(ls_string(file, "path"), "index.html"))
			home = g_strdup(ls_string(file, "content"));
	}
	g_assert_nonnull(*css);
	g_assert_nonnull(home);
	return g_steal_pointer(&home);
}

/* The section of @html whose class names @klass, up to its end. */
static gchar *
section_of(const gchar *html, const gchar *klass){
	g_autofree gchar *needle = g_strdup_printf(" %s", klass);
	const gchar *start = strstr(html, needle), *open, *end;
	if (start == NULL)
		return NULL;
	open = g_strrstr_len(html, start - html, "<section");
	end = strstr(start, "</section>");
	if (open == NULL || end == NULL)
		return NULL;
	return g_strndup(open, (gsize)(end - open));
}

/* Every variant this slice adds renders on a preset site: its class and
 * its styles are there, it shows the supplied facts it is for, and the
 * design still keeps the clash rules. */
static void
test_new_variants(void){
	g_autoptr(LsComponents) components = library();
	const gchar *added[][3] = {
		{"hero", "split-frame", NULL}, {"hero", "text-first", NULL}, {"hero", "photo-pair", "<img"},
		{"services", "round-photos", "<img"}, {"services", "feature-rows", "<img"},
		{"facts", "band", "1998"}, {"facts", "logos", "WXXI News"}, {"facts", "rating", "212"},
		{"steps", "three-steps", "We give a price first."}, {"about", "owner-letter", "<img"},
		{"areas", "location-blocks", "214 Main Street"}, {"areas", "shop-chooser", "Webster shop"},
		{"faq", "two-column", "Q10?"}, {"cta", "photo-band", "<img"},
		{"header", "split", NULL}, {"header", "over-photo", NULL}, {"footer", "centered", "Mon-Fri 7am-6pm"}};
	guint i;
	for (i = 0; i < G_N_ELEMENTS(added); i++) {
		g_autofree gchar *css = NULL;
		g_autofree gchar *home = compose_with(components, "evergreen", added[i][0], added[i][1], &css);
		g_autofree gchar *klass = g_strdup_printf("%s--%s", added[i][0], added[i][1]);
		g_autofree gchar *rule = g_strdup_printf(".%s", klass);
		gboolean layout = g_str_equal(added[i][0], "header") || g_str_equal(added[i][0], "footer");
		if (strstr(home, klass) == NULL)
			g_error("%s is not on the home page:\n%s", klass, strstr(home, "<main"));
		if (strstr(css, rule) == NULL)
			g_error("%s has no styles", klass);
		if (layout && added[i][2] != NULL) {
			const gchar *footer = strstr(home, "<footer");
			g_assert_nonnull(footer);
			if (strstr(footer, added[i][2]) == NULL)
				g_error("%s does not show %s", klass, added[i][2]);
		} else if (!layout) {
			g_autofree gchar *section = section_of(home, klass);
			if (section == NULL)
				g_error("%s renders no section", klass);
			if (added[i][2] != NULL && strstr(section, added[i][2]) == NULL)
				g_error("%s does not show %s:\n%s", klass, added[i][2], section);
		}
		/* No gimmicks in the design layer and its variants. */
		{
			const gchar *layer = strstr(css, "/* design */");
			g_assert_nonnull(layer);
			g_assert_null(strstr(layer, "rotate("));
			g_assert_null(strstr(layer, "-webkit-text-stroke"));
			g_assert_null(strstr(layer, "texture"));
		}
	}
	/* The pair shows two photos side by side; the band shows the services. */
	{
		g_autofree gchar *css = NULL;
		g_autofree gchar *home = compose_with(components, "evergreen", "hero", "photo-pair", &css);
		g_autofree gchar *hero = section_of(home, "hero--photo-pair");
		const gchar *second = strstr(hero, "<img");
		g_assert_nonnull(second);
		g_assert_nonnull(strstr(second + 4, "<img"));
	}
	{
		g_autofree gchar *css = NULL;
		g_autofree gchar *home = compose_with(components, "evergreen", "facts", "band", &css);
		g_autofree gchar *band = section_of(home, "facts--band");
		g_assert_nonnull(strstr(band, "Drain cleaning"));
		g_assert_nonnull(strstr(band, "tone-brand"));
	}
	/* The trust strip names the town of the address, not its suite. */
	{
		g_autofree gchar *css = NULL;
		g_autofree gchar *home = compose_with(components, "evergreen", "facts", "strip", &css);
		g_autofree gchar *strip = section_of(home, "facts--strip");
		g_assert_nonnull(strip);
		g_assert_nonnull(strstr(strip, "Fairport"));
		g_assert_null(strstr(strip, "Suite 2"));
	}
	/* The photo pair leaves room on the first screen for a long headline
	 * without the page script (slice 4's roofer: four lines of capitals). */
	{
		g_autofree gchar *pair = NULL;
		g_assert_true(g_file_get_contents("data/components/hero--photo-pair.css", &pair, NULL, NULL));
		g_assert_nonnull(strstr(pair, "100svh - 30rem"));
		/* On wide screens the words run under both photos, so a long
		 * headline takes fewer lines. */
		g_assert_nonnull(strstr(pair, ".hero--photo-pair .hero__title { max-width: 30ch; }"));
	}
	/* The pill header keeps the name and the menu on one row on a phone,
	 * and rounds fully only on wider screens (slice 4's dentist). */
	{
		g_autofree gchar *kit = NULL;
		g_assert_true(g_file_get_contents("data/components/design.css", &kit, NULL, NULL));
		g_assert_nonnull(strstr(kit, ".design.header--pill .site-header__inner:not(:has(.site-nav.is-open)) { flex-wrap: nowrap; }"));
		g_assert_null(strstr(kit, "border-radius: 999px; background: var(--bg); box-shadow: var(--shadow-3), 0 0 0 1px var(--line);"));
	}
	/* The over-photo header needs a full-bleed opening. */
	{
		g_autoptr(JsonObject) design = design_for(components, rich, "lindholm", eight_photos);
		g_autoptr(GError) error = NULL;
		g_assert_true(ls_design_set_layout(block(components), design, "hero", "split-card", &error));
		g_assert_false(ls_design_set_layout(block(components), design, "header", "over-photo", &error));
		g_assert_error(error, LS_ERROR, LS_ERROR_INVALID);
	}
}

/* The facts the new sections show come from the order only: press names,
 * the rating and the shops, as supplied, and nothing without them. */
static void
test_supplied_facts(void){
	g_autoptr(JsonNode) site = parse(rich);
	g_autoptr(JsonObject) facts = ls_design_facts(json_node_get_object(site));
	g_autoptr(JsonNode) bare = parse(salon);
	g_autoptr(JsonObject) none = ls_design_facts(json_node_get_object(bare));
	g_autoptr(LsComponents) components = library();
	g_autoptr(JsonObject) thin = design_for(components, salon, "evergreen", four_photos);
	g_assert_cmpuint(json_array_get_length(json_object_get_array_member(facts, "press")), ==, 2);
	g_assert_cmpint(json_object_get_int_member(json_object_get_object_member(facts, "rating"), "count"), ==, 212);
	g_assert_cmpuint(json_array_get_length(json_object_get_array_member(facts, "locations")), ==, 2);
	g_assert_false(json_object_has_member(none, "press"));
	g_assert_false(json_object_has_member(none, "rating"));
	g_assert_false(json_object_has_member(none, "locations"));
	/* Without them no trust strip, area or band shows on the home. */
	g_assert_null(variant_of(thin, "facts"));
	g_assert_null(variant_of(thin, "areas"));
	/* Recipes refuse malformed press names, ratings and shops. */
	{
		const gchar *bad[] = {
			"press: []", "press: ['']", "press: [1]",
			"rating: {score: 6, count: 3, source: Google}", "rating: {score: 4.5, source: Google}",
			"rating: {score: 4.5, count: 0, source: Google}", "rating: {score: 4.5, count: 3}",
			"rating: {score: 4.5, count: 3, source: Google, stars: 5}",
			"locations: []", "locations: [{name: A}]", "locations: [{address: '1 Main St'}]",
			"locations: [{name: A, address: '1 Main St', fax: 1}]"};
		guint i;
		for (i = 0; i < G_N_ELEMENTS(bad); i++) {
			g_autofree gchar *yaml = g_strdup_printf("version: 1\nname: test\nsite: {brief: hi, %s}\n"
				"pipeline: {steps: [{id: a, type: frontend}]}", bad[i]);
			g_autoptr(GError) error = NULL;
			g_autoptr(LsRecipe) recipe = ls_recipe_new(yaml, &error);
			if (recipe != NULL)
				g_error("accepted %s", bad[i]);
			g_assert_error(error, LS_ERROR, LS_ERROR_INVALID);
		}
	}
	{
		g_autoptr(GError) error = NULL;
		g_autoptr(LsRecipe) recipe = ls_recipe_new("version: 1\nname: test\nsite: {brief: hi, press: [WXXI News],"
			" rating: {score: 4.9, count: 212, source: Google},"
			" locations: [{name: Fairport shop, address: '214 Main Street', hours: Mon-Fri 7am-6pm, phone: 585-555-0141}]}\n"
			"pipeline: {steps: [{id: a, type: frontend}]}", &error);
		g_assert_no_error(error);
		g_assert_nonnull(recipe);
	}
}

/* A writer may give services photos in a layout that draws none (slice 3's
 * roofer): the page still shows a supplied photo, and the composed site
 * passes the taste rules instead of failing the build. */
static void
test_photo_every_page(void){
	g_autoptr(LsComponents) components = library();
	g_autoptr(JsonNode) plan = NULL, bundle = NULL;
	g_autoptr(GString) problems = g_string_new(NULL);
	const gchar *services;
	bundle = compose_site(components, "evergreen", NULL, NULL,
		"{\"services.html\": {\"path\": \"services.html\", \"sections\": ["
		"{\"component\": \"page-header\", \"variant\": \"plain\", \"content\": {\"heading\": \"Services\"}},"
		"{\"component\": \"services\", \"variant\": \"rows\", \"content\": {\"heading\": \"What we fix\","
		" \"items\": [{\"name\": \"Emergency repairs\", \"summary\": \"Burst pipes and leaks.\", \"art\": {\"src\": \"assets/a.jpg\", \"alt\": \"A job photo.\"}},"
		" {\"name\": \"Drain cleaning\", \"summary\": \"Kitchen and main lines.\", \"art\": {\"src\": \"assets/b.jpg\", \"alt\": \"A job photo.\"}},"
		" {\"name\": \"Water heaters\", \"summary\": \"Tank and tankless.\", \"art\": {\"src\": \"assets/c.jpg\", \"alt\": \"A job photo.\"}}]}},"
		"{\"component\": \"faq\", \"variant\": \"list\", \"content\": {\"items\": [{\"question\": \"One?\","
		" \"answer\": \"Yes.\"}, {\"question\": \"Two?\", \"answer\": \"Yes.\"}, {\"question\": \"Three?\","
		" \"answer\": \"Yes.\"}]}}]}}", &plan);
	services = file_in(bundle, "services.html");
	g_assert_nonnull(services);
	if (strstr(services, "src=\"assets/") == NULL)
		g_error("services.html shows no supplied photo:\n%s", strstr(services, "<main"));
	if (!ls_components_check_taste(components, plan, bundle, problems))
		g_error("taste: %s", problems->str);
}

/* ---- Slice 4: skeletons, headers, heroes, footers, overlays, motion ---- */

/* The layouts of @type in the library block: "header", "hero" and "footer"
 * are variants; "skeletons", "overlays" and "motion" are blocks of their own. */
static guint
count_of(LsComponents *components, const gchar *type){
	JsonObject *library_block = block(components);
	guint count = 0;
	if (json_object_has_member(library_block, type) && JSON_NODE_HOLDS_OBJECT(json_object_get_member(library_block, type))) {
		JsonObject *entries = json_object_get_object_member(library_block, type);
		g_autoptr(GList) names = json_object_get_members(entries);
		GList *name;
		for (name = names; name != NULL; name = name->next) {
			JsonObject *entry = json_object_get_object_member(entries, name->data);
			if (ls_string(entry, "source") == NULL || strlen(ls_string(entry, "source")) < 20 ||
				strstr(ls_string(entry, "source"), "TODO") != NULL)
				g_error("%s %s names no source", type, (const gchar *)name->data);
			count++;
		}
	} else {
		JsonObject *variants = json_object_get_object_member(library_block, "variants");
		g_autoptr(GList) names = json_object_get_members(variants);
		g_autofree gchar *prefix = g_strconcat(type, "/", NULL);
		GList *name;
		for (name = names; name != NULL; name = name->next) {
			const gchar *source = ls_string(json_object_get_object_member(variants, name->data), "source");
			if (!g_str_has_prefix(name->data, prefix))
				continue;
			if (strstr(source, "TODO") != NULL)
				g_error("%s names no source", (const gchar *)name->data);
			count++;
		}
	}
	return count;
}

static gboolean
file_exists_in_library(const gchar *name){
	g_autofree gchar *path = g_build_filename("data/components", name, NULL);
	return g_file_test(path, G_FILE_TEST_IS_REGULAR);
}

/* The library has at least eight headers, ten heroes, six page skeletons,
 * six footers, six overlay or placement components and the three motion
 * levels; each names its source and brings its styles. */
static void
test_layout_library(void){
	g_autoptr(LsComponents) components = library();
	JsonObject *library_block = block(components);
	const gchar *levels[] = {"none", "calm", "lively"};
	guint i;
	g_assert_cmpuint(count_of(components, "header"), >=, 8);
	g_assert_cmpuint(count_of(components, "hero"), >=, 10);
	g_assert_cmpuint(count_of(components, "footer"), >=, 6);
	g_assert_cmpuint(count_of(components, "skeletons"), >=, 6);
	g_assert_cmpuint(count_of(components, "overlays"), >=, 6);
	g_assert_cmpuint(count_of(components, "motion"), ==, 3);
	for (i = 0; i < G_N_ELEMENTS(levels); i++)
		g_assert_true(json_object_has_member(json_object_get_object_member(library_block, "motion"), levels[i]));
	{
		JsonObject *skeletons = json_object_get_object_member(library_block, "skeletons");
		g_autoptr(GList) names = json_object_get_members(skeletons);
		GList *name;
		for (name = names; name != NULL; name = name->next) {
			g_autofree gchar *css = g_strdup_printf("skeleton--%s.css", (const gchar *)name->data);
			if (!file_exists_in_library(css))
				g_error("skeleton %s has no %s", (const gchar *)name->data, css);
		}
	}
	{
		JsonObject *overlays = json_object_get_object_member(library_block, "overlays");
		g_autoptr(GList) names = json_object_get_members(overlays);
		GList *name;
		for (name = names; name != NULL; name = name->next) {
			JsonObject *entry = json_object_get_object_member(overlays, name->data);
			const gchar *pair = ls_string(entry, "variant");
			g_autofree gchar *css = g_strdup_printf("overlay--%s.css", (const gchar *)name->data);
			/* A placement component is a section variant; an overlay has
			 * its own styles. */
			if (pair != NULL)
				g_assert_nonnull(json_object_get_object_member(json_object_get_object_member(library_block, "variants"), pair));
			else if (!file_exists_in_library(css))
				g_error("overlay %s has no %s", (const gchar *)name->data, css);
		}
	}
	g_assert_true(file_exists_in_library("motion.js"));
	g_assert_true(file_exists_in_library("motion.css"));
}

/* Every design names its skeleton, motion level and overlays; the owner's
 * words choose them first; ls_design_check refuses unknown ones. */
static void
test_layout_picks(void){
	g_autoptr(LsComponents) components = library();
	g_autoptr(JsonObject) design = design_for(components, rich, "evergreen", eight_photos);
	g_autoptr(GString) why = g_string_new(NULL);
	const gchar *cases[][3] = {
		{"playful, bold and lively", "motion", "lively"}, {"calm and warm", "motion", "calm"},
		{"serious, no-nonsense", "motion", "none"}, {"editorial and classic", "skeleton", "magazine"},
		{"minimal, simple, elegant", "skeleton", "column"}};
	guint i;
	g_assert_nonnull(ls_string(design, "skeleton"));
	g_assert_nonnull(ls_string(design, "motion"));
	g_assert_nonnull(json_object_get_array_member(design, "overlays"));
	g_assert_true(ls_design_check(block(components), design, why));
	for (i = 0; i < G_N_ELEMENTS(cases); i++) {
		g_autofree gchar *site = g_strdup_printf("{\"brand\": {\"name\": \"Hartwell Plumbing\", \"tagline\": \"Plumbing\"},"
			" \"contact\": {\"phone\": \"585-555-0141\"}, \"offers\": [{\"id\": \"a\", \"name\": \"One\"},"
			" {\"id\": \"b\", \"name\": \"Two\"}], \"style\": \"%s\"}", cases[i][0]);
		g_autoptr(JsonObject) worded = design_for(components, site, "evergreen", four_photos);
		if (g_strcmp0(ls_string(worded, cases[i][1]), cases[i][2]) != 0)
			g_error("\"%s\" gave %s %s, not %s", cases[i][0], cases[i][1], ls_string(worded, cases[i][1]), cases[i][2]);
	}
	json_object_set_string_member(design, "skeleton", "spiral");
	g_assert_false(ls_design_check(block(components), design, why));
	g_assert_nonnull(strstr(why->str, "skeleton"));
	json_object_set_string_member(design, "skeleton", "stack");
	json_object_set_string_member(design, "motion", "frantic");
	g_string_truncate(why, 0);
	g_assert_false(ls_design_check(block(components), design, why));
	g_assert_nonnull(strstr(why->str, "motion"));
}

/* An avoid entry as the builder's fingerprint writes it, with the
 * skeleton and header too. */
static gchar *
fingerprint_of(JsonObject *design){
	return g_strdup_printf("[{\"v\": 1, \"preset\": \"%s\", \"skeleton\": \"%s\", \"header\": \"%s\","
		" \"home\": [\"hero/%s\"]}]", ls_string(design, "preset"), ls_string(design, "skeleton"),
		ls_string(design, "header"), variant_of(design, "hero"));
}

/* Two businesses of one trade in one town differ in skeleton, header and
 * hero, not only in preset; the same order beside itself too, even when
 * the owner's words pick one skeleton. The fingerprint records them. */
static void
test_layout_neighbours(void){
	g_autoptr(LsComponents) components = library();
	const gchar *orders[] = {plumber, rich, salon};
	guint i;
	for (i = 0; i < G_N_ELEMENTS(orders); i++) {
		g_autoptr(JsonObject) first = design_for(components, orders[i], NULL, eight_photos);
		g_autofree gchar *avoid = fingerprint_of(first);
		g_autoptr(JsonNode) site = parse(orders[i]);
		g_autoptr(JsonNode) photos = parse(eight_photos);
		g_autoptr(GError) error = NULL;
		g_autofree gchar *picked = NULL;
		g_autoptr(JsonObject) second = NULL;
		json_object_set_member(json_node_get_object(site), "avoid", parse(avoid));
		picked = ls_design_pick_preset(block(components), themes_of(components), json_node_get_object(site));
		second = ls_design_new(block(components), themes_of(components), json_node_get_object(site), picked,
			json_node_get_array(photos), &error);
		g_assert_no_error(error);
		if (g_strcmp0(ls_string(first, "skeleton"), ls_string(second, "skeleton")) == 0 ||
			g_strcmp0(ls_string(first, "header"), ls_string(second, "header")) == 0 ||
			g_strcmp0(variant_of(first, "hero"), variant_of(second, "hero")) == 0)
			g_error("order %u and its neighbour share skeleton %s/%s, header %s/%s or hero %s/%s", i,
				ls_string(first, "skeleton"), ls_string(second, "skeleton"), ls_string(first, "header"),
				ls_string(second, "header"), variant_of(first, "hero"), variant_of(second, "hero"));
	}
	/* The fingerprint carries the skeleton and the header. */
	{
		g_autoptr(JsonNode) plan = NULL;
		g_autoptr(JsonNode) bundle = compose_site(components, "evergreen", NULL, NULL, NULL, &plan);
		g_autoptr(JsonObject) fp = ls_components_fingerprint(components, plan, NULL);
		JsonObject *design = json_object_get_object_member(json_node_get_object(plan), "design");
		g_assert_nonnull(bundle);
		g_assert_cmpstr(ls_string(fp, "skeleton"), ==, ls_string(design, "skeleton"));
		g_assert_cmpstr(ls_string(fp, "header"), ==, ls_string(design, "header"));
	}
	/* A recipe takes the fingerprint back as site.avoid. */
	{
		g_autoptr(GError) error = NULL;
		g_autoptr(LsRecipe) recipe = ls_recipe_new("version: 1\nname: test\nsite: {brief: hi, avoid: [{v: 1, preset: evergreen,"
			" skeleton: bento, header: drawer, footer: mega, motion: calm, home: [hero/fullscreen]}]}\n"
			"pipeline: {steps: [{id: a, type: frontend}]}", &error);
		g_assert_no_error(error);
		g_assert_nonnull(recipe);
	}
}

/* Businesses of each trade spread over the new layouts. */
static void
test_layout_spread(void){
	g_autoptr(LsComponents) components = library();
	const gchar *trades[] = {"Plumbing", "Dental", "Law", "Bakery", "Salon"};
	const gchar *kinds[] = {"skeleton", "header", "hero", "footer", "motion"};
	const guint least[] = {5, 6, 7, 5, 2};
	guint t, k;
	for (t = 0; t < G_N_ELEMENTS(trades); t++) {
		GHashTable *seen[G_N_ELEMENTS(kinds)];
		guint i;
		for (k = 0; k < G_N_ELEMENTS(kinds); k++)
			seen[k] = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
		for (i = 0; i < 80; i++) {
			g_autofree gchar *site = g_strdup_printf("{\"brand\": {\"name\": \"Example %u %s\", \"tagline\":"
				" \"%s since 1998\"}, \"contact\": {\"phone\": \"585-555-0141\", \"address\": \"1 Main Street, Fairport, NY 14450\"},"
				" \"offers\": [{\"id\": \"a\", \"name\": \"One\"}, {\"id\": \"b\", \"name\": \"Two\"}, {\"id\": \"c\", \"name\": \"Three\"}],"
				" \"requirements\": \"Hours: Mon-Fri 9am-5pm. Service area: Fairport and Webster.\"}", i, trades[t], trades[t]);
			g_autoptr(JsonObject) design = design_for(components, site, NULL, four_photos);
			g_autoptr(GString) broken = g_string_new(NULL);
			for (k = 0; k < G_N_ELEMENTS(kinds); k++)
			{
				const gchar *value = g_str_equal(kinds[k], "hero") ? variant_of(design, "hero") : ls_string(design, kinds[k]);
				g_hash_table_add(seen[k], g_strdup(value != NULL ? value : "(none)"));
			}
			if (!ls_design_check(block(components), design, broken))
				g_error("%s: %s", site, broken->str);
		}
		for (k = 0; k < G_N_ELEMENTS(kinds); k++)
			if (g_hash_table_size(seen[k]) < least[k])
				g_error("%s: %u %ss over 80 businesses, need %u", trades[t], g_hash_table_size(seen[k]), kinds[k], least[k]);
		for (k = 0; k < G_N_ELEMENTS(kinds); k++)
			g_hash_table_unref(seen[k]);
	}
}

/* A floating action bar never doubles a header that already keeps the
 * action in view: the rail, the pill, the shrinking bar and the bar that
 * turns solid on scroll (Ben, 2026-11-04: the salon showed "Book online"
 * twice and the bar covered the questions). */
static void
test_sticky_bar_headers(void){
	g_autoptr(LsComponents) components = library();
	const gchar *held[] = {"rail", "pill", "shrink", "transparent-solid"};
	guint i, j, bars = 0;
	for (i = 0; i < 400; i++) {
		g_autofree gchar *site = g_strdup_printf("{\"brand\": {\"name\": \"Example %u Salon\", \"tagline\": \"Salon\"},"
			" \"contact\": {\"phone\": \"503-555-0122\"}, \"offers\": [{\"id\": \"a\", \"name\": \"One\"},"
			" {\"id\": \"b\", \"name\": \"Two\"}]}", i);
		g_autoptr(JsonObject) design = design_for(components, site, NULL, four_photos);
		JsonArray *overlays = json_object_get_array_member(design, "overlays");
		gboolean bar = FALSE;
		for (j = 0; j < json_array_get_length(overlays); j++)
			bar = bar || g_strcmp0(json_array_get_string_element(overlays, j), "sticky-bar") == 0;
		if (!bar)
			continue;
		bars++;
		for (j = 0; j < G_N_ELEMENTS(held); j++)
			if (g_strcmp0(ls_string(design, "header"), held[j]) == 0)
				g_error("%s: sticky-bar beside the %s header", site, held[j]);
	}
	g_assert_cmpuint(bars, >, 20);
}

/* Every new header, hero, footer, skeleton, overlay and motion level
 * renders on a preset site with its styles; overlays keep their
 * keyboard contracts; motion never hides content without its script and
 * stops for reduced motion. */
static void
test_layout_render(void){
	g_autoptr(LsComponents) components = library();
	const gchar *added[][3] = {
		{"header", "centered-split", "header--centered-split"}, {"header", "transparent-solid", "header--transparent-solid"},
		{"header", "shrink", "header--shrink"}, {"header", "drawer", "header--drawer"}, {"header", "overlay", "header--overlay"},
		{"header", "pill", "header--pill"}, {"header", "rail", "header--rail"},
		{"hero", "fullscreen", "hero--fullscreen"}, {"hero", "split-screen", "hero--split-screen"},
		{"hero", "offset-card", "hero--offset-card"}, {"hero", "collage", "hero--collage"},
		{"hero", "big-type", "hero--big-type"}, {"hero", "thumb-strip", "hero--thumb-strip"},
		{"hero", "form", "hero--form"}, {"hero", "stats-bar", "hero--stats-bar"},
		{"footer", "big-cta", "footer--big-cta"}, {"footer", "mega", "footer--mega"}, {"footer", "minimal", "footer--minimal"},
		{"footer", "split-form", "footer--split-form"}, {"footer", "wordmark", "footer--wordmark"},
		{"skeleton", "alternating", "skeleton--alternating"}, {"skeleton", "bento", "skeleton--bento"},
		{"skeleton", "sticky-side", "skeleton--sticky-side"}, {"skeleton", "asymmetric", "skeleton--asymmetric"},
		{"skeleton", "overlap", "skeleton--overlap"}, {"skeleton", "magazine", "skeleton--magazine"},
		{"skeleton", "column", "skeleton--column"}, {"skeleton", "stack", "skeleton--stack"},
		{"overlay", "announcement", "class=\"announce"}, {"overlay", "badge", "class=\"hero__badge"},
		{"overlay", "sticky-bar", "overlay--sticky-bar"}, {"overlay", "modal-form", "<dialog class=\"quote-dialog"},
		{"overlay", "lightbox", "data-lightbox"},
		{"areas", "card-over-photo", "areas--card-over-photo"}, {"reviews", "strip", "reviews--strip"},
		{"motion", "calm", "motion--calm"}, {"motion", "lively", "motion--lively"}, {"motion", "none", "motion--none"}};
	guint i;
	for (i = 0; i < G_N_ELEMENTS(added); i++) {
		g_autoptr(JsonNode) plan = NULL;
		g_autoptr(JsonNode) bundle = compose_site(components, "evergreen", added[i][0], added[i][1], NULL, &plan);
		const gchar *home = file_in(bundle, "index.html"), *css = file_in(bundle, "styles.css");
		const gchar *js = file_in(bundle, "site.js");
		g_autoptr(GString) problems = g_string_new(NULL);
		g_autofree gchar *rule = NULL;
		if (strstr(home, added[i][2]) == NULL)
			g_error("%s/%s: no %s on the home page", added[i][0], added[i][1], added[i][2]);
		rule = g_strdup_printf(".%s--%s", added[i][0], added[i][1]);
		if (!g_str_equal(added[i][1], "none") && !g_str_equal(added[i][1], "stack") && strstr(css, rule) == NULL)
			g_error("%s/%s has no styles (%s)", added[i][0], added[i][1], rule);
		if (!ls_components_check_taste(components, plan, bundle, problems))
			g_error("%s/%s: %s", added[i][0], added[i][1], problems->str);
		/* No gimmicks: nothing turns, outlines its text or wears a texture. */
		g_assert_null(strstr(strstr(css, "/* design */"), "rotate("));
		g_assert_null(strstr(strstr(css, "/* design */"), "-webkit-text-stroke"));
		/* Motion: only with the script, only without reduced motion. */
		if (g_str_equal(added[i][0], "motion")) {
			gboolean moving = !g_str_equal(added[i][1], "none");
			/* The motion script's own guard; the form script also asks. */
			g_assert_true((strstr(js, "(prefers-reduced-motion: reduce)\").matches) return;") != NULL) == moving);
			g_assert_true((strstr(js, "is-pending") != NULL) == moving);
		}
	}
	/* The motion styles hide nothing on their own: a hidden state needs the
	 * class the script adds, inside the no-preference query. */
	{
		g_autofree gchar *motion = NULL;
		const gchar *at;
		g_assert_true(g_file_get_contents("data/components/motion.css", &motion, NULL, NULL));
		g_assert_true(g_str_has_prefix(strstr(motion, "@media"), "@media (prefers-reduced-motion: no-preference)"));
		for (at = strstr(motion, "opacity: 0;"); at != NULL; at = strstr(at + 1, "opacity: 0;")) {
			const gchar *rule = g_strrstr_len(motion, at - motion, "}");
			g_autofree gchar *selector = g_strndup(rule != NULL ? rule : motion, (gsize)(at - (rule != NULL ? rule : motion)));
			if (strstr(selector, ".is-pending") == NULL)
				g_error("motion.css hides content without the script's class: %s", selector);
		}
	}
	/* Overlays: the drawer and the dialogs answer Escape, trap focus and
	 * give it back; the lightbox and the quote form are dialogs. */
	{
		g_autoptr(JsonNode) plan = NULL;
		g_autoptr(JsonNode) bundle = compose_site(components, "evergreen", "header", "drawer", NULL, &plan);
		const gchar *js = file_in(bundle, "site.js"), *home = file_in(bundle, "index.html");
		g_assert_nonnull(strstr(js, "\"Escape\""));
		g_assert_nonnull(strstr(js, "inert"));
		g_assert_nonnull(strstr(js, "focus()"));
		g_assert_nonnull(strstr(home, "aria-controls=\"site-nav\""));
	}
	{
		g_autoptr(JsonNode) plan = NULL;
		g_autoptr(JsonNode) bundle = compose_site(components, "evergreen", "overlay", "modal-form", NULL, &plan);
		const gchar *home = file_in(bundle, "index.html");
		const gchar *dialog = strstr(home, "<dialog class=\"quote-dialog");
		g_assert_nonnull(dialog);
		g_assert_nonnull(strstr(dialog, "aria-labelledby="));
		g_assert_nonnull(strstr(dialog, "class=\"dialog-close"));
		/* Without the script the quote link still leads to the form. */
		g_assert_nonnull(strstr(home, "data-dialog=\"quote-dialog\""));
	}
}

/* The component gallery: every header, hero, skeleton, footer, overlay and
 * motion level on a composed site that passes the taste rules. With
 * LS_GALLERY_OUTPUT set, each site is written there as DIR/NN-NAME/. */
static void
test_gallery(void){
	g_autoptr(LsComponents) components = library();
	const gchar *output = g_getenv("LS_GALLERY_OUTPUT");
	const gchar *types[] = {"header", "hero", "footer", "skeletons", "overlays", "motion"};
	guint t, n = 0;
	g_assert_true(g_file_get_contents("tests/fixtures/gallery/pages.json", &gallery_pages, NULL, NULL));
	g_assert_true(g_file_get_contents("tests/fixtures/gallery/photos.json", &gallery_photos, NULL, NULL));
	for (t = 0; t < G_N_ELEMENTS(types); t++) {
		JsonObject *library_block = block(components);
		g_autoptr(GList) names = NULL;
		GList *name;
		const gchar *type = g_str_equal(types[t], "skeletons") ? "skeleton" : g_str_equal(types[t], "overlays") ? "overlay" : types[t];
		if (json_object_has_member(library_block, types[t]))
			names = json_object_get_members(json_object_get_object_member(library_block, types[t]));
		else {
			g_autoptr(GList) all = json_object_get_members(json_object_get_object_member(library_block, "variants"));
			GList *pair;
			g_autofree gchar *prefix = g_strconcat(types[t], "/", NULL);
			for (pair = all; pair != NULL; pair = pair->next)
				if (g_str_has_prefix(pair->data, prefix))
					names = g_list_append(names, (gchar *)pair->data + strlen(prefix));
		}
		for (name = names; name != NULL; name = name->next) {
			g_autoptr(JsonNode) plan = NULL;
			g_autoptr(JsonNode) bundle = NULL;
			g_autoptr(GString) problems = g_string_new(NULL);
			const gchar *placed = g_str_equal(type, "overlay") ? ls_string(json_object_get_object_member(
				json_object_get_object_member(library_block, "overlays"), name->data), "variant") : NULL;
			/* A placement component is a section variant. */
			if (placed != NULL) {
				g_autofree gchar *section = g_strndup(placed, (gsize)(strchr(placed, '/') - placed));
				bundle = compose_site(components, "evergreen", section, strchr(placed, '/') + 1, NULL, &plan);
			} else
				bundle = compose_site(components, "evergreen", type, name->data, NULL, &plan);
			if (!ls_components_check_taste(components, plan, bundle, problems))
				g_error("%s/%s: %s", type, (const gchar *)name->data, problems->str);
			/* The opening shows a supplied photo. */
			{
				const gchar *main = strstr(file_in(bundle, "index.html"), "<main");
				const gchar *end = main != NULL ? strstr(main, "</section>") : NULL;
				g_autofree gchar *opening = end != NULL ? g_strndup(main, (gsize)(end - main)) : NULL;
				if (opening == NULL || strstr(opening, "<img src=\"assets/photos/") == NULL)
					g_error("%s/%s: the opening shows no photo", type, (const gchar *)name->data);
			}
			n++;
			if (output != NULL) {
				JsonArray *files = json_object_get_array_member(json_node_get_object(bundle), "files");
				g_autofree gchar *dir = g_strdup_printf("%s/%02u-%s-%s", output, n, type, (const gchar *)name->data);
				guint f;
				for (f = 0; f < json_array_get_length(files); f++) {
					JsonObject *file = json_array_get_object_element(files, f);
					g_autofree gchar *path = g_build_filename(dir, ls_string(file, "path"), NULL);
					g_autofree gchar *parent = g_path_get_dirname(path);
					if (ls_string(file, "encoding") != NULL)
						continue;
					g_assert_cmpint(g_mkdir_with_parents(parent, 0755), ==, 0);
					g_assert_true(g_file_set_contents(path, ls_string(file, "content"), -1, NULL));
				}
			}
		}
	}
	g_assert_cmpuint(n, >=, 8 + 10 + 6 + 6 + 6 + 3);
	g_clear_pointer(&gallery_pages, g_free);
	g_clear_pointer(&gallery_photos, g_free);
}

/* ---- Light and dark (docs/components.org, "Light and dark") ------------- */

/* The custom properties declared in @css from @start up to @stop (or the
 * end), later declarations replacing earlier ones, added to @into. A
 * var(--x) reference is not a declaration. */
static void
css_vars(GHashTable *into, const gchar *start, const gchar *stop){
	const gchar *end = stop != NULL && strstr(start, stop) != NULL ? strstr(start, stop) : start + strlen(start);
	const gchar *at = start;
	while ((at = strstr(at, "--")) != NULL && at < end) {
		const gchar *colon = strchr(at, ':'), *semi = colon != NULL ? strchr(colon, ';') : NULL;
		if (colon == NULL || semi == NULL || semi > end)
			break;
		if (at > start && strchr("\t\n {;", at[-1]) != NULL && memchr(at, ' ', (gsize)(colon - at)) == NULL) {
			g_autofree gchar *value = g_strndup(colon + 1, (gsize)(semi - colon - 1));
			g_hash_table_insert(into, g_strndup(at + 2, (gsize)(colon - at - 2)), g_strdup(g_strstrip(value)));
		}
		at = semi;
	}
}

/* Text and ground pairs every scheme keeps: {text, ground, minimum}. The
 * line-strong border and the focus ring are controls, at 3:1. */
static const struct { const gchar *text, *ground; gdouble minimum; } scheme_pairs[] = {
	{"ink", "paper", 4.5}, {"muted", "paper", 4.5}, {"ink", "surface", 4.5}, {"muted", "surface", 4.5},
	{"accent-ink", "accent", 4.5}, {"accent-text", "paper", 4.5}, {"accent-text", "surface", 4.5},
	{"danger", "paper", 4.5}, {"fg", "bg", 4.5}, {"muted-fg", "bg", 4.5}, {"surface-fg", "surface", 4.5},
	{"muted-fg", "surface", 4.5}, {"primary-text", "bg", 4.5}, {"primary-text", "surface", 4.5},
	{"primary-fg", "primary", 4.5}, {"primary-fg", "primary-hover", 4.5}, {"brand-fg", "brand", 4.5},
	{"brand-fg", "brand-hover", 4.5}, {"brand-muted", "brand", 4.5}, {"brand-accent", "brand", 4.5},
	{"inverse-fg", "inverse", 4.5}, {"inverse-muted", "inverse", 4.5}, {"photo-primary-fg", "photo-primary", 4.5},
	{"photo-primary-fg", "photo-primary-hover", 4.5}, {"focus", "bg", 3}, {"line-strong", "paper", 3}};

static void
check_scheme_pairs(const gchar *preset, const gchar *scheme, GHashTable *vars){
	guint i;
	for (i = 0; i < G_N_ELEMENTS(scheme_pairs); i++) {
		const gchar *text = g_hash_table_lookup(vars, scheme_pairs[i].text);
		const gchar *ground = g_hash_table_lookup(vars, scheme_pairs[i].ground);
		if (text == NULL || ground == NULL || *text != '#' || *ground != '#')
			g_error("%s %s: --%s or --%s is not a colour (%s, %s)", preset, scheme, scheme_pairs[i].text,
				scheme_pairs[i].ground, text != NULL ? text : "missing", ground != NULL ? ground : "missing");
		if (ls_design_contrast(text, ground) < scheme_pairs[i].minimum)
			g_error("%s %s: --%s %s on --%s %s is %.2f:1, under %.1f:1", preset, scheme, scheme_pairs[i].text, text,
				scheme_pairs[i].ground, ground, ls_design_contrast(text, ground), scheme_pairs[i].minimum);
	}
}

static gdouble
hex_luminance(const gchar *hex){
	guint r, g, b;
	gdouble c[3];
	guint i;
	g_assert_cmpint(sscanf(hex, "#%02x%02x%02x", &r, &g, &b), ==, 3);
	c[0] = r / 255.0; c[1] = g / 255.0; c[2] = b / 255.0;
	for (i = 0; i < 3; i++)
		c[i] = c[i] <= 0.04045 ? c[i] / 12.92 : pow((c[i] + 0.055) / 1.055, 2.4);
	return 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2];
}

/* The difference in hue between two #rrggbb colours, in degrees. */
static gdouble
hue_gap(const gchar *a, const gchar *b){
	const gchar *colors[2];
	gdouble hues[2], gap;
	guint i;
	colors[0] = a;
	colors[1] = b;
	for (i = 0; i < 2; i++) {
		guint r, g, bl;
		gdouble c[3], max, min, h = 0;
		g_assert_cmpint(sscanf(colors[i], "#%02x%02x%02x", &r, &g, &bl), ==, 3);
		c[0] = r / 255.0; c[1] = g / 255.0; c[2] = bl / 255.0;
		max = MAX(c[0], MAX(c[1], c[2]));
		min = MIN(c[0], MIN(c[1], c[2]));
		if (max > min) {
			if (max == c[0])
				h = fmod((c[1] - c[2]) / (max - min), 6);
			else if (max == c[1])
				h = (c[2] - c[0]) / (max - min) + 2;
			else
				h = (c[0] - c[1]) / (max - min) + 4;
		}
		hues[i] = fmod(h * 60 + 360, 360);
	}
	gap = fabs(hues[0] - hues[1]);
	return MIN(gap, 360 - gap);
}

/* Every preset carries its own dark scheme, written by hand in
 * themes.yaml: dark grounds, surfaces lighter than the page for depth,
 * its own accent and status colours, photo scrim and brand bands. Every
 * text pair holds AA in both schemes, under three orders, and the
 * stylesheet a site ships keeps them. */
static void
test_dark_schemes(void){
	g_autoptr(LsComponents) components = library();
	const gchar *tokens[] = {"paper", "ink", "muted", "surface", "line", "accent", "accent_ink"};
	const gchar *roles[] = {"brand", "brand_fg", "inverse", "inverse_fg", "highlight", "photo_primary", "photo_primary_fg"};
	const gchar *pairs[][2] = {{"bg", "fg"}, {"bg", "muted_fg"}, {"surface", "surface_fg"}, {"surface", "muted_fg"},
		{"bg", "primary_text"}, {"surface", "primary_text"}, {"primary", "primary_fg"}, {"brand", "brand_fg"},
		{"brand", "brand_muted"}, {"inverse", "inverse_fg"}, {"inverse", "inverse_muted"},
		{"photo_primary", "photo_primary_fg"}, {"brand", "brand_accent"}, {"surface", "fg"}};
	guint p, i;
	for (p = 0; p < G_N_ELEMENTS(presets); p++) {
		JsonObject *preset = json_object_get_object_member(json_object_get_object_member(themes_of(components),
			presets[p][0]), "design");
		JsonObject *dark = json_object_has_member(preset, "dark") ? json_object_get_object_member(preset, "dark") : NULL;
		JsonObject *dark_tokens, *dark_roles;
		if (dark == NULL)
			g_error("%s has no hand-written dark scheme", presets[p][0]);
		dark_tokens = json_object_get_object_member(dark, "tokens");
		dark_roles = json_object_get_object_member(dark, "roles");
		for (i = 0; i < G_N_ELEMENTS(tokens); i++)
			if (ls_string(dark_tokens, tokens[i]) == NULL || *ls_string(dark_tokens, tokens[i]) != '#')
				g_error("%s: dark.tokens.%s must be #rrggbb", presets[p][0], tokens[i]);
		for (i = 0; i < G_N_ELEMENTS(roles); i++)
			if (ls_string(dark_roles, roles[i]) == NULL || *ls_string(dark_roles, roles[i]) != '#')
				g_error("%s: dark.roles.%s must be #rrggbb", presets[p][0], roles[i]);
		g_assert_cmpfloat(json_object_get_double_member(dark, "scrim"), >=, 0.2);
		g_assert_cmpfloat(json_object_get_double_member(dark, "scrim"), <=, 0.85);
		/* A dark ground, and a surface lifted above it rather than a black
		 * shadow under it. */
		if (hex_luminance(ls_string(dark_tokens, "paper")) > 0.03)
			g_error("%s: dark paper %s is not dark", presets[p][0], ls_string(dark_tokens, "paper"));
		if (hex_luminance(ls_string(dark_tokens, "surface")) <= hex_luminance(ls_string(dark_tokens, "paper")))
			g_error("%s: dark surface %s is not lighter than the paper", presets[p][0], ls_string(dark_tokens, "surface"));
	}
	/* The design the picker builds carries both schemes, each repaired to
	 * AA, under the owner's colour words too. */
	for (p = 0; p < G_N_ELEMENTS(presets); p++) {
		const gchar *orders[] = {plumber, salon, yellow_order};
		guint o;
		for (o = 0; o < G_N_ELEMENTS(orders); o++) {
			g_autoptr(JsonObject) design = design_for(components, orders[o], presets[p][0], four_photos);
			g_autoptr(GString) why = g_string_new(NULL);
			JsonObject *dark = json_object_get_object_member(design, "dark");
			JsonObject *dark_roles = dark != NULL ? json_object_get_object_member(dark, "roles") : NULL;
			g_assert_cmpstr(ls_string(design, "scheme"), ==, "auto");
			g_assert_nonnull(dark_roles);
			for (i = 0; i < G_N_ELEMENTS(pairs); i++)
				if (ls_design_contrast(ls_string(dark_roles, pairs[i][0]), ls_string(dark_roles, pairs[i][1])) < 4.5)
					g_error("%s dark: %s on %s is under 4.5:1", presets[p][0], pairs[i][1], pairs[i][0]);
			if (!ls_design_check(block(components), design, why))
				g_error("%s: %s", presets[p][0], why->str);
			/* The owner's colour paints the same parts in dark as in light:
			 * a band that wears it in light wears it in dark. */
			if (json_array_get_length(json_object_get_array_member(design, "colors")) > 0) {
				JsonObject *light = json_object_get_object_member(design, "roles");
				const gchar *tied[] = {"brand", "inverse", "photo_primary", "primary", "highlight"};
				guint k;
				/* Each ground may be lightened or deepened to keep its own text
				 * at AA, as in light, but keeps the owner's hue. */
				for (k = 0; k < G_N_ELEMENTS(tied); k++) {
					if (g_strcmp0(ls_string(light, tied[k]), ls_string(light, "brand")) != 0)
						continue;
					if (hue_gap(ls_string(dark_roles, tied[k]), ls_string(dark_roles, "brand")) > 20)
						g_error("%s dark: %s wears the owner's colour in light (%s) but %s in dark, where the brand is %s",
							presets[p][0], tied[k], ls_string(light, tied[k]), ls_string(dark_roles, tied[k]),
							ls_string(dark_roles, "brand"));
				}
			}
		}
	}
	/* What a site ships: the light properties, then the dark ones the
	 * visitor's system or the toggle asks for, the same in both places. */
	for (p = 0; p < G_N_ELEMENTS(presets); p++) {
		g_autoptr(JsonNode) plan = NULL;
		g_autoptr(JsonNode) bundle = compose_site(components, presets[p][0], NULL, NULL, NULL, &plan);
		const gchar *css = file_in(bundle, "styles.css");
		const gchar *chosen = strstr(css, ":root[data-scheme=\"dark\"] {");
		const gchar *system = strstr(css, "@media (prefers-color-scheme: dark) {\n:root:not([data-scheme=\"light\"]) {");
		g_autoptr(GHashTable) light = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
		g_autoptr(GHashTable) dark = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
		g_autoptr(GHashTable) followed = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
		GHashTableIter iter;
		gpointer key, value;
		if (chosen == NULL || system == NULL)
			g_error("%s: styles.css has no dark scheme for the toggle and the system setting", presets[p][0]);
		css_vars(light, css, "@layer ls-base {");
		g_hash_table_iter_init(&iter, light);
		while (g_hash_table_iter_next(&iter, &key, &value)) {
			g_hash_table_insert(dark, g_strdup(key), g_strdup(value));
			g_hash_table_insert(followed, g_strdup(key), g_strdup(value));
		}
		css_vars(dark, strchr(chosen, '{'), "}");
		css_vars(followed, strchr(strstr(system, ":root:not"), '{'), "}");
		check_scheme_pairs(presets[p][0], "light", light);
		check_scheme_pairs(presets[p][0], "dark", dark);
		g_hash_table_iter_init(&iter, dark);
		while (g_hash_table_iter_next(&iter, &key, &value))
			if (g_strcmp0(g_hash_table_lookup(followed, key), value) != 0)
				g_error("%s: --%s differs between the toggle's dark and the system's dark", presets[p][0], (gchar *)key);
		/* Depth from lighter surfaces, not black shadows; the photo scrim
		 * is the dark scheme's own. */
		if (strstr(g_hash_table_lookup(dark, "shadow-2"), "16 24 40") != NULL)
			g_error("%s: the dark scheme keeps the light scheme's black shadow", presets[p][0]);
		g_assert_nonnull(g_hash_table_lookup(dark, "scrim"));
	}
}

/* Followed by default, chosen by the visitor, locked by the owner. The
 * head sets a chosen scheme before the stylesheet can paint; without
 * scripts the system setting still applies; the toggle sits in the
 * header and the footer and only shows when its script runs; nothing
 * animates the change. */
static void
test_scheme_page(void){
	g_autoptr(LsComponents) components = library();
	g_autoptr(JsonNode) plan = NULL;
	g_autoptr(JsonNode) bundle = compose_site(components, "evergreen", NULL, NULL, NULL, &plan);
	JsonArray *files = json_object_get_array_member(json_node_get_object(bundle), "files");
	const gchar *css = file_in(bundle, "styles.css"), *js = file_in(bundle, "site.js");
	guint i, pages = 0;
	for (i = 0; i < json_array_get_length(files); i++) {
		JsonObject *file = json_array_get_object_element(files, i);
		const gchar *html = ls_string(file, "content");
		const gchar *head_end, *script, *sheet, *header, *footer;
		if (!g_str_has_suffix(ls_string(file, "path"), ".html"))
			continue;
		pages++;
		head_end = strstr(html, "</head>");
		script = strstr(html, "<script>");
		sheet = strstr(html, "<link rel=\"stylesheet\"");
		if (script == NULL || sheet == NULL || script > sheet || script > head_end)
			g_error("%s: the scheme script does not run before the first stylesheet", ls_string(file, "path"));
		if (strstr(html, "<style") != NULL && strstr(html, "<style") < script)
			g_error("%s: a style element comes before the scheme script", ls_string(file, "path"));
		g_assert_nonnull(g_strstr_len(script, strstr(script, "</script>") - script, "localStorage"));
		g_assert_nonnull(g_strstr_len(script, strstr(script, "</script>") - script, "\"ls-scheme\""));
		g_assert_nonnull(g_strstr_len(script, strstr(script, "</script>") - script, "dataset.scheme"));
		g_assert_nonnull(strstr(html, "<meta name=\"color-scheme\" content=\"light dark\">"));
		header = strstr(html, "<header class=\"site-header\"");
		footer = strstr(html, "<footer class=\"site-footer\"");
		g_assert_nonnull(header);
		g_assert_nonnull(footer);
		if (strstr(header, "data-scheme-toggle") == NULL || strstr(header, "data-scheme-toggle") > strstr(header, "</header>"))
			g_error("%s: no scheme toggle in the header", ls_string(file, "path"));
		if (strstr(footer, "data-scheme-toggle") == NULL)
			g_error("%s: no scheme toggle in the footer", ls_string(file, "path"));
		g_assert_nonnull(strstr(html, "<button class=\"scheme-toggle\" type=\"button\" data-scheme-toggle hidden"));
	}
	g_assert_cmpuint(pages, >=, 5);
	/* The toggle's script cycles system, light, dark and keeps the choice. */
	g_assert_nonnull(strstr(js, "\"ls-scheme\""));
	g_assert_nonnull(strstr(js, "[\"system\", \"light\", \"dark\"]"));
	g_assert_nonnull(strstr(js, "localStorage.setItem"));
	g_assert_nonnull(strstr(js, "localStorage.removeItem"));
	g_assert_nonnull(strstr(js, "[data-scheme-toggle]"));
	/* Without scripts the system setting applies through the media query,
	 * and the change of scheme never animates. */
	g_assert_nonnull(strstr(css, "@media (prefers-color-scheme: dark) {\n:root:not([data-scheme=\"light\"]) {"));
	g_assert_nonnull(strstr(css, ".scheme-toggle"));
	{
		const gchar *start = strstr(css, "@media (prefers-color-scheme: dark)");
		const gchar *end = strstr(css, "@layer ls-base {");
		g_autofree gchar *scheme = g_strndup(start, (gsize)(end - start));
		g_assert_null(strstr(scheme, "transition"));
		g_assert_null(strstr(scheme, "animation"));
	}
	/* The owner's lock: one scheme, no toggle. */
	{
		const gchar *locks[] = {"light", "dark"};
		guint l;
		for (l = 0; l < G_N_ELEMENTS(locks); l++) {
			g_autoptr(JsonNode) locked_plan = NULL;
			g_autoptr(JsonNode) locked = NULL;
			g_autofree gchar *meta = g_strdup_printf("<meta name=\"color-scheme\" content=\"%s\">", locks[l]);
			const gchar *home, *styles;
			JsonObject *design;
			compose_scheme = locks[l];
			locked = compose_site(components, "evergreen", NULL, NULL, NULL, &locked_plan);
			compose_scheme = NULL;
			home = file_in(locked, "index.html");
			styles = file_in(locked, "styles.css");
			design = json_object_get_object_member(json_node_get_object(locked_plan), "design");
			g_assert_cmpstr(ls_string(design, "scheme"), ==, locks[l]);
			g_assert_nonnull(strstr(home, meta));
			g_assert_null(strstr(home, "data-scheme-toggle"));
			g_assert_null(strstr(styles, "prefers-color-scheme"));
			g_assert_null(strstr(styles, "data-scheme"));
			{
				g_autoptr(GHashTable) vars = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
				const gchar *paper;
				css_vars(vars, styles, "@layer ls-base {");
				paper = g_hash_table_lookup(vars, "paper");
				if (g_str_equal(locks[l], "dark") ? hex_luminance(paper) > 0.03 : hex_luminance(paper) < 0.5)
					g_error("locked %s: the page ground is %s", locks[l], paper);
				check_scheme_pairs("evergreen", locks[l], vars);
			}
		}
	}
	/* The order's style.scheme locks the site as site.scheme does. */
	{
		const gchar *locks[] = {"light", "dark", "auto"};
		guint l;
		for (l = 0; l < G_N_ELEMENTS(locks); l++) {
			g_autofree gchar *order = g_strdup_printf("{\"brand\": {\"name\": \"Lock %u\"}, \"style\": {\"scheme\": \"%s\"},"
				" \"contact\": {\"phone\": \"585-555-0141\"}, \"offers\": [{\"id\": \"a\", \"name\": \"Repairs\"}]}", l, locks[l]);
			g_autoptr(JsonObject) design = design_for(components, order, "evergreen", four_photos);
			g_assert_cmpstr(ls_string(design, "scheme"), ==, locks[l]);
		}
	}
	/* The picker never chooses dark by itself: every trade, every preset. */
	{
		const gchar *trades[] = {"Plumbing", "Hair salon", "Law office", "Bakery", "Dental practice", "Roofing",
			"Bowling club", "Accounting", "Barber", "Landscaping"};
		guint t;
		for (t = 0; t < G_N_ELEMENTS(trades); t++) {
			g_autofree gchar *order = g_strdup_printf("{\"brand\": {\"name\": \"%s %u\", \"tagline\": \"%s\"},"
				" \"contact\": {\"phone\": \"585-555-0141\"}, \"offers\": [{\"id\": \"a\", \"name\": \"%s\"}]}",
				trades[t], t, trades[t], trades[t]);
			g_autoptr(JsonObject) design = design_for(components, order, NULL, four_photos);
			g_assert_cmpstr(ls_string(design, "scheme"), ==, "auto");
		}
	}
}

/* ---- Controls (docs/components.org, "Controls") ------------------------- */

/* A contact page whose form shows every control. */
static const gchar controls_page[] =
	"{\"contact.html\": {\"path\": \"contact.html\", \"sections\": [{\"component\": \"page-header\", \"variant\": \"plain\","
	" \"content\": {\"heading\": \"Every control\"}}, {\"component\": \"form\", \"variant\": \"stacked\", \"id\": \"quote\","
	" \"content\": {\"heading\": \"Ask for a quote\", \"mode\": \"endpoint\", \"target\": \"/api/forms/quote\", \"fields\": ["
	"{\"name\": \"name\", \"label\": \"Your name\", \"required\": true, \"autocomplete\": \"name\"},"
	"{\"name\": \"rooms\", \"label\": \"Rooms\", \"type\": \"number\", \"min\": 1, \"max\": 20},"
	"{\"name\": \"day\", \"label\": \"Preferred day\", \"type\": \"date\"},"
	"{\"name\": \"at\", \"label\": \"Preferred time\", \"type\": \"time\"},"
	"{\"name\": \"budget\", \"label\": \"Budget\", \"type\": \"range\", \"min\": 0, \"max\": 5000, \"step\": 250},"
	"{\"name\": \"service\", \"label\": \"Service\", \"type\": \"radio\", \"required\": true, \"options\": [\"Repair\", \"Install\", \"Inspect\", \"Other\"]},"
	"{\"name\": \"size\", \"label\": \"Job size\", \"type\": \"toggle\", \"options\": [\"Small\", \"Medium\", \"Large\"]},"
	"{\"name\": \"town\", \"label\": \"Town\", \"type\": \"select\", \"options\": [\"Fairport\", \"Penfield\", \"Perinton\","
	" \"Pittsford\", \"Webster\", \"East Rochester\", \"Brighton\", \"Victor\"]},"
	"{\"name\": \"urgent\", \"label\": \"This is urgent\", \"type\": \"switch\"},"
	"{\"name\": \"photos\", \"label\": \"Photos of the job\", \"type\": \"file\", \"hint\": \"Optional.\"},"
	"{\"name\": \"agree\", \"label\": \"You may contact me about this request\", \"type\": \"checkbox\", \"required\": true},"
	"{\"name\": \"job\", \"label\": \"What needs doing?\", \"type\": \"textarea\", \"hint\": \"A sentence or two.\"}],"
	" \"submit\": \"Send\"}}, {\"component\": \"faq\", \"variant\": \"list\", \"content\": {\"items\": [{\"question\": \"One?\","
	" \"answer\": \"Yes.\"}, {\"question\": \"Two?\", \"answer\": \"Yes.\"}, {\"question\": \"Three?\", \"answer\": \"Yes.\"}]}}]}}";

/* A contact page as a writer answers it: name, email, phone, the services
 * as a select and a message, in @mode. */
static gchar *
enquiry_page(const gchar *mode){
	return g_strdup_printf("{\"contact.html\": {\"path\": \"contact.html\", \"sections\": [{\"component\": \"page-header\","
		" \"variant\": \"plain\", \"content\": {\"heading\": \"Contact\"}}, {\"component\": \"form\", \"variant\": \"stacked\","
		" \"id\": \"quote\", \"content\": {\"heading\": \"Ask for a quote\", \"mode\": \"%s\", \"target\": \"%s\", \"fields\": ["
		"{\"name\": \"name\", \"label\": \"Name\", \"required\": true, \"autocomplete\": \"name\"},"
		"{\"name\": \"email\", \"label\": \"Email\", \"type\": \"email\", \"required\": true, \"autocomplete\": \"email\"},"
		"{\"name\": \"phone\", \"label\": \"Phone\", \"type\": \"tel\", \"autocomplete\": \"tel\"},"
		"{\"name\": \"service\", \"label\": \"Service\", \"type\": \"select\", \"options\": [\"Emergency repairs\","
		" \"Drain cleaning\", \"Water heaters\", \"Other question\"]},"
		"{\"name\": \"message\", \"label\": \"Message\", \"type\": \"textarea\", \"required\": true}], \"submit\": \"Send\"}},"
		" {\"component\": \"faq\", \"variant\": \"list\", \"content\": {\"items\": [{\"question\": \"One?\", \"answer\": \"Yes.\"},"
		" {\"question\": \"Two?\", \"answer\": \"Yes.\"}, {\"question\": \"Three?\", \"answer\": \"Yes.\"}]}}]}}", mode,
		g_str_equal(mode, "mailto") ? "office@hartwell.example" : "/api/forms/quote");
}

/* The markup of the field whose control is named @name in @html: from the
 * field's opening element to the next field's, or NULL. */
static gchar *
field_of(const gchar *html, const gchar *name){
	g_autofree gchar *needle = g_strdup_printf("name=\"%s\"", name);
	const gchar *at = strstr(html, needle), *start, *end, *next_div, *next_set;
	if (at == NULL)
		return NULL;
	{
		/* The field's own element, not its error or hint paragraph. */
		const gchar *div = g_strrstr_len(html, at - html, "<div class=\"field");
		const gchar *set = g_strrstr_len(html, at - html, "<fieldset class=\"field");
		start = div != NULL && (set == NULL || div > set) ? div : set;
		if (start == NULL)
			return NULL;
		start = strstr(start, "class=");
	}
	next_div = strstr(at, "class=\"field ");
	next_set = strstr(at, "class=\"field\"");
	end = next_div != NULL && (next_set == NULL || next_div < next_set) ? next_div : next_set;
	if (end == NULL)
		end = strstr(at, "</form>");
	return g_strndup(start, (gsize)(end - start));
}

/* Every control in @html's forms is a native element with a visible label
 * (its own, or the fieldset's legend for a choice) and an error message
 * tied to it by aria-describedby. */
static void
check_controls_labelled(const gchar *where, const gchar *html){
	const gchar *kinds[] = {"<input", "<select", "<textarea"};
	guint k, controls = 0;
	for (k = 0; k < G_N_ELEMENTS(kinds); k++) {
		const gchar *at = html;
		while ((at = strstr(at, kinds[k])) != NULL) {
			const gchar *start = at, *close = strchr(at, '>');
			g_autofree gchar *tag = g_strndup(start, (gsize)(close - start + 1));
			g_autofree gchar *id = NULL;
			const gchar *idp = strstr(tag, " id=\""), *described = strstr(tag, "aria-describedby=\"");
			at = close;
			if (strstr(tag, "type=\"hidden\"") != NULL || strstr(tag, "tabindex=\"-1\"") != NULL)
				continue;
			controls++;
			if (idp == NULL)
				g_error("%s: a control has no id: %s", where, tag);
			id = g_strndup(idp + 5, (gsize)(strchr(idp + 5, '"') - idp - 5));
			if (strstr(tag, "type=\"radio\"") != NULL) {
				const gchar *label = g_strrstr_len(html, start - html, "<label class=\"choice");
				const gchar *ended = label != NULL ? strstr(label, "</label>") : NULL;
				const gchar *set = g_strrstr_len(html, start - html, "<fieldset");
				const gchar *legend = set != NULL ? strstr(set, "<legend") : NULL;
				if (label == NULL || ended == NULL || ended < start || set == NULL || legend == NULL || legend > start)
					g_error("%s: radio %s is not inside a choice label under a legend", where, id);
			} else {
				g_autofree gchar *label = g_strdup_printf("<label for=\"%s\"", id);
				if (strstr(html, label) == NULL)
					g_error("%s: control %s has no label", where, id);
			}
			if (described == NULL)
				g_error("%s: control %s has no aria-describedby", where, id);
			{
				g_autofree gchar *list = g_strndup(described + 18, (gsize)(strchr(described + 18, '"') - described - 18));
				g_auto(GStrv) ids = g_strsplit(list, " ", -1);
				guint i;
				gboolean error = FALSE;
				for (i = 0; ids[i] != NULL; i++) {
					g_autofree gchar *target = g_strdup_printf("id=\"%s\"", ids[i]);
					if (strstr(html, target) == NULL)
						g_error("%s: control %s describes itself by missing %s", where, id, ids[i]);
					error = error || g_str_has_suffix(ids[i], "-error");
				}
				if (!error)
					g_error("%s: control %s has no error message tied to it", where, id);
			}
		}
	}
	g_assert_cmpuint(controls, >, 0);
}

/* Every control renders from its native element with a label, a focus
 * ring, a 44 px target and the preset's corners, borders and accent. */
static void
test_controls(void){
	g_autoptr(LsComponents) components = library();
	g_autoptr(JsonNode) plan = NULL;
	g_autoptr(JsonNode) bundle = compose_site(components, "evergreen", NULL, NULL, controls_page, &plan);
	const gchar *html = file_in(bundle, "contact.html"), *css = file_in(bundle, "styles.css");
	const gchar *output = g_getenv("LS_CONTROLS_OUTPUT");
	g_autofree gchar *service = field_of(html, "service");
	g_autofree gchar *size = field_of(html, "size");
	g_autofree gchar *urgent = field_of(html, "urgent");
	g_autofree gchar *budget = field_of(html, "budget");
	g_autofree gchar *photos = field_of(html, "photos");
	g_autofree gchar *agree = field_of(html, "agree");
	g_autofree gchar *town = field_of(html, "town");
	g_autofree gchar *at = field_of(html, "at");
	g_autofree gchar *rooms = field_of(html, "rooms");
	g_autofree gchar *job = field_of(html, "job");
	g_assert_nonnull(html);
	check_controls_labelled("contact.html", html);
	/* Choices are radio buttons in a fieldset with a legend. */
	g_assert_nonnull(service);
	g_assert_true(g_str_has_prefix(service, "class=\"field field--choice"));
	g_assert_nonnull(strstr(service, "<legend"));
	g_assert_nonnull(strstr(service, "type=\"radio\" "));
	g_assert_nonnull(strstr(service, " required"));
	g_assert_nonnull(strstr(service, ">Inspect<"));
	g_assert_nonnull(size);
	g_assert_nonnull(strstr(size, "field--toggle"));
	g_assert_nonnull(strstr(size, "type=\"radio\" "));
	g_assert_nonnull(strstr(urgent, "type=\"checkbox\" role=\"switch\""));
	g_assert_nonnull(strstr(budget, "type=\"range\""));
	g_assert_nonnull(strstr(budget, "min=\"0\" max=\"5000\" step=\"250\""));
	g_assert_nonnull(strstr(budget, "<output"));
	g_assert_nonnull(strstr(photos, "type=\"file\""));
	g_assert_nonnull(strstr(photos, "accept=\"image/*\""));
	g_assert_nonnull(strstr(html, "enctype=\"multipart/form-data\""));
	g_assert_nonnull(strstr(agree, "type=\"checkbox\""));
	g_assert_null(strstr(agree, "role=\"switch\""));
	g_assert_nonnull(strstr(town, "<select"));
	g_assert_nonnull(strstr(at, "type=\"time\""));
	g_assert_nonnull(strstr(rooms, "type=\"number\""));
	g_assert_nonnull(strstr(rooms, "min=\"1\" max=\"20\""));
	g_assert_nonnull(strstr(job, "<textarea"));
	/* No control is a div pretending to be one. */
	g_assert_null(strstr(html, "role=\"radio\""));
	g_assert_null(strstr(html, "role=\"checkbox\""));
	g_assert_null(strstr(html, "<div role=\"switch\""));
	/* The styles: a 44 px target for every control, a focus ring, and the
	 * preset's control corners, borders and accent. */
	{
		const gchar *start = strstr(css, "/* controls */");
		g_autofree gchar *controls = NULL;
		const gchar *rules[] = {".choice", ".field--toggle", ".field--switch", "input[type=\"range\"]",
			"::file-selector-button", ":focus-visible", "[aria-invalid=\"true\"]"};
		guint r;
		g_assert_nonnull(start);
		/* Up to the first component's block. */
		controls = g_strndup(start, (gsize)(strstr(start, "@layer ls-components") - start));
		for (r = 0; r < G_N_ELEMENTS(rules); r++)
			if (strstr(controls, rules[r]) == NULL)
				g_error("the control styles have no %s rule", rules[r]);
		g_assert_nonnull(strstr(controls, "min-height: 2.75rem"));
		g_assert_nonnull(strstr(controls, "var(--radius-control"));
		g_assert_nonnull(strstr(controls, "var(--border"));
		g_assert_nonnull(strstr(controls, "var(--primary"));
		g_assert_nonnull(strstr(controls, "var(--heading-weight"));
		g_assert_nonnull(strstr(controls, "outline: 3px solid var(--focus)"));
	}
	/* The script sends the files with the fields and checks every choice. */
	g_assert_nonnull(strstr(file_in(bundle, "site.js"), "new FormData(form)"));
	if (output != NULL) {
		JsonArray *files = json_object_get_array_member(json_node_get_object(bundle), "files");
		guint f;
		for (f = 0; f < json_array_get_length(files); f++) {
			JsonObject *file = json_array_get_object_element(files, f);
			g_autofree gchar *path = g_build_filename(output, ls_string(file, "path"), NULL);
			g_autofree gchar *parent = g_path_get_dirname(path);
			if (ls_string(file, "encoding") != NULL)
				continue;
			g_assert_cmpint(g_mkdir_with_parents(parent, 0755), ==, 0);
			g_assert_true(g_file_set_contents(path, ls_string(file, "content"), -1, NULL));
		}
	}
	/* The page check refuses controls without what they need. */
	{
		const gchar *bad[] = {"{\"name\": \"s\", \"label\": \"S\", \"type\": \"radio\"}",
			"{\"name\": \"s\", \"label\": \"S\", \"type\": \"toggle\", \"options\": []}",
			"{\"name\": \"s\", \"label\": \"S\", \"type\": \"text\", \"options\": [\"A\"]}",
			"{\"name\": \"s\", \"label\": \"S\", \"type\": \"range\"}",
			"{\"name\": \"s\", \"label\": \"S\", \"type\": \"range\", \"min\": 9, \"max\": 1}",
			"{\"name\": \"s\", \"label\": \"S\", \"type\": \"switch\", \"options\": [\"On\"]}",
			"{\"name\": \"s\", \"label\": \"S\", \"type\": \"color\"}"};
		guint b;
		for (b = 0; b < G_N_ELEMENTS(bad); b++) {
			g_autofree gchar *text = g_strdup_printf("{\"path\": \"contact.html\", \"sections\": [{\"component\": \"page-header\","
				" \"variant\": \"plain\", \"content\": {\"heading\": \"Contact\"}}, {\"component\": \"form\", \"variant\": \"stacked\","
				" \"content\": {\"heading\": \"Ask\", \"mode\": \"local\", \"fields\": [%s], \"submit\": \"Send\"}}]}", bad[b]);
			g_autoptr(JsonNode) page = parse(text);
			g_autoptr(GError) error = NULL;
			if (ls_components_check_page(components, plan, "contact.html", page, &error))
				g_error("the page check accepted %s", bad[b]);
			g_assert_nonnull(strstr(error->message, "fields"));
		}
	}
}

/* The forms the builder writes use the controls: the services as a choice,
 * how to reply as a radio group, a preferred date and time, a switch for
 * urgent work where the order offers it, a photo upload where the trade
 * benefits and the form can carry files, and an agreement box; the booking
 * form and a supplied form too. */
static void
test_forms_use_controls(void){
	g_autoptr(LsComponents) components = library();
	const gchar *modes[] = {"mailto", "endpoint"};
	guint m;
	for (m = 0; m < G_N_ELEMENTS(modes); m++) {
		g_autofree gchar *pages = enquiry_page(modes[m]);
		g_autoptr(JsonNode) plan = NULL;
		g_autoptr(JsonNode) bundle = compose_site(components, "evergreen", NULL, NULL, pages, &plan);
		const gchar *html = file_in(bundle, "contact.html");
		g_autofree gchar *service = field_of(html, "service");
		g_autofree gchar *reply = field_of(html, "contact_by");
		g_autofree gchar *date = field_of(html, "preferred_date");
		g_autofree gchar *time = field_of(html, "preferred_time");
		g_autofree gchar *urgent = field_of(html, "urgent");
		g_autofree gchar *agree = field_of(html, "agree");
		g_autofree gchar *photos = field_of(html, "photos");
		check_controls_labelled(modes[m], html);
		if (service == NULL || strstr(service, "type=\"radio\"") == NULL)
			g_error("%s: the services are not a choice of radio buttons", modes[m]);
		g_assert_nonnull(strstr(service, ">Water heaters<"));
		if (reply == NULL || strstr(reply, "type=\"radio\"") == NULL || strstr(reply, "<legend") == NULL)
			g_error("%s: no 'contact me by' radio group", modes[m]);
		g_assert_nonnull(date);
		g_assert_nonnull(strstr(date, "type=\"date\""));
		g_assert_nonnull(time);
		g_assert_nonnull(strstr(time, "type=\"time\""));
		/* The rich order offers emergency repairs. */
		if (urgent == NULL || strstr(urgent, "role=\"switch\"") == NULL)
			g_error("%s: no urgent switch", modes[m]);
		if (agree == NULL || strstr(agree, "type=\"checkbox\"") == NULL || strstr(agree, " required") == NULL)
			g_error("%s: no required agreement box", modes[m]);
		/* A mail link cannot carry a file; a posted form can. */
		if (g_str_equal(modes[m], "mailto"))
			g_assert_null(photos);
		else {
			g_assert_nonnull(photos);
			g_assert_nonnull(strstr(photos, "type=\"file\""));
			g_assert_null(strstr(photos, " required"));
		}
		/* The message stays last before the agreement. */
		g_assert_true(strstr(html, "name=\"message\"") < strstr(html, "name=\"agree\""));
		g_assert_true(strstr(html, "name=\"preferred_time\"") < strstr(html, "name=\"message\""));
		/* The booking page's form: the service as a choice and the agreement. */
		if (m == 0) {
			const gchar *book = file_in(bundle, "book.html");
			const gchar *form = book != NULL ? strstr(book, "data-booking-form") : NULL;
			g_assert_nonnull(form);
			g_assert_nonnull(strstr(form, "name=\"service\""));
			g_assert_nonnull(strstr(form, "type=\"radio\""));
			g_assert_nonnull(strstr(form, "name=\"agree\""));
			g_assert_nonnull(strstr(file_in(bundle, "site.js"), "\"Service: \""));
			check_controls_labelled("book.html", form);
		}
	}
	/* Urgent work only where the order offers it. */
	{
		g_autoptr(JsonObject) plumbing = design_for(components, plumber, "evergreen", four_photos);
		g_autoptr(JsonObject) styling = design_for(components, salon, "evergreen", four_photos);
		JsonObject *controls = json_object_get_object_member(plumbing, "controls");
		g_assert_true(json_object_get_boolean_member(controls, "urgent"));
		g_assert_true(json_object_get_boolean_member(controls, "photos"));
		controls = json_object_get_object_member(styling, "controls");
		g_assert_false(json_object_get_boolean_member(controls, "urgent"));
		g_assert_false(json_object_get_boolean_member(controls, "photos"));
	}
	/* A supplied form is drawn exactly, in the same controls: the hero
	 * form and the modal form. */
	{
		const gchar *places[][2] = {{"hero", "form"}, {"overlay", "modal-form"}};
		guint p;
		for (p = 0; p < G_N_ELEMENTS(places); p++) {
			g_autoptr(JsonNode) plan = NULL;
			g_autoptr(JsonNode) bundle = compose_site(components, "evergreen", places[p][0], places[p][1], NULL, &plan);
			const gchar *home = file_in(bundle, "index.html");
			g_autofree gchar *reply = field_of(home, "contact_by");
			if (reply == NULL || strstr(reply, "type=\"radio\"") == NULL)
				g_error("%s/%s: the supplied radio group is not drawn", places[p][0], places[p][1]);
			g_assert_null(field_of(home, "urgent"));
			check_controls_labelled(places[p][1], strstr(home, "<form class=\"form__form quote-form"));
		}
	}
}

static void write_bundle(JsonNode *bundle, const gchar *dir);

/* No link on @html leads to the booking address @book, and a link to the
 * page itself (@self) is only the navigation's current-page link. */
static void
check_no_self_link(const gchar *where, const gchar *html, const gchar *book, const gchar *self){
	g_autofree gchar *to_book = g_strdup_printf("href=\"%s\"", book);
	g_autofree gchar *to_self = g_strdup_printf("href=\"%s\"", self);
	const gchar *at = html;
	if (strstr(html, to_book) != NULL)
		g_error("%s links to its own booking address %s", where, book);
	while ((at = strstr(at, to_self)) != NULL) {
		at += strlen(to_self);
		if (!g_str_has_prefix(at, " aria-current=\"page\""))
			g_error("%s links to itself outside the navigation: %.120s", where, at - strlen(to_self));
	}
}

/* The booking request form of @html's booking calendar, from its wrapper
 * to the end of its form, inclusive, or NULL. */
static gchar *
booking_request_of(const gchar *html){
	const gchar *start = html != NULL ? strstr(html, "data-booking-request") : NULL;
	const gchar *end = start != NULL ? strstr(start, "</form>") : NULL;
	return end != NULL ? g_strndup(start, (gsize)(end + strlen("</form>") - start)) : NULL;
}

/* When open times cannot load, the booking page is a booking request form
 * that works without scripts and sends the way the site's contact form
 * does: the service as radios, a preferred date and time, name, how to
 * reach the visitor, a note and the agreement box, in words that say the
 * business confirms the time. The booking page never links to itself as
 * its booking action; its book action leads to the form on the page.
 * With LS_BOOKING_OUTPUT set, the mail-form site is written there for
 * tests/booking.sh. */
static void
test_booking_request(void){
	g_autoptr(LsComponents) components = library();
	const gchar *output = g_getenv("LS_BOOKING_OUTPUT");
	const gchar *modes[] = {"mailto", "endpoint", "none"};
	const gchar *actions[] = {"action=\"mailto:office@hartwell.example\"", "action=\"/api/forms/quote\"",
		"action=\"mailto:office@hartwell.example\""};
	guint m;
	for (m = 0; m < G_N_ELEMENTS(modes); m++) {
		g_autofree gchar *pages = g_str_equal(modes[m], "none") ? NULL : enquiry_page(modes[m]);
		g_autoptr(JsonNode) plan = NULL;
		g_autoptr(JsonNode) bundle = compose_site(components, "evergreen", NULL, NULL, pages, &plan);
		const gchar *book = file_in(bundle, "book.html");
		const gchar *app;
		g_autofree gchar *request = booking_request_of(book);
		g_autofree gchar *service = NULL, *date = NULL, *time = NULL, *name = NULL, *reach = NULL;
		g_autofree gchar *notes = NULL, *agree = NULL, *wrapper = NULL, *form = NULL;
		/* Written first, so a failed check below leaves the site to look at. */
		if (output != NULL && m == 0)
			write_bundle(bundle, output);
		g_assert_nonnull(book);
		if (request == NULL)
			g_error("%s: the booking page has no booking request form", modes[m]);
		/* Neither the wrapper nor the form waits for a script. */
		wrapper = g_strndup(request, (gsize)(strchr(request, '>') - request));
		form = g_strndup(strstr(request, "<form"), (gsize)(strchr(strstr(request, "<form"), '>') - strstr(request, "<form")));
		g_assert_null(strstr(wrapper, " hidden"));
		g_assert_null(strstr(form, " hidden"));
		app = strstr(book, "booking-calendar__app");
		g_assert_nonnull(app);
		g_assert_true(strstr(book, "data-booking-request") < app);
		g_assert_true(g_str_has_prefix(app, "booking-calendar__app\" hidden"));
		/* It sends the way the contact form does. */
		if (strstr(form, actions[m]) == NULL)
			g_error("%s: the request form does not send like the contact form: %s", modes[m], form);
		g_assert_nonnull(strstr(form, "method=\"post\""));
		g_assert_nonnull(strstr(form, "data-validate"));
		if (g_str_equal(modes[m], "endpoint"))
			g_assert_nonnull(strstr(form, "data-endpoint-form"));
		else
			g_assert_nonnull(strstr(form, "enctype=\"text/plain\""));
		check_controls_labelled(modes[m], request);
		service = field_of(request, "service");
		date = field_of(request, "preferred_date");
		time = field_of(request, "preferred_time");
		name = field_of(request, "name");
		reach = field_of(request, "reach");
		notes = field_of(request, "notes");
		agree = field_of(request, "agree");
		if (service == NULL || strstr(service, "type=\"radio\"") == NULL || strstr(service, " required") == NULL)
			g_error("%s: the services are not a required choice of radio buttons", modes[m]);
		g_assert_nonnull(date);
		g_assert_nonnull(strstr(date, "type=\"date\""));
		g_assert_nonnull(strstr(date, " required"));
		g_assert_nonnull(time);
		g_assert_nonnull(strstr(time, "type=\"time\""));
		g_assert_nonnull(name);
		g_assert_nonnull(strstr(name, "autocomplete=\"name\""));
		g_assert_nonnull(strstr(name, " required"));
		g_assert_nonnull(reach);
		g_assert_nonnull(strstr(reach, ">Email or phone"));
		g_assert_nonnull(strstr(reach, " required"));
		g_assert_nonnull(notes);
		g_assert_nonnull(strstr(notes, "<textarea"));
		if (agree == NULL || strstr(agree, "type=\"checkbox\"") == NULL || strstr(agree, " required") == NULL)
			g_error("%s: no required agreement box on the request form", modes[m]);
		/* Its words say the business confirms the time. */
		g_assert_nonnull(strstr(request, "will confirm the time"));
		/* No made-up open times: the page names no time of day itself. */
		g_assert_null(strstr(request, " AM<"));
		g_assert_null(strstr(request, " PM<"));
		check_no_self_link(modes[m], book, "/book", "book.html");
	}
}

/* Writes every text file of @bundle under @dir. */
static void
write_bundle(JsonNode *bundle, const gchar *dir){
	JsonArray *files = json_object_get_array_member(json_node_get_object(bundle), "files");
	guint f;
	for (f = 0; f < json_array_get_length(files); f++) {
		JsonObject *file = json_array_get_object_element(files, f);
		g_autofree gchar *path = g_build_filename(dir, ls_string(file, "path"), NULL);
		g_autofree gchar *parent = g_path_get_dirname(path);
		if (ls_string(file, "encoding") != NULL)
			continue;
		g_assert_cmpint(g_mkdir_with_parents(parent, 0755), ==, 0);
		g_assert_true(g_file_set_contents(path, ls_string(file, "content"), -1, NULL));
	}
}

/* The controls gallery: one site per control, its contact page a form
 * that shows that control in the made-up plumber's words. With
 * LS_CONTROLS_GALLERY set, each site is written there as DIR/NN-NAME/. */
static void
test_controls_gallery(void){
	g_autoptr(LsComponents) components = library();
	const gchar *output = g_getenv("LS_CONTROLS_GALLERY");
	const gchar *const pages[][4] = {
		{"checkbox", "Checkboxes", "Tick what applies; the box and its words are one target.",
			"{\"name\": \"agree\", \"label\": \"You may contact me about this request.\", \"type\": \"checkbox\", \"required\": true},"
			"{\"name\": \"remind\", \"label\": \"Send me a reminder the day before\", \"type\": \"checkbox\"},"
			"{\"name\": \"pets\", \"label\": \"There are pets at home\", \"type\": \"checkbox\", \"hint\": \"So the plumber knows to close doors behind them.\"}"},
		{"radio", "Radio buttons", "Choose one; every choice is a full row.",
			"{\"name\": \"service\", \"label\": \"Which service?\", \"type\": \"radio\", \"required\": true, \"options\": [\"Emergency repairs\", \"Drain cleaning\", \"Water heaters\", \"Something else\"]},"
			"{\"name\": \"contact_by\", \"label\": \"Contact me by\", \"type\": \"radio\", \"options\": [\"Phone\", \"Email\"]}"},
		{"switch", "Switches", "On or off at once, with no send needed to see the state.",
			"{\"name\": \"urgent\", \"label\": \"This is urgent\", \"type\": \"switch\"},"
			"{\"name\": \"text_me\", \"label\": \"Text me when the plumber is on the way\", \"type\": \"switch\", \"hint\": \"One message, on the day.\"}"},
		{"toggle", "Toggle buttons", "A short choice drawn as a row of buttons; they are radio buttons underneath.",
			"{\"name\": \"size\", \"label\": \"How big is the job?\", \"type\": \"toggle\", \"options\": [\"Small\", \"Medium\", \"Large\"]},"
			"{\"name\": \"home\", \"label\": \"Where is it?\", \"type\": \"toggle\", \"required\": true, \"options\": [\"House\", \"Apartment\"]}"},
		{"select", "Select", "A long list of choices in the browser's own list.",
			"{\"name\": \"town\", \"label\": \"Town\", \"type\": \"select\", \"options\": [\"Fairport\", \"Penfield\", \"Perinton\", \"Pittsford\", \"Webster\", \"East Rochester\", \"Brighton\", \"Victor\"]},"
			"{\"name\": \"heard\", \"label\": \"How did you find us?\", \"type\": \"select\", \"options\": [\"A neighbour\", \"A search\", \"A van in the street\", \"Something else\"]}"},
		{"textarea", "Text areas", "Room for a few sentences, with a limit the browser keeps.",
			"{\"name\": \"job\", \"label\": \"What needs doing?\", \"type\": \"textarea\", \"required\": true, \"maxlength\": 2000, \"hint\": \"A sentence or two is enough.\"},"
			"{\"name\": \"access\", \"label\": \"How do we get in?\", \"type\": \"textarea\"}"},
		{"text", "Text, email, phone and number", "One line each, with the keyboard a phone should show.",
			"{\"name\": \"name\", \"label\": \"Your name\", \"required\": true, \"autocomplete\": \"name\"},"
			"{\"name\": \"email\", \"label\": \"Email\", \"type\": \"email\", \"autocomplete\": \"email\"},"
			"{\"name\": \"phone\", \"label\": \"Phone\", \"type\": \"tel\", \"autocomplete\": \"tel\"},"
			"{\"name\": \"rooms\", \"label\": \"How many bathrooms?\", \"type\": \"number\", \"min\": 1, \"max\": 9}"},
		{"date-time", "Date and time", "The browser's own pickers, in the page's colour scheme.",
			"{\"name\": \"preferred_date\", \"label\": \"Preferred date\", \"type\": \"date\"},"
			"{\"name\": \"preferred_time\", \"label\": \"Preferred time\", \"type\": \"time\", \"hint\": \"We confirm the time by phone.\"}"},
		{"range", "Range", "A slider with its value beside it.",
			"{\"name\": \"budget\", \"label\": \"Rough budget in dollars\", \"type\": \"range\", \"min\": 0, \"max\": 5000, \"step\": 250},"
			"{\"name\": \"fixtures\", \"label\": \"Fixtures to replace\", \"type\": \"range\", \"min\": 1, \"max\": 10}"},
		{"file", "File upload", "Photos of the job travel with the form when it posts.",
			"{\"name\": \"photos\", \"label\": \"Photos of the job\", \"type\": \"file\", \"hint\": \"Optional. Pictures help us see the job.\"}"},
		{"validation", "Messages", "Send the form empty: each field says what it needs, above the field, and a line counts them.",
			"{\"name\": \"name\", \"label\": \"Your name\", \"required\": true, \"autocomplete\": \"name\"},"
			"{\"name\": \"email\", \"label\": \"Email\", \"type\": \"email\", \"required\": true, \"autocomplete\": \"email\"},"
			"{\"name\": \"service\", \"label\": \"Which service?\", \"type\": \"radio\", \"required\": true, \"options\": [\"Repairs\", \"Drains\", \"Heaters\"]},"
			"{\"name\": \"agree\", \"label\": \"You may contact me about this request.\", \"type\": \"checkbox\", \"required\": true}"}};
	guint i;
	g_assert_true(g_file_get_contents("tests/fixtures/gallery/photos.json", &gallery_photos, NULL, NULL));
	for (i = 0; i < G_N_ELEMENTS(pages); i++) {
		g_autoptr(JsonNode) plan = NULL;
		g_autoptr(JsonNode) bundle = NULL;
		g_autoptr(GString) problems = g_string_new(NULL);
		g_autofree gchar *extra = g_strdup_printf("{\"contact.html\": {\"path\": \"contact.html\", \"sections\": ["
			"{\"component\": \"page-header\", \"variant\": \"plain\", \"content\": {\"heading\": \"%s\"}},"
			"{\"component\": \"form\", \"variant\": \"card\", \"id\": \"quote\", \"content\": {\"heading\": \"%s\","
			" \"intro\": \"%s\", \"mode\": \"endpoint\", \"target\": \"/api/forms/quote\", \"fields\": [%s], \"submit\": \"Send\"}},"
			"{\"component\": \"faq\", \"variant\": \"list\", \"content\": {\"heading\": \"About this control\", \"items\": ["
			"{\"question\": \"Is it the browser's own control?\", \"answer\": \"Yes. It is drawn in the site's colours.\"},"
			"{\"question\": \"Does it work in dark?\", \"answer\": \"Yes. Use the Colours button.\"},"
			"{\"question\": \"Does it work with a keyboard?\", \"answer\": \"Yes. Tab reaches it.\"}]}}]}}",
			pages[i][1], pages[i][1], pages[i][2], pages[i][3]);
		bundle = compose_site(components, "evergreen", NULL, NULL, extra, &plan);
		if (!ls_components_check_taste(components, plan, bundle, problems))
			g_error("%s: %s", pages[i][0], problems->str);
		check_controls_labelled(pages[i][0], file_in(bundle, "contact.html"));
		if (output != NULL) {
			g_autofree gchar *dir = g_strdup_printf("%s/%02u-control-%s", output, i + 1, pages[i][0]);
			write_bundle(bundle, dir);
		}
	}
	g_clear_pointer(&gallery_photos, g_free);
}

/* The schemes gallery: one composed site per preset, each in both colour
 * schemes. With LS_SCHEMES_OUTPUT set, each is written there as
 * DIR/PRESET/. */
static void
test_scheme_gallery(void){
	g_autoptr(LsComponents) components = library();
	const gchar *output = g_getenv("LS_SCHEMES_OUTPUT");
	guint p;
	g_assert_true(g_file_get_contents("tests/fixtures/gallery/pages.json", &gallery_pages, NULL, NULL));
	g_assert_true(g_file_get_contents("tests/fixtures/gallery/photos.json", &gallery_photos, NULL, NULL));
	for (p = 0; p < G_N_ELEMENTS(presets); p++) {
		g_autoptr(JsonNode) plan = NULL;
		g_autoptr(JsonNode) bundle = compose_site(components, presets[p][0], NULL, NULL, NULL, &plan);
		g_autoptr(GString) problems = g_string_new(NULL);
		if (!ls_components_check_taste(components, plan, bundle, problems))
			g_error("%s: %s", presets[p][0], problems->str);
		g_assert_nonnull(strstr(file_in(bundle, "styles.css"), ":root[data-scheme=\"dark\"]"));
		if (output != NULL) {
			g_autofree gchar *dir = g_build_filename(output, presets[p][0], NULL);
			write_bundle(bundle, dir);
		}
	}
	g_clear_pointer(&gallery_pages, g_free);
	g_clear_pointer(&gallery_photos, g_free);
}

/* A header layout styles the header's own name, never the footer's: a
 * header rule on every .brand floated the footer's name, white on white in
 * light and plain to see in dark. */
static void
test_header_brand_scope(void){
	g_autofree gchar *css = NULL;
	g_auto(GStrv) lines = NULL;
	guint i;
	g_assert_true(g_file_get_contents("data/components/design.css", &css, NULL, NULL));
	lines = g_strsplit(css, "\n", -1);
	for (i = 0; lines[i] != NULL; i++) {
		const gchar *brace = strchr(lines[i], '{');
		g_autofree gchar *selector = brace != NULL ? g_strndup(lines[i], (gsize)(brace - lines[i])) : NULL;
		if (selector != NULL && strstr(selector, ".header--") != NULL && strstr(selector, ".brand") != NULL &&
			strstr(selector, ".site-header") == NULL && strstr(selector, ".site-footer") == NULL)
			g_error("design.css line %u styles every .brand under a header layout: %s", i + 1, selector);
	}
}

/* On a preset site a row of inline links that the writer gave
 * descriptions draws as a list, so the descriptions show: inline hides
 * them and left a section of short links, as the site gate found on the
 * bakery's booking page. Links without descriptions stay inline. */
static void
test_inline_links(void){
	g_autoptr(LsComponents) components = library();
	const gchar *pages = "{\"contact.html\": {\"path\": \"contact.html\", \"sections\": ["
		"{\"component\": \"page-header\", \"variant\": \"plain\", \"content\": {\"heading\": \"Contact\"}},"
		"{\"component\": \"links\", \"variant\": \"inline\", \"id\": \"described\", \"content\": {\"heading\": \"Ready to ask?\", \"items\": ["
		"{\"link\": {\"label\": \"Open the contact form\", \"href\": \"index.html\"}, \"body\": \"Send a quote request or an enquiry.\"},"
		"{\"link\": {\"label\": \"Read common questions\", \"href\": \"questions.html\"}, \"body\": \"Answers about the work.\"}]}},"
		"{\"component\": \"links\", \"variant\": \"inline\", \"id\": \"plain\", \"content\": {\"heading\": \"More\", \"items\": ["
		"{\"link\": {\"label\": \"Questions\", \"href\": \"questions.html\"}}, {\"link\": {\"label\": \"Home\", \"href\": \"index.html\"}}]}}]}}";
	g_autoptr(JsonNode) plan = NULL;
	g_autoptr(JsonNode) bundle = compose_site(components, "evergreen", NULL, NULL, pages, &plan);
	const gchar *html = file_in(bundle, "contact.html");
	const gchar *described = strstr(html, "id=\"described\"");
	const gchar *plain = strstr(html, "id=\"plain\"");
	g_assert_nonnull(described);
	g_assert_nonnull(plain);
	g_assert_nonnull(g_strrstr_len(html, described - html, "links links--list"));
	g_assert_null(strstr(g_strrstr_len(html, described - html, "<section"), "links--inline\" id=\"described"));
	g_assert_nonnull(strstr(g_strrstr_len(html, plain - html, "<section"), "links--inline"));
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
	g_test_add_func("/design/neighbours", test_neighbours);
	g_test_add_func("/design/spread", test_spread);
	g_test_add_func("/design/new-variants", test_new_variants);
	g_test_add_func("/design/supplied-facts", test_supplied_facts);
	g_test_add_func("/design/photo-every-page", test_photo_every_page);
	g_test_add_func("/design/layout-library", test_layout_library);
	g_test_add_func("/design/layout-picks", test_layout_picks);
	g_test_add_func("/design/layout-neighbours", test_layout_neighbours);
	g_test_add_func("/design/layout-spread", test_layout_spread);
	g_test_add_func("/design/sticky-bar-headers", test_sticky_bar_headers);
	g_test_add_func("/design/layout-render", test_layout_render);
	g_test_add_func("/design/gallery", test_gallery);
	g_test_add_func("/design/dark-schemes", test_dark_schemes);
	g_test_add_func("/design/scheme-page", test_scheme_page);
	g_test_add_func("/design/controls", test_controls);
	g_test_add_func("/design/forms-use-controls", test_forms_use_controls);
	g_test_add_func("/design/booking-request", test_booking_request);
	g_test_add_func("/design/header-brand-scope", test_header_brand_scope);
	g_test_add_func("/design/inline-links", test_inline_links);
	g_test_add_func("/design/controls-gallery", test_controls_gallery);
	g_test_add_func("/design/scheme-gallery", test_scheme_gallery);
	return g_test_run();
}
