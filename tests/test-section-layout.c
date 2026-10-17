/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "core/ls-components.h"
#include "core/ls-util.h"
#include <glib/gstdio.h>

static JsonNode *
load(const gchar *name)
{
	g_autofree gchar *path = g_strconcat("tests/fixtures/composed/", name, NULL);
	g_autofree gchar *text = NULL;
	g_assert_true(g_file_get_contents(path, &text, NULL, NULL));
	return ls_json_parse(text, NULL);
}

static void
render_matrix(void)
{
	const gchar *headers[] = {"top", "side"};
	const gchar *frames[] = {"rail", "top"};
	const gchar *routes[] = {"index", "work", "start", "journal", "visit"};
	const gchar *output = g_getenv("SECTION_LAYOUT_OUTPUT");
	g_autoptr(GError) error = NULL;
	g_autoptr(LsComponents) components = ls_components_new(&error);
	g_autoptr(JsonObject) pages = json_object_new();
	guint h, f, i;
	g_assert_no_error(error);
	for (i = 0; i < G_N_ELEMENTS(routes); i++) {
		g_autofree gchar *fixture = g_strconcat(routes[i], ".json", NULL);
		g_autofree gchar *route = g_strconcat(routes[i], ".html", NULL);
		json_object_set_member(pages, route, load(fixture));
	}
	{
		JsonArray *sections = json_object_get_array_member(json_object_get_object_member(pages, "index.html"), "sections");
		JsonNode *seed = json_node_copy(json_array_get_element(sections, 1));
		const gchar *variants[] = {"columns", "split", "list"};
		const gchar *headings[] = {"Three things from one warm room", "From the oven, in three shapes", "How we can help you"};
		json_array_remove_element(sections, 1);
		for (i = 0; i < G_N_ELEMENTS(variants); i++) {
			g_autofree gchar *encoded = json_to_string(seed, FALSE);
			JsonNode *node = ls_json_parse(encoded, NULL);
			JsonObject *section = json_node_get_object(node), *content = json_object_get_object_member(section, "content");
			g_autofree gchar *id = g_strdup_printf("readable-%u", i);
			json_object_set_string_member(section, "id", i ? id : "how");
			json_object_set_string_member(section, "variant", variants[i]);
			json_object_set_string_member(content, "heading", headings[i]);
			json_object_set_string_member(content, "intro", "Everything starts with careful work. Tell us what you need and we will help you choose.");
			json_array_add_element(sections, node);
		}
		json_node_unref(seed);
	}
	for (f = 0; f < G_N_ELEMENTS(frames); f++) {
		for (h = 0; h < G_N_ELEMENTS(headers); h++) {
			g_autoptr(JsonNode) plan = load("plan.json");
			JsonObject *object = json_node_get_object(plan);
			JsonObject *brand = json_object_get_object_member(object, "brand");
			JsonObject *tokens = json_object_get_object_member(object, "tokens");
			JsonObject *look = json_object_get_object_member(object, "look");
			g_autofree gchar *text = NULL;
			g_autoptr(JsonNode) bundle = NULL;
			JsonArray *files;
			json_object_set_double_member(tokens, "base_size", 1.1);
			json_object_set_double_member(tokens, "ratio", 1.35);
			json_object_set_string_member(tokens, "ink", "#112f46");
			json_object_set_string_member(tokens, "accent", "#ad3d00");
			json_object_set_string_member(object, "name", "Example's Plumbing & Heating");
			json_object_set_string_member(brand, "wordmark", "Example's Plumbing & Heating");
			json_object_remove_member(brand, "mark");
			json_object_set_string_member(look, "header", "stacked");
			json_object_set_string_member(look, "heads", "split");
			json_object_set_string_member(look, "headpos", headers[h]);
			json_object_set_string_member(look, "frame", frames[f]);
			text = ls_components_compose(components, plan, pages, "https://example.com/", &error);
			g_assert_no_error(error);
			g_assert_nonnull(text);
			bundle = ls_json_parse(text, &error);
			g_assert_no_error(error);
			files = json_object_get_array_member(json_node_get_object(bundle), "files");
			g_assert_cmpuint(json_array_get_length(files), >, 10);
			if (output == NULL) continue;
			{
				g_autofree gchar *variant = g_strdup_printf("%s-%s", frames[f], headers[h]);
				g_autofree gchar *directory = g_build_filename(output, variant, "assets", NULL);
				g_autofree gchar *path = g_build_filename(directory, "bench.svg", NULL);
				g_autofree gchar *art = NULL;
				g_assert_cmpint(g_mkdir_with_parents(directory, 0700), ==, 0);
				g_assert_true(g_file_get_contents("tests/fixtures/composed/bench.svg", &art, NULL, &error));
				g_assert_no_error(error);
				g_assert_true(g_file_set_contents(path, art, -1, &error));
				g_assert_no_error(error);
			}
			for (i = 0; i < json_array_get_length(files); i++) {
				JsonObject *file = json_array_get_object_element(files, i);
				g_autofree gchar *variant = g_strdup_printf("%s-%s", frames[f], headers[h]);
				g_autofree gchar *path = g_build_filename(output, variant, ls_string(file, "path"), NULL);
				g_autofree gchar *parent = g_path_get_dirname(path);
				g_assert_cmpint(g_mkdir_with_parents(parent, 0700), ==, 0);
				g_assert_true(g_file_set_contents(path, ls_string(file, "content"), -1, &error));
				g_assert_no_error(error);
			}
		}
	}
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/components/section-layout-matrix", render_matrix);
	return g_test_run();
}
