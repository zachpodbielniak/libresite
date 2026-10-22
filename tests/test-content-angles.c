/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* Content angles: the builder deals each site a headline angle and an art
 * subject, and tells the writers, so ten businesses do not all get
 * "Dependable X for your home" over the same drawing. A model is a fixture
 * that writes the home heading from whatever angle it is told; the test
 * fails when the ten fixture briefs come out with one headline pattern or
 * one art subject. See docs/components.org, "Content angles". */
#include "libresite.h"
#include "core/ls-components.h"
#include "core/ls-util.h"
#include <string.h>

#define DISTINCT_LIMIT 6
/* Headlines are dealt only from the taste block's five statement angles
 * (docs/components.org, "Taste"), so ten sites show at most five. */
#define HEADLINE_LIMIT 5

static JsonNode *
load(const gchar *path){
	g_autofree gchar *text = NULL;
	g_assert_true(g_file_get_contents(path, &text, NULL, NULL));
	return ls_json_parse(text, NULL);
}

static gchar *
replace(gchar *text, const gchar *from, const gchar *to){
	gchar **parts = g_strsplit(text, from, -1);
	gchar *out = g_strjoinv(to, parts);
	g_strfreev(parts);
	g_free(text);
	return out;
}

static gchar *
fill(JsonObject *brief, gchar *text){
	JsonArray *services = json_object_get_array_member(brief, "services");
	text = replace(text, "{{name}}", json_object_get_string_member(brief, "name"));
	text = replace(text, "{{trade}}", json_object_get_string_member(brief, "trade"));
	text = replace(text, "{{s1}}", json_array_get_string_element(services, 0));
	text = replace(text, "{{s2}}", json_array_get_string_element(services, 1));
	return replace(text, "{{s3}}", json_array_get_string_element(services, 2));
}

/* A string member's value after @key":" in the TASK part of @prompt. */
static gchar *
task_value(const gchar *prompt, const gchar *needle){
	const gchar *task = strstr(prompt, "TASK (JSON)");
	const gchar *start = task != NULL ? strstr(task, needle) : NULL;
	const gchar *end;
	if (start == NULL)
		return NULL;
	start += strlen(needle);
	end = strchr(start, '"');
	return end != NULL ? g_strndup(start, (gsize)(end - start)) : NULL;
}

/* The writer: the sameness fixture answers, with the home heading taken
 * from the angle it is told (or the fixture's own heading when it is set
 * to ignore angles). Artwork prompts are recorded. */
typedef struct {
	GObject parent_instance;
	GMutex lock;
	JsonObject *brief;
	gboolean ignore;
	GPtrArray *art;
	GPtrArray *plans;
} Writer;
typedef struct { GObjectClass parent_class; } WriterClass;
static void writer_iface_init(LsProviderInterface *iface);
GType writer_get_type(void);
G_DEFINE_TYPE_WITH_CODE(Writer, writer, G_TYPE_OBJECT, G_IMPLEMENT_INTERFACE(LS_TYPE_PROVIDER, writer_iface_init))
G_DEFINE_AUTOPTR_CLEANUP_FUNC(Writer, g_object_unref)

static gchar *
fixture(const gchar *name){
	g_autofree gchar *path = g_strconcat("tests/fixtures/sameness/", name, NULL);
	gchar *text = NULL;
	g_assert_true(g_file_get_contents(path, &text, NULL, NULL));
	return text;
}

static gchar *
writer_generate(LsProvider *provider, JsonObject *settings, const gchar *system, const gchar *prompt,
	GCancellable *cancellable, GError **error){
	Writer *self = (Writer *)provider;
	const gchar *schema = ls_string(settings, "json_schema");
	g_autofree gchar *route = NULL;
	(void)system;
	(void)cancellable;
	(void)error;
	if (schema != NULL && strstr(schema, "\"nav\"") != NULL) {
		g_mutex_lock(&self->lock);
		g_ptr_array_add(self->plans, g_strdup(prompt));
		g_mutex_unlock(&self->lock);
		return fill(self->brief, fixture("plan.json"));
	}
	if (schema != NULL && strstr(schema, "\"required\": [\"css\"]") != NULL)
		return g_strdup("{\"css\":\".em { color: var(--accent-text); }\"}");
	if (schema != NULL && strstr(schema, "\"svg\"") != NULL) {
		g_autofree gchar *svg = fixture("art.svg");
		g_autoptr(JsonNode) node = json_node_new(JSON_NODE_OBJECT);
		JsonObject *object = json_object_new();
		g_mutex_lock(&self->lock);
		g_ptr_array_add(self->art, g_strdup(prompt));
		g_mutex_unlock(&self->lock);
		json_object_set_string_member(object, "svg", svg);
		json_node_take_object(node, object);
		return ls_json_encode(node);
	}
	route = task_value(prompt, "\"route\":\"");
	g_assert_nonnull(route);
	if (g_str_equal(route, "index.html") && !self->ignore) {
		g_autofree gchar *angle = task_value(prompt, "\"headline_angle\":{\"name\":\"");
		g_autoptr(JsonNode) headlines = load("tests/fixtures/sameness/headlines.json");
		g_autoptr(JsonNode) page = NULL;
		gchar *text = fill(self->brief, fixture("index.json"));
		JsonObject *opening;
		g_assert_nonnull(angle);
		page = ls_json_parse(text, NULL);
		g_free(text);
		opening = json_object_get_object_member(json_array_get_object_element(
			json_object_get_array_member(json_node_get_object(page), "sections"), 0), "content");
		{
			gchar *heading = fill(self->brief, g_strdup(json_object_get_string_member(json_node_get_object(headlines), angle)));
			json_object_set_string_member(opening, "heading", heading);
			g_free(heading);
		}
		return ls_json_encode(page);
	}
	{
		g_autofree gchar *name = g_strndup(route, strlen(route) - strlen(".html"));
		g_autofree gchar *file = g_strconcat(name, ".json", NULL);
		return fill(self->brief, fixture(file));
	}
}

static void
writer_iface_init(LsProviderInterface *iface){
	iface->generate = writer_generate;
}
static void
writer_finalize(GObject *object){
	Writer *self = (Writer *)object;
	g_ptr_array_unref(self->art);
	g_ptr_array_unref(self->plans);
	g_mutex_clear(&self->lock);
	G_OBJECT_CLASS(writer_parent_class)->finalize(object);
}
static void
writer_class_init(WriterClass *klass){
	G_OBJECT_CLASS(klass)->finalize = writer_finalize;
}
static void
writer_init(Writer *self){
	g_mutex_init(&self->lock);
	self->art = g_ptr_array_new_with_free_func(g_free);
	self->plans = g_ptr_array_new_with_free_func(g_free);
}

/* A heading's pattern: the business's own words turned back into slots. */
static gchar *
pattern(JsonObject *brief, const gchar *heading){
	JsonArray *services = json_object_get_array_member(brief, "services");
	gchar *text = g_strdup(heading);
	text = replace(text, json_object_get_string_member(brief, "name"), "{{name}}");
	text = replace(text, json_array_get_string_element(services, 0), "{{s1}}");
	text = replace(text, json_array_get_string_element(services, 1), "{{s2}}");
	text = replace(text, json_array_get_string_element(services, 2), "{{s3}}");
	return replace(text, json_object_get_string_member(brief, "trade"), "{{trade}}");
}

typedef struct {
	GHashTable *headlines;	/* distinct home heading patterns */
	GHashTable *subjects;	/* distinct art subjects the artist was given */
} Spread;

/* Builds every fixture brief through the plan and pages steps. */
static void
build_all(gboolean ignore, Spread *spread){
	g_autoptr(JsonNode) briefs = load("tests/fixtures/sameness/briefs.json");
	JsonArray *list = json_node_get_array(briefs);
	guint i, a;
	spread->headlines = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	spread->subjects = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	for (i = 0; i < json_array_get_length(list); i++) {
		JsonObject *brief = json_array_get_object_element(list, i);
		g_autoptr(GError) error = NULL;
		g_autofree gchar *text = g_strdup_printf("{\"version\": 1, \"name\": \"%s\", \"site\": {\"brief\": \"%s\"},"
			" \"ai\": {\"provider\": \"claude-code\", \"retries\": 0}, \"pipeline\": {\"steps\": ["
			" {\"id\": \"plan\", \"type\": \"plan\", \"options\": {\"sector\": \"trades\"}},"
			" {\"id\": \"pages\", \"type\": \"pages\", \"options\": {\"parallel\": 6}}]}}",
			json_object_get_string_member(brief, "slug"), json_object_get_string_member(brief, "brief"));
		g_autoptr(LsRecipe) recipe = ls_recipe_new(text, &error);
		g_autoptr(LsPrompts) prompts = ls_prompts_new(NULL, &error);
		g_autoptr(LsContext) context = ls_context_new();
		g_autoptr(Writer) writer = g_object_new(writer_get_type(), NULL);
		g_autoptr(LsStep) plan_step = g_object_new(LS_TYPE_PLAN_STEP, NULL);
		g_autoptr(LsStep) pages_step = g_object_new(LS_TYPE_PAGES_STEP, NULL);
		g_autoptr(GRegex) h1 = g_regex_new("<h1[^>]*>(.*?)</h1>", G_REGEX_DOTALL, 0, NULL);
		g_autoptr(GRegex) tags = g_regex_new("<[^>]+>", 0, 0, NULL);
		g_autoptr(GMatchInfo) match = NULL;
		g_autofree gchar *inner = NULL, *plain = NULL;
		g_assert_no_error(error);
		writer->brief = brief;
		writer->ignore = ignore;
		if (!ls_step_execute(plan_step, recipe, json_array_get_object_element(ls_recipe_get_steps(recipe), 0),
			prompts, context, LS_PROVIDER(writer), NULL, &error))
			g_error("%s", error != NULL ? error->message : "(no error)");
		g_assert_true(ls_step_execute(pages_step, recipe, json_array_get_object_element(ls_recipe_get_steps(recipe), 1),
			prompts, context, LS_PROVIDER(writer), NULL, &error));
		g_assert_no_error(error);
		/* The plan writer is told both, and never to draw the stock house. */
		g_assert_nonnull(strstr(g_ptr_array_index(writer->plans, 0), "\"headline_angle\":{\"name\":\""));
		g_assert_nonnull(strstr(g_ptr_array_index(writer->plans, 0), "\"art_subject\":{\"name\":\""));
		g_assert_true(g_regex_match(h1, ls_artifact_get_content(ls_context_lookup(context, "index.html")), 0, &match));
		inner = g_match_info_fetch(match, 1);
		plain = g_regex_replace_literal(tags, inner, -1, 0, "", 0, NULL);
		g_hash_table_add(spread->headlines, pattern(brief, g_strstrip(plain)));
		/* Every artwork but the mark is told the site's subject. */
		for (a = 0; a < writer->art->len; a++) {
			const gchar *prompt = g_ptr_array_index(writer->art, a);
			g_autofree gchar *subject = task_value(prompt, "\"subject\":\"");
			if (strstr(prompt, "\"path\":\"assets/mark.svg\"") != NULL) {
				g_assert_null(subject);
				continue;
			}
			g_assert_nonnull(subject);
			g_hash_table_add(spread->subjects, g_steal_pointer(&subject));
		}
	}
}

/* Ten businesses get at least six headline patterns and six art subjects
 * when the writer follows what it is told; a writer that ignores the
 * angle gives one pattern, which is what the check exists to catch. */
static void
test_spread(void){
	Spread steered, ignored;
	build_all(FALSE, &steered);
	build_all(TRUE, &ignored);
	g_test_message("headline patterns %u (ignored: %u), art subjects %u", g_hash_table_size(steered.headlines),
		g_hash_table_size(ignored.headlines), g_hash_table_size(steered.subjects));
	g_assert_cmpuint(g_hash_table_size(steered.headlines), >=, HEADLINE_LIMIT);
	g_assert_cmpuint(g_hash_table_size(steered.subjects), >=, DISTINCT_LIMIT);
	g_assert_cmpuint(g_hash_table_size(ignored.headlines), ==, 1);
	g_hash_table_unref(steered.headlines);
	g_hash_table_unref(steered.subjects);
	g_hash_table_unref(ignored.headlines);
	g_hash_table_unref(ignored.subjects);
}

/* The deal: same seed, same angle and subject; plans carry them in the
 * builder's structure and the plan check accepts only catalog values. */
static void
test_deal(void){
	g_autoptr(GError) error = NULL;
	g_autoptr(LsComponents) components = ls_components_new(&error);
	g_autoptr(JsonObject) first = NULL, second = NULL;
	g_autoptr(JsonNode) plan = load("tests/fixtures/composed/plan.json");
	JsonObject *structure;
	g_assert_no_error(error);
	first = ls_components_suggest_angles(components, "angle\nbrief");
	second = ls_components_suggest_angles(components, "angle\nbrief");
	g_assert_cmpstr(json_object_get_string_member(json_object_get_object_member(first, "headline"), "name"), ==,
		json_object_get_string_member(json_object_get_object_member(second, "headline"), "name"));
	g_assert_nonnull(json_object_get_string_member(json_object_get_object_member(first, "subject"), "description"));
	g_assert_true(ls_components_apply_seed(components, plan, "angle\nbrief", "trades", FALSE));
	structure = json_object_get_object_member(json_node_get_object(plan), "structure");
	g_assert_cmpstr(json_object_get_string_member(structure, "angle"), ==,
		json_object_get_string_member(json_object_get_object_member(first, "headline"), "name"));
	g_assert_cmpstr(json_object_get_string_member(structure, "subject"), ==,
		json_object_get_string_member(json_object_get_object_member(first, "subject"), "name"));
	g_assert_true(ls_components_check_plan(components, plan, &error));
	g_assert_no_error(error);
	json_object_set_string_member(structure, "angle", "shouting");
	g_assert_false(ls_components_check_plan(components, plan, &error));
	g_assert_nonnull(strstr(error->message, "structure.angle"));
}

int
main(int argc, char **argv){
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/content-angles/deal", test_deal);
	g_test_add_func("/content-angles/spread", test_spread);
	return g_test_run();
}
