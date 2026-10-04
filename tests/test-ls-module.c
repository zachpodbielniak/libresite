/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* Supplied podbay module snippets and stylesheets: the checks that decide
 * what may be drawn into a page, and the page markup made from a snippet. */
#include "libresite.h"
#include <string.h>

#define FIXTURE "tests/fixtures/podbay/"

static gchar *
fixture(const gchar *name){
	g_autofree gchar *path = g_strconcat(FIXTURE, name, NULL);
	gchar *text = NULL;
	g_assert_true(g_file_get_contents(path, &text, NULL, NULL));
	return text;
}

/* @text with the first @needle replaced by @value. */
static gchar *
replace_once(const gchar *text, const gchar *needle, const gchar *value){
	const gchar *at = strstr(text, needle);
	g_assert_nonnull(at);
	return g_strdup_printf("%.*s%s%s", (gint)(at - text), text, value, at + strlen(needle));
}

static void
test_ids(void){
	g_assert_true(ls_module_id_valid("contact-form"));
	g_assert_true(ls_module_id_valid("a"));
	g_assert_true(ls_module_id_valid("quote2"));
	g_assert_false(ls_module_id_valid(NULL));
	g_assert_false(ls_module_id_valid(""));
	g_assert_false(ls_module_id_valid("Contact"));
	g_assert_false(ls_module_id_valid("2fast"));
	g_assert_false(ls_module_id_valid("-x"));
	g_assert_false(ls_module_id_valid("x-"));
	g_assert_false(ls_module_id_valid("a--b"));
	g_assert_false(ls_module_id_valid("contact_form"));
	g_assert_false(ls_module_id_valid("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));
}

/* podbay's own default snippet, drawn by pb_module_render, is accepted. */
static void
test_snippet_valid(void){
	g_autoptr(GError) error = NULL;
	g_autofree gchar *snippet = fixture("contact-form.html");
	g_autofree gchar *scoped = NULL;
	g_assert_true(ls_module_check_snippet("contact-form", snippet, &error));
	g_assert_no_error(error);
	/* An absolute https:// action and a select with options are fine too. */
	scoped = replace_once(snippet, "hx-post=\"/forms/contact\"", "hx-post=\"https://forms.example/contact\"");
	{
		g_autofree gchar *with_select = replace_once(scoped, "<button",
			"<div class=\"podbay-field\"><label for=\"contact-topic\">Topic</label><select id=\"contact-topic\" "
			"name=\"topic\"><option value=\"\"></option><option>Repairs &amp; parts</option></select></div>\n<button");
		g_assert_true(ls_module_check_snippet("contact-form", with_select, &error));
		g_assert_no_error(error);
	}
}

/* Everything outside podbay's restricted markup is refused with a reason. */
static void
test_snippet_refused(void){
	g_autofree gchar *snippet = fixture("contact-form.html");
	const gchar *cases[][3] = {
		{"<button", "<script>alert(1)</script><button", "script"},
		{"<button type=\"submit\"", "<button type=\"submit\" onclick=\"x()\"", "onclick"},
		{"<button type=\"submit\"", "<button type=\"submit\" style=\"color:red\"", "style"},
		{"<button", "<a href=\"/x\">x</a><button", "<a>"},
		{"<button", "<img src=x><button", "img"},
		{"class=\"podbay podbay-contact-form\"", "class=\"podbay\"", "podbay-contact-form"},
		{"hx-post=\"/forms/contact\"", "hx-post=\"javascript:alert(1)\"", "hx-post"},
		{"hx-post=\"/forms/contact\"", "hx-post=\"//evil.example/x\"", "hx-post"},
		{"hx-post=\"/forms/contact\"", "hx-post=\"http://plain.example/x\"", "hx-post"},
		{"hx-post=\"/forms/contact\" ", "", "hx-post"},
		{"hx-target=\"#contact-result\"", "hx-target=\"#nowhere\"", "hx-target"},
		{"hx-swap=\"innerHTML\"", "hx-swap=\"innerHTML settle:1s\"", "hx-swap"},
		{"id=\"contact-email\"", "id=\"contact-first-name\"", "id"},
		{"</textarea></div>", "</textarea>", "close"},
		{"<button type=\"submit\">Send message</button>", "<button type=\"submit\">Send &bogus message</button>", "&"},
		{"<button type=\"submit\">Send message</button>", "<button type=\"submit\">Send > message</button>", ">"},
		{"type=\"email\"", "type='email'", "quote"},
		{"type=\"email\"", "type=\"hidden\"", "type"},
		{"<button type=\"submit\">Send message</button>\n", "", "button"},
		{"</form>\n", "</form>\n<form class=\"podbay podbay-contact-form\"></form>\n", "one form"},
		{"<form", "Hello <form", "one form"},
	};
	guint i;
	for (i = 0; i < G_N_ELEMENTS(cases); i++) {
		g_autoptr(GError) error = NULL;
		g_autofree gchar *broken = replace_once(snippet, cases[i][0], cases[i][1]);
		if (ls_module_check_snippet("contact-form", broken, &error))
			g_error("case %u (%s) was accepted:\n%s", i, cases[i][2], broken);
		g_assert_error(error, LS_ERROR, LS_ERROR_INVALID);
		g_assert_nonnull(strstr(error->message, "contact-form"));
	}
	/* The wrong module id, a NUL-free but huge snippet and an empty one. */
	{
		g_autoptr(GError) error = NULL;
		GString *huge = g_string_new(snippet);
		g_assert_false(ls_module_check_snippet("booking", snippet, &error));
		g_clear_error(&error);
		while (huge->len <= 65536)
			g_string_append(huge, "          ");
		g_assert_false(ls_module_check_snippet("contact-form", huge->str, &error));
		g_clear_error(&error);
		g_string_free(huge, TRUE);
		g_assert_false(ls_module_check_snippet("contact-form", "   ", &error));
		g_clear_error(&error);
	}
}

static void
test_css_valid(void){
	g_autoptr(GError) error = NULL;
	g_autofree gchar *css = fixture("contact-form.css");
	g_assert_true(ls_module_check_css("contact-form", css, &error));
	g_assert_no_error(error);
	g_assert_true(ls_module_check_css("contact-form",
		".podbay-contact-form:is(.x), .podbay-contact-form[hidden] { width: calc(100% - var(--a, 1rem)); }", &error));
	g_assert_no_error(error);
}

/* podbay's stylesheet rules: flat rules scoped to the module's class, no
 * at-rules, strings, escapes, !important or fetches. */
static void
test_css_refused(void){
	const gchar *cases[] = {
		"",
		"/* only a comment */",
		"@import \"x.css\";",
		".podbay-contact-form { background: url(x.png); }",
		".podbay-contact-form { color: red !important; }",
		".podbay-contact-form { content: \"x\"; }",
		".podbay-contact-form { content: 'x'; }",
		".podbay-contact-form { color: \\72 ed; }",
		"body { color: red; }",
		".podbay-contact-formx { color: red; }",
		".podbay-contact-form, body { color: red; }",
		".podbay-contact-form { .inner { color: red; } }",
		".podbay-contact-form { color: red; ",
		".podbay-contact-form { color: red; } }",
		".podbay-contact-form { color: red; } /* open",
		".podbay-contact-form { color: expression(alert(1)); }",
		".podbay-contact-form </style> { color: red; }",
		".podbay-contact-form & .x { color: red; }",
		".podbay-booking { color: red; }",
	};
	guint i;
	for (i = 0; i < G_N_ELEMENTS(cases); i++) {
		g_autoptr(GError) error = NULL;
		if (ls_module_check_css("contact-form", cases[i], &error))
			g_error("css case %u was accepted: %s", i, cases[i]);
		g_assert_error(error, LS_ERROR, LS_ERROR_INVALID);
	}
}

/* The page markup posts without scripts too: the form gains the same action
 * and method, and a marker the site's script finds. Nothing else changes. */
static void
test_snippet_html(void){
	g_autofree gchar *snippet = fixture("contact-form.html");
	g_autofree gchar *html = ls_module_snippet_html(snippet, "Not sent & try again");
	const gchar *rest = strstr(snippet, " hx-post=");
	g_assert_nonnull(html);
	g_assert_true(g_str_has_prefix(html, "<form action=\"/forms/contact\" method=\"post\" data-podbay-form "
		"data-failed=\"Not sent &amp; try again\" class=\"podbay podbay-contact-form\""));
	g_assert_nonnull(strstr(html, rest));
	g_assert_null(ls_module_snippet_html("<div></div>", "x"));
	/* Any whitespace may separate attributes, and only the hx-post attribute
	 * itself gives the action, never text inside another value. */
	{
		g_autoptr(GError) error = NULL;
		g_autofree gchar *lines = replace_once(snippet, "<form class=", "<form\nclass=");
		g_autofree gchar *decoy = replace_once(snippet, "class=\"podbay podbay-contact-form\"",
			"class=\"podbay podbay-contact-form hx-post=\"");
		g_autofree gchar *drawn = NULL;
		g_assert_true(ls_module_check_snippet("contact-form", lines, &error));
		g_assert_no_error(error);
		drawn = ls_module_snippet_html(lines, "x");
		g_assert_nonnull(drawn);
		g_assert_true(g_str_has_prefix(drawn, "<form action=\"/forms/contact\" method=\"post\""));
		g_clear_pointer(&drawn, g_free);
		g_assert_true(ls_module_check_snippet("contact-form", decoy, &error));
		g_assert_no_error(error);
		drawn = ls_module_snippet_html(decoy, "x");
		g_assert_nonnull(drawn);
		g_assert_true(g_str_has_prefix(drawn, "<form action=\"/forms/contact\" method=\"post\""));
	}
}

/* The posting script, read offline: it never runs the host's answer as
 * script or assigns it as raw markup, keeps entries unless the host took
 * the send, shows the site's own words when nothing came back, and leaves a
 * page that loads htmx to htmx. */
static void
test_script(void){
	g_autofree gchar *js = NULL;
	const gchar *parsed, *reset, *failed;
	g_assert_true(g_file_get_contents("data/components/module.js", &js, NULL, NULL));
	g_assert_nonnull(strstr(js, "document.querySelectorAll(\"form[data-podbay-form]\")"));
	g_assert_nonnull(strstr(js, "if (window.htmx || !window.fetch"));
	g_assert_nonnull(strstr(js, "\"HX-Request\": \"true\""));
	g_assert_nonnull(strstr(js, "credentials: \"same-origin\""));
	parsed = strstr(js, "new DOMParser().parseFromString(");
	g_assert_nonnull(parsed);
	g_assert_nonnull(strstr(parsed, "querySelectorAll(\"script, iframe, frame, object, embed, base, meta, link\")"));
	/* Handlers and script addresses are dropped from what remains. */
	g_assert_nonnull(strstr(parsed, "name.indexOf(\"on\") === 0"));
	g_assert_nonnull(strstr(parsed, "/^(javascript|vbscript):/"));
	g_assert_null(strstr(js, ".innerHTML ="));
	g_assert_null(strstr(js, "insertAdjacentHTML"));
	g_assert_null(strstr(js, "eval("));
	/* All eight swap modes, and nothing else. */
	g_assert_nonnull(strstr(js, "[\"innerHTML\", \"outerHTML\", \"beforebegin\", \"afterbegin\", \"beforeend\", "
		"\"afterend\", \"delete\", \"none\"]"));
	reset = strstr(js, "if (answer.ok) form.reset();");
	g_assert_nonnull(reset);
	failed = strstr(js, ".catch(function () {\n\t\t\tfailed(into);");
	g_assert_nonnull(failed);
	g_assert_nonnull(strstr(js, "into.textContent = form.getAttribute(\"data-failed\");"));
}

int
main(int argc, char **argv){
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/module/ids", test_ids);
	g_test_add_func("/module/snippet/valid", test_snippet_valid);
	g_test_add_func("/module/snippet/refused", test_snippet_refused);
	g_test_add_func("/module/css/valid", test_css_valid);
	g_test_add_func("/module/css/refused", test_css_refused);
	g_test_add_func("/module/snippet/html", test_snippet_html);
	g_test_add_func("/module/script", test_script);
	return g_test_run();
}
