/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* The gate without a browser: the default design keeps its WCAG AA colour
 * pairs, its stylesheet keeps the reduced-motion guard, and its home page
 * stays within the page-weight budget. The browser half is tests/gate.sh;
 * with GATE_OUTPUT set, the site is written out for it.
 * See docs/components.org, "Variety gate". */
#include "libresite.h"
#include "core/ls-components.h"
#include <math.h>
#include <string.h>

/* Home page without remote fonts: HTML, styles and script, raw and gzipped. */
#define WEIGHT_LIMIT 153600
#define GZIP_LIMIT 40960

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

/* The fixture site in the default design, its home opening applied as the
 * pages step applies it. */
static gchar *
compose_default(LsComponents *components, JsonNode **plan_out){
	const gchar *routes[] = {"index", "work", "start", "journal", "visit"};
	g_autoptr(JsonNode) plan = load("tests/fixtures/composed/plan.json");
	g_autoptr(JsonObject) pages = json_object_new();
	g_autoptr(GError) error = NULL;
	gchar *text;
	guint i;
	g_assert_true(ls_components_apply_default(components, plan, FALSE));
	for (i = 0; i < G_N_ELEMENTS(routes); i++) {
		g_autofree gchar *fixture = g_strdup_printf("tests/fixtures/composed/%s.json", routes[i]);
		g_autofree gchar *route = g_strdup_printf("%s.html", routes[i]);
		JsonNode *page = load(fixture);
		if (i == 0)
			ls_components_apply_opening(components, plan, route, page);
		json_object_set_member(pages, route, page);
	}
	text = ls_components_compose(components, plan, pages, NULL, &error);
	g_assert_no_error(error);
	*plan_out = g_steal_pointer(&plan);
	return text;
}

static void
test_site(void){
	g_autoptr(GError) error = NULL;
	g_autoptr(LsComponents) components = ls_components_new(&error);
	g_autoptr(JsonNode) plan = NULL;
	g_autofree gchar *text = NULL, *home = NULL;
	g_autoptr(JsonNode) bundle = NULL;
	JsonObject *tokens;
	const gchar *pairs[][2] = {{"ink", "paper"}, {"ink", "surface"}, {"muted", "paper"}, {"muted", "surface"},
		{"accent_ink", "accent"}};
	const gchar *css;
	gsize weight, zipped;
	guint p;
	g_assert_no_error(error);
	text = compose_default(components, &plan);
	bundle = ls_json_parse(text, NULL);
	tokens = json_object_get_object_member(json_node_get_object(plan), "tokens");
	css = bundle_file(bundle, "styles.css");
	home = g_strconcat(bundle_file(bundle, "index.html"), css, bundle_file(bundle, "site.js"), NULL);
	weight = strlen(home);
	zipped = gzipped(home);
	/* Contrast: every text pair at AA, and the plan passes its checks. */
	for (p = 0; p < G_N_ELEMENTS(pairs); p++)
		g_assert_cmpfloat(ratio(json_object_get_string_member(tokens, pairs[p][0]),
			json_object_get_string_member(tokens, pairs[p][1])), >=, 4.5);
	g_assert_true(ls_components_check_plan(components, plan, &error));
	g_assert_no_error(error);
	/* Reduced motion: the library's guard is in the stylesheet. */
	g_assert_nonnull(strstr(css, "@media (prefers-reduced-motion: reduce)"));
	g_assert_nonnull(strstr(css, "animation-duration: 0.01ms !important"));
	/* Weight. */
	g_assert_cmpuint(weight, <=, WEIGHT_LIMIT);
	g_assert_cmpuint(zipped, <=, GZIP_LIMIT);
	g_test_message("home page %" G_GSIZE_FORMAT " bytes (limit %d), %" G_GSIZE_FORMAT " gzipped (limit %d)",
		weight, WEIGHT_LIMIT, zipped, GZIP_LIMIT);
	/* With GATE_OUTPUT, the site is written out for the browser half. */
	if (g_getenv("GATE_OUTPUT") != NULL) {
		JsonArray *files = json_object_get_array_member(json_node_get_object(bundle), "files");
		g_autofree gchar *svg = NULL;
		const gchar *paths[2] = {NULL, "assets/mark.svg"};
		guint i;
		g_assert_true(g_file_get_contents("tests/fixtures/composed/mark.svg", &svg, NULL, NULL));
		for (i = 0; i <= json_array_get_length(files); i++) {
			JsonObject *file = i < json_array_get_length(files) ? json_array_get_object_element(files, i) : NULL;
			g_autofree gchar *out = NULL, *parent = NULL;
			paths[0] = file != NULL ? json_object_get_string_member(file, "path") : paths[1];
			out = g_build_filename(g_getenv("GATE_OUTPUT"), "default", paths[0], NULL);
			parent = g_path_get_dirname(out);
			g_assert_cmpint(g_mkdir_with_parents(parent, 0700), ==, 0);
			g_assert_true(g_file_set_contents(out, file != NULL ? json_object_get_string_member(file, "content") : svg,
				-1, NULL));
		}
	}
}

int
main(int argc, char **argv){
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/gate/site", test_site);
	return g_test_run();
}
