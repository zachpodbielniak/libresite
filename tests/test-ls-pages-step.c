/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "libresite.h"
#include <string.h>
#include "test-image-fixtures.h"

#define FIXTURE "tests/fixtures/composed/"

/* A thread-safe scripted transport. It answers by response schema: plans,
 * page specifications or artwork, and records concurrency and prompts. */
typedef struct {
	GObject parent_instance;
	GMutex lock;
	gint in_flight;
	gint max_in_flight;
	gint calls;
	GHashTable *prompts;		/* route or art path -> GPtrArray of prompts */
	GHashTable *failures;		/* route -> remaining bad answers */
	GHashTable *threads;		/* GThread* seen */
	gboolean bad_plan;		/* first plan answer fails contrast */
	gboolean drop_note;		/* first start.html answer lacks its literal */
	gboolean default_plan;	/* first plan answer uses the house default colors */
	gint bad_signature;		/* invalid signature answers to give first */
	gboolean block;			/* wait for cancellation */
	const gchar *photo;		/* a supplied photo swapped in for the bench drawing on index and journal */
	GCancellable *started;		/* cancelled when a call begins */
	gchar *last_schema;
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

/* The key after "route" or "path" in the TASK data identifies the job. */
static gchar *
job_key(const gchar *prompt, const gchar *field){
	g_autofree gchar *needle = g_strdup_printf("\"%s\":\"", field);
	const gchar *task = strstr(prompt, "TASK (JSON)");
	const gchar *start = task != NULL ? strstr(task, needle) : NULL;
	const gchar *end;
	if (start == NULL)
		return NULL;
	start += strlen(needle);
	end = strchr(start, '"');
	return end != NULL ? g_strndup(start, (gsize)(end - start)) : NULL;
}

static gchar *
fake_generate(LsProvider *provider, JsonObject *settings, const gchar *system,
	const gchar *prompt, GCancellable *cancellable, GError **error){
	FakeProvider *self = (FakeProvider *)provider;
	const gchar *schema = ls_string(settings, "json_schema");
	g_autofree gchar *key = NULL;
	gchar *answer = NULL;
	GPtrArray *seen;
	(void)system;
	(void)error;
	g_mutex_lock(&self->lock);
	self->calls++;
	self->in_flight++;
	self->max_in_flight = MAX(self->max_in_flight, self->in_flight);
	g_hash_table_add(self->threads, g_thread_self());
	g_free(self->last_schema);
	self->last_schema = g_strdup(schema);
	g_mutex_unlock(&self->lock);
	if (self->started != NULL)
		g_cancellable_cancel(self->started);
	while (self->block && !g_cancellable_is_cancelled(cancellable))
		g_usleep(1000);
	g_usleep(40000);
	if (schema != NULL && strstr(schema, "\"nav\"") != NULL) {
		key = g_strdup("plan");
		answer = fixture("plan.json");
		g_mutex_lock(&self->lock);
		if (self->bad_plan) {
			gchar *swap = strstr(answer, "#57524a");
			gchar *home = strstr(answer, "\"path\": \"index.html\"");
			/* Contrast is repaired natively; a missing home page is not. */
			memcpy(swap, "#c8c0b0", 7);
			memcpy(home, "\"path\": \"indey.html\"", 21);
			self->bad_plan = FALSE;
		}
		if (self->default_plan) {
			/* Warm cream paper with a rust accent, still at AA contrast. */
			memcpy(strstr(answer, "#1f5c4a"), "#a8431c", 7);
			self->default_plan = FALSE;
		}
		g_mutex_unlock(&self->lock);
	} else if (schema != NULL && strstr(schema, "\"required\": [\"css\"]") != NULL) {
		/* The signature layer: a small divider drawn from a data image. */
		key = g_strdup("signature");
		g_mutex_lock(&self->lock);
		if (self->bad_signature > 0) {
			self->bad_signature--;
			answer = g_strdup("{\"css\":\".em { color: #ff0000; }\"}");
		} else
			answer = g_strdup("{\"css\":\"main > .block + .block::before { content: \\\"\\\"; display: block; width: 3rem; "
				"height: 0.6rem; margin-inline: auto; background-color: var(--accent); -webkit-mask: url(\\\"data:image/svg+xml,"
				"%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 30 6'%3E%3Cpath d='M0 3h30'/%3E%3C/svg%3E\\\"); } "
				".em { color: var(--accent-text); }\"}");
		g_mutex_unlock(&self->lock);
	} else if (schema != NULL && strstr(schema, "\"svg\"") != NULL) {
		g_autofree gchar *svg = fixture("mark.svg");
		g_autoptr(JsonNode) node = json_node_new(JSON_NODE_OBJECT);
		g_autoptr(JsonObject) object = json_object_new();
		key = job_key(prompt, "path");
		json_object_set_string_member(object, "svg", svg);
		json_node_set_object(node, object);
		answer = ls_json_encode(node);
	} else {
		gint remaining;
		key = job_key(prompt, "route");
		g_assert_nonnull(key);
		g_mutex_lock(&self->lock);
		remaining = GPOINTER_TO_INT(g_hash_table_lookup(self->failures, key));
		if (remaining > 0)
			g_hash_table_insert(self->failures, g_strdup(key), GINT_TO_POINTER(remaining - 1));
		g_mutex_unlock(&self->lock);
		if (remaining > 0)
			answer = g_strdup_printf("{\"path\":\"%s\",\"sections\":[{\"component\":\"banner\",\"content\":{}}]}", key);
		else {
			g_autofree gchar *name = g_strndup(key, strlen(key) - 5);
			g_autofree gchar *file = g_strconcat(name, ".json", NULL);
			answer = fixture(file);
			g_mutex_lock(&self->lock);
			if (self->drop_note && g_str_equal(key, "start.html")) {
				gchar *note = strstr(answer, "Nothing is sent.");
				memcpy(note, "Nothing leaves.!", 16);
				self->drop_note = FALSE;
			}
			if (self->photo != NULL && (g_str_equal(key, "index.html") || g_str_equal(key, "journal.html"))) {
				gchar **parts = g_strsplit(answer, "assets/bench.svg", -1);
				g_free(answer);
				answer = g_strjoinv(self->photo, parts);
				g_strfreev(parts);
			}
			g_mutex_unlock(&self->lock);
		}
	}
	g_mutex_lock(&self->lock);
	seen = g_hash_table_lookup(self->prompts, key != NULL ? key : "?");
	if (seen == NULL) {
		seen = g_ptr_array_new_with_free_func(g_free);
		g_hash_table_insert(self->prompts, g_strdup(key != NULL ? key : "?"), seen);
	}
	g_ptr_array_add(seen, g_strdup(prompt));
	self->in_flight--;
	g_mutex_unlock(&self->lock);
	return answer;
}

static void
fake_finalize(GObject *object){
	FakeProvider *self = (FakeProvider *)object;
	g_hash_table_unref(self->prompts);
	g_hash_table_unref(self->failures);
	g_hash_table_unref(self->threads);
	g_free(self->last_schema);
	g_mutex_clear(&self->lock);
	G_OBJECT_CLASS(fake_provider_parent_class)->finalize(object);
}
static void
fake_iface_init(LsProviderInterface *iface){
	iface->generate = fake_generate;
}
static void
fake_provider_class_init(FakeProviderClass *klass){
	G_OBJECT_CLASS(klass)->finalize = fake_finalize;
}
static void
fake_provider_init(FakeProvider *self){
	g_mutex_init(&self->lock);
	self->prompts = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify)g_ptr_array_unref);
	self->failures = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	self->threads = g_hash_table_new(g_direct_hash, g_direct_equal);
}

static GPtrArray *
prompts_for(FakeProvider *self, const gchar *key){
	return g_hash_table_lookup(self->prompts, key);
}

typedef struct {
	LsRecipe *recipe;
	LsPrompts *prompts;
	LsContext *context;
	FakeProvider *provider;
	GThread *main;
	gint off_thread;
	gint progress;
	GString *messages;	/* every progress message, one per line */
} Fixture;

static const gchar *recipe_text =
	"version: 1\n"
	"name: harbor\n"
	"site: {brief: An illustrative furniture repair workshop, style: editorial}\n"
	"ai: {provider: claude-code, retries: RETRIES}\n"
	"pipeline:\n"
	"  steps:\n"
	"    - {id: plan, type: plan, instructions: Plan three pages.}\n"
	"    - {id: pages, type: pages, options: {parallel: PARALLEL}}\n"
	"    - id: acceptance\n"
	"      type: validate\n"
	"      options:\n"
	"        required: [index.html, work.html, start.html, journal.html, visit.html, styles.css, site.js]\n"
	"        checks:\n"
	"          - {path: start.html, contains: ['Nothing is sent. Your request']}\n";

static void
on_progress(LsStep *step, const gchar *message, gpointer data){
	Fixture *fixture = data;
	(void)step;
	fixture->progress++;
	g_string_append_printf(fixture->messages, "%s\n", message);
	if (g_thread_self() != fixture->main)
		fixture->off_thread++;
}

static void
on_usage(LsProvider *provider, const gchar *name, const gchar *model, gint64 in, gint64 out, gpointer data){
	Fixture *fixture = data;
	(void)provider;
	(void)name;
	(void)model;
	(void)in;
	(void)out;
	if (g_thread_self() != fixture->main)
		fixture->off_thread++;
}

static void
fixture_setup(Fixture *fixture, gint parallel, gint retries){
	g_autoptr(GError) error = NULL;
	g_autofree gchar *text = g_strdup(recipe_text);
	g_autofree gchar *parallel_text = g_strdup_printf("%d", parallel);
	g_autofree gchar *retries_text = g_strdup_printf("%d", retries);
	gchar **parts;
	parts = g_strsplit(text, "PARALLEL", -1);
	g_free(text);
	text = g_strjoinv(parallel_text, parts);
	g_strfreev(parts);
	parts = g_strsplit(text, "RETRIES", -1);
	g_free(text);
	text = g_strjoinv(retries_text, parts);
	g_strfreev(parts);
	fixture->recipe = ls_recipe_new(text, &error);
	g_assert_no_error(error);
	fixture->prompts = ls_prompts_new(NULL, &error);
	g_assert_no_error(error);
	fixture->context = ls_context_new();
	fixture->provider = g_object_new(fake_provider_get_type(), NULL);
	fixture->main = g_thread_self();
	fixture->off_thread = 0;
	fixture->progress = 0;
	fixture->messages = g_string_new(NULL);
	g_signal_connect(fixture->provider, "usage", G_CALLBACK(on_usage), fixture);
}

static void
fixture_teardown(Fixture *fixture){
	g_clear_object(&fixture->recipe);
	g_clear_object(&fixture->prompts);
	g_clear_object(&fixture->context);
	g_clear_object(&fixture->provider);
	g_string_free(fixture->messages, TRUE);
}

static JsonObject *
step_config(Fixture *fixture, guint index){
	return json_array_get_object_element(ls_recipe_get_steps(fixture->recipe), index);
}

static gboolean
run_step(Fixture *fixture, GType type, guint index, GCancellable *cancellable, GError **error){
	g_autoptr(LsStep) step = g_object_new(type, NULL);
	gboolean okay;
	g_signal_connect(step, "progress", G_CALLBACK(on_progress), fixture);
	okay = ls_step_execute(step, fixture->recipe, step_config(fixture, index), fixture->prompts,
		fixture->context, LS_PROVIDER(fixture->provider), cancellable, error);
	return okay;
}

static void
run_plan(Fixture *fixture){
	g_autoptr(GError) error = NULL;
	g_assert_true(run_step(fixture, LS_TYPE_PLAN_STEP, 0, NULL, &error));
	g_assert_no_error(error);
}

static const gchar *
artifact_text(LsContext *context, const gchar *path){
	const LsArtifact *artifact = ls_context_lookup(context, path);
	return artifact != NULL ? ls_artifact_get_content(artifact) : NULL;
}

/* The plan step asks for the plan schema and stores a checked plan. */
static void
test_plan(void){
	Fixture fixture;
	GPtrArray *prompts;
	fixture_setup(&fixture, 4, 1);
	fixture.provider->bad_plan = TRUE;
	run_plan(&fixture);
	g_assert_nonnull(strstr(fixture.provider->last_schema, "\"tokens\""));
	g_assert_nonnull(artifact_text(fixture.context, "site/plan.json"));
	prompts = prompts_for(fixture.provider, "plan");
	g_assert_cmpuint(prompts->len, ==, 2);
	g_assert_nonnull(strstr(g_ptr_array_index(prompts, 0), "COMPONENTS"));
	g_assert_nonnull(strstr(g_ptr_array_index(prompts, 0), "Plan three pages."));
	g_assert_nonnull(strstr(g_ptr_array_index(prompts, 0), "\"suggested_opening\":\""));
	g_assert_nonnull(strstr(g_ptr_array_index(prompts, 0), "\"suggested_look\":{"));
	g_assert_nonnull(strstr(g_ptr_array_index(prompts, 0), "\"suggested_palette\":{"));
	g_assert_nonnull(strstr(g_ptr_array_index(prompts, 0), "\"suggested_type\":{"));
	g_assert_nonnull(strstr(g_ptr_array_index(prompts, 0), "\"suggested_home\":{"));
	g_assert_nonnull(strstr(g_ptr_array_index(prompts, 0), "Nothing is sent."));
	g_assert_nonnull(strstr(g_ptr_array_index(prompts, 1), "PREVIOUS ATTEMPT REJECTED"));
	g_assert_nonnull(strstr(g_ptr_array_index(prompts, 1), "index.html"));
	g_assert_null(strstr(strstr(g_ptr_array_index(prompts, 1), "PREVIOUS ATTEMPT REJECTED"), "contrast"));
	g_assert_cmpint(fixture.off_thread, ==, 0);
	fixture_teardown(&fixture);
}

/* Pages and art are written at once; the site is composed natively. */
static void
test_parallel(void){
	Fixture fixture;
	g_autoptr(GError) error = NULL;
	const gchar *paths[] = {"index.html", "work.html", "start.html", "404.html", "privacy.html",
		"terms.html", "styles.css", "site.js", "robots.txt", "assets/mark.svg", "assets/bench.svg",
		"site/pages/index.json", "site/pages/work.json", "site/pages/start.json", "site/signature.css"};
	guint i;
	fixture_setup(&fixture, 8, 1);
	run_plan(&fixture);
	{
		gboolean okay = run_step(&fixture, LS_TYPE_PAGES_STEP, 1, NULL, &error);
		g_assert_no_error(error);
		g_assert_true(okay);
	}
	for (i = 0; i < G_N_ELEMENTS(paths); i++)
		if (ls_context_lookup(fixture.context, paths[i]) == NULL)
			g_error("missing %s", paths[i]);
	/* One call more than pages and artwork alone: the signature layer. */
	g_assert_cmpint(fixture.provider->calls, ==, 9);
	g_assert_cmpint(fixture.provider->max_in_flight, >=, 3);
	/* The signature layer comes last in the styles and is kept for rebuilds. */
	g_assert_nonnull(strstr(artifact_text(fixture.context, "styles.css"), "/* Site signature */\n@layer ls-signature {\nmain > .block + .block::before"));
	g_assert_nonnull(strstr(artifact_text(fixture.context, "site/signature.css"), "var(--accent)"));
	{
		const gchar *prompt = g_ptr_array_index(prompts_for(fixture.provider, "signature"), 0);
		g_assert_nonnull(strstr(prompt, "\"genes\":{\"curve\":"));
		g_assert_nonnull(strstr(prompt, "motif"));
		g_assert_null(strstr(prompt, "\"pages\""));
	}
	g_assert_cmpuint(g_hash_table_size(fixture.provider->threads), >=, 3);
	g_assert_cmpint(fixture.off_thread, ==, 0);
	g_assert_cmpint(fixture.progress, >=, 5);
	g_assert_nonnull(strstr(artifact_text(fixture.context, "start.html"), "Nothing is sent."));
	/* Each writer sees its own brief, the page's required literals and the catalog. */
	g_assert_nonnull(strstr(g_ptr_array_index(prompts_for(fixture.provider, "start.html"), 0), "Nothing is sent."));
	g_assert_nonnull(strstr(g_ptr_array_index(prompts_for(fixture.provider, "work.html"), 0), "COMPONENTS"));
	/* Every artwork is drawn in the site's art style. */
	g_assert_nonnull(strstr(g_ptr_array_index(prompts_for(fixture.provider, "assets/bench.svg"), 0),
		"\"style\":\"Bold carved marks"));
	g_assert_null(strstr(g_ptr_array_index(prompts_for(fixture.provider, "work.html"), 0), "Local form"));
	fixture_teardown(&fixture);
}

/* A signature that breaks the rules is sent back once with the reason; one
 * that never passes is left out and the site still builds, plainly. */
static void
test_signature_fallback(void){
	gint bad[] = {1, 5};
	guint v;
	for (v = 0; v < G_N_ELEMENTS(bad); v++) {
		Fixture fixture;
		g_autoptr(GError) error = NULL;
		gboolean okay;
		fixture_setup(&fixture, 8, 1);
		run_plan(&fixture);
		fixture.provider->bad_signature = bad[v];
		okay = run_step(&fixture, LS_TYPE_PAGES_STEP, 1, NULL, &error);
		g_assert_no_error(error);
		g_assert_true(okay);
		if (v == 0) {
			GPtrArray *prompts = prompts_for(fixture.provider, "signature");
			g_assert_cmpuint(prompts->len, ==, 2);
			g_assert_nonnull(strstr(g_ptr_array_index(prompts, 1), "colours must be var(--paper)"));
			g_assert_nonnull(ls_context_lookup(fixture.context, "site/signature.css"));
		} else {
			g_assert_null(ls_context_lookup(fixture.context, "site/signature.css"));
			g_assert_null(strstr(artifact_text(fixture.context, "styles.css"), "Site signature"));
			g_assert_nonnull(ls_context_lookup(fixture.context, "index.html"));
		}
		fixture_teardown(&fixture);
	}
}

/* The plan reports the facts to ask the owner; the pages step writes the
 * site's fingerprint; an order that lists it as avoid is dealt another
 * layout and cannot reuse the same signature layer. */
static void
test_sameness_run(void){
	Fixture first, second;
	g_autoptr(GError) error = NULL;
	g_autoptr(JsonNode) fingerprint = NULL, plan_a = NULL, plan_b = NULL;
	g_autofree gchar *fp_text = NULL, *avoid_recipe = NULL, *text = NULL;
	JsonObject *fp;
	gchar **parts;
	fixture_setup(&first, 8, 1);
	run_plan(&first);
	g_assert_nonnull(strstr(first.messages->str, "facts to ask the owner: What are your opening hours?"));
	g_assert_null(strstr(first.messages->str, "dealt a layout"));
	g_assert_true(run_step(&first, LS_TYPE_PAGES_STEP, 1, NULL, &error));
	g_assert_no_error(error);
	fingerprint = ls_json_parse(artifact_text(first.context, "site/fingerprint.json"), &error);
	g_assert_no_error(error);
	fp = json_node_get_object(fingerprint);
	g_assert_cmpint(json_object_get_int_member(fp, "v"), ==, 1);
	g_assert_cmpuint(json_array_get_length(json_object_get_array_member(fp, "signature")), >, 0);
	g_assert_cmpstr(json_array_get_string_element(json_object_get_array_member(fp, "home"), 0), ==, "hero/split");
	/* The same order, now asked to avoid the first site. The fake answers a
	 * fixed plan whatever it is dealt, so compare only what the builder dealt:
	 * the layout and genes, not the fixture's own colours, type and home. */
	json_object_remove_member(fp, "colors");
	json_object_remove_member(fp, "fonts");
	json_object_remove_member(fp, "home");
	fp_text = ls_json_encode(fingerprint);
	text = g_strdup(recipe_text);
	parts = g_strsplit(text, "style: editorial}", 2);
	avoid_recipe = g_strdup_printf("%sstyle: editorial, avoid: [%s]}%s", parts[0], fp_text, parts[1]);
	g_strfreev(parts);
	fixture_setup(&second, 8, 1);
	g_clear_object(&second.recipe);
	{
		g_autofree gchar *filled = NULL;
		parts = g_strsplit(avoid_recipe, "PARALLEL", -1);
		filled = g_strjoinv("8", parts);
		g_strfreev(parts);
		parts = g_strsplit(filled, "RETRIES", -1);
		g_free(filled);
		filled = g_strjoinv("1", parts);
		g_strfreev(parts);
		second.recipe = ls_recipe_new(filled, &error);
		g_assert_no_error(error);
	}
	run_plan(&second);
	g_assert_nonnull(strstr(second.messages->str, "dealt a layout "));
	plan_a = ls_json_parse(artifact_text(first.context, "site/plan.json"), NULL);
	plan_b = ls_json_parse(artifact_text(second.context, "site/plan.json"), NULL);
	g_assert_cmpfloat(json_object_get_double_member(json_object_get_object_member(json_node_get_object(plan_a), "structure"), "width"), !=,
		json_object_get_double_member(json_object_get_object_member(json_node_get_object(plan_b), "structure"), "width"));
	g_assert_true(run_step(&second, LS_TYPE_PAGES_STEP, 1, NULL, &error));
	g_assert_no_error(error);
	/* The fake writes the same layer every time: sent back once, then left out. */
	{
		GPtrArray *prompts = prompts_for(second.provider, "signature");
		g_assert_cmpuint(prompts->len, ==, 2);
		g_assert_nonnull(strstr(g_ptr_array_index(prompts, 1), "repeat an earlier site's signature layer"));
	}
	g_assert_null(ls_context_lookup(second.context, "site/signature.css"));
	g_assert_nonnull(ls_context_lookup(second.context, "index.html"));
	fixture_teardown(&first);
	fixture_teardown(&second);
}

static void
test_sequential(void){
	Fixture fixture;
	g_autoptr(GError) error = NULL;
	fixture_setup(&fixture, 1, 1);
	run_plan(&fixture);
	{
		gboolean okay = run_step(&fixture, LS_TYPE_PAGES_STEP, 1, NULL, &error);
		g_assert_no_error(error);
		g_assert_true(okay);
	}
	g_assert_cmpint(fixture.provider->max_in_flight, ==, 1);
	fixture_teardown(&fixture);
}

/* A rejected page is retried alone, with the exact rejection. */
static void
test_retry_one(void){
	Fixture fixture;
	g_autoptr(GError) error = NULL;
	GPtrArray *work;
	fixture_setup(&fixture, 8, 1);
	run_plan(&fixture);
	g_hash_table_insert(fixture.provider->failures, g_strdup("work.html"), GINT_TO_POINTER(1));
	{
		gboolean okay = run_step(&fixture, LS_TYPE_PAGES_STEP, 1, NULL, &error);
		g_assert_no_error(error);
		g_assert_true(okay);
	}
	g_assert_cmpint(fixture.provider->calls, ==, 10);
	work = prompts_for(fixture.provider, "work.html");
	g_assert_cmpuint(work->len, ==, 2);
	g_assert_nonnull(strstr(g_ptr_array_index(work, 1), "unknown component 'banner'"));
	g_assert_cmpuint(prompts_for(fixture.provider, "index.html")->len, ==, 1);
	fixture_teardown(&fixture);
}

/* Exhausted retries fail the step and leave the project untouched. */
static void
test_failure(void){
	Fixture fixture;
	g_autoptr(GError) error = NULL;
	fixture_setup(&fixture, 8, 1);
	run_plan(&fixture);
	g_hash_table_insert(fixture.provider->failures, g_strdup("work.html"), GINT_TO_POINTER(9));
	g_assert_false(run_step(&fixture, LS_TYPE_PAGES_STEP, 1, NULL, &error));
	g_assert_error(error, LS_ERROR, LS_ERROR_INVALID);
	g_assert_nonnull(strstr(error->message, "work.html"));
	g_assert_null(ls_context_lookup(fixture.context, "index.html"));
	g_assert_null(ls_context_lookup(fixture.context, "assets/bench.svg"));
	g_assert_nonnull(ls_context_lookup(fixture.context, "site/plan.json"));
	fixture_teardown(&fixture);
}

/* Downstream acceptance failures go back to the one page that caused them. */
static void
test_acceptance(void){
	Fixture fixture;
	g_autoptr(GError) error = NULL;
	GPtrArray *start;
	fixture_setup(&fixture, 8, 1);
	run_plan(&fixture);
	fixture.provider->drop_note = TRUE;
	{
		gboolean okay = run_step(&fixture, LS_TYPE_PAGES_STEP, 1, NULL, &error);
		g_assert_no_error(error);
		g_assert_true(okay);
	}
	start = prompts_for(fixture.provider, "start.html");
	g_assert_cmpuint(start->len, ==, 2);
	g_assert_nonnull(strstr(g_ptr_array_index(start, 1), "PREVIOUS ATTEMPT REJECTED"));
	g_assert_nonnull(strstr(g_ptr_array_index(start, 1), "start.html"));
	g_assert_cmpuint(prompts_for(fixture.provider, "work.html")->len, ==, 1);
	fixture_teardown(&fixture);
}

/* Cancellation stops every worker; the step returns only after they finish. */
static void
test_cancel(void){
	Fixture fixture;
	g_autoptr(GError) error = NULL;
	g_autoptr(GCancellable) cancel = g_cancellable_new();
	fixture_setup(&fixture, 8, 1);
	run_plan(&fixture);
	fixture.provider->block = TRUE;
	fixture.provider->started = cancel;
	g_assert_false(run_step(&fixture, LS_TYPE_PAGES_STEP, 1, cancel, &error));
	g_assert_error(error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
	g_assert_cmpint(fixture.provider->in_flight, ==, 0);
	g_assert_null(ls_context_lookup(fixture.context, "index.html"));
	fixture.provider->started = NULL;
	fixture_teardown(&fixture);
}

/* A review pass rewrites page specifications only, from the current ones. */
static void
test_review(void){
	Fixture fixture;
	g_autoptr(GError) error = NULL;
	g_autoptr(JsonObject) config = NULL;
	g_autoptr(JsonObject) options = json_object_new();
	g_autoptr(LsStep) step = g_object_new(LS_TYPE_PAGES_STEP, NULL);
	fixture_setup(&fixture, 8, 1);
	run_plan(&fixture);
	{
		gboolean okay = run_step(&fixture, LS_TYPE_PAGES_STEP, 1, NULL, &error);
		g_assert_no_error(error);
		g_assert_true(okay);
	}
	config = json_object_new();
	json_object_set_string_member(config, "id", "review");
	json_object_set_string_member(config, "type", "pages");
	json_object_set_string_member(options, "mode", "review");
	json_object_set_object_member(config, "options", json_object_ref(options));
	{
		gboolean okay = ls_step_execute(step, fixture.recipe, config, fixture.prompts, fixture.context,
		LS_PROVIDER(fixture.provider), NULL, &error);
		g_assert_no_error(error);
		g_assert_true(okay);
	}
	/* The plan, seven write calls (with the signature) and five review calls:
	 * a review keeps the signature it found and writes no new one. */
	g_assert_cmpint(fixture.provider->calls, ==, 1 + 8 + 5);
	g_assert_cmpuint(prompts_for(fixture.provider, "signature")->len, ==, 1);
	g_assert_nonnull(strstr(artifact_text(fixture.context, "styles.css"), "/* Site signature */"));
	g_assert_cmpuint(prompts_for(fixture.provider, "work.html")->len, ==, 2);
	g_assert_nonnull(strstr(g_ptr_array_index(prompts_for(fixture.provider, "work.html"), 1), "Ladderback chair"));
	fixture_teardown(&fixture);
}

static void
test_requires_plan(void){
	Fixture fixture;
	g_autoptr(GError) error = NULL;
	fixture_setup(&fixture, 8, 1);
	g_assert_false(run_step(&fixture, LS_TYPE_PAGES_STEP, 1, NULL, &error));
	g_assert_error(error, LS_ERROR, LS_ERROR_INVALID);
	g_assert_nonnull(strstr(error->message, "site/plan.json"));
	g_assert_cmpint(fixture.provider->calls, ==, 0);
	fixture_teardown(&fixture);
}

static void
test_options(void){
	const gchar *bad[] = {"{parallel: 0}", "{parallel: 17}", "{parallel: two}", "{mode: rewrite}",
		"{base_url: 'http://example.com'}", "{extra: 1}"};
	const gchar *good[] = {"{}", "{parallel: 16}", "{mode: review, parallel: 2}",
		"{base_url: 'https://example.com/shop/'}"};
	guint i;
	for (i = 0; i < G_N_ELEMENTS(bad); i++) {
		g_autoptr(GError) error = NULL;
		g_autoptr(JsonNode) node = ls_yaml_parse(bad[i], NULL);
		g_assert_false(ls_pages_step_check_options(json_node_get_object(node), &error));
		g_assert_error(error, LS_ERROR, LS_ERROR_INVALID);
	}
	for (i = 0; i < G_N_ELEMENTS(good); i++) {
		g_autoptr(GError) error = NULL;
		g_autoptr(JsonNode) node = ls_yaml_parse(good[i], NULL);
		g_assert_true(ls_pages_step_check_options(json_node_get_object(node), &error));
		g_assert_no_error(error);
	}
	g_assert_true(ls_pages_step_check_options(NULL, NULL));
}


/* A plan step that names a theme asks only for content and structure, and
 * the stored plan carries the theme's whole design. */
static void
test_plan_theme(void){
	Fixture fixture;
	GPtrArray *prompts;
	g_autoptr(JsonNode) plan = NULL;
	JsonObject *options = json_object_new();
	JsonObject *root;
	fixture_setup(&fixture, 4, 1);
	json_object_set_string_member(options, "theme", "midnight");
	json_object_set_object_member(step_config(&fixture, 0), "options", options);
	run_plan(&fixture);
	g_assert_null(strstr(fixture.provider->last_schema, "\"tokens\""));
	g_assert_null(strstr(fixture.provider->last_schema, "\"restyle\""));
	g_assert_nonnull(strstr(fixture.provider->last_schema, "\"pages\""));
	prompts = prompts_for(fixture.provider, "plan");
	g_assert_cmpuint(prompts->len, ==, 1);
	g_assert_nonnull(strstr(g_ptr_array_index(prompts, 0), "\"theme\":{\"name\":\"midnight\""));
	g_assert_null(strstr(g_ptr_array_index(prompts, 0), "\"suggested_look\":{"));
	g_assert_null(strstr(g_ptr_array_index(prompts, 0), "\"suggested_palette\":{"));
	g_assert_nonnull(strstr(g_ptr_array_index(prompts, 0), "\"suggested_home\":{"));
	plan = ls_json_parse(artifact_text(fixture.context, "site/plan.json"), NULL);
	root = json_node_get_object(plan);
	g_assert_cmpstr(ls_string(json_object_get_object_member(root, "tokens"), "paper"), ==, "#0e1422");
	g_assert_cmpstr(ls_string(json_object_get_object_member(root, "look"), "header"), ==, "floating");
	g_assert_false(json_object_has_member(root, "restyle"));
	g_assert_cmpstr(ls_string(root, "name"), ==, "Harbor & Pine");
	fixture_teardown(&fixture);
}

/* Plan options allow only a known theme, and preflight names the choices. */
static void
test_plan_options(void){
	const gchar *cases[][2] = {{"{theme: nope}", "Unknown theme 'nope'; available: editorial"},
		{"{theme: midnight, extra: 1}", "plan options allow only theme"}, {"{theme: 3}", "plan options allow only theme"},
		{"{sector: nope}", "Unknown sector 'nope'; available: trades"}, {"{sector: 3}", "and sector"},
		{"{images: {max: 10}}", "options.images allows max"}, {"{images: 3}", "plus images"},
		{"{images: {widths: [480], format: avif}}", "options.images allows max"}};
	guint i;
	for (i = 0; i < G_N_ELEMENTS(cases); i++) {
		Fixture fixture;
		g_autoptr(GError) error = NULL;
		g_autoptr(JsonNode) node = ls_yaml_parse(cases[i][0], NULL);
		fixture_setup(&fixture, 4, 1);
		json_object_set_object_member(step_config(&fixture, 0), "options", json_object_ref(json_node_get_object(node)));
		g_assert_false(ls_spec_step_check(fixture.prompts, fixture.recipe, step_config(&fixture, 0), &error));
		g_assert_error(error, LS_ERROR, LS_ERROR_INVALID);
		if (strstr(error->message, cases[i][1]) == NULL)
			g_error("case %u lacks '%s': %s", i, cases[i][1], error->message);
		g_assert_cmpint(fixture.provider->calls, ==, 0);
		fixture_teardown(&fixture);
	}
}

/* A plan that falls back on the house default is sent back with the reason,
 * and the sector option deals an identity that fits it. */
static void
test_plan_identity(void){
	Fixture fixture;
	GPtrArray *prompts;
	g_autoptr(GError) error = NULL;
	g_autoptr(LsComponents) components = ls_components_new(&error);
	JsonObject *options = json_object_new();
	const gchar *task;
	g_autofree gchar *wanted = NULL;
	g_assert_no_error(error);
	fixture_setup(&fixture, 4, 1);
	json_object_set_string_member(options, "sector", "food");
	json_object_set_object_member(step_config(&fixture, 0), "options", options);
	g_assert_true(ls_spec_step_check(fixture.prompts, fixture.recipe, step_config(&fixture, 0), &error));
	fixture.provider->default_plan = TRUE;
	run_plan(&fixture);
	prompts = prompts_for(fixture.provider, "plan");
	g_assert_cmpuint(prompts->len, ==, 2);
	g_assert_nonnull(strstr(g_ptr_array_index(prompts, 1), "house default"));
	/* The prompt carries exactly the identity the library deals for this site. */
	{
		JsonObject *root = ls_recipe_get_root(fixture.recipe);
		g_autofree gchar *seed = g_strconcat(ls_string(root, "name"), "\n",
			ls_string(json_object_get_object_member(root, "site"), "brief"), NULL);
		g_autoptr(JsonObject) identity = ls_components_suggest_identity(components, seed, "food");
		wanted = g_strdup_printf("\"suggested_palette\":{\"summary\":\"%s\"",
			ls_string(json_object_get_object_member(identity, "palette"), "summary"));
	}
	task = strstr(g_ptr_array_index(prompts, 0), "TASK (JSON)");
	g_assert_nonnull(task);
	g_assert_nonnull(strstr(task, wanted));
	fixture_teardown(&fixture);
}

/* Supplied photographs reach the plan prompt with their pixel sizes read
 * from the seeds (PNG, JPEG and WebP headers), and the builder writes them
 * into the stored plan, sized for phones; a photo without a readable seed
 * stops the step. The seeds are real images drawn at test time. */
static gchar *
base64_of(GBytes *bytes){
	return g_base64_encode(g_bytes_get_data(bytes, NULL), g_bytes_get_size(bytes));
}

static gchar *
photo_recipe(void){
	g_autoptr(GBytes) bench = image_fixture(640, 480, FALSE, ".png");
	g_autoptr(GBytes) chair = image_fixture(1800, 1200, FALSE, ".jpg[Q=85]");
	g_autoptr(GBytes) shop = image_fixture(1600, 900, FALSE, ".webp");
	g_autofree gchar *bench64 = base64_of(bench);
	g_autofree gchar *chair64 = base64_of(chair);
	g_autofree gchar *shop64 = base64_of(shop);
	return g_strdup_printf(
		"version: 1\n"
		"name: harbor\n"
		"site:\n"
		"  brief: An illustrative furniture repair workshop\n"
		"  style: editorial\n"
		"  photos:\n"
		"    - {path: assets/bench.png, alt: A workbench with clamps, credit: Test}\n"
		"    - {path: assets/chair.jpg, alt: A repaired chair}\n"
		"    - {path: assets/shop.webp, alt: The shop front}\n"
		"seed_artifacts:\n"
		"  - {path: assets/bench.png, encoding: base64, content: %s}\n"
		"  - {path: assets/chair.jpg, encoding: base64, content: %s}\n"
		"  - {path: assets/shop.webp, encoding: base64, content: %s}\n"
		"ai: {provider: claude-code, retries: 1}\n"
		"pipeline:\n"
		"  steps:\n"
		"    - {id: plan, type: plan, instructions: Plan three pages.}\n"
		"    - {id: pages, type: pages}\n"
		"    - id: acceptance\n"
		"      type: validate\n"
		"      options: {required: [index.html]}\n", bench64, chair64, shop64);
}

/* @text with the first @needle replaced by @value. */
static gchar *
replace_once(const gchar *text, const gchar *needle, const gchar *value){
	const gchar *at = strstr(text, needle);
	g_assert_nonnull(at);
	return g_strdup_printf("%.*s%s%s", (gint)(at - text), text, value, at + strlen(needle));
}

/* Supplied social profiles are the builder's: the stored plan lists the
 * recipe's profiles with recognised networks, replacing any the model
 * wrote, and a recipe without profiles leaves the plan without any. */
static void
test_plan_social(void){
	g_autoptr(GError) error = NULL;
	g_autofree gchar *text = photo_recipe();
	g_autofree gchar *with_social = NULL;
	g_autoptr(LsPrompts) prompts = ls_prompts_new(NULL, &error);
	const gchar *variants[2];
	guint v;
	g_assert_no_error(error);
	with_social = replace_once(text, "  photos:\n", "  social:\n"
		"    - {url: 'https://twitter.com/harbor_repair'}\n"
		"    - {url: 'https://fosstodon.org/@harbor', label: Fediverse}\n"
		"  photos:\n");
	variants[0] = with_social;
	variants[1] = text;
	for (v = 0; v < 2; v++) {
		g_autoptr(LsRecipe) recipe = ls_recipe_new(variants[v], &error);
		g_autoptr(LsContext) context = NULL;
		FakeProvider *provider = g_object_new(fake_provider_get_type(), NULL);
		g_autoptr(LsStep) step = g_object_new(LS_TYPE_PLAN_STEP, NULL);
		g_autoptr(JsonNode) plan = NULL;
		JsonObject *root;
		g_assert_no_error(error);
		context = ls_recipe_create_context(recipe, &error);
		g_assert_no_error(error);
		g_assert_true(ls_step_execute(step, recipe, json_array_get_object_element(ls_recipe_get_steps(recipe), 0),
			prompts, context, LS_PROVIDER(provider), NULL, &error));
		g_assert_no_error(error);
		plan = ls_json_parse(ls_artifact_get_content(ls_context_lookup(context, "site/plan.json")), NULL);
		root = json_node_get_object(plan);
		if (v == 0) {
			JsonArray *social = json_object_get_array_member(root, "social");
			JsonObject *first = json_array_get_object_element(social, 0);
			JsonObject *second = json_array_get_object_element(social, 1);
			g_assert_cmpuint(json_array_get_length(social), ==, 2);
			g_assert_cmpstr(json_object_get_string_member(first, "network"), ==, "x");
			g_assert_cmpstr(json_object_get_string_member(first, "handle"), ==, "@harbor_repair");
			g_assert_cmpstr(json_object_get_string_member(second, "network"), ==, "mastodon");
			g_assert_cmpstr(json_object_get_string_member(second, "label"), ==, "Fediverse");
			g_assert_null(strstr(ls_artifact_get_content(ls_context_lookup(context, "site/plan.json")), "harborbench"));
		} else
			g_assert_false(json_object_has_member(root, "social"));
		g_object_unref(provider);
	}
}

/* The business phone and email are the builder's too: the stored plan
 * carries exactly the supplied ones, and nothing when none was supplied. */
static void
test_plan_contact(void){
	g_autoptr(GError) error = NULL;
	g_autoptr(LsPrompts) prompts = ls_prompts_new(NULL, &error);
	const gchar *contacts[] = {
		"  contact: {phone: '+1 (555) 014-2270', email: hello@harbor.example, address: 1 Quay Street}\n  photos:\n",
		"  contact: {address: 1 Quay Street}\n  photos:\n",
		"  photos:\n"};
	guint v;
	g_assert_no_error(error);
	for (v = 0; v < G_N_ELEMENTS(contacts); v++) {
		g_autofree gchar *base = photo_recipe();
		g_autofree gchar *text = replace_once(base, "  photos:\n", contacts[v]);
		g_autoptr(LsRecipe) recipe = ls_recipe_new(text, &error);
		g_autoptr(LsContext) context = NULL;
		FakeProvider *provider = g_object_new(fake_provider_get_type(), NULL);
		g_autoptr(LsStep) step = g_object_new(LS_TYPE_PLAN_STEP, NULL);
		g_autoptr(JsonNode) plan = NULL;
		JsonObject *root;
		g_assert_no_error(error);
		context = ls_recipe_create_context(recipe, &error);
		g_assert_no_error(error);
		g_assert_true(ls_step_execute(step, recipe, json_array_get_object_element(ls_recipe_get_steps(recipe), 0),
			prompts, context, LS_PROVIDER(provider), NULL, &error));
		g_assert_no_error(error);
		plan = ls_json_parse(ls_artifact_get_content(ls_context_lookup(context, "site/plan.json")), NULL);
		root = json_node_get_object(plan);
		if (v == 0) {
			JsonObject *contact = json_object_get_object_member(root, "contact");
			g_assert_nonnull(contact);
			g_assert_cmpuint(json_object_get_size(contact), ==, 2);
			g_assert_cmpstr(json_object_get_string_member(contact, "phone"), ==, "+1 (555) 014-2270");
			g_assert_cmpstr(json_object_get_string_member(contact, "email"), ==, "hello@harbor.example");
		} else
			g_assert_false(json_object_has_member(root, "contact"));
		g_object_unref(provider);
	}
}

/* @text indented by @spaces on every line, for a YAML block scalar. */
static gchar *
indented(const gchar *text, const gchar *spaces){
	g_auto(GStrv) lines = g_strsplit(text, "\n", -1);
	GString *out = g_string_new(NULL);
	guint i;
	for (i = 0; lines[i] != NULL; i++)
		if (*lines[i] != '\0')
			g_string_append_printf(out, "%s%s\n", spaces, lines[i]);
	return g_string_free(out, FALSE);
}

/* Supplied podbay modules are builder facts: the stored plan carries each
 * one's snippet and stylesheet as supplied, replacing any the model wrote,
 * while the planner sees only ids and titles, never the markup. */
static void
test_plan_modules(void){
	g_autoptr(GError) error = NULL;
	g_autoptr(LsPrompts) prompts = ls_prompts_new(NULL, &error);
	g_autofree gchar *snippet = fixture("../podbay/contact-form.html");
	g_autofree gchar *css = fixture("../podbay/contact-form.css");
	g_autofree gchar *snippet_yaml = indented(snippet, "        ");
	g_autofree gchar *css_yaml = indented(css, "        ");
	g_autofree gchar *modules = g_strdup_printf("  modules:\n    - id: contact-form\n      title: Contact us\n"
		"      snippet: |\n%s      css: |\n%s  photos:\n", snippet_yaml, css_yaml);
	g_autofree gchar *base = photo_recipe();
	g_autofree gchar *text = replace_once(base, "  photos:\n", modules);
	g_autoptr(LsRecipe) recipe = ls_recipe_new(text, &error);
	g_autoptr(LsContext) context = NULL;
	FakeProvider *provider = g_object_new(fake_provider_get_type(), NULL);
	g_autoptr(LsStep) step = g_object_new(LS_TYPE_PLAN_STEP, NULL);
	g_autoptr(JsonNode) plan = NULL;
	JsonArray *stored;
	JsonObject *module;
	GPtrArray *seen;
	const gchar *prompt;
	g_assert_no_error(error);
	context = ls_recipe_create_context(recipe, &error);
	g_assert_no_error(error);
	g_assert_true(ls_step_execute(step, recipe, json_array_get_object_element(ls_recipe_get_steps(recipe), 0),
		prompts, context, LS_PROVIDER(provider), NULL, &error));
	g_assert_no_error(error);
	plan = ls_json_parse(ls_artifact_get_content(ls_context_lookup(context, "site/plan.json")), NULL);
	stored = json_object_get_array_member(json_node_get_object(plan), "modules");
	g_assert_nonnull(stored);
	g_assert_cmpuint(json_array_get_length(stored), ==, 1);
	module = json_array_get_object_element(stored, 0);
	g_assert_cmpstr(json_object_get_string_member(module, "id"), ==, "contact-form");
	g_assert_cmpstr(json_object_get_string_member(module, "title"), ==, "Contact us");
	g_assert_cmpstr(json_object_get_string_member(module, "snippet"), ==, snippet);
	g_assert_cmpstr(json_object_get_string_member(module, "css"), ==, css);
	seen = g_hash_table_lookup(provider->prompts, "plan");
	g_assert_nonnull(seen);
	prompt = g_ptr_array_index(seen, 0);
	g_assert_nonnull(strstr(prompt, "contact-form"));
	g_assert_null(strstr(prompt, "hx-post"));
	g_assert_null(strstr(prompt, "--podbay-gap"));
	/* Page writers see the plan's modules by id and title only. */
	{
		g_autoptr(LsStep) pages = g_object_new(LS_TYPE_PAGES_STEP, NULL);
		GPtrArray *home;
		g_assert_true(ls_step_execute(pages, recipe, json_array_get_object_element(ls_recipe_get_steps(recipe), 1),
			prompts, context, LS_PROVIDER(provider), NULL, &error));
		g_assert_no_error(error);
		home = g_hash_table_lookup(provider->prompts, "index.html");
		g_assert_nonnull(home);
		g_assert_nonnull(strstr(g_ptr_array_index(home, 0), "\"modules\":[{\"id\":\"contact-form\",\"title\":\"Contact us\"}]"));
		g_assert_null(strstr(g_ptr_array_index(home, 0), "hx-post"));
	}
	g_object_unref(provider);
}

static void
test_plan_photos(void){
	g_autoptr(GError) error = NULL;
	g_autofree gchar *text = photo_recipe();
	g_autoptr(LsRecipe) recipe = ls_recipe_new(text, &error);
	g_autoptr(LsPrompts) prompts = ls_prompts_new(NULL, &error);
	g_autoptr(LsContext) context = NULL;
	FakeProvider *provider = g_object_new(fake_provider_get_type(), NULL);
	g_autoptr(LsStep) step = g_object_new(LS_TYPE_PLAN_STEP, NULL);
	g_autoptr(JsonNode) plan = NULL;
	g_autofree gchar *preview = NULL;
	JsonArray *photos;
	const gchar *task;
	g_assert_no_error(error);
	context = ls_recipe_create_context(recipe, &error);
	g_assert_no_error(error);
	g_assert_true(ls_step_execute(step, recipe, json_array_get_object_element(ls_recipe_get_steps(recipe), 0),
		prompts, context, LS_PROVIDER(provider), NULL, &error));
	g_assert_no_error(error);
	task = strstr(g_ptr_array_index(prompts_for(provider, "plan"), 0), "TASK (JSON)");
	g_assert_nonnull(strstr(task, "\"supplied_photos\":[{\"path\":\"assets/bench.png\",\"alt\":\"A workbench with clamps\","
		"\"credit\":\"Test\",\"width\":640,\"height\":480}"));
	g_assert_nonnull(strstr(task, "\"path\":\"assets/chair.jpg\",\"alt\":\"A repaired chair\",\"width\":1800,\"height\":1200}"));
	g_assert_nonnull(strstr(task, "\"path\":\"assets/shop.webp\",\"alt\":\"The shop front\",\"width\":1600,\"height\":900}"));
	plan = ls_json_parse(ls_artifact_get_content(ls_context_lookup(context, "site/plan.json")), NULL);
	photos = json_object_get_array_member(json_node_get_object(plan), "photos");
	g_assert_cmpuint(json_array_get_length(photos), ==, 3);
	/* Stored photos are sized for phones: the 1800 px chair is capped at
	 * 1600 with smaller widths beside it, and the original is archived, out
	 * of the published tree but still in the run record. */
	{
		JsonObject *chair = json_array_get_object_element(photos, 1);
		JsonArray *variants = json_object_get_array_member(chair, "variants");
		const LsArtifact *variant = ls_context_lookup(context, "assets/chair-800w.webp");
		g_assert_cmpint(json_object_get_int_member(chair, "width"), ==, 1600);
		g_assert_cmpint(json_object_get_int_member(chair, "height"), ==, 1067);
		g_assert_cmpuint(json_array_get_length(variants), ==, 4);
		g_assert_nonnull(variant);
		g_assert_true(ls_artifact_is_binary(variant));
		g_assert_null(ls_context_lookup(context, "assets/chair.jpg"));
		g_assert_nonnull(ls_context_lookup_archived(context, "assets/chair.jpg"));
		g_assert_cmpuint(json_array_get_length(json_object_get_array_member(json_array_get_object_element(photos, 0),
			"variants")), ==, 2);
	}
	/* A preview lists the photos without reading any seed. */
	preview = ls_spec_step_preview(prompts, recipe, json_array_get_object_element(ls_recipe_get_steps(recipe), 0), &error);
	g_assert_no_error(error);
	g_assert_nonnull(strstr(preview, "\"supplied_photos\":[{\"path\":\"assets/bench.png\""));
	/* A photo whose seed is not an image stops the step before any call. */
	{
		g_autofree gchar *broken = photo_recipe();
		g_autoptr(LsRecipe) bad = NULL;
		g_autoptr(LsContext) bad_context = NULL;
		FakeProvider *quiet = g_object_new(fake_provider_get_type(), NULL);
		g_autoptr(LsStep) bad_step = g_object_new(LS_TYPE_PLAN_STEP, NULL);
		gchar *png = strstr(broken, "iVBORw0KGgo");
		memcpy(png, "aGVsbG8gd2", 10);
		bad = ls_recipe_new(broken, &error);
		g_assert_no_error(error);
		bad_context = ls_recipe_create_context(bad, &error);
		g_assert_no_error(error);
		g_assert_false(ls_step_execute(bad_step, bad, json_array_get_object_element(ls_recipe_get_steps(bad), 0),
			prompts, bad_context, LS_PROVIDER(quiet), NULL, &error));
		g_assert_error(error, LS_ERROR, LS_ERROR_INVALID);
		g_assert_nonnull(strstr(error->message, "assets/bench.png"));
		g_assert_cmpint(quiet->calls, ==, 0);
		g_clear_error(&error);
		g_object_unref(quiet);
	}
	g_object_unref(provider);
}

/* The issue's case end to end: a 1536x1024 PNG shown as the home page's hero
 * and in the journal. */
static gchar *
phone_recipe(GBytes *png){
	g_autofree gchar *png64 = base64_of(png);
	return g_strdup_printf(
		"version: 1\n"
		"name: harbor\n"
		"site:\n"
		"  brief: An illustrative furniture repair workshop\n"
		"  style: editorial\n"
		"  photos:\n"
		"    - {path: assets/photos/harbor.png, alt: The workshop at dawn}\n"
		"seed_artifacts:\n"
		"  - {path: assets/photos/harbor.png, encoding: base64, content: %s}\n"
		"ai: {provider: claude-code, retries: 1}\n"
		"pipeline:\n"
		"  steps:\n"
		"    - {id: plan, type: plan, instructions: Plan three pages.}\n"
		"    - {id: pages, type: pages, options: {parallel: 4, base_url: 'https://harbor.example/'}}\n", png64);
}

static LsContext *
build_with_photo(GBytes *png){
	g_autoptr(GError) error = NULL;
	g_autofree gchar *text = phone_recipe(png);
	g_autoptr(LsRecipe) recipe = ls_recipe_new(text, &error);
	g_autoptr(LsPrompts) prompts = ls_prompts_new(NULL, &error);
	g_autoptr(LsStep) plan = g_object_new(LS_TYPE_PLAN_STEP, NULL);
	g_autoptr(LsStep) pages = g_object_new(LS_TYPE_PAGES_STEP, NULL);
	FakeProvider *provider = g_object_new(fake_provider_get_type(), NULL);
	LsContext *context;
	g_assert_no_error(error);
	context = ls_recipe_create_context(recipe, &error);
	g_assert_no_error(error);
	provider->photo = "assets/photos/harbor.png";
	g_assert_true(ls_step_execute(plan, recipe, json_array_get_object_element(ls_recipe_get_steps(recipe), 0),
		prompts, context, LS_PROVIDER(provider), NULL, &error));
	g_assert_no_error(error);
	ls_step_execute(pages, recipe, json_array_get_object_element(ls_recipe_get_steps(recipe), 1),
		prompts, context, LS_PROVIDER(provider), NULL, &error);
	g_assert_no_error(error);
	/* Page writers name the supplied photo; its sized files are the builder's. */
	{
		const gchar *task = strstr(g_ptr_array_index(prompts_for(provider, "index.html"), 0), "TASK (JSON)");
		g_assert_nonnull(strstr(task, "\"path\":\"assets/photos/harbor.png\""));
		g_assert_null(strstr(task, "harbor-480w.webp"));
		g_assert_null(strstr(task, "harbor-1536w.webp"));
	}
	g_object_unref(provider);
	return context;
}

/* Every <img> tag in @html that shows @src. */
static GPtrArray *
img_tags(const gchar *html, const gchar *src){
	g_autofree gchar *needle = g_strdup_printf("<img src=\"%s\"", src);
	GPtrArray *tags = g_ptr_array_new_with_free_func(g_free);
	const gchar *at = html;
	while ((at = strstr(at, needle)) != NULL) {
		const gchar *end = strchr(at, '>');
		g_assert_nonnull(end);
		g_ptr_array_add(tags, g_strndup(at, (gsize)(end - at + 1)));
		at = end;
	}
	return tags;
}

static gsize
artifact_size(LsContext *context, const gchar *path){
	const LsArtifact *artifact = ls_context_lookup(context, path);
	gsize size = 0;
	g_assert_nonnull(artifact);
	g_assert_true(ls_artifact_is_binary(artifact));
	ls_artifact_get_data(artifact, &size);
	return size;
}

/* Pages reference the phone-sized WebP variants with srcset, sizes and
 * dimensions; the hero loads eagerly at high priority and the journal's
 * photos lazily; link previews use the capped variant; the original PNG is
 * archived, not published; and two runs produce identical sites. */
static void
test_phone_images(void){
	g_autoptr(GBytes) png = image_fixture(1536, 1024, FALSE, ".png");
	g_autoptr(LsContext) context = build_with_photo(png);
	g_autoptr(LsContext) again = build_with_photo(png);
	const gchar *srcset = "srcset=\"assets/photos/harbor-480w.webp 480w, assets/photos/harbor-800w.webp 800w, "
		"assets/photos/harbor-1200w.webp 1200w, assets/photos/harbor-1536w.webp 1536w\"";
	const gchar *pages[] = {"index.html", "journal.html", "styles.css"};
	g_autoptr(GPtrArray) hero = img_tags(artifact_text(context, "index.html"), "assets/photos/harbor-1536w.webp");
	g_autoptr(GPtrArray) journal = img_tags(artifact_text(context, "journal.html"), "assets/photos/harbor-1536w.webp");
	g_autofree gchar *first = NULL, *second = NULL;
	gsize before, phone, desktop;
	guint i;
	g_assert_cmpuint(hero->len, ==, 1);
	g_assert_nonnull(strstr(g_ptr_array_index(hero, 0), srcset));
	g_assert_nonnull(strstr(g_ptr_array_index(hero, 0), " sizes=\""));
	g_assert_nonnull(strstr(g_ptr_array_index(hero, 0), "width=\"1536\" height=\"1024\""));
	g_assert_nonnull(strstr(g_ptr_array_index(hero, 0), "fetchpriority=\"high\""));
	g_assert_null(strstr(g_ptr_array_index(hero, 0), "loading=\"lazy\""));
	g_assert_cmpuint(journal->len, >=, 1);
	for (i = 0; i < journal->len; i++) {
		g_assert_nonnull(strstr(g_ptr_array_index(journal, i), srcset));
		g_assert_nonnull(strstr(g_ptr_array_index(journal, i), "loading=\"lazy\""));
		g_assert_null(strstr(g_ptr_array_index(journal, i), "fetchpriority"));
	}
	g_assert_nonnull(strstr(artifact_text(context, "index.html"),
		"https://harbor.example/assets/photos/harbor-1536w.webp"));
	for (i = 0; i < G_N_ELEMENTS(pages); i++)
		g_assert_null(strstr(artifact_text(context, pages[i]), "harbor.png"));
	g_assert_null(ls_context_lookup(context, "assets/photos/harbor.png"));
	g_assert_nonnull(ls_context_lookup_archived(context, "assets/photos/harbor.png"));
	/* Page weight for the photo: the old page shipped the PNG to everyone; a
	 * phone 390 px wide at 2x now picks the 800w file, a desktop the largest. */
	before = g_bytes_get_size(png);
	phone = artifact_size(context, "assets/photos/harbor-800w.webp");
	desktop = artifact_size(context, "assets/photos/harbor-1536w.webp");
	g_test_message("hero photo weight: PNG %" G_GSIZE_FORMAT " bytes; phone 800w WebP %" G_GSIZE_FORMAT
		"; desktop 1536w WebP %" G_GSIZE_FORMAT, before, phone, desktop);
	g_assert_cmpuint(phone * 10, <, before);
	g_assert_cmpuint(desktop * 4, <, before);
	first = ls_context_snapshot(context);
	second = ls_context_snapshot(again);
	g_assert_cmpstr(first, ==, second);
}

int
main(int argc, char **argv){
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/pages/plan", test_plan);
	g_test_add_func("/pages/plan-theme", test_plan_theme);
	g_test_add_func("/pages/plan-options", test_plan_options);
	g_test_add_func("/pages/plan-identity", test_plan_identity);
	g_test_add_func("/pages/plan-photos", test_plan_photos);
	g_test_add_func("/pages/plan-social", test_plan_social);
	g_test_add_func("/pages/phone-images", test_phone_images);
	g_test_add_func("/pages/plan-contact", test_plan_contact);
	g_test_add_func("/pages/plan-modules", test_plan_modules);
	g_test_add_func("/pages/signature-fallback", test_signature_fallback);
	g_test_add_func("/pages/sameness-run", test_sameness_run);
	g_test_add_func("/pages/parallel", test_parallel);
	g_test_add_func("/pages/sequential", test_sequential);
	g_test_add_func("/pages/retry-one", test_retry_one);
	g_test_add_func("/pages/failure", test_failure);
	g_test_add_func("/pages/acceptance", test_acceptance);
	g_test_add_func("/pages/cancel", test_cancel);
	g_test_add_func("/pages/review", test_review);
	g_test_add_func("/pages/requires-plan", test_requires_plan);
	g_test_add_func("/pages/options", test_options);
	return g_test_run();
}
