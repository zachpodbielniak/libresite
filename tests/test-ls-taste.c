/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* Taste (epic #17 child 23): the builder deals only calm systems, visible
 * copy never says "illustrative", headlines are statements, every page of a
 * photo site shows a photograph and no page wears letter chips. A site
 * that breaks any of these fails ls_components_check_taste(). */
#include "libresite.h"
#include "core/ls-components.h"
#include "core/ls-taste.h"
#include "core/ls-util.h"
#include "core/ls-visual.h"
#include <stdio.h>
#include <string.h>

static JsonNode *
read_json(const gchar *path){
	g_autofree gchar *text = NULL;
	g_assert_true(g_file_get_contents(path, &text, NULL, NULL));
	return ls_json_parse(text, NULL);
}

static JsonNode *
photo_plan(void){
	JsonNode *plan = read_json("tests/fixtures/composed/plan.json");
	g_autoptr(JsonNode) bar = read_json("tests/fixtures/bar.json");
	json_object_set_member(json_node_get_object(plan), "photos",
		json_node_copy(json_object_get_member(json_node_get_object(bar), "photos")));
	return plan;
}

/* The section content at @index of @page. */
static JsonObject *
content_of(JsonNode *page, guint index){
	JsonArray *sections = json_object_get_array_member(json_node_get_object(page), "sections");
	return json_object_get_object_member(json_array_get_object_element(sections, index), "content");
}

static void
test_disclosure_words(void){
	g_assert_cmpstr(ls_taste_disclosure("Highfold Roofing is an illustrative business.", FALSE), ==, "illustrative");
	g_assert_cmpstr(ls_taste_disclosure("Illustrative food truck", FALSE), ==, "illustrative");
	g_assert_nonnull(ls_taste_disclosure("These photographs are stock imagery used as visual references.", FALSE));
	g_assert_nonnull(ls_taste_disclosure("Each photograph is a stock photo, not our work.", FALSE));
	g_assert_nonnull(ls_taste_disclosure("This is an invented business.", FALSE));
	/* The footer may disclose, in other words. */
	g_assert_null(ls_taste_disclosure("Example business. Photos: StockSnap, CC0.", TRUE));
	g_assert_null(ls_taste_disclosure("An invented business; stock photography.", TRUE));
	g_assert_cmpstr(ls_taste_disclosure("Illustrative business. Stock photography.", TRUE), ==, "illustrative");
	/* Ordinary words that merely contain the letters pass. */
	g_assert_null(ls_taste_disclosure("We illustrate options with drawings and keep stock of parts.", FALSE));
}

static void
test_illustrative_copy_fails(void){
	g_autoptr(GError) error = NULL;
	g_autoptr(LsComponents) library = ls_components_new(&error);
	g_autoptr(JsonNode) plan = read_json("tests/fixtures/composed/plan.json");
	g_autoptr(JsonNode) page = read_json("tests/fixtures/composed/index.json");
	g_assert_no_error(error);
	g_assert_true(ls_components_check_page(library, plan, "index.html", page, &error));
	g_assert_no_error(error);
	json_object_set_string_member(content_of(page, 0), "lede", "Nightjar Ink is an illustrative tattoo shop.");
	g_assert_false(ls_components_check_page(library, plan, "index.html", page, &error));
	g_assert_nonnull(error);
	g_assert_nonnull(strstr(error->message, "illustrative"));
	g_clear_error(&error);
	json_object_set_string_member(content_of(page, 0), "lede",
		"These photographs are stock imagery used as visual references.");
	g_assert_false(ls_components_check_page(library, plan, "index.html", page, NULL));
	/* The footer note discloses without the word. */
	json_object_set_string_member(json_object_get_object_member(json_node_get_object(plan), "footer"), "note",
		"Illustrative business. Stock photography.");
	g_assert_false(ls_components_check_plan(library, plan, NULL));
	json_object_set_string_member(json_object_get_object_member(json_node_get_object(plan), "footer"), "note",
		"An example business. Stock photography from StockSnap, CC0.");
	g_assert_true(ls_components_check_plan(library, plan, &error));
	g_assert_no_error(error);
}

static void
test_question_headline_fails(void){
	g_autoptr(LsComponents) library = ls_components_new(NULL);
	g_autoptr(JsonNode) plan = read_json("tests/fixtures/composed/plan.json");
	g_autoptr(JsonNode) page = read_json("tests/fixtures/composed/index.json");
	g_autoptr(JsonNode) visual = read_json("tests/fixtures/bar.json");
	g_autoptr(GError) error = NULL;
	JsonObject *root = json_node_get_object(visual);
	json_object_set_string_member(content_of(page, 0), "heading", "Is your roof doing its job?");
	g_assert_false(ls_components_check_page(library, plan, "index.html", page, &error));
	g_assert_nonnull(strstr(error->message, "question"));
	json_object_set_string_member(content_of(page, 0), "heading", "Roof inspections and repairs");
	g_assert_true(ls_components_check_page(library, plan, "index.html", page, NULL));
	json_object_set_string_member(json_object_get_object_member(root, "visual"), "headline", "Will this be a treat?");
	g_assert_false(ls_visual_check_plan(root, NULL));
}

/* Over many seeds the builder never deals a gimmick. */
static void
test_no_gimmicks_dealt(void){
	static const gchar *const banned[][2] = {
		{"images", "tilt"}, {"images", "blob"}, {"images", "taped"}, {"lettering", "outline"},
		{"lettering", "gradient"}, {"emphasis", "outline"}, {"texture", "dots"}, {"texture", "aurora"},
		{"backdrop", "glow"}, {"backdrop", "mesh"}, {"edges", "torn"}, {"photos", "duotone"},
		{"photos", "arch"}, {"nav", "circled"}, {"nav", "pill"}, {"buttons", "knob"}, {"buttons", "chrome"},
		{"heads", "disc"}, {"panels", "glass"}, {"panels", "folded"}, {"footer", "arched"}, {"footer", "chips"},
		{"header", "floating"}, {"header", "glass"}};
	static const gchar *const banned_structure[][2] = {
		{"kit", "spotlight"}, {"kit", "aurora"}, {"kit", "hinge"}, {"kit", "ticker"}, {"kit", "parallax"},
		{"direction", "collage"}, {"direction", "stamp"}, {"direction", "window"}, {"direction", "duotone"}, {"direction", "torn"},
		{"opening", "hero/brutal"}, {"opening", "cover/stub"}, {"opening", "cover/flood"},
		{"opening", "manifesto/stepped"}, {"opening", "hero/kinetic"},
		{"angle", "question"}, {"angle", "character"}};
	g_autoptr(LsComponents) library = ls_components_new(NULL);
	guint s, i;
	for (s = 0; s < 240; s++) {
		g_autofree gchar *seed = g_strdup_printf("taste-%u", s);
		g_autoptr(JsonNode) plan = s % 2 == 0 ? photo_plan() : read_json("tests/fixtures/composed/plan.json");
		JsonObject *root = json_node_get_object(plan), *look, *structure, *variants, *forms;
		JsonArray *extras;
		g_assert_true(ls_components_apply_seed(library, plan, seed, "trades", FALSE));
		look = json_object_get_object_member(root, "look");
		structure = json_object_get_object_member(root, "structure");
		for (i = 0; i < G_N_ELEMENTS(banned); i++)
			if (g_strcmp0(ls_string(look, banned[i][0]), banned[i][1]) == 0)
				g_error("seed %s dealt look %s %s", seed, banned[i][0], banned[i][1]);
		for (i = 0; i < G_N_ELEMENTS(banned_structure); i++)
			if (g_strcmp0(ls_string(structure, banned_structure[i][0]), banned_structure[i][1]) == 0)
				g_error("seed %s dealt %s %s", seed, banned_structure[i][0], banned_structure[i][1]);
		variants = json_object_get_object_member(structure, "variants");
		g_assert_cmpstr(ls_string(variants, "cards"), !=, "polaroid");
		g_assert_cmpstr(ls_string(variants, "gallery"), !=, "polaroid");
		/* Nor a banned opening through the per-component variants. */
		g_assert_cmpstr(ls_string(variants, "hero"), !=, "arch");
		g_assert_cmpstr(ls_string(variants, "hero"), !=, "brutal");
		g_assert_cmpstr(ls_string(variants, "cover"), !=, "stub");
		forms = json_object_get_object_member(structure, "forms");
		if (forms != NULL) {
			g_assert_cmpstr(ls_string(forms, "offer"), !=, "directory");
			g_assert_cmpstr(ls_string(forms, "practical"), !=, "glossary");
			g_assert_cmpstr(ls_string(forms, "process"), !=, "jobsheet");
		}
		extras = json_object_get_array_member(structure, "extras");
		for (i = 0; extras != NULL && i < json_array_get_length(extras); i++)
			g_assert_cmpstr(json_array_get_string_element(extras, i), !=, "marquee");
	}
}

/* A light, coloured ground: chroma over the limit with its brightest
 * channel at least 0.45. Deep colours stay, however saturated. */
static gboolean
loud(const gchar *hex){
	guint r = 0, g = 0, b = 0;
	if (hex == NULL || sscanf(hex, "#%02x%02x%02x", &r, &g, &b) != 3)
		return FALSE;
	return ls_taste_chroma(hex) > 0.16 && MAX(MAX(r, g), b) / 255.0 >= 0.45;
}

/* Vivid and not dark (HSL lightness over 0.45): what reads as neon over
 * a whole band. A deep rust or navy is strong, not neon. */
static gboolean
neon(const gchar *hex){
	guint r = 0, g = 0, b = 0;
	if (hex == NULL || sscanf(hex, "#%02x%02x%02x", &r, &g, &b) != 3)
		return FALSE;
	return ls_taste_chroma(hex) > 0.35 && (MAX(MAX(r, g), b) + MIN(MIN(r, g), b)) / 510.0 > 0.45;
}

/* Page and surface stay calm; no neon or loud backgrounds. */
static void
test_calm_palette(void){
	g_autoptr(LsComponents) library = ls_components_new(NULL);
	guint s;
	g_assert_cmpfloat(ls_taste_chroma("#99ffff"), >, 0.16);
	g_assert_cmpfloat(ls_taste_chroma("#f3f0e9"), <=, 0.16);
	for (s = 0; s < 240; s++) {
		g_autofree gchar *seed = g_strdup_printf("palette-%u", s);
		g_autoptr(JsonNode) plan = read_json("tests/fixtures/composed/plan.json");
		g_autoptr(GError) error = NULL;
		JsonObject *tokens;
		g_assert_true(ls_components_apply_seed(library, plan, seed, s % 3 == 0 ? "food" : "trades", FALSE));
		tokens = json_object_get_object_member(json_node_get_object(plan), "tokens");
		if (loud(ls_string(tokens, "paper")) || loud(ls_string(tokens, "surface")))
			g_error("seed %s paper %s surface %s", seed, ls_string(tokens, "paper"), ls_string(tokens, "surface"));
		/* The accent fills bands too: never neon. */
		if (neon(ls_string(tokens, "accent")))
			g_error("seed %s accent %s is neon", seed, ls_string(tokens, "accent"));
		g_assert_true(ls_components_check_identity(library, plan, NULL, &error));
		g_assert_no_error(error);
	}
	/* The visual sequence's material worlds too. */
	{
		const gchar *worlds[] = {"chalk", "copper", "forest", "electric", "ceramic", "oven"};
		g_autoptr(JsonNode) bar = read_json("tests/fixtures/bar.json");
		for (s = 0; s < G_N_ELEMENTS(worlds); s++) {
			g_autoptr(JsonNode) plan = read_json("tests/fixtures/composed/plan.json");
			JsonObject *root = json_node_get_object(plan), *extra = json_node_get_object(bar), *tokens;
			g_autoptr(GError) error = NULL;
			json_object_set_member(root, "photos", json_node_copy(json_object_get_member(extra, "photos")));
			json_object_set_member(root, "media", json_node_copy(json_object_get_member(extra, "media")));
			json_object_set_member(root, "visual", json_node_copy(json_object_get_member(extra, "visual")));
			json_object_set_string_member(json_object_get_object_member(root, "visual"), "world", worlds[s]);
			g_assert_true(ls_components_apply_seed(library, plan, "world", "trades", FALSE));
			tokens = json_object_get_object_member(root, "tokens");
			if (neon(ls_string(tokens, "accent")) || loud(ls_string(tokens, "paper")))
				g_error("world %s: paper %s accent %s", worlds[s], ls_string(tokens, "paper"), ls_string(tokens, "accent"));
			g_assert_true(ls_components_check_identity(library, plan, NULL, &error));
			g_assert_no_error(error);
		}
	}
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

/* Every page of a photo site shows a photograph once the builder has seen
 * it, inner pages included. */
static void
test_photo_every_page(void){
	const gchar *routes[] = {"index", "work", "start", "journal", "visit"};
	g_autoptr(LsComponents) library = ls_components_new(NULL);
	g_autoptr(JsonNode) plan = photo_plan(), bundle = NULL;
	g_autoptr(JsonObject) pages = json_object_new();
	g_autoptr(GError) error = NULL;
	g_autoptr(GString) problems = g_string_new(NULL);
	g_autofree gchar *text = NULL;
	guint i;
	g_assert_true(ls_components_apply_seed(library, plan, "photo-every-page", "trades", FALSE));
	for (i = 0; i < G_N_ELEMENTS(routes); i++) {
		g_autofree gchar *fixture = g_strdup_printf("tests/fixtures/composed/%s.json", routes[i]);
		g_autofree gchar *route = g_strdup_printf("%s.html", routes[i]);
		JsonNode *page = read_json(fixture);
		ls_components_spread_photos(library, plan, page);
		ls_components_ensure_photo(library, plan, route, page);
		g_assert_true(ls_components_check_page(library, plan, route, page, &error));
		g_assert_no_error(error);
		json_object_set_member(pages, route, page);
	}
	text = ls_components_compose(library, plan, pages, NULL, &error);
	g_assert_no_error(error);
	bundle = ls_json_parse(text, NULL);
	for (i = 0; i < G_N_ELEMENTS(routes); i++) {
		g_autofree gchar *route = g_strdup_printf("%s.html", routes[i]);
		const gchar *html = bundle_file(bundle, route);
		g_assert_nonnull(html);
		if (strstr(html, "src=\"assets/room") == NULL && strstr(html, "src=\"assets/detail") == NULL &&
			strstr(html, "src=\"assets/proof") == NULL)
			g_error("%s shows no photograph", route);
	}
	if (!ls_components_check_taste(library, plan, bundle, problems))
		g_error("%s", problems->str);
}

/* The site check fails a site with each forbidden thing. */
static void
test_site_check(void){
	static const struct { const gchar *path, *from, *to, *expect; } cases[] = {
		{"index.html", "</main>", "<p>An illustrative business.</p></main>", "illustrative"},
		{"index.html", "</footer>", "<p>Illustrative business.</p></footer>", "illustrative"},
		{"work.html", "<main", "<h1>Will this be a treat?</h1><main", "question"},
		{"work.html", "<body class=\"", "<body class=\"look-images-tilt ", "tilt"},
		{"work.html", "<body class=\"", "<body class=\"look-lettering-outline ", "outline"},
		{"work.html", "<main", "<section class=\"block block--cards cards cards--polaroid\"></section><main", "polaroid"},
		{"work.html", "<main", "<nav class=\"directory__letters\"><ul><li><a href=\"#a\">C</a></li><li><a href=\"#b\">D</a></li></ul></nav><main", "letter"},
		{"work.html", "<body class=\"", "<body class=\"kit-spotlight ", "spotlight"},
		{"styles.css", "--paper:", "--paper: #99ffff; --was-paper:", "colour"}
	};
	g_autoptr(LsComponents) library = ls_components_new(NULL);
	guint c;
	for (c = 0; c < G_N_ELEMENTS(cases); c++) {
		const gchar *routes[] = {"index", "work", "start", "journal", "visit"};
		g_autoptr(JsonNode) plan = photo_plan(), bundle = NULL;
		g_autoptr(JsonObject) pages = json_object_new();
		g_autoptr(GString) problems = g_string_new(NULL);
		g_autoptr(GError) error = NULL;
		g_autofree gchar *text = NULL;
		JsonArray *files;
		guint i;
		g_assert_true(ls_components_apply_seed(library, plan, "site-check", "trades", FALSE));
		for (i = 0; i < G_N_ELEMENTS(routes); i++) {
			g_autofree gchar *fixture = g_strdup_printf("tests/fixtures/composed/%s.json", routes[i]);
			g_autofree gchar *route = g_strdup_printf("%s.html", routes[i]);
			JsonNode *page = read_json(fixture);
			ls_components_ensure_photo(library, plan, route, page);
			json_object_set_member(pages, route, page);
		}
		text = ls_components_compose(library, plan, pages, NULL, &error);
		g_assert_no_error(error);
		bundle = ls_json_parse(text, NULL);
		if (!ls_components_check_taste(library, plan, bundle, problems))
			g_error("case %u before the change: %s", c, problems->str);
		files = json_object_get_array_member(json_node_get_object(bundle), "files");
		for (i = 0; i < json_array_get_length(files); i++) {
			JsonObject *f = json_array_get_object_element(files, i);
			if (g_strcmp0(ls_string(f, "path"), cases[c].path) == 0) {
				const gchar *content = ls_string(f, "content");
				const gchar *at = strstr(content, cases[c].from);
				g_autofree gchar *head = NULL, *changed = NULL;
				g_assert_nonnull(at);
				head = g_strndup(content, (gsize)(at - content));
				changed = g_strconcat(head, cases[c].to, at + strlen(cases[c].from), NULL);
				json_object_set_string_member(f, "content", changed);
			}
		}
		g_assert_false(ls_components_check_taste(library, plan, bundle, problems));
		if (strstr(problems->str, cases[c].expect) == NULL)
			g_error("case %u: expected \"%s\" in: %s", c, cases[c].expect, problems->str);
	}
	/* A photo site's page without a photograph fails too. */
	{
		g_autoptr(JsonNode) plan = photo_plan(), bundle = NULL;
		g_autoptr(JsonObject) pages = json_object_new();
		g_autoptr(GString) problems = g_string_new(NULL);
		g_autofree gchar *text = NULL;
		const gchar *routes[] = {"index", "work", "start", "journal", "visit"};
		guint i;
		g_assert_true(ls_components_apply_seed(library, plan, "site-check", "trades", FALSE));
		for (i = 0; i < G_N_ELEMENTS(routes); i++) {
			g_autofree gchar *fixture = g_strdup_printf("tests/fixtures/composed/%s.json", routes[i]);
			g_autofree gchar *route = g_strdup_printf("%s.html", routes[i]);
			json_object_set_member(pages, route, read_json(fixture));
		}
		text = ls_components_compose(library, plan, pages, NULL, NULL);
		bundle = ls_json_parse(text, NULL);
		g_assert_false(ls_components_check_taste(library, plan, bundle, problems));
		g_assert_nonnull(strstr(problems->str, "photograph"));
	}
}

/* A writer may name a gimmick variant itself; the builder renders a calm
 * one instead. */
static void
test_writer_gimmick_variant(void){
	g_autoptr(LsComponents) library = ls_components_new(NULL);
	g_autoptr(JsonNode) plan = read_json("tests/fixtures/composed/plan.json"), bundle = NULL;
	g_autoptr(JsonObject) pages = json_object_new();
	g_autoptr(GError) error = NULL;
	g_autofree gchar *text = NULL;
	const gchar *routes[] = {"index", "work", "start", "journal", "visit"};
	JsonNode *page;
	guint i;
	g_assert_true(ls_components_apply_seed(library, plan, "writer-gimmick", "trades", FALSE));
	for (i = 0; i < G_N_ELEMENTS(routes); i++) {
		g_autofree gchar *fixture = g_strdup_printf("tests/fixtures/composed/%s.json", routes[i]);
		g_autofree gchar *route = g_strdup_printf("%s.html", routes[i]);
		json_object_set_member(pages, route, read_json(fixture));
	}
	page = json_object_get_member(pages, "work.html");
	json_array_add_element(json_object_get_array_member(json_node_get_object(page), "sections"), ls_json_parse(
		"{\"component\": \"cards\", \"variant\": \"polaroid\", \"content\": {\"heading\": \"Pieces\", "
		"\"items\": [{\"title\": \"Chair\"}, {\"title\": \"Table\"}]}}", NULL));
	json_object_set_string_member(json_array_get_object_element(json_object_get_array_member(
		json_node_get_object(json_object_get_member(pages, "index.html")), "sections"), 0), "variant", "arch");
	json_object_set_member(json_node_get_object(plan), "structure", ls_json_parse("{\"variants\": {}}", NULL));
	text = ls_components_compose(library, plan, pages, NULL, &error);
	g_assert_no_error(error);
	bundle = ls_json_parse(text, NULL);
	g_assert_null(strstr(bundle_file(bundle, "work.html"), "cards--polaroid"));
	g_assert_null(strstr(bundle_file(bundle, "index.html"), "hero--arch"));
	g_assert_nonnull(strstr(bundle_file(bundle, "work.html"), "block--cards"));
}

/* Stock or drawn media carry no caption saying so: the footer note does. */
static void
test_stock_caption(void){
	g_autoptr(LsComponents) library = ls_components_new(NULL);
	g_autoptr(JsonNode) plan = read_json("tests/fixtures/composed/plan.json"), bundle = NULL;
	g_autoptr(JsonNode) bar = read_json("tests/fixtures/bar.json");
	g_autoptr(JsonObject) pages = json_object_new();
	g_autoptr(GError) error = NULL;
	g_autofree gchar *text = NULL;
	const gchar *routes[] = {"index", "work", "start", "journal", "visit"};
	JsonObject *root = json_node_get_object(plan), *extra = json_node_get_object(bar);
	guint i;
	json_object_set_member(root, "photos", json_node_copy(json_object_get_member(extra, "photos")));
	json_object_set_member(root, "media", json_node_copy(json_object_get_member(extra, "media")));
	json_object_set_member(root, "visual", json_node_copy(json_object_get_member(extra, "visual")));
	json_object_set_string_member(json_object_get_object_member(root, "media"), "kind", "stock");
	json_object_set_string_member(json_object_get_object_member(root, "media"), "source", "CC0 stock photograph");
	g_assert_true(ls_components_apply_seed(library, plan, "stock-caption", "trades", FALSE));
	for (i = 0; i < G_N_ELEMENTS(routes); i++) {
		g_autofree gchar *fixture = g_strdup_printf("tests/fixtures/composed/%s.json", routes[i]);
		g_autofree gchar *route = g_strdup_printf("%s.html", routes[i]);
		JsonNode *node = read_json(fixture);
		ls_visual_apply_page(root, json_node_get_object(node));
		json_object_set_member(pages, route, node);
	}
	text = ls_components_compose(library, plan, pages, NULL, &error);
	g_assert_no_error(error);
	bundle = ls_json_parse(text, NULL);
	g_assert_nonnull(strstr(bundle_file(bundle, "index.html"), "scene--"));
	g_assert_null(strstr(bundle_file(bundle, "index.html"), "CC0 stock photograph"));
	g_assert_null(strstr(bundle_file(bundle, "index.html"), "scene__credit"));
}

int
main(int argc, char **argv){
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/taste/disclosure-words", test_disclosure_words);
	g_test_add_func("/taste/illustrative-copy-fails", test_illustrative_copy_fails);
	g_test_add_func("/taste/question-headline-fails", test_question_headline_fails);
	g_test_add_func("/taste/no-gimmicks-dealt", test_no_gimmicks_dealt);
	g_test_add_func("/taste/calm-palette", test_calm_palette);
	g_test_add_func("/taste/photo-every-page", test_photo_every_page);
	g_test_add_func("/taste/site-check", test_site_check);
	g_test_add_func("/taste/writer-gimmick-variant", test_writer_gimmick_variant);
	g_test_add_func("/taste/stock-caption", test_stock_caption);
	return g_test_run();
}
