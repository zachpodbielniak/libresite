/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* A mobile check of twenty real-model sites found six faults, each held
 * here and in the browser gate (tests/mobile.sh): the heading fitter
 * measured through a running entrance and squeezed text to 6px; a dealt
 * tint repainted an accent band under white text; desktop photos were
 * served for a third of the screen; breadcrumbs and links standing alone
 * were too small to tap; web fonts swapped in late and moved the page; and
 * portrait photos were cropped to a strip. With MOBILE_OUTPUT set, photo
 * sites are written out for the browser gate. See docs/components.org,
 * "Mobile findings". */
#include "libresite.h"
#include "core/ls-components.h"
#include "core/ls-images.h"
#include "core/ls-util.h"
#include "test-image-fixtures.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

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

static gchar *
read_file(const gchar *path){
	gchar *text = NULL;
	g_assert_true(g_file_get_contents(path, &text, NULL, NULL));
	return text;
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

/* A supplied photo: drawn, sized into its WebP variants as the images step
 * does, and described for the plan. Each variant's bytes are kept in
 * @files under its path. */
static JsonObject *
photo(const gchar *path, const gchar *alt, gint width, gint height, GHashTable *files){
	g_autoptr(GBytes) source = image_fixture(width, height, FALSE, ".jpg[Q=80]");
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) variants = ls_images_variants(path, source, NULL, &error);
	JsonObject *out = json_object_new();
	JsonArray *list = json_array_new();
	LsImageVariant *largest;
	guint i;
	g_assert_no_error(error);
	g_assert_cmpuint(variants->len, >=, 2);
	for (i = 0; i < variants->len; i++) {
		LsImageVariant *variant = g_ptr_array_index(variants, i);
		JsonObject *entry = json_object_new();
		json_object_set_string_member(entry, "path", variant->path);
		json_object_set_int_member(entry, "width", variant->width);
		json_object_set_int_member(entry, "height", variant->height);
		json_array_add_object_element(list, entry);
		g_hash_table_insert(files, g_strdup(variant->path), g_bytes_ref(variant->data));
	}
	largest = g_ptr_array_index(variants, variants->len - 1);
	json_object_set_string_member(out, "path", path);
	json_object_set_string_member(out, "alt", alt);
	json_object_set_int_member(out, "width", largest->width);
	json_object_set_int_member(out, "height", largest->height);
	json_object_set_array_member(out, "variants", list);
	return out;
}

/* Two landscape and two portrait photos, the shapes a real order brings. */
static JsonArray *
photos(GHashTable *files){
	JsonArray *out = json_array_new();
	json_array_add_object_element(out, photo("assets/photos/porch.jpg", "A painted porch in the evening", 1800, 1200, files));
	json_array_add_object_element(out, photo("assets/photos/table.jpg", "A long table set with flowers", 1100, 1650, files));
	json_array_add_object_element(out, photo("assets/photos/yard.jpg", "A tidy back yard", 1700, 1100, files));
	json_array_add_object_element(out, photo("assets/photos/door.jpg", "A tall front door", 1000, 1500, files));
	return out;
}

/* A home page that puts photos in the places real plans do: a wide strip,
 * a gallery led by a portrait, cards and a story; and the words that stand
 * alone as links: a contact card, a link line and a list of links. The
 * contact card is the padded outline: under a scaled entrance its padding
 * once read as overflow and squeezed its title to 6px. */
static const gchar *const home_json = "{'path':'index.html','sections':["
	"{'component':'hero','variant':'split','content':{'heading':'Porches, doors and yards, done properly',"
	"'lede':'Painting and repair for older houses.','actions':[{'href':'start.html','label':'Ask for an estimate'}],"
	"'art':{'src':'assets/photos/porch.jpg','alt':'A painted porch in the evening'}}},"
	"{'component':'media','variant':'cinema','content':{'heading':'Tables set for the evening',"
	"'body':'Low flowers run the length of the table.','art':{'src':'assets/photos/table.jpg','alt':'A long table set with flowers'}}},"
	"{'component':'gallery','variant':'wide','content':{'heading':'Recent work','items':["
	"{'art':{'src':'assets/photos/door.jpg','alt':'A tall front door'},'caption':'A front door, stripped and painted.'},"
	"{'art':{'src':'assets/photos/yard.jpg','alt':'A tidy back yard'},'caption':'A yard after the clear-up.'},"
	"{'art':{'src':'assets/photos/porch.jpg','alt':'A painted porch in the evening'},'caption':'The porch, finished.'}]}},"
	"{'component':'statement','variant':'band','content':{'text':'A quiet morning, a small crew, a house left tidy.',"
	"'link':{'href':'work.html','label':'See recent work'}}},"
	"{'component':'cards','variant':'grid','content':{'heading':'What we do','items':["
	"{'title':'Porches','body':'Rot repair and paint.','art':{'src':'assets/photos/porch.jpg','alt':'A painted porch in the evening'},'link':{'href':'work.html','label':'Porches'}},"
	"{'title':'Doors','body':'Stripping and refinishing.','art':{'src':'assets/photos/door.jpg','alt':'A tall front door'},'link':{'href':'work.html','label':'Doors'}},"
	"{'title':'Yards','body':'Clear-ups and fences.','art':{'src':'assets/photos/yard.jpg','alt':'A tidy back yard'},'link':{'href':'work.html','label':'Yards'}}]}},"
	"{'component':'text','variant':'narrow','content':{'heading':'Before we start','body':"
	"'We look at the whole job first and give you a written price. Read [how we price](start.html) before you call.\\n\\n"
	"[Read the full care guide](journal.html)\\n\\n- [Hours and directions](visit.html)\\n- [Recent work](work.html)'}},"
	"{'component':'contact-card','variant':'outline','content':{'heading':'Talk to Rosa','items':["
	"{'label':'Phone','value':'(555) 010-2254','link':{'href':'tel:+15550102254','label':'(555) 010-2254'}},"
	"{'label':'Workshop','value':'12 Wharf Street, Fennick Bay'}]}},"
	"{'component':'cta','variant':'band','content':{'heading':'Ready to start?','actions':[{'href':'start.html','label':'Ask for an estimate'}]}}]}";

/* The fixture plan dealt from @seed with the photos, its home page made
 * from home_json with the dealt opening, and the site composed. */
static JsonNode *
photo_site(LsComponents *components, const gchar *seed, const gchar *kit, GHashTable *files){
	const gchar *routes[] = {"work", "start", "journal", "visit"};
	g_autoptr(JsonNode) plan = load("tests/fixtures/composed/plan.json");
	g_autoptr(JsonObject) pages = json_object_new();
	g_autoptr(GError) error = NULL;
	g_autofree gchar *text = NULL;
	JsonNode *home = json(home_json);
	JsonObject *root = json_node_get_object(plan), *structure;
	guint i;
	json_object_set_array_member(root, "photos", photos(files));
	g_assert_true(ls_components_apply_seed(components, plan, seed, "trades", FALSE));
	json_object_set_member(root, "fonts",
		json("[{'family':'Fraunces','weights':[400,700],'italic':true},{'family':'Work Sans','weights':[400,600]}]"));
	structure = json_object_get_object_member(root, "structure");
	/* The page's own variants render as written, so every shape is met. */
	if (json_object_has_member(structure, "variants"))
		json_object_remove_member(structure, "variants");
	if (kit != NULL)
		json_object_set_string_member(structure, "kit", kit);
	ls_components_apply_opening(components, plan, "index.html", home);
	ls_components_spread_photos(components, plan, home);
	if (!ls_components_check_page(components, plan, "index.html", home, &error))
		g_error("%s: %s", seed, error->message);
	json_object_set_member(pages, "index.html", home);
	for (i = 0; i < G_N_ELEMENTS(routes); i++) {
		g_autofree gchar *fixture = g_strdup_printf("tests/fixtures/composed/%s.json", routes[i]);
		g_autofree gchar *route = g_strdup_printf("%s.html", routes[i]);
		json_object_set_member(pages, route, load(fixture));
	}
	if (!ls_components_check_plan(components, plan, &error))
		g_error("%s: %s", seed, error->message);
	text = ls_components_compose(components, plan, pages, NULL, &error);
	g_assert_no_error(error);
	return ls_json_parse(text, NULL);
}

/* The heading fitter measures the page at rest, with entrances lifted off,
 * and never shrinks text below 16px (or its own size). */
static void
test_fitter(void){
	g_autofree gchar *script = read_file("data/components/nav.js");
	g_assert_nonnull(strstr(script, "animation.effect = null;"));
	g_assert_nonnull(strstr(script, "atRest(fitAll);"));
	g_assert_nonnull(strstr(script, "floor = Math.max(size * 0.3, Math.min(size, 16));"));
	g_assert_null(strstr(script, "floor = size * 0.3;"));
}

/* A dealt tint or ink never repaints a section that paints its own accent
 * band; the accent band's text colour would land on the tint. */
static void
test_painted_bands(void){
	g_autofree gchar *css = read_file("data/components/look.css");
	const gchar *painted = ":not(.cta--band, .statement--band, .marquee--accent, .contact-card--band)";
	g_autofree gchar *tint = g_strconcat("main > .block.sec-tone.sec-tone-tint", painted, " {", NULL);
	g_autofree gchar *ink = g_strconcat("main > .block.sec-tone.sec-tone-ink", painted, ",", NULL);
	g_assert_nonnull(strstr(css, tint));
	g_assert_nonnull(strstr(css, ink));
	g_assert_null(strstr(css, "main > .block.sec-tone.sec-tone-tint {"));
}

/* A rich-text paragraph or list item that is one link and nothing else is
 * a link line, a full tap target; a link inside a sentence is not. */
static void
test_link_lines(void){
	g_autoptr(GError) error = NULL;
	g_autofree gchar *alone = ls_components_render_rich("[Read the full care guide](journal.html)", &error);
	g_autofree gchar *sentence = ls_components_render_rich("Read [how we price](start.html) first.", &error);
	g_autofree gchar *list = ls_components_render_rich("- [Hours and directions](visit.html)\n- Open [late](visit.html) on Fridays", &error);
	g_autofree gchar *two = ls_components_render_rich("[Hours](visit.html) [Work](work.html)", &error);
	g_assert_no_error(error);
	g_assert_cmpstr(alone, ==, "<p class=\"link-line\"><a href=\"journal.html\">Read the full care guide</a></p>\n");
	g_assert_cmpstr(sentence, ==, "<p>Read <a href=\"start.html\">how we price</a> first.</p>\n");
	g_assert_cmpstr(list, ==, "<ul><li class=\"link-line\"><a href=\"visit.html\">Hours and directions</a></li>"
		"<li>Open <a href=\"visit.html\">late</a> on Fridays</li></ul>\n");
	g_assert_null(strstr(two, "link-line"));
}

/* Standalone links, breadcrumbs, contents entries and footer links are at
 * least 44px square, and so is the label that toggles a checkbox. */
static void
test_tap_targets(void){
	g_autofree gchar *css = read_file("data/components/base.css");
	g_assert_nonnull(strstr(css, ":is(.crumbs, .page-contents) a, main :is(li, dd):not(.prose *) > a:only-child, "
		"main p[class] > a:only-child, main .prose .link-line > a,\n"
		"main :is(h2, h3, h4, cite, [class$=\"__title\"], [class$=\"__name\"]) > a:only-child {\n"
		"\tdisplay: inline-flex; align-items: center; min-height: 44px; min-width: 44px;\n}"));
	g_assert_nonnull(strstr(css, ".site-footer a { display: inline-flex; align-items: center; min-height: 44px; min-width: 44px; }"));
	/* A checkbox is small; its label is the target, a full 44px row. */
	g_assert_nonnull(strstr(css, ".field--check label { display: flex; align-items: center; min-height: 44px; }"));
}

/* WCAG contrast of two #rrggbb colours. */
static gdouble
contrast_of(const gchar *a, const gchar *b){
	const gchar *pair[] = {a, b};
	gdouble l[2];
	guint i, c;
	for (i = 0; i < 2; i++) {
		guint rgb[3];
		g_assert_cmpint(sscanf(pair[i], "#%02x%02x%02x", &rgb[0], &rgb[1], &rgb[2]), ==, 3);
		l[i] = 0;
		for (c = 0; c < 3; c++) {
			gdouble v = rgb[c] / 255.0;
			v = v <= 0.04045 ? v / 12.92 : pow((v + 0.055) / 1.055, 2.4);
			l[i] += v * (c == 0 ? 0.2126 : c == 1 ? 0.7152 : 0.0722);
		}
	}
	return (MAX(l[0], l[1]) + 0.05) / (MIN(l[0], l[1]) + 0.05);
}

/* The warning red must read on the surface panels it marks, not only on
 * the paper: a warning callout's title sits on the surface. */
static void
test_danger(void){
	g_autoptr(LsComponents) components = library();
	g_autoptr(JsonNode) plan = load("tests/fixtures/composed/plan.json");
	g_autoptr(JsonObject) pages = json_object_new();
	g_autoptr(GError) error = NULL;
	g_autofree gchar *text = NULL;
	g_autoptr(JsonNode) bundle = NULL;
	JsonObject *tokens = json_object_get_object_member(json_node_get_object(plan), "tokens");
	const gchar *routes[] = {"index", "work", "start", "journal", "visit"};
	guint i;
	/* #a4161a reads 7.4:1 on white but 4.2:1 on this grey. */
	json_object_set_string_member(tokens, "paper", "#ffffff");
	json_object_set_string_member(tokens, "surface", "#c4c4c4");
	json_object_set_string_member(tokens, "ink", "#111111");
	json_object_set_string_member(tokens, "muted", "#333333");
	for (i = 0; i < G_N_ELEMENTS(routes); i++) {
		g_autofree gchar *fixture = g_strdup_printf("tests/fixtures/composed/%s.json", routes[i]);
		g_autofree gchar *route = g_strdup_printf("%s.html", routes[i]);
		json_object_set_member(pages, route, load(fixture));
	}
	text = ls_components_compose(components, plan, pages, NULL, &error);
	g_assert_no_error(error);
	bundle = ls_json_parse(text, NULL);
	g_assert_nonnull(strstr(bundle_file(bundle, "styles.css"), "--danger: #111111;"));
	g_assert_null(strstr(bundle_file(bundle, "styles.css"), "--danger: #a4161a;"));
}

/* Panels on an accent band (question cards, a statement's box) take the
 * accent surface, which keeps the band's text at AA: lightening a mid
 * accent toward its white text once left the questions at 3.8:1. */
static void
test_accent_surface(void){
	g_autoptr(LsComponents) components = library();
	g_autoptr(JsonNode) plan = load("tests/fixtures/composed/plan.json");
	g_autoptr(JsonObject) pages = json_object_new();
	g_autoptr(GError) error = NULL;
	g_autofree gchar *text = NULL, *look = read_file("data/components/look.css");
	g_autoptr(JsonNode) bundle = NULL;
	JsonObject *tokens = json_object_get_object_member(json_node_get_object(plan), "tokens");
	const gchar *routes[] = {"index", "work", "start", "journal", "visit"}, *css, *at;
	gchar surface[8];
	guint i;
	json_object_set_string_member(tokens, "accent", "#ad539f");
	json_object_set_string_member(tokens, "accent_ink", "#ffffff");
	for (i = 0; i < G_N_ELEMENTS(routes); i++) {
		g_autofree gchar *fixture = g_strdup_printf("tests/fixtures/composed/%s.json", routes[i]);
		g_autofree gchar *route = g_strdup_printf("%s.html", routes[i]);
		json_object_set_member(pages, route, load(fixture));
	}
	text = ls_components_compose(components, plan, pages, NULL, &error);
	g_assert_no_error(error);
	bundle = ls_json_parse(text, NULL);
	css = bundle_file(bundle, "styles.css");
	at = strstr(css, "--accent-surface: #");
	g_assert_nonnull(at);
	g_strlcpy(surface, at + strlen("--accent-surface: "), sizeof surface);
	g_assert_cmpfloat(contrast_of(surface, "#ffffff"), >=, 4.5);
	g_assert_cmpstr(surface, !=, "#ad539f");
	g_assert_nonnull(strstr(look, "--ls-accent-surface: var(--accent-surface"));
	g_assert_nonnull(strstr(look, "--surface: var(--ls-accent-surface);"));
}

/* Web fonts never swap in late: optional display, and each family's Latin
 * files preloaded from the head with the stylesheet, at most eight. Photo
 * sites carry the script that sizes photos to their boxes and mats a
 * photo the box would crop to a strip. */
static void
test_fonts_and_photos(void){
	g_autoptr(LsComponents) components = library();
	g_autoptr(GHashTable) files = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify)g_bytes_unref);
	g_autoptr(JsonNode) site = photo_site(components, "fonts\nA house painter.", NULL, files);
	const gchar *html = bundle_file(site, "index.html");
	const gchar *script = bundle_file(site, "site.js");
	const gchar *css = bundle_file(site, "styles.css");
	const gchar *weights[] = {"fraunces-latin-400-normal", "fraunces-latin-400-italic", "fraunces-latin-700-normal",
		"fraunces-latin-700-italic", "work-sans-latin-400-normal", "work-sans-latin-600-normal"};
	const gchar *at;
	guint i, count = 0;
	g_assert_nonnull(html);
	g_assert_nonnull(strstr(html, "&amp;display=optional\">"));
	g_assert_null(strstr(html, "display=swap"));
	for (i = 0; i < G_N_ELEMENTS(weights); i++) {
		g_autofree gchar *family = g_strndup(weights[i], strstr(weights[i], "-latin-") - weights[i]);
		g_autofree gchar *link = g_strdup_printf("<link rel=\"preload\" href=\"https://fonts.bunny.net/%s/files/%s.woff2\" "
			"as=\"font\" type=\"font/woff2\" crossorigin>", family, weights[i]);
		if (strstr(html, link) == NULL)
			g_error("missing %s", link);
	}
	for (at = strstr(html, "rel=\"preload\""); at != NULL; at = strstr(at + 1, "rel=\"preload\""))
		count++;
	g_assert_cmpuint(count, ==, G_N_ELEMENTS(weights));
	/* The preloads come before the stylesheet that asks for the files. */
	g_assert_true(strstr(html, "rel=\"preload\"") < strstr(html, "fonts.bunny.net/css"));
	g_assert_nonnull(script);
	g_assert_nonnull(strstr(script, "img.sizes = Math.ceil("));
	g_assert_nonnull(strstr(script, "img.classList.add(\"is-matted\");"));
	g_assert_nonnull(css);
	g_assert_nonnull(strstr(css, "main img.is-matted { object-fit: contain !important;"));
	{
		g_autoptr(JsonNode) plan = load("tests/fixtures/composed/plan.json");
		g_autoptr(JsonObject) pages = json_object_new();
		g_autofree gchar *text = NULL;
		g_autoptr(JsonNode) bundle = NULL;
		const gchar *routes[] = {"index", "work", "start", "journal", "visit"};
		g_autoptr(GError) error = NULL;
		json_object_set_member(json_node_get_object(plan), "fonts", json("[{'family':'Inter','weights':[100,200,300,400,500,600],"
			"'italic':true},{'family':'Lora','weights':[400]}]"));
		for (i = 0; i < G_N_ELEMENTS(routes); i++) {
			g_autofree gchar *fixture = g_strdup_printf("tests/fixtures/composed/%s.json", routes[i]);
			g_autofree gchar *route = g_strdup_printf("%s.html", routes[i]);
			json_object_set_member(pages, route, load(fixture));
		}
		text = ls_components_compose(components, plan, pages, NULL, &error);
		g_assert_no_error(error);
		bundle = ls_json_parse(text, NULL);
		for (count = 0, at = strstr(bundle_file(bundle, "index.html"), "rel=\"preload\""); at != NULL; at = strstr(at + 1, "rel=\"preload\""))
			count++;
		g_assert_cmpuint(count, ==, 8);
	}
}

/* With MOBILE_OUTPUT, photo sites for the browser gate: one per seed, the
 * first with the spotlight kit whose entrance once squeezed headings. */
static void
test_emit(void){
	const gchar *output = g_getenv("MOBILE_OUTPUT");
	g_autoptr(LsComponents) components = library();
	guint s;
	if (output == NULL)
		return;
	for (s = 0; s < 8; s++) {
		g_autoptr(GHashTable) files = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify)g_bytes_unref);
		g_autofree gchar *seed = g_strdup_printf("mobile-%u\nA house painter %u.", s, s);
		g_autofree gchar *name = g_strdup_printf("photos-%u", s);
		g_autoptr(JsonNode) site = photo_site(components, seed, s == 0 ? "spotlight" : NULL, files);
		JsonArray *list = json_object_get_array_member(json_node_get_object(site), "files");
		GHashTableIter iter;
		gpointer path, bytes;
		guint i;
		for (i = 0; i < json_array_get_length(list); i++) {
			JsonObject *file = json_array_get_object_element(list, i);
			g_autofree gchar *out = g_build_filename(output, name, json_object_get_string_member(file, "path"), NULL);
			g_autofree gchar *parent = g_path_get_dirname(out);
			g_assert_cmpint(g_mkdir_with_parents(parent, 0700), ==, 0);
			g_assert_true(g_file_set_contents(out, json_object_get_string_member(file, "content"), -1, NULL));
		}
		g_hash_table_iter_init(&iter, files);
		while (g_hash_table_iter_next(&iter, &path, &bytes)) {
			g_autofree gchar *out = g_build_filename(output, name, (const gchar *)path, NULL);
			g_autofree gchar *parent = g_path_get_dirname(out);
			gsize size = 0;
			gconstpointer data = g_bytes_get_data(bytes, &size);
			g_assert_cmpint(g_mkdir_with_parents(parent, 0700), ==, 0);
			g_assert_true(g_file_set_contents(out, data, (gssize)size, NULL));
		}
		for (i = 0; i < 2; i++) {
			const gchar *art = i == 0 ? "bench.svg" : "mark.svg";
			g_autofree gchar *source = g_build_filename("tests/fixtures/composed", art, NULL);
			g_autofree gchar *out = g_build_filename(output, name, "assets", art, NULL);
			g_autofree gchar *svg = read_file(source);
			g_assert_true(g_file_set_contents(out, svg, -1, NULL));
		}
	}
}

int
main(int argc, char **argv){
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/mobile/fitter", test_fitter);
	g_test_add_func("/mobile/painted-bands", test_painted_bands);
	g_test_add_func("/mobile/link-lines", test_link_lines);
	g_test_add_func("/mobile/tap-targets", test_tap_targets);
	g_test_add_func("/mobile/danger", test_danger);
	g_test_add_func("/mobile/accent-surface", test_accent_surface);
	g_test_add_func("/mobile/fonts-and-photos", test_fonts_and_photos);
	g_test_add_func("/mobile/emit", test_emit);
	return g_test_run();
}
