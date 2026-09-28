/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "libresite.h"
#include <glib/gstdio.h>

/* A native prune never calls its transport; this provider fails the test if
 * it is ever asked to generate. */
typedef struct { GObject parent_instance; } UnusedProvider;
typedef struct { GObjectClass parent_class; } UnusedProviderClass;
static void unused_provider_iface(LsProviderInterface *iface);
GType unused_provider_get_type(void);
G_DEFINE_TYPE_WITH_CODE(UnusedProvider, unused_provider, G_TYPE_OBJECT,
	G_IMPLEMENT_INTERFACE(LS_TYPE_PROVIDER, unused_provider_iface))
static gchar *
unused_generate(LsProvider *provider, JsonObject *settings, const gchar *system,
	const gchar *prompt, GCancellable *cancellable, GError **error){
	(void)provider; (void)settings; (void)system; (void)prompt; (void)cancellable; (void)error;
	g_assert_not_reached();
	return NULL;
}
static void
unused_provider_iface(LsProviderInterface *iface){
	iface->generate = unused_generate;
}
static void
unused_provider_class_init(UnusedProviderClass *klass){
	(void)klass;
}
static void
unused_provider_init(UnusedProvider *self){
	(void)self;
}

/* Build notes steer later steps but are not part of the delivered site. Prune
 * removes listed files that exist, ignores absent ones and is repeatable. */
static void
prune_contract(void){
	const gchar *invalid[] = {"{}", "{\"paths\":[]}", "{\"paths\":[\"../x\"]}",
		"{\"paths\":[\"a\",\"a\"]}", "{\"paths\":[1]}", "{\"paths\":\"a\"}",
		"{\"paths\":[\"a\"],\"extra\":true}"};
	g_autoptr(GError) error = NULL;
	g_autoptr(LsStep) step = g_object_new(LS_TYPE_PRUNE_STEP, NULL);
	g_autoptr(LsContext) context = ls_context_new();
	g_autoptr(JsonNode) config = ls_json_parse("{\"id\":\"prune\",\"type\":\"prune\","
		"\"options\":{\"paths\":[\"docs/product.org\",\"docs/design.org\",\"docs/absent.org\"]}}", NULL);
	g_autofree gchar *before = NULL;
	g_autofree gchar *after = NULL;
	guint i;
	g_assert_false(ls_prune_step_check_options(NULL, &error));
	g_assert_error(error, LS_ERROR, LS_ERROR_INVALID);
	g_clear_error(&error);
	for (i = 0; i < G_N_ELEMENTS(invalid); i++) {
		g_autoptr(JsonNode) options = ls_json_parse(invalid[i], NULL);
		g_assert_nonnull(options);
		g_assert_false(ls_prune_step_check_options(json_node_get_object(options), &error));
		g_assert_error(error, LS_ERROR, LS_ERROR_INVALID);
		g_clear_error(&error);
	}
	g_assert_true(ls_context_apply(context, "{\"files\":["
		"{\"path\":\"index.html\",\"content\":\"<main>Home</main>\"},"
		"{\"path\":\"docs/product.org\",\"content\":\"notes\"},"
		"{\"path\":\"docs/design.org\",\"content\":\"notes\"}]}", &error));
	g_assert_no_error(error);
	g_assert_true(ls_step_execute(step, NULL, json_node_get_object(config), NULL, context, NULL, NULL, &error));
	g_assert_no_error(error);
	g_assert_null(ls_context_lookup(context, "docs/product.org"));
	g_assert_null(ls_context_lookup(context, "docs/design.org"));
	g_assert_nonnull(ls_context_lookup(context, "index.html"));
	before = ls_context_snapshot(context);
	g_assert_true(ls_step_execute(step, NULL, json_node_get_object(config), NULL, context, NULL, NULL, &error));
	g_assert_no_error(error);
	after = ls_context_snapshot(context);
	g_assert_cmpstr(before, ==, after);
}

/* The published site never contains pruned build notes, while the pipeline
 * still runs and publishes the rest. */
static void
prune_publish(void){
	g_autoptr(LsRecipe) recipe = ls_recipe_new(
		"version: 1\nname: prune\nsite: {brief: contract}\n"
		"seed_artifacts:\n"
		"  - {path: index.html, content: '<main>Home</main>'}\n"
		"  - {path: docs/design.org, content: 'build notes'}\n"
		"pipeline:\n  steps:\n"
		"    - {id: notes, type: prune, options: {paths: [docs/design.org]}}\n", NULL);
	g_autoptr(LsPrompts) prompts = ls_prompts_new(NULL, NULL);
	g_autoptr(GObject) provider = g_object_new(unused_provider_get_type(), NULL);
	g_autoptr(LsPipeline) pipeline = ls_pipeline_new(LS_PROVIDER(provider), prompts);
	g_autoptr(GError) error = NULL;
	g_autofree gchar *temp = g_dir_make_tmp("libresite-prune-XXXXXX", NULL);
	g_autofree gchar *output = g_build_filename(temp, "site", NULL);
	g_autofree gchar *index = g_build_filename(output, "index.html", NULL);
	g_autofree gchar *notes = g_build_filename(output, "docs", "design.org", NULL);
	g_assert_nonnull(recipe);
	g_assert_true(ls_pipeline_run(pipeline, recipe, output, FALSE, NULL, &error));
	g_assert_no_error(error);
	g_assert_true(g_file_test(index, G_FILE_TEST_IS_REGULAR));
	g_assert_false(g_file_test(notes, G_FILE_TEST_EXISTS));
}

/* Component specifications live under site/; one prefix removes all of them. */
static void
prune_prefixes(void){
	const gchar *invalid[] = {"{\"prefixes\":[]}", "{\"prefixes\":[\"site\"]}", "{\"prefixes\":[\"../\"]}",
		"{\"prefixes\":[\"/\"]}", "{\"prefixes\":[\"site/\",\"site/\"]}"};
	g_autoptr(GError) error = NULL;
	g_autoptr(LsStep) step = g_object_new(LS_TYPE_PRUNE_STEP, NULL);
	g_autoptr(LsContext) context = ls_context_new();
	g_autoptr(JsonNode) config = ls_json_parse("{\"id\":\"prune\",\"type\":\"prune\","
		"\"options\":{\"prefixes\":[\"site/\"],\"paths\":[\"docs/design.org\"]}}", NULL);
	g_autoptr(JsonNode) only = ls_json_parse("{\"prefixes\":[\"site/\"]}", NULL);
	guint i;
	for (i = 0; i < G_N_ELEMENTS(invalid); i++) {
		g_autoptr(JsonNode) options = ls_json_parse(invalid[i], NULL);
		g_assert_false(ls_prune_step_check_options(json_node_get_object(options), &error));
		g_assert_error(error, LS_ERROR, LS_ERROR_INVALID);
		g_clear_error(&error);
	}
	g_assert_true(ls_prune_step_check_options(json_node_get_object(only), &error));
	g_assert_no_error(error);
	g_assert_true(ls_context_apply(context, "{\"files\":["
		"{\"path\":\"index.html\",\"content\":\"<main>Home</main>\"},"
		"{\"path\":\"site.js\",\"content\":\"x\"},"
		"{\"path\":\"site/plan.json\",\"content\":\"{}\"},"
		"{\"path\":\"site/pages/index.json\",\"content\":\"{}\"},"
		"{\"path\":\"docs/design.org\",\"content\":\"notes\"}]}", &error));
	g_assert_no_error(error);
	g_assert_true(ls_step_execute(step, NULL, json_node_get_object(config), NULL, context, NULL, NULL, &error));
	g_assert_no_error(error);
	g_assert_null(ls_context_lookup(context, "site/plan.json"));
	g_assert_null(ls_context_lookup(context, "site/pages/index.json"));
	g_assert_null(ls_context_lookup(context, "docs/design.org"));
	g_assert_nonnull(ls_context_lookup(context, "site.js"));
	g_assert_nonnull(ls_context_lookup(context, "index.html"));
}

int
main(int argc, char **argv){
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/prune/contract", prune_contract);
	g_test_add_func("/prune/publish", prune_publish);
	g_test_add_func("/prune/prefixes", prune_prefixes);
	return g_test_run();
}
