/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* Type and colour range: a curated library of font pairings dealt per
 * sector, and palettes generated from each site's seed with a contrast
 * guard. See docs/components.org, "Type and colour range". */
#include "libresite.h"
#include "core/ls-components.h"
#include "core/ls-util.h"
#include <math.h>
#include <string.h>

static LsComponents *
library(void){
	g_autoptr(GError) error = NULL;
	LsComponents *components = ls_components_new(&error);
	g_assert_no_error(error);
	return components;
}

static JsonNode *
yaml(const gchar *path){
	g_autofree gchar *text = NULL;
	g_assert_true(g_file_get_contents(path, &text, NULL, NULL));
	return ls_yaml_parse(text, NULL);
}

static JsonNode *
load(const gchar *path){
	g_autofree gchar *text = NULL;
	g_assert_true(g_file_get_contents(path, &text, NULL, NULL));
	return ls_json_parse(text, NULL);
}

static gdouble
channel(const gchar *hex, guint i){
	gchar pair[3] = {hex[1 + 2 * i], hex[2 + 2 * i], 0};
	gdouble c = g_ascii_strtoull(pair, NULL, 16) / 255.0;
	return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}

static gdouble
relative(const gchar *hex){
	return 0.2126 * channel(hex, 0) + 0.7152 * channel(hex, 1) + 0.0722 * channel(hex, 2);
}

static gdouble
ratio(const gchar *a, const gchar *b){
	gdouble x = relative(a), y = relative(b);
	return (MAX(x, y) + 0.05) / (MIN(x, y) + 0.05);
}

/* The hue of a colour in degrees, 0 to 360. */
static gdouble
hue(const gchar *hex){
	gdouble r, g, b, max, min, h;
	gchar pair[3] = {0, 0, 0};
	pair[0] = hex[1]; pair[1] = hex[2]; r = g_ascii_strtoull(pair, NULL, 16) / 255.0;
	pair[0] = hex[3]; pair[1] = hex[4]; g = g_ascii_strtoull(pair, NULL, 16) / 255.0;
	pair[0] = hex[5]; pair[1] = hex[6]; b = g_ascii_strtoull(pair, NULL, 16) / 255.0;
	max = MAX(r, MAX(g, b));
	min = MIN(r, MIN(g, b));
	if (max == min)
		return 0;
	if (max == r)
		h = fmod((g - b) / (max - min), 6);
	else if (max == g)
		h = (b - r) / (max - min) + 2;
	else
		h = (r - g) / (max - min) + 4;
	h *= 60;
	return h < 0 ? h + 360 : h;
}

/* The library offers at least eight pairings to every sector, 40 in all,
 * and every pairing loads only open families it names first in a stack. */
static void
test_pairings(void){
	g_autoptr(JsonNode) identities = yaml("data/components/identities.yaml");
	g_autoptr(JsonNode) pairings = yaml("data/components/pairings.yaml");
	JsonObject *typefaces = json_object_get_object_member(json_node_get_object(identities), "typefaces");
	JsonObject *added = json_object_get_object_member(json_node_get_object(pairings), "pairings");
	JsonObject *sectors = json_object_get_object_member(json_node_get_object(identities), "sectors");
	g_autoptr(GList) names = json_object_get_members(sectors);
	g_autoptr(GList) entries = json_object_get_members(added);
	GList *name, *entry;
	g_assert_cmpuint(json_object_get_size(typefaces) + json_object_get_size(added), >=, 40);
	for (entry = entries; entry != NULL; entry = entry->next) {
		JsonObject *pairing = json_object_get_object_member(added, entry->data);
		JsonArray *fonts = json_object_get_array_member(pairing, "fonts");
		JsonObject *tokens = json_object_get_object_member(pairing, "tokens");
		guint i;
		g_assert_false(json_object_has_member(typefaces, entry->data));
		g_assert_cmpuint(json_array_get_length(fonts), <=, 3);
		for (i = 0; i < json_array_get_length(fonts); i++) {
			const gchar *family = json_object_get_string_member(json_array_get_object_element(fonts, i), "family");
			g_autofree gchar *quoted = g_strdup_printf("'%s'", family);
			const gchar *display = json_object_get_string_member(tokens, "font_display");
			const gchar *body = json_object_get_string_member(tokens, "font_body");
			g_assert_true(g_str_has_prefix(display, quoted) || g_str_has_prefix(display, family) ||
				g_str_has_prefix(body, quoted) || g_str_has_prefix(body, family));
			g_assert_null(strstr(family, "Fraunces"));
		}
	}
	for (name = names; name != NULL; name = name->next) {
		guint count = 0;
		JsonObject *groups[2] = {typefaces, added};
		guint g;
		for (g = 0; g < 2; g++) {
			g_autoptr(GList) all = json_object_get_members(groups[g]);
			GList *item;
			for (item = all; item != NULL; item = item->next) {
				JsonArray *fits = json_object_get_array_member(json_object_get_object_member(groups[g], item->data), "fits");
				guint i;
				for (i = 0; i < json_array_get_length(fits); i++)
					count += g_strcmp0(json_array_get_string_element(fits, i), name->data) == 0;
			}
		}
		if (count < 8)
			g_error("sector %s has only %u pairings", (const gchar *)name->data, count);
	}
}

/* Generated palettes: most sites get one, every text pair reaches AA, the
 * house default never appears, the same seed gives the same palette, and
 * the palettes spread across the wheel and across light, dark and tinted. */
static void
test_generated(void){
	g_autoptr(LsComponents) components = library();
	g_auto(GStrv) sectors = NULL;
	guint i, generated = 0, total = 0, dark = 0, light = 0, hues[12] = {0}, covered = 0;
	g_autoptr(GHashTable) seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	sectors = ls_components_list_sectors(components);
	for (i = 0; i < 1600; i++) {
		const gchar *sector = sectors[i % g_strv_length(sectors)];
		g_autofree gchar *seed = g_strdup_printf("palette-%u\nAn illustrative business number %u.", i, i);
		g_autoptr(JsonObject) identity = ls_components_suggest_identity(components, seed, sector);
		g_autoptr(JsonObject) again = ls_components_suggest_identity(components, seed, sector);
		JsonObject *palette = json_object_get_object_member(identity, "palette");
		JsonObject *tokens = json_object_get_object_member(palette, "tokens");
		g_autofree gchar *first = NULL, *second = NULL;
		const gchar *pairs[][2] = {{"ink", "paper"}, {"ink", "surface"}, {"muted", "paper"}, {"muted", "surface"},
			{"accent_ink", "accent"}};
		guint p;
		{
			g_autoptr(JsonNode) a = json_node_new(JSON_NODE_OBJECT), b = json_node_new(JSON_NODE_OBJECT);
			json_node_set_object(a, palette);
			json_node_set_object(b, json_object_get_object_member(again, "palette"));
			first = ls_json_encode(a);
			second = ls_json_encode(b);
		}
		g_assert_cmpstr(first, ==, second);
		total++;
		if (!g_str_has_prefix(json_object_get_string_member(palette, "name"), "generated-"))
			continue;
		generated++;
		g_assert_nonnull(json_object_get_string_member(palette, "summary"));
		g_assert_cmpuint(json_object_get_size(tokens), ==, 7);
		for (p = 0; p < G_N_ELEMENTS(pairs); p++) {
			gdouble r = ratio(json_object_get_string_member(tokens, pairs[p][0]),
				json_object_get_string_member(tokens, pairs[p][1]));
			if (r < 4.5)
				g_error("%s: %s on %s is %.2f:1", first, pairs[p][0], pairs[p][1], r);
		}
		/* A plan wearing it passes the plan checks and is its own identity. */
		{
			g_autoptr(JsonNode) plan = load("tests/fixtures/composed/plan.json");
			g_autoptr(GError) error = NULL;
			g_assert_true(ls_components_apply_seed(components, plan, seed, sector, FALSE));
			if (!ls_components_check_plan(components, plan, &error))
				g_error("%s: %s", first, error->message);
			g_assert_true(ls_components_check_identity(components, plan, identity, &error));
		}
		if (relative(json_object_get_string_member(tokens, "paper")) < 0.2)
			dark++;
		else
			light++;
		hues[(guint)(hue(json_object_get_string_member(tokens, "accent")) / 30) % 12]++;
		g_hash_table_add(seen, g_strdup(first));
	}
	/* About 80 percent generated, as the catalog asks. */
	g_assert_cmpuint(generated * 100, >=, total * 70);
	g_assert_cmpuint(generated * 100, <=, total * 90);
	g_assert_cmpuint(g_hash_table_size(seen), ==, generated);
	g_assert_cmpuint(dark * 100, >=, generated * 20);
	g_assert_cmpuint(light * 100, >=, generated * 50);
	for (i = 0; i < 12; i++)
		covered += hues[i] > generated / 40;
	g_assert_cmpuint(covered, ==, 12);
}

/* Ten home-services businesses: ten different palettes and at least seven
 * different pairings. */
static void
test_briefs(void){
	g_autoptr(LsComponents) components = library();
	g_autoptr(JsonNode) briefs = load("tests/fixtures/sameness/briefs.json");
	g_autoptr(GHashTable) palettes = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	g_autoptr(GHashTable) types = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	JsonArray *list = json_node_get_array(briefs);
	guint i;
	for (i = 0; i < json_array_get_length(list); i++) {
		JsonObject *brief = json_array_get_object_element(list, i);
		g_autofree gchar *seed = g_strconcat(json_object_get_string_member(brief, "slug"), "\n",
			json_object_get_string_member(brief, "brief"), NULL);
		g_autoptr(JsonObject) identity = ls_components_suggest_identity(components, seed, "trades");
		g_hash_table_add(palettes, g_strdup(json_object_get_string_member(json_object_get_object_member(identity, "palette"), "name")));
		g_hash_table_add(types, g_strdup(json_object_get_string_member(json_object_get_object_member(identity, "type"), "name")));
	}
	g_assert_cmpuint(g_hash_table_size(palettes), ==, 10);
	g_assert_cmpuint(g_hash_table_size(types), >=, 7);
}

int
main(int argc, char **argv){
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/type-colour/pairings", test_pairings);
	g_test_add_func("/type-colour/generated", test_generated);
	g_test_add_func("/type-colour/briefs", test_briefs);
	return g_test_run();
}
