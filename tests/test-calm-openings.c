/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* Calm, but not one template (epic #17 item 24): the seed deals the
 * photo-led opening from at least four calm compositions, earlier sites
 * steer the next one off the opening just worn, and no page shows a
 * supplied photo twice. */
#include "libresite.h"
#include "core/ls-components.h"
#include "core/ls-util.h"
#include "core/ls-visual.h"
#include <string.h>

static const gchar *const calm_scenes[] = {"split", "right", "above", "scrim", NULL};

static JsonNode *
read_json(const gchar *path){
	g_autofree gchar *text = NULL;
	g_assert_true(g_file_get_contents(path, &text, NULL, NULL));
	return ls_json_parse(text, NULL);
}

/* The composed fixture with the bar's media and visual sequence and
 * @count supplied photos (the bar's three plus invented ones). */
static JsonNode *
visual_plan(guint count){
	JsonNode *plan = read_json("tests/fixtures/composed/plan.json");
	JsonObject *root = json_node_get_object(plan);
	g_autoptr(JsonNode) data = read_json("tests/fixtures/bar.json");
	JsonObject *extra = json_node_get_object(data);
	g_autoptr(GList) names = json_object_get_members(extra);
	JsonArray *photos;
	GList *n;
	guint i;
	for (n = names; n != NULL; n = n->next)
		json_object_set_member(root, n->data, json_node_copy(json_object_get_member(extra, n->data)));
	photos = json_object_get_array_member(root, "photos");
	for (i = json_array_get_length(photos); i < count; i++) {
		JsonObject *photo = json_object_new();
		g_autofree gchar *path = g_strdup_printf("assets/extra-%u.jpg", i);
		g_autofree gchar *alt = g_strdup_printf("Extra photograph %u", i);
		json_object_set_string_member(photo, "path", path);
		json_object_set_string_member(photo, "alt", alt);
		json_object_set_string_member(photo, "credit", "Fixture photographs");
		json_object_set_int_member(photo, "width", 1600);
		json_object_set_int_member(photo, "height", 1000);
		json_array_add_object_element(photos, photo);
	}
	while (json_array_get_length(photos) > count)
		json_array_remove_element(photos, json_array_get_length(photos) - 1);
	return plan;
}

/* A home page whose writer leans on the same photos again and again: a
 * story, a carousel and a gallery over the hero and detail photos. */
static JsonNode *
greedy_home(void){
	return ls_json_parse(
		"{\"path\":\"index.html\",\"sections\":["
		"{\"component\":\"hero\",\"variant\":\"split\",\"content\":{\"heading\":\"Plumbing, done properly\","
		"\"body\":\"Kitchens and bathrooms.\",\"art\":{\"src\":\"assets/room.jpg\",\"alt\":\"A bright room\"}}},"
		"{\"component\":\"story\",\"variant\":\"right\",\"content\":{\"heading\":\"How a visit runs\","
		"\"art\":{\"src\":\"assets/room.jpg\",\"alt\":\"A bright room\"},"
		"\"items\":[{\"title\":\"Look\",\"body\":\"We look first.\"},{\"title\":\"Fix\",\"body\":\"Then we fix it.\"}]}},"
		"{\"component\":\"carousel\",\"variant\":\"cards\",\"content\":{\"heading\":\"Around the house\",\"items\":["
		"{\"art\":{\"src\":\"assets/room.jpg\",\"alt\":\"A bright room\"},\"caption\":\"A bright room\"},"
		"{\"art\":{\"src\":\"assets/detail.jpg\",\"alt\":\"A brass fitting\"},\"caption\":\"A brass fitting\"},"
		"{\"art\":{\"src\":\"assets/room.jpg\",\"alt\":\"A bright room\"},\"caption\":\"Again\"}]}},"
		"{\"component\":\"media\",\"variant\":\"full\",\"content\":{\"art\":{\"src\":\"assets/detail.jpg\",\"alt\":\"A brass fitting\"}}},"
		"{\"component\":\"cta\",\"variant\":\"band\",\"content\":{\"heading\":\"Book a visit\","
		"\"actions\":[{\"href\":\"visit.html\",\"label\":\"Book\"}]}}]}", NULL);
}

/* Every supplied photo @node shows, counted into @seen: a scene's phone
 * crop counts only when it is another photo than the scene's own. */
static void
count_photos(JsonObject *plan, JsonNode *node, GHashTable *seen){
	if (JSON_NODE_HOLDS_ARRAY(node)) {
		JsonArray *list = json_node_get_array(node);
		guint i;
		for (i = 0; i < json_array_get_length(list); i++)
			count_photos(plan, json_array_get_element(list, i), seen);
	} else if (JSON_NODE_HOLDS_OBJECT(node)) {
		JsonObject *object = json_node_get_object(node);
		g_autoptr(GList) members = json_object_get_members(object);
		GList *m;
		if (ls_string(object, "src") != NULL) {
			const gchar *src = ls_string(object, "src");
			g_hash_table_insert(seen, g_strdup(src), GUINT_TO_POINTER(GPOINTER_TO_UINT(g_hash_table_lookup(seen, src)) + 1));
			return;
		}
		for (m = members; m != NULL; m = m->next) {
			JsonNode *child = json_object_get_member(object, m->data);
			if (g_str_equal(m->data, "portrait") && JSON_NODE_HOLDS_OBJECT(child) &&
				json_object_has_member(object, "art") &&
				g_strcmp0(ls_string(json_node_get_object(child), "src"),
					ls_string(json_object_get_object_member(object, "art"), "src")) == 0)
				continue;
			count_photos(plan, child, seen);
		}
	}
}

static void
assert_no_repeat(JsonObject *plan, JsonNode *page){
	g_autoptr(GHashTable) seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	GHashTableIter iter;
	gpointer key, value;
	count_photos(plan, json_object_get_member(json_node_get_object(page), "sections"), seen);
	g_hash_table_iter_init(&iter, seen);
	while (g_hash_table_iter_next(&iter, &key, &value))
		if (GPOINTER_TO_UINT(value) > 1)
			g_error("%s shows %s %u times", ls_string(json_node_get_object(page), "path"), (const gchar *)key,
				GPOINTER_TO_UINT(value));
}

/* At least four calm openings, every one of them calm, the same seed
 * always the same one. */
static void
test_scenes_dealt(void){
	g_autoptr(LsComponents) library = ls_components_new(NULL);
	g_autoptr(GHashTable) seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	guint i;
	for (i = 0; i < 64; i++) {
		g_autofree gchar *seed = g_strdup_printf("site-%u", i);
		g_autofree gchar *scene = ls_components_suggest_scene(library, seed);
		g_autofree gchar *again = ls_components_suggest_scene(library, seed);
		g_assert_nonnull(scene);
		g_assert_cmpstr(scene, ==, again);
		g_assert_true(g_strv_contains(calm_scenes, scene));
		g_hash_table_add(seen, g_steal_pointer(&scene));
	}
	g_assert_cmpuint(g_hash_table_size(seen), >=, 4);
}

/* The seed, not the writer, picks the opening the plan wears. */
static void
test_seed_sets_layout(void){
	g_autoptr(LsComponents) library = ls_components_new(NULL);
	g_autoptr(GHashTable) seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	guint i;
	for (i = 0; i < 32; i++) {
		g_autoptr(JsonNode) plan = visual_plan(4);
		g_autofree gchar *seed = g_strdup_printf("layout-%u", i);
		g_autofree gchar *scene = ls_components_suggest_scene(library, seed);
		JsonObject *visual = json_object_get_object_member(json_node_get_object(plan), "visual");
		g_assert_true(ls_components_apply_seed(library, plan, seed, "trades", FALSE));
		g_assert_cmpstr(ls_string(visual, "layout"), ==, scene);
		g_assert_true(ls_visual_check_plan(json_node_get_object(plan), NULL));
		g_hash_table_add(seen, g_strdup(ls_string(visual, "layout")));
	}
	g_assert_cmpuint(g_hash_table_size(seen), >=, 4);
}

/* Eighteen sites in a row, each steering off the ones before it: no two
 * neighbours open the same way, and no opening is worn by over a third. */
static void
test_no_two_in_a_row(void){
	g_autoptr(LsComponents) library = ls_components_new(NULL);
	g_autoptr(JsonArray) avoid = json_array_new();
	g_autoptr(GHashTable) worn = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	g_autofree gchar *last = NULL;
	guint i;
	for (i = 0; i < 18; i++) {
		g_autofree gchar *seed = g_strdup_printf("showcase business %u", i);
		g_autofree gchar *chosen = ls_components_choose_photo_seed(library, seed, "trades", avoid, NULL);
		g_autofree gchar *scene = ls_components_suggest_scene(library, chosen);
		g_autofree gchar *opening = g_strconcat("scene/", scene, NULL);
		JsonObject *fingerprint = json_object_new();
		JsonArray *home = json_array_new();
		guint count;
		if (last != NULL)
			g_assert_cmpstr(scene, !=, last);
		json_array_add_string_element(home, opening);
		json_object_set_array_member(fingerprint, "home", home);
		json_array_add_object_element(avoid, fingerprint);
		count = GPOINTER_TO_UINT(g_hash_table_lookup(worn, scene)) + 1;
		g_hash_table_insert(worn, g_strdup(scene), GUINT_TO_POINTER(count));
		g_assert_cmpuint(count, <=, 6);
		g_free(last);
		last = g_steal_pointer(&scene);
	}
}

/* The writer reuses the hero and detail photos all down the home page;
 * with four or more supplied, every photo on the page is a different one
 * and the page still checks. */
static void
test_no_photo_twice(void){
	g_autoptr(LsComponents) library = ls_components_new(NULL);
	g_autoptr(JsonNode) plan = visual_plan(6), page = greedy_home();
	JsonObject *root = json_node_get_object(plan);
	g_autoptr(GError) error = NULL;
	g_autofree gchar *text = NULL;
	g_assert_true(ls_components_apply_seed(library, plan, "no-photo-twice", "trades", FALSE));
	ls_visual_apply_page(root, json_node_get_object(page));
	ls_components_spread_photos(library, plan, page);
	assert_no_repeat(root, page);
	g_assert_true(ls_components_check_page(library, plan, "index.html", page, &error));
	g_assert_no_error(error);
	/* The writer's story keeps its words. */
	text = ls_json_encode(json_object_get_member(json_node_get_object(page), "sections"));
	g_assert_nonnull(strstr(text, "We look first."));
}

/* With only two photos, a repeat that cannot take a fresh photo goes: a
 * carousel or gallery item, or a section that cannot stand without its
 * photo. The sections without photos stay. */
static void
test_short_of_photos(void){
	g_autoptr(LsComponents) library = ls_components_new(NULL);
	g_autoptr(JsonNode) plan = visual_plan(2), page = greedy_home();
	JsonObject *root = json_node_get_object(plan), *media = json_object_get_object_member(root, "media");
	g_autoptr(GError) error = NULL;
	g_autofree gchar *text = NULL;
	json_object_remove_member(media, "proof");
	json_object_set_string_member(media, "portrait", "assets/room.jpg");
	g_assert_true(ls_components_apply_seed(library, plan, "short-of-photos", "trades", FALSE));
	ls_visual_apply_page(root, json_node_get_object(page));
	ls_components_spread_photos(library, plan, page);
	assert_no_repeat(root, page);
	g_assert_true(ls_components_check_page(library, plan, "index.html", page, &error));
	g_assert_no_error(error);
	text = ls_json_encode(json_object_get_member(json_node_get_object(page), "sections"));
	g_assert_nonnull(strstr(text, "\"label\":\"Book\""));
}

/* An inner page that shows a photo of the home sequence takes a spare one
 * the sequence does not show, different routes different spares, so the
 * site shows more of its photos; with no spare left it keeps its own. */
static void
test_inner_spares(void){
	g_autoptr(LsComponents) library = ls_components_new(NULL);
	g_autoptr(GHashTable) spares = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	const gchar *routes[] = {"work.html", "start.html", "journal.html", "visit.html"};
	guint i;
	for (i = 0; i < G_N_ELEMENTS(routes); i++) {
		g_autoptr(JsonNode) plan = visual_plan(6);
		g_autofree gchar *text = g_strdup_printf("{\"path\":\"%s\",\"sections\":["
			"{\"component\":\"page-header\",\"variant\":\"plain\",\"content\":{\"heading\":\"Our work\"}},"
			"{\"component\":\"media\",\"variant\":\"full\",\"content\":{\"art\":{\"src\":\"assets/room.jpg\",\"alt\":\"A bright room\"}}}]}",
			routes[i]);
		g_autoptr(JsonNode) page = ls_json_parse(text, NULL);
		JsonObject *art;
		const gchar *src;
		g_assert_true(ls_components_apply_seed(library, plan, "inner-spares", "trades", FALSE));
		g_assert_true(ls_components_spread_photos(library, plan, page));
		art = json_object_get_object_member(json_object_get_object_member(json_array_get_object_element(
			json_object_get_array_member(json_node_get_object(page), "sections"), 1), "content"), "art");
		src = ls_string(art, "src");
		g_assert_true(g_str_has_prefix(src, "assets/extra-"));
		g_hash_table_add(spares, g_strdup(src));
	}
	g_assert_cmpuint(g_hash_table_size(spares), >=, 2);
	/* The bar's three photos are all in the sequence: no spare, no change. */
	{
		g_autoptr(JsonNode) plan = visual_plan(3);
		g_autoptr(JsonNode) page = ls_json_parse("{\"path\":\"work.html\",\"sections\":["
			"{\"component\":\"page-header\",\"variant\":\"plain\",\"content\":{\"heading\":\"Our work\"}},"
			"{\"component\":\"media\",\"variant\":\"full\",\"content\":{\"art\":{\"src\":\"assets/room.jpg\",\"alt\":\"A bright room\"}}}]}", NULL);
		g_assert_true(ls_components_apply_seed(library, plan, "inner-spares", "trades", FALSE));
		g_assert_false(ls_components_spread_photos(library, plan, page));
	}
}

/* A built site: the composed fixture's five routes over @count photos,
 * through the pages step's order of builder passes. */
static JsonNode *
build_site(LsComponents *library, JsonNode *plan, gboolean spread){
	const gchar *routes[] = {"index", "work", "start", "journal", "visit"};
	g_autoptr(JsonObject) pages = json_object_new();
	g_autoptr(GError) error = NULL;
	g_autofree gchar *text = NULL;
	guint i;
	g_assert_true(ls_components_apply_seed(library, plan, "built-site", "trades", FALSE));
	for (i = 0; i < G_N_ELEMENTS(routes); i++) {
		g_autofree gchar *route = g_strdup_printf("%s.html", routes[i]);
		g_autofree gchar *fixture = g_strdup_printf("tests/fixtures/composed/%s.json", routes[i]);
		JsonNode *page = i == 0 ? greedy_home() : read_json(fixture);
		ls_visual_apply_page(json_node_get_object(plan), json_node_get_object(page));
		if (spread)
			ls_components_spread_photos(library, plan, page);
		ls_components_ensure_photo(library, plan, route, page);
		json_object_set_member(pages, route, page);
	}
	text = ls_components_compose(library, plan, pages, NULL, &error);
	if (error != NULL)
		g_error("%s", error->message);
	return ls_json_parse(text, NULL);
}

/* The site check fails a page that shows one photo twice; a site given
 * six photos shows at least four of them. */
static void
test_site_check(void){
	g_autoptr(LsComponents) library = ls_components_new(NULL);
	g_autoptr(JsonNode) plan = visual_plan(6), bundle = build_site(library, plan, TRUE);
	g_autoptr(JsonNode) loose_plan = visual_plan(6), loose = build_site(library, loose_plan, FALSE);
	g_autoptr(GString) problems = g_string_new(NULL);
	if (!ls_components_check_taste(library, plan, bundle, problems))
		g_error("%s", problems->str);
	g_assert_false(ls_components_check_taste(library, loose_plan, loose, problems));
	g_assert_nonnull(strstr(problems->str, "twice"));
	/* Four distinct photos across the site. */
	g_assert_cmpuint(ls_components_count_photos(library, plan, bundle), >=, 4);
	/* Fewer when only the hero and detail photos stay on the built pages. */
	{
		JsonArray *files = json_object_get_array_member(json_node_get_object(bundle), "files");
		g_autoptr(GRegex) other = g_regex_new("<(img|source)[^>]*assets/(proof|extra-[0-9])[^>]*>", 0, 0, NULL);
		guint i;
		for (i = 0; i < json_array_get_length(files); i++) {
			JsonObject *f = json_array_get_object_element(files, i);
			const gchar *content = ls_string(f, "content");
			if (g_str_has_suffix(ls_string(f, "path"), ".html") && content != NULL) {
				g_autofree gchar *changed = g_regex_replace_literal(other, content, -1, 0, "", 0, NULL);
				json_object_set_string_member(f, "content", changed);
			}
		}
		g_assert_cmpuint(ls_components_count_photos(library, plan, bundle), ==, 2);
	}
}

int
main(int argc, char **argv){
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/calm-openings/scenes-dealt", test_scenes_dealt);
	g_test_add_func("/calm-openings/seed-sets-layout", test_seed_sets_layout);
	g_test_add_func("/calm-openings/no-two-in-a-row", test_no_two_in_a_row);
	g_test_add_func("/calm-openings/no-photo-twice", test_no_photo_twice);
	g_test_add_func("/calm-openings/short-of-photos", test_short_of_photos);
	g_test_add_func("/calm-openings/inner-spares", test_inner_spares);
	g_test_add_func("/calm-openings/site-check", test_site_check);
	return g_test_run();
}
