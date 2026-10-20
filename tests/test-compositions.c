/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* Home compositions: the builder deals each site one of the catalog's home
 * layouts, which narrows its opening, sets the builder axes it names, adds
 * its own styles to the home page and orders the home sections by role.
 * See docs/components.org, "Home compositions". */
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

/* The catalog's compositions block, read straight from the source. */
static JsonObject *
layouts(void){
	static JsonNode *catalog = NULL;
	if (catalog == NULL) {
		g_autofree gchar *text = NULL;
		g_assert_true(g_file_get_contents("data/components/catalog.yaml", &text, NULL, NULL));
		catalog = ls_yaml_parse(text, NULL);
		g_assert_nonnull(catalog);
	}
	return json_object_get_object_member(json_object_get_object_member(json_node_get_object(catalog),
		"compositions"), "layouts");
}

static JsonObject *
member(JsonNode *plan, const gchar *name){
	return json_object_get_object_member(json_node_get_object(plan), name);
}

static gboolean
listed(JsonArray *list, const gchar *name){
	guint i;
	for (i = 0; list != NULL && i < json_array_get_length(list); i++)
		if (g_strcmp0(json_array_get_string_element(list, i), name) == 0)
			return TRUE;
	return FALSE;
}

/* At least eight compositions, every one dealt across sites, each with its
 * own opening, never with a frame it avoids, its look set, and the same
 * seed dealing the same composition. */
static void
test_deal(void){
	g_autoptr(LsComponents) components = library();
	g_autoptr(GHashTable) dealt = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	guint i;
	g_assert_cmpuint(json_object_get_size(layouts()), >=, 8);
	for (i = 0; i < 400; i++) {
		g_autofree gchar *seed = g_strdup_printf("site-%u\nAn illustrative business number %u.", i, i);
		g_autoptr(JsonNode) plan = load("tests/fixtures/composed/plan.json");
		g_autoptr(JsonNode) again = load("tests/fixtures/composed/plan.json");
		g_autoptr(GError) error = NULL;
		g_autofree gchar *suggested = ls_components_suggest_opening(components, seed);
		JsonObject *structure, *layout, *look;
		const gchar *composition;
		g_assert_true(ls_components_apply_seed(components, plan, seed, "trades", FALSE));
		g_assert_true(ls_components_apply_seed(components, again, seed, "trades", FALSE));
		structure = member(plan, "structure");
		look = member(plan, "look");
		composition = json_object_get_string_member(structure, "composition");
		g_assert_cmpstr(composition, ==, json_object_get_string_member(member(again, "structure"), "composition"));
		layout = json_object_get_object_member(layouts(), composition);
		g_assert_nonnull(layout);
		/* The plan prompt's suggestion is the opening the builder applies. */
		g_assert_cmpstr(json_object_get_string_member(structure, "opening"), ==, suggested);
		g_assert_true(listed(json_object_get_array_member(layout, "openings"), suggested));
		if (json_object_has_member(layout, "avoid"))
			g_assert_false(listed(json_object_get_array_member(layout, "avoid"),
				json_object_get_string_member(look, "frame")));
		if (json_object_has_member(layout, "look")) {
			JsonObject *wanted = json_object_get_object_member(layout, "look");
			g_autoptr(GList) axes = json_object_get_members(wanted);
			GList *axis;
			for (axis = axes; axis != NULL; axis = axis->next)
				g_assert_cmpstr(json_object_get_string_member(look, axis->data), ==,
					json_object_get_string_member(wanted, axis->data));
		}
		g_assert_true(ls_components_check_plan(components, plan, &error));
		g_assert_no_error(error);
		g_hash_table_add(dealt, g_strdup(composition));
	}
	g_assert_cmpuint(g_hash_table_size(dealt), ==, json_object_get_size(layouts()));
}

/* A theme keeps its frame: a composition that avoids it is re-dealt. */
static void
test_themed(void){
	g_autoptr(LsComponents) components = library();
	guint i;
	for (i = 0; i < 60; i++) {
		g_autofree gchar *seed = g_strdup_printf("themed-%u\nA themed site.", i);
		g_autoptr(JsonNode) plan = load("tests/fixtures/composed/plan.json");
		g_autoptr(GError) error = NULL;
		JsonObject *layout;
		g_assert_true(ls_components_apply_theme(components, plan, "swiss", &error));
		g_assert_no_error(error);
		json_object_set_string_member(member(plan, "look"), "frame", "center");
		g_assert_true(ls_components_apply_seed(components, plan, seed, NULL, TRUE));
		g_assert_cmpstr(json_object_get_string_member(member(plan, "look"), "frame"), ==, "center");
		layout = json_object_get_object_member(layouts(),
			json_object_get_string_member(member(plan, "structure"), "composition"));
		if (json_object_has_member(layout, "avoid"))
			g_assert_false(listed(json_object_get_array_member(layout, "avoid"), "center"));
	}
}

static JsonNode *
home_with(const gchar *const *components){
	JsonNode *page = json_node_new(JSON_NODE_OBJECT);
	JsonObject *root = json_object_new();
	JsonArray *sections = json_array_new();
	guint i;
	for (i = 0; components[i] != NULL; i++) {
		JsonObject *section = json_object_new();
		json_object_set_string_member(section, "component", components[i]);
		json_object_set_object_member(section, "content", json_object_new());
		json_array_add_object_element(sections, section);
	}
	json_object_set_string_member(root, "path", "index.html");
	json_object_set_array_member(root, "sections", sections);
	json_node_take_object(page, root);
	return page;
}

static gchar *
order_of(JsonNode *page){
	JsonArray *sections = json_object_get_array_member(json_node_get_object(page), "sections");
	GString *out = g_string_new(NULL);
	guint i;
	for (i = 0; i < json_array_get_length(sections); i++)
		g_string_append_printf(out, "%s%s", i > 0 ? " " : "",
			json_object_get_string_member(json_array_get_object_element(sections, i), "component"));
	return g_string_free(out, FALSE);
}

/* Each composition orders the same home by its roles: the opening stays
 * first, closing sections end it, and the compositions disagree. */
static void
test_order(void){
	g_autoptr(LsComponents) components = library();
	const gchar *const home[] = {"hero", "services", "steps", "numbers", "text", "faq", "cta", NULL};
	const gchar *const roles[] = {NULL, "offer", "process", "proof", "story", "practical", NULL};
	g_autoptr(GHashTable) orders = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	g_autoptr(GList) names = json_object_get_members(layouts());
	GList *name;
	for (name = names; name != NULL; name = name->next) {
		g_autoptr(JsonNode) plan = load("tests/fixtures/composed/plan.json");
		g_autoptr(JsonNode) page = home_with(home);
		g_autoptr(JsonNode) again = home_with(home);
		JsonArray *wanted = json_object_get_array_member(json_object_get_object_member(layouts(), name->data), "order");
		JsonArray *sections;
		g_autofree gchar *first = NULL, *second = NULL;
		guint i, j;
		g_assert_true(ls_components_apply_seed(components, plan, "order\nbrief", NULL, FALSE));
		json_object_set_string_member(member(plan, "structure"), "composition", name->data);
		ls_components_apply_composition(components, plan, page);
		ls_components_apply_composition(components, plan, again);
		first = order_of(page);
		second = order_of(again);
		g_assert_cmpstr(first, ==, second);
		sections = json_object_get_array_member(json_node_get_object(page), "sections");
		g_assert_cmpuint(json_array_get_length(sections), ==, 7);
		g_assert_cmpstr(json_object_get_string_member(json_array_get_object_element(sections, 0), "component"), ==, "hero");
		g_assert_cmpstr(json_object_get_string_member(json_array_get_object_element(sections, 6), "component"), ==, "cta");
		/* One section per role here, so the order is the composition's. */
		for (i = 0; i < json_array_get_length(wanted); i++) {
			const gchar *component = json_object_get_string_member(json_array_get_object_element(sections, i + 1), "component");
			for (j = 1; roles[j] != NULL; j++)
				if (g_str_equal(home[j], component))
					g_assert_cmpstr(roles[j], ==, json_array_get_string_element(wanted, i));
		}
		g_hash_table_add(orders, g_steal_pointer(&first));
	}
	g_assert_cmpuint(g_hash_table_size(orders), ==, json_object_get_size(layouts()));
}

/* Two sections of one component are kept apart when another of their role
 * can sit between them; plans without a composition are left alone. */
static void
test_apart(void){
	g_autoptr(LsComponents) components = library();
	const gchar *const home[] = {"hero", "services", "services", "cards", "cta", NULL};
	g_autoptr(JsonNode) plan = load("tests/fixtures/composed/plan.json");
	g_autoptr(JsonNode) bare = load("tests/fixtures/composed/plan.json");
	g_autoptr(JsonNode) page = home_with(home);
	g_autoptr(JsonNode) kept = home_with(home);
	g_autofree gchar *order = NULL, *untouched = NULL;
	g_assert_true(ls_components_apply_seed(components, plan, "apart\nbrief", NULL, FALSE));
	ls_components_apply_composition(components, plan, page);
	order = order_of(page);
	g_assert_cmpstr(order, ==, "hero services cards services cta");
	g_assert_false(ls_components_apply_composition(components, bare, kept));
	untouched = order_of(kept);
	g_assert_cmpstr(untouched, ==, "hero services services cards cta");
}

static const gchar *
bundle_file(JsonNode *bundle, const gchar *path){
	JsonArray *files = json_object_get_array_member(json_node_get_object(bundle), "files");
	guint i;
	for (i = 0; i < json_array_get_length(files); i++) {
		JsonObject *file = json_array_get_object_element(files, i);
		if (g_strcmp0(json_object_get_string_member(file, "path"), path) == 0)
			return json_object_get_string_member(file, "content");
	}
	return NULL;
}

/* Rendered, the eight are eight different home pages: the home page alone
 * carries its composition's class and styles, a side rail adds a contents
 * column, and no two compositions render the same home. */
static void
test_render(void){
	g_autoptr(LsComponents) components = library();
	g_autoptr(GHashTable) homes = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	g_autoptr(GList) names = json_object_get_members(layouts());
	const gchar *routes[] = {"index", "work", "start", "journal", "visit"};
	GList *name;
	guint i;
	for (name = names; name != NULL; name = name->next) {
		JsonObject *layout = json_object_get_object_member(layouts(), name->data);
		g_autoptr(JsonNode) plan = load("tests/fixtures/composed/plan.json");
		g_autoptr(JsonObject) pages = json_object_new();
		g_autoptr(JsonNode) bundle = NULL;
		g_autoptr(GError) error = NULL;
		g_autofree gchar *text = NULL, *wanted = NULL, *css_marker = NULL;
		const gchar *index, *work, *css;
		gchar *body;
		g_assert_true(ls_components_apply_seed(components, plan, "render\nbrief", NULL, FALSE));
		json_object_set_string_member(member(plan, "structure"), "composition", name->data);
		json_object_set_string_member(member(plan, "look"), "frame", "top");
		if (json_object_has_member(layout, "look")) {
			g_autoptr(GList) axes = json_object_get_members(json_object_get_object_member(layout, "look"));
			GList *axis;
			for (axis = axes; axis != NULL; axis = axis->next)
				json_object_set_string_member(member(plan, "look"), axis->data,
					json_object_get_string_member(json_object_get_object_member(layout, "look"), axis->data));
		}
		for (i = 0; i < G_N_ELEMENTS(routes); i++) {
			g_autofree gchar *fixture = g_strdup_printf("tests/fixtures/composed/%s.json", routes[i]);
			g_autofree gchar *route = g_strdup_printf("%s.html", routes[i]);
			JsonNode *page = load(fixture);
			if (i == 0) {
				/* Each composition opens with its own first opening, under a
				 * heading with long words. */
				JsonObject *hero = json_array_get_object_element(
					json_object_get_array_member(json_node_get_object(page), "sections"), 0);
				json_object_set_string_member(json_object_get_object_member(hero, "content"), "heading",
					"Dependable furniture restoration");
				json_object_set_string_member(member(plan, "structure"), "opening",
					json_array_get_string_element(json_object_get_array_member(layout, "openings"), 0));
				ls_components_apply_opening(components, plan, route, page);
				ls_components_apply_composition(components, plan, page);
			}
			json_object_set_member(pages, route, page);
		}
		text = ls_components_compose(components, plan, pages, NULL, &error);
		g_assert_no_error(error);
		bundle = ls_json_parse(text, NULL);
		index = bundle_file(bundle, "index.html");
		work = bundle_file(bundle, "work.html");
		css = bundle_file(bundle, "styles.css");
		wanted = g_strdup_printf(" home-%s\"", (const gchar *)name->data);
		g_assert_nonnull(strstr(index, wanted));
		g_assert_null(strstr(work, " home-"));
		css_marker = g_strdup_printf(".home-%s", (const gchar *)name->data);
		if (json_object_has_member(layout, "css") && json_object_get_boolean_member(layout, "css"))
			g_assert_nonnull(strstr(css, css_marker));
		else
			g_assert_null(strstr(css, "/* home composition */"));
		if (json_object_has_member(layout, "sidebar"))
			g_assert_nonnull(strstr(index, "page-aside"));
		else
			g_assert_null(strstr(index, "page-aside"));
		/* With COMPOSITIONS_OUTPUT, the site is written for the browser check
		 * (tests/compositions.sh). */
		if (g_getenv("COMPOSITIONS_OUTPUT") != NULL) {
			JsonArray *files = json_object_get_array_member(json_node_get_object(bundle), "files");
			const gchar *art[] = {"bench.svg", "mark.svg"};
			for (i = 0; i < json_array_get_length(files); i++) {
				JsonObject *file = json_array_get_object_element(files, i);
				g_autofree gchar *out = g_build_filename(g_getenv("COMPOSITIONS_OUTPUT"), name->data,
					json_object_get_string_member(file, "path"), NULL);
				g_autofree gchar *parent = g_path_get_dirname(out);
				g_assert_cmpint(g_mkdir_with_parents(parent, 0700), ==, 0);
				g_assert_true(g_file_set_contents(out, json_object_get_string_member(file, "content"), -1, NULL));
			}
			for (i = 0; i < G_N_ELEMENTS(art); i++) {
				g_autofree gchar *source = g_build_filename("tests/fixtures/composed", art[i], NULL);
				g_autofree gchar *out = g_build_filename(g_getenv("COMPOSITIONS_OUTPUT"), name->data, "assets", art[i], NULL);
				g_autofree gchar *svg = NULL;
				g_autofree gchar *parent = g_path_get_dirname(out);
				g_assert_true(g_file_get_contents(source, &svg, NULL, NULL));
				g_assert_cmpint(g_mkdir_with_parents(parent, 0700), ==, 0);
				g_assert_true(g_file_set_contents(out, svg, -1, NULL));
			}
		}
		/* The home page as a visitor gets it: markup plus its styles. */
		body = g_strconcat(strstr(index, "<body"), css, NULL);
		g_assert_true(g_hash_table_add(homes, body));
	}
}

int
main(int argc, char **argv){
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/compositions/deal", test_deal);
	g_test_add_func("/compositions/themed", test_themed);
	g_test_add_func("/compositions/order", test_order);
	g_test_add_func("/compositions/apart", test_apart);
	g_test_add_func("/compositions/render", test_render);
	return g_test_run();
}
