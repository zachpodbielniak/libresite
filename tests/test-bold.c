/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* Bold openings and forms: twelve striking home openings and eight
 * standout section forms rebuilt from the inspiration survey, dealt often;
 * supplied photographs take the opening's picture ahead of a drawing; a
 * larger display scale and more confident colour. See docs/components.org,
 * "Bold openings and forms". */
#include "libresite.h"
#include "core/ls-components.h"
#include "core/ls-util.h"
#include <string.h>

static const gchar *const bold_openings[] = {"cover/flood", "cover/wordmark", "cover/halves", "cover/tint",
	"cover/stub", "cover/cinema", "hero/arch", "hero/brutal", "hero/kinetic", "hero/breakout", "poster/fold",
	"manifesto/stepped", NULL};

/* Each new form with the family and the role it joins. */
static const gchar *const bold_forms[][2] = {{"slabs", "offer"}, {"menuboard", "offer"}, {"volumes", "offer"},
	{"jobsheet", "process"}, {"progress", "process"}, {"stairs", "process"}, {"expect", "practical"},
	{"broadsheet", "practical"}};

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

static gboolean
array_has(JsonArray *list, const gchar *name){
	guint i;
	for (i = 0; list != NULL && i < json_array_get_length(list); i++)
		if (g_strcmp0(json_array_get_string_element(list, i), name) == 0)
			return TRUE;
	return FALSE;
}

static JsonObject *
section(JsonNode *page, guint index){
	return json_array_get_object_element(json_object_get_array_member(json_node_get_object(page), "sections"), index);
}

/* The fixture plan dealt from @seed, with @opening as its home opening. */
static JsonNode *
dealt_plan(LsComponents *components, const gchar *seed, const gchar *opening, gboolean photos){
	JsonNode *plan = load("tests/fixtures/composed/plan.json");
	if (photos)
		json_object_set_member(json_node_get_object(plan), "photos", json("[{'path':'assets/photos/yard.jpg',"
			"'alt':'A finished brick path through a lawn','width':1200,'height':800}]"));
	g_assert_true(ls_components_apply_seed(components, plan, seed, "trades", FALSE));
	if (opening != NULL)
		json_object_set_string_member(path(json_node_get_object(plan), "structure", NULL), "opening", opening);
	return plan;
}

/* Every bold opening joins its component, every bold form its family and
 * its role, as the library loads them. */
static void
test_pools(void){
	g_autoptr(LsComponents) components = library();
	JsonObject *root = json_node_get_object(ls_components_get_catalog(components));
	JsonObject *families = path(root, "section_forms", "families");
	JsonObject *roles = path(root, "compositions", "roles");
	guint i;
	for (i = 0; bold_openings[i] != NULL; i++) {
		g_auto(GStrv) pair = g_strsplit(bold_openings[i], "/", 2);
		JsonObject *variants = json_object_get_object_member(path(path(root, "components", NULL), pair[0], NULL), "variants");
		if (!json_object_has_member(variants, pair[1]))
			g_error("opening %s is not in the library", bold_openings[i]);
	}
	for (i = 0; i < G_N_ELEMENTS(bold_forms); i++) {
		g_assert_nonnull(path(root, "components", bold_forms[i][0]));
		if (!array_has(json_object_get_array_member(families, bold_forms[i][1]), bold_forms[i][0]))
			g_error("form %s is not in the %s family", bold_forms[i][0], bold_forms[i][1]);
		if (!array_has(json_object_get_array_member(roles, bold_forms[i][1]), bold_forms[i][0]))
			g_error("form %s does not have the %s role", bold_forms[i][0], bold_forms[i][1]);
	}
}

/* Across sites every bold opening and form is dealt, and the bold
 * openings open a good share of all sites, not a rare few. */
static void
test_dealt(void){
	g_autoptr(LsComponents) components = library();
	g_autoptr(GHashTable) seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	guint s, i, bold = 0, sites = 2000;
	for (s = 0; s < sites; s++) {
		g_autofree gchar *seed = g_strdup_printf("bold-%u\nA business %u.", s, s);
		g_autoptr(JsonObject) structure = ls_components_deal_structure(components, seed);
		const gchar *opening = json_object_get_string_member(structure, "opening");
		JsonObject *forms = json_object_get_object_member(structure, "forms");
		g_autoptr(GList) families = json_object_get_members(forms);
		GList *family;
		g_hash_table_add(seen, g_strdup(opening));
		bold += g_strv_contains(bold_openings, opening);
		for (family = families; family != NULL; family = family->next)
			g_hash_table_add(seen, g_strdup(json_object_get_string_member(forms, family->data)));
	}
	for (i = 0; bold_openings[i] != NULL; i++)
		if (!g_hash_table_contains(seen, bold_openings[i]))
			g_error("opening %s is never dealt", bold_openings[i]);
	for (i = 0; i < G_N_ELEMENTS(bold_forms); i++)
		if (!g_hash_table_contains(seen, bold_forms[i][0]))
			g_error("form %s is never dealt", bold_forms[i][0]);
	if (bold * 100 < sites * 35)
		g_error("bold openings open %u of %u sites, want at least 35 percent", bold, sites);
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
		g_autoptr(JsonNode) plan = dealt_plan(components, "photos\nA landscaper.", openings[i], TRUE);
		g_autoptr(JsonNode) bare = dealt_plan(components, "photos\nA landscaper.", openings[i], FALSE);
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
		g_assert_cmpstr(json_object_get_string_member(art, "src"), ==, "assets/bench.svg");
	}
	/* The poster that hides its picture gives way to an opening that shows it. */
	{
		g_autoptr(JsonNode) plan = dealt_plan(components, "photos\nA landscaper.", "poster/fold", TRUE);
		g_autoptr(JsonNode) page = load("tests/fixtures/composed/index.json");
		JsonObject *first;
		json_object_set_member(path(section(page, 0), "content", NULL), "art",
			json("{'src':'assets/photos/yard.jpg','alt':'A finished brick path through a lawn'}"));
		g_assert_true(ls_components_apply_opening(components, plan, "index.html", page));
		first = section(page, 0);
		g_assert_false(g_str_equal(json_object_get_string_member(first, "component"), "poster") &&
			g_str_equal(json_object_get_string_member(first, "variant"), "fold"));
		g_assert_cmpstr(json_object_get_string_member(path(path(first, "content", NULL), "art", NULL), "src"), ==,
			"assets/photos/yard.jpg");
	}
}

/* Each bold opening takes the writer's words and passes the page check;
 * its styles reach only the site that wears it. */
static void
test_openings_render(void){
	g_autoptr(LsComponents) components = library();
	guint i;
	for (i = 0; bold_openings[i] != NULL; i++) {
		g_autoptr(JsonNode) plan = dealt_plan(components, "render\nA roofer.", bold_openings[i], FALSE);
		g_autoptr(JsonNode) page = load("tests/fixtures/composed/index.json");
		g_autoptr(JsonObject) pages = json_object_new();
		g_autoptr(GError) error = NULL;
		g_auto(GStrv) pair = g_strsplit(bold_openings[i], "/", 2);
		g_autofree gchar *text = NULL, *mark = g_strdup_printf("%s--%s", pair[0], pair[1]);
		const gchar *other = i == 0 ? bold_openings[1] : bold_openings[0];
		g_auto(GStrv) other_pair = g_strsplit(other, "/", 2);
		g_autofree gchar *other_rule = g_strdup_printf(".%s--%s ", other_pair[0], other_pair[1]);
		/* Openings without a picture never take a page whose writer put one there. */
		if (g_str_equal(pair[0], "poster") || g_str_equal(pair[0], "manifesto"))
			json_object_remove_member(path(section(page, 0), "content", NULL), "art");
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

/* A section of each family becomes each bold form with the writer's words
 * and passes the page check. */
static void
test_forms(void){
	g_autoptr(LsComponents) components = library();
	guint i;
	for (i = 0; i < G_N_ELEMENTS(bold_forms); i++) {
		g_autoptr(JsonNode) plan = dealt_plan(components, "forms\nA cleaner.", NULL, FALSE);
		g_autoptr(JsonNode) page = json("{'path':'index.html','sections':[{'component':'hero','variant':'statement',"
			"'content':{'heading':'Clean homes'}},{'component':'services','variant':'rows','content':{'heading':'What we do',"
			"'intro':'Three kinds of visit.','items':[{'name':'Weekly visits','summary':'Kitchens, bathrooms and floors.'},"
			"{'name':'Deep cleans','summary':'Behind and under everything.'},{'name':'Move-out cleans','summary':'Ready for the next people.'}]}},"
			"{'component':'steps','variant':'row','content':{'heading':'How it works','items':[{'title':'Tell us the rooms',"
			"'body':'A short message is enough.'},{'title':'We visit','body':'On the day we agreed.'},{'title':'You check',"
			"'body':'We fix anything you spot.'}]}},{'component':'faq','variant':'list','content':{'heading':'Questions',"
			"'items':[{'question':'Do you bring supplies?','answer':'Yes, all of them.'},{'question':'Are you insured?',"
			"'answer':'Yes, and we show the certificate on request.'}]}},{'component':'cta','variant':'band',"
			"'content':{'heading':'Book a visit','actions':[{'href':'start.html','label':'Book'}]}}]}");
		g_autoptr(GError) error = NULL;
		JsonObject *forms = path(path(json_node_get_object(plan), "structure", NULL), "forms", NULL);
		JsonObject *made = NULL;
		JsonArray *sections, *items;
		guint s;
		json_object_set_string_member(forms, bold_forms[i][1], bold_forms[i][0]);
		ls_components_apply_forms(components, plan, "index.html", page);
		sections = json_object_get_array_member(json_node_get_object(page), "sections");
		for (s = 0; s < json_array_get_length(sections); s++)
			if (g_str_equal(json_object_get_string_member(json_array_get_object_element(sections, s), "component"),
				bold_forms[i][0]))
				made = json_array_get_object_element(sections, s);
		if (made == NULL)
			g_error("no section became %s", bold_forms[i][0]);
		items = json_object_get_array_member(path(made, "content", NULL), "items");
		g_assert_cmpuint(json_array_get_length(items), >=, 2);
		g_assert_nonnull(json_object_get_string_member(json_array_get_object_element(items, 0), "title"));
		g_assert_nonnull(json_object_get_string_member(json_array_get_object_element(items, 0), "body"));
		if (!ls_components_check_page(components, plan, "index.html", page, &error))
			g_error("%s: %s", bold_forms[i][0], error->message);
	}
}

/* Display sizes run larger: the scale gene starts at 1 and averages well
 * over it. */
static void
test_scale(void){
	g_autoptr(LsComponents) components = library();
	gdouble total = 0;
	guint s, sites = 400;
	for (s = 0; s < sites; s++) {
		g_autofree gchar *seed = g_strdup_printf("scale-%u\nA business %u.", s, s);
		g_autoptr(JsonObject) structure = ls_components_deal_structure(components, seed);
		gdouble scale = json_object_get_double_member(structure, "scale");
		g_assert_cmpfloat(scale, >=, 1.0);
		g_assert_cmpfloat(scale, <=, 1.5);
		total += scale;
	}
	g_assert_cmpfloat(total / sites, >=, 1.15);
}

/* More generated palettes are a bold colour field or a deep saturated
 * ground than a pale or tinted one. */
static void
test_colour(void){
	g_autoptr(LsComponents) components = library();
	guint s, generated = 0, confident = 0;
	for (s = 0; s < 1500; s++) {
		g_autofree gchar *seed = g_strdup_printf("colour-%u\nA business %u.", s, s);
		g_autoptr(JsonObject) identity = ls_components_suggest_identity(components, seed, "trades");
		JsonObject *palette = json_object_get_object_member(identity, "palette");
		const gchar *summary = json_object_get_string_member(palette, "summary");
		if (!g_str_has_prefix(json_object_get_string_member(palette, "name"), "generated-"))
			continue;
		generated++;
		confident += strstr(summary, " bold ") != NULL || strstr(summary, " saturated deep ") != NULL;
	}
	g_assert_cmpuint(generated, >, 1000);
	if (confident * 100 < generated * 38)
		g_error("%u of %u generated palettes are bold or deep, want at least 38 percent", confident, generated);
}

int
main(int argc, char **argv){
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/bold/pools", test_pools);
	g_test_add_func("/bold/dealt", test_dealt);
	g_test_add_func("/bold/photos", test_photos);
	g_test_add_func("/bold/openings", test_openings_render);
	g_test_add_func("/bold/forms", test_forms);
	g_test_add_func("/bold/scale", test_scale);
	g_test_add_func("/bold/colour", test_colour);
	return g_test_run();
}
