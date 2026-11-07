/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* Bold openings: striking home openings rebuilt from the inspiration
 * survey, for themes and explicit choices; each renders in the writer's
 * words, and supplied photographs take the opening's picture ahead of a
 * drawing. See docs/components.org, "Bold openings and forms". */
#include "libresite.h"
#include "core/ls-components.h"
#include "core/ls-util.h"
#include <string.h>

static const gchar *const bold_openings[] = {"cover/flood", "cover/wordmark", "cover/halves", "cover/tint",
	"cover/stub", "cover/cinema", "hero/arch", "hero/brutal", "hero/kinetic", "hero/breakout", NULL};

static LsComponents *
library(void){
	g_autoptr(GError) error = NULL;
	LsComponents *components = ls_components_new(&error);
	g_assert_no_error(error);
	return components;
}

static JsonNode *
load(const gchar *path){
	g_autofree gchar *text = NULL;
	g_assert_true(g_file_get_contents(path, &text, NULL, NULL));
	return ls_json_parse(text, NULL);
}

static JsonNode *
json(const gchar *text){
	g_autofree gchar *quoted = g_strdelimit(g_strdup(text), "'", '"');
	JsonNode *node = ls_json_parse(quoted, NULL);
	g_assert_nonnull(node);
	return node;
}

static JsonObject *
path(JsonObject *root, const gchar *first, const gchar *second){
	JsonObject *block = json_object_get_object_member(root, first);
	return second != NULL ? json_object_get_object_member(block, second) : block;
}

static JsonObject *
section(JsonNode *page, guint index){
	return json_array_get_object_element(json_object_get_array_member(json_node_get_object(page), "sections"), index);
}

/* The fixture plan in the default design, with @opening as its home
 * opening. */
static JsonNode *
fixture_plan(LsComponents *components, const gchar *opening, gboolean photos){
	JsonNode *plan = load("tests/fixtures/composed/plan.json");
	if (photos)
		json_object_set_member(json_node_get_object(plan), "photos", json("[{'path':'assets/photos/yard.jpg',"
			"'alt':'A finished brick path through a lawn','width':1200,'height':800}]"));
	g_assert_true(ls_components_apply_default(components, plan, FALSE));
	if (opening != NULL)
		json_object_set_string_member(path(json_node_get_object(plan), "structure", NULL), "opening", opening);
	return plan;
}

/* A supplied photo takes the opening's picture ahead of a drawing; with
 * no photos the drawing stays; an opening that hides its picture never
 * takes a page whose writer put a photo there. */
static void
test_photos(void){
	g_autoptr(LsComponents) components = library();
	const gchar *openings[] = {"cover/flood", "cover/halves", "hero/arch", "hero/banner", "cover/full"};
	guint i;
	for (i = 0; i < G_N_ELEMENTS(openings); i++) {
		g_autoptr(JsonNode) plan = fixture_plan(components, openings[i], TRUE);
		g_autoptr(JsonNode) bare = fixture_plan(components, openings[i], FALSE);
		g_autoptr(JsonNode) page = load("tests/fixtures/composed/index.json");
		g_autoptr(JsonNode) drawn = load("tests/fixtures/composed/index.json");
		g_autoptr(GError) error = NULL;
		JsonObject *art;
		g_assert_true(ls_components_apply_opening(components, plan, "index.html", page));
		art = path(path(section(page, 0), "content", NULL), "art", NULL);
		if (g_strcmp0(json_object_get_string_member(art, "src"), "assets/photos/yard.jpg") != 0)
			g_error("%s shows %s, not the supplied photo", openings[i], json_object_get_string_member(art, "src"));
		g_assert_cmpstr(json_object_get_string_member(art, "alt"), ==, "A finished brick path through a lawn");
		g_assert_true(ls_components_check_page(components, plan, "index.html", page, &error));
		g_assert_no_error(error);
		g_assert_true(ls_components_apply_opening(components, bare, "index.html", drawn));
		art = path(path(section(drawn, 0), "content", NULL), "art", NULL);
		g_assert_cmpstr(json_object_get_string_member(art, "src"), ==, "assets/mark.svg");
	}
}

/* Each bold opening takes the writer's words and passes the page check;
 * its styles reach only the site that wears it. */
static void
test_openings_render(void){
	g_autoptr(LsComponents) components = library();
	guint i;
	for (i = 0; bold_openings[i] != NULL; i++) {
		g_autoptr(JsonNode) plan = fixture_plan(components, bold_openings[i], FALSE);
		g_autoptr(JsonNode) page = load("tests/fixtures/composed/index.json");
		g_autoptr(JsonObject) pages = json_object_new();
		g_autoptr(GError) error = NULL;
		g_auto(GStrv) pair = g_strsplit(bold_openings[i], "/", 2);
		g_autofree gchar *text = NULL, *mark = g_strdup_printf("%s--%s", pair[0], pair[1]);
		const gchar *other = i == 0 ? bold_openings[1] : bold_openings[0];
		g_auto(GStrv) other_pair = g_strsplit(other, "/", 2);
		g_autofree gchar *other_rule = g_strdup_printf(".%s--%s ", other_pair[0], other_pair[1]);
		g_assert_true(ls_components_apply_opening(components, plan, "index.html", page));
		g_assert_cmpstr(json_object_get_string_member(section(page, 0), "component"), ==, pair[0]);
		g_assert_cmpstr(json_object_get_string_member(section(page, 0), "variant"), ==, pair[1]);
		if (!ls_components_check_page(components, plan, "index.html", page, &error))
			g_error("%s: %s", bold_openings[i], error->message);
		json_object_set_member(pages, "index.html", json_node_ref(page));
		{
			const gchar *routes[] = {"work", "start", "journal", "visit"};
			guint r;
			for (r = 0; r < G_N_ELEMENTS(routes); r++) {
				g_autofree gchar *fixture = g_strdup_printf("tests/fixtures/composed/%s.json", routes[r]);
				g_autofree gchar *route = g_strdup_printf("%s.html", routes[r]);
				json_object_set_member(pages, route, load(fixture));
			}
		}
		text = ls_components_compose(components, plan, pages, NULL, &error);
		g_assert_no_error(error);
		if (strstr(text, mark) == NULL)
			g_error("%s is not rendered", bold_openings[i]);
		g_assert_null(strstr(text, other_rule));
		if (strstr(text, "Old furniture, made useful again") == NULL)
			g_error("%s lost the writer's heading", bold_openings[i]);
	}
}

int
main(int argc, char **argv){
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/bold/photos", test_photos);
	g_test_add_func("/bold/openings", test_openings_render);
	return g_test_run();
}
