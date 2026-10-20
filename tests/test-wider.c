/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* Sameness under a tenth: every scored choice has a wide pool (the wider
 * block joins its entries to the looks, openings, compositions, kits,
 * directions, section forms and palette modes), and a site built with
 * earlier sites to avoid keeps the deal furthest from the closest of them.
 * See docs/components.org, "Sameness under a tenth". */
#include "libresite.h"
#include "core/ls-components.h"
#include "core/ls-util.h"
#include <string.h>

static LsComponents *
library(void){
	g_autoptr(GError) error = NULL;
	LsComponents *components = ls_components_new(&error);
	g_assert_no_error(error);
	return components;
}

/* The fingerprint @seed deals over the fixture plan, before any model. */
static JsonObject *
dealt(LsComponents *components, const gchar *seed){
	g_autofree gchar *text = NULL;
	g_autoptr(JsonNode) deal = NULL;
	g_autoptr(JsonObject) identity = ls_components_suggest_identity(components, seed, "trades");
	JsonObject *tokens, *palette, *type;
	const gchar *keys[] = {"paper", "ink", "accent"};
	guint i;
	g_assert_true(g_file_get_contents("tests/fixtures/composed/plan.json", &text, NULL, NULL));
	deal = ls_json_parse(text, NULL);
	tokens = json_object_get_object_member(json_node_get_object(deal), "tokens");
	palette = json_object_get_object_member(json_object_get_object_member(identity, "palette"), "tokens");
	type = json_object_get_object_member(json_object_get_object_member(identity, "type"), "tokens");
	ls_components_apply_structure(components, deal, seed, FALSE);
	json_object_set_object_member(json_node_get_object(deal), "look", ls_components_suggest_look(components, seed));
	for (i = 0; i < G_N_ELEMENTS(keys); i++)
		json_object_set_string_member(tokens, keys[i], json_object_get_string_member(palette, keys[i]));
	json_object_set_string_member(tokens, "font_display", json_object_get_string_member(type, "font_display"));
	json_object_set_string_member(tokens, "font_body", json_object_get_string_member(type, "font_body"));
	return ls_components_fingerprint(components, deal, NULL);
}

static JsonObject *
path(JsonObject *root, const gchar *first, const gchar *second){
	JsonObject *block = json_object_get_object_member(root, first);
	return second != NULL ? json_object_get_object_member(block, second) : block;
}

/* The pools as loaded, each at least as wide as the plan asks. */
static void
test_pools(void){
	g_autoptr(LsComponents) components = library();
	JsonObject *root = json_node_get_object(ls_components_get_catalog(components));
	struct { const gchar *axis; guint least; } looks[] = {
		{"header", 20}, {"nav", 20}, {"heads", 20}, {"buttons", 20}, {"panels", 20}, {"footer", 20},
		{"rhythm", 12}, {"photos", 12}, {"lettering", 12}
	};
	const gchar *openers[] = {"hero", "cover", "poster", "manifesto"};
	guint i, openings = 0;
	for (i = 0; i < G_N_ELEMENTS(looks); i++) {
		JsonObject *values = json_object_get_object_member(path(root, "looks", looks[i].axis), "values");
		if (json_object_get_size(values) < looks[i].least)
			g_error("look %s has %u values, wants %u", looks[i].axis, json_object_get_size(values), looks[i].least);
	}
	for (i = 0; i < G_N_ELEMENTS(openers); i++)
		openings += json_object_get_size(json_object_get_object_member(path(root, "components", openers[i]), "variants"));
	g_assert_cmpuint(openings, >=, 21);
	g_assert_cmpuint(json_object_get_size(path(root, "compositions", "layouts")), >=, 20);
	g_assert_cmpuint(json_object_get_size(path(root, "motion_kits", "kits")), >=, 12);
	g_assert_cmpuint(json_object_get_size(path(root, "art_directions", "directions")), >=, 12);
	g_assert_cmpuint(json_object_get_size(path(root, "palette_generator", "modes")), >=, 6);
	/* Each section form family grew by its wider forms. */
	{
		JsonObject *families = path(root, "section_forms", "families");
		g_assert_cmpuint(json_array_get_length(json_object_get_array_member(families, "offer")), >=, 6);
		g_assert_cmpuint(json_array_get_length(json_object_get_array_member(families, "process")), >=, 5);
		g_assert_cmpuint(json_array_get_length(json_object_get_array_member(families, "practical")), >=, 4);
	}
}

/* Every wider value is dealt: over many seeds each joined look value,
 * composition, kit and direction turns up. */
static void
test_dealt(void){
	g_autoptr(LsComponents) components = library();
	JsonObject *root = json_node_get_object(ls_components_get_catalog(components));
	JsonObject *wider = json_object_get_object_member(root, "wider");
	g_autoptr(GHashTable) seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	const gchar *blocks[][2] = {{"layouts", "composition"}, {"kits", "kit"}, {"directions", "direction"}};
	guint s, b;
	for (s = 0; s < 3000; s++) {
		g_autofree gchar *seed = g_strdup_printf("wider-%u\nA business %u.", s, s);
		g_autoptr(JsonNode) plan = ls_json_parse("{\"look\": {}}", NULL);
		JsonObject *look, *structure;
		g_autoptr(GList) axes = NULL;
		GList *axis;
		g_assert_true(ls_components_apply_seed(components, plan, seed, "trades", FALSE));
		look = json_object_get_object_member(json_node_get_object(plan), "look");
		structure = json_object_get_object_member(json_node_get_object(plan), "structure");
		axes = json_object_get_members(look);
		for (axis = axes; axis != NULL; axis = axis->next)
			g_hash_table_add(seen, g_strdup_printf("look/%s/%s", (const gchar *)axis->data,
				json_object_get_string_member(look, axis->data)));
		for (b = 0; b < G_N_ELEMENTS(blocks); b++)
			if (json_object_has_member(structure, blocks[b][1]))
				g_hash_table_add(seen, g_strdup_printf("%s/%s", blocks[b][0],
					json_object_get_string_member(structure, blocks[b][1])));
	}
	g_assert_true(json_object_has_member(wider, "looks"));
	{
		JsonObject *looks = json_object_get_object_member(wider, "looks");
		g_autoptr(GList) axes = json_object_get_members(looks);
		GList *axis;
		for (axis = axes; axis != NULL; axis = axis->next) {
			g_autoptr(GList) values = json_object_get_members(json_object_get_object_member(looks, axis->data));
			GList *value;
			for (value = values; value != NULL; value = value->next) {
				g_autofree gchar *key = g_strdup_printf("look/%s/%s", (const gchar *)axis->data, (const gchar *)value->data);
				if (!g_hash_table_contains(seen, key))
					g_error("%s is never dealt", key);
			}
		}
	}
	for (b = 0; b < G_N_ELEMENTS(blocks); b++) {
		g_autoptr(GList) names = NULL;
		GList *name;
		g_assert_true(json_object_has_member(wider, blocks[b][0]));
		names = json_object_get_members(json_object_get_object_member(wider, blocks[b][0]));
		for (name = names; name != NULL; name = name->next) {
			g_autofree gchar *key = g_strdup_printf("%s/%s", blocks[b][0], (const gchar *)name->data);
			if (!g_hash_table_contains(seen, key))
				g_error("%s is never dealt", key);
		}
	}
}

/* A catalog whose wider block reuses a name, or names a block or family
 * that does not exist, does not load. */
static void
test_rejects(void){
	g_autoptr(LsComponents) components = library();
	g_autofree gchar *snapshot = ls_components_get_snapshot(components);
	const gchar *bad[] = {
		"  layouts:\n    split:\n      summary: Again.\n      openings: [hero/split]\n      order: [offer, process, proof, story, practical]\n",
		"  looks:\n    header:\n      bar: Again.\n",
		"  forms:\n    nowhere: [faq]\n",
		"  modes: {sepia: 10}\n",
		"  colours: {}\n"
	};
	guint i;
	for (i = 0; i < G_N_ELEMENTS(bad); i++) {
		g_autoptr(JsonNode) node = ls_json_parse(snapshot, NULL);
		JsonObject *files = json_object_get_object_member(json_node_get_object(node), "files");
		g_autofree gchar *catalog = g_strdup(json_object_get_string_member(files, "catalog.yaml"));
		g_autofree gchar *changed = NULL, *text = NULL;
		gchar *at = strstr(catalog, "\nwider:\n");
		g_autoptr(GError) error = NULL;
		g_autoptr(LsComponents) broken = NULL;
		g_assert_nonnull(at);
		at[strlen("\nwider:\n")] = '\0';
		changed = g_strconcat(catalog, bad[i], NULL);
		json_object_set_string_member(files, "catalog.yaml", changed);
		text = ls_json_encode(node);
		broken = ls_components_new_from_snapshot(text, &error);
		g_assert_null(broken);
		g_assert_error(error, LS_ERROR, LS_ERROR_INVALID);
	}
}

/* Sites built one after another in one market: each keeps, of its
 * LS_COMPONENTS_DEALS deals, the one furthest from the closest earlier
 * site, so the market spreads further than the seeds alone would. */
static void
test_steer(void){
	g_autoptr(LsComponents) components = library();
	g_autoptr(JsonArray) steered = json_array_new();
	g_autoptr(JsonArray) plain = json_array_new();
	gdouble steered_sum = 0, plain_sum = 0;
	guint i, j, pairs = 0;
	for (i = 0; i < 12; i++) {
		g_autofree gchar *seed = g_strdup_printf("market-%u\nA roofer in a small town, number %u.", i, i);
		gdouble closest = 0, own = 0;
		g_autofree gchar *chosen = ls_components_choose_seed(components, seed, "trades", steered, &closest);
		g_autofree gchar *again = ls_components_choose_seed(components, seed, "trades", steered, NULL);
		g_autoptr(JsonObject) fingerprint = dealt(components, chosen);
		g_autoptr(JsonObject) original = dealt(components, seed);
		g_assert_cmpstr(chosen, ==, again);
		for (j = 0; j < json_array_get_length(steered); j++)
			own = MAX(own, ls_components_similarity(original, json_array_get_object_element(steered, j)));
		g_assert_cmpfloat(closest, <=, own + 0.001);
		json_array_add_object_element(steered, json_object_ref(fingerprint));
		json_array_add_object_element(plain, json_object_ref(original));
	}
	for (i = 0; i < 12; i++)
		for (j = i + 1; j < 12; j++) {
			steered_sum += ls_components_similarity(json_array_get_object_element(steered, i),
				json_array_get_object_element(steered, j));
			plain_sum += ls_components_similarity(json_array_get_object_element(plain, i),
				json_array_get_object_element(plain, j));
			pairs++;
		}
	g_test_message("market similarity %.3f steered, %.3f from the seeds alone", steered_sum / pairs, plain_sum / pairs);
	g_assert_cmpfloat(steered_sum / pairs, <, plain_sum / pairs - 0.02);
}

int
main(int argc, char **argv){
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/wider/pools", test_pools);
	g_test_add_func("/wider/dealt", test_dealt);
	g_test_add_func("/wider/rejects", test_rejects);
	g_test_add_func("/wider/steer", test_steer);
	return g_test_run();
}
