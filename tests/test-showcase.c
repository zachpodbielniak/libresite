/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* Supplied photographs keep their colour inside every cover treatment.
 * Explicit monochrome photo looks remain available to authored themes. */
#include "libresite.h"
#include "core/ls-components.h"
#include "core/ls-prompts.h"
#include "core/ls-util.h"
#include "test-image-fixtures.h"
#include <string.h>

static JsonNode *
load(const gchar *path){
	g_autofree gchar *text = NULL;
	g_assert_true(g_file_get_contents(path, &text, NULL, NULL));
	return ls_json_parse(text, NULL);
}

static void
write_bytes(const gchar *directory, const gchar *path, gconstpointer data, gssize size){
	g_autofree gchar *out = g_build_filename(directory, path, NULL);
	g_autofree gchar *parent = g_path_get_dirname(out);
	g_assert_cmpint(g_mkdir_with_parents(parent, 0700), ==, 0);
	g_assert_true(g_file_set_contents(out, data, size, NULL));
}

static const gchar *const photos_json = "[{\"path\":\"assets/photos/shop.jpg\",\"alt\":\"A shop front\",\"width\":2000,\"height\":1300}]";

/* The fixture plan dealt from @seed with a supplied photo. */
static JsonNode *
photo_plan(LsComponents *components, const gchar *seed){
	JsonNode *plan = load("tests/fixtures/composed/plan.json");
	json_object_set_member(json_node_get_object(plan), "photos", ls_json_parse(photos_json, NULL));
	g_assert_true(ls_components_apply_seed(components, plan, seed, "trades", FALSE));
	return plan;
}

static const gchar *
plan_opening(JsonNode *plan){
	return json_object_get_string_member(json_object_get_object_member(json_node_get_object(plan), "structure"),
		"opening");
}

/* The showcase's photo sites: four of thirteen opened on the same card over
 * a photograph, because a layout with one photo-led opening always fell
 * back on it. Photo openings spread across the whole set. */
static void
test_photo_spread(void){
	g_autoptr(GError) error = NULL;
	g_autoptr(LsComponents) components = ls_components_new(&error);
	g_autoptr(GHashTable) counts = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	GHashTableIter iter;
	gpointer key, value;
	guint s, sites = 400, most = 0;
	const gchar *top = NULL;
	g_assert_no_error(error);
	for (s = 0; s < sites; s++) {
		g_autofree gchar *seed = g_strdup_printf("showcase-%u\nA business %u.", s, s);
		g_autofree gchar *opening = ls_components_suggest_photo_opening(components, seed);
		guint n = GPOINTER_TO_UINT(g_hash_table_lookup(counts, opening)) + 1;
		g_hash_table_insert(counts, g_steal_pointer(&opening), GUINT_TO_POINTER(n));
	}
	g_hash_table_iter_init(&iter, counts);
	while (g_hash_table_iter_next(&iter, &key, &value))
		if (GPOINTER_TO_UINT(value) > most) {
			most = GPOINTER_TO_UINT(value);
			top = key;
		}
	g_test_message("photo openings: %u kinds, most common %s on %u of %u", g_hash_table_size(counts), top, most, sites);
	/* Taste leaves about a dozen calm photo openings, so one may lead a
	 * little more often than the old 12 percent. */
	if (most * 100 > sites * 15)
		g_error("%s opens %u of %u photo sites (limit 15%%)", top, most, sites);
}

/* Eighteen photo sites built in turn, each steering away from the ones
 * before it, never open the same way three times: the steering weighs the
 * opening a photo site actually wears, not the one dealt before the swap. */
static void
test_photo_steering(void){
	g_autoptr(GError) error = NULL;
	g_autoptr(LsComponents) components = ls_components_new(&error);
	g_autoptr(JsonArray) avoid = json_array_new();
	g_autoptr(GHashTable) counts = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	guint s, worst = 0;
	g_assert_no_error(error);
	for (s = 0; s < 18; s++) {
		g_autofree gchar *seed = g_strdup_printf("showcase-%u\nA business %u.", s, s);
		g_autofree gchar *chosen = ls_components_choose_photo_seed(components, seed, "trades", avoid, NULL);
		g_autoptr(JsonNode) plan = photo_plan(components, chosen);
		JsonObject *fp = ls_components_fingerprint(components, plan, NULL);
		JsonArray *home = json_array_new();
		guint n = GPOINTER_TO_UINT(g_hash_table_lookup(counts, plan_opening(plan))) + 1;
		g_hash_table_insert(counts, g_strdup(plan_opening(plan)), GUINT_TO_POINTER(n));
		worst = MAX(worst, n);
		json_array_add_string_element(home, plan_opening(plan));
		json_object_set_array_member(fp, "home", home);
		json_array_add_object_element(avoid, fp);
	}
	g_test_message("18 steered photo sites: %u openings, at most %u alike", g_hash_table_size(counts), worst);
	g_assert_cmpuint(worst, <=, 2);
}

/* Under a masthead nameplate the cinema cover's ink bars, wide screen and
 * title card push its headline below the first screen, so a photo site
 * with that frame opens another way. */
static void
test_masthead_cinema(void){
	g_autoptr(GError) error = NULL;
	g_autoptr(LsComponents) components = ls_components_new(&error);
	guint s, mastheads = 0, cinemas = 0;
	g_assert_no_error(error);
	for (s = 0; s < 600; s++) {
		g_autofree gchar *seed = g_strdup_printf("showcase-%u\nA business %u.", s, s);
		g_autoptr(JsonNode) plan = photo_plan(components, seed);
		g_autofree gchar *suggested = ls_components_suggest_photo_opening(components, seed);
		const gchar *frame = json_object_get_string_member(json_object_get_object_member(json_node_get_object(plan), "look"),
			"frame");
		g_assert_cmpstr(suggested, ==, plan_opening(plan));
		cinemas += g_str_equal(plan_opening(plan), "cover/cinema");
		if (g_strcmp0(frame, "masthead") != 0)
			continue;
		mastheads++;
		if (g_str_equal(plan_opening(plan), "cover/cinema"))
			g_error("seed %u opens on the cinema cover under a masthead", s);
	}
	g_test_message("%u masthead photo sites, %u cinema openings elsewhere", mastheads, cinemas);
	/* Taste deals no masthead frame any more; the guard stays for themes. */
	g_assert_cmpuint(mastheads, ==, 0);
	g_assert_cmpuint(cinemas, >, 0);
}

static void
test_opening_copy(void){
	g_autoptr(GError) error = NULL;
	g_autoptr(LsPrompts) prompts = ls_prompts_new(NULL, &error);
	g_autofree gchar *rendered = NULL;
	g_assert_no_error(error);
	rendered = ls_prompts_render_stage(prompts, "page", NULL, "{\"kind\":\"home\"}", &error);
	g_assert_no_error(error);
	g_assert_nonnull(strstr(rendered, "3 to 8 words"));
	g_assert_nonnull(strstr(rendered, "at most 24 words"));
	g_assert_nonnull(strstr(rendered, "Preserve supplied exact copy"));
}

static void
test_photo_covers(void){
	g_autoptr(GError) error = NULL;
	g_autoptr(LsComponents) components = ls_components_new(&error);
	const gchar *output = g_getenv("SHOWCASE_OUTPUT");
	const gchar *variants[] = {"tint", "flood", "wordmark", "cinema", "band"};
	const gchar *looks[] = {"natural", "matted", "mono", "duotone"};
	const gchar *routes[] = {"index", "work", "start", "journal", "visit"};
	gboolean guard_seen = FALSE;
	guint short_word_headings = 0;
	guint v, l, i;
	g_assert_no_error(error);
	for (v = 0; v < G_N_ELEMENTS(variants); v++) {
		for (l = 0; l < G_N_ELEMENTS(looks); l++) {
			g_autoptr(JsonNode) plan = load("tests/fixtures/composed/plan.json");
			g_autoptr(JsonObject) pages = json_object_new();
			g_autofree gchar *text = NULL;
			g_autoptr(JsonNode) bundle = NULL;
			g_autofree gchar *name = g_strdup_printf("%s-%s", variants[v], looks[l]);
			g_autofree gchar *directory = output != NULL ? g_build_filename(output, name, NULL) : NULL;
			JsonObject *root = json_node_get_object(plan);
			JsonArray *photos = json_array_new();
			JsonObject *photo = json_object_new();
			JsonArray *files;
			json_object_set_string_member(photo, "path", "assets/photos/room.jpg");
			json_object_set_string_member(photo, "alt", "A colourful room");
			json_object_set_int_member(photo, "width", 1200);
			json_object_set_int_member(photo, "height", 800);
			json_array_add_object_element(photos, photo);
			json_object_set_array_member(root, "photos", photos);
			/* A live signature erased the label's backing over a photograph. */
			json_object_set_string_member(root, "signature", ".kicker { background: transparent; color: var(--muted); }");
			json_object_set_string_member(json_object_get_object_member(root, "look"), "photos", looks[l]);
			/* The tall masthead pushed live openings below the first screen;
			 * the builder no longer pairs it with the cinema cover. */
			if (!g_str_equal(variants[v], "cinema"))
				json_object_set_string_member(json_object_get_object_member(root, "look"), "frame", "masthead");
			for (i = 0; i < G_N_ELEMENTS(routes); i++) {
				g_autofree gchar *path = g_strdup_printf("tests/fixtures/composed/%s.json", routes[i]);
				g_autofree gchar *route = g_strconcat(routes[i], ".html", NULL);
				JsonNode *page = load(path);
				if (i == 0) {
					JsonArray *sections = json_object_get_array_member(json_node_get_object(page), "sections");
					JsonObject *first = json_array_get_object_element(sections, 0);
					JsonObject *content = json_object_get_object_member(first, "content");
					JsonObject *art = json_object_get_object_member(content, "art");
					json_object_set_string_member(first, "component", "cover");
					json_object_set_string_member(first, "variant", variants[v]);
					if (g_str_equal(variants[v], "wordmark"))
						json_object_set_string_member(content, "heading", "A place for you to begin");
					json_object_set_string_member(art, "src", "assets/photos/room.jpg");
					json_object_set_string_member(art, "alt", "A colourful room");
				}
				json_object_set_member(pages, route, page);
			}
			text = ls_components_compose(components, plan, pages, NULL, &error);
			g_assert_no_error(error);
			g_assert_nonnull(text);
			bundle = ls_json_parse(text, NULL);
			files = json_object_get_array_member(json_node_get_object(bundle), "files");
			for (i = 0; i < json_array_get_length(files); i++) {
				JsonObject *file = json_array_get_object_element(files, i);
				const gchar *path = json_object_get_string_member(file, "path");
				const gchar *content = json_object_get_string_member(file, "content");
				if (g_str_equal(path, "styles.css") && strstr(content, "/* Supplied photo colour */") != NULL)
					guard_seen = TRUE;
				if (g_str_equal(path, "index.html") && g_str_equal(variants[v], "wordmark") &&
					strstr(content, "cover__title--long") != NULL)
					short_word_headings++;
				if (directory != NULL)
					write_bytes(directory, path, content, -1);
			}
			if (directory != NULL) {
				g_autoptr(GBytes) image = image_fixture(1200, 800, FALSE, ".jpg[Q=80]");
				gsize size;
				gconstpointer data = g_bytes_get_data(image, &size);
				const gchar *art[] = {"bench.svg", "mark.svg"};
				write_bytes(directory, "assets/photos/room.jpg", data, (gssize)size);
				for (i = 0; i < G_N_ELEMENTS(art); i++) {
					g_autofree gchar *path = g_build_filename("tests/fixtures/composed", art[i], NULL);
					g_autofree gchar *target = g_build_filename("assets", art[i], NULL);
					g_autofree gchar *svg = NULL;
					g_assert_true(g_file_get_contents(path, &svg, NULL, NULL));
					write_bytes(directory, target, svg, -1);
				}
			}
		}
	}
	g_assert_true(guard_seen);
	g_assert_cmpuint(short_word_headings, ==, G_N_ELEMENTS(looks));
}

int
main(int argc, char **argv){
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/showcase/photo-covers", test_photo_covers);
	g_test_add_func("/showcase/opening-copy", test_opening_copy);
	g_test_add_func("/showcase/photo-spread", test_photo_spread);
	g_test_add_func("/showcase/photo-steering", test_photo_steering);
	g_test_add_func("/showcase/masthead-cinema", test_masthead_cinema);
	return g_test_run();
}
