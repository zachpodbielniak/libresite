/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* Outline lettering stays legible: stroke-only text is kept for headings
 * and manifesto lines of at most three words, and only where the column is
 * wide; everything longer, and everything on a phone, is filled. The
 * browser half is in tests/gate.sh. See docs/components.org, "Outline
 * lettering". */
#include "libresite.h"
#include "core/ls-components.h"
#include <string.h>

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

/* The fixture site with outline lettering and @opening as its home. */
static JsonNode *
compose(LsComponents *components, const gchar *opening_json){
	const gchar *routes[] = {"work", "start", "journal", "visit"};
	g_autoptr(JsonNode) plan = load("tests/fixtures/composed/plan.json");
	g_autoptr(JsonObject) pages = json_object_new();
	g_autoptr(GError) error = NULL;
	g_autofree gchar *home = g_strdup_printf("{\"path\":\"index.html\",\"sections\":[%s,"
		"{\"component\":\"cta\",\"variant\":\"band\",\"content\":{\"heading\":\"Ready to start?\","
		"\"actions\":[{\"href\":\"start.html\",\"label\":\"Start\"}]}}]}", opening_json);
	g_autofree gchar *text = NULL;
	guint i;
	json_object_set_string_member(json_object_get_object_member(json_node_get_object(plan), "look"), "lettering", "outline");
	if (json_object_has_member(json_node_get_object(plan), "structure"))
		json_object_remove_member(json_node_get_object(plan), "structure");
	json_object_set_member(pages, "index.html", ls_json_parse(home, &error));
	g_assert_no_error(error);
	for (i = 0; i < G_N_ELEMENTS(routes); i++) {
		g_autofree gchar *fixture = g_strdup_printf("tests/fixtures/composed/%s.json", routes[i]);
		g_autofree gchar *route = g_strdup_printf("%s.html", routes[i]);
		json_object_set_member(pages, route, load(fixture));
	}
	text = ls_components_compose(components, plan, pages, NULL, &error);
	g_assert_no_error(error);
	return ls_json_parse(text, NULL);
}

/* The page says whether its main heading is short enough to outline. */
static void
test_heading(void){
	g_autoptr(GError) error = NULL;
	g_autoptr(LsComponents) components = ls_components_new(&error);
	g_autoptr(JsonNode) short_site = NULL, long_site = NULL;
	g_assert_no_error(error);
	short_site = compose(components, "{\"component\":\"hero\",\"variant\":\"statement\",\"content\":{\"heading\":\"Roofs that last\"}}");
	long_site = compose(components, "{\"component\":\"hero\",\"variant\":\"statement\",\"content\":"
		"{\"heading\":\"A roof that holds when the weather turns\"}}");
	g_assert_nonnull(strstr(bundle_file(short_site, "index.html"), " h1-short"));
	g_assert_null(strstr(bundle_file(long_site, "index.html"), " h1-short"));
	/* Outline lettering needs the short mark and a wide column. */
	{
		const gchar *css = bundle_file(long_site, "styles.css");
		g_assert_null(strstr(css, ".look-lettering-outline main h1 { color: transparent"));
		g_assert_nonnull(strstr(css, ".look-lettering-outline.h1-short main h1 { color: transparent"));
	}
}

/* Manifesto lines say whether they are short; only short ones outline. */
static void
test_manifesto(void){
	g_autoptr(GError) error = NULL;
	g_autoptr(LsComponents) components = ls_components_new(&error);
	g_autoptr(JsonNode) site = NULL;
	const gchar *html, *css;
	g_assert_no_error(error);
	site = compose(components, "{\"component\":\"manifesto\",\"variant\":\"outlined\",\"content\":{\"heading\":\"Clearday\","
		"\"lines\":[{\"text\":\"Weekly visits that keep a home steady.\"},{\"text\":\"Deep cleans.\"},"
		"{\"text\":\"Move-out cleans for an empty, ready space.\"}]}}");
	html = bundle_file(site, "index.html");
	css = bundle_file(site, "styles.css");
	g_assert_nonnull(strstr(html, "<li class=\"manifesto__line manifesto__line--short\"><span class=\"manifesto__n\" aria-hidden=\"true\">02</span><span class=\"manifesto__text\">Deep cleans.</span>"));
	g_assert_nonnull(strstr(html, "<li class=\"manifesto__line\"><span class=\"manifesto__n\" aria-hidden=\"true\">03</span>"));
	g_assert_null(strstr(css, ".manifesto--outlined .manifesto__line { color: transparent"));
	g_assert_nonnull(strstr(css, ".manifesto--outlined .manifesto__line--short:not(:first-child) { color: transparent"));
}

/* Every stroke-only rule in the library sits inside a wide-column
 * container query, so no phone gets stroke-only text. */
static void
test_rules(void){
	const gchar *files[] = {"data/components/look.css", "data/components/manifesto.css"};
	guint f;
	for (f = 0; f < G_N_ELEMENTS(files); f++) {
		g_autofree gchar *css = NULL;
		const gchar *at;
		g_assert_true(g_file_get_contents(files[f], &css, NULL, NULL));
		for (at = strstr(css, "color: transparent; -webkit-text-stroke"); at != NULL;
			at = strstr(at + 1, "color: transparent; -webkit-text-stroke")) {
			const gchar *query = g_strrstr_len(css, at - css, "@container (min-width: 40rem)");
			const gchar *close = query != NULL ? strstr(query, "\n}") : NULL;
			if (query == NULL || close == NULL || close < at)
				g_error("%s: stroke-only text outside a wide-column query near: %.80s", files[f], at - 60);
		}
	}
}

int
main(int argc, char **argv){
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/outline/heading", test_heading);
	g_test_add_func("/outline/manifesto", test_manifesto);
	g_test_add_func("/outline/rules", test_rules);
	return g_test_run();
}
