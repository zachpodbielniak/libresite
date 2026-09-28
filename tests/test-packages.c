/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "libresite.h"
#include <glib/gstdio.h>
#include <string.h>

/* The second call fails once, leaving a real completed-step checkpoint. */
typedef struct {
	GObject parent_instance;
	guint calls;
	gboolean fail_second;
} PackageProvider;
typedef struct { GObjectClass parent_class; } PackageProviderClass;
static void provider_init(LsProviderInterface *iface);
GType package_provider_get_type(void);
G_DEFINE_TYPE_WITH_CODE(PackageProvider, package_provider, G_TYPE_OBJECT,
	G_IMPLEMENT_INTERFACE(LS_TYPE_PROVIDER, provider_init))
static gchar *
generate(LsProvider *provider, JsonObject *settings, const gchar *system,
	const gchar *prompt, GCancellable *cancellable, GError **error){
	PackageProvider *self = (PackageProvider *)provider;
	(void)settings;
	(void)cancellable;
	g_assert_cmpstr(system, ==, "Archived 2026 catalog\n");
	self->calls++;
	if (self->calls == 2 && self->fail_second) {
		g_set_error_literal(error, LS_ERROR, LS_ERROR_PROVIDER, "Interrupted fixture");
		return NULL;
	}
	if (strstr(prompt, "\"id\":\"first\"") != NULL)
		return g_strdup("{\"files\":[{\"path\":\"first.txt\",\"content\":\"Retained first step\"}]}");
	g_assert_nonnull(strstr(prompt, "Retained first step"));
	return g_strdup("{\"files\":[{\"path\":\"second.txt\",\"content\":\"Resumed second step\"}]}");
}
static void
provider_init(LsProviderInterface *iface){
	iface->generate = generate;
}
static void
package_provider_class_init(PackageProviderClass *klass){
	(void)klass;
}
static void
package_provider_init(PackageProvider *self){
	self->fail_second = TRUE;
}
static const gchar *catalog_text =
	"# Keep comments and trailing whitespace exactly\nversion: 1\n"
	"system: |\n  Archived 2026 catalog\nstyles: {editorial: 'Archived style'}\n"
	"stages: {generate: 'Archived stage'}\n";
static const gchar *recipe_text =
	"# Archived customer inputs\nversion: 1\nname: sold-job\n"
	"package: {sku: studio, release: '1.0.0', kind: site}\n"
	"site: {brief: 'Keep this scope'}\nai: {retries: 0, timeout: 1}\n"
	"pipeline:\n  steps:\n    - {id: scaffold, type: scaffold}\n"
	"    - {id: first, type: generate}\n"
	"    - {id: second, type: generate}\n    - id: acceptance\n"
	"      type: validate\n      options:\n        required: [first.txt, second.txt]\n"
	"        checks: [{path: second.txt, contains: ['Resumed second step']}]\n";

/* Only remove the test's private directory, never follow symlinks. */
static void
remove_tree(const gchar *path){
	GStatBuf info;
	g_assert_cmpint(g_lstat(path, &info), ==, 0);
	if (S_ISDIR(info.st_mode)) {
		g_autoptr(GDir) directory = g_dir_open(path, 0, NULL);
		const gchar *name;
		g_assert_nonnull(directory);
		while ((name = g_dir_read_name(directory)) != NULL) {
			g_autofree gchar *child = g_build_filename(path, name, NULL);
			remove_tree(child);
		}
		g_assert_cmpint(g_rmdir(path), ==, 0);
	} else
		g_assert_cmpint(g_unlink(path), ==, 0);
}

/* Failed load must never expose a half-verified recipe/catalog pair. */
static void
assert_rejected(const gchar *directory){
	g_autoptr(LsRecipe) recipe = NULL;
	g_autoptr(LsPrompts) prompts = NULL;
	g_autoptr(GError) error = NULL;
	g_assert_false(ls_package_load(directory, &recipe, &prompts, &error));
	g_assert_nonnull(error);
	g_assert_null(recipe);
	g_assert_null(prompts);
}

/* Round-trip preserves comments, input bytes and digests, not just YAML meaning.
 * Test each pin independently against its known-valid positive control. */
static void
roundtrip(void){
	g_autoptr(GError) error = NULL;
	g_autoptr(LsRecipe) recipe = ls_recipe_new(recipe_text, &error), loaded = NULL;
	g_autoptr(LsPrompts) prompts = ls_prompts_new_from_yaml(catalog_text, &error), archived = NULL;
	g_autofree gchar *directory = g_dir_make_tmp("libresite-package-XXXXXX", &error);
	g_autofree gchar *sale = g_build_filename(directory, "sale", NULL);
	g_autofree gchar *recipe_path = g_build_filename(sale, "recipe.yaml", NULL);
	g_autofree gchar *catalog_path = g_build_filename(sale, "prompts.yaml", NULL);
	g_autofree gchar *manifest_path = g_build_filename(sale, "package.yaml", NULL);
	g_autofree gchar *scaffold_path = g_build_filename(sale, "scaffold.json", NULL);
	g_autofree gchar *scaffold = NULL;
	g_autofree gchar *manifest = NULL;
	g_autofree gchar *changed = g_strconcat(catalog_text, "# edited\n", NULL);
	g_assert_no_error(error);
	ls_recipe_set_timeout(recipe, 99);
	ls_recipe_set_unlimited(recipe, TRUE);
	g_assert_true(ls_package_save(sale, recipe, prompts, &error));
	g_assert_true(ls_package_load(sale, &loaded, &archived, &error));
	g_assert_no_error(error);
	g_assert_cmpstr(ls_recipe_get_source(loaded), ==, recipe_text);
	g_assert_cmpstr(ls_prompts_get_source(archived), ==, catalog_text);
	g_assert_cmpstr(ls_prompts_get_digest(archived), ==, ls_prompts_get_digest(prompts));
	g_assert_true(g_file_get_contents(scaffold_path, &scaffold, NULL, &error));
	g_assert_cmpstr(ls_recipe_get_scaffold_source(loaded), ==, scaffold);
	{
		g_autoptr(JsonObject) ai = ls_recipe_resolve_ai(loaded,
			json_array_get_object_element(ls_recipe_get_steps(loaded), 0));
		g_assert_cmpint(json_object_get_int_member(ai, "timeout"), ==, 1);
	}
	/* Re-sealing must not touch any already published file or empty directory. */
	g_assert_false(ls_package_save(sale, recipe, prompts, &error));
	g_assert_nonnull(error);
	g_clear_error(&error);
	g_assert_true(g_file_get_contents(manifest_path, &manifest, NULL, &error));
	g_assert_true(g_file_set_contents(recipe_path, "version: 2\n", -1, &error));
	assert_rejected(sale);
	g_assert_true(g_file_set_contents(recipe_path, recipe_text, -1, &error));
	g_assert_true(g_file_set_contents(catalog_path, changed, -1, &error));
	assert_rejected(sale);
	g_assert_true(g_file_set_contents(catalog_path, catalog_text, -1, &error));
	g_assert_true(g_file_set_contents(manifest_path, "version: 2\n", -1, &error));
	assert_rejected(sale);
	{
		g_autofree gchar *wrong = g_strconcat(manifest, "extra: unsupported\n", NULL);
		g_assert_true(g_file_set_contents(manifest_path, wrong, -1, &error));
		assert_rejected(sale);
	}
	{
		g_autoptr(GRegex) pattern = g_regex_new("sku: 'studio'", 0, 0, &error);
		g_autofree gchar *wrong = g_regex_replace_literal(pattern, manifest, -1, 0,
			"sku: 'trades'", 0, &error);
		g_assert_true(g_file_set_contents(manifest_path, wrong, -1, &error));
		assert_rejected(sale);
	}
	g_assert_true(g_file_set_contents(manifest_path, manifest, -1, &error));
	g_assert_true(g_file_set_contents(scaffold_path, "{\"files\":[]}", -1, &error));
	assert_rejected(sale);
	g_assert_cmpint(g_unlink(scaffold_path), ==, 0);
	assert_rejected(sale);
	g_assert_true(g_file_set_contents(scaffold_path, scaffold, -1, &error));
	g_assert_cmpint(g_unlink(catalog_path), ==, 0);
	assert_rejected(sale);
	g_assert_no_error(error);
	remove_tree(directory);
}

/* This exercises a real resume against old saved bytes after the current
 * catalog changes, and proves the completed step was not repeated. */
static void
resume_package(void){
	g_autoptr(GError) error = NULL;
	g_autofree gchar *directory = g_dir_make_tmp("libresite-resume-package-XXXXXX", &error);
	g_autofree gchar *sale = g_build_filename(directory, "sale", NULL);
	g_autofree gchar *moved = g_build_filename(directory, "archive-2027", NULL);
	g_autofree gchar *output = g_build_filename(directory, "site", NULL);
	g_autofree gchar *changed = g_strconcat(catalog_text, "# 2027 replacement\n", NULL);
	g_autoptr(LsRecipe) recipe = ls_recipe_new(recipe_text, &error), loaded = NULL;
	g_autoptr(LsPrompts) prompts = ls_prompts_new_from_yaml(catalog_text, &error);
	g_autoptr(LsPrompts) current = ls_prompts_new_from_yaml(changed, &error), archived = NULL;
	g_autoptr(LsProvider) provider = LS_PROVIDER(g_object_new(package_provider_get_type(), NULL));
	g_autoptr(LsPipeline) pipeline = ls_pipeline_new(provider, prompts);
	PackageProvider *fixture = (PackageProvider *)provider;
	g_assert_no_error(error);
	/* Simulate an older release whose native template differs from today's
	 * embedded scaffold. Both the source and its path inventory are pinned. */
	g_assert_true(ls_recipe_set_scaffold_source(recipe,
		"{\"files\":[{\"path\":\"Makefile\",\"content\":\"Archived native template 2026\"}]}", &error));
	g_assert_true(ls_package_save(sale, recipe, prompts, &error));
	g_assert_false(ls_pipeline_run(pipeline, recipe, output, FALSE, NULL, &error));
	g_assert_error(error, LS_ERROR, LS_ERROR_PROVIDER);
	g_assert_cmpuint(fixture->calls, ==, 2);
	g_clear_error(&error);
	g_clear_object(&pipeline);
	pipeline = ls_pipeline_new(provider, current);
	g_assert_false(ls_pipeline_run(pipeline, recipe, output, TRUE, NULL, &error));
	g_assert_error(error, LS_ERROR, LS_ERROR_RECOVERY);
	g_assert_cmpuint(fixture->calls, ==, 2);
	g_clear_error(&error);
	g_clear_object(&pipeline);
	/* Matching recipe/catalog alone still fail with changed native templates. */
	{
		g_autoptr(LsRecipe) unpinned = ls_recipe_new(recipe_text, &error);
		pipeline = ls_pipeline_new(provider, prompts);
		g_assert_false(ls_pipeline_run(pipeline, unpinned, output, TRUE, NULL, &error));
		g_assert_error(error, LS_ERROR, LS_ERROR_RECOVERY);
		g_assert_cmpuint(fixture->calls, ==, 2);
		g_clear_error(&error);
		g_clear_object(&pipeline);
	}
	g_clear_object(&recipe);
	g_clear_object(&prompts);
	g_assert_cmpint(g_rename(sale, moved), ==, 0);
	g_assert_true(ls_package_load(moved, &loaded, &archived, &error));
	pipeline = ls_pipeline_new(provider, archived);
	fixture->fail_second = FALSE;
	ls_recipe_set_timeout(loaded, 120);
	g_assert_true(ls_pipeline_run(pipeline, loaded, output, TRUE, NULL, &error));
	g_assert_no_error(error);
	g_assert_cmpuint(fixture->calls, ==, 3);
	{
		g_autofree gchar *path = g_build_filename(output, "Makefile", NULL);
		g_autofree gchar *contents = ls_read_text(path, &error);
		g_assert_no_error(error);
		g_assert_cmpstr(contents, ==, "Archived native template 2026");
	}
	remove_tree(directory);
}

/* Archived templates retain the normal seed-preservation and path-validation
 * contract, including transactional rejection and self-assignment ownership. */
static void
native_snapshot(void){
	const gchar *source = "{\"files\":[{\"path\":\"Makefile\",\"content\":\"Archived\"}]}";
	const gchar *invalid[] = {"[]", "{}", "{\"files\":null}",
		"{\"files\":[{\"path\":\"../escape\",\"content\":\"bad\"}]}",
		"{\"files\":[],\"delete\":[\"Makefile\"]}"};
	g_autoptr(GError) error = NULL;
	g_autoptr(LsRecipe) recipe = ls_recipe_new(recipe_text, &error);
	g_autoptr(LsContext) context = ls_context_new();
	g_autofree gchar *rendered = NULL;
	guint i;
	g_assert_true(ls_recipe_set_scaffold_source(recipe, source, &error));
	g_assert_true(ls_recipe_set_scaffold_source(recipe, ls_recipe_get_scaffold_source(recipe), &error));
	for (i = 0; i < G_N_ELEMENTS(invalid); i++) {
		g_assert_false(ls_recipe_set_scaffold_source(recipe, invalid[i], &error));
		g_assert_nonnull(error);
		g_clear_error(&error);
		g_assert_cmpstr(ls_recipe_get_scaffold_source(recipe), ==, source);
	}
	rendered = ls_scaffold_step_render_recipe(recipe, NULL, context, &error);
	g_assert_cmpstr(rendered, ==, source);
	g_clear_pointer(&rendered, g_free);
	g_assert_true(ls_context_apply(context,
		"{\"files\":[{\"path\":\"Makefile\",\"content\":\"Approved seed\"}]}", &error));
	rendered = ls_scaffold_step_render_recipe(recipe, NULL, context, &error);
	g_assert_cmpstr(rendered, ==, "{\"files\":[]}");
	g_assert_true(ls_recipe_set_scaffold_source(recipe, NULL, &error));
	g_assert_null(ls_recipe_get_scaffold_source(recipe));
	g_assert_no_error(error);
}

/* Bad metadata fails parsing before any workspace or provider is available. */
static void
metadata_invalid(void){
	const gchar *values[] = {
		"null", "[]", "{sku: studio, release: '1.0.0', kind: application}",
		"{sku: Studio, release: '1.0.0', kind: site}",
		"{sku: studio, release: 1, kind: site}",
		"{sku: studio, release: '1.0.0', kind: other}",
		"{sku: studio, release: '1.0.0', kind: site, extra: bad}",
		"{sku: '../studio', release: '1.0.0', kind: site}",
		"{sku: application, release: '1.0.0', kind: site}",
		"{sku: \"studio\\n\", release: '1.0.0', kind: site}",
		"{sku: studio, release: \"1.0.0\\n\", kind: site}"
	};
	guint i;
	for (i = 0; i < G_N_ELEMENTS(values); i++) {
		g_autoptr(GError) error = NULL;
		g_autoptr(GRegex) pattern = g_regex_new("package: [^\\n]+", 0, 0, &error);
		g_autofree gchar *replacement = g_strconcat("package: ", values[i], NULL);
		g_autofree gchar *text = g_regex_replace_literal(pattern, recipe_text, -1, 0,
			replacement, 0, &error);
		g_autoptr(LsRecipe) recipe = ls_recipe_new(text, &error);
		g_assert_null(recipe);
		g_assert_error(error, LS_ERROR, LS_ERROR_INVALID);
	}
}

/* A sold scope still ends at native acceptance; only prune steps, which can
 * remove build notes but never add content, may follow it. */
static void
package_tail(void){
	const gchar *tails[] = {
		"    - {id: notes, type: prune, options: {paths: [docs/design.org]}}\n",
		"    - {id: notes, type: prune, options: {paths: [docs/design.org]}}\n"
		"    - {id: more, type: prune, options: {paths: [docs/product.org]}}\n",
		"    - {id: late, type: generate}\n",
		"    - {id: late, type: generate}\n"
		"    - {id: notes, type: prune, options: {paths: [docs/design.org]}}\n"
	};
	guint i;
	for (i = 0; i < G_N_ELEMENTS(tails); i++) {
		g_autoptr(GError) error = NULL;
		g_autofree gchar *text = g_strconcat(recipe_text, tails[i], NULL);
		g_autoptr(LsRecipe) recipe = ls_recipe_new(text, &error);
		if (i < 2) {
			g_assert_no_error(error);
			g_assert_nonnull(recipe);
		} else {
			g_assert_null(recipe);
			g_assert_error(error, LS_ERROR, LS_ERROR_INVALID);
		}
	}
}

/* Keep the promised route inventory independent of the recipes so deleting a
 * route from both site.pages and acceptance cannot make a scope regression pass. */
static void
check_scope(const gchar *sku, JsonArray *required){
	const gchar *names[] = {"trades", "professional-services", "restaurant", "nonprofit",
		"studio", "small-saas", "landing"};
	const gchar *routes[] = {
		"index.html,services.html,service-detail.html,coverage.html,about.html,estimate.html,404.html",
		"index.html,services.html,engagement.html,approach.html,insights.html,contact.html,404.html",
		"index.html,menu.html,story.html,visit.html,private-dining.html,reservations.html,404.html",
		"index.html,mission.html,programs.html,impact.html,get-involved.html,support.html,404.html",
		"index.html,work.html,work-atlas.html,work-common.html,approach.html,journal.html,start.html,404.html",
		"index.html,features.html,pricing.html,docs.html,about.html,demo.html,404.html",
		"index.html,work.html,work-atlas.html,work-common.html,approach.html,journal.html,start.html,404.html"
	};
	guint i, j, k;
	for (i = 0; i < G_N_ELEMENTS(names); i++) {
		g_auto(GStrv) expected = NULL;
		g_autofree gchar *all_routes = NULL;
		guint actual_count = 0;
		if (!g_str_equal(sku, names[i]))
			continue;
		all_routes = g_strconcat(routes[i], ",privacy.html,terms.html", NULL);
		expected = g_strsplit(all_routes, ",", -1);
		for (j = 0; j < json_array_get_length(required); j++) {
			const gchar *path = json_array_get_string_element(required, j);
			if (g_str_has_suffix(path, ".html"))
				actual_count++;
		}
		g_assert_cmpuint(actual_count, ==, g_strv_length(expected));
		for (j = 0; expected[j] != NULL; j++) {
			gboolean found = FALSE;
			for (k = 0; k < json_array_get_length(required); k++)
				found |= g_str_equal(expected[j], json_array_get_string_element(required, k));
			g_assert_true(found);
		}
	}
}

/* The native acceptance step: the last step that is not a trailing prune. */
static JsonObject *
acceptance_step(JsonArray *steps){
	guint tail = json_array_get_length(steps);
	while (tail > 1 && g_strcmp0(ls_string(json_array_get_object_element(steps, tail - 1), "type"),
			"prune") == 0)
		tail--;
	return json_array_get_object_element(steps, tail - 1);
}

/* Exercise the actual final native validator on a positive text fixture, then
 * remove each promised file and break every literal predicate independently.
 * This proves source acceptance wiring, not model or browser quality. */
static void
sku_acceptance(gconstpointer data){
	const gchar *sku = (const gchar *)data;
	g_autofree gchar *path = g_strdup_printf("recipes/%s.yaml", sku);
	g_autoptr(GError) error = NULL;
	g_autofree gchar *yaml = ls_read_text(path, &error);
	g_autoptr(LsRecipe) recipe = ls_recipe_new(yaml, &error);
	g_autoptr(LsPrompts) prompts = ls_prompts_new(NULL, &error);
	g_autoptr(LsStep) step = g_object_new(LS_TYPE_VALIDATE_STEP, NULL);
	g_autoptr(LsProvider) provider = LS_PROVIDER(g_object_new(package_provider_get_type(), NULL));
	JsonArray *steps = ls_recipe_get_steps(recipe);
	JsonObject *config = acceptance_step(steps);
	JsonObject *options = json_object_get_object_member(config, "options");
	JsonArray *required = json_object_get_array_member(options, "required");
	JsonArray *checks = json_object_get_array_member(options, "checks");
	g_autoptr(JsonNode) bundle = json_node_new(JSON_NODE_OBJECT);
	g_autoptr(JsonObject) object = json_object_new();
	g_autoptr(JsonArray) files = json_array_new();
	guint i, j, k;
	g_assert_no_error(error);
	g_assert_true(ls_validate_step_check_options(options, &error));
	check_scope(sku, required);
	/* Static SKUs start from native files: the scaffold, or a plan whose
	 * composed site includes the built-in utility pages. */
	if (!g_str_equal(sku, "application")) {
		const gchar *first = ls_string(json_array_get_object_element(steps, 0), "type");
		g_assert_true(g_str_equal(first, "scaffold") || g_str_equal(first, "plan"));
	}
	for (i = 0; i < json_array_get_length(required); i++) {
		const gchar *file_path = json_array_get_string_element(required, i);
		g_autoptr(GString) content = g_string_new("Positive native fixture\n");
		JsonObject *file = json_object_new();
		for (j = 0; j < json_array_get_length(checks); j++) {
			JsonObject *check = json_array_get_object_element(checks, j);
			if (g_str_equal(file_path, ls_string(check, "path"))) {
				JsonArray *contains = json_object_get_array_member(check, "contains");
				for (k = 0; k < json_array_get_length(contains); k++)
					g_string_append_printf(content, "%s\n", json_array_get_string_element(contains, k));
			}
		}
		json_object_set_string_member(file, "path", file_path);
		json_object_set_string_member(file, "content", content->str);
		json_array_add_object_element(files, file);
	}
	json_object_set_array_member(object, "files", json_array_ref(files));
	json_node_set_object(bundle, object);
	for (i = 0; i <= json_array_get_length(files); i++) {
		g_autoptr(LsContext) context = ls_context_new();
		g_autofree gchar *text = NULL;
		JsonObject *file = i > 0 ? json_array_get_object_element(files, i - 1) : NULL;
		g_autofree gchar *original = file != NULL ? g_strdup(ls_string(file, "path")) : NULL;
		if (file != NULL)
			json_object_set_string_member(file, "path", "missing-file-control");
		text = ls_json_encode(bundle);
		g_assert_true(ls_context_apply(context, text, &error));
		if (i == 0)
			g_assert_true(ls_step_execute(step, recipe, config, prompts, context, provider, NULL, &error));
		else {
			g_assert_false(ls_step_execute(step, recipe, config, prompts, context, provider, NULL, &error));
			g_assert_error(error, LS_ERROR, LS_ERROR_VALIDATION);
			g_clear_error(&error);
			json_object_set_string_member(file, "path", original);
		}
	}
	for (i = 0; i < json_array_get_length(checks); i++) {
		JsonObject *check = json_array_get_object_element(checks, i);
		for (j = 0; j < json_array_get_length(files); j++) {
			JsonObject *file = json_array_get_object_element(files, j);
			if (g_str_equal(ls_string(file, "path"), ls_string(check, "path"))) {
				g_autoptr(LsContext) context = ls_context_new();
				g_autofree gchar *original = g_strdup(ls_string(file, "content"));
				g_autofree gchar *text = NULL;
				json_object_set_string_member(file, "content", "Broken literal predicate");
				text = ls_json_encode(bundle);
				g_assert_true(ls_context_apply(context, text, &error));
				g_assert_false(ls_step_execute(step, recipe, config, prompts, context, provider, NULL, &error));
				g_assert_error(error, LS_ERROR, LS_ERROR_VALIDATION);
				g_clear_error(&error);
				json_object_set_string_member(file, "content", original);
			}
		}
	}
	g_assert_cmpuint(((PackageProvider *)provider)->calls, ==, 0);
	g_assert_no_error(error);
}
int
main(int argc, char **argv){
	const gchar *skus[] = {"trades", "professional-services", "restaurant", "nonprofit",
		"studio", "small-saas", "application", "landing"};
	guint i;
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/package/roundtrip", roundtrip);
	g_test_add_func("/package/metadata-invalid", metadata_invalid);
	g_test_add_func("/package/tail", package_tail);
	g_test_add_func("/package/resume", resume_package);
	g_test_add_func("/package/native-snapshot", native_snapshot);
	for (i = 0; i < G_N_ELEMENTS(skus); i++) {
		g_autofree gchar *name = g_strdup_printf("/package/acceptance/%s", skus[i]);
		g_test_add_data_func(name, skus[i], sku_acceptance);
	}
	return g_test_run();
}
