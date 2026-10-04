/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* Recipe contact facts: the business phone and email the builder links. */
#include "libresite.h"
#include <string.h>

static gchar *
recipe_with(const gchar *contact){
	return g_strdup_printf("version: 1\nname: test\nsite: {brief: hi, contact: %s}\n"
		"pipeline: {steps: [{id: a, type: frontend}]}", contact);
}

/* Phone numbers and addresses as a person writes them are accepted. */
static void
contact_valid(void){
	const gchar *contacts[] = {"{phone: '+1 (555) 014-2270', email: hello@harbor.example}",
		"{phone: '0161 496.0000'}", "{email: a.b+c@x.example}", "{address: 1 Quay Street}", "{}"};
	guint i;
	for (i = 0; i < G_N_ELEMENTS(contacts); i++) {
		g_autofree gchar *text = recipe_with(contacts[i]);
		g_autoptr(GError) error = NULL;
		g_autoptr(LsRecipe) recipe = ls_recipe_new(text, &error);
		g_assert_no_error(error);
		g_assert_nonnull(recipe);
	}
}

/* Anything the builder could not turn into a call or mail link is refused
 * with the rule, including through the form adapter. */
static void
contact_invalid(void){
	const gchar *contacts[] = {"{phone: call us}", "{phone: '+1 555 0100 ext 2'}", "{phone: '12'}",
		"{phone: '1+2 345'}", "{phone: '0123456789012345678'}", "{email: not-an-address}",
		"{email: 'a b@x.example'}", "{email: 'a@b@x.example'}", "{email: '<a@x.example>'}"};
	guint i;
	for (i = 0; i < G_N_ELEMENTS(contacts); i++) {
		g_autofree gchar *text = recipe_with(contacts[i]);
		g_autoptr(GError) error = NULL;
		g_autoptr(LsRecipe) recipe = ls_recipe_new(text, &error);
		g_assert_null(recipe);
		g_assert_error(error, LS_ERROR, LS_ERROR_INVALID);
		if (strstr(error->message, strstr(contacts[i], "phone") != NULL ? "contact.phone" : "contact.email") == NULL)
			g_error("%s: unexpected message %s", contacts[i], error->message);
	}
	{
		g_autoptr(GError) error = NULL;
		g_autoptr(LsRecipe) recipe = ls_recipe_from_form(
			"{\"name\":\"t\",\"site\":{\"pages\":[{\"path\":\"/\",\"title\":\"Home\"}],\"offers\":[],"
			"\"contact\":{\"phone\":\"call us\"},\"brand\":{\"name\":\"T\"},\"copy_lock\":[],"
			"\"languages\":{\"default\":\"en\",\"supported\":[\"en\"]}}}", &error);
		g_assert_null(recipe);
		g_assert_nonnull(error);
		g_assert_nonnull(strstr(error->message, "contact.phone"));
	}
}

/* A caller's footer credit is checked where the recipe is read. */
static void
credit_recipe(void){
	const gchar *valid = "version: 1\nname: test\nsite: {brief: hi, credit: {text: Built with Example, "
		"url: 'https://example.com/'}}\npipeline: {steps: [{id: a, type: frontend}]}";
	const gchar *invalid[] = {"{text: Built with Example}", "{text: Built with Example, url: 'http://example.com/'}",
		"{text: '', url: 'https://example.com/'}", "Built with Example"};
	guint i;
	{
		g_autoptr(GError) error = NULL;
		g_autoptr(LsRecipe) recipe = ls_recipe_new(valid, &error);
		g_assert_no_error(error);
		g_assert_nonnull(recipe);
	}
	for (i = 0; i < G_N_ELEMENTS(invalid); i++) {
		g_autofree gchar *text = g_strdup_printf("version: 1\nname: test\nsite: {brief: hi, credit: %s}\n"
			"pipeline: {steps: [{id: a, type: frontend}]}", invalid[i]);
		g_autoptr(GError) error = NULL;
		g_autoptr(LsRecipe) recipe = ls_recipe_new(text, &error);
		g_assert_null(recipe);
		g_assert_error(error, LS_ERROR, LS_ERROR_INVALID);
		g_assert_nonnull(strstr(error->message, "site.credit"));
	}
}

/* A recipe whose site carries @modules (JSON), built as JSON so snippets and
 * stylesheets need no YAML quoting. */
static gchar *
recipe_with_modules(const gchar *modules){
	return g_strdup_printf("{\"version\": 1, \"name\": \"test\", \"site\": {\"brief\": \"hi\", \"modules\": %s}, "
		"\"pipeline\": {\"steps\": [{\"id\": \"a\", \"type\": \"frontend\"}]}}", modules);
}

static gchar *
podbay_fixture(const gchar *name){
	g_autofree gchar *path = g_strconcat("tests/fixtures/podbay/", name, NULL);
	gchar *text = NULL;
	g_assert_true(g_file_get_contents(path, &text, NULL, NULL));
	return text;
}

/* One module entry as JSON: @id with the fixture snippet and stylesheet,
 * each optionally replaced. */
static gchar *
module_entry(const gchar *id, const gchar *snippet, const gchar *css, const gchar *extra){
	g_autofree gchar *html = snippet != NULL ? g_strdup(snippet) : podbay_fixture("contact-form.html");
	g_autofree gchar *style = css != NULL ? g_strdup(css) : podbay_fixture("contact-form.css");
	g_autoptr(JsonNode) node = json_node_new(JSON_NODE_OBJECT);
	g_autofree gchar *text = NULL;
	JsonObject *object = json_object_new();
	json_object_set_string_member(object, "id", id);
	json_object_set_string_member(object, "snippet", html);
	json_object_set_string_member(object, "css", style);
	json_node_take_object(node, object);
	text = ls_json_encode(node);
	if (extra == NULL)
		return g_steal_pointer(&text);
	text[strlen(text) - 1] = '\0';
	return g_strdup_printf("%s, %s}", text, extra);
}

/* Supplied podbay modules: each a snippet and stylesheet podbay drew, checked
 * before anything is generated. */
static void
modules_valid(void){
	g_autoptr(GError) error = NULL;
	g_autofree gchar *entry = module_entry("contact-form", NULL, NULL, "\"title\": \"Contact us\"");
	g_autofree gchar *list = g_strdup_printf("[%s]", entry);
	g_autofree gchar *text = recipe_with_modules(list);
	g_autoptr(LsRecipe) recipe = ls_recipe_new(text, &error);
	g_autoptr(LsRecipe) again = NULL;
	g_autofree gchar *yaml = NULL;
	g_autofree gchar *snippet = podbay_fixture("contact-form.html");
	JsonObject *site;
	g_assert_no_error(error);
	g_assert_nonnull(recipe);
	/* The exported YAML keeps the snippet byte for byte. */
	yaml = ls_recipe_to_yaml(recipe, &error);
	g_assert_no_error(error);
	again = ls_recipe_new(yaml, &error);
	g_assert_no_error(error);
	site = json_object_get_object_member(ls_recipe_get_root(again), "site");
	g_assert_cmpstr(json_object_get_string_member(json_array_get_object_element(
		json_object_get_array_member(site, "modules"), 0), "snippet"), ==, snippet);
}

static void
modules_invalid(void){
	g_autofree gchar *good = module_entry("contact-form", NULL, NULL, NULL);
	g_autofree gchar *wrong_id = module_entry("booking", NULL, NULL, NULL);
	g_autofree gchar *script = module_entry("contact-form",
		"<form class=\"podbay podbay-contact-form\" hx-post=\"/x\" hx-target=\"#r\" hx-swap=\"innerHTML\">"
		"<script>alert(1)</script><button type=\"submit\">Send</button><div id=\"r\"></div></form>", NULL, NULL);
	g_autofree gchar *css = module_entry("contact-form", NULL, "body { color: red; }", NULL);
	g_autofree gchar *extra = module_entry("contact-form", NULL, NULL, "\"action\": \"/x\"");
	g_autofree gchar *bad_id = module_entry("Contact", NULL, NULL, NULL);
	g_autofree gchar *twice = g_strdup_printf("[%s, %s]", good, good);
	g_autofree gchar *one_script = g_strdup_printf("[%s]", script);
	g_autofree gchar *one_css = g_strdup_printf("[%s]", css);
	g_autofree gchar *one_extra = g_strdup_printf("[%s]", extra);
	g_autofree gchar *one_wrong = g_strdup_printf("[%s]", wrong_id);
	g_autofree gchar *one_bad = g_strdup_printf("[%s]", bad_id);
	const gchar *cases[] = {"[]", "{}", "[1]", twice, one_script, one_css, one_extra, one_wrong, one_bad};
	guint i;
	for (i = 0; i < G_N_ELEMENTS(cases); i++) {
		g_autoptr(GError) error = NULL;
		g_autofree gchar *text = recipe_with_modules(cases[i]);
		g_autoptr(LsRecipe) recipe = ls_recipe_new(text, &error);
		if (recipe != NULL)
			g_error("modules case %u was accepted", i);
		g_assert_error(error, LS_ERROR, LS_ERROR_INVALID);
		if (strstr(error->message, "modules") == NULL && strstr(error->message, "podbay module") == NULL)
			g_error("modules case %u: unexpected message %s", i, error->message);
	}
}

int
main(int argc, char **argv){
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/recipe/contact-valid", contact_valid);
	g_test_add_func("/recipe/contact-invalid", contact_invalid);
	g_test_add_func("/recipe/credit", credit_recipe);
	g_test_add_func("/recipe/modules-valid", modules_valid);
	g_test_add_func("/recipe/modules-invalid", modules_invalid);
	return g_test_run();
}
