/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "libresite.h"
#include <glib/gstdio.h>
#include <string.h>

#define FIXTURE "tests/fixtures/composed/"

/* Answers by response schema from the composed-site fixture; stateless, so
 * concurrent calls from a parallel step are safe. */
typedef struct {
	GObject parent_instance;
	gint calls;
} SiteProvider;
typedef struct { GObjectClass parent_class; } SiteProviderClass;
static void site_iface_init(LsProviderInterface *iface);
GType site_provider_get_type(void);
G_DEFINE_TYPE_WITH_CODE(SiteProvider, site_provider, G_TYPE_OBJECT,
	G_IMPLEMENT_INTERFACE(LS_TYPE_PROVIDER, site_iface_init))

static gchar *
read_fixture(const gchar *name){
	g_autofree gchar *path = g_strconcat(FIXTURE, name, NULL);
	gchar *text = NULL;
	g_assert_true(g_file_get_contents(path, &text, NULL, NULL));
	return text;
}

static gchar *
site_generate(LsProvider *provider, JsonObject *settings, const gchar *system, const gchar *prompt,
	GCancellable *cancellable, GError **error){
	const gchar *schema = ls_string(settings, "json_schema");
	const gchar *task = strstr(prompt, "TASK (JSON)");
	(void)system;
	(void)cancellable;
	(void)error;
	g_atomic_int_inc(&((SiteProvider *)provider)->calls);
	g_assert_nonnull(schema);
	if (strstr(schema, "\"tokens\"") != NULL)
		return read_fixture("plan.json");
	if (strstr(schema, "\"svg\"") != NULL) {
		g_autofree gchar *svg = read_fixture("mark.svg");
		g_autoptr(JsonNode) node = json_node_new(JSON_NODE_OBJECT);
		g_autoptr(JsonObject) object = json_object_new();
		json_object_set_string_member(object, "svg", svg);
		json_node_set_object(node, object);
		return ls_json_encode(node);
	}
	if (strstr(task, "\"route\":\"index.html\"") != NULL)
		return read_fixture("index.json");
	if (strstr(task, "\"route\":\"work.html\"") != NULL)
		return read_fixture("work.json");
	return read_fixture("start.json");
}
static void
site_iface_init(LsProviderInterface *iface){
	iface->generate = site_generate;
}
static void
site_provider_class_init(SiteProviderClass *klass){
	(void)klass;
}
static void
site_provider_init(SiteProvider *self){
	(void)self;
}

static const gchar *composed_recipe =
	"version: 1\n"
	"name: harbor\n"
	"site: {brief: An illustrative furniture repair workshop, style: editorial}\n"
	"ai: {provider: claude-code, retries: 1}\n"
	"pipeline:\n"
	"  steps:\n"
	"    - {id: plan, type: plan}\n"
	"    - {id: pages, type: pages, options: {parallel: 4, base_url: 'https://harbor.example/'}}\n"
	"    - id: acceptance\n"
	"      type: validate\n"
	"      options:\n"
	"        required: [index.html, work.html, start.html, styles.css, site.js, robots.txt, sitemap.xml]\n"
	"        checks:\n"
	"          - {path: start.html, contains: ['Nothing is sent.']}\n"
	"    - {id: specs, type: prune, options: {prefixes: [site/]}}\n";

static gchar *
publish_path(const gchar *output, const gchar *path){
	return g_build_filename(output, path, NULL);
}

/* Plan, parallel pages, acceptance and prune publish a complete static site. */
static void
test_composed_run(void){
	g_autoptr(GError) error = NULL;
	g_autoptr(LsRecipe) recipe = ls_recipe_new(composed_recipe, &error);
	g_autoptr(LsPrompts) prompts = ls_prompts_new(NULL, &error);
	g_autoptr(GObject) provider = g_object_new(site_provider_get_type(), NULL);
	g_autoptr(LsPipeline) pipeline = ls_pipeline_new(LS_PROVIDER(provider), prompts);
	g_autofree gchar *temp = g_dir_make_tmp("libresite-pipeline-XXXXXX", NULL);
	g_autofree gchar *output = g_build_filename(temp, "site", NULL);
	const gchar *present[] = {"index.html", "work.html", "start.html", "404.html", "privacy.html",
		"terms.html", "styles.css", "site.js", "robots.txt", "sitemap.xml", "assets/bench.svg", "assets/mark.svg"};
	g_autofree gchar *specs = publish_path(output, "site");
	g_autofree gchar *index_path = publish_path(output, "index.html");
	g_autofree gchar *index = NULL;
	guint i;
	g_assert_no_error(error);
	{
		gboolean okay = ls_pipeline_run(pipeline, recipe, output, FALSE, NULL, &error);
		g_assert_no_error(error);
		g_assert_true(okay);
	}
	for (i = 0; i < G_N_ELEMENTS(present); i++) {
		g_autofree gchar *path = publish_path(output, present[i]);
		if (!g_file_test(path, G_FILE_TEST_IS_REGULAR))
			g_error("missing %s", present[i]);
	}
	g_assert_false(g_file_test(specs, G_FILE_TEST_EXISTS));
	g_assert_cmpint(((SiteProvider *)provider)->calls, ==, 1 + 3 + 2);
	g_assert_true(g_file_get_contents(index_path, &index, NULL, NULL));
	g_assert_nonnull(strstr(index, "<link rel=\"canonical\" href=\"https://harbor.example/\">"));
}

static void
expect_invalid(const gchar *yaml, const gchar *needle){
	g_autoptr(GError) error = NULL;
	g_autoptr(LsRecipe) recipe = ls_recipe_new(yaml, &error);
	g_autoptr(LsPrompts) prompts = ls_prompts_new(NULL, NULL);
	g_autoptr(GObject) provider = g_object_new(site_provider_get_type(), NULL);
	g_autoptr(LsPipeline) pipeline = ls_pipeline_new(LS_PROVIDER(provider), prompts);
	g_assert_no_error(error);
	g_assert_false(ls_pipeline_validate(pipeline, recipe, &error));
	g_assert_error(error, LS_ERROR, LS_ERROR_INVALID);
	if (strstr(error->message, needle) == NULL)
		g_error("preflight error lacks '%s': %s", needle, error->message);
	g_assert_cmpint(((SiteProvider *)provider)->calls, ==, 0);
}

/* Component steps fail preflight, before any paid call, when misconfigured. */
static void
test_preflight(void){
	expect_invalid("version: 1\nname: x\nsite: {brief: b}\npipeline:\n  steps:\n"
		"    - {id: pages, type: pages}\n", "plan step");
	expect_invalid("version: 1\nname: x\nsite: {brief: b}\npipeline:\n  steps:\n"
		"    - {id: plan, type: plan}\n    - {id: pages, type: pages, options: {parallel: 99}}\n", "pages");
	expect_invalid("version: 1\nname: x\nsite: {brief: b}\npipeline:\n  steps:\n"
		"    - {id: plan, type: plan}\n    - {id: specs, type: prune, options: {prefixes: [site]}}\n", "prune");
}

/* A replacement catalog without spec_system cannot run component steps. */
static void
test_catalog_required(void){
	g_autoptr(GError) error = NULL;
	g_autoptr(LsRecipe) recipe = ls_recipe_new(composed_recipe, &error);
	g_autoptr(LsPrompts) prompts = ls_prompts_new_from_yaml(
		"version: 1\nsystem: s\nstyles: {editorial: e}\nstages: {plan: p, page: q, art: a}\n", &error);
	g_autoptr(GObject) provider = g_object_new(site_provider_get_type(), NULL);
	g_autoptr(LsPipeline) pipeline = ls_pipeline_new(LS_PROVIDER(provider), prompts);
	g_assert_no_error(error);
	g_assert_false(ls_pipeline_validate(pipeline, recipe, &error));
	g_assert_error(error, LS_ERROR, LS_ERROR_INVALID);
	g_assert_nonnull(strstr(error->message, "spec_system"));
}

int
main(int argc, char **argv){
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/pipeline/composed-run", test_composed_run);
	g_test_add_func("/pipeline/composed-preflight", test_preflight);
	g_test_add_func("/pipeline/composed-catalog", test_catalog_required);
	return g_test_run();
}
