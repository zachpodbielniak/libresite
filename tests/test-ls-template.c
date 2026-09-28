/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "core/ls-template.h"
#include "core/ls-util.h"

static gchar *
render(const gchar *source, const gchar *json, GError **error){
	g_autoptr(JsonNode) view = ls_json_parse(json, NULL);
	g_assert_nonnull(view);
	return ls_template_render(source, view, error);
}

static void
check(const gchar *source, const gchar *json, const gchar *expected){
	g_autoptr(GError) error = NULL;
	g_autofree gchar *result = render(source, json, &error);
	g_assert_no_error(error);
	g_assert_cmpstr(result, ==, expected);
}

/* Variables escape every HTML-significant character; triples do not. */
static void
test_escape(void){
	check("<p>{{v}}</p>", "{\"v\":\"<a href='x'>&\\\"</a>\"}",
		"<p>&lt;a href=&#39;x&#39;&gt;&amp;&quot;&lt;/a&gt;</p>");
	check("{{{v}}}|{{&v}}", "{\"v\":\"<b>\"}", "<b>|<b>");
	check("{{n}} {{f}} {{t}} {{missing}}.", "{\"n\":3,\"f\":1.5,\"t\":true}", "3 1.5 true .");
}

/* Sections iterate arrays, enter objects and test truthiness. */
static void
test_sections(void){
	check("{{#items}}[{{name}}]{{/items}}",
		"{\"items\":[{\"name\":\"a\"},{\"name\":\"b\"}]}", "[a][b]");
	check("{{#items}}{{.}},{{/items}}", "{\"items\":[\"x\",\"y\"]}", "x,y,");
	check("{{#art}}<img src=\"{{src}}\">{{/art}}", "{\"art\":{\"src\":\"a.svg\"}}",
		"<img src=\"a.svg\">");
	check("{{#on}}yes{{/on}}{{#off}}no{{/off}}{{#empty}}no{{/empty}}{{#blank}}no{{/blank}}",
		"{\"on\":true,\"off\":false,\"empty\":[],\"blank\":\"\"}", "yes");
	check("{{^items}}none{{/items}}{{^full}}no{{/full}}", "{\"items\":[],\"full\":[1]}", "none");
}

/* Names resolve outward through the context stack and through dots. */
static void
test_lookup(void){
	check("{{#items}}{{label}}@{{site}} {{/items}}",
		"{\"site\":\"S\",\"items\":[{\"label\":\"a\"},{\"label\":\"b\",\"site\":\"T\"}]}",
		"a@S b@T ");
	check("{{link.href}}/{{link.label}}", "{\"link\":{\"href\":\"x.html\",\"label\":\"X\"}}",
		"x.html/X");
	check("{{! note }}a{{#x}}{{#x}}b{{/x}}{{/x}}", "{\"x\":true}", "ab");
}

/* Malformed templates are programming errors surfaced as GErrors, never output. */
static void
test_errors(void){
	const gchar *bad[] = {"{{#a}}x", "{{/a}}", "{{#a}}{{/b}}", "{{a", "{{}}", "{{{a}}"};
	guint i;
	for (i = 0; i < G_N_ELEMENTS(bad); i++) {
		g_autoptr(GError) error = NULL;
		g_autofree gchar *result = render(bad[i], "{}", &error);
		g_assert_null(result);
		g_assert_error(error, LS_ERROR, LS_ERROR_INVALID);
	}
}

int
main(int argc, char **argv){
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/template/escape", test_escape);
	g_test_add_func("/template/sections", test_sections);
	g_test_add_func("/template/lookup", test_lookup);
	g_test_add_func("/template/errors", test_errors);
	return g_test_run();
}
