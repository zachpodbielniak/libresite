/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* Motion kits: the builder deals each site one set of CSS-first motion
 * (entrances, parallax, an animated backdrop, a marquee band, counters and
 * hover and focus with character), all of it off for reduced motion and
 * small enough for the page weight. See docs/components.org, "Motion". */
#include "libresite.h"
#include "core/ls-components.h"
#include "core/ls-util.h"
#include <string.h>

/* Every kit's styles, and its script, stay this small. */
#define KIT_CSS_LIMIT 4096
#define KIT_JS_LIMIT 2048

static JsonNode *
load(const gchar *path){
	g_autofree gchar *text = NULL;
	g_assert_true(g_file_get_contents(path, &text, NULL, NULL));
	return ls_json_parse(text, NULL);
}

static LsComponents *
library(void){
	g_autoptr(GError) error = NULL;
	LsComponents *components = ls_components_new(&error);
	g_assert_no_error(error);
	return components;
}

/* The catalog's motion kits. */
static JsonObject *
kits(void){
	static JsonNode *catalog = NULL;
	if (catalog == NULL) {
		/* As loaded, so entries from the wider block are there too. */
		g_autoptr(LsComponents) library = ls_components_new(NULL);
		g_assert_nonnull(library);
		catalog = json_node_ref(ls_components_get_catalog(library));
		g_assert_nonnull(catalog);
	}
	return json_object_get_object_member(json_object_get_object_member(json_node_get_object(catalog),
		"motion_kits"), "kits");
}

static gboolean
flag(JsonObject *kit, const gchar *name){
	return json_object_has_member(kit, name) && json_object_get_boolean_member(kit, name);
}

static JsonObject *
member(JsonNode *plan, const gchar *name){
	return json_object_get_object_member(json_node_get_object(plan), name);
}

/* Several kits, every one dealt across sites, each with its look, the same
 * seed always dealing the same kit, and themes left still. */
/* The catalog's taste block (docs/components.org, "Taste"): what the
 * builder may deal. Without it everything may be. */
static JsonObject *
taste_block(LsComponents *components){
	JsonObject *root = json_node_get_object(ls_components_get_catalog(components));
	return json_object_has_member(root, "taste") ? json_object_get_object_member(root, "taste") : NULL;
}

static void
test_deal(void){
	g_autoptr(LsComponents) components = library();
	g_autoptr(GHashTable) dealt = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	guint i;
	g_assert_cmpuint(json_object_get_size(kits()), >=, 6);
	for (i = 0; i < 300; i++) {
		g_autofree gchar *seed = g_strdup_printf("moving-%u\nAn illustrative business %u.", i, i);
		g_autoptr(JsonNode) plan = load("tests/fixtures/composed/plan.json");
		g_autoptr(JsonNode) again = load("tests/fixtures/composed/plan.json");
		g_autoptr(GError) error = NULL;
		const gchar *name;
		JsonObject *kit;
		g_assert_true(ls_components_apply_seed(components, plan, seed, "trades", FALSE));
		g_assert_true(ls_components_apply_seed(components, again, seed, "trades", FALSE));
		name = json_object_get_string_member(member(plan, "structure"), "kit");
		g_assert_cmpstr(name, ==, json_object_get_string_member(member(again, "structure"), "kit"));
		kit = json_object_get_object_member(kits(), name);
		g_assert_nonnull(kit);
		if (json_object_has_member(kit, "look")) {
			JsonObject *wanted = json_object_get_object_member(kit, "look");
			g_autoptr(GList) axes = json_object_get_members(wanted);
			GList *axis;
			for (axis = axes; axis != NULL; axis = axis->next) {
				JsonObject *calm = json_object_get_object_member(taste_block(components), "looks");
				/* Taste may move a kit's gimmick back inside the calm values. */
				if (calm != NULL && json_object_has_member(calm, axis->data)) {
					JsonArray *allowed = json_object_get_array_member(calm, axis->data);
					gboolean kept = FALSE;
					guint k;
					for (k = 0; k < json_array_get_length(allowed); k++)
						kept |= g_strcmp0(json_array_get_string_element(allowed, k),
							json_object_get_string_member(wanted, axis->data)) == 0;
					if (!kept)
						continue;
				}
				g_assert_cmpstr(json_object_get_string_member(member(plan, "look"), axis->data), ==,
					json_object_get_string_member(wanted, axis->data));
			}
		}
		g_assert_true(ls_components_check_plan(components, plan, &error));
		g_assert_no_error(error);
		g_hash_table_add(dealt, g_strdup(name));
	}
	/* Every kit taste allows is dealt, and no other. */
	g_assert_cmpuint(g_hash_table_size(dealt), ==, taste_block(components) != NULL
		? json_array_get_length(json_object_get_array_member(taste_block(components), "kits"))
		: json_object_get_size(kits()));
	{
		g_autoptr(JsonNode) plan = load("tests/fixtures/composed/plan.json");
		g_autoptr(GError) error = NULL;
		g_assert_true(ls_components_apply_theme(components, plan, "swiss", &error));
		g_assert_no_error(error);
		g_assert_true(ls_components_apply_seed(components, plan, "themed\nsite", NULL, TRUE));
		g_assert_false(json_object_has_member(member(plan, "structure"), "kit"));
	}
	{
		g_autoptr(JsonNode) plan = load("tests/fixtures/composed/plan.json");
		g_autoptr(GError) error = NULL;
		g_assert_true(ls_components_apply_seed(components, plan, "bad\nkit", NULL, FALSE));
		json_object_set_string_member(member(plan, "structure"), "kit", "no-such-kit");
		g_assert_false(ls_components_check_plan(components, plan, &error));
		g_assert_nonnull(strstr(error->message, "structure.kit"));
	}
}

/* Removes every "@media (prefers-reduced-motion: no-preference) { ... }"
 * block and every @keyframes block from @css. */
static gchar *
outside_motion_blocks(const gchar *css){
	GString *out = g_string_new(NULL);
	const gchar *p = css;
	while (*p != '\0') {
		const gchar *media = strstr(p, "@media (prefers-reduced-motion: no-preference)");
		const gchar *frames = strstr(p, "@keyframes");
		const gchar *start = media == NULL ? frames : frames == NULL ? media : MIN(media, frames);
		const gchar *q;
		gint depth = 0;
		if (start == NULL) {
			g_string_append(out, p);
			break;
		}
		g_string_append_len(out, p, start - p);
		q = strchr(start, '{');
		g_assert_nonnull(q);
		for (; *q != '\0'; q++) {
			if (*q == '{')
				depth++;
			else if (*q == '}' && --depth == 0)
				break;
		}
		g_assert_cmpint(depth, ==, 0);
		p = q + 1;
	}
	return g_string_free(out, FALSE);
}

/* Reduced motion means none: every animation, transition and transform a
 * kit adds sits inside a no-preference media block, its script asks the
 * same question first, and each file stays within its weight. A kit that
 * names no script has none. */
static void
test_files(void){
	g_autoptr(GList) names = json_object_get_members(kits());
	GList *name;
	for (name = names; name != NULL; name = name->next) {
		JsonObject *kit = json_object_get_object_member(kits(), name->data);
		g_autofree gchar *css_path = g_strdup_printf("data/components/kit-%s.css", (const gchar *)name->data);
		const gchar *script = json_object_has_member(kit, "script") ? json_object_get_string_member(kit, "script") : NULL;
		g_autofree gchar *js_path = g_strdup_printf("data/components/motion-%s.js", script != NULL ? script : "none");
		g_autofree gchar *css = NULL, *js = NULL, *rest = NULL;
		gsize size = 0;
		g_assert_true(g_file_get_contents(css_path, &css, &size, NULL));
		g_assert_cmpuint(size, <=, KIT_CSS_LIMIT);
		{
			/* Comments may describe motion; only rules count. */
			g_autoptr(GRegex) comments = g_regex_new("/\\*.*?\\*/", G_REGEX_DOTALL, 0, NULL);
			g_autofree gchar *rules = g_regex_replace_literal(comments, css, -1, 0, "", 0, NULL);
			rest = outside_motion_blocks(rules);
		}
		if (strstr(rest, "animation") != NULL || strstr(rest, "transition") != NULL ||
			strstr(rest, "transform") != NULL || strstr(rest, "scroll-behavior") != NULL)
			g_error("%s moves outside a no-preference block", css_path);
		g_assert_nonnull(strstr(css, "@media (prefers-reduced-motion: no-preference)"));
		/* Hover and focus with character: every kit has both. */
		g_assert_nonnull(strstr(css, ":hover"));
		g_assert_nonnull(strstr(css, ":focus-visible"));
		if (script != NULL) {
			g_assert_true(g_file_get_contents(js_path, &js, &size, NULL));
			g_assert_cmpuint(size, <=, KIT_JS_LIMIT);
			g_assert_nonnull(strstr(js, "prefers-reduced-motion: reduce"));
		} else
			g_assert_false(g_file_test(js_path, G_FILE_TEST_EXISTS));
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

static gchar *
compose_with(LsComponents *components, const gchar *kit, JsonNode **plan_out){
	const gchar *routes[] = {"index", "work", "start", "journal", "visit"};
	g_autoptr(JsonNode) plan = load("tests/fixtures/composed/plan.json");
	g_autoptr(JsonObject) pages = json_object_new();
	g_autoptr(GError) error = NULL;
	gchar *text;
	guint i;
	g_assert_true(ls_components_apply_seed(components, plan, "kit\nbrief", NULL, FALSE));
	if (kit != NULL)
		json_object_set_string_member(member(plan, "structure"), "kit", kit);
	else
		json_object_remove_member(member(plan, "structure"), "kit");
	for (i = 0; i < G_N_ELEMENTS(routes); i++) {
		g_autofree gchar *fixture = g_strdup_printf("tests/fixtures/composed/%s.json", routes[i]);
		g_autofree gchar *route = g_strdup_printf("%s.html", routes[i]);
		json_object_set_member(pages, route, load(fixture));
	}
	/* A numbers section, so counters have something to count. */
	{
		JsonArray *sections = json_object_get_array_member(json_object_get_object_member(pages, "index.html"), "sections");
		g_autoptr(JsonNode) numbers = ls_json_parse("{\"component\": \"numbers\", \"content\": {\"heading\": \"In numbers\","
			" \"items\": [{\"label\": \"Chairs mended\", \"value\": \"1,240\"}, {\"label\": \"Years at the bench\", \"value\": \"18\"}]}}", NULL);
		json_array_add_element(sections, json_node_copy(numbers));
	}
	text = ls_components_compose(components, plan, pages, NULL, &error);
	g_assert_no_error(error);
	if (plan_out != NULL)
		*plan_out = g_steal_pointer(&plan);
	return text;
}

/* Rendered, a kit is site-wide: every page carries its class, the styles
 * and script come only with the dealt kit, a marquee band is decorative
 * and hidden from assistive technology, and a kit adds little weight. */
static void
test_render(void){
	g_autoptr(LsComponents) components = library();
	g_autoptr(GList) names = json_object_get_members(kits());
	g_autofree gchar *still_text = compose_with(components, NULL, NULL);
	g_autoptr(JsonNode) still = ls_json_parse(still_text, NULL);
	gsize base = strlen(bundle_file(still, "styles.css")) + strlen(bundle_file(still, "site.js"));
	const gchar *output = g_getenv("MOTION_OUTPUT");
	GList *name;
	g_assert_null(strstr(bundle_file(still, "index.html"), " kit-"));
	for (name = names; name != NULL; name = name->next) {
		JsonObject *kit = json_object_get_object_member(kits(), name->data);
		g_autofree gchar *text = compose_with(components, name->data, NULL);
		g_autoptr(JsonNode) bundle = ls_json_parse(text, NULL);
		g_autofree gchar *wanted = g_strdup_printf(" kit-%s", (const gchar *)name->data);
		g_autofree gchar *marker = g_strdup_printf(".kit-%s", (const gchar *)name->data);
		const gchar *index = bundle_file(bundle, "index.html"), *work = bundle_file(bundle, "work.html");
		const gchar *css = bundle_file(bundle, "styles.css"), *js = bundle_file(bundle, "site.js");
		g_assert_nonnull(strstr(index, wanted));
		g_assert_nonnull(strstr(work, wanted));
		g_assert_nonnull(strstr(css, marker));
		g_assert_cmpuint(strlen(css) + strlen(js) - base, <=, KIT_CSS_LIMIT + KIT_JS_LIMIT);
		if (json_object_has_member(kit, "script"))
			g_assert_nonnull(strstr(js, "prefers-reduced-motion: reduce"));
		else
			g_assert_cmpstr(js, ==, bundle_file(still, "site.js"));
		if (flag(kit, "marquee")) {
			g_assert_nonnull(strstr(index, "<div class=\"kit-marquee\" aria-hidden=\"true\">"));
			g_assert_nonnull(strstr(index, "Harbor &amp; Pine"));
		} else
			g_assert_null(strstr(index, "kit-marquee"));
		if (output != NULL) {
			JsonArray *files = json_object_get_array_member(json_node_get_object(bundle), "files");
			const gchar *art[] = {"bench.svg", "mark.svg"};
			guint i;
			for (i = 0; i < json_array_get_length(files); i++) {
				JsonObject *file = json_array_get_object_element(files, i);
				g_autofree gchar *out = g_build_filename(output, name->data, json_object_get_string_member(file, "path"), NULL);
				g_autofree gchar *parent = g_path_get_dirname(out);
				g_assert_cmpint(g_mkdir_with_parents(parent, 0700), ==, 0);
				g_assert_true(g_file_set_contents(out, json_object_get_string_member(file, "content"), -1, NULL));
			}
			for (i = 0; i < G_N_ELEMENTS(art); i++) {
				g_autofree gchar *source = g_build_filename("tests/fixtures/composed", art[i], NULL);
				g_autofree gchar *out = g_build_filename(output, name->data, "assets", art[i], NULL);
				g_autofree gchar *svg = NULL;
				g_autofree gchar *parent = g_path_get_dirname(out);
				g_assert_true(g_file_get_contents(source, &svg, NULL, NULL));
				g_assert_cmpint(g_mkdir_with_parents(parent, 0700), ==, 0);
				g_assert_true(g_file_set_contents(out, svg, -1, NULL));
			}
		}
	}
}

int
main(int argc, char **argv){
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/motion/deal", test_deal);
	g_test_add_func("/motion/files", test_files);
	g_test_add_func("/motion/render", test_render);
	return g_test_run();
}
