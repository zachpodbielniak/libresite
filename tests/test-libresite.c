/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "libresite.h"
#include <glib/gstdio.h>
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

typedef struct {
	GObject parent_instance;
	guint calls;
	guint fail_at;
	guint malformed;
	GPtrArray *models;
} FixtureProvider;
typedef struct {
	GObjectClass parent_class;
} FixtureProviderClass;
static void fixture_iface_init(LsProviderInterface *iface);
GType fixture_provider_get_type(void);
G_DEFINE_TYPE_WITH_CODE(
	FixtureProvider,
	fixture_provider,
	G_TYPE_OBJECT,
	G_IMPLEMENT_INTERFACE(LS_TYPE_PROVIDER, fixture_iface_init)
)
static const gchar *bundle =
	"{\"files\":[{\"path\":\"index.html\",\"content\":\"<!doctype html><html "
	"lang=\\\"en\\\"><title>Test</title><main><h1>Working "
	"site</h1></main></html>\"},{\"path\":\"docs/build.org\",\"content\":\"#+title: Build\\nOpen "
	"index.html locally.\\n\"}]}";
static const gchar *recipe_text =
	"version: 1\nname: test\nsite:\n  brief: Build a small site\n  style: editorial\nai:\n  "
	"provider: codex\n  model: first-model\n  retries: 0\npipeline:\n  steps:\n"
	"    - id: first\n      type: frontend\n"
	"    - id: second\n      type: review\n      ai:\n        provider: claude-code\n        "
	"model: second-model\n"
	"    - id: verify\n      type: validate\n      options:\n        required: [index.html, "
	"docs/build.org]\n";
/* Inject only the transport. Production recipe, prompts, steps, artifact store,
 * checkpoints, publication and signals remain in every pipeline test. */
static gchar *
fixture_generate(
	LsProvider *provider,
	JsonObject *settings,
	const gchar *system,
	const gchar *prompt,
	GCancellable *cancellable,
	GError **error
){
	FixtureProvider *self = (FixtureProvider *)provider;
	(void)cancellable;
	g_assert_nonnull(strstr(system, "RESPONSE PROTOCOL"));
	g_assert_nonnull(strstr(prompt, "CURRENT PROJECT"));
	self->calls++;
	g_ptr_array_add(self->models, g_strdup(ls_string(settings, "model")));
	if (self->calls == self->fail_at) {
		g_set_error_literal(error, LS_ERROR, LS_ERROR_PROVIDER, "Simulated provider interruption");
		return NULL;
	}
	if (self->malformed > 0) {
		self->malformed--;
		return g_strdup("not JSON");
	}
	return g_strdup(bundle);
}
static void
fixture_iface_init(LsProviderInterface *iface){
	iface->generate = fixture_generate;
}
static void
fixture_finalize(GObject *object){
	g_ptr_array_unref(((FixtureProvider *)object)->models);
	G_OBJECT_CLASS(fixture_provider_parent_class)->finalize(object);
}
static void
fixture_provider_class_init(FixtureProviderClass *klass){
	G_OBJECT_CLASS(klass)->finalize = fixture_finalize;
}
static void
fixture_provider_init(FixtureProvider *self){
	self->models = g_ptr_array_new_with_free_func(g_free);
}
/* Remove only test-owned temporary directories, without following symlinks. */
static void
remove_tree(const gchar *path){
	if (g_file_test(path, G_FILE_TEST_IS_DIR) && !g_file_test(path, G_FILE_TEST_IS_SYMLINK)) {
		GDir *dir = g_dir_open(path, 0, NULL);
		const gchar *name;
		g_assert_nonnull(dir);
		while ((name = g_dir_read_name(dir)) != NULL) {
			g_autofree gchar *child = g_build_filename(path, name, NULL);
			remove_tree(child);
		}
		g_dir_close(dir);
		g_assert_cmpint(g_rmdir(path), ==, 0);
	} else
		g_assert_cmpint(g_unlink(path), ==, 0);
}
static void
invalid_recipe(gconstpointer data){
	g_autoptr(GError) error = NULL;
	g_autoptr(LsRecipe) recipe = ls_recipe_new((const gchar *)data, &error);
	g_assert_null(recipe);
	g_assert_nonnull(error);
}
static void
valid_recipe(void){
	g_autoptr(GError) error = NULL;
	g_autoptr(LsRecipe) recipe = ls_recipe_new(recipe_text, &error);
	g_autoptr(JsonObject) settings = NULL;
	JsonArray *steps;
	g_assert_no_error(error);
	g_assert_nonnull(recipe);
	steps = ls_recipe_get_steps(recipe);
	g_assert_cmpuint(json_array_get_length(steps), ==, 3);
	settings = ls_recipe_resolve_ai(recipe, json_array_get_object_element(steps, 1));
	g_assert_cmpstr(ls_string(settings, "model"), ==, "second-model");
	g_assert_cmpstr(ls_string(settings, "provider"), ==, "claude-code");
	g_assert_cmpint(json_object_get_int_member(settings, "timeout"), ==, 600);
}
static void
provider_defaults(void){
	g_autoptr(LsRecipe) recipe = ls_recipe_new(recipe_text, NULL);
	g_autoptr(JsonObject) step = json_object_new();
	g_autoptr(JsonObject) settings = NULL;
	JsonObject *ai = json_object_new();
	json_object_set_string_member(ai, "provider", "cursor");
	json_object_set_object_member(step, "ai", ai);
	settings = ls_recipe_resolve_ai(recipe, step);
	g_assert_null(ls_string(settings, "model"));
	g_assert_cmpstr(ls_string(settings, "provider"), ==, "cursor");
}
static void
invalid_path(gconstpointer data){
	g_autoptr(GError) error = NULL;
	g_autoptr(LsArtifact) artifact = ls_artifact_new((const gchar *)data, "content", &error);
	g_assert_null(artifact);
	g_assert_error(error, LS_ERROR, LS_ERROR_INVALID);
}
static void
boxed_artifact(void){
	g_autoptr(LsArtifact) original = ls_artifact_new("assets/brand.svg", "<svg/>", NULL);
	g_autoptr(LsArtifact) copy = (LsArtifact *)g_boxed_copy(LS_TYPE_ARTIFACT, original);
	g_assert_cmpstr(ls_artifact_get_path(copy), ==, "assets/brand.svg");
	g_assert_cmpstr(ls_artifact_get_content(copy), ==, "<svg/>");
	g_assert_true(copy != original);
}
static void
invalid_bundle(gconstpointer data){
	g_autoptr(LsContext) context = ls_context_new();
	g_autoptr(GError) error = NULL;
	g_autofree gchar *before = NULL, *after = NULL;
	g_assert_true(ls_context_apply(context, bundle, &error));
	g_assert_no_error(error);
	before = ls_context_snapshot(context);
	g_assert_false(ls_context_apply(context, (const gchar *)data, &error));
	g_assert_nonnull(error);
	after = ls_context_snapshot(context);
	g_assert_cmpstr(before, ==, after);
}
static void
artifact_replace(void){
	g_autoptr(LsContext) context = ls_context_new();
	g_assert_true(ls_context_apply(context, bundle, NULL));
	g_assert_true(ls_context_apply(
		context, "{\"files\":[{\"path\":\"index.html\",\"content\":\"new\"}]}", NULL
	));
	g_assert_cmpstr(ls_artifact_get_content(ls_context_lookup(context, "index.html")), ==, "new");
	g_assert_nonnull(ls_context_lookup(context, "docs/build.org"));
}
static void
count_step(LsPipeline *pipeline, const gchar *id, guint index, gpointer data){
	guint *count = (guint *)data;
	(void)pipeline;
	(void)id;
	(void)index;
	(*count)++;
}
static void
count_artifact(LsPipeline *pipeline, LsArtifact *artifact, gpointer data){
	guint *count = (guint *)data;
	(void)pipeline;
	g_assert_true(ls_path_valid(ls_artifact_get_path(artifact)));
	(*count)++;
}
/* Each mode measures a different lifecycle boundary against a positive run. */
static void
pipeline_case(gconstpointer data){
	const gchar *mode = (const gchar *)data;
	g_autoptr(GError) error = NULL;
	g_autoptr(LsRecipe) recipe = ls_recipe_new(recipe_text, &error);
	g_autoptr(LsPrompts) prompts = ls_prompts_new(NULL, &error);
	g_autoptr(GObject) provider = g_object_new(fixture_provider_get_type(), NULL);
	FixtureProvider *fixture = (FixtureProvider *)provider;
	g_autoptr(LsPipeline) pipeline = ls_pipeline_new(LS_PROVIDER(provider), prompts);
	g_autoptr(GCancellable) cancellable = g_cancellable_new();
	g_autofree gchar *temp = g_dir_make_tmp("libresite-test-XXXXXX", &error);
	g_autofree gchar *output = g_build_filename(temp, "site", NULL);
	g_autofree gchar *work = g_strconcat(output, ".libresite-work", NULL);
	g_autofree gchar *checkpoint = g_build_filename(work, "checkpoint.json", NULL);
	g_autofree gchar *index = g_build_filename(output, "index.html", NULL);
	guint started = 0, completed = 0, written = 0;
	g_assert_no_error(error);
	g_signal_connect(pipeline, "step-started", G_CALLBACK(count_step), &started);
	g_signal_connect(pipeline, "step-completed", G_CALLBACK(count_step), &completed);
	g_signal_connect(pipeline, "artifact-written", G_CALLBACK(count_artifact), &written);
	if (g_str_equal(mode, "cancel")) {
		g_cancellable_cancel(cancellable);
		g_assert_false(ls_pipeline_run(pipeline, recipe, output, FALSE, cancellable, &error));
		g_assert_error(error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
		g_assert_false(g_file_test(work, G_FILE_TEST_EXISTS));
		g_assert_cmpuint(fixture->calls, ==, 0);
	} else if (g_str_equal(mode, "existing")) {
		g_assert_cmpint(g_mkdir(output, 0700), ==, 0);
		g_assert_false(ls_pipeline_run(pipeline, recipe, output, FALSE, NULL, &error));
		g_assert_error(error, LS_ERROR, LS_ERROR_CONFLICT);
		g_assert_cmpuint(fixture->calls, ==, 0);
	} else if (g_str_equal(mode, "symlink")) {
		g_assert_cmpint(symlink(temp, work), ==, 0);
		g_assert_false(ls_pipeline_run(pipeline, recipe, output, TRUE, NULL, &error));
		g_assert_error(error, LS_ERROR, LS_ERROR_CONFLICT);
		g_assert_cmpuint(fixture->calls, ==, 0);
	} else if (g_str_equal(mode, "lock")) {
		g_autofree gchar *lock = g_build_filename(work, "lock", NULL);
		gint fd;
		g_assert_cmpint(g_mkdir(work, 0700), ==, 0);
		fd = g_open(lock, O_CREAT | O_RDWR, 0600);
		g_assert_cmpint(fd, >=, 0);
		g_assert_cmpint(flock(fd, LOCK_EX), ==, 0);
		g_assert_false(ls_pipeline_run(pipeline, recipe, output, TRUE, NULL, &error));
		g_assert_error(error, LS_ERROR, LS_ERROR_CONFLICT);
		g_assert_cmpuint(fixture->calls, ==, 0);
		close(fd);
	} else if (
		g_str_equal(mode, "resume") || g_str_equal(mode, "corrupt") || g_str_equal(mode, "changed")
	) {
		fixture->fail_at = 2;
		g_assert_false(ls_pipeline_run(pipeline, recipe, output, FALSE, NULL, &error));
		g_assert_error(error, LS_ERROR, LS_ERROR_PROVIDER);
		g_clear_error(&error);
		g_assert_false(g_file_test(output, G_FILE_TEST_EXISTS));
		g_assert_true(g_file_test(checkpoint, G_FILE_TEST_IS_REGULAR));
		g_assert_cmpuint(completed, ==, 1);
		if (g_str_equal(mode, "corrupt")) {
			g_assert_true(g_file_set_contents(checkpoint, "{}", -1, NULL));
			g_assert_false(ls_pipeline_run(pipeline, recipe, output, TRUE, NULL, &error));
			g_assert_error(error, LS_ERROR, LS_ERROR_CONFLICT);
		} else if (g_str_equal(mode, "changed")) {
			g_autofree gchar *changed = g_strconcat(recipe_text, "# harmless comment\n", NULL);
			g_autoptr(LsRecipe) other = ls_recipe_new(changed, NULL);
			/* Semantic changes invalidate; formatting-only changes do not. */
			json_object_set_string_member(ls_recipe_get_root(other), "name", "different");
			g_assert_false(ls_pipeline_run(pipeline, other, output, TRUE, NULL, &error));
			g_assert_error(error, LS_ERROR, LS_ERROR_CONFLICT);
		} else {
			g_assert_true(ls_pipeline_run(pipeline, recipe, output, TRUE, NULL, &error));
			g_assert_no_error(error);
			g_assert_cmpuint(fixture->calls, ==, 3);
			g_assert_cmpuint(completed, ==, 3);
			g_assert_cmpuint(written, ==, 2);
			g_assert_true(g_file_test(index, G_FILE_TEST_IS_REGULAR));
			g_assert_false(g_file_test(work, G_FILE_TEST_EXISTS));
		}
	} else {
		g_assert_true(ls_pipeline_run(pipeline, recipe, output, FALSE, NULL, &error));
		g_assert_no_error(error);
		g_assert_cmpuint(fixture->calls, ==, 2);
		g_assert_cmpuint(started, ==, 3);
		g_assert_cmpuint(completed, ==, 3);
		g_assert_cmpuint(written, ==, 2);
		g_assert_cmpstr((const gchar *)g_ptr_array_index(fixture->models, 0), ==, "first-model");
		g_assert_cmpstr((const gchar *)g_ptr_array_index(fixture->models, 1), ==, "second-model");
		g_assert_true(g_file_test(index, G_FILE_TEST_IS_REGULAR));
	}
	remove_tree(temp);
}
static void
retry_step(void){
	g_autoptr(GObject) provider = g_object_new(fixture_provider_get_type(), NULL);
	FixtureProvider *fixture = (FixtureProvider *)provider;
	g_autoptr(LsPrompts) prompts = ls_prompts_new(NULL, NULL);
	g_autoptr(LsRecipe) recipe = ls_recipe_new(recipe_text, NULL);
	g_autoptr(LsContext) context = ls_context_new();
	g_autoptr(LsStep) step = g_object_new(LS_TYPE_GENERATE_STEP, NULL);
	g_autoptr(GError) error = NULL;
	JsonObject *config = json_array_get_object_element(ls_recipe_get_steps(recipe), 0);
	JsonObject *ai = json_object_new();
	json_object_set_int_member(ai, "retries", 1);
	json_object_set_object_member(config, "ai", ai);
	fixture->malformed = 1;
	g_assert_true(
		ls_step_execute(step, recipe, config, prompts, context, LS_PROVIDER(provider), NULL, &error)
	);
	g_assert_no_error(error);
	g_assert_cmpuint(fixture->calls, ==, 2);
	g_assert_nonnull(ls_context_lookup(context, "index.html"));
}
static void
prompt_limit(void){
	g_autoptr(LsPrompts) prompts = ls_prompts_new(NULL, NULL);
	g_autoptr(LsRecipe) recipe = ls_recipe_new(recipe_text, NULL);
	g_autoptr(LsContext) context = ls_context_new();
	g_autoptr(GError) error = NULL;
	g_autofree gchar *large = g_strnfill(100001, 'x');
	g_autofree gchar *response =
		g_strdup_printf("{\"files\":[{\"path\":\"big.txt\",\"content\":\"%s\"}]}", large);
	g_autofree gchar *prompt = NULL;
	g_assert_true(ls_context_apply(context, response, NULL));
	prompt = ls_prompts_render(
		prompts,
		recipe,
		json_array_get_object_element(ls_recipe_get_steps(recipe), 0),
		context,
		&error
	);
	g_assert_null(prompt);
	g_assert_error(error, LS_ERROR, LS_ERROR_LIMIT);
}
static void
registry(void){
	g_autoptr(LsAiProvider) provider = ls_ai_provider_new();
	g_autoptr(LsPrompts) prompts = ls_prompts_new(NULL, NULL);
	g_autoptr(LsPipeline) pipeline = ls_pipeline_new(LS_PROVIDER(provider), prompts);
	g_autoptr(GError) error = NULL;
	g_assert_false(ls_pipeline_register(pipeline, "abstract", LS_TYPE_STEP, &error));
	g_assert_error(error, LS_ERROR, LS_ERROR_INVALID);
	g_clear_error(&error);
	g_assert_false(ls_pipeline_register(pipeline, "frontend", LS_TYPE_GENERATE_STEP, &error));
	g_assert_error(error, LS_ERROR, LS_ERROR_INVALID);
	g_clear_error(&error);
	g_assert_true(ls_pipeline_register(pipeline, "custom", LS_TYPE_GENERATE_STEP, &error));
	g_assert_no_error(error);
}
/* Exercise JSON lexical boundaries independently of the artifact schema. */
static void
json_limits(void){
	g_autoptr(GError) error = NULL;
	g_autoptr(JsonNode) parsed = NULL;
	g_autoptr(GString) deep = g_string_new(NULL);
	guint i;
	parsed = ls_json_parse("{\"literal\":\"\\\\u0000\"}", &error);
	g_assert_no_error(error);
	g_assert_nonnull(parsed);
	g_clear_pointer(&parsed, json_node_unref);
	for (i = 0; i < 40; i++) g_string_append_c(deep, '[');
	g_string_append_c(deep, '0');
	for (i = 0; i < 40; i++) g_string_append_c(deep, ']');
	parsed = ls_json_parse(deep->str, &error);
	g_assert_null(parsed);
	g_assert_error(error, LS_ERROR, LS_ERROR_INVALID);
}

/* Catalog configuration errors must be caught by local preflight. */
static void
preflight_case(gconstpointer data){
	const gchar *mode = (const gchar *)data;
	g_autoptr(LsRecipe) recipe = ls_recipe_new(recipe_text, NULL);
	g_autoptr(LsAiProvider) provider = ls_ai_provider_new();
	g_autoptr(LsPrompts) prompts = ls_prompts_new(NULL, NULL);
	g_autoptr(LsPipeline) pipeline = ls_pipeline_new(LS_PROVIDER(provider), prompts);
	g_autoptr(GError) error = NULL;
	JsonObject *step = json_array_get_object_element(ls_recipe_get_steps(recipe), 0);
	if (g_str_equal(mode, "style"))
		json_object_set_string_member(json_object_get_object_member(ls_recipe_get_root(recipe), "site"), "style", "missing");
	else if (g_str_equal(mode, "prompt"))
		json_object_set_string_member(step, "prompt", "missing");
	else if (g_str_equal(mode, "type"))
		json_object_set_string_member(step, "type", "missing");
	else {
		JsonObject *options = json_object_new();
		json_object_set_string_member(step, "type", "validate");
		json_object_set_string_member(options, "required", "not an array");
		json_object_set_object_member(step, "options", options);
	}
	g_assert_false(ls_pipeline_validate(pipeline, recipe, &error));
	g_assert_error(error, LS_ERROR, LS_ERROR_INVALID);
}

/* Replacement catalogs are checked using the same path as the installed CLI. */
static void
catalog_override(void){
	g_autoptr(GError) error = NULL;
	g_autofree gchar *temp = g_dir_make_tmp("libresite-catalog-XXXXXX", NULL);
	g_autofree gchar *path = g_build_filename(temp, "prompts.yaml", NULL);
	g_autoptr(LsPrompts) prompts = NULL;
	g_assert_true(g_file_set_contents(path, "version: 1\nsystem: Custom\nstyles: {editorial: Custom}\nstages: {frontend: Custom}\n", -1, NULL));
	prompts = ls_prompts_new(path, &error);
	g_assert_no_error(error);
	g_assert_cmpstr(ls_prompts_get_system(prompts), ==, "Custom");
	g_clear_object(&prompts);
	g_assert_true(g_file_set_contents(path, "version: 1\nsystem: Custom\nstyles: []\nstages: {}\n", -1, NULL));
	prompts = ls_prompts_new(path, &error);
	g_assert_null(prompts);
	g_assert_error(error, LS_ERROR, LS_ERROR_INVALID);
	remove_tree(temp);
}

/* Test-data tables keep edge cases explicit and independently reported. */
int
main(int argc, char **argv){
	const gchar *invalid_recipes[] = {
		"",
		"[]",
		"name: x\nname: y",
		"---\nname: x\n---\nname: y",
		"name: &x hi\nsite: *x",
		"? [a, b]\n: c",
		"name: !!str x",
		"version: 1\nname: test\nsite: {}\npipeline: {steps: []}",
		"version: 2\nname: test\nsite: {brief: hi}\npipeline: {steps: [{id: a, type: frontend}]}",
		"version: 1\nname: test\nsite: {brief: hi}\nai: {provider: nope}\npipeline: {steps: [{id: "
		"a, type: frontend}]}",
		"version: 1\nname: test\nsite: {brief: hi}\nai: {timeout: -1}\npipeline: {steps: [{id: a, "
		"type: frontend}]}",
		"version: 1\nname: test\nsite: {brief: hi}\nai: {retries: 1.5}\npipeline: {steps: [{id: a, "
		"type: frontend}]}",
		"version: 1\nname: test\nsite: {brief: hi}\nai: {modle: x}\npipeline: {steps: [{id: a, "
		"type: frontend}]}",
		"version: 1\nname: test\nsite: {brief: hi}\npipeline: {steps: [{id: a, type: frontend}, "
		"{id: a, type: review}]}",
		"version: 1\nname: test\nsite: {brief: true}\npipeline: {steps: [{id: a, type: frontend}]}",
		"version: 1\nname: test\nsite: {brief: hi}\npipeline: {steps: [{id: a, type: frontend, "
		"options: []}]}"
	};
	const gchar *paths[] = {
		"",
		"/etc/passwd",
		"../x",
		"a/../x",
		"a//x",
		"a/./x",
		"C:\\x",
		"a\\b",
		".git/config",
		".libresite/run.json",
		"a/",
		"x\ny",
		".",
		".."
	};
	const gchar *bundles[] = {
		"{\"files\":[],\"files\":[]}",
		"{\"files\":[{\"path\":\"a\",\"path\":\"b\",\"content\":\"x\"}]}",
		"{\"files\":[{\"path\":\"a\\u0000b\",\"content\":\"x\"}]}",
		"{\"files\":[{\"path\":\"a\",\"content\":\"x\\u0000y\"}]}",
		"bad",
		"[]",
		"{\"files\":null}",
		"{\"files\":[null]}",
		"{\"files\":[{\"path\":\"a\",\"content\":3}]}",
		"{\"files\":[{\"path\":\"new\",\"content\":\"x\"},{\"path\":\"../"
		"escape\",\"content\":\"bad\"}]}",
		"{\"files\":[{\"path\":\"index.html\",\"content\":\"a\"},{\"path\":\"index.html\","
		"\"content\":\"b\"}]}",
		"{\"files\":[{\"path\":\"docs\",\"content\":\"a\"}]}",
		"{\"files\":[{\"path\":\"a\",\"content\":\"a\"},{\"path\":\"a/b\",\"content\":\"b\"}]}",
		"{\"files\":[],\"commands\":[\"evil\"]}",
		"{\"files\":[],\"summary\":false}"
	};
	const gchar *modes[] = {
		"success", "resume", "corrupt", "changed", "cancel", "existing", "symlink", "lock"
	};
	guint i;
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/recipe/valid", valid_recipe);
	g_test_add_func("/recipe/provider-defaults", provider_defaults);
	for (i = 0; i < G_N_ELEMENTS(invalid_recipes); i++) {
		g_autofree gchar *name = g_strdup_printf("/recipe/invalid/%u", i);
		g_test_add_data_func(name, invalid_recipes[i], invalid_recipe);
	}
	for (i = 0; i < G_N_ELEMENTS(paths); i++) {
		g_autofree gchar *name = g_strdup_printf("/artifact/path/%u", i);
		g_test_add_data_func(name, paths[i], invalid_path);
	}
	for (i = 0; i < G_N_ELEMENTS(bundles); i++) {
		g_autofree gchar *name = g_strdup_printf("/artifact/transaction/%u", i);
		g_test_add_data_func(name, bundles[i], invalid_bundle);
	}
	for (i = 0; i < G_N_ELEMENTS(modes); i++) {
		g_autofree gchar *name = g_strdup_printf("/pipeline/%s", modes[i]);
		g_test_add_data_func(name, modes[i], pipeline_case);
	}
	g_test_add_func("/artifact/boxed", boxed_artifact);
	g_test_add_func("/artifact/replace", artifact_replace);
	g_test_add_func("/step/retry", retry_step);
	g_test_add_func("/prompt/limit", prompt_limit);
	g_test_add_func("/pipeline/registry", registry);
	g_test_add_func("/json/limits", json_limits);
	g_test_add_func("/prompt/catalog", catalog_override);
	g_test_add_data_func("/preflight/style", "style", preflight_case);
	g_test_add_data_func("/preflight/prompt", "prompt", preflight_case);
	g_test_add_data_func("/preflight/type", "type", preflight_case);
	g_test_add_data_func("/preflight/options", "options", preflight_case);
	return g_test_run();
}
