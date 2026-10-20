/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* The seed decides: the builder, not the model, sets each site's look,
 * colours, type, opening and structure; the model writes the words and art
 * inside them. See docs/components.org, "The seed decides". */
#include "libresite.h"
#include "core/ls-components.h"
#include "core/ls-util.h"
#include <string.h>

static JsonNode *
load(const gchar *path){
	g_autofree gchar *text = NULL;
	g_assert_true(g_file_get_contents(path, &text, NULL, NULL));
	return ls_json_parse(text, NULL);
}

static LsComponents *
library(void){
	g_autoptr(GError) error = NULL;
	LsComponents *components = ls_components_new(&error);
	g_assert_no_error(error);
	return components;
}

static JsonObject *
member(JsonNode *plan, const gchar *name){
	return json_object_get_object_member(json_node_get_object(plan), name);
}

static const gchar *const flourishes[] = {"ornament", "emphasis", "kicker", "backdrop", "edges", "texture", "links", NULL};

static gboolean
is_flourish(const gchar *axis){
	return g_strv_contains(flourishes, axis);
}

static JsonObject *composition_look(const gchar *composition);

/* The values the dealt art direction chooses @axis from (docs/components.org,
 * "Art direction"), or NULL when it leaves the axis to the seed. */
static JsonArray *
direction_choices(JsonNode *plan, const gchar *axis){
	static JsonNode *catalog = NULL;
	JsonObject *structure = json_object_get_object_member(json_node_get_object(plan), "structure");
	JsonObject *directions, *direction, *look;
	if (catalog == NULL) {
		/* As loaded, so entries from the wider block are there too. */
		g_autoptr(LsComponents) library = ls_components_new(NULL);
		g_assert_nonnull(library);
		catalog = json_node_ref(ls_components_get_catalog(library));
	}
	directions = json_object_get_object_member(json_object_get_object_member(json_node_get_object(catalog), "art_directions"),
		"directions");
	direction = json_object_has_member(structure, "direction")
		? json_object_get_object_member(directions, json_object_get_string_member(structure, "direction")) : NULL;
	look = direction != NULL ? json_object_get_object_member(direction, "look") : NULL;
	return look != NULL && json_object_has_member(look, axis) ? json_object_get_array_member(look, axis) : NULL;
}

/* The value the builder sets for @axis over the seed's: the motion kit's
 * (docs/components.org, "Motion"), else the home composition's. */
static const gchar *
override(JsonNode *plan, const gchar *axis){
	static JsonNode *catalog = NULL;
	JsonObject *structure = json_object_get_object_member(json_node_get_object(plan), "structure");
	JsonObject *kits, *kit, *own;
	if (catalog == NULL) {
		/* As loaded, so entries from the wider block are there too. */
		g_autoptr(LsComponents) library = ls_components_new(NULL);
		g_assert_nonnull(library);
		catalog = json_node_ref(ls_components_get_catalog(library));
	}
	kits = json_object_get_object_member(json_object_get_object_member(json_node_get_object(catalog), "motion_kits"), "kits");
	kit = json_object_has_member(structure, "kit") ? json_object_get_object_member(kits, json_object_get_string_member(structure, "kit")) : NULL;
	if (kit != NULL && json_object_has_member(kit, "look") &&
		json_object_has_member(json_object_get_object_member(kit, "look"), axis))
		return json_object_get_string_member(json_object_get_object_member(kit, "look"), axis);
	own = composition_look(json_object_get_string_member(structure, "composition"));
	return own != NULL && json_object_has_member(own, axis) ? json_object_get_string_member(own, axis) : NULL;
}

/* The layout axes the dealt home composition sets (docs/components.org,
 * "Home compositions"), read from the catalog. */
static JsonArray *
composition_openings(const gchar *composition){
	static JsonNode *catalog = NULL;
	if (catalog == NULL) {
		g_autoptr(LsComponents) library = ls_components_new(NULL);
		g_assert_nonnull(library);
		catalog = json_node_ref(ls_components_get_catalog(library));
	}
	return json_object_get_array_member(json_object_get_object_member(json_object_get_object_member(json_object_get_object_member(
		json_node_get_object(catalog), "compositions"), "layouts"), composition), "openings");
}

static JsonObject *
composition_look(const gchar *composition){
	static JsonNode *catalog = NULL;
	JsonObject *layout;
	if (catalog == NULL) {
		/* As loaded, so entries from the wider block are there too. */
		g_autoptr(LsComponents) library = ls_components_new(NULL);
		g_assert_nonnull(library);
		catalog = json_node_ref(ls_components_get_catalog(library));
	}
	layout = json_object_get_object_member(json_object_get_object_member(json_object_get_object_member(
		json_node_get_object(catalog), "compositions"), "layouts"), composition);
	return layout != NULL && json_object_has_member(layout, "look") ? json_object_get_object_member(layout, "look") : NULL;
}

/* Every look axis but the retired flourishes is the seed's, whatever the
 * model chose, except those the dealt home composition or motion kit sets;
 * the same seed gives the same look. */
static void
test_look(void){
	g_autoptr(LsComponents) components = library();
	const gchar *seeds[] = {"fernhill-plumbing\nAn illustrative plumber.", "bluewell-pools\nAn illustrative pool service."};
	guint s;
	for (s = 0; s < G_N_ELEMENTS(seeds); s++) {
		g_autoptr(JsonNode) plan = load("tests/fixtures/composed/plan.json");
		g_autoptr(JsonNode) again = load("tests/fixtures/composed/plan.json");
		g_autoptr(JsonObject) dealt = ls_components_suggest_look(components, seeds[s]);
		g_autoptr(GList) axes = json_object_get_members(dealt);
		g_autoptr(GError) error = NULL;
		g_autofree gchar *first = NULL, *second = NULL;
		GList *axis;
		json_object_set_string_member(member(plan, "look"), "art", "woodcut");
		g_assert_true(ls_components_apply_seed(components, plan, seeds[s], "trades", FALSE));
		for (axis = axes; axis != NULL; axis = axis->next) {
			const gchar *set = override(plan, axis->data);
			JsonArray *choices = direction_choices(plan, axis->data);
			if (is_flourish(axis->data))
				continue;
			/* The art direction picks these from its own lists. */
			if (choices != NULL) {
				const gchar *value = json_object_get_string_member(member(plan, "look"), axis->data);
				guint c;
				gboolean found = FALSE;
				for (c = 0; c < json_array_get_length(choices); c++)
					found |= g_strcmp0(json_array_get_string_element(choices, c), value) == 0;
				g_assert_true(found);
				continue;
			}
			g_assert_cmpstr(json_object_get_string_member(member(plan, "look"), axis->data), ==,
				set != NULL ? set : json_object_get_string_member(dealt, axis->data));
		}
		g_assert_true(ls_components_apply_seed(components, again, seeds[s], "trades", FALSE));
		first = ls_json_encode(plan);
		second = ls_json_encode(again);
		g_assert_cmpstr(first, ==, second);
		g_assert_true(ls_components_check_plan(components, plan, &error));
		g_assert_no_error(error);
	}
}

/* Colours and type come from the dealt identity, even when the model wrote
 * the house default; a plan without web fonts keeps its own stacks. */
static void
test_identity(void){
	g_autoptr(LsComponents) components = library();
	const gchar *seed = "ridgeback-roofing\nAn illustrative roofer.";
	g_autoptr(JsonObject) identity = ls_components_suggest_identity(components, seed, "trades");
	JsonObject *palette = json_object_get_object_member(json_object_get_object_member(identity, "palette"), "tokens");
	JsonObject *type = json_object_get_object_member(identity, "type");
	g_autoptr(JsonNode) plan = load("tests/fixtures/composed/plan.json");
	g_autoptr(JsonNode) local = load("tests/fixtures/composed/plan.json");
	g_autoptr(GError) error = NULL;
	JsonObject *tokens = member(plan, "tokens");
	g_autoptr(GList) keys = json_object_get_members(palette);
	GList *key;
	JsonArray *fonts;
	/* The house default: warm cream paper, a rust accent, Fraunces. */
	json_object_set_string_member(tokens, "accent", "#a8431c");
	json_object_set_string_member(tokens, "font_display", "Fraunces, Georgia, serif");
	json_object_set_array_member(json_node_get_object(plan), "fonts", json_array_new());
	{
		JsonObject *font = json_object_new();
		JsonArray *weights = json_array_new();
		json_array_add_int_element(weights, 700);
		json_object_set_string_member(font, "family", "Fraunces");
		json_object_set_array_member(font, "weights", weights);
		json_array_add_object_element(json_object_get_array_member(json_node_get_object(plan), "fonts"), font);
	}
	g_assert_true(ls_components_apply_seed(components, plan, seed, "trades", FALSE));
	for (key = keys; key != NULL; key = key->next)
		g_assert_cmpstr(json_object_get_string_member(tokens, key->data), ==,
			json_object_get_string_member(palette, key->data));
	g_assert_cmpstr(json_object_get_string_member(tokens, "font_display"), ==,
		json_object_get_string_member(json_object_get_object_member(type, "tokens"), "font_display"));
	fonts = json_object_get_array_member(json_node_get_object(plan), "fonts");
	g_assert_cmpuint(json_array_get_length(fonts), ==,
		json_array_get_length(json_object_get_array_member(type, "fonts")));
	g_assert_cmpstr(json_object_get_string_member(json_array_get_object_element(fonts, 0), "family"), ==,
		json_object_get_string_member(json_array_get_object_element(json_object_get_array_member(type, "fonts"), 0), "family"));
	g_assert_true(ls_components_check_plan(components, plan, &error));
	g_assert_no_error(error);
	g_assert_true(ls_components_check_identity(components, plan, identity, &error));
	g_assert_no_error(error);
	/* No web fonts asked for: the colours change, the stacks do not. */
	g_assert_false(json_object_has_member(json_node_get_object(local), "fonts"));
	g_assert_true(ls_components_apply_seed(components, local, seed, "trades", FALSE));
	g_assert_false(json_object_has_member(json_node_get_object(local), "fonts"));
	g_assert_cmpstr(json_object_get_string_member(member(local, "tokens"), "font_display"), ==,
		"\"Avenir Next\", \"Segoe UI\", system-ui, sans-serif");
	g_assert_cmpstr(json_object_get_string_member(member(local, "tokens"), "paper"), ==,
		json_object_get_string_member(palette, "paper"));
}

/* A theme is a whole design: the seed keeps every look axis it names (the
 * builder still deals the frame it leaves out), its tokens and its fonts. */
static void
test_themed(void){
	g_autoptr(LsComponents) components = library();
	g_autoptr(JsonNode) plan = load("tests/fixtures/composed/plan.json");
	g_autoptr(JsonNode) before = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GList) axes = NULL;
	g_autofree gchar *tokens = NULL, *after_tokens = NULL;
	GList *axis;
	g_assert_true(ls_components_apply_theme(components, plan, "swiss", &error));
	g_assert_no_error(error);
	{
		g_autofree gchar *text = ls_json_encode(plan);
		before = ls_json_parse(text, NULL);
	}
	{
		g_autoptr(JsonNode) node = json_node_new(JSON_NODE_OBJECT);
		json_node_set_object(node, member(before, "tokens"));
		tokens = ls_json_encode(node);
	}
	g_assert_true(ls_components_apply_seed(components, plan, "any site\nany brief", "trades", TRUE));
	axes = json_object_get_members(member(before, "look"));
	for (axis = axes; axis != NULL; axis = axis->next)
		g_assert_cmpstr(json_object_get_string_member(member(plan, "look"), axis->data), ==,
			json_object_get_string_member(member(before, "look"), axis->data));
	{
		g_autoptr(JsonNode) node = json_node_new(JSON_NODE_OBJECT);
		json_node_set_object(node, member(plan, "tokens"));
		after_tokens = ls_json_encode(node);
	}
	g_assert_cmpstr(after_tokens, ==, tokens);
}

static gboolean
opening_shows_art(const gchar *opening){
	return !g_str_equal(opening, "hero/minimal") && !g_str_equal(opening, "poster/type") &&
		!g_str_equal(opening, "poster/stamped") && !g_str_has_prefix(opening, "manifesto/");
}

/* The home page opens with the dealt opening, whatever the model wrote,
 * keeping its words; every dealable opening is reachable and valid. */
static void
test_opening(void){
	g_autoptr(LsComponents) components = library();
	g_autoptr(JsonNode) dealt = load("tests/fixtures/composed/plan.json");
	g_autoptr(GHashTable) reached = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	const gchar *choices[] = {"hero/statement", "hero/split", "hero/banner", "hero/centered", "hero/overlap",
		"hero/minimal", "cover/full", "cover/framed", "cover/split", "poster/type", "poster/stamped",
		"poster/backdrop", "manifesto/stacked", "manifesto/numbered", "manifesto/outlined"};
	guint c;
	g_assert_true(ls_components_apply_seed(components, dealt, "harbor\nfurniture", NULL, FALSE));
	g_assert_nonnull(json_object_get_string_member(member(dealt, "structure"), "opening"));
	for (c = 0; c < G_N_ELEMENTS(choices); c++) {
		g_autoptr(JsonNode) plan = load("tests/fixtures/composed/plan.json");
		g_autoptr(JsonNode) page = load("tests/fixtures/composed/index.json");
		g_autoptr(GError) error = NULL;
		g_autofree gchar *expected = g_strdup(choices[c]);
		JsonObject *first, *content;
		g_autofree gchar *rendered = NULL;
		g_assert_true(ls_components_apply_seed(components, plan, "harbor\nfurniture", NULL, FALSE));
		json_object_set_string_member(member(plan, "structure"), "opening", choices[c]);
		/* Openings that hide a picture are only dealt to a writer without one. */
		if (!opening_shows_art(choices[c])) {
			JsonObject *section = json_array_get_object_element(
				json_object_get_array_member(json_node_get_object(page), "sections"), 0);
			json_object_remove_member(json_object_get_object_member(section, "content"), "art");
		}
		g_assert_true(ls_components_check_plan(components, plan, &error));
		g_assert_no_error(error);
		/* hero/split is what the fixture writer already chose. */
		g_assert_true(ls_components_apply_opening(components, plan, "index.html", page) == (c != 1));
		first = json_array_get_object_element(json_object_get_array_member(json_node_get_object(page), "sections"), 0);
		content = json_object_get_object_member(first, "content");
		rendered = g_strdup_printf("%s/%s", json_object_get_string_member(first, "component"),
			json_object_get_string_member(first, "variant"));
		g_assert_cmpstr(rendered, ==, expected);
		g_assert_cmpstr(json_object_get_string_member(content, "heading"), ==, "Old furniture, made useful again");
		/* A manifesto states the lede line by line. */
		if (g_str_has_prefix(expected, "manifesto/")) {
			g_autofree gchar *lines = json_to_string(json_object_get_member(content, "lines"), FALSE);
			g_assert_nonnull(strstr(lines, "We repair joints, finishes and structure so a piece can go back to work."));
		}
		g_assert_true(ls_components_check_page(components, plan, "index.html", page, &error));
		g_assert_no_error(error);
		g_hash_table_add(reached, g_steal_pointer(&rendered));
	}
	g_assert_cmpuint(g_hash_table_size(reached), ==, G_N_ELEMENTS(choices));
}

/* An opening that cannot hold the words falls back to one that can; one
 * the model chose itself (custom) and plans without an opening are kept. */
static void
test_opening_fallback(void){
	g_autoptr(LsComponents) components = library();
	g_autoptr(JsonNode) plan = load("tests/fixtures/composed/plan.json");
	g_autoptr(JsonNode) page = load("tests/fixtures/composed/index.json");
	g_autoptr(JsonNode) bare = load("tests/fixtures/composed/plan.json");
	g_autoptr(JsonNode) kept = load("tests/fixtures/composed/index.json");
	g_autoptr(GError) error = NULL;
	JsonObject *first, *content;
	g_assert_true(ls_components_apply_seed(components, plan, "harbor\nfurniture", NULL, FALSE));
	json_object_set_string_member(member(plan, "structure"), "opening", "poster/type");
	first = json_array_get_object_element(json_object_get_array_member(json_node_get_object(page), "sections"), 0);
	content = json_object_get_object_member(first, "content");
	json_object_set_string_member(content, "heading",
		"Old furniture, made useful again by careful hands in a small workshop near the harbor");
	g_assert_true(ls_components_apply_opening(components, plan, "index.html", page));
	/* The sections were replaced; read the new opening. */
	first = json_array_get_object_element(json_object_get_array_member(json_node_get_object(page), "sections"), 0);
	g_assert_cmpstr(json_object_get_string_member(first, "component"), !=, "poster");
	g_assert_true(ls_components_check_page(components, plan, "index.html", page, &error));
	g_assert_no_error(error);
	/* A picture in the opening is never hidden: a manifesto dealt to a hero
	 * with art becomes an opening that shows it, one of the site's own
	 * composition when one fits, and sites do not all land on the same one. */
	{
		g_autoptr(GHashTable) landed = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
		guint s;
		for (s = 0; s < 24; s++) {
			g_autoptr(JsonNode) pictured = load("tests/fixtures/composed/index.json");
			g_autoptr(JsonNode) dealt = load("tests/fixtures/composed/plan.json");
			g_autofree gchar *seed = g_strdup_printf("harbor %u\nfurniture", s);
			JsonObject *opening, *structure;
			const gchar *component, *variant;
			g_autofree gchar *pair = NULL;
			g_assert_true(ls_components_apply_seed(components, dealt, seed, NULL, FALSE));
			structure = member(dealt, "structure");
			json_object_set_string_member(structure, "opening", "manifesto/stacked");
			g_assert_true(ls_components_apply_opening(components, dealt, "index.html", pictured));
			opening = json_array_get_object_element(json_object_get_array_member(json_node_get_object(pictured), "sections"), 0);
			component = json_object_get_string_member(opening, "component");
			variant = json_object_get_string_member(opening, "variant");
			pair = g_strdup_printf("%s/%s", component, variant);
			g_assert_cmpstr(json_object_get_string_member(json_object_get_object_member(
				json_object_get_object_member(opening, "content"), "art"), "src"), ==, "assets/bench.svg");
			g_assert_true(ls_components_check_page(components, dealt, "index.html", pictured, NULL));
			{
				JsonArray *fitting = composition_openings(json_object_get_string_member(structure, "composition"));
				gboolean any = FALSE, listed = FALSE;
				guint f;
				for (f = 0; f < json_array_get_length(fitting); f++) {
					const gchar *choice = json_array_get_string_element(fitting, f);
					any |= !g_str_has_prefix(choice, "manifesto/") && !g_str_equal(choice, "hero/minimal") &&
						!g_str_has_prefix(choice, "poster/type") && !g_str_has_prefix(choice, "poster/stamped");
					listed |= g_str_equal(choice, pair);
				}
				if (any)
					g_assert_true(listed);
			}
			g_hash_table_add(landed, g_steal_pointer(&pair));
		}
		g_assert_cmpuint(g_hash_table_size(landed), >=, 5);
	}
	/* No structure.opening: the model's opening stays. */
	g_assert_false(ls_components_apply_opening(components, bare, "index.html", kept));
	first = json_array_get_object_element(json_object_get_array_member(json_node_get_object(kept), "sections"), 0);
	g_assert_cmpstr(json_object_get_string_member(first, "variant"), ==, "split");
}

/* Ten businesses, ten skeletons: no two of the sameness briefs share their
 * look, opening, palette and type. */
static void
test_skeletons(void){
	g_autoptr(LsComponents) components = library();
	g_autoptr(JsonNode) briefs = load("tests/fixtures/sameness/briefs.json");
	g_autoptr(GHashTable) seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	JsonArray *list = json_node_get_array(briefs);
	guint i;
	for (i = 0; i < json_array_get_length(list); i++) {
		JsonObject *brief = json_array_get_object_element(list, i);
		g_autofree gchar *seed = g_strconcat(json_object_get_string_member(brief, "slug"), "\n",
			json_object_get_string_member(brief, "brief"), NULL);
		g_autoptr(JsonNode) plan = load("tests/fixtures/sameness/plan.json");
		g_autoptr(JsonNode) look = json_node_new(JSON_NODE_OBJECT);
		g_autoptr(JsonNode) tokens = json_node_new(JSON_NODE_OBJECT);
		g_autofree gchar *look_text = NULL, *tokens_text = NULL, *skeleton = NULL;
		g_assert_true(ls_components_apply_seed(components, plan, seed, "trades", FALSE));
		json_node_set_object(look, member(plan, "look"));
		json_node_set_object(tokens, member(plan, "tokens"));
		look_text = ls_json_encode(look);
		tokens_text = ls_json_encode(tokens);
		skeleton = g_strconcat(look_text, json_object_get_string_member(member(plan, "structure"), "opening"),
			tokens_text, NULL);
		g_assert_true(g_hash_table_add(seen, g_steal_pointer(&skeleton)));
	}
}

int
main(int argc, char **argv){
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/seed/look", test_look);
	g_test_add_func("/seed/identity", test_identity);
	g_test_add_func("/seed/themed", test_themed);
	g_test_add_func("/seed/opening", test_opening);
	g_test_add_func("/seed/opening-fallback", test_opening_fallback);
	g_test_add_func("/seed/skeletons", test_skeletons);
	return g_test_run();
}
