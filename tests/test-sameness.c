/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* Sameness: how alike ten home-services sites come out. Each brief goes
 * through the real plan and pages steps against a model that gives every
 * business the same answer (the same plan, opening, sections and words with
 * only the name, trade and services swapped). Whatever still differs between
 * the ten sites is what the builder guarantees; the rest is the model's
 * choice. See docs/components.org, "Measuring sameness". */
#include "libresite.h"
#include <math.h>
#include <string.h>

#define FIXTURE "tests/fixtures/sameness/"

/* A soft guard since the taste rework (docs/components.org, "Taste"):
 * being different is not being good, so the mean, pair, order and mix
 * limits are reported, not enforced. Only clones fail: no two of the ten
 * sites may come out more alike than the clone limit. */
#define SAMENESS_CLONE_LIMIT 0.5
#define SAMENESS_MEAN_LIMIT 0.15
#define SAMENESS_PAIR_LIMIT 0.25
#define SAMENESS_ORDER_LIMIT 0.15
#define SAMENESS_MIX_LIMIT 0.25

static const gchar *const routes[] = {"index.html", "services.html", "contact.html"};

/* ---- The model that answers every business alike ---------------------- */

typedef struct {
	GObject parent_instance;
	JsonObject *brief;	/* borrowed; read on worker threads, never changed */
} SameProvider;
typedef struct { GObjectClass parent_class; } SameProviderClass;
static void same_iface_init(LsProviderInterface *iface);
GType same_provider_get_type(void);
G_DEFINE_TYPE_WITH_CODE(SameProvider, same_provider, G_TYPE_OBJECT,
	G_IMPLEMENT_INTERFACE(LS_TYPE_PROVIDER, same_iface_init))
G_DEFINE_AUTOPTR_CLEANUP_FUNC(SameProvider, g_object_unref)

static gchar *
fixture(const gchar *name){
	g_autofree gchar *path = g_strconcat(FIXTURE, name, NULL);
	gchar *text = NULL;
	g_assert_true(g_file_get_contents(path, &text, NULL, NULL));
	return text;
}

static gchar *
replace(gchar *text, const gchar *from, const gchar *to){
	gchar **parts = g_strsplit(text, from, -1);
	gchar *out = g_strjoinv(to, parts);
	g_strfreev(parts);
	g_free(text);
	return out;
}

/* The fixture answer with this business's name, trade and services. */
static gchar *
fill(JsonObject *brief, gchar *text){
	JsonArray *services = json_object_get_array_member(brief, "services");
	text = replace(text, "{{name}}", json_object_get_string_member(brief, "name"));
	text = replace(text, "{{trade}}", json_object_get_string_member(brief, "trade"));
	text = replace(text, "{{s1}}", json_array_get_string_element(services, 0));
	text = replace(text, "{{s2}}", json_array_get_string_element(services, 1));
	return replace(text, "{{s3}}", json_array_get_string_element(services, 2));
}

/* The route a page writer was asked for, from its TASK data. */
static gchar *
task_route(const gchar *prompt){
	const gchar *task = strstr(prompt, "TASK (JSON)");
	const gchar *start = task != NULL ? strstr(task, "\"route\":\"") : NULL;
	const gchar *end;
	if (start == NULL)
		return NULL;
	start += strlen("\"route\":\"");
	end = strchr(start, '"');
	return end != NULL ? g_strndup(start, (gsize)(end - start)) : NULL;
}

static gchar *
same_generate(LsProvider *provider, JsonObject *settings, const gchar *system,
	const gchar *prompt, GCancellable *cancellable, GError **error){
	SameProvider *self = (SameProvider *)provider;
	const gchar *schema = ls_string(settings, "json_schema");
	(void)system;
	(void)cancellable;
	(void)error;
	if (schema != NULL && strstr(schema, "\"nav\"") != NULL)
		return fill(self->brief, fixture("plan.json"));
	if (schema != NULL && strstr(schema, "\"required\": [\"css\"]") != NULL)
		return g_strdup("{\"css\":\".em { color: var(--accent-text); }\"}");
	if (schema != NULL && strstr(schema, "\"svg\"") != NULL) {
		g_autofree gchar *svg = fixture("art.svg");
		g_autoptr(JsonNode) node = json_node_new(JSON_NODE_OBJECT);
		JsonObject *object = json_object_new();
		json_object_set_string_member(object, "svg", svg);
		json_node_take_object(node, object);
		return ls_json_encode(node);
	}
	{
		g_autofree gchar *route = task_route(prompt);
		g_autofree gchar *name = NULL, *file = NULL;
		g_assert_nonnull(route);
		name = g_strndup(route, strlen(route) - strlen(".html"));
		file = g_strconcat(name, ".json", NULL);
		return fill(self->brief, fixture(file));
	}
}

static void
same_iface_init(LsProviderInterface *iface){
	iface->generate = same_generate;
}
static void
same_provider_class_init(SameProviderClass *klass){
	(void)klass;
}
static void
same_provider_init(SameProvider *self){
	(void)self;
}

/* ---- One built site --------------------------------------------------- */

typedef struct {
	gchar *slug;
	JsonNode *plan;			/* site/plan.json as built */
	JsonObject *fingerprint;	/* site/fingerprint.json as built */
	GPtrArray *home;		/* rendered home sections in order; the opening as component/variant */
	GHashTable *components;	/* every rendered component on the site */
	GHashTable *mix;		/* every rendered component/variant on the site */
	gchar *index;			/* the rendered home page */
} Site;

static void
site_free(Site *site){
	g_free(site->slug);
	g_clear_pointer(&site->plan, json_node_unref);
	g_clear_pointer(&site->fingerprint, json_object_unref);
	g_clear_pointer(&site->home, g_ptr_array_unref);
	g_clear_pointer(&site->components, g_hash_table_unref);
	g_clear_pointer(&site->mix, g_hash_table_unref);
	g_free(site->index);
	g_free(site);
}
G_DEFINE_AUTOPTR_CLEANUP_FUNC(Site, site_free)

static JsonArray *
load_briefs(void){
	g_autofree gchar *text = fixture("briefs.json");
	g_autoptr(JsonNode) node = ls_json_parse(text, NULL);
	g_assert_nonnull(node);
	g_assert_true(JSON_NODE_HOLDS_ARRAY(node));
	return json_array_ref(json_node_get_array(node));
}

static gchar *
recipe_for(JsonObject *brief, JsonArray *avoid){
	g_autoptr(JsonNode) node = NULL;
	g_autoptr(JsonNode) earlier = json_node_new(JSON_NODE_ARRAY);
	g_autofree gchar *avoided = NULL;
	g_autofree gchar *text = NULL;
	json_node_set_array(earlier, avoid != NULL ? avoid : json_array_new());
	avoided = ls_json_encode(earlier);
	if (avoid == NULL)
		json_array_unref(json_node_get_array(earlier));
	text = g_strdup_printf(
		"{\"version\": 1, \"name\": \"%s\", \"site\": {\"brief\": \"%s\", \"avoid\": %s},"
		" \"ai\": {\"provider\": \"claude-code\", \"retries\": 0},"
		" \"pipeline\": {\"steps\": ["
		"  {\"id\": \"plan\", \"type\": \"plan\", \"options\": {\"sector\": \"trades\"},"
		"   \"instructions\": \"Plan a home page, a services page and a contact page.\"},"
		"  {\"id\": \"pages\", \"type\": \"pages\", \"options\": {\"parallel\": 6}}]}}",
		json_object_get_string_member(brief, "slug"), json_object_get_string_member(brief, "brief"), avoided);
	/* The briefs hold no quotes or backslashes, so the text is plain JSON. */
	node = ls_json_parse(text, NULL);
	g_assert_nonnull(node);
	return g_steal_pointer(&text);
}

static gboolean
run_step(GType type, LsRecipe *recipe, guint index, LsPrompts *prompts, LsContext *context,
	LsProvider *provider, GError **error){
	g_autoptr(LsStep) step = g_object_new(type, NULL);
	JsonObject *config = json_array_get_object_element(ls_recipe_get_steps(recipe), index);
	return ls_step_execute(step, recipe, config, prompts, context, provider, NULL, error);
}

static const gchar *
text_of(LsContext *context, const gchar *path){
	const LsArtifact *artifact = ls_context_lookup(context, path);
	g_assert_nonnull(artifact);
	return ls_artifact_get_content(artifact);
}

/* Reads the sections a page actually rendered, after the builder dealt its
 * variants: section.html writes "block block--NAME NAME NAME--VARIANT". The
 * home order is read as component and variant, so the sections every trade
 * needs count as alike only in the same form at the same place. */
static void
read_sections(const gchar *html, GPtrArray *order, GHashTable *components, GHashTable *mix){
	g_autoptr(GRegex) regex = g_regex_new(
		"<section class=\"block block--([a-z0-9-]+) [a-z0-9-]+ [a-z0-9-]+--([a-z0-9-]+)", 0, 0, NULL);
	g_autoptr(GMatchInfo) match = NULL;
	g_regex_match(regex, html, 0, &match);
	while (g_match_info_matches(match)) {
		g_autofree gchar *component = g_match_info_fetch(match, 1);
		g_autofree gchar *variant = g_match_info_fetch(match, 2);
		gchar *both = g_strdup_printf("%s/%s", component, variant);
		if (order != NULL)
			g_ptr_array_add(order, g_strdup(both));
		g_hash_table_add(mix, both);
		g_hash_table_add(components, g_strdup(component));
		g_match_info_next(match, NULL);
	}
}

/* Builds one brief through the plan and pages steps. With @output set the
 * finished site is also written to @output/SLUG for the browser check. */
static Site *
build_site(JsonObject *brief, JsonArray *avoid, const gchar *output){
	g_autoptr(GError) error = NULL;
	g_autofree gchar *text = recipe_for(brief, avoid);
	g_autoptr(LsRecipe) recipe = ls_recipe_new(text, &error);
	g_autoptr(LsPrompts) prompts = NULL;
	g_autoptr(LsContext) context = ls_context_new();
	g_autoptr(SameProvider) provider = g_object_new(same_provider_get_type(), NULL);
	g_autoptr(JsonNode) fingerprint = NULL;
	Site *site = g_new0(Site, 1);
	guint i;
	g_assert_no_error(error);
	prompts = ls_prompts_new(NULL, &error);
	g_assert_no_error(error);
	provider->brief = brief;
	g_assert_true(run_step(LS_TYPE_PLAN_STEP, recipe, 0, prompts, context, LS_PROVIDER(provider), &error));
	g_assert_no_error(error);
	if (!run_step(LS_TYPE_PAGES_STEP, recipe, 1, prompts, context, LS_PROVIDER(provider), &error))
		g_error("%s", error->message);
	g_assert_no_error(error);
	site->slug = g_strdup(json_object_get_string_member(brief, "slug"));
	site->plan = ls_json_parse(text_of(context, "site/plan.json"), &error);
	g_assert_no_error(error);
	fingerprint = ls_json_parse(text_of(context, "site/fingerprint.json"), &error);
	g_assert_no_error(error);
	site->fingerprint = json_object_ref(json_node_get_object(fingerprint));
	site->home = g_ptr_array_new_with_free_func(g_free);
	site->components = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	site->mix = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	site->index = g_strdup(text_of(context, "index.html"));
	for (i = 0; i < G_N_ELEMENTS(routes); i++)
		read_sections(text_of(context, routes[i]), i == 0 ? site->home : NULL, site->components, site->mix);
	g_assert_cmpuint(site->home->len, >=, 3);
	if (output != NULL) {
		g_autofree gchar *directory = g_build_filename(output, site->slug, NULL);
		gboolean exported;
		g_assert_cmpint(g_mkdir_with_parents(output, 0700), ==, 0);
		exported = ls_context_export(context, directory, NULL, &error);
		g_assert_no_error(error);
		g_assert_true(exported);
	}
	return site;
}

/* ---- How alike two sites are, part by part (1 = the same) ------------- */

/* Share of the look axes a visitor sees that carry the same value. */
static gdouble
look_likeness(Site *a, Site *b){
	JsonObject *la = json_object_get_object_member(a->fingerprint, "look");
	JsonObject *lb = json_object_get_object_member(b->fingerprint, "look");
	g_autoptr(GList) axes = json_object_get_members(la);
	GList *axis;
	guint same = 0, shared = 0;
	for (axis = axes; axis != NULL; axis = axis->next)
		if (json_object_has_member(lb, axis->data)) {
			shared++;
			same += g_strcmp0(json_object_get_string_member(la, axis->data),
				json_object_get_string_member(lb, axis->data)) == 0;
		}
	return shared > 0 ? (gdouble)same / shared : 0;
}

/* Twice the longest common subsequence over both lengths: the same
 * sections in the same order score 1, the same sections reordered less. */
static gdouble
order_likeness(GPtrArray *a, GPtrArray *b){
	g_autofree guint *table = g_new0(guint, (a->len + 1) * (b->len + 1));
	guint i, j, width = b->len + 1;
	if (a->len + b->len == 0)
		return 1;
	for (i = 1; i <= a->len; i++)
		for (j = 1; j <= b->len; j++)
			table[i * width + j] = g_str_equal(g_ptr_array_index(a, i - 1), g_ptr_array_index(b, j - 1))
				? table[(i - 1) * width + j - 1] + 1
				: MAX(table[(i - 1) * width + j], table[i * width + j - 1]);
	return 2.0 * table[a->len * width + b->len] / (a->len + b->len);
}

/* Jaccard share of two sets of rendered names. */
static gdouble
set_likeness(GHashTable *a, GHashTable *b){
	GHashTableIter iter;
	gpointer key;
	guint shared = 0, total;
	g_hash_table_iter_init(&iter, a);
	while (g_hash_table_iter_next(&iter, &key, NULL))
		shared += g_hash_table_contains(b, key);
	total = g_hash_table_size(a) + g_hash_table_size(b) - shared;
	return total > 0 ? (gdouble)shared / total : 1;
}

static gboolean
parse_hex(const gchar *text, gdouble rgb[3]){
	guint i;
	if (text == NULL || strlen(text) != 7 || text[0] != '#')
		return FALSE;
	for (i = 0; i < 3; i++) {
		gint high = g_ascii_xdigit_value(text[1 + 2 * i]), low = g_ascii_xdigit_value(text[2 + 2 * i]);
		if (high < 0 || low < 0)
			return FALSE;
		rgb[i] = (high * 16 + low) / 255.0;
	}
	return TRUE;
}

/* Mean likeness of the colours that paint most of a page: 1 for the same
 * colour, 0 from a distance of 0.35 in unit RGB, as the library scores it. */
static gdouble
palette_likeness(Site *a, Site *b){
	const gchar *const hues[] = {"paper", "ink", "surface", "accent"};
	JsonObject *ta = json_object_get_object_member(json_node_get_object(a->plan), "tokens");
	JsonObject *tb = json_object_get_object_member(json_node_get_object(b->plan), "tokens");
	gdouble near = 0;
	guint i, c;
	for (i = 0; i < G_N_ELEMENTS(hues); i++) {
		gdouble x[3], y[3], distance = 0;
		if (!parse_hex(ls_string(ta, hues[i]), x) || !parse_hex(ls_string(tb, hues[i]), y))
			continue;
		for (c = 0; c < 3; c++)
			distance += (x[c] - y[c]) * (x[c] - y[c]);
		near += MAX(0, 1 - sqrt(distance) / 0.35);
	}
	return near / G_N_ELEMENTS(hues);
}

/* The display family counts for more than the body family. */
static gdouble
type_likeness(Site *a, Site *b){
	JsonObject *fa = json_object_get_object_member(a->fingerprint, "fonts");
	JsonObject *fb = json_object_get_object_member(b->fingerprint, "fonts");
	return 0.6 * (ls_string(fa, "display") != NULL && g_strcmp0(ls_string(fa, "display"), ls_string(fb, "display")) == 0) +
		0.4 * (ls_string(fa, "body") != NULL && g_strcmp0(ls_string(fa, "body"), ls_string(fb, "body")) == 0);
}

typedef struct {
	const gchar *name;
	gdouble weight;
} Part;

static const Part parts[] = {
	{"look", 0.25}, {"order", 0.20}, {"mix", 0.20}, {"palette", 0.20}, {"type", 0.15}
};

static void
pair_parts(Site *a, Site *b, gdouble out[G_N_ELEMENTS(parts)]){
	out[0] = look_likeness(a, b);
	out[1] = order_likeness(a->home, b->home);
	/* The same components in other variants are half alike. */
	out[2] = 0.5 * set_likeness(a->components, b->components) + 0.5 * set_likeness(a->mix, b->mix);
	out[3] = palette_likeness(a, b);
	out[4] = type_likeness(a, b);
}

static gdouble
weighted(const gdouble values[G_N_ELEMENTS(parts)]){
	gdouble score = 0;
	guint p;
	for (p = 0; p < G_N_ELEMENTS(parts); p++)
		score += parts[p].weight * values[p];
	return score;
}

/* ---- Tests ------------------------------------------------------------ */

static Site *
fake_site(const gchar *const *home, const gchar *paper, const gchar *display){
	Site *site = g_new0(Site, 1);
	g_autofree gchar *plan = g_strdup_printf("{\"tokens\": {\"paper\": \"%s\", \"ink\": \"#111111\","
		" \"surface\": \"#eeeeee\", \"accent\": \"#aa2200\"}}", paper);
	g_autofree gchar *fp = g_strdup_printf("{\"look\": {\"frame\": \"%s\", \"heads\": \"plain\"},"
		" \"fonts\": {\"display\": \"%s\", \"body\": \"inter\"}}", display, display);
	g_autoptr(JsonNode) fp_node = ls_json_parse(fp, NULL);
	guint i;
	site->plan = ls_json_parse(plan, NULL);
	site->fingerprint = json_object_ref(json_node_get_object(fp_node));
	site->home = g_ptr_array_new_with_free_func(g_free);
	site->components = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	site->mix = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	for (i = 0; home[i] != NULL; i++) {
		g_ptr_array_add(site->home, g_strdup(home[i]));
		g_hash_table_add(site->components, g_strdup(home[i]));
		g_hash_table_add(site->mix, g_strdup(home[i]));
	}
	return site;
}

/* The measures: a site is wholly like itself; reordering the same sections
 * lowers order but not mix; nothing shared scores nothing. */
static void
test_measures(void){
	const gchar *const one[] = {"hero/split", "services", "steps", "faq", NULL};
	const gchar *const reordered[] = {"hero/split", "faq", "steps", "services", NULL};
	const gchar *const other[] = {"cover/full", "gallery", "voices", NULL};
	g_autoptr(Site) a = fake_site(one, "#ffffff", "inter");
	g_autoptr(Site) b = fake_site(reordered, "#ffffff", "inter");
	g_autoptr(Site) c = fake_site(other, "#000000", "fraunces");
	gdouble values[G_N_ELEMENTS(parts)], total = 0;
	guint p;
	for (p = 0; p < G_N_ELEMENTS(parts); p++)
		total += parts[p].weight;
	g_assert_cmpfloat(fabs(total - 1), <, 1e-9);
	pair_parts(a, a, values);
	g_assert_cmpfloat(fabs(weighted(values) - 1), <, 1e-9);
	pair_parts(a, b, values);
	g_assert_cmpfloat(values[1], <, 1);
	g_assert_cmpfloat(values[1], >, 0);
	g_assert_cmpfloat(values[2], ==, 1);
	pair_parts(a, c, values);
	g_assert_cmpfloat(values[1], ==, 0);
	g_assert_cmpfloat(values[2], ==, 0);
	g_assert_cmpfloat(values[4], ==, 0.4);
	/* White and black paper share nothing; the other three colours match. */
	g_assert_cmpfloat(fabs(values[3] - 0.75), <, 1e-9);
}

/* The same business built twice comes out the same, so the score below is
 * a property of the builder and its briefs, not of chance. */
static void
test_rebuild(void){
	g_autoptr(JsonArray) briefs = load_briefs();
	g_autoptr(Site) first = build_site(json_array_get_object_element(briefs, 0), NULL, NULL);
	g_autoptr(Site) second = build_site(json_array_get_object_element(briefs, 0), NULL, NULL);
	gdouble values[G_N_ELEMENTS(parts)];
	g_assert_cmpstr(first->index, ==, second->index);
	pair_parts(first, second, values);
	g_assert_cmpfloat(fabs(weighted(values) - 1), <, 1e-9);
}

static void
add_part_means(JsonObject *object, const gdouble sums[G_N_ELEMENTS(parts)], guint pairs){
	guint p;
	for (p = 0; p < G_N_ELEMENTS(parts); p++)
		json_object_set_double_member(object, parts[p].name, sums[p] / pairs);
}

/* Builds the ten briefs and scores every pair. The score is reported every
 * run, and ten sites over either limit fail the test: the variety gate. */
static void
test_briefs(void){
	const gchar *output = g_getenv("SAMENESS_OUTPUT");
	g_autoptr(JsonArray) briefs = load_briefs();
	g_autoptr(GPtrArray) sites = g_ptr_array_new_with_free_func((GDestroyNotify)site_free);
	g_autoptr(JsonArray) avoid = json_array_new();
	g_autoptr(JsonObject) report = json_object_new();
	JsonObject *means = json_object_new();
	JsonArray *pairs = json_array_new();
	gdouble sums[G_N_ELEMENTS(parts)] = {0}, total = 0, worst = -1, library = 0;
	const gchar *worst_a = NULL, *worst_b = NULL;
	guint i, j, p, count = 0;
	g_assert_cmpuint(json_array_get_length(briefs), ==, 10);
	for (i = 0; i < json_array_get_length(briefs); i++) {
		g_ptr_array_add(sites, build_site(json_array_get_object_element(briefs, i), avoid, output));
		/* Each order in one market avoids the sites built before it, as an
		 * embedder passes them in site.avoid (docs/components.org, "Sameness"). */
		json_array_add_object_element(avoid, json_object_ref(((Site *)g_ptr_array_index(sites, i))->fingerprint));
	}
	for (i = 0; i < sites->len; i++)
		for (j = i + 1; j < sites->len; j++) {
			Site *a = g_ptr_array_index(sites, i), *b = g_ptr_array_index(sites, j);
			gdouble values[G_N_ELEMENTS(parts)], score;
			JsonObject *pair = json_object_new();
			pair_parts(a, b, values);
			score = weighted(values);
			json_object_set_string_member(pair, "a", a->slug);
			json_object_set_string_member(pair, "b", b->slug);
			for (p = 0; p < G_N_ELEMENTS(parts); p++) {
				json_object_set_double_member(pair, parts[p].name, values[p]);
				sums[p] += values[p];
			}
			json_object_set_double_member(pair, "score", score);
			json_array_add_object_element(pairs, pair);
			library += ls_components_similarity(a->fingerprint, b->fingerprint);
			total += score;
			count++;
			if (score > worst) {
				worst = score;
				worst_a = a->slug;
				worst_b = b->slug;
			}
		}
	add_part_means(means, sums, count);
	json_object_set_double_member(report, "score", total / count);
	json_object_set_double_member(report, "worst", worst);
	json_object_set_double_member(report, "fingerprint_similarity", library / count);
	json_object_set_double_member(report, "mean_limit", SAMENESS_MEAN_LIMIT);
	json_object_set_double_member(report, "pair_limit", SAMENESS_PAIR_LIMIT);
	json_object_set_double_member(report, "order_limit", SAMENESS_ORDER_LIMIT);
	json_object_set_double_member(report, "mix_limit", SAMENESS_MIX_LIMIT);
	json_object_set_double_member(report, "clone_limit", SAMENESS_CLONE_LIMIT);
	json_object_set_object_member(report, "parts", means);
	json_object_set_array_member(report, "pairs", pairs);
	g_test_message("sameness %.3f (limit %.2f), worst pair %.3f %s/%s (limit %.2f); look %.2f order %.2f "
		"mix %.2f palette %.2f type %.2f; fingerprint similarity %.3f",
		total / count, SAMENESS_MEAN_LIMIT, worst, worst_a, worst_b, SAMENESS_PAIR_LIMIT,
		sums[0] / count, sums[1] / count, sums[2] / count, sums[3] / count, sums[4] / count, library / count);
	if (output != NULL) {
		g_autoptr(JsonNode) node = json_node_new(JSON_NODE_OBJECT);
		g_autoptr(GError) error = NULL;
		g_autofree gchar *path = g_build_filename(output, "score.json", NULL);
		g_autofree gchar *text = NULL;
		json_node_set_object(node, report);
		text = ls_json_encode(node);
		g_assert_true(g_file_set_contents(path, text, -1, &error));
		g_assert_no_error(error);
	}
	if (total / count > SAMENESS_MEAN_LIMIT || worst > SAMENESS_PAIR_LIMIT ||
		sums[1] / count > SAMENESS_ORDER_LIMIT || sums[2] / count > SAMENESS_MIX_LIMIT)
		g_test_message("soft guard: over a soft limit, reported only: sameness %.3f (%.2f), worst pair %.3f (%.2f), "
			"order %.3f (%.2f), mix %.3f (%.2f)", total / count, SAMENESS_MEAN_LIMIT, worst, SAMENESS_PAIR_LIMIT,
			sums[1] / count, SAMENESS_ORDER_LIMIT, sums[2] / count, SAMENESS_MIX_LIMIT);
	if (worst <= SAMENESS_CLONE_LIMIT)
		return;
	g_test_fail_printf("two different businesses came out as clones: worst pair %.3f %s/%s (clone limit %.2f)",
		worst, worst_a, worst_b, SAMENESS_CLONE_LIMIT);
}

int
main(int argc, char **argv){
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/sameness/measures", test_measures);
	g_test_add_func("/sameness/rebuild", test_rebuild);
	g_test_add_func("/sameness/briefs", test_briefs);
	return g_test_run();
}
