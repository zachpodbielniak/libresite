/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* The wow pass on real sites: six real-model builds with supplied photos
 * showed five weak spots, each held here. A site with photos opens on one;
 * supplied photos keep their colour; sections never drift apart by more
 * than a screen's worth of nothing; the signature divider punctuates
 * instead of fencing every section; and no photo shows twice on a page.
 * A recompose of the six showed two more: a split opening squeezed into
 * the journal column, and a banner whose photo fell below the first screen.
 * See docs/components.org, "The wow pass". */
#include "libresite.h"
#include "core/ls-components.h"
#include "core/ls-util.h"
#include <stdlib.h>
#include <string.h>

/* Openings led by their picture on the first screen: every cover, and
 * these. The arch and the banner raise their picture under the words,
 * below the first screen of a wide one, so photo sites skip them. */
static const gchar *const picture_led[] = {"hero/split", "poster/backdrop", "hero/breakout", NULL};

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
object(JsonObject *root, const gchar *name){
	return json_object_get_object_member(root, name);
}

static JsonObject *
section(JsonNode *page, guint index){
	return json_array_get_object_element(json_object_get_array_member(json_node_get_object(page), "sections"), index);
}

static const gchar *const photos_json = "[{'path':'assets/photos/patio.jpg','alt':'A sunny patio','width':2000,'height':1300},"
	"{'path':'assets/photos/lawn.jpg','alt':'A lawn with daisies','width':2000,'height':1330},"
	"{'path':'assets/photos/shrub.jpg','alt':'A dense green shrub','width':2000,'height':1330}]";

/* The fixture plan in the default design, with the supplied photos when
 * @photos. */
static JsonNode *
fixture_plan(LsComponents *components, gboolean photos){
	JsonNode *plan = load("tests/fixtures/composed/plan.json");
	if (photos)
		json_object_set_member(json_node_get_object(plan), "photos", json(photos_json));
	g_assert_true(ls_components_apply_default(components, plan, FALSE));
	return plan;
}

static gboolean
is_picture_led(const gchar *opening){
	return g_str_has_prefix(opening, "cover/") || g_strv_contains(picture_led, opening);
}

/* A site with photos opens on the default photo opening, led by its
 * picture, and the opening shows a supplied photo; a site without photos
 * keeps the default opening. */
static void
test_photo_opening(void){
	g_autoptr(LsComponents) components = library();
	g_autoptr(JsonNode) plan = fixture_plan(components, TRUE);
	g_autoptr(JsonNode) bare = fixture_plan(components, FALSE);
	g_autoptr(JsonNode) page = load("tests/fixtures/composed/index.json");
	JsonObject *fallback = object(json_node_get_object(ls_components_get_catalog(components)), "default");
	const gchar *opening = json_object_get_string_member(object(json_node_get_object(plan), "structure"), "opening");
	JsonObject *art;
	g_assert_cmpstr(opening, ==, json_object_get_string_member(fallback, "photo_opening"));
	g_assert_true(is_picture_led(opening));
	g_assert_cmpstr(json_object_get_string_member(object(json_node_get_object(bare), "structure"), "opening"), ==,
		json_object_get_string_member(fallback, "opening"));
	g_assert_true(ls_components_apply_opening(components, plan, "index.html", page));
	art = object(object(section(page, 0), "content"), "art");
	g_assert_nonnull(art);
	g_assert_true(g_str_has_prefix(json_object_get_string_member(art, "src"), "assets/photos/"));
}

/* Supplied photos keep their colour: the default design shows them as
 * they are, and the printed-ink artwork treatment leaves photos alone. */
static void
test_photo_colour(void){
	g_autoptr(LsComponents) components = library();
	g_autoptr(JsonNode) plan = fixture_plan(components, TRUE);
	g_assert_cmpstr(json_object_get_string_member(object(json_node_get_object(plan), "look"), "photos"), ==, "natural");
	/* Gradient lettering clips its fill to the letters, wiping out the
	 * band of page colour a cover's headline sits on over the photo. */
	{
		gchar *css = NULL;
		g_assert_true(g_file_get_contents("data/components/look.css", &css, NULL, NULL));
		g_assert_null(strstr(css, ".look-lettering-gradient main h1 {"));
		g_assert_nonnull(strstr(css, ".look-lettering-gradient main h1:not(.cover h1, .hero--banner h1) {"));
		g_free(css);
	}
	{
		gchar *css = NULL;
		g_assert_true(g_file_get_contents("data/components/look.css", &css, NULL, NULL));
		g_assert_null(strstr(css, ".look-images-print main img {"));
		g_assert_nonnull(strstr(css, ".look-images-print main img[src$=\".svg\"]"));
		g_free(css);
	}
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

/* The fixture site built from @plan. */
static JsonNode *
compose(LsComponents *components, JsonNode *plan){
	const gchar *routes[] = {"index", "work", "start", "journal", "visit"};
	g_autoptr(JsonObject) pages = json_object_new();
	g_autoptr(GError) error = NULL;
	g_autofree gchar *text = NULL;
	guint i;
	for (i = 0; i < G_N_ELEMENTS(routes); i++) {
		g_autofree gchar *fixture = g_strdup_printf("tests/fixtures/composed/%s.json", routes[i]);
		g_autofree gchar *route = g_strdup_printf("%s.html", routes[i]);
		json_object_set_member(pages, route, load(fixture));
	}
	text = ls_components_compose(components, plan, pages, NULL, &error);
	g_assert_no_error(error);
	return ls_json_parse(text, NULL);
}

/* The largest space between sections, in rem, from the site's tokens. */
static gdouble
section_space_max(const gchar *css){
	const gchar *at = strstr(css, "--section-space: clamp(");
	const gchar *close;
	const gchar *comma;
	g_assert_nonnull(at);
	close = strchr(at, ')');
	for (comma = close; comma > at && *comma != ','; comma--)
		;
	return g_ascii_strtod(comma + 1, NULL);
}

/* The airiest density still keeps sections within
 * eight rem of space; the signature's divider shows at every third gap and
 * never adds a section's worth of space of its own. */
static void
test_gaps(void){
	g_autoptr(LsComponents) components = library();
	g_autoptr(JsonNode) plan = fixture_plan(components, FALSE);
	g_autoptr(JsonNode) site = NULL;
	JsonObject *root = json_node_get_object(plan);
	const gchar *css, *signature;
	json_object_set_string_member(object(root, "tokens"), "density", "airy");
	json_object_set_string_member(root, "signature", "main > .block + .block::before{content:\"\";display:block;"
		"width:7.5rem;height:1rem;margin-inline:auto;margin-bottom:var(--section-space);background:var(--accent)}");
	site = compose(components, plan);
	css = bundle_file(site, "styles.css");
	g_assert_nonnull(css);
	if (section_space_max(css) > 8)
		g_error("sections drift %g rem apart", section_space_max(css));
	signature = strstr(css, "/* Site signature */");
	g_assert_nonnull(signature);
	g_assert_nonnull(strstr(signature, "main > .block + .block:not(:nth-child(3n+1))::before { display: none; }"));
	g_assert_nonnull(strstr(signature, "main > .block + .block::before { margin-block: 0 var(--space-5); }"));
}

/* A photo the opening leads with is not shown again further down: a later
 * section that repeats it takes a supplied photo the page has not shown,
 * and a repeat with none left goes (item 24: no photo twice on a page). */
static void
test_photo_twice(void){
	g_autoptr(LsComponents) components = library();
	g_autoptr(JsonNode) plan = fixture_plan(components, TRUE);
	g_autoptr(JsonNode) page = json("{'path':'index.html','sections':["
		"{'component':'cover','variant':'full','content':{'heading':'Gardens you will sit in',"
		"'art':{'src':'assets/photos/patio.jpg','alt':'A sunny patio'}}},"
		"{'component':'media','variant':'inset','content':{'heading':'The patio',"
		"'art':{'src':'assets/photos/patio.jpg','alt':'A sunny patio'}}},"
		"{'component':'gallery','variant':'grid','content':{'items':["
		"{'art':{'src':'assets/photos/patio.jpg','alt':'A sunny patio'}},"
		"{'art':{'src':'assets/photos/lawn.jpg','alt':'A lawn with daisies'}}]}}]}");
	g_autoptr(GHashTable) used = g_hash_table_new(g_str_hash, g_str_equal);
	const gchar *media;
	g_assert_true(ls_components_spread_photos(components, plan, page));
	media = json_object_get_string_member(object(object(section(page, 1), "content"), "art"), "src");
	g_assert_cmpstr(media, !=, "assets/photos/patio.jpg");
	g_assert_true(g_str_has_prefix(media, "assets/photos/"));
	g_assert_cmpstr(json_object_get_string_member(object(object(section(page, 1), "content"), "art"), "alt"), !=, "A sunny patio");
	g_hash_table_add(used, "assets/photos/patio.jpg");
	g_hash_table_add(used, (gpointer)media);
	/* Three photos, four places: the gallery's first item takes the last
	 * fresh photo and its second, a repeat, goes; a gallery needs two
	 * items, so the photo-only gallery goes too. */
	g_assert_cmpuint(json_array_get_length(json_object_get_array_member(json_node_get_object(page), "sections")), ==, 2);
	g_assert_false(ls_components_spread_photos(components, plan, page));
	{
		g_autoptr(GError) error = NULL;
		g_assert_true(ls_components_check_page(components, plan, "index.html", page, &error));
		g_assert_no_error(error);
	}
}

/* A split opening in the narrow journal frame breaks out of its column:
 * beside the words in 46rem its photo shrank to a stamp. */
static void
test_split_column(void){
	gchar *css = NULL;
	g_assert_true(g_file_get_contents("data/components/look.css", &css, NULL, NULL));
	g_assert_nonnull(strstr(css, ".look-frame-center { --max-width: min(var(--frame-width), 46rem); }"));
	g_assert_nonnull(strstr(css, ".look-frame-center main > .hero--split:first-child { --max-width: min(var(--frame-width), 80rem); }"));
	g_free(css);
}

int
main(int argc, char **argv){
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/wow/photo-opening", test_photo_opening);
	g_test_add_func("/wow/photo-colour", test_photo_colour);
	g_test_add_func("/wow/gaps", test_gaps);
	g_test_add_func("/wow/photo-twice", test_photo_twice);
	g_test_add_func("/wow/split-column", test_split_column);
	return g_test_run();
}
