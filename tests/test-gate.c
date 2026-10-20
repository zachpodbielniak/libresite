/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* The variety gate without a browser: across many dealt sites, every plan
 * keeps its WCAG AA colour pairs, every stylesheet keeps the reduced-motion
 * guard and its motion behind it, and every home page stays within the
 * page-weight budget, with the variety work itself costing little. The
 * browser half is tests/gate.sh. See docs/components.org, "Variety gate". */
#include "libresite.h"
#include "core/ls-components.h"
#include <math.h>
#include <string.h>

#define SITES 60
/* Home page without remote fonts: HTML, styles and script, raw and gzipped. */
#define WEIGHT_LIMIT 153600
#define GZIP_LIMIT 40960
/* What the composition, motion kit, art direction and extra variants may
 * add to a site's styles and script, raw. */
#define VARIETY_LIMIT 16384

static JsonNode *
load(const gchar *path){
	g_autofree gchar *text = NULL;
	g_assert_true(g_file_get_contents(path, &text, NULL, NULL));
	return ls_json_parse(text, NULL);
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

static gsize
gzipped(const gchar *text){
	g_autoptr(GZlibCompressor) compressor = g_zlib_compressor_new(G_ZLIB_COMPRESSOR_FORMAT_GZIP, 9);
	g_autoptr(GOutputStream) memory = g_memory_output_stream_new_resizable();
	g_autoptr(GOutputStream) stream = g_converter_output_stream_new(memory, G_CONVERTER(compressor));
	g_autoptr(GError) error = NULL;
	gsize written = 0;
	g_assert_true(g_output_stream_write_all(stream, text, strlen(text), &written, NULL, &error));
	g_assert_true(g_output_stream_close(stream, NULL, &error));
	g_assert_no_error(error);
	return g_memory_output_stream_get_data_size(G_MEMORY_OUTPUT_STREAM(memory));
}

static gdouble
channel(const gchar *hex, guint i){
	gchar pair[3] = {hex[1 + 2 * i], hex[2 + 2 * i], 0};
	gdouble c = g_ascii_strtoull(pair, NULL, 16) / 255.0;
	return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}

static gdouble
ratio(const gchar *a, const gchar *b){
	gdouble x = 0.2126 * channel(a, 0) + 0.7152 * channel(a, 1) + 0.0722 * channel(a, 2);
	gdouble y = 0.2126 * channel(b, 0) + 0.7152 * channel(b, 1) + 0.0722 * channel(b, 2);
	return (MAX(x, y) + 0.05) / (MIN(x, y) + 0.05);
}

/* The fixture site dealt from @seed, its home opening and order applied as
 * the pages step applies them; with @plain, without the variety work. */
static gchar *
compose_dealt(LsComponents *components, const gchar *seed, gboolean plain, JsonNode **plan_out){
	const gchar *routes[] = {"index", "work", "start", "journal", "visit"};
	g_autoptr(JsonNode) plan = load("tests/fixtures/composed/plan.json");
	g_autoptr(JsonObject) pages = json_object_new();
	g_autoptr(GError) error = NULL;
	gchar *text;
	guint i;
	g_assert_true(ls_components_apply_seed(components, plan, seed, "trades", FALSE));
	if (plain) {
		const gchar *extras[] = {"composition", "kit", "direction"};
		JsonObject *structure = json_object_get_object_member(json_node_get_object(plan), "structure");
		for (i = 0; i < G_N_ELEMENTS(extras); i++)
			if (json_object_has_member(structure, extras[i]))
				json_object_remove_member(structure, extras[i]);
		json_object_remove_member(structure, "variants");
	}
	for (i = 0; i < G_N_ELEMENTS(routes); i++) {
		g_autofree gchar *fixture = g_strdup_printf("tests/fixtures/composed/%s.json", routes[i]);
		g_autofree gchar *route = g_strdup_printf("%s.html", routes[i]);
		JsonNode *page = load(fixture);
		if (i == 0) {
			ls_components_apply_opening(components, plan, route, page);
			ls_components_apply_composition(components, plan, page);
		}
		json_object_set_member(pages, route, page);
	}
	text = ls_components_compose(components, plan, pages, NULL, &error);
	g_assert_no_error(error);
	if (plan_out != NULL)
		*plan_out = g_steal_pointer(&plan);
	return text;
}

static void
test_sites(void){
	g_autoptr(GError) error = NULL;
	g_autoptr(LsComponents) components = ls_components_new(&error);
	gsize heaviest = 0, heaviest_gzip = 0, costliest = 0;
	guint s;
	g_assert_no_error(error);
	for (s = 0; s < SITES; s++) {
		g_autofree gchar *seed = g_strdup_printf("gate-%u\nAn illustrative home-services business %u.", s, s);
		g_autoptr(JsonNode) plan = NULL;
		g_autofree gchar *text = compose_dealt(components, seed, FALSE, &plan);
		g_autofree gchar *plain_text = compose_dealt(components, seed, TRUE, NULL);
		g_autoptr(JsonNode) bundle = ls_json_parse(text, NULL);
		g_autoptr(JsonNode) plain = ls_json_parse(plain_text, NULL);
		JsonObject *tokens = json_object_get_object_member(json_node_get_object(plan), "tokens");
		const gchar *pairs[][2] = {{"ink", "paper"}, {"ink", "surface"}, {"muted", "paper"}, {"muted", "surface"},
			{"accent_ink", "accent"}};
		const gchar *index = bundle_file(bundle, "index.html"), *css = bundle_file(bundle, "styles.css");
		const gchar *js = bundle_file(bundle, "site.js");
		g_autofree gchar *home = g_strconcat(index, css, js, NULL);
		gsize weight = strlen(home), zipped = gzipped(home);
		gsize cost = strlen(css) + strlen(js) - strlen(bundle_file(plain, "styles.css")) - strlen(bundle_file(plain, "site.js"));
		guint p;
		/* Contrast: every text pair at AA, and the plan passes its checks. */
		for (p = 0; p < G_N_ELEMENTS(pairs); p++)
			g_assert_cmpfloat(ratio(json_object_get_string_member(tokens, pairs[p][0]),
				json_object_get_string_member(tokens, pairs[p][1])), >=, 4.5);
		g_assert_true(ls_components_check_plan(components, plan, &error));
		g_assert_no_error(error);
		/* Reduced motion: the library's guard is in every stylesheet, and the
		 * motion kit's movement sits behind its no-preference block. */
		g_assert_nonnull(strstr(css, "@media (prefers-reduced-motion: reduce)"));
		g_assert_nonnull(strstr(css, "animation-duration: 0.01ms !important"));
		{
			const gchar *kit = strstr(css, "/* motion kit */");
			if (kit != NULL)
				g_assert_nonnull(strstr(kit, "@media (prefers-reduced-motion: no-preference)"));
		}
		/* Weight. */
		g_assert_cmpuint(weight, <=, WEIGHT_LIMIT);
		g_assert_cmpuint(zipped, <=, GZIP_LIMIT);
		g_assert_cmpuint(cost, <=, VARIETY_LIMIT);
		heaviest = MAX(heaviest, weight);
		heaviest_gzip = MAX(heaviest_gzip, zipped);
		costliest = MAX(costliest, cost);
	}
	g_test_message("%u sites: heaviest home page %" G_GSIZE_FORMAT " bytes (limit %d), %" G_GSIZE_FORMAT
		" gzipped (limit %d); variety adds at most %" G_GSIZE_FORMAT " bytes (limit %d)", SITES, heaviest,
		WEIGHT_LIMIT, heaviest_gzip, GZIP_LIMIT, costliest, VARIETY_LIMIT);
}

int
main(int argc, char **argv){
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/gate/sites", test_sites);
	return g_test_run();
}
