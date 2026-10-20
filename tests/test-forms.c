/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* Section forms and extras: the builder deals each site the component its
 * offers, process and questions take (the writer's words carried over),
 * and extra home sections built only from the site's own words; no two of
 * the ten fixture businesses share their skeleton. See docs/components.org,
 * "Section forms and extras". */
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

static JsonObject *
block(const gchar *name){
	static JsonNode *catalog = NULL;
	if (catalog == NULL) {
		g_autofree gchar *text = NULL;
		g_assert_true(g_file_get_contents("data/components/catalog.yaml", &text, NULL, NULL));
		catalog = ls_yaml_parse(text, NULL);
	}
	return json_object_get_object_member(json_node_get_object(catalog), name);
}

static LsComponents *
library(void){
	g_autoptr(GError) error = NULL;
	LsComponents *components = ls_components_new(&error);
	g_assert_no_error(error);
	return components;
}

static JsonObject *
member(JsonNode *node, const gchar *name){
	return json_object_get_object_member(json_node_get_object(node), name);
}

static JsonObject *
section(JsonNode *page, guint index){
	return json_array_get_object_element(json_object_get_array_member(json_node_get_object(page), "sections"), index);
}

static const gchar *
component_at(JsonNode *page, guint index){
	return json_object_get_string_member(section(page, index), "component");
}

/* Every family offers at least two forms; across sites each is dealt; a
 * section becomes its site's form with the writer's words and passes the
 * page check; the same plan always converts the same way. */
static void
test_forms(void){
	g_autoptr(LsComponents) components = library();
	JsonObject *families = json_object_get_object_member(block("section_forms"), "families");
	g_autoptr(GList) names = json_object_get_members(families);
	g_autoptr(GHashTable) dealt = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	GList *name;
	guint s, total = 0;
	for (name = names; name != NULL; name = name->next) {
		g_assert_cmpuint(json_array_get_length(json_object_get_array_member(families, name->data)), >=, 2);
		total += json_array_get_length(json_object_get_array_member(families, name->data));
	}
	for (s = 0; s < 300; s++) {
		g_autofree gchar *seed = g_strdup_printf("form-%u\nA business %u.", s, s);
		g_autoptr(JsonObject) structure = ls_components_deal_structure(components, seed);
		JsonObject *forms = json_object_get_object_member(structure, "forms");
		for (name = names; name != NULL; name = name->next)
			g_hash_table_add(dealt, g_strdup_printf("%s/%s", (const gchar *)name->data,
				json_object_get_string_member(forms, name->data)));
	}
	g_assert_cmpuint(g_hash_table_size(dealt), ==, total);
	/* Offers as cards: the fixture's services section keeps its words. */
	{
		g_autoptr(JsonNode) plan = load("tests/fixtures/composed/plan.json");
		g_autoptr(JsonNode) page = ls_json_parse("{\"path\":\"index.html\",\"sections\":[{\"component\":\"hero\","
			"\"variant\":\"statement\",\"content\":{\"heading\":\"Repairs\"}},{\"component\":\"services\",\"variant\":\"rows\","
			"\"content\":{\"heading\":\"What we do\",\"items\":[{\"name\":\"Chairs\",\"summary\":\"Loose joints made firm.\"},"
			"{\"name\":\"Tables\",\"summary\":\"Wobbles fixed.\"},{\"name\":\"Cabinets\",\"summary\":\"Doors hung true.\"}]}},"
			"{\"component\":\"faq\",\"variant\":\"list\",\"content\":{\"heading\":\"Questions\",\"items\":[{\"question\":"
			"\"Do you collect?\",\"answer\":\"Yes, nearby.\"}]}}]}", NULL);
		g_autoptr(JsonNode) again = NULL;
		g_autoptr(GError) error = NULL;
		JsonObject *forms;
		JsonArray *items;
		{
			g_autofree gchar *copy = ls_json_encode(page);
			again = ls_json_parse(copy, NULL);
		}
		g_assert_true(ls_components_apply_seed(components, plan, "forms\nbrief", NULL, FALSE));
		forms = json_object_get_object_member(member(plan, "structure"), "forms");
		json_object_set_string_member(forms, "offer", "cards");
		json_object_set_string_member(forms, "practical", "details");
		g_assert_true(ls_components_apply_forms(components, plan, "index.html", page));
		g_assert_cmpstr(component_at(page, 0), ==, "hero");
		g_assert_cmpstr(component_at(page, 1), ==, "cards");
		g_assert_cmpstr(component_at(page, 2), ==, "details");
		items = json_object_get_array_member(json_object_get_object_member(section(page, 1), "content"), "items");
		g_assert_cmpstr(json_object_get_string_member(json_array_get_object_element(items, 0), "title"), ==, "Chairs");
		g_assert_cmpstr(json_object_get_string_member(json_array_get_object_element(items, 0), "body"), ==, "Loose joints made firm.");
		g_assert_cmpstr(json_object_get_string_member(json_object_get_object_member(section(page, 1), "content"), "heading"), ==, "What we do");
		g_assert_true(ls_components_check_page(components, plan, "index.html", page, &error));
		g_assert_no_error(error);
		/* Converting again changes nothing: the page already has its forms. */
		g_assert_false(ls_components_apply_forms(components, plan, "index.html", page));
		/* Another dealt form takes the same words. */
		json_object_set_string_member(forms, "offer", "bento");
		g_assert_true(ls_components_apply_forms(components, plan, "index.html", again));
		g_assert_cmpstr(component_at(again, 1), ==, "bento");
	}
}

/* Extras come only from the site's own words: a band of its offers'
 * names, its tagline as a statement, links to its own pages. */
static void
test_extras(void){
	g_autoptr(LsComponents) components = library();
	g_autoptr(JsonNode) plan = load("tests/fixtures/composed/plan.json");
	g_autoptr(JsonNode) page = ls_json_parse("{\"path\":\"index.html\",\"sections\":[{\"component\":\"hero\","
		"\"variant\":\"statement\",\"content\":{\"heading\":\"Repairs\"}},{\"component\":\"services\",\"variant\":\"rows\","
		"\"content\":{\"heading\":\"What we do\",\"items\":[{\"name\":\"Chairs\"},{\"name\":\"Tables\"},{\"name\":\"Cabinets\"}]}},"
		"{\"component\":\"cta\",\"variant\":\"band\",\"content\":{\"heading\":\"Start\",\"actions\":[{\"href\":\"start.html\","
		"\"label\":\"Start\"}]}}]}", NULL);
	g_autoptr(GError) error = NULL;
	JsonArray *extras, *sections;
	guint i;
	gboolean marquee = FALSE, statement = FALSE, links = FALSE;
	g_assert_true(ls_components_apply_seed(components, plan, "extras\nbrief", NULL, FALSE));
	extras = json_array_new();
	json_array_add_string_element(extras, "marquee");
	json_array_add_string_element(extras, "statement");
	json_array_add_string_element(extras, "links");
	json_object_set_array_member(member(plan, "structure"), "extras", extras);
	g_assert_true(ls_components_apply_extras(components, plan, page));
	sections = json_object_get_array_member(json_node_get_object(page), "sections");
	g_assert_cmpuint(json_array_get_length(sections), ==, 6);
	g_assert_cmpstr(component_at(page, 0), ==, "hero");
	for (i = 0; i < json_array_get_length(sections); i++) {
		JsonObject *content = json_object_get_object_member(section(page, i), "content");
		const gchar *component = component_at(page, i);
		if (g_str_equal(component, "marquee")) {
			JsonArray *items = json_object_get_array_member(content, "items");
			marquee = TRUE;
			g_assert_cmpstr(json_object_get_string_member(json_array_get_object_element(items, 0), "text"), ==, "Chairs");
		} else if (g_str_equal(component, "statement")) {
			statement = TRUE;
			g_assert_cmpstr(json_object_get_string_member(content, "text"), ==, "Repair, restore, return");
		} else if (g_str_equal(component, "links")) {
			JsonArray *items = json_object_get_array_member(content, "items");
			guint l;
			links = TRUE;
			g_assert_cmpuint(json_array_get_length(items), >=, 2);
			for (l = 0; l < json_array_get_length(items); l++)
				g_assert_cmpstr(json_object_get_string_member(json_object_get_object_member(
					json_array_get_object_element(items, l), "link"), "href"), !=, "index.html");
		}
	}
	g_assert_true(marquee && statement && links);
	g_assert_true(ls_components_check_page(components, plan, "index.html", page, &error));
	g_assert_no_error(error);
	/* Twice is once. */
	g_assert_false(ls_components_apply_extras(components, plan, page));
}

/* No two of the ten fixture businesses share opening, composition,
 * palette and type, nor composition, home sequence and dealt variants. */
static void
test_unique(void){
	g_autoptr(LsComponents) components = library();
	g_autoptr(JsonNode) briefs = load("tests/fixtures/sameness/briefs.json");
	g_autoptr(GHashTable) identities = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	g_autoptr(GHashTable) skeletons = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	JsonArray *list = json_node_get_array(briefs);
	guint i, s;
	for (i = 0; i < json_array_get_length(list); i++) {
		JsonObject *brief = json_array_get_object_element(list, i);
		g_autofree gchar *seed = g_strconcat(json_object_get_string_member(brief, "slug"), "\n",
			json_object_get_string_member(brief, "brief"), NULL);
		g_autoptr(JsonObject) identity = ls_components_suggest_identity(components, seed, "trades");
		g_autoptr(JsonNode) plan = load("tests/fixtures/sameness/plan.json");
		g_autoptr(JsonNode) page = load("tests/fixtures/sameness/index.json");
		g_autoptr(JsonNode) variants = json_node_new(JSON_NODE_OBJECT);
		g_autofree gchar *variant_text = NULL;
		JsonObject *structure;
		GString *sequence = g_string_new(NULL);
		g_assert_true(ls_components_apply_seed(components, plan, seed, "trades", FALSE));
		structure = member(plan, "structure");
		ls_components_apply_opening(components, plan, "index.html", page);
		ls_components_apply_forms(components, plan, "index.html", page);
		ls_components_apply_extras(components, plan, page);
		ls_components_apply_composition(components, plan, page);
		for (s = 0; s < json_array_get_length(json_object_get_array_member(json_node_get_object(page), "sections")); s++)
			g_string_append_printf(sequence, "%s ", component_at(page, s));
		json_node_set_object(variants, json_object_get_object_member(structure, "variants"));
		variant_text = ls_json_encode(variants);
		g_assert_true(g_hash_table_add(identities, g_strdup_printf("%s|%s|%s|%s",
			json_object_get_string_member(structure, "opening"), json_object_get_string_member(structure, "composition"),
			json_object_get_string_member(json_object_get_object_member(identity, "palette"), "name"),
			json_object_get_string_member(json_object_get_object_member(identity, "type"), "name"))));
		g_assert_true(g_hash_table_add(skeletons, g_strdup_printf("%s|%s|%s",
			json_object_get_string_member(structure, "composition"), sequence->str, variant_text)));
		g_string_free(sequence, TRUE);
	}
}

int
main(int argc, char **argv){
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/forms/forms", test_forms);
	g_test_add_func("/forms/extras", test_extras);
	g_test_add_func("/forms/unique", test_unique);
	return g_test_run();
}
