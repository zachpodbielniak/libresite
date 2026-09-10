/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "libresite.h"
#include <glib/gstdio.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

typedef struct {
	GObject parent_instance;
	guint calls;
	guint fail_at;
	guint malformed;
	guint incomplete;
	GPtrArray *models;
	gboolean revision;
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
	g_assert_nonnull(strstr(prompt, "PIPELINE ACCEPTANCE"));
	g_assert_nonnull(strstr(prompt, "docs/build.org"));
	self->calls++;
	if (self->revision) {
		g_assert_nonnull(strstr(prompt, "SITE DATA brief is the change brief"));
		g_assert_nonnull(strstr(prompt, "user-maintained sentinel"));
		if (self->calls > 1)
			g_assert_null(strstr(prompt, "obsolete sentinel"));
	}
	g_ptr_array_add(self->models, g_strdup(ls_string(settings, "model")));
	if (self->calls == self->fail_at) {
		g_set_error_literal(error, LS_ERROR, LS_ERROR_PROVIDER, "Simulated provider interruption");
		return NULL;
	}
	if (self->malformed > 0) {
		self->malformed--;
		return g_strdup("not JSON");
	}
	if (self->incomplete > 0) {
		self->incomplete--;
		return g_strdup("{\"files\":[{\"path\":\"index.html\",\"content\":\"incorrect draft\"},{\"path\":\"unwanted.txt\",\"content\":\"rejected attempt\"}]}");
	}
	if (self->revision && self->calls == 1)
		return g_strdup("{\"files\":[{\"path\":\"index.html\",\"content\":\"revised\"},"
			"{\"path\":\"docs/build.org\",\"content\":\"updated docs\"}],\"delete\":[\"old.txt\"]}");
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

/* A failed mixed delete/write bundle cannot discard any existing source. */
static void
artifact_delete(void){
	g_autoptr(LsContext) context = ls_context_new();
	g_autoptr(GError) error = NULL;
	g_autofree gchar *snapshot = NULL;
	g_assert_true(ls_context_apply(context, bundle, NULL));
	g_assert_true(ls_context_apply(context,
		"{\"files\":[{\"path\":\"docs\",\"content\":\"now a file\",\"executable\":true}],"
		"\"delete\":[\"docs/build.org\"]}", &error));
	g_assert_no_error(error);
	g_assert_null(ls_context_lookup(context, "docs/build.org"));
	g_assert_nonnull(ls_context_lookup(context, "index.html"));
	g_assert_true(ls_artifact_get_executable(ls_context_lookup(context, "docs")));
	g_assert_true(ls_context_apply(context,
		"{\"files\":[{\"path\":\"docs\",\"content\":\"keep execute\"}]}", &error));
	g_assert_true(ls_artifact_get_executable(ls_context_lookup(context, "docs")));
	snapshot = ls_context_snapshot(context);
	g_assert_true(ls_context_restore(context, snapshot, &error));
	g_assert_true(ls_artifact_get_executable(ls_context_lookup(context, "docs")));
	g_assert_true(ls_context_apply(context,
		"{\"files\":[{\"path\":\"docs\",\"content\":\"plain\",\"executable\":false}]}", &error));
	g_assert_false(ls_artifact_get_executable(ls_context_lookup(context, "docs")));
	g_assert_true(ls_context_restore(context, "{\"files\":[]}", &error));
	g_assert_null(ls_context_lookup(context, "index.html"));
}

/* Import rejects unsupported entries explicitly instead of losing source data.
 * Metadata exclusion has a positive control containing deliberately invalid data. */
static void
import_case(gconstpointer data){
	const gchar *mode = (const gchar *)data;
	g_autofree gchar *temp = g_dir_make_tmp("libresite-import-XXXXXX", NULL);
	g_autofree gchar *file = g_build_filename(temp, "source.txt", NULL);
	g_autofree gchar *metadata = g_build_filename(temp, ".git", NULL);
	g_autoptr(GError) error = NULL;
	g_autoptr(LsContext) context = NULL;
	g_autoptr(GCancellable) cancellable = g_cancellable_new();
	g_assert_true(g_file_set_contents(metadata, "\0", 1, NULL));
	if (g_str_equal(mode, "symlink"))
		g_assert_cmpint(symlink(".git", file), ==, 0);
	else if (g_str_equal(mode, "fifo"))
		g_assert_cmpint(mkfifo(file, 0600), ==, 0);
	else if (g_str_equal(mode, "binary"))
		g_assert_true(g_file_set_contents(file, "before\0after", 12, NULL));
	else if (g_str_equal(mode, "utf8"))
		g_assert_true(g_file_set_contents(file, "\xff", 1, NULL));
	else if (!g_str_equal(mode, "empty")) {
		g_assert_true(g_file_set_contents(file, "text sentinel", -1, NULL));
		g_assert_cmpint(g_chmod(file, 0700), ==, 0);
	}
	if (g_str_equal(mode, "cancel"))
		g_cancellable_cancel(cancellable);
	context = ls_context_import(temp, cancellable, &error);
	if (g_str_equal(mode, "success")) {
		g_assert_no_error(error);
		g_assert_nonnull(context);
		g_assert_true(ls_context_is_revision(context));
		g_assert_cmpstr(ls_artifact_get_content(ls_context_lookup(context, "source.txt")),
			==, "text sentinel");
		g_assert_true(ls_artifact_get_executable(ls_context_lookup(context, "source.txt")));
		g_assert_null(ls_context_lookup(context, ".git"));
	} else {
		g_assert_null(context);
		g_assert_nonnull(error);
		if (g_str_equal(mode, "cancel"))
			g_assert_error(error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
	}
	remove_tree(temp);
}

/* Check incremental publication and source-bound recovery through real steps. */
static void
revision_case(gconstpointer data){
	const gchar *mode = (const gchar *)data;
	g_autoptr(GError) error = NULL;
	g_autoptr(LsRecipe) recipe = ls_recipe_new(recipe_text, NULL);
	g_autoptr(LsPrompts) prompts = ls_prompts_new(NULL, NULL);
	g_autoptr(GObject) provider = g_object_new(fixture_provider_get_type(), NULL);
	FixtureProvider *fixture = (FixtureProvider *)provider;
	g_autoptr(LsPipeline) pipeline = ls_pipeline_new(LS_PROVIDER(provider), prompts);
	g_autofree gchar *temp = g_dir_make_tmp("libresite-revision-XXXXXX", NULL);
	g_autofree gchar *project = g_build_filename(temp, "source", NULL);
	g_autofree gchar *output = g_build_filename(temp, "result", NULL);
	g_autofree gchar *retained = g_build_filename(project, "retained.txt", NULL);
	g_autofree gchar *obsolete = g_build_filename(project, "old.txt", NULL);
	g_autofree gchar *published = g_build_filename(output, "retained.txt", NULL);
	g_autofree gchar *deleted = g_build_filename(output, "old.txt", NULL);
	g_autofree gchar *before = NULL, *after = NULL;
	g_autoptr(LsContext) source = NULL;
	GStatBuf info;
	g_assert_cmpint(g_mkdir(project, 0700), ==, 0);
	g_assert_true(g_file_set_contents(retained, "user-maintained sentinel", -1, NULL));
	g_assert_cmpint(g_chmod(retained, 0700), ==, 0);
	g_assert_true(g_file_set_contents(obsolete, "obsolete sentinel", -1, NULL));
	source = ls_context_import(project, NULL, NULL);
	before = ls_context_snapshot(source);
	fixture->revision = TRUE;
	if (g_str_equal(mode, "nested")) {
		g_autofree gchar *nested = g_build_filename(project, "output", NULL);
		g_assert_false(ls_pipeline_revise(pipeline, recipe, project, nested, FALSE, NULL, &error));
		g_assert_error(error, LS_ERROR, LS_ERROR_CONFLICT);
		g_assert_cmpuint(fixture->calls, ==, 0);
	} else {
		fixture->fail_at = 2;
		g_assert_false(ls_pipeline_revise(pipeline, recipe, project, output, FALSE, NULL, &error));
		g_assert_error(error, LS_ERROR, LS_ERROR_PROVIDER);
		g_clear_error(&error);
		g_assert_false(g_file_test(output, G_FILE_TEST_EXISTS));
		if (g_str_equal(mode, "changed"))
			g_assert_true(g_file_set_contents(retained, "changed source", -1, NULL));
		else if (g_str_equal(mode, "mode-changed"))
			g_assert_cmpint(g_chmod(retained, 0600), ==, 0);
		else if (g_str_equal(mode, "brief-changed")) {
			g_autofree gchar *changed = g_strconcat(recipe_text, "# new parser-neutral comment\n", NULL);
			gchar *brief = strstr(changed, "Build a small site");
			/* Same-length content edit changes the parsed recipe fingerprint. */
			memcpy(brief, "Alter a small site", strlen("Alter a small site"));
			g_clear_object(&recipe);
			recipe = ls_recipe_new(changed, NULL);
		}
		if (g_str_equal(mode, "resume")) {
			g_assert_true(ls_pipeline_revise(pipeline, recipe, project, output, TRUE, NULL, &error));
			g_assert_no_error(error);
			g_assert_cmpuint(fixture->calls, ==, 3);
			g_assert_true(g_file_test(published, G_FILE_TEST_IS_REGULAR));
			g_assert_false(g_file_test(deleted, G_FILE_TEST_EXISTS));
			g_assert_cmpint(g_stat(published, &info), ==, 0);
			g_assert_cmpuint(info.st_mode & 0777, ==, 0700);
		} else {
			g_assert_false(ls_pipeline_revise(pipeline, recipe, project, output, TRUE, NULL, &error));
			g_assert_error(error, LS_ERROR, LS_ERROR_CONFLICT);
			g_assert_cmpuint(fixture->calls, ==, 2);
			g_assert_false(g_file_test(output, G_FILE_TEST_EXISTS));
		}
	}
	if (g_str_equal(mode, "nested") || g_str_equal(mode, "resume") ||
		g_str_equal(mode, "brief-changed")) {
		g_clear_object(&source);
		source = ls_context_import(project, NULL, NULL);
		after = ls_context_snapshot(source);
		g_assert_cmpstr(before, ==, after);
	}
	remove_tree(temp);
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
/* A valid JSON bundle can still violate the output contract. Reject it before
 * checkpointing, retry with feedback, and retain none of its incidental files. */
static void
acceptance_retry(gconstpointer data){
	gboolean exhaust = GPOINTER_TO_INT(data);
	g_autoptr(GObject) provider = g_object_new(fixture_provider_get_type(), NULL);
	FixtureProvider *fixture = (FixtureProvider *)provider;
	g_autoptr(LsPrompts) prompts = ls_prompts_new(NULL, NULL);
	g_autoptr(LsRecipe) recipe = ls_recipe_new(recipe_text, NULL);
	g_autoptr(LsContext) context = ls_context_new();
	g_autoptr(LsStep) step = g_object_new(LS_TYPE_GENERATE_STEP, NULL);
	g_autoptr(GError) error = NULL;
	JsonObject *config = json_array_get_object_element(ls_recipe_get_steps(recipe), 1);
	JsonObject *ai = json_object_get_object_member(config, "ai");
	json_object_set_int_member(ai, "retries", 1);
	fixture->incomplete = exhaust ? 2 : 1;
	g_assert_true(ls_context_apply(context, "{\"files\":[{\"path\":\"index.html\",\"content\":\"original\"}]}", NULL));
	if (exhaust) {
		g_assert_false(ls_step_execute(step, recipe, config, prompts, context, LS_PROVIDER(provider), NULL, &error));
		g_assert_error(error, LS_ERROR, LS_ERROR_INVALID);
		g_assert_nonnull(strstr(error->message, "docs/build.org"));
		g_assert_cmpstr(ls_artifact_get_content(ls_context_lookup(context, "index.html")), ==, "original");
	} else {
		g_assert_true(ls_step_execute(step, recipe, config, prompts, context, LS_PROVIDER(provider), NULL, &error));
		g_assert_no_error(error);
		g_assert_nonnull(ls_context_lookup(context, "docs/build.org"));
	}
	g_assert_cmpuint(fixture->calls, ==, 2);
	g_assert_null(ls_context_lookup(context, "unwanted.txt"));
}

static void
prompt_limit(void){
	g_autoptr(LsPrompts) prompts = ls_prompts_new(NULL, NULL);
	g_autoptr(LsRecipe) recipe = ls_recipe_new(recipe_text, NULL);
	g_autoptr(LsContext) context = ls_context_new();
	g_autoptr(GError) error = NULL;
	g_autofree gchar *large = g_strnfill(100001, 'x');
	gsize needed;
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
	g_assert_no_error(error);
	g_assert_nonnull(strstr(prompt, large));
	needed = strlen(prompt) + strlen(ls_prompts_get_system(prompts));
	g_clear_pointer(&prompt, g_free);
	/* Exact boundary accepts, one byte less rejects without truncation. */
	ls_prompts_set_max_bytes(prompts, needed);
	prompt = ls_prompts_render(prompts, recipe,
		json_array_get_object_element(ls_recipe_get_steps(recipe), 0), context, &error);
	g_assert_no_error(error);
	g_assert_nonnull(prompt);
	g_clear_pointer(&prompt, g_free);
	ls_prompts_set_max_bytes(prompts, needed - 1);
	prompt = ls_prompts_render(prompts, recipe,
		json_array_get_object_element(ls_recipe_get_steps(recipe), 0), context, &error);
	g_assert_null(prompt);
	g_assert_error(error, LS_ERROR, LS_ERROR_LIMIT);
	g_clear_error(&error);
	/* Zero removes a previously configured cap. */
	ls_prompts_set_max_bytes(prompts, 0);
	prompt = ls_prompts_render(prompts, recipe,
		json_array_get_object_element(ls_recipe_get_steps(recipe), 0), context, &error);
	g_assert_no_error(error);
	g_assert_nonnull(strstr(prompt, large));
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

/* Bundled application generation must keep loopback Host aliases and AGENTS.md. */
static void
application_generation_contracts(void){
	g_autoptr(GError) error = NULL;
	g_autofree gchar *recipe_yaml = NULL;
	g_autoptr(LsRecipe) recipe = NULL;
	g_autoptr(LsPrompts) prompts = ls_prompts_new(NULL, NULL);
	g_autoptr(LsContext) context = ls_context_new();
	g_autofree gchar *prompt = NULL;
	JsonArray *steps;
	JsonObject *backend;
	JsonObject *acceptance;
	JsonArray *required;
	gboolean saw_agents;
	guint i;
	guint j;

	g_assert_true(g_file_get_contents("recipes/application.yaml", &recipe_yaml, NULL, NULL));
	g_assert_nonnull(strstr(recipe_yaml, "AGENTS.md"));
	g_assert_nonnull(strstr(recipe_yaml, "localhost"));
	recipe = ls_recipe_new(recipe_yaml, &error);
	g_assert_no_error(error);
	g_assert_nonnull(strstr(ls_prompts_get_system(prompts), "AGENTS.md"));
	g_assert_nonnull(strstr(ls_prompts_get_system(prompts), "localhost"));
	g_assert_nonnull(strstr(ls_prompts_get_system(prompts), "do not require a pre-existing .db"));
	backend = NULL;
	acceptance = NULL;
	steps = ls_recipe_get_steps(recipe);
	for (i = 0; i < json_array_get_length(steps); i++) {
		JsonObject *step = json_array_get_object_element(steps, i);
		const gchar *id = json_object_get_string_member(step, "id");
		if (g_strcmp0(id, "backend") == 0)
			backend = step;
		if (g_strcmp0(id, "acceptance") == 0)
			acceptance = step;
	}
	g_assert_nonnull(backend);
	g_assert_nonnull(acceptance);
	required = json_object_get_array_member(json_object_get_object_member(acceptance, "options"), "required");
	saw_agents = FALSE;
	for (j = 0; j < json_array_get_length(required); j++) {
		JsonNode *node = json_array_get_element(required, j);
		const gchar *path = json_node_get_value_type(node) == G_TYPE_STRING
			? json_node_get_string(node)
			: NULL;
		if (g_strcmp0(path, "AGENTS.md") == 0)
			saw_agents = TRUE;
	}
	g_assert_true(saw_agents);
	prompt = ls_prompts_render(prompts, recipe, backend, context, &error);
	g_assert_no_error(error);
	g_assert_nonnull(strstr(prompt, "localhost"));
	g_assert_nonnull(strstr(prompt, "AGENTS.md"));
	g_assert_nonnull(strstr(prompt, "127.0.0.1"));
	g_assert_nonnull(strstr(prompt, "Do not generate a product that tells the user localhost is intentionally rejected"));
}

/* Stack profiles are opt-in catalog data, independent of visual style. Unknown
 * selections fail preflight, before the injected transport can be called. */
static void
stack_profiles(void){
	const gchar *selectors[] = {NULL, "c-glib", "unknown", "../c-glib"};
	const gchar *invalid[] = {"''", "null", "[]", "{}", "42", "false"};
	g_autoptr(LsPrompts) prompts = ls_prompts_new(NULL, NULL);
	g_autoptr(LsContext) context = ls_context_new();
	guint i;
	for (i = 0; i < G_N_ELEMENTS(selectors); i++) {
		g_autofree gchar *field = selectors[i] != NULL
			? g_strdup_printf(", stack: '%s'", selectors[i]) : g_strdup("");
		g_autofree gchar *yaml = g_strdup_printf("version: 1\nname: profile\n"
			"site: {brief: test, style: minimalist%s}\npipeline: {steps: [{id: page, type: frontend}]}\n", field);
		g_autoptr(GError) error = NULL;
		g_autoptr(LsRecipe) recipe = ls_recipe_new(yaml, &error);
		g_autoptr(GObject) provider = g_object_new(fixture_provider_get_type(), NULL);
		g_autoptr(LsPipeline) pipeline = ls_pipeline_new(LS_PROVIDER(provider), prompts);
		g_autofree gchar *rendered = NULL;
		g_assert_no_error(error);
		if (i >= 2) {
			g_assert_false(ls_pipeline_validate(pipeline, recipe, &error));
			g_assert_error(error, LS_ERROR, LS_ERROR_INVALID);
			g_assert_nonnull(strstr(error->message, "Unknown stack profile"));
			g_assert_cmpuint(((FixtureProvider *)provider)->calls, ==, 0);
			continue;
		}
		g_assert_true(ls_pipeline_validate(pipeline, recipe, &error));
		rendered = ls_prompts_render(prompts, recipe,
			json_array_get_object_element(ls_recipe_get_steps(recipe), 0), context, &error);
		g_assert_no_error(error);
		if (i == 0)
			g_assert_null(strstr(rendered, "orm_engine_new_sqlite"));
		else {
			g_assert_nonnull(strstr(rendered, "orm_engine_new_sqlite"));
			g_assert_nonnull(strstr(rendered, "warm monochrome"));
		}
	}
	for (i = 0; i < G_N_ELEMENTS(invalid); i++) {
		g_autofree gchar *yaml = g_strdup_printf("version: 1\nname: profile\n"
			"site: {brief: test, stack: %s}\npipeline: {steps: [{id: page, type: frontend}]}\n", invalid[i]);
		invalid_recipe(yaml);
	}
}

/* Replacement catalogs may omit stacks for old recipes. Every supplied profile
 * is validated, including unused entries, and no embedded fallback leaks in. */
static void
stack_catalogs(void){
	const gchar *groups[] = {"", "stacks: {custom: Custom-stack}\n", "stacks: {}\n",
		"stacks: []\n", "stacks: null\n", "stacks: {unused: ''}\n",
		"stacks: {custom: Valid, unused: 7}\n", "stacks: {unused: {nested: nope}}\n"};
	g_autofree gchar *temp = g_dir_make_tmp("libresite-stacks-XXXXXX", NULL);
	g_autofree gchar *path = g_build_filename(temp, "catalog.yaml", NULL);
	guint i;
	for (i = 0; i < G_N_ELEMENTS(groups); i++) {
		g_autofree gchar *yaml = g_strconcat("version: 1\nsystem: Custom\n"
			"styles: {editorial: Plain}\nstages: {frontend: Page}\n", groups[i], NULL);
		g_autoptr(GError) error = NULL;
		g_autoptr(LsPrompts) prompts = NULL;
		g_assert_true(g_file_set_contents(path, yaml, -1, &error));
		prompts = ls_prompts_new(path, &error);
		if (i >= 2) {
			g_assert_null(prompts);
			g_assert_error(error, LS_ERROR, LS_ERROR_INVALID);
		} else {
			g_autoptr(LsRecipe) recipe = ls_recipe_new("version: 1\nname: test\n"
				"site: {brief: test, stack: custom}\npipeline: {steps: [{id: page, type: frontend}]}", NULL);
			g_autoptr(LsContext) context = ls_context_new();
			g_autofree gchar *rendered = NULL;
			g_assert_no_error(error);
			rendered = ls_prompts_render(prompts, recipe,
				json_array_get_object_element(ls_recipe_get_steps(recipe), 0), context, &error);
			if (i == 0) {
				g_assert_null(rendered);
				g_assert_error(error, LS_ERROR, LS_ERROR_INVALID);
			} else {
				g_assert_no_error(error);
				g_assert_nonnull(strstr(rendered, "Custom-stack"));
				g_assert_null(strstr(rendered, "orm_engine_new"));
			}
		}
	}
	remove_tree(temp);
}

/* All stages receive verified API context; the demo rejects each missing or
 * empty required file. These sentinel bundles test presence, not app quality. */
static void
application_stack_artifacts(void){
	const gchar *contracts[] = {"deps/manifest.yaml", "scripts/bootstrap-deps.sh", "config/application.yaml",
		"src/main.c", "src/app-config.c", "src/app-config.h", "src/pages.c", "src/database.c", "src/database.h",
		"sql/bootstrap-sqlite.sql", "sql/bootstrap-postgresql.sql", "sql/bootstrap-mysql.sql",
		"tests/test-config.c", "tests/test-database.c", "tests/http.sh", "docs/configuration.org", "docs/database.org"};
	g_autofree gchar *yaml = NULL;
	g_autoptr(LsRecipe) recipe = NULL;
	g_autoptr(LsPrompts) prompts = ls_prompts_new(NULL, NULL);
	g_autoptr(LsContext) empty = ls_context_new();
	g_autoptr(LsStep) validator = g_object_new(LS_TYPE_VALIDATE_STEP, NULL);
	JsonArray *steps, *required;
	JsonObject *acceptance;
	guint i, j;
	g_assert_true(g_file_get_contents("recipes/application.yaml", &yaml, NULL, NULL));
	recipe = ls_recipe_new(yaml, NULL);
	g_assert_nonnull(recipe);
	steps = ls_recipe_get_steps(recipe);
	acceptance = json_array_get_object_element(steps, json_array_get_length(steps) - 1);
	required = json_object_get_array_member(json_object_get_object_member(acceptance, "options"), "required");
	for (i = 0; i < G_N_ELEMENTS(contracts); i++) {
		gboolean found = FALSE;
		for (j = 0; j < json_array_get_length(required); j++)
			found |= g_str_equal(contracts[i], json_array_get_string_element(required, j));
		g_assert_true(found);
	}
	for (i = 0; i + 1 < json_array_get_length(steps); i++) {
		g_autoptr(GError) error = NULL;
		g_autofree gchar *rendered = ls_prompts_render(prompts, recipe,
			json_array_get_object_element(steps, i), empty, &error);
		g_assert_no_error(error);
		g_assert_nonnull(strstr(rendered, "yaml_node_to_json_node"));
		g_assert_nonnull(strstr(rendered, "htmx_router_post"));
		g_assert_nonnull(strstr(rendered, "orm_connection_query_with_params"));
		g_assert_nonnull(strstr(rendered, "orm_result_get_error"));
		g_assert_nonnull(strstr(rendered, "gitlab.com/"));
		g_assert_nonnull(strstr(rendered, "sql/bootstrap-postgresql.sql"));
	}
	/* A complete positive control, then missing and empty variants of every path. */
	for (i = 0; i <= json_array_get_length(required) * 2; i++) {
		g_autoptr(LsContext) context = ls_context_new();
		g_autoptr(GError) error = NULL;
		g_autoptr(GString) artifact_bundle = g_string_new("{\"files\":[");
		gboolean first = TRUE;
		for (j = 0; j < json_array_get_length(required); j++) {
			gboolean affected = i > 0 && j == (i - 1) / 2;
			if (affected && i % 2 == 1)
				continue;
			g_string_append_printf(artifact_bundle, "%s{\"path\":\"%s\",\"content\":\"%s\"}",
				first ? "" : ",", json_array_get_string_element(required, j), affected ? "" : "sentinel");
			first = FALSE;
		}
		g_string_append(artifact_bundle, "]}");
		g_assert_true(ls_context_apply(context, artifact_bundle->str, &error));
		g_assert_cmpint(ls_step_execute(validator, recipe, acceptance, prompts, context, NULL, NULL, &error), ==, i == 0);
		if (i == 0)
			g_assert_no_error(error);
		else {
			g_assert_error(error, LS_ERROR, LS_ERROR_INVALID);
			g_assert_nonnull(strstr(error->message, json_array_get_string_element(required, (i - 1) / 2)));
		}
	}
}

/* Exercise the actual bundled final validators, not a second validation schema.
 * Sentinel text satisfies literal predicates only; it is not an SEO-valid site. */
static void
seo_artifacts(gconstpointer data){
	const gchar *recipe_path = (const gchar *)data;
	const gchar *paths[] = {
		"config/seo.yaml", "docs/seo.org", "tests/seo.sh",
		"robots.txt", "scripts/prepare-seo.sh"
	};
	g_autoptr(GError) load_error = NULL;
	g_autofree gchar *yaml = NULL;
	g_autoptr(LsRecipe) recipe = NULL;
	g_autoptr(LsPrompts) prompts = ls_prompts_new(NULL, NULL);
	g_autoptr(LsStep) validator = g_object_new(LS_TYPE_VALIDATE_STEP, NULL);
	g_autofree gchar *padding = g_strnfill(600, ' ');
	JsonArray *steps;
	JsonObject *acceptance;
	JsonArray *required;
	guint count = g_str_has_suffix(recipe_path, "/application.yaml") ? 3 : G_N_ELEMENTS(paths);
	guint i;
	guint j;
	g_assert_true(g_file_get_contents(recipe_path, &yaml, NULL, &load_error));
	g_assert_no_error(load_error);
	recipe = ls_recipe_new(yaml, &load_error);
	g_assert_no_error(load_error);
	g_assert_nonnull(recipe);
	steps = ls_recipe_get_steps(recipe);
	acceptance = json_array_get_object_element(steps, json_array_get_length(steps) - 1);
	required = json_object_get_array_member(json_object_get_object_member(acceptance, "options"), "required");
	/* Removing an SEO deliverable from a recipe must fail this test too. */
	for (i = 0; i < count; i++) {
		gboolean found = FALSE;
		for (j = 0; j < json_array_get_length(required); j++)
			found |= g_str_equal(paths[i], json_array_get_string_element(required, j));
		g_assert_true(found);
	}
	/* Positive control, then missing, empty and whitespace-only SEO artifacts. */
	for (i = 0; i <= count * 3; i++) {
		g_autoptr(LsContext) context = ls_context_new();
		g_autoptr(GError) error = NULL;
		g_autoptr(GString) candidate = g_string_new("{\"files\":[");
		gboolean first = TRUE;
		for (j = 0; j < json_array_get_length(required); j++) {
			const gchar *path = json_array_get_string_element(required, j);
			gboolean affected = i > 0 && g_str_equal(path, paths[(i - 1) / 3]);
			guint variant = i > 0 ? (i - 1) % 3 : 0;
			if (affected && variant == 0)
				continue;
			g_string_append_printf(candidate, "%s{\"path\":\"%s\",\"content\":\"", first ? "" : ",", path);
			if (!affected) {
				/* The checked landing also requires these exact literal tokens. */
				g_string_append(candidate,
					"<html lang=\\\"en\\\"><title>Fixture</title>"
					"<meta name=\\\"viewport\\\"><meta name=\\\"description\\\">"
					"<meta property=\\\"og:title\\\"><meta name=\\\"twitter:card\\\">"
					"<main>noindex</main></html> :focus-visible prefers-reduced-motion #+title:");
				g_string_append(candidate, padding);
			} else if (variant == 2) {
				g_string_append(candidate, " \\n\\t");
			}
			g_string_append(candidate, "\"}");
			first = FALSE;
		}
		g_string_append(candidate, "]}");
		g_assert_true(ls_context_apply(context, candidate->str, &error));
		g_assert_no_error(error);
		g_assert_cmpint(ls_step_execute(validator, recipe, acceptance, prompts, context, NULL, NULL, &error), ==, i == 0);
		if (i == 0)
			g_assert_no_error(error);
		else {
			g_assert_error(error, LS_ERROR, LS_ERROR_INVALID);
			g_assert_nonnull(strstr(error->message, paths[(i - 1) / 3]));
		}
	}
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
		"{\"files\":[],\"delete\":null}",
		"{\"files\":[],\"delete\":[1]}",
		"{\"files\":[],\"delete\":[\"../escape\"]}",
		"{\"files\":[],\"delete\":[\"missing\"]}",
		"{\"files\":[],\"delete\":[\"index.html\",\"index.html\"]}",
		"{\"files\":[{\"path\":\"index.html\",\"content\":\"x\"}],\"delete\":[\"index.html\"]}",
		"{\"files\":[{\"path\":\"../bad\",\"content\":\"x\"}],\"delete\":[\"index.html\"]}",
		"{\"files\":[{\"path\":\"index.html\",\"content\":\"x\",\"executable\":1}]}",
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
	const gchar *imports[] = {"success", "empty", "symlink", "fifo", "binary", "utf8", "cancel"};
	const gchar *revisions[] = {"resume", "changed", "mode-changed", "brief-changed", "nested"};
	guint i;
	g_test_init(&argc, &argv, NULL);
	for (i = 0; i < G_N_ELEMENTS(imports); i++) {
		g_autofree gchar *name = g_strdup_printf("/revision/import/%s", imports[i]);
		g_test_add_data_func(name, imports[i], import_case);
	}
	for (i = 0; i < G_N_ELEMENTS(revisions); i++) {
		g_autofree gchar *name = g_strdup_printf("/revision/pipeline/%s", revisions[i]);
		g_test_add_data_func(name, revisions[i], revision_case);
	}
	g_test_add_func("/artifact/delete", artifact_delete);
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
	g_test_add_data_func("/step/acceptance-retry", GINT_TO_POINTER(FALSE), acceptance_retry);
	g_test_add_data_func("/step/acceptance-exhaustion", GINT_TO_POINTER(TRUE), acceptance_retry);
	g_test_add_func("/prompt/limit", prompt_limit);
	g_test_add_func("/pipeline/registry", registry);
	g_test_add_func("/json/limits", json_limits);
	g_test_add_func("/prompt/catalog", catalog_override);
	g_test_add_func("/prompt/application-contracts", application_generation_contracts);
	g_test_add_func("/prompt/stack-profiles", stack_profiles);
	g_test_add_func("/prompt/stack-catalogs", stack_catalogs);
	g_test_add_func("/prompt/application-stack-artifacts", application_stack_artifacts);
	g_test_add_data_func("/seo/landing", "recipes/landing.yaml", seo_artifacts);
	g_test_add_data_func("/seo/checked-landing", "recipes/checked-landing.yaml", seo_artifacts);
	g_test_add_data_func("/seo/flagship", "recipes/flagship.yaml", seo_artifacts);
	g_test_add_data_func("/seo/mixed-providers", "recipes/mixed-providers.yaml", seo_artifacts);
	g_test_add_data_func("/seo/application", "recipes/application.yaml", seo_artifacts);
	g_test_add_data_func("/preflight/style", "style", preflight_case);
	g_test_add_data_func("/preflight/prompt", "prompt", preflight_case);
	g_test_add_data_func("/preflight/type", "type", preflight_case);
	g_test_add_data_func("/preflight/options", "options", preflight_case);
	return g_test_run();
}
