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

/* The fixture plan dealt from @seed, with the supplied photos when @photos. */
static JsonNode *
dealt_plan(LsComponents *components, const gchar *seed, gboolean photos){
	JsonNode *plan = load("tests/fixtures/composed/plan.json");
	if (photos)
		json_object_set_member(json_node_get_object(plan), "photos", json(photos_json));
	g_assert_true(ls_components_apply_seed(components, plan, seed, "trades", FALSE));
	return plan;
}

static gboolean
is_picture_led(const gchar *opening){
	return g_str_has_prefix(opening, "cover/") || g_strv_contains(picture_led, opening);
}

/* A site with photos is dealt an opening led by its picture, and the
 * opening shows a supplied photo; the prompt's suggestion agrees; sites
 * without photos keep the opening they were dealt before. */
static void
test_photo_opening(void){
	g_autoptr(LsComponents) components = library();
	g_autoptr(GHashTable) seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	guint s, sites = 300;
	for (s = 0; s < sites; s++) {
		g_autofree gchar *seed = g_strdup_printf("wow-%u\nA business %u.", s, s);
		g_autoptr(JsonNode) plan = dealt_plan(components, seed, TRUE);
		g_autoptr(JsonNode) bare = dealt_plan(components, seed, FALSE);
		g_autoptr(JsonObject) dealt = ls_components_deal_structure(components, seed);
		g_autoptr(JsonNode) page = load("tests/fixtures/composed/index.json");
		g_autofree gchar *suggested = ls_components_suggest_photo_opening(components, seed);
		const gchar *opening = json_object_get_string_member(object(json_node_get_object(plan), "structure"), "opening");
		JsonObject *art;
		if (!is_picture_led(opening))
			g_error("seed %u has photos but opens with %s", s, opening);
		g_assert_cmpstr(suggested, ==, opening);
		g_assert_cmpstr(json_object_get_string_member(object(json_node_get_object(bare), "structure"), "opening"), ==,
			json_object_get_string_member(dealt, "opening"));
		g_hash_table_add(seen, g_strdup(opening));
		g_assert_true(ls_components_apply_opening(components, plan, "index.html", page));
		art = object(object(section(page, 0), "content"), "art");
		g_assert_nonnull(art);
		if (!g_str_has_prefix(json_object_get_string_member(art, "src"), "assets/photos/"))
			g_error("seed %u opens with %s, not a photo", s, json_object_get_string_member(art, "src"));
	}
	if (g_hash_table_size(seen) < 10)
		g_error("photo sites open with only %u openings", g_hash_table_size(seen));
}

/* Whether @block's @name list holds an opening its picture leads. */
static gboolean
any_led(JsonObject *block, const gchar *name){
	JsonArray *openings = block != NULL && json_object_has_member(block, name)
		? json_object_get_array_member(block, name) : NULL;
	guint i;
	for (i = 0; openings != NULL && i < json_array_get_length(openings); i++)
		if (is_picture_led(json_array_get_string_element(openings, i)))
			return TRUE;
	return FALSE;
}

/* Whether @composition offers an opening its picture leads: its own
 * openings, or those the bold block deals into it. */
static gboolean
offers_led(const gchar *composition){
	static JsonNode *catalog = NULL;
	const gchar *blocks[] = {"compositions", "wider"};
	JsonObject *root;
	guint b;
	if (catalog == NULL) {
		g_autofree gchar *text = NULL;
		g_assert_true(g_file_get_contents("data/components/catalog.yaml", &text, NULL, NULL));
		catalog = ls_yaml_parse(text, NULL);
	}
	root = json_node_get_object(catalog);
	for (b = 0; b < G_N_ELEMENTS(blocks); b++) {
		JsonObject *layouts = object(object(root, blocks[b]), "layouts");
		if (json_object_has_member(layouts, composition) && any_led(object(layouts, composition), "openings"))
			return TRUE;
	}
	return any_led(object(object(root, "bold"), "deal_in"), composition);
}

/* A photo site whose composition offers no picture-led opening picks from
 * every such home opening, not only the first component that has one. */
static void
test_photo_fallback(void){
	g_autoptr(LsComponents) components = library();
	g_autoptr(GHashTable) kinds = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	guint s, fell = 0;
	for (s = 0; s < 400; s++) {
		g_autofree gchar *seed = g_strdup_printf("fallback-%u\nA business %u.", s, s);
		g_autoptr(JsonNode) plan = dealt_plan(components, seed, TRUE);
		g_autoptr(JsonNode) bare = dealt_plan(components, seed, FALSE);
		JsonObject *structure = object(json_node_get_object(plan), "structure");
		const gchar *composition = json_object_get_string_member_with_default(structure, "composition", NULL);
		const gchar *opening = json_object_get_string_member(structure, "opening");
		/* Only sites whose own opening and composition offer no picture
		 * fall back to the pool. */
		if ((composition != NULL && offers_led(composition)) ||
			is_picture_led(json_object_get_string_member(object(json_node_get_object(bare), "structure"), "opening")))
			continue;
		fell++;
		g_hash_table_add(kinds, g_strndup(opening, (gsize)(strchr(opening, '/') - opening)));
	}
	g_assert_cmpuint(fell, >=, 10);
	if (g_hash_table_size(kinds) < 2)
		g_error("%u fallback photo sites all open with one component", fell);
}

/* Supplied photos keep their colour: no site is dealt a black-and-white or
 * two-tone photo treatment, and the printed-ink artwork treatment leaves
 * photos alone. */
static void
test_photo_colour(void){
	g_autoptr(LsComponents) components = library();
	g_autoptr(GHashTable) seen = g_hash_table_new(g_str_hash, g_str_equal);
	guint s;
	for (s = 0; s < 3000; s++) {
		g_autofree gchar *seed = g_strdup_printf("colour-%u\nA business %u.", s, s);
		g_autoptr(JsonObject) look = ls_components_suggest_look(components, seed);
		const gchar *photos = json_object_get_string_member(look, "photos");
		if (g_strcmp0(photos, "mono") == 0 || g_strcmp0(photos, "duotone") == 0)
			g_error("seed %u drains its photos with %s", s, photos);
		g_hash_table_add(seen, (gpointer)json_object_get_string_member(look, "photos"));
	}
	g_assert_cmpuint(g_hash_table_size(seen), >=, 8);
	/* Gradient lettering clips its fill to the letters, wiping out the
	 * band of page colour a cover's headline sits on over the photo. */
	{
		gchar *css = NULL;
		g_assert_true(g_file_get_contents("data/components/look.css", &css, NULL, NULL));
		g_assert_null(strstr(css, ".look-lettering-gradient main h1 {"));
		g_assert_nonnull(strstr(css, ".look-lettering-gradient main h1:not(.cover h1, .poster--backdrop h1, .hero--banner h1) {"));
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

/* The slowest pace on the airiest density still keeps sections within
 * eight rem of space; the signature's divider shows at every third gap and
 * never adds a section's worth of space of its own. */
static void
test_gaps(void){
	g_autoptr(LsComponents) components = library();
	g_autoptr(JsonNode) plan = dealt_plan(components, "gaps\nA plumber.", FALSE);
	g_autoptr(JsonNode) site = NULL;
	JsonObject *root = json_node_get_object(plan);
	const gchar *css, *signature;
	json_object_set_double_member(object(root, "structure"), "pace", 2.1);
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
 * and keeps it when there is none. */
static void
test_photo_twice(void){
	g_autoptr(LsComponents) components = library();
	g_autoptr(JsonNode) plan = dealt_plan(components, "twice\nA landscaper.", TRUE);
	g_autoptr(JsonNode) page = json("{'path':'index.html','sections':["
		"{'component':'cover','variant':'full','content':{'heading':'Gardens you will sit in',"
		"'art':{'src':'assets/photos/patio.jpg','alt':'A sunny patio'}}},"
		"{'component':'media','variant':'inset','content':{'heading':'The patio',"
		"'art':{'src':'assets/photos/patio.jpg','alt':'A sunny patio'}}},"
		"{'component':'gallery','variant':'grid','content':{'items':["
		"{'art':{'src':'assets/photos/patio.jpg','alt':'A sunny patio'}},"
		"{'art':{'src':'assets/photos/lawn.jpg','alt':'A lawn with daisies'}}]}}]}");
	g_autoptr(GHashTable) used = g_hash_table_new(g_str_hash, g_str_equal);
	JsonArray *items;
	const gchar *media;
	g_assert_true(ls_components_spread_photos(components, plan, page));
	media = json_object_get_string_member(object(object(section(page, 1), "content"), "art"), "src");
	g_assert_cmpstr(media, !=, "assets/photos/patio.jpg");
	g_assert_true(g_str_has_prefix(media, "assets/photos/"));
	g_assert_cmpstr(json_object_get_string_member(object(object(section(page, 1), "content"), "art"), "alt"), !=, "A sunny patio");
	g_hash_table_add(used, "assets/photos/patio.jpg");
	g_hash_table_add(used, (gpointer)media);
	items = json_object_get_array_member(object(section(page, 2), "content"), "items");
	/* Three photos, four places: the last repeat keeps its photo. */
	g_assert_cmpstr(json_object_get_string_member(object(json_array_get_object_element(items, 0), "art"), "src"), !=,
		"assets/photos/patio.jpg");
	g_assert_false(g_hash_table_contains(used,
		json_object_get_string_member(object(json_array_get_object_element(items, 0), "art"), "src")));
	g_assert_cmpstr(json_object_get_string_member(object(json_array_get_object_element(items, 1), "art"), "src"), ==,
		"assets/photos/lawn.jpg");
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
	g_test_add_func("/wow/photo-fallback", test_photo_fallback);
	g_test_add_func("/wow/photo-colour", test_photo_colour);
	g_test_add_func("/wow/gaps", test_gaps);
	g_test_add_func("/wow/photo-twice", test_photo_twice);
	g_test_add_func("/wow/split-column", test_split_column);
	return g_test_run();
}
