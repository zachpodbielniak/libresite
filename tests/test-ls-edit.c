/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "libresite.h"
#include <glib/gstdio.h>
#include <string.h>

#define FIXTURE "tests/fixtures/composed/"

/* Answers the composed fixture by schema; the edit answer is scripted. */
typedef struct {
	GObject parent_instance;
	gint calls;
	gint edit_calls;
	GPtrArray *answers;	/* edit answers, used in order; the last repeats */
	gchar *last_prompt;
} FakeProvider;
typedef struct { GObjectClass parent_class; } FakeProviderClass;
static void fake_iface_init(LsProviderInterface *iface);
GType fake_provider_get_type(void);
G_DEFINE_TYPE_WITH_CODE(FakeProvider, fake_provider, G_TYPE_OBJECT,
	G_IMPLEMENT_INTERFACE(LS_TYPE_PROVIDER, fake_iface_init))

static gchar *
fixture(const gchar *name){
	g_autofree gchar *path = g_strconcat(FIXTURE, name, NULL);
	gchar *text = NULL;
	g_assert_true(g_file_get_contents(path, &text, NULL, NULL));
	return text;
}

static gchar *
fake_generate(LsProvider *provider, JsonObject *settings, const gchar *system, const gchar *prompt,
	GCancellable *cancellable, GError **error){
	FakeProvider *self = (FakeProvider *)provider;
	const gchar *schema = ls_string(settings, "json_schema");
	const gchar *task = strstr(prompt, "TASK (JSON)");
	(void)system;
	(void)cancellable;
	(void)error;
	self->calls++;
	g_assert_nonnull(schema);
	if (strstr(schema, "\"summary\"") != NULL) {
		guint index = MIN((guint)self->edit_calls, self->answers->len - 1);
		self->edit_calls++;
		g_free(self->last_prompt);
		self->last_prompt = g_strdup(prompt);
		return g_strdup(g_ptr_array_index(self->answers, index));
	}
	if (strstr(schema, "\"tokens\"") != NULL)
		return fixture("plan.json");
	if (strstr(schema, "\"required\": [\"css\"]") != NULL)
		return g_strdup("{\"css\": \".em { color: var(--accent-text); }\"}");
	if (strstr(schema, "\"svg\"") != NULL) {
		g_autofree gchar *svg = fixture("mark.svg");
		g_autoptr(JsonNode) node = json_node_new(JSON_NODE_OBJECT);
		JsonObject *object = json_object_new();
		json_object_set_string_member(object, "svg", svg);
		json_node_take_object(node, object);
		return ls_json_encode(node);
	}
	{
		const gchar *routes[] = {"index", "work", "start", "journal", "visit"};
		guint i;
		for (i = 0; i < G_N_ELEMENTS(routes); i++) {
			g_autofree gchar *needle = g_strdup_printf("\"route\":\"%s.html\"", routes[i]);
			g_autofree gchar *file = g_strconcat(routes[i], ".json", NULL);
			if (task != NULL && strstr(task, needle) != NULL)
				return fixture(file);
		}
	}
	g_assert_not_reached();
	return NULL;
}
static void
fake_iface_init(LsProviderInterface *iface){
	iface->generate = fake_generate;
}
static void
fake_provider_finalize(GObject *object){
	FakeProvider *self = (FakeProvider *)object;
	g_ptr_array_unref(self->answers);
	g_free(self->last_prompt);
	G_OBJECT_CLASS(fake_provider_parent_class)->finalize(object);
}
static void
fake_provider_class_init(FakeProviderClass *klass){
	G_OBJECT_CLASS(klass)->finalize = fake_provider_finalize;
}
static void
fake_provider_init(FakeProvider *self){
	self->answers = g_ptr_array_new_with_free_func(g_free);
}

static const gchar *recipe_text =
	"version: 1\n"
	"name: harbor\n"
	"site: {brief: An illustrative furniture repair workshop, style: editorial}\n"
	"ai: {provider: claude-code, retries: 1}\n"
	"pipeline:\n"
	"  steps:\n"
	"    - {id: plan, type: plan}\n"
	"    - {id: pages, type: pages, options: {parallel: 4, base_url: 'https://harbor.example/'}}\n"
	"    - {id: specs, type: prune, options: {prefixes: [site/], archive: true}}\n";

typedef struct {
	gchar *temp;
	gchar *site;
	FakeProvider *provider;
	LsPrompts *prompts;
	JsonObject *settings;
	GString *progress;
} Site;

static void
on_progress(LsEditor *editor, const gchar *message, gpointer data){
	(void)editor;
	g_string_append_printf((GString *)data, "%s\n", message);
}

/* A published site from the fixture, ready to edit. */
static void
site_setup(Site *site){
	g_autoptr(GError) error = NULL;
	g_autoptr(LsRecipe) recipe = ls_recipe_new(recipe_text, &error);
	g_autoptr(LsPipeline) pipeline = NULL;
	g_assert_no_error(error);
	site->temp = g_dir_make_tmp("libresite-edit-XXXXXX", NULL);
	site->site = g_build_filename(site->temp, "site", NULL);
	site->provider = g_object_new(fake_provider_get_type(), NULL);
	site->prompts = ls_prompts_new(NULL, &error);
	g_assert_no_error(error);
	pipeline = ls_pipeline_new(LS_PROVIDER(site->provider), site->prompts);
	g_assert_true(ls_pipeline_run(pipeline, recipe, site->site, FALSE, NULL, &error));
	g_assert_no_error(error);
	site->settings = json_object_new();
	json_object_set_string_member(site->settings, "provider", "claude-code");
	json_object_set_int_member(site->settings, "retries", 1);
	json_object_set_int_member(site->settings, "timeout", 0);
	site->progress = g_string_new(NULL);
	site->provider->calls = 0;
}

static void
remove_all(const gchar *path){
	gchar *argv[] = {(gchar *)"rm", (gchar *)"-rf", (gchar *)path, NULL};
	g_assert_true(g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL, NULL, NULL));
}

static void
site_teardown(Site *site){
	remove_all(site->temp);
	g_free(site->temp);
	g_free(site->site);
	g_object_unref(site->provider);
	g_object_unref(site->prompts);
	json_object_unref(site->settings);
	g_string_free(site->progress, TRUE);
}

static gchar *
git(const gchar *directory, const gchar *a, const gchar *b, const gchar *c){
	gchar *argv[] = {(gchar *)"git", (gchar *)"-C", (gchar *)directory, (gchar *)a, (gchar *)b, (gchar *)c, NULL};
	gchar *out = NULL;
	gint status = 0;
	g_assert_true(g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL,
		&out, NULL, &status, NULL));
	g_assert_true(g_spawn_check_wait_status(status, NULL));
	return g_strstrip(out);
}

static gchar *
read_file(const gchar *directory, const gchar *name){
	g_autofree gchar *path = g_build_filename(directory, name, NULL);
	gchar *text = NULL;
	g_assert_true(g_file_get_contents(path, &text, NULL, NULL));
	return text;
}

static gchar *
work_with_heading(const gchar *heading){
	g_autofree gchar *work = fixture("work.json");
	g_auto(GStrv) parts = g_strsplit(work, "\"Recent work\"", 2);
	g_autofree gchar *quoted = g_strdup_printf("\"%s\"", heading);
	return g_strjoin(quoted, parts[0], parts[1], NULL);
}

static gboolean
apply(Site *site, const gchar *request, gchar **summary, gchar **commit, GError **error){
	g_autoptr(LsEditor) editor = ls_editor_new(LS_PROVIDER(site->provider), site->prompts, site->settings);
	g_signal_connect(editor, "progress", G_CALLBACK(on_progress), site->progress);
	return ls_editor_apply(editor, site->site, request, summary, commit, NULL, error);
}

/* "Change this thing": one page changes, the site is recomposed, the record
 * keeps the new specification and a new version lands on the site's history. */
static void
test_edit_page(void){
	Site site;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *summary = NULL, *commit = NULL, *work = NULL, *answer = NULL, *page = NULL;
	g_autofree gchar *count = NULL, *subject = NULL, *head = NULL, *record = NULL, *index = NULL;
	g_autofree gchar *before = NULL, *status = NULL;
	site_setup(&site);
	before = read_file(site.site, "index.html");
	work = work_with_heading("Recent repairs");
	answer = g_strdup_printf("{\"summary\": \"Rename the work page heading.\", \"pages\": [%s]}", work);
	g_ptr_array_add(site.provider->answers, g_steal_pointer(&answer));
	{
		gboolean okay = apply(&site, "Call the work page Recent repairs", &summary, &commit, &error);
		g_assert_no_error(error);
		g_assert_true(okay);
	}
	g_assert_cmpstr(summary, ==, "Rename the work page heading");
	g_assert_cmpint(site.provider->calls, ==, 1);
	/* The model saw the request, the plan and every page, not the structure genes. */
	g_assert_nonnull(strstr(site.provider->last_prompt, "Call the work page Recent repairs"));
	g_assert_nonnull(strstr(site.provider->last_prompt, "\"journal.html\""));
	g_assert_null(strstr(site.provider->last_prompt, "\"structure\""));
	page = read_file(site.site, "work.html");
	g_assert_nonnull(strstr(page, "Recent repairs"));
	g_assert_null(strstr(page, ">Recent work</h1>"));
	/* Untouched pages render as before. */
	index = read_file(site.site, "index.html");
	g_assert_cmpstr(index, ==, before);
	count = git(site.site, "rev-list", "--count", "HEAD");
	g_assert_cmpstr(count, ==, "2");
	subject = git(site.site, "log", "-1", "--format=%s");
	g_assert_cmpstr(subject, ==, "feat: Rename the work page heading");
	head = git(site.site, "rev-parse", "HEAD", NULL);
	g_assert_cmpstr(head, ==, commit);
	status = git(site.site, "status", "--porcelain", NULL);
	g_assert_cmpstr(status, ==, "");
	record = read_file(site.site, ".libresite/run.json");
	g_assert_nonnull(strstr(record, "Recent repairs"));
	g_assert_nonnull(strstr(site.progress->str, "committed"));
	site_teardown(&site);
}

/* A plan change (one colour) recomposes the styles; a second edit builds on the first. */
static void
test_edit_plan_twice(void){
	Site site;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *styles = NULL, *count = NULL, *work = NULL, *second = NULL, *page = NULL;
	g_autoptr(JsonNode) plan = NULL;
	g_autoptr(JsonNode) tokens = NULL;
	site_setup(&site);
	{
		g_autofree gchar *plan_text = fixture("plan.json");
		plan = ls_json_parse(plan_text, NULL);
	}
	tokens = json_node_copy(json_object_get_member(json_node_get_object(plan), "tokens"));
	json_object_set_string_member(json_node_get_object(tokens), "accent", "#0b5d8a");
	{
		g_autofree gchar *tokens_text = ls_json_encode(tokens);
		g_ptr_array_add(site.provider->answers,
			g_strdup_printf("{\"summary\": \"Make the accent a deep blue\", \"plan\": {\"tokens\": %s}, \"pages\": []}",
				tokens_text));
	}
	g_assert_true(apply(&site, "Make the accent blue", NULL, NULL, &error));
	g_assert_no_error(error);
	styles = read_file(site.site, "styles.css");
	g_assert_nonnull(strstr(styles, "--accent: #0b5d8a;"));
	work = work_with_heading("Our repairs");
	second = g_strdup_printf("{\"summary\": \"Retitle work\", \"pages\": [%s]}", work);
	g_ptr_array_set_size(site.provider->answers, 0);
	g_ptr_array_add(site.provider->answers, g_steal_pointer(&second));
	g_assert_true(apply(&site, "Retitle the work page", NULL, NULL, &error));
	g_assert_no_error(error);
	page = read_file(site.site, "work.html");
	g_assert_nonnull(strstr(page, "Our repairs"));
	g_clear_pointer(&styles, g_free);
	styles = read_file(site.site, "styles.css");
	g_assert_nonnull(strstr(styles, "--accent: #0b5d8a;"));
	count = git(site.site, "rev-list", "--count", "HEAD");
	g_assert_cmpstr(count, ==, "3");
	site_teardown(&site);
}

/* Rejected answers go back once with the reason; when none passes, nothing
 * on disk or in the history changes. */
static void
test_edit_rejected(void){
	Site site;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *before = NULL, *after = NULL, *record_before = NULL, *record_after = NULL;
	g_autofree gchar *count = NULL, *status = NULL;
	site_setup(&site);
	before = read_file(site.site, "work.html");
	record_before = read_file(site.site, ".libresite/run.json");
	g_ptr_array_add(site.provider->answers, g_strdup("{\"summary\": \"x\", \"plan\": {\"structure\": {}}, \"pages\": []}"));
	g_ptr_array_add(site.provider->answers, g_strdup("{\"summary\": \"y\", \"pages\": [{\"path\": \"work.html\", "
		"\"sections\": [{\"component\": \"banner\", \"content\": {}}]}]}"));
	g_assert_false(apply(&site, "Break it", NULL, NULL, &error));
	g_assert_error(error, LS_ERROR, LS_ERROR_VALIDATION);
	g_assert_cmpint(site.provider->edit_calls, ==, 2);
	/* The retry carried the first rejection. */
	g_assert_nonnull(strstr(site.provider->last_prompt, "structure"));
	g_assert_nonnull(strstr(site.provider->last_prompt, "builder"));
	after = read_file(site.site, "work.html");
	g_assert_cmpstr(after, ==, before);
	record_after = read_file(site.site, ".libresite/run.json");
	g_assert_cmpstr(record_after, ==, record_before);
	count = git(site.site, "rev-list", "--count", "HEAD");
	g_assert_cmpstr(count, ==, "1");
	status = git(site.site, "status", "--porcelain", NULL);
	g_assert_cmpstr(status, ==, "");
	site_teardown(&site);
}

/* Uncommitted changes, or committed changes the record does not know about,
 * stop the edit before any model call. */
static void
test_edit_refuses_drift(void){
	Site site;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *stray = NULL, *page = NULL, *edited = NULL, *index = NULL;
	site_setup(&site);
	g_ptr_array_add(site.provider->answers, g_strdup("{\"summary\": \"x\", \"pages\": []}"));
	stray = g_build_filename(site.site, "notes.txt", NULL);
	g_assert_true(g_file_set_contents(stray, "mine", -1, NULL));
	g_assert_false(apply(&site, "Anything", NULL, NULL, &error));
	g_assert_error(error, LS_ERROR, LS_ERROR_CONFLICT);
	g_assert_nonnull(strstr(error->message, "uncommitted"));
	g_assert_cmpint(site.provider->calls, ==, 0);
	g_clear_error(&error);
	g_assert_cmpint(g_unlink(stray), ==, 0);
	/* A hand edit committed outside the builder: the outlines no longer match. */
	page = g_build_filename(site.site, "index.html", NULL);
	index = read_file(site.site, "index.html");
	edited = g_strconcat(index, "<!-- hand edit -->\n", NULL);
	g_assert_true(g_file_set_contents(page, edited, -1, NULL));
	g_free(git(site.site, "add", "index.html", NULL));
	{
		gchar *argv[] = {(gchar *)"git", (gchar *)"-C", site.site, (gchar *)"-c", (gchar *)"user.name=T",
			(gchar *)"-c", (gchar *)"user.email=t@t", (gchar *)"commit", (gchar *)"-qm", (gchar *)"hand", NULL};
		gint status = 0;
		g_assert_true(g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL, &status, NULL));
		g_assert_true(g_spawn_check_wait_status(status, NULL));
	}
	g_assert_false(apply(&site, "Anything", NULL, NULL, &error));
	g_assert_error(error, LS_ERROR, LS_ERROR_CONFLICT);
	g_assert_nonnull(strstr(error->message, "index.html"));
	g_assert_cmpint(site.provider->calls, ==, 0);
	site_teardown(&site);
}

/* A directory that is not a composed site with a record is refused plainly. */
static void
test_edit_not_a_site(void){
	g_autoptr(GError) error = NULL;
	g_autofree gchar *temp = g_dir_make_tmp("libresite-edit-XXXXXX", NULL);
	g_autoptr(LsPrompts) prompts = ls_prompts_new(NULL, NULL);
	g_autoptr(GObject) provider = g_object_new(fake_provider_get_type(), NULL);
	g_autoptr(JsonObject) settings = json_object_new();
	g_autoptr(LsEditor) editor = ls_editor_new(LS_PROVIDER(provider), prompts, settings);
	g_assert_false(ls_editor_apply(editor, temp, "Anything", NULL, NULL, NULL, &error));
	g_assert_error(error, LS_ERROR, LS_ERROR_INVALID);
	g_assert_cmpint(((FakeProvider *)provider)->calls, ==, 0);
	g_clear_error(&error);
	g_assert_false(ls_editor_apply(editor, temp, "   ", NULL, NULL, NULL, &error));
	g_assert_error(error, LS_ERROR, LS_ERROR_INVALID);
	remove_all(temp);
}

int
main(int argc, char **argv){
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/edit/page", test_edit_page);
	g_test_add_func("/edit/plan-twice", test_edit_plan_twice);
	g_test_add_func("/edit/rejected", test_edit_rejected);
	g_test_add_func("/edit/refuses-drift", test_edit_refuses_drift);
	g_test_add_func("/edit/not-a-site", test_edit_not_a_site);
	return g_test_run();
}
