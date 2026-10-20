/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* Component variants: services, reviews, process, questions, calls to
 * action, contact and galleries each have at least three looks, every one
 * dealt per seed, with the extra ones in their own files. See
 * docs/components.org, "Component variants". */
#include "libresite.h"
#include "core/ls-components.h"
#include "core/ls-util.h"
#include <string.h>

/* An extra variant's styles stay this small. */
#define VARIANT_CSS_LIMIT 2048

static const gchar *const covered[] = {"services", "voices", "steps", "faq", "cta", "contact-card", "gallery", NULL};

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
catalog(void){
	static JsonNode *node = NULL;
	if (node == NULL) {
		g_autofree gchar *text = NULL;
		g_assert_true(g_file_get_contents("data/components/catalog.yaml", &text, NULL, NULL));
		node = ls_yaml_parse(text, NULL);
		g_assert_nonnull(node);
	}
	return json_node_get_object(node);
}

static JsonObject *
extra(void){
	return json_object_get_object_member(catalog(), "extra_variants");
}

/* Content for one section of @component, valid for every variant. */
static const gchar *
content_for(const gchar *component){
	if (g_str_equal(component, "services"))
		return "{'heading':'What we do','items':[{'name':'Chair repair','summary':'Loose joints made firm.'},"
			"{'name':'Refinishing','summary':'Old finish off, new finish on.'},{'name':'Upholstery','summary':'New seats.'}]}";
	if (g_str_equal(component, "voices"))
		return "{'heading':'What people say','items':[{'quote':'The chair is better than new.','name':'A customer'},"
			"{'quote':'Careful, quick and honest.','name':'Another customer','role':'Homeowner'},"
			"{'quote':'They explained every step.','name':'A third customer'}]}";
	if (g_str_equal(component, "steps"))
		return "{'heading':'How it works','items':[{'title':'Describe the piece','body':'Tell us what is wrong.'},"
			"{'title':'Get an estimate','body':'We reply in writing.'},{'title':'We repair it','body':'Then you collect it.'}]}";
	if (g_str_equal(component, "faq"))
		return "{'heading':'Questions','items':[{'question':'Do you give estimates?','answer':'Yes, in writing.'},"
			"{'question':'How long does it take?','answer':'Most repairs take two weeks.'}]}";
	if (g_str_equal(component, "cta"))
		return "{'heading':'Ready to start?','body':'Tell us about the piece.','actions':[{'href':'start.html','label':'Start a repair'}]}";
	if (g_str_equal(component, "contact-card"))
		return "{'heading':'Get in touch','body':'We answer within a day.','items':[{'label':'Email','value':'hello@example.com',"
			"'link':{'href':'mailto:hello@example.com','label':'hello@example.com'}},{'label':'Workshop','value':'Open by appointment'}]}";
	return "{'heading':'Recent work','items':[{'art':{'src':'assets/bench.svg','alt':'A bench'},'caption':'A bench'},"
		"{'art':{'src':'assets/bench.svg','alt':'A table'},'caption':'A table'},{'art':{'src':'assets/bench.svg','alt':'A chair'}}]}";
}

static gchar *
quotes(const gchar *text){
	return g_strdelimit(g_strdup(text), "'", '"');
}

/* Every covered component has at least three variants once the extras
 * join, and each extra is new, described and has its own small file
 * that colours only with the site's tokens and moves nothing. */
static void
test_catalog(void){
	JsonObject *components = json_object_get_object_member(catalog(), "components");
	guint i;
	for (i = 0; covered[i] != NULL; i++) {
		JsonObject *own = json_object_get_object_member(json_object_get_object_member(components, covered[i]), "variants");
		JsonObject *added = json_object_get_object_member(extra(), covered[i]);
		g_autoptr(GList) names = added != NULL ? json_object_get_members(added) : NULL;
		GList *name;
		g_assert_nonnull(added);
		g_assert_cmpuint(json_object_get_size(own) + json_object_get_size(added), >=, 3);
		for (name = names; name != NULL; name = name->next) {
			g_autofree gchar *path = g_strdup_printf("data/components/%s--%s.css", covered[i], (const gchar *)name->data);
			g_autofree gchar *css = NULL, *selector = g_strdup_printf(".%s--%s", covered[i], (const gchar *)name->data);
			g_auto(GStrv) lines = NULL;
			gsize size = 0;
			guint l;
			g_assert_false(json_object_has_member(own, name->data));
			g_assert_nonnull(json_object_get_string_member(added, name->data));
			g_assert_true(g_file_get_contents(path, &css, &size, NULL));
			g_assert_cmpuint(size, <=, VARIANT_CSS_LIMIT);
			g_assert_nonnull(strstr(css, selector));
			g_assert_null(strstr(css, "animation"));
			g_assert_null(strstr(css, "transition"));
			lines = g_strsplit(css, "\n", -1);
			for (l = 0; lines[l] != NULL; l++)
				if (strstr(lines[l], "mask") == NULL && (strstr(lines[l], "#") != NULL || strstr(lines[l], "rgb(") != NULL))
					g_error("%s colours outside the site's tokens: %s", path, lines[l]);
		}
	}
}

/* Each extra variant renders: a page naming it passes the page check,
 * the section carries its class, and its styles come with it and only
 * with it. */
static void
test_render(void){
	g_autoptr(LsComponents) components = library();
	const gchar *output = g_getenv("VARIANTS_OUTPUT");
	guint i;
	for (i = 0; covered[i] != NULL; i++) {
		JsonObject *added = json_object_get_object_member(extra(), covered[i]);
		g_autoptr(GList) names = json_object_get_members(added);
		GList *name;
		for (name = names; name != NULL; name = name->next) {
			const gchar *routes[] = {"work", "start", "journal", "visit"};
			g_autoptr(JsonNode) plan = load("tests/fixtures/composed/plan.json");
			g_autoptr(JsonObject) pages = json_object_new();
			g_autoptr(GError) error = NULL;
			g_autofree gchar *content = quotes(content_for(covered[i]));
			g_autofree gchar *page_text = g_strdup_printf("{\"path\":\"index.html\",\"sections\":[{\"component\":\"hero\","
				"\"variant\":\"statement\",\"content\":{\"heading\":\"Dependable furniture restoration\"}},"
				"{\"component\":\"%s\",\"variant\":\"%s\",\"content\":%s}]}", covered[i], (const gchar *)name->data, content);
			g_autoptr(JsonNode) page = ls_json_parse(page_text, &error);
			g_autofree gchar *text = NULL, *marker = NULL, *selector = NULL;
			g_autoptr(JsonNode) bundle = NULL;
			JsonArray *files;
			const gchar *index = NULL, *css = NULL;
			guint f, r;
			g_assert_no_error(error);
			/* No dealt family: the section renders the variant it names. */
			if (json_object_has_member(json_node_get_object(plan), "structure"))
				json_object_remove_member(json_node_get_object(plan), "structure");
			g_assert_true(ls_components_check_page(components, plan, "index.html", page, &error));
			g_assert_no_error(error);
			json_object_set_member(pages, "index.html", json_node_ref(page));
			for (r = 0; r < G_N_ELEMENTS(routes); r++) {
				g_autofree gchar *fixture = g_strdup_printf("tests/fixtures/composed/%s.json", routes[r]);
				g_autofree gchar *route = g_strdup_printf("%s.html", routes[r]);
				json_object_set_member(pages, route, load(fixture));
			}
			text = ls_components_compose(components, plan, pages, NULL, &error);
			g_assert_no_error(error);
			bundle = ls_json_parse(text, NULL);
			files = json_object_get_array_member(json_node_get_object(bundle), "files");
			for (f = 0; f < json_array_get_length(files); f++) {
				JsonObject *file = json_array_get_object_element(files, f);
				if (g_strcmp0(json_object_get_string_member(file, "path"), "index.html") == 0)
					index = json_object_get_string_member(file, "content");
				if (g_strcmp0(json_object_get_string_member(file, "path"), "styles.css") == 0)
					css = json_object_get_string_member(file, "content");
			}
			marker = g_strdup_printf("%s %s--%s", covered[i], covered[i], (const gchar *)name->data);
			selector = g_strdup_printf(".%s--%s ", covered[i], (const gchar *)name->data);
			g_assert_nonnull(strstr(index, marker));
			g_assert_nonnull(strstr(css, selector));
			/* The other extras of the component stay out. */
			{
				GList *other;
				for (other = names; other != NULL; other = other->next) {
					g_autofree gchar *absent = g_strdup_printf(".%s--%s ", covered[i], (const gchar *)other->data);
					if (other != name)
						g_assert_null(strstr(css, absent));
				}
			}
			if (output != NULL) {
				g_autofree gchar *site = g_strdup_printf("%s--%s", covered[i], (const gchar *)name->data);
				for (f = 0; f < json_array_get_length(files); f++) {
					JsonObject *file = json_array_get_object_element(files, f);
					g_autofree gchar *out = g_build_filename(output, site, json_object_get_string_member(file, "path"), NULL);
					g_autofree gchar *parent = g_path_get_dirname(out);
					g_assert_cmpint(g_mkdir_with_parents(parent, 0700), ==, 0);
					g_assert_true(g_file_set_contents(out, json_object_get_string_member(file, "content"), -1, NULL));
				}
				{
					g_autofree gchar *svg = NULL;
					g_autofree gchar *out = g_build_filename(output, site, "assets", "bench.svg", NULL);
					g_autofree gchar *parent = g_path_get_dirname(out);
					g_assert_true(g_file_get_contents("tests/fixtures/composed/bench.svg", &svg, NULL, NULL));
					g_assert_cmpint(g_mkdir_with_parents(parent, 0700), ==, 0);
					g_assert_true(g_file_set_contents(out, svg, -1, NULL));
				}
			}
		}
	}
}

/* Chosen per seed: across sites the builder deals every variant of every
 * covered component, the extras included, and the model may name them. */
static void
test_dealt(void){
	g_autoptr(LsComponents) components = library();
	g_autoptr(GHashTable) dealt = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	g_autofree gchar *reference = ls_components_describe(components);
	guint i, s;
	for (s = 0; s < 400; s++) {
		g_autofree gchar *seed = g_strdup_printf("variant-%u\nA business %u.", s, s);
		g_autoptr(JsonObject) structure = ls_components_deal_structure(components, seed);
		JsonObject *variants = json_object_get_object_member(structure, "variants");
		for (i = 0; covered[i] != NULL; i++)
			g_hash_table_add(dealt, g_strdup_printf("%s/%s", covered[i], json_object_get_string_member(variants, covered[i])));
	}
	for (i = 0; covered[i] != NULL; i++) {
		JsonObject *added = json_object_get_object_member(extra(), covered[i]);
		g_autoptr(GList) names = json_object_get_members(added);
		GList *name;
		for (name = names; name != NULL; name = name->next) {
			g_autofree gchar *pair = g_strdup_printf("%s/%s", covered[i], (const gchar *)name->data);
			if (!g_hash_table_contains(dealt, pair))
				g_error("%s is never dealt", pair);
			g_assert_nonnull(strstr(reference, json_object_get_string_member(added, name->data)));
		}
	}
}

int
main(int argc, char **argv){
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/variants/catalog", test_catalog);
	g_test_add_func("/variants/render", test_render);
	g_test_add_func("/variants/dealt", test_dealt);
	return g_test_run();
}
