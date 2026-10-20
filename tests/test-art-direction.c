/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* Art direction: the builder deals each unthemed site one way of drawing
 * and showing its artwork (duotone, cut-out shapes, framed, collage or line
 * art), which sets the drawing style and image treatment, briefs the
 * artist and styles the drawn artwork. Supplied photographs keep their own
 * treatment. See docs/components.org, "Art direction". */
#include "libresite.h"
#include "core/ls-components.h"
#include "core/ls-util.h"
#include <string.h>

#define DIRECTION_CSS_LIMIT 3072

static LsComponents *
library(void){
	g_autoptr(GError) error = NULL;
	LsComponents *components = ls_components_new(&error);
	g_assert_no_error(error);
	return components;
}

static JsonNode *
load(const gchar *path){
	g_autofree gchar *text = NULL;
	g_assert_true(g_file_get_contents(path, &text, NULL, NULL));
	return ls_json_parse(text, NULL);
}

static JsonObject *
directions(void){
	static JsonNode *node = NULL;
	if (node == NULL) {
		g_autofree gchar *text = NULL;
		g_assert_true(g_file_get_contents("data/components/catalog.yaml", &text, NULL, NULL));
		node = ls_yaml_parse(text, NULL);
		g_assert_nonnull(node);
	}
	return json_object_get_object_member(json_object_get_object_member(json_node_get_object(node), "art_directions"),
		"directions");
}

static JsonObject *
member(JsonNode *plan, const gchar *name){
	return json_object_get_object_member(json_node_get_object(plan), name);
}

static gboolean
listed(JsonArray *list, const gchar *value){
	guint i;
	for (i = 0; i < json_array_get_length(list); i++)
		if (g_strcmp0(json_array_get_string_element(list, i), value) == 0)
			return TRUE;
	return FALSE;
}

/* The five the issue names, each dealt across sites with its own drawing
 * style and image treatment, the same seed always the same; themes keep
 * their own; the plan check refuses an unknown direction. */
static void
test_deal(void){
	g_autoptr(LsComponents) components = library();
	g_autoptr(GHashTable) dealt = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	const gchar *named[] = {"duotone", "cutout", "framed", "collage", "lineart"};
	guint i;
	for (i = 0; i < G_N_ELEMENTS(named); i++)
		g_assert_true(json_object_has_member(directions(), named[i]));
	for (i = 0; i < 300; i++) {
		g_autofree gchar *seed = g_strdup_printf("drawn-%u\nAn illustrative business %u.", i, i);
		g_autoptr(JsonNode) plan = load("tests/fixtures/composed/plan.json");
		g_autoptr(JsonNode) again = load("tests/fixtures/composed/plan.json");
		g_autoptr(GError) error = NULL;
		g_autofree gchar *first = NULL, *second = NULL;
		JsonObject *direction, *look;
		const gchar *name;
		g_autoptr(GList) axes = NULL;
		GList *axis;
		g_assert_true(ls_components_apply_seed(components, plan, seed, "trades", FALSE));
		g_assert_true(ls_components_apply_seed(components, again, seed, "trades", FALSE));
		first = ls_json_encode(plan);
		second = ls_json_encode(again);
		g_assert_cmpstr(first, ==, second);
		name = json_object_get_string_member(member(plan, "structure"), "direction");
		direction = json_object_get_object_member(directions(), name);
		g_assert_nonnull(direction);
		look = json_object_get_object_member(direction, "look");
		axes = json_object_get_members(look);
		for (axis = axes; axis != NULL; axis = axis->next)
			g_assert_true(listed(json_object_get_array_member(look, axis->data),
				json_object_get_string_member(member(plan, "look"), axis->data)));
		/* Photographs are the customer's: no direction sets their treatment. */
		g_assert_false(json_object_has_member(look, "photos"));
		g_assert_true(ls_components_check_plan(components, plan, &error));
		g_assert_no_error(error);
		g_hash_table_add(dealt, g_strdup(name));
	}
	g_assert_cmpuint(g_hash_table_size(dealt), ==, json_object_get_size(directions()));
	{
		g_autoptr(JsonNode) plan = load("tests/fixtures/composed/plan.json");
		g_autoptr(GError) error = NULL;
		g_assert_true(ls_components_apply_theme(components, plan, "swiss", &error));
		g_assert_no_error(error);
		g_assert_true(ls_components_apply_seed(components, plan, "themed\nsite", NULL, TRUE));
		g_assert_false(json_object_has_member(member(plan, "structure"), "direction"));
	}
	{
		g_autoptr(JsonNode) plan = load("tests/fixtures/composed/plan.json");
		g_autoptr(GError) error = NULL;
		g_assert_true(ls_components_apply_seed(components, plan, "bad\ndirection", NULL, FALSE));
		json_object_set_string_member(member(plan, "structure"), "direction", "watercolour");
		g_assert_false(ls_components_check_plan(components, plan, &error));
		g_assert_nonnull(strstr(error->message, "structure.direction"));
	}
}

/* Each direction's styles touch only drawn artwork (SVG), never the brand
 * mark or photographs, colour only with tokens, move nothing and stay
 * small; each briefs the artist. */
static void
test_files(void){
	g_autoptr(LsComponents) components = library();
	g_autoptr(GList) names = json_object_get_members(directions());
	GList *name;
	for (name = names; name != NULL; name = name->next) {
		JsonObject *direction = json_object_get_object_member(directions(), name->data);
		g_autofree gchar *path = g_strdup_printf("data/components/art-%s.css", (const gchar *)name->data);
		g_autofree gchar *css = NULL;
		g_auto(GStrv) lines = NULL;
		gsize size = 0;
		guint l;
		g_assert_nonnull(json_object_get_string_member(direction, "summary"));
		g_assert_cmpstr(ls_components_get_direction_brief(components, name->data), ==,
			json_object_get_string_member(direction, "brief"));
		g_assert_true(g_file_get_contents(path, &css, &size, NULL));
		g_assert_cmpuint(size, <=, DIRECTION_CSS_LIMIT);
		g_assert_null(strstr(css, "animation"));
		g_assert_null(strstr(css, "transition"));
		lines = g_strsplit(css, "\n", -1);
		for (l = 0; lines[l] != NULL; l++) {
			if (strstr(lines[l], "img") != NULL && strstr(lines[l], "{") != NULL && strstr(lines[l], "/*") == NULL &&
				strstr(lines[l], "[src$=\".svg\"]") == NULL)
				g_error("%s styles images other than drawn artwork: %s", path, lines[l]);
			if (strstr(lines[l], "mask") == NULL && (strstr(lines[l], "#") != NULL || strstr(lines[l], "rgb(") != NULL))
				g_error("%s colours outside the site's tokens: %s", path, lines[l]);
		}
	}
	g_assert_null(ls_components_get_direction_brief(components, "watercolour"));
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

/* Rendered, the direction is site-wide: every page carries its class and
 * the styles come only with the dealt direction. */
static void
test_render(void){
	g_autoptr(LsComponents) components = library();
	g_autoptr(GList) names = json_object_get_members(directions());
	const gchar *output = g_getenv("ART_OUTPUT");
	GList *name;
	for (name = names; name != NULL; name = name->next) {
		const gchar *routes[] = {"index", "work", "start", "journal", "visit"};
		g_autoptr(JsonNode) plan = load("tests/fixtures/composed/plan.json");
		g_autoptr(JsonObject) pages = json_object_new();
		g_autoptr(JsonNode) bundle = NULL;
		g_autoptr(GError) error = NULL;
		g_autofree gchar *text = NULL, *wanted = NULL, *marker = NULL;
		g_autoptr(GList) others = json_object_get_members(directions());
		GList *other;
		guint i;
		g_assert_true(ls_components_apply_seed(components, plan, "render\nart", NULL, FALSE));
		json_object_set_string_member(member(plan, "structure"), "direction", name->data);
		for (i = 0; i < G_N_ELEMENTS(routes); i++) {
			g_autofree gchar *fixture = g_strdup_printf("tests/fixtures/composed/%s.json", routes[i]);
			g_autofree gchar *route = g_strdup_printf("%s.html", routes[i]);
			json_object_set_member(pages, route, load(fixture));
		}
		text = ls_components_compose(components, plan, pages, NULL, &error);
		g_assert_no_error(error);
		bundle = ls_json_parse(text, NULL);
		wanted = g_strdup_printf(" art-%s", (const gchar *)name->data);
		marker = g_strdup_printf(".art-%s ", (const gchar *)name->data);
		g_assert_nonnull(strstr(bundle_file(bundle, "index.html"), wanted));
		g_assert_nonnull(strstr(bundle_file(bundle, "work.html"), wanted));
		g_assert_nonnull(strstr(bundle_file(bundle, "styles.css"), marker));
		for (other = others; other != NULL; other = other->next) {
			g_autofree gchar *absent = g_strdup_printf(".art-%s ", (const gchar *)other->data);
			if (!g_str_equal(other->data, name->data))
				g_assert_null(strstr(bundle_file(bundle, "styles.css"), absent));
		}
		if (output != NULL) {
			JsonArray *files = json_object_get_array_member(json_node_get_object(bundle), "files");
			const gchar *art[] = {"bench.svg", "mark.svg"};
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
				g_autofree gchar *svg = NULL, *parent = g_path_get_dirname(out);
				g_assert_true(g_file_get_contents(source, &svg, NULL, NULL));
				g_assert_cmpint(g_mkdir_with_parents(parent, 0700), ==, 0);
				g_assert_true(g_file_set_contents(out, svg, -1, NULL));
			}
		}
	}
}

/* A model that answers from the sameness fixtures and keeps every
 * artwork prompt it is sent. */
typedef struct {
	GObject parent_instance;
	GMutex lock;
	GPtrArray *art;	/* artwork prompts */
} Recorder;
typedef struct { GObjectClass parent_class; } RecorderClass;
static void recorder_iface_init(LsProviderInterface *iface);
GType recorder_get_type(void);
G_DEFINE_TYPE_WITH_CODE(Recorder, recorder, G_TYPE_OBJECT, G_IMPLEMENT_INTERFACE(LS_TYPE_PROVIDER, recorder_iface_init))
G_DEFINE_AUTOPTR_CLEANUP_FUNC(Recorder, g_object_unref)

static gchar *
fixture_text(const gchar *name){
	g_autofree gchar *path = g_strconcat("tests/fixtures/sameness/", name, NULL);
	gchar *text = NULL, **parts;
	const gchar *from[] = {"{{name}}", "{{trade}}", "{{s1}}", "{{s2}}", "{{s3}}"};
	const gchar *to[] = {"Fernhill Plumbing", "plumbing", "Leak repair", "Drain clearing", "Water heaters"};
	guint i;
	g_assert_true(g_file_get_contents(path, &text, NULL, NULL));
	for (i = 0; i < G_N_ELEMENTS(from); i++) {
		parts = g_strsplit(text, from[i], -1);
		g_free(text);
		text = g_strjoinv(to[i], parts);
		g_strfreev(parts);
	}
	return text;
}

static gchar *
recorder_generate(LsProvider *provider, JsonObject *settings, const gchar *system, const gchar *prompt,
	GCancellable *cancellable, GError **error){
	Recorder *self = (Recorder *)provider;
	const gchar *schema = ls_string(settings, "json_schema");
	const gchar *task, *route, *end;
	(void)system;
	(void)cancellable;
	(void)error;
	if (schema != NULL && strstr(schema, "\"nav\"") != NULL)
		return fixture_text("plan.json");
	if (schema != NULL && strstr(schema, "\"required\": [\"css\"]") != NULL)
		return g_strdup("{\"css\":\".em { color: var(--accent-text); }\"}");
	if (schema != NULL && strstr(schema, "\"svg\"") != NULL) {
		g_autofree gchar *svg = fixture_text("art.svg");
		g_autoptr(JsonNode) node = json_node_new(JSON_NODE_OBJECT);
		JsonObject *object = json_object_new();
		g_mutex_lock(&self->lock);
		g_ptr_array_add(self->art, g_strdup(prompt));
		g_mutex_unlock(&self->lock);
		json_object_set_string_member(object, "svg", svg);
		json_node_take_object(node, object);
		return ls_json_encode(node);
	}
	task = strstr(prompt, "TASK (JSON)");
	route = task != NULL ? strstr(task, "\"route\":\"") : NULL;
	g_assert_nonnull(route);
	route += strlen("\"route\":\"");
	end = strstr(route, ".html");
	{
		g_autofree gchar *name = g_strndup(route, (gsize)(end - route));
		g_autofree gchar *file = g_strconcat(name, ".json", NULL);
		return fixture_text(file);
	}
}

static void
recorder_iface_init(LsProviderInterface *iface){
	iface->generate = recorder_generate;
}
static void
recorder_finalize(GObject *object){
	Recorder *self = (Recorder *)object;
	g_ptr_array_unref(self->art);
	g_mutex_clear(&self->lock);
	G_OBJECT_CLASS(recorder_parent_class)->finalize(object);
}
static void
recorder_class_init(RecorderClass *klass){
	G_OBJECT_CLASS(klass)->finalize = recorder_finalize;
}
static void
recorder_init(Recorder *self){
	g_mutex_init(&self->lock);
	self->art = g_ptr_array_new_with_free_func(g_free);
}

/* Every artwork is briefed with the site's direction, beside its style. */
static void
test_artist(void){
	g_autoptr(GError) error = NULL;
	g_autoptr(LsRecipe) recipe = ls_recipe_new("{\"version\": 1, \"name\": \"fernhill-plumbing\", \"site\": {\"brief\":"
		" \"An illustrative plumber.\"}, \"ai\": {\"provider\": \"claude-code\", \"retries\": 0}, \"pipeline\":"
		" {\"steps\": [{\"id\": \"plan\", \"type\": \"plan\", \"options\": {\"sector\": \"trades\"}},"
		" {\"id\": \"pages\", \"type\": \"pages\", \"options\": {\"parallel\": 4}}]}}", &error);
	g_autoptr(LsPrompts) prompts = ls_prompts_new(NULL, &error);
	g_autoptr(LsContext) context = ls_context_new();
	g_autoptr(Recorder) provider = g_object_new(recorder_get_type(), NULL);
	g_autoptr(LsComponents) components = library();
	g_autoptr(LsStep) plan_step = g_object_new(LS_TYPE_PLAN_STEP, NULL);
	g_autoptr(LsStep) pages_step = g_object_new(LS_TYPE_PAGES_STEP, NULL);
	g_autoptr(JsonNode) plan = NULL;
	const gchar *direction;
	g_autofree gchar *brief = NULL;
	guint i;
	g_assert_no_error(error);
	g_assert_true(ls_step_execute(plan_step, recipe, json_array_get_object_element(ls_recipe_get_steps(recipe), 0),
		prompts, context, LS_PROVIDER(provider), NULL, &error));
	g_assert_no_error(error);
	g_assert_true(ls_step_execute(pages_step, recipe, json_array_get_object_element(ls_recipe_get_steps(recipe), 1),
		prompts, context, LS_PROVIDER(provider), NULL, &error));
	g_assert_no_error(error);
	plan = ls_json_parse(ls_artifact_get_content(ls_context_lookup(context, "site/plan.json")), NULL);
	direction = json_object_get_string_member(member(plan, "structure"), "direction");
	g_assert_nonnull(direction);
	{
		g_autoptr(JsonNode) node = json_node_new(JSON_NODE_VALUE);
		g_autofree gchar *quoted = NULL;
		json_node_set_string(node, ls_components_get_direction_brief(components, direction));
		quoted = ls_json_encode(node);
		brief = g_strdup_printf("\"direction\":%s", quoted);
	}
	g_assert_cmpuint(provider->art->len, >=, 2);
	for (i = 0; i < provider->art->len; i++)
		g_assert_nonnull(strstr(g_ptr_array_index(provider->art, i), brief));
	g_assert_nonnull(strstr(ls_artifact_get_content(ls_context_lookup(context, "index.html")), "art-"));
}

int
main(int argc, char **argv){
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/art-direction/deal", test_deal);
	g_test_add_func("/art-direction/files", test_files);
	g_test_add_func("/art-direction/render", test_render);
	g_test_add_func("/art-direction/artist", test_artist);
	return g_test_run();
}
