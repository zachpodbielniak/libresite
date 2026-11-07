/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* The default design: until the hand-made templates exist, the builder sets
 * one calm design on every unthemed site, the same whatever the site is
 * called; the model writes the words. See docs/components.org, "The default
 * design". */
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

/* The catalog's default block, as loaded. */
static JsonObject *
defaults(LsComponents *components){
	return json_object_get_object_member(json_node_get_object(ls_components_get_catalog(components)), "default");
}

static gchar *
encode_member(JsonNode *plan, const gchar *name){
	g_autoptr(JsonNode) node = json_node_new(JSON_NODE_OBJECT);
	json_node_set_object(node, member(plan, name));
	return ls_json_encode(node);
}

/* Every look axis takes its default, the first value the catalog lists,
 * whatever the model wrote, and two sites with different names come out
 * with one look and one set of colours. */
static void
test_look(void){
	g_autoptr(LsComponents) components = library();
	g_autoptr(JsonNode) plan = load("tests/fixtures/composed/plan.json");
	g_autoptr(JsonNode) other = load("tests/fixtures/composed/plan.json");
	g_autoptr(GError) error = NULL;
	g_autoptr(GList) axes = NULL;
	g_autofree gchar *look = NULL, *other_look = NULL, *tokens = NULL, *other_tokens = NULL;
	JsonObject *catalog_looks = json_object_get_object_member(json_node_get_object(ls_components_get_catalog(components)),
		"looks");
	GList *axis;
	json_object_set_string_member(member(plan, "look"), "header", "glass");
	json_object_set_string_member(json_node_get_object(other), "name", "Copper & Gauge Plumbing");
	g_assert_true(ls_components_apply_default(components, plan, FALSE));
	g_assert_true(ls_components_apply_default(components, other, FALSE));
	axes = json_object_get_members(catalog_looks);
	for (axis = axes; axis != NULL; axis = axis->next) {
		JsonObject *values = json_object_get_object_member(json_object_get_object_member(catalog_looks, axis->data),
			"values");
		g_autoptr(GList) names = json_object_get_members(values);
		g_assert_cmpstr(json_object_get_string_member(member(plan, "look"), axis->data), ==, names->data);
	}
	look = encode_member(plan, "look");
	other_look = encode_member(other, "look");
	tokens = encode_member(plan, "tokens");
	other_tokens = encode_member(other, "tokens");
	g_assert_cmpstr(look, ==, other_look);
	g_assert_cmpstr(tokens, ==, other_tokens);
	g_assert_true(ls_components_check_plan(components, plan, &error));
	g_assert_no_error(error);
}

/* The colours are the default palette's and, when the plan asks for web
 * fonts, the type is the default pairing's; a plan without web fonts keeps
 * its installed stacks. */
static void
test_identity(void){
	g_autoptr(LsComponents) components = library();
	g_autoptr(JsonNode) plan = load("tests/fixtures/composed/plan.json");
	g_autoptr(JsonNode) local = load("tests/fixtures/composed/plan.json");
	g_autoptr(JsonObject) identity = ls_components_get_default_identity(components);
	g_autoptr(GError) error = NULL;
	JsonObject *palette = json_object_get_object_member(json_object_get_object_member(identity, "palette"), "tokens");
	JsonObject *type = json_object_get_object_member(identity, "type");
	JsonArray *fonts;
	g_assert_cmpstr(json_object_get_string_member(json_object_get_object_member(identity, "palette"), "name"), ==,
		json_object_get_string_member(defaults(components), "palette"));
	g_assert_cmpstr(json_object_get_string_member(type, "name"), ==,
		json_object_get_string_member(defaults(components), "typeface"));
	g_assert_cmpstr(json_object_get_string_member(json_object_get_object_member(identity, "home"), "name"), ==,
		json_object_get_string_member(defaults(components), "home"));
	json_object_set_array_member(json_node_get_object(plan), "fonts", json_array_new());
	{
		JsonObject *font = json_object_new();
		JsonArray *weights = json_array_new();
		json_array_add_int_element(weights, 700);
		json_object_set_string_member(font, "family", "Fraunces");
		json_object_set_array_member(font, "weights", weights);
		json_array_add_object_element(json_object_get_array_member(json_node_get_object(plan), "fonts"), font);
	}
	g_assert_true(ls_components_apply_default(components, plan, FALSE));
	g_assert_cmpstr(json_object_get_string_member(member(plan, "tokens"), "paper"), ==,
		json_object_get_string_member(palette, "paper"));
	g_assert_cmpstr(json_object_get_string_member(member(plan, "tokens"), "accent"), ==,
		json_object_get_string_member(palette, "accent"));
	g_assert_cmpstr(json_object_get_string_member(member(plan, "tokens"), "font_display"), ==,
		json_object_get_string_member(json_object_get_object_member(type, "tokens"), "font_display"));
	fonts = json_object_get_array_member(json_node_get_object(plan), "fonts");
	g_assert_cmpstr(json_object_get_string_member(json_array_get_object_element(fonts, 0), "family"), ==,
		json_object_get_string_member(json_array_get_object_element(json_object_get_array_member(type, "fonts"), 0),
		"family"));
	g_assert_true(ls_components_check_plan(components, plan, &error));
	g_assert_no_error(error);
	g_assert_true(ls_components_check_identity(components, plan, identity, &error));
	g_assert_no_error(error);
	g_assert_false(json_object_has_member(json_node_get_object(local), "fonts"));
	g_assert_true(ls_components_apply_default(components, local, FALSE));
	g_assert_false(json_object_has_member(json_node_get_object(local), "fonts"));
	g_assert_cmpstr(json_object_get_string_member(member(local, "tokens"), "font_display"), ==,
		"\"Avenir Next\", \"Segoe UI\", system-ui, sans-serif");
}

/* A theme is a whole design: every look axis it names and its tokens stay;
 * the builder axes it leaves open (the frame) take their defaults. */
static void
test_themed(void){
	g_autoptr(LsComponents) components = library();
	g_autoptr(JsonNode) plan = load("tests/fixtures/composed/plan.json");
	g_autoptr(GError) error = NULL;
	g_autoptr(JsonNode) before = NULL;
	g_autoptr(GList) axes = NULL;
	g_autofree gchar *tokens = NULL, *after_tokens = NULL;
	GList *axis;
	g_assert_true(ls_components_apply_theme(components, plan, "swiss", &error));
	g_assert_no_error(error);
	{
		g_autofree gchar *text = ls_json_encode(plan);
		before = ls_json_parse(text, NULL);
	}
	tokens = encode_member(plan, "tokens");
	g_assert_false(json_object_has_member(member(plan, "look"), "frame"));
	g_assert_true(ls_components_apply_default(components, plan, TRUE));
	axes = json_object_get_members(member(before, "look"));
	for (axis = axes; axis != NULL; axis = axis->next)
		g_assert_cmpstr(json_object_get_string_member(member(plan, "look"), axis->data), ==,
			json_object_get_string_member(member(before, "look"), axis->data));
	g_assert_cmpstr(json_object_get_string_member(member(plan, "look"), "frame"), ==, "top");
	after_tokens = encode_member(plan, "tokens");
	g_assert_cmpstr(after_tokens, ==, tokens);
	g_assert_cmpstr(json_object_get_string_member(member(plan, "structure"), "opening"), ==,
		json_object_get_string_member(defaults(components), "opening"));
}

/* One home opening: the default without photos, the photo opening with
 * them, and a writer's other opening is rewritten to it in its own words. */
static void
test_opening(void){
	g_autoptr(LsComponents) components = library();
	g_autoptr(JsonNode) plan = load("tests/fixtures/composed/plan.json");
	g_autoptr(JsonNode) photo_plan = load("tests/fixtures/composed/plan.json");
	g_autoptr(JsonNode) page = load("tests/fixtures/composed/index.json");
	g_autoptr(GError) error = NULL;
	JsonObject *first;
	g_assert_true(ls_components_apply_default(components, plan, FALSE));
	g_assert_cmpstr(json_object_get_string_member(member(plan, "structure"), "opening"), ==,
		json_object_get_string_member(defaults(components), "opening"));
	g_assert_cmpuint(json_object_get_size(member(plan, "structure")), ==, 1);
	json_object_set_array_member(json_node_get_object(photo_plan), "photos", json_array_new());
	{
		JsonObject *photo = json_object_new();
		json_object_set_string_member(photo, "path", "assets/photos/crew.jpg");
		json_object_set_string_member(photo, "alt", "Two plumbers fitting a boiler");
		json_object_set_int_member(photo, "width", 1600);
		json_object_set_int_member(photo, "height", 1000);
		json_array_add_object_element(json_object_get_array_member(json_node_get_object(photo_plan), "photos"), photo);
	}
	g_assert_true(ls_components_apply_default(components, photo_plan, FALSE));
	g_assert_cmpstr(json_object_get_string_member(member(photo_plan, "structure"), "opening"), ==,
		json_object_get_string_member(defaults(components), "photo_opening"));
	/* The fixture home page opens with another hero; it takes the default. */
	first = json_array_get_object_element(json_object_get_array_member(json_node_get_object(page), "sections"), 0);
	json_object_set_string_member(first, "component", "hero");
	json_object_set_string_member(first, "variant", "statement");
	if (json_object_has_member(json_object_get_object_member(first, "content"), "art"))
		json_object_remove_member(json_object_get_object_member(first, "content"), "art");
	g_assert_true(ls_components_apply_opening(components, plan, "index.html", page));
	first = json_array_get_object_element(json_object_get_array_member(json_node_get_object(page), "sections"), 0);
	g_assert_cmpstr(json_object_get_string_member(first, "component"), ==, "hero");
	g_assert_cmpstr(json_object_get_string_member(first, "variant"), ==,
		strchr(json_object_get_string_member(defaults(components), "opening"), '/') + 1);
	g_assert_true(ls_components_check_page(components, plan, "index.html", page, &error));
	g_assert_no_error(error);
}

/* The gimmick openings are gone from the library. */
static void
test_no_gimmicks(void){
	g_autoptr(LsComponents) components = library();
	JsonObject *catalog = json_node_get_object(ls_components_get_catalog(components));
	JsonObject *list = json_object_get_object_member(catalog, "components");
	JsonObject *heads = json_object_get_object_member(json_object_get_object_member(json_object_get_object_member(catalog,
		"looks"), "heads"), "values");
	g_assert_false(json_object_has_member(list, "manifesto"));
	g_assert_false(json_object_has_member(list, "poster"));
	g_assert_false(json_object_has_member(heads, "numbered"));
	g_assert_false(json_object_has_member(heads, "ghost"));
	g_assert_false(json_object_has_member(heads, "disc"));
}

/* Only the brand mark is drawn: other planned artwork is dropped. */
static void
test_art(void){
	g_autoptr(LsComponents) components = library();
	g_autoptr(JsonNode) plan = load("tests/fixtures/composed/plan.json");
	g_autoptr(JsonNode) markless = load("tests/fixtures/composed/plan.json");
	JsonArray *art;
	g_assert_cmpuint(json_array_get_length(json_object_get_array_member(json_node_get_object(plan), "art")), ==, 2);
	g_assert_true(ls_components_apply_default(components, plan, FALSE));
	art = json_object_get_array_member(json_node_get_object(plan), "art");
	g_assert_cmpuint(json_array_get_length(art), ==, 1);
	g_assert_cmpstr(json_object_get_string_member(json_array_get_object_element(art, 0), "path"), ==, "assets/mark.svg");
	json_object_remove_member(member(markless, "brand"), "mark");
	g_assert_true(ls_components_apply_default(components, markless, TRUE));
	g_assert_cmpuint(json_array_get_length(json_object_get_array_member(json_node_get_object(markless), "art")), ==, 0);
}

/* Existing sites keep the structure an older builder dealt them: the plan
 * check still accepts it, and nothing else. */
static void
test_retired_structure(void){
	g_autoptr(LsComponents) components = library();
	g_autoptr(JsonNode) plan = load("tests/fixtures/composed/plan.json");
	g_autoptr(GError) error = NULL;
	JsonObject *structure = json_object_new();
	JsonObject *variants = json_object_new();
	json_object_set_string_member(variants, "services", "ledger");
	json_object_set_double_member(structure, "width", 72.5);
	json_object_set_string_member(structure, "composition", "banded");
	json_object_set_string_member(structure, "kit", "still");
	json_object_set_string_member(structure, "direction", "framed");
	json_object_set_string_member(structure, "angle", "plain");
	json_object_set_object_member(structure, "variants", variants);
	json_object_set_string_member(structure, "opening", "hero/split");
	json_object_set_object_member(json_node_get_object(plan), "structure", structure);
	ls_components_check_plan(components, plan, &error);
	g_assert_no_error(error);
	json_object_set_string_member(structure, "palette", "harbor");
	g_assert_false(ls_components_check_plan(components, plan, &error));
	g_assert_nonnull(strstr(error->message, "structure.palette"));
	g_clear_error(&error);
	json_object_remove_member(structure, "palette");
	json_object_set_string_member(structure, "opening", "manifesto/numbered");
	g_assert_false(ls_components_check_plan(components, plan, &error));
	g_assert_nonnull(strstr(error->message, "structure.opening"));
}

/* An existing site may wear a look value that is gone, such as numbered
 * section heads: the plan repair gives it the default, and the check then
 * passes. */
static void
test_retired_look(void){
	g_autoptr(LsComponents) components = library();
	g_autoptr(JsonNode) plan = load("tests/fixtures/composed/plan.json");
	g_autoptr(GError) error = NULL;
	g_autofree gchar *repairs = NULL;
	json_object_set_string_member(member(plan, "look"), "heads", "numbered");
	g_assert_false(ls_components_check_plan(components, plan, &error));
	g_assert_nonnull(strstr(error->message, "look.heads"));
	g_clear_error(&error);
	repairs = ls_components_repair_plan(components, plan);
	g_assert_nonnull(strstr(repairs, "look.heads -> plain"));
	g_assert_cmpstr(json_object_get_string_member(member(plan, "look"), "heads"), ==, "plain");
	g_assert_true(ls_components_check_plan(components, plan, &error));
	g_assert_no_error(error);
}

/* A customer's site never says it is an example; only a demo build does. */
static void
test_demo(void){
	g_autoptr(LsComponents) components = library();
	g_autoptr(JsonNode) plan = load("tests/fixtures/composed/plan.json");
	g_autoptr(GError) error = NULL;
	json_object_set_string_member(member(plan, "footer"), "note", "Harbor & Pine is an example business.");
	g_assert_false(ls_components_check_plan(components, plan, &error));
	g_assert_nonnull(strstr(error->message, "example business"));
	g_clear_error(&error);
	json_object_set_boolean_member(json_node_get_object(plan), "demo", TRUE);
	g_assert_true(ls_components_check_plan(components, plan, &error));
	g_assert_no_error(error);
	/* Stock photographs may still be credited in the footer. */
	json_object_set_boolean_member(json_node_get_object(plan), "demo", FALSE);
	json_object_set_string_member(member(plan, "footer"), "note", "Some pictures are stock photographs.");
	g_assert_true(ls_components_check_plan(components, plan, &error));
	g_assert_no_error(error);
}

int
main(int argc, char **argv){
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/default/look", test_look);
	g_test_add_func("/default/identity", test_identity);
	g_test_add_func("/default/themed", test_themed);
	g_test_add_func("/default/opening", test_opening);
	g_test_add_func("/default/no-gimmicks", test_no_gimmicks);
	g_test_add_func("/default/art", test_art);
	g_test_add_func("/default/retired-structure", test_retired_structure);
	g_test_add_func("/default/retired-look", test_retired_look);
	g_test_add_func("/default/demo", test_demo);
	return g_test_run();
}
