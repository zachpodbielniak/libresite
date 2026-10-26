/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* Every option, composed: one site per component (all its variants, the
 * extras included), per home opening, home composition, motion kit, art
 * direction, font pairing and palette, so the browser gate
 * (tests/options.sh) can check each at 390, 768 and 1440. Here each must
 * compose and pass the page and plan checks; with OPTIONS_OUTPUT set, the
 * sites are written out. See docs/components.org, "Mobile by default". */
#include "libresite.h"
#include "core/ls-components.h"
#include "core/ls-util.h"
#include <string.h>

static JsonNode *
json(const gchar *text){
	g_autofree gchar *quoted = g_strdelimit(g_strdup(text), "'", '"');
	JsonNode *node = ls_json_parse(quoted, NULL);
	g_assert_nonnull(node);
	return node;
}

static JsonNode *
load(const gchar *path){
	g_autofree gchar *text = NULL;
	g_assert_true(g_file_get_contents(path, &text, NULL, NULL));
	return ls_json_parse(text, NULL);
}

/* Copies every member of @from into @into. */
static void
join(JsonObject *into, JsonObject *from){
	g_autoptr(GList) names = json_object_get_members(from);
	GList *name;
	for (name = names; name != NULL; name = name->next)
		json_object_set_member(into, name->data, json_node_copy(json_object_get_member(from, name->data)));
}

/* The catalog with the wider and bold blocks joined where the library
 * joins them, so every wider and bold option gets its site too. */
static JsonObject *
catalog(void){
	static JsonNode *node = NULL;
	if (node == NULL) {
		g_autofree gchar *text = NULL;
		JsonObject *root, *wider;
		g_assert_true(g_file_get_contents("data/components/catalog.yaml", &text, NULL, NULL));
		node = ls_yaml_parse(text, NULL);
		root = json_node_get_object(node);
		wider = json_object_has_member(root, "wider") ? json_object_get_object_member(root, "wider") : NULL;
		if (wider != NULL && json_object_has_member(wider, "looks")) {
			JsonObject *looks = json_object_get_object_member(wider, "looks");
			g_autoptr(GList) axes = json_object_get_members(looks);
			GList *axis;
			if (!json_object_has_member(root, "extra_looks"))
				json_object_set_object_member(root, "extra_looks", json_object_new());
			for (axis = axes; axis != NULL; axis = axis->next) {
				JsonObject *extra = json_object_get_object_member(root, "extra_looks");
				if (!json_object_has_member(extra, axis->data))
					json_object_set_object_member(extra, axis->data, json_object_new());
				join(json_object_get_object_member(extra, axis->data), json_object_get_object_member(looks, axis->data));
			}
		}
		if (wider != NULL && json_object_has_member(wider, "openings")) {
			JsonObject *openings = json_object_get_object_member(wider, "openings");
			g_autoptr(GList) owners = json_object_get_members(openings);
			GList *owner;
			for (owner = owners; owner != NULL; owner = owner->next)
				join(json_object_get_object_member(json_object_get_object_member(json_object_get_object_member(root,
					"components"), owner->data), "variants"), json_object_get_object_member(openings, owner->data));
		}
		{
			const gchar *joins[][3] = {{"layouts", "compositions", "layouts"}, {"kits", "motion_kits", "kits"},
				{"directions", "art_directions", "directions"}};
			guint j;
			for (j = 0; wider != NULL && j < G_N_ELEMENTS(joins); j++)
				if (json_object_has_member(wider, joins[j][0]))
					join(json_object_get_object_member(json_object_get_object_member(root, joins[j][1]), joins[j][2]),
						json_object_get_object_member(wider, joins[j][0]));
		}
		/* The bold block's openings and components. */
		if (json_object_has_member(root, "bold")) {
			JsonObject *bold = json_object_get_object_member(root, "bold");
			JsonObject *openings = json_object_get_object_member(bold, "openings");
			g_autoptr(GList) owners = json_object_get_members(openings);
			GList *owner;
			for (owner = owners; owner != NULL; owner = owner->next)
				join(json_object_get_object_member(json_object_get_object_member(json_object_get_object_member(root,
					"components"), owner->data), "variants"), json_object_get_object_member(openings, owner->data));
			join(json_object_get_object_member(root, "components"), json_object_get_object_member(bold, "components"));
		}
	}
	return json_node_get_object(node);
}

/* Realistic sample words, longest first, so a narrow box meets real lengths. */
static const gchar *const words[] = {"Careful repairs for older homes and the people in them",
	"Written estimates before any work starts", "Same-week visits", "Chimney flashing"};

static JsonNode *
sample_value(JsonObject *slot, guint depth){
	const gchar *type = ls_string(slot, "type");
	JsonNode *node;
	if (g_str_equal(type, "text")) {
		gint64 max = json_object_has_member(slot, "max") ? json_object_get_int_member(slot, "max") : 40;
		g_autofree gchar *text = g_strndup(words[depth % G_N_ELEMENTS(words)], MAX(max, 1));
		g_strchomp(text);
		node = json_node_new(JSON_NODE_VALUE);
		json_node_set_string(node, text);
		return node;
	}
	if (g_str_equal(type, "rich"))
		return json("'We look at the whole job first, explain what we found in plain words, and give you a written price before anything starts. Then we do the work and clean up.'");
	if (g_str_equal(type, "link"))
		return json("{'href':'work.html','label':'See recent work'}");
	if (g_str_equal(type, "links"))
		return json("[{'href':'start.html','label':'Ask for an estimate'},{'href':'visit.html','label':'Visit us'}]");
	if (g_str_equal(type, "image"))
		return json("{'src':'assets/bench.svg','alt':'A drawing of the workshop bench'}");
	if (g_str_equal(type, "choice")) {
		node = json_node_new(JSON_NODE_VALUE);
		json_node_set_string(node, json_array_get_string_element(json_object_get_array_member(slot, "values"), 0));
		return node;
	}
	if (g_str_equal(type, "flag")) {
		node = json_node_new(JSON_NODE_VALUE);
		json_node_set_boolean(node, TRUE);
		return node;
	}
	if (g_str_equal(type, "items")) {
		JsonObject *fields = json_object_get_object_member(slot, "fields");
		gint64 wanted = json_object_has_member(slot, "min_items") ? json_object_get_int_member(slot, "min_items") : 0;
		JsonArray *items = json_array_new();
		gint64 i;
		wanted = MAX(wanted, 3);
		if (json_object_has_member(slot, "max_items"))
			wanted = MIN(wanted, json_object_get_int_member(slot, "max_items"));
		for (i = 0; i < wanted; i++) {
			JsonObject *item = json_object_new();
			g_autoptr(GList) names = json_object_get_members(fields);
			GList *name;
			for (name = names; name != NULL; name = name->next)
				json_object_set_member(item, name->data,
					sample_value(json_object_get_object_member(fields, name->data), depth + (guint)i));
			json_array_add_object_element(items, item);
		}
		node = json_node_new(JSON_NODE_ARRAY);
		json_node_take_array(node, items);
		return node;
	}
	g_error("no sample for slot type %s", type);
	return NULL;
}

static JsonNode *
sample_content(const gchar *name, JsonObject *component){
	JsonObject *slots = json_object_get_object_member(component, "slots");
	g_autoptr(GList) names = NULL;
	GList *slot;
	JsonObject *content;
	JsonNode *node;
	if (g_str_equal(name, "form"))
		return json("{'heading':'Ask for an estimate','mode':'local','fields':[{'name':'job','label':'What needs doing','required':true}],"
			"'submit':'Prepare the request','download_name':'request.txt'}");
	if (g_str_equal(name, "filter"))
		return json("{'heading':'Recent work','all_label':'All','categories':[{'label':'Roofs'},{'label':'Gutters'}],"
			"'empty':'None yet.','items':[{'title':'A slate roof','category':'Roofs'},{'title':'New gutters','category':'Gutters'}]}");
	if (g_str_equal(name, "module"))
		return json("{'heading':'Write to us','module':'contact-form'}");
	if (g_str_equal(name, "custom"))
		return json("{'label':'Custom region','html':'<div><p>A custom moment of the site.</p></div>'}");
	if (g_str_equal(name, "hours"))
		return json("{'heading':'Hours','items':[{'days':'Monday to Friday','times':'8:00 to 17:00'}],"
			"'timezone':'America/New_York','open_label':'Open now','closed_label':'Closed now',"
			"'schedule':[{'day':'mon','opens':'08:00','closes':'17:00'}]}");
	content = json_object_new();
	names = json_object_get_members(slots);
	for (slot = names; slot != NULL; slot = slot->next)
		json_object_set_member(content, slot->data, sample_value(json_object_get_object_member(slots, slot->data), 0));
	node = json_node_new(JSON_NODE_OBJECT);
	json_node_take_object(node, content);
	return node;
}

/* The fixture plan, dealt from one seed and ready for an option. */
static JsonNode *
base_plan(LsComponents *components){
	JsonNode *plan = load("tests/fixtures/composed/plan.json");
	JsonObject *root = json_node_get_object(plan);
	json_object_set_member(root, "contact", json("{'phone':'+1 555 0100'}"));
	{
		JsonArray *modules = json_array_new();
		JsonObject *module = json_object_new();
		gchar *text = NULL;
		json_object_set_string_member(module, "id", "contact-form");
		g_assert_true(g_file_get_contents("tests/fixtures/podbay/contact-form.html", &text, NULL, NULL));
		json_object_set_string_member(module, "snippet", text);
		g_free(text);
		g_assert_true(g_file_get_contents("tests/fixtures/podbay/contact-form.css", &text, NULL, NULL));
		json_object_set_string_member(module, "css", text);
		g_free(text);
		json_array_add_object_element(modules, module);
		json_object_set_array_member(root, "modules", modules);
	}
	g_assert_true(ls_components_apply_seed(components, plan, "options\nEvery option at every width.", "trades", FALSE));
	/* The fixture's restyle (red tabs) tests the restyle plumbing; the
	 * browser gates judge the library's own colours. */
	if (json_object_has_member(root, "restyle"))
		json_object_remove_member(root, "restyle");
	return plan;
}

static guint written = 0;

/* Whether @home composes over @plan with the fixture's other pages. */
static gboolean
composes(LsComponents *components, JsonNode *plan, JsonNode *home){
	const gchar *routes[] = {"work", "start", "journal", "visit"};
	g_autoptr(JsonObject) pages = json_object_new();
	g_autofree gchar *text = NULL;
	guint i;
	json_object_set_member(pages, "index.html", json_node_ref(home));
	for (i = 0; i < G_N_ELEMENTS(routes); i++) {
		g_autofree gchar *fixture = g_strdup_printf("tests/fixtures/composed/%s.json", routes[i]);
		g_autofree gchar *route = g_strdup_printf("%s.html", routes[i]);
		json_object_set_member(pages, route, load(fixture));
	}
	text = ls_components_compose(components, plan, pages, NULL, NULL);
	return text != NULL;
}

/* Composes @plan with @home and the fixture's other pages and, with
 * OPTIONS_OUTPUT, writes it as OPTIONS_OUTPUT/NAME. */
static void
emit(LsComponents *components, JsonNode *plan, JsonNode *home, const gchar *name){
	const gchar *routes[] = {"work", "start", "journal", "visit"};
	const gchar *output = g_getenv("OPTIONS_OUTPUT");
	g_autoptr(JsonObject) pages = json_object_new();
	g_autoptr(GError) error = NULL;
	g_autofree gchar *text = NULL;
	g_autoptr(JsonNode) bundle = NULL;
	JsonArray *files;
	guint i;
	json_object_set_member(pages, "index.html", json_node_ref(home));
	for (i = 0; i < G_N_ELEMENTS(routes); i++) {
		g_autofree gchar *fixture = g_strdup_printf("tests/fixtures/composed/%s.json", routes[i]);
		g_autofree gchar *route = g_strdup_printf("%s.html", routes[i]);
		json_object_set_member(pages, route, load(fixture));
	}
	if (!ls_components_check_plan(components, plan, &error))
		g_error("%s: %s", name, error->message);
	text = ls_components_compose(components, plan, pages, NULL, &error);
	if (text == NULL)
		g_error("%s: %s", name, error->message);
	written++;
	if (output == NULL)
		return;
	bundle = ls_json_parse(text, NULL);
	files = json_object_get_array_member(json_node_get_object(bundle), "files");
	for (i = 0; i < json_array_get_length(files); i++) {
		JsonObject *file = json_array_get_object_element(files, i);
		g_autofree gchar *out = g_build_filename(output, name, json_object_get_string_member(file, "path"), NULL);
		g_autofree gchar *parent = g_path_get_dirname(out);
		g_assert_cmpint(g_mkdir_with_parents(parent, 0700), ==, 0);
		g_assert_true(g_file_set_contents(out, json_object_get_string_member(file, "content"), -1, NULL));
	}
	for (i = 0; i < 2; i++) {
		const gchar *art = i == 0 ? "bench.svg" : "mark.svg";
		g_autofree gchar *source = g_build_filename("tests/fixtures/composed", art, NULL);
		g_autofree gchar *out = g_build_filename(output, name, "assets", art, NULL);
		g_autofree gchar *svg = NULL, *parent = g_path_get_dirname(out);
		g_assert_true(g_file_get_contents(source, &svg, NULL, NULL));
		g_assert_cmpint(g_mkdir_with_parents(parent, 0700), ==, 0);
		g_assert_true(g_file_set_contents(out, svg, -1, NULL));
	}
}

static const gchar *const openings[] = {"hero", "cover", "poster", "manifesto", NULL};

/* One home page per component, every variant a section, the variant
 * rendered as named (no dealt family). */
static void
test_components(void){
	g_autoptr(GError) error = NULL;
	g_autoptr(LsComponents) components = ls_components_new(&error);
	JsonObject *all = json_object_get_object_member(catalog(), "components");
	JsonObject *extra = json_object_get_object_member(catalog(), "extra_variants");
	g_autoptr(GList) names = json_object_get_members(all);
	GList *name;
	g_assert_no_error(error);
	for (name = names; name != NULL; name = name->next) {
		JsonObject *component = json_object_get_object_member(all, name->data);
		g_autoptr(GList) variants = json_object_get_members(json_object_get_object_member(component, "variants"));
		g_autoptr(JsonNode) plan = base_plan(components);
		g_autoptr(JsonNode) home = json("{'path':'index.html','sections':[{'component':'hero','variant':'statement',"
			"'content':{'heading':'Every variant'}}]}");
		JsonArray *sections = json_object_get_array_member(json_node_get_object(home), "sections");
		g_autofree gchar *site = g_strdup_printf("component-%s", (const gchar *)name->data);
		GList *variant;
		if (g_strv_contains(openings, name->data) || g_str_equal(name->data, "page-header"))
			continue;
		if (g_str_equal(name->data, "booking-calendar"))
			json_object_set_member(json_node_get_object(plan), "booking", json("{'href':'/book'}"));
		if (json_object_has_member(extra, name->data)) {
			g_autoptr(GList) added = json_object_get_members(json_object_get_object_member(extra, name->data));
			GList *a;
			for (a = added; a != NULL; a = a->next)
				variants = g_list_append(variants, a->data);
		}
		json_object_remove_member(json_object_get_object_member(json_node_get_object(plan), "structure"), "variants");
		for (variant = variants; variant != NULL; variant = variant->next) {
			JsonObject *section = json_object_new();
			json_object_set_string_member(section, "component", name->data);
			json_object_set_string_member(section, "variant", variant->data);
			json_object_set_member(section, "content", sample_content(name->data, component));
			json_array_add_object_element(sections, section);
		}
		if (composes(components, plan, home)) {
			emit(components, plan, home, site);
			continue;
		}
		/* A component that may appear once a page gets a site per variant. */
		for (variant = variants; variant != NULL; variant = variant->next) {
			g_autoptr(JsonNode) single = json("{'path':'index.html','sections':[{'component':'hero','variant':'statement',"
				"'content':{'heading':'One variant'}}]}");
			JsonObject *section = json_object_new();
			g_autofree gchar *one = g_strdup_printf("%s-%s", site, (const gchar *)variant->data);
			json_object_set_string_member(section, "component", name->data);
			json_object_set_string_member(section, "variant", variant->data);
			json_object_set_member(section, "content", sample_content(name->data, component));
			json_array_add_object_element(json_object_get_array_member(json_node_get_object(single), "sections"), section);
			emit(components, plan, single, one);
		}
	}
	g_assert_cmpuint(written, >=, 60);
}

/* One home page per opening variant, interior openings included. */
static void
test_openings(void){
	g_autoptr(GError) error = NULL;
	g_autoptr(LsComponents) components = ls_components_new(&error);
	JsonObject *all = json_object_get_object_member(catalog(), "components");
	guint o;
	g_assert_no_error(error);
	for (o = 0; openings[o] != NULL; o++) {
		JsonObject *component = json_object_get_object_member(all, openings[o]);
		g_autoptr(GList) variants = json_object_get_members(json_object_get_object_member(component, "variants"));
		GList *variant;
		for (variant = variants; variant != NULL; variant = variant->next) {
			g_autoptr(JsonNode) plan = base_plan(components);
			g_autoptr(JsonNode) home = load("tests/fixtures/composed/index.json");
			JsonArray *sections = json_object_get_array_member(json_node_get_object(home), "sections");
			JsonObject *first = json_object_new();
			g_autofree gchar *site = g_strdup_printf("opening-%s-%s", openings[o], (const gchar *)variant->data);
			json_object_set_string_member(first, "component", openings[o]);
			json_object_set_string_member(first, "variant", variant->data);
			json_object_set_member(first, "content", sample_content(openings[o], component));
			json_array_remove_element(sections, 0);
			{
				JsonArray *rebuilt = json_array_new();
				guint i;
				json_array_add_object_element(rebuilt, first);
				for (i = 0; i < json_array_get_length(sections); i++)
					json_array_add_element(rebuilt, json_node_copy(json_array_get_element(sections, i)));
				json_object_set_array_member(json_node_get_object(home), "sections", rebuilt);
			}
			emit(components, plan, home, site);
		}
	}
}

/* One site per dealt option: home composition, motion kit, art direction,
 * font pairing and palette, each over the fixture site. */
static void
test_dealt(void){
	g_autoptr(GError) error = NULL;
	g_autoptr(LsComponents) components = ls_components_new(&error);
	struct { const gchar *block, *list, *key; } dealt[] = {
		{"compositions", "layouts", "composition"}, {"motion_kits", "kits", "kit"}, {"art_directions", "directions", "direction"}
	};
	guint d, i;
	g_assert_no_error(error);
	for (d = 0; d < G_N_ELEMENTS(dealt); d++) {
		JsonObject *options = json_object_get_object_member(json_object_get_object_member(catalog(), dealt[d].block), dealt[d].list);
		g_autoptr(GList) names = json_object_get_members(options);
		GList *name;
		for (name = names; name != NULL; name = name->next) {
			g_autoptr(JsonNode) plan = base_plan(components);
			g_autoptr(JsonNode) home = load("tests/fixtures/composed/index.json");
			g_autofree gchar *site = g_strdup_printf("%s-%s", dealt[d].key, (const gchar *)name->data);
			json_object_set_string_member(json_object_get_object_member(json_node_get_object(plan), "structure"),
				dealt[d].key, name->data);
			ls_components_apply_composition(components, plan, home);
			emit(components, plan, home, site);
		}
	}
	/* Every extra look value. */
	{
		JsonObject *extra = json_object_get_object_member(catalog(), "extra_looks");
		g_autoptr(GList) axes = json_object_get_members(extra);
		GList *axis;
		for (axis = axes; axis != NULL; axis = axis->next) {
			g_autoptr(GList) values = json_object_get_members(json_object_get_object_member(extra, axis->data));
			GList *value;
			for (value = values; value != NULL; value = value->next) {
				g_autoptr(JsonNode) plan = base_plan(components);
				g_autoptr(JsonNode) home = load("tests/fixtures/composed/index.json");
				g_autofree gchar *site = g_strdup_printf("look-%s-%s", (const gchar *)axis->data, (const gchar *)value->data);
				json_object_set_string_member(json_object_get_object_member(json_node_get_object(plan), "look"),
					axis->data, value->data);
				emit(components, plan, home, site);
			}
		}
	}
	/* Font pairings and palettes: curated ones and generated ones. */
	{
		g_autoptr(JsonNode) identities = NULL, pairings = NULL;
		JsonObject *groups[3];
		g_autofree gchar *text = NULL;
		guint g;
		g_assert_true(g_file_get_contents("data/components/identities.yaml", &text, NULL, NULL));
		identities = ls_yaml_parse(text, NULL);
		g_free(g_steal_pointer(&text));
		g_assert_true(g_file_get_contents("data/components/pairings.yaml", &text, NULL, NULL));
		pairings = ls_yaml_parse(text, NULL);
		groups[0] = json_object_get_object_member(json_node_get_object(identities), "typefaces");
		groups[1] = json_object_get_object_member(json_node_get_object(pairings), "pairings");
		groups[2] = json_object_get_object_member(json_node_get_object(identities), "palettes");
		for (g = 0; g < G_N_ELEMENTS(groups); g++) {
			g_autoptr(GList) names = json_object_get_members(groups[g]);
			GList *name;
			for (name = names; name != NULL; name = name->next) {
				JsonObject *entry = json_object_get_object_member(groups[g], name->data);
				JsonObject *tokens = json_object_get_object_member(entry, "tokens");
				g_autoptr(JsonNode) plan = base_plan(components);
				g_autoptr(JsonNode) home = load("tests/fixtures/composed/index.json");
				g_autoptr(GList) keys = json_object_get_members(tokens);
				GList *key;
				g_autofree gchar *site = g_strdup_printf("%s-%s", g == 2 ? "palette" : "type", (const gchar *)name->data);
				JsonObject *plan_tokens = json_object_get_object_member(json_node_get_object(plan), "tokens");
				for (key = keys; key != NULL; key = key->next)
					json_object_set_member(plan_tokens, key->data, json_node_copy(json_object_get_member(tokens, key->data)));
				if (json_object_has_member(entry, "fonts")) {
					g_autofree gchar *fonts = ls_json_encode(json_object_get_member(entry, "fonts"));
					json_object_set_member(json_node_get_object(plan), "fonts", ls_json_parse(fonts, NULL));
				}
				emit(components, plan, home, site);
			}
		}
		for (i = 0; i < 24; i++) {
			g_autofree gchar *seed = g_strdup_printf("generated palette %u", i);
			g_autoptr(JsonObject) identity = ls_components_suggest_identity(components, seed, NULL);
			JsonObject *palette = json_object_get_object_member(identity, "palette");
			JsonObject *tokens = json_object_get_object_member(palette, "tokens");
			g_autoptr(JsonNode) plan = base_plan(components);
			g_autoptr(JsonNode) home = load("tests/fixtures/composed/index.json");
			g_autoptr(GList) keys = json_object_get_members(tokens);
			GList *key;
			g_autofree gchar *site = g_strdup_printf("palette-%s", json_object_get_string_member(palette, "name"));
			for (key = keys; key != NULL; key = key->next)
				json_object_set_string_member(json_object_get_object_member(json_node_get_object(plan), "tokens"),
					key->data, json_object_get_string_member(tokens, key->data));
			emit(components, plan, home, site);
		}
	}
}

int
main(int argc, char **argv){
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/options/components", test_components);
	g_test_add_func("/options/openings", test_openings);
	g_test_add_func("/options/dealt", test_dealt);
	return g_test_run();
}
