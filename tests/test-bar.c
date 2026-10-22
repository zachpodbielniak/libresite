/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "libresite.h"
#include "core/ls-components.h"
#include "core/ls-util.h"
#include "core/ls-visual.h"
#include <string.h>

static JsonNode *
read_json(const gchar *path){
	g_autofree gchar *text = NULL;
	g_assert_true(g_file_get_contents(path, &text, NULL, NULL));
	return ls_json_parse(text, NULL);
}

static JsonNode *
visual_plan(void){
	JsonNode *plan = read_json("tests/fixtures/composed/plan.json");
	JsonObject *root = json_node_get_object(plan);
	g_autoptr(JsonNode) data = read_json("tests/fixtures/bar.json");
	JsonObject *extra = json_node_get_object(data);
	g_autoptr(GList) names = json_object_get_members(extra);
	GList *n;
	for (n = names; n != NULL; n = n->next)
		json_object_set_member(root, n->data, json_node_copy(json_object_get_member(extra, n->data)));
	return plan;
}

static void
test_contract(void){
	g_autoptr(GError) error = NULL;
	g_autoptr(LsComponents) library = ls_components_new(&error);
	g_autoptr(JsonNode) plan = visual_plan();
	g_assert_no_error(error);
	g_assert_true(ls_components_check_plan(library, plan, &error));
	g_assert_no_error(error);
}

/* A strict structured-output model fills fields it was told to omit with
 * null; a null css or restyle is no style, not a broken plan. */
static void
test_null_style(void){
	g_autoptr(LsComponents) library = ls_components_new(NULL);
	g_autoptr(JsonNode) plan = visual_plan();
	g_autoptr(GError) error = NULL;
	json_object_set_null_member(json_node_get_object(plan), "restyle");
	json_object_set_null_member(json_node_get_object(plan), "css");
	g_assert_true(ls_components_check_plan(library, plan, &error));
	g_assert_no_error(error);
}

static void
test_reject_untrusted(void){
	g_autoptr(LsComponents) library = ls_components_new(NULL);
	const gchar *keys[] = {"hero", "portrait", "detail", "clip", "kind", "source"};
	guint i;
	for (i = 0; i < G_N_ELEMENTS(keys); i++) {
		g_autoptr(JsonNode) plan = visual_plan();
		g_autoptr(GError) error = NULL;
		JsonObject *media = json_object_get_object_member(json_node_get_object(plan), "media");
		json_object_set_string_member(media, keys[i], i == 5 ? "" : "https://invalid.example/untrusted");
		g_assert_false(ls_components_check_plan(library, plan, &error));
		g_assert_nonnull(error);
	}
	{
		g_autoptr(JsonNode) plan = visual_plan();
		JsonObject *media = json_object_get_object_member(json_node_get_object(plan), "media");
		JsonArray *facts = json_array_new();
		JsonObject *fact = json_object_new();
		json_object_set_string_member(fact, "value", "12");
		json_object_set_string_member(fact, "label", "Installations");
		json_array_add_object_element(facts, fact);
		json_object_set_array_member(media, "facts", facts);
		g_assert_false(ls_components_check_plan(library, plan, NULL));
		json_object_set_string_member(media, "kind", "project");
		g_assert_false(ls_components_check_plan(library, plan, NULL));
		json_object_set_string_member(fact, "source", "Approved project record");
		g_assert_true(ls_components_check_plan(library, plan, NULL));
	}
}

static void
test_headline_budget(void){
	g_autoptr(LsComponents) library = ls_components_new(NULL);
	g_autoptr(JsonNode) plan = visual_plan();
	JsonObject *v = json_object_get_object_member(json_node_get_object(plan), "visual");
	json_object_set_string_member(v, "headline", "One two three four five six seven eight nine");
	g_assert_false(ls_components_check_plan(library, plan, NULL));
	json_object_set_string_member(v, "headline", "Water, working beautifully.");
	g_assert_true(ls_components_check_plan(library, plan, NULL));
}

static void
test_full_portrait_contract(void){
	g_autoptr(JsonNode) plan = visual_plan();
	g_autoptr(GError) error = NULL;
	JsonObject *root = json_node_get_object(plan);
	JsonObject *v = json_object_get_object_member(root, "visual");
	JsonObject *portrait = json_array_get_object_element(json_object_get_array_member(root, "photos"), 1);
	json_object_set_string_member(v, "layout", "full");
	g_assert_true(ls_visual_check_plan(root, NULL));
	json_object_set_int_member(portrait, "width", 2400);
	json_object_set_int_member(portrait, "height", 800);
	g_assert_false(ls_visual_check_plan(root, &error));
	g_assert_nonnull(strstr(error->message, "portrait"));
	g_clear_error(&error);
	json_object_set_string_member(v, "layout", "split");
	g_assert_true(ls_visual_check_plan(root, &error));
	g_assert_no_error(error);
}

static void
test_world_identity(void){
	const gchar *worlds[] = {"chalk", "copper", "forest", "electric", "ceramic", "oven"};
	g_autoptr(LsComponents) library = ls_components_new(NULL);
	guint i;
	for (i = 0; i < G_N_ELEMENTS(worlds); i++) {
		g_autoptr(JsonNode) plan = visual_plan();
		g_autoptr(GError) error = NULL;
		JsonObject *root = json_node_get_object(plan);
		json_object_set_string_member(json_object_get_object_member(root, "visual"), "world", worlds[i]);
		ls_components_apply_seed(library, plan, "world-identity", "trades", FALSE);
		g_test_message("world: %s", worlds[i]);
		g_assert_true(ls_components_check_plan(library, plan, &error));
		g_assert_no_error(error);
		g_assert_true(ls_components_check_identity(library, plan, NULL, &error));
		g_assert_no_error(error);
	}
}

static void
test_retry_diagnostics(void){
	const gchar *keys[] = {"idea", "world", "layout", "headline", "craft_copy"};
	const gchar *values[] = {"This internal art direction sentence exceeds the stated limit because it explains every single material and compositional decision in the whole page.", "metal", "cinematic", "One two three four five six seven eight nine", ""};
	guint i;
	for (i = 0; i < G_N_ELEMENTS(keys); i++) {
		g_autoptr(JsonNode) plan = visual_plan();
		g_autoptr(GError) error = NULL;
		JsonObject *v = json_object_get_object_member(json_node_get_object(plan), "visual");
		g_autofree gchar *field = g_strconcat("visual.", keys[i], NULL);
		json_object_set_string_member(v, keys[i], values[i]);
		g_assert_false(ls_visual_check_plan(json_node_get_object(plan), &error));
		g_assert_nonnull(strstr(error->message, field));
	}
}

static void
test_display_whitespace(void){
	g_autoptr(JsonNode) plan = visual_plan();
	JsonObject *v = json_object_get_object_member(json_node_get_object(plan), "visual");
	json_object_set_string_member(v, "headline", "  Water,   working   beautifully.  ");
	g_assert_true(ls_visual_check_plan(json_node_get_object(plan), NULL));
}

static void
test_clip_bounds(void){
	g_autoptr(JsonNode) plan = visual_plan();
	JsonObject *root = json_node_get_object(plan);
	JsonObject *media = json_object_get_object_member(root, "media");
	g_autoptr(LsContext) context = ls_context_new();
	g_autofree guchar *bytes = g_malloc0(2 * 1024 * 1024 + 1);
	guint sizes[] = {5, 32, 2 * 1024 * 1024 + 1};
	guint i;
	json_object_set_string_member(media, "clip", "assets/film.webm");
	g_assert_false(ls_visual_check_assets(root, context, NULL));
	memcpy(bytes, "\x1a\x45\xdf\xa3", 4);
	for (i = 0; i < G_N_ELEMENTS(sizes); i++) {
		g_autofree gchar *encoded = g_base64_encode(bytes, sizes[i]);
		g_autofree gchar *bundle = g_strdup_printf("{\"files\":[{\"path\":\"assets/film.webm\",\"encoding\":\"base64\",\"content\":\"%s\"}]}", encoded);
		g_assert_true(ls_context_apply(context, bundle, NULL));
		g_assert_cmpint(ls_visual_check_assets(root, context, NULL), ==, i == 1);
	}
}

static void
test_coherence(void){
	g_autoptr(JsonNode) plan = visual_plan();
	JsonObject *root = json_node_get_object(plan);
	JsonObject *look = json_object_get_object_member(root, "look");
	json_object_set_string_member(look, "buttons", "ticket");
	json_object_set_string_member(look, "edges", "torn");
	json_object_set_string_member(look, "photos", "print");
	ls_visual_apply_plan(root);
	g_assert_cmpstr(ls_string(look, "buttons"), ==, "square");
	g_assert_cmpstr(ls_string(look, "edges"), ==, "straight");
	g_assert_cmpstr(ls_string(look, "photos"), ==, "natural");
}

static void
test_opening_action(void){
	g_autoptr(JsonNode) plan = visual_plan();
	g_autoptr(JsonNode) page = read_json("tests/fixtures/composed/index.json");
	JsonObject *root = json_node_get_object(plan), *spec = json_node_get_object(page);
	JsonArray *sections = json_object_get_array_member(spec, "sections"), *actions = json_array_new();
	JsonObject *content = json_object_get_object_member(json_array_get_object_element(sections, 0), "content");
	JsonObject *action = json_object_new();
	json_object_remove_member(root, "action");
	json_object_set_string_member(action, "href", "visit.html");
	json_object_set_string_member(action, "label", "Arrange a visit");
	json_array_add_object_element(actions, action);
	json_object_set_array_member(content, "actions", actions);
	ls_visual_apply_page(root, spec);
	sections = json_object_get_array_member(spec, "sections");
	content = json_object_get_object_member(json_array_get_object_element(sections, 0), "content");
	g_assert_true(json_object_has_member(content, "actions"));
	actions = json_object_get_array_member(content, "actions");
	g_assert_cmpuint(json_array_get_length(actions), ==, 1);
	g_assert_cmpstr(ls_string(json_array_get_object_element(actions, 0), "href"), ==, "visit.html");
}

static const gchar *
bundle_file(JsonNode *bundle, const gchar *path){
	JsonArray *files = json_object_get_array_member(json_node_get_object(bundle), "files");
	guint i;
	for (i = 0; i < json_array_get_length(files); i++) {
		JsonObject *f = json_array_get_object_element(files, i);
		if (g_strcmp0(ls_string(f, "path"), path) == 0)
			return ls_string(f, "content");
	}
	return NULL;
}

static void
test_sequence(void){
	g_autoptr(LsComponents) library = ls_components_new(NULL);
	const gchar *worlds[] = {"chalk", "copper", "forest", "electric", "ceramic", "oven"};
	const gchar *layouts[] = {"full", "split", "object"};
	const gchar *routes[] = {"index", "work", "start", "journal", "visit"};
	const gchar *output = g_getenv("BAR_OUTPUT");
	guint w, l, i;
	for (w = 0; w < G_N_ELEMENTS(worlds); w++) for (l = 0; l < G_N_ELEMENTS(layouts); l++) {
		g_autoptr(JsonNode) plan = visual_plan(), bundle = NULL;
		g_autoptr(JsonObject) pages = json_object_new();
		g_autoptr(GError) error = NULL;
		g_autofree gchar *text = NULL;
		JsonObject *root = json_node_get_object(plan);
		JsonObject *v = json_object_get_object_member(root, "visual");
		JsonObject *media = json_object_get_object_member(root, "media");
		const gchar *html;
		json_object_set_string_member(json_object_get_object_member(root, "brand"), "wordmark", "Longname Plumbing Company");
		json_object_set_string_member(v, "world", worlds[w]);
		json_object_set_string_member(v, "layout", layouts[l]);
		json_object_set_string_member(media, "clip", "assets/film.webm");
		json_object_set_string_member(media, "clip_portrait", "assets/phone.webm");
		g_assert_true(ls_components_apply_seed(library, plan, "visual-sequence", "trades", FALSE));
		for (i = 0; i < G_N_ELEMENTS(routes); i++) {
			g_autofree gchar *fixture = g_strdup_printf("tests/fixtures/composed/%s.json", routes[i]);
			g_autofree gchar *route = g_strdup_printf("%s.html", routes[i]);
			JsonNode *page = read_json(fixture);
			ls_visual_apply_page(root, json_node_get_object(page));
			json_object_set_member(pages, route, page);
		}
		text = ls_components_compose(library, plan, pages, NULL, &error);
		if (error != NULL) g_error("%s", error->message);
		g_assert_nonnull(text);
		bundle = ls_json_parse(text, NULL);
		html = bundle_file(bundle, "index.html");
		g_assert_nonnull(strstr(html, "visual-sequence"));
		{
			g_autofree gchar *variant = g_strdup_printf("scene--%s", layouts[l]);
			g_assert_nonnull(strstr(html, variant));
		}
		g_assert_true(strstr(html, "id=\"outcome\"") < strstr(html, "id=\"craft\""));
		g_assert_true(strstr(html, "id=\"craft\"") < strstr(html, "id=\"proof\""));
		g_assert_nonnull(strstr(html, "data-src=\"assets/film.webm\""));
		g_assert_null(strstr(html, "<source src=\"assets/film.webm"));
		/* Drawn or stock media are disclosed in the footer, not captioned. */
		g_assert_null(strstr(html, "Fixture photographs"));
		g_assert_null(strstr(bundle_file(bundle, "work.html"), "scene__film"));
		if (output != NULL) {
			JsonArray *files = json_object_get_array_member(json_node_get_object(bundle), "files");
			for (i = 0; i < json_array_get_length(files); i++) {
				JsonObject *f = json_array_get_object_element(files, i);
				g_autofree gchar *dir = g_strdup_printf("%s/%s-%s", output, worlds[w], layouts[l]);
				g_autofree gchar *path = g_build_filename(dir, ls_string(f, "path"), NULL);
				g_autofree gchar *parent = g_path_get_dirname(path);
				g_assert_cmpint(g_mkdir_with_parents(parent, 0700), ==, 0);
				g_assert_true(g_file_set_contents(path, ls_string(f, "content"), -1, NULL));
			}
		}
	}
}

int
main(int argc, char **argv){
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/bar/asset-contract", test_contract);
	g_test_add_func("/bar/world-identity", test_world_identity);
	g_test_add_func("/bar/full-portrait-contract", test_full_portrait_contract);
	g_test_add_func("/bar/retry-diagnostics", test_retry_diagnostics);
	g_test_add_func("/bar/display-whitespace", test_display_whitespace);
	g_test_add_func("/bar/reject-untrusted", test_reject_untrusted);
	g_test_add_func("/bar/headline-budget", test_headline_budget);
	g_test_add_func("/bar/sequence", test_sequence);
	g_test_add_func("/bar/clip-bounds", test_clip_bounds);
	g_test_add_func("/bar/coherence", test_coherence);
	g_test_add_func("/bar/opening-action", test_opening_action);
	g_test_add_func("/bar/null-style", test_null_style);
	return g_test_run();
}
