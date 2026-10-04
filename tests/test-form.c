/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* The live form script: what it does after a send. Offline, without a
 * browser: these check the shipped script's order of operations against
 * the host's form answers (JSON for a script, ?sent=1 or ?error=CODE on the
 * page a no-script post returns to). */
#include "libresite.h"
#include <string.h>

static gchar *
script(void){
	gchar *text = NULL;
	g_assert_true(g_file_get_contents("data/components/form.js", &text, NULL, NULL));
	return text;
}

/* The text of @js from @from up to (not including) @to; both must exist. */
static gchar *
between(const gchar *js, const gchar *from, const gchar *to){
	const gchar *start = strstr(js, from);
	const gchar *end = start != NULL ? strstr(start, to) : NULL;
	if (start == NULL || end == NULL)
		g_error("no '%s' ... '%s' in form.js", from, to);
	return g_strndup(start, end - start);
}

/* The script asks for JSON and counts a send only on a 2xx answer whose JSON
 * says "received": then the form is cleared and hidden and the confirmation
 * shown in its place. Any other answer, or none, is a refusal. */
static void
test_confirm_in_place(void){
	g_autofree gchar *js = script();
	const gchar *endpoint = strstr(js, "[data-endpoint-form]");
	const gchar *parsed, *received, *reset, *shown, *refused, *failed;
	g_assert_nonnull(endpoint);
	/* No page is opened after a send. */
	g_assert_null(strstr(js, "data-thanks"));
	g_assert_null(strstr(js, "location.assign"));
	g_assert_null(strstr(js, "location.href ="));
	g_assert_nonnull(strstr(endpoint, "\"Accept\": \"application/json\""));
	/* The old rule, any 2xx is a send, is gone. */
	g_assert_null(strstr(endpoint, "if (!response.ok) throw"));
	parsed = strstr(endpoint, "response.json()");
	received = strstr(endpoint, "if (result.ok && answer.status === \"received\") {");
	reset = strstr(endpoint, "form.reset();");
	shown = strstr(endpoint, "confirm();\n\t\t\t} else refuse(result.ok ? null : answer.field);");
	failed = strstr(endpoint, ".catch(function () {");
	g_assert_nonnull(parsed);
	g_assert_nonnull(received);
	g_assert_nonnull(reset);
	g_assert_nonnull(shown);
	g_assert_nonnull(failed);
	g_assert_true(parsed < received && received < reset && reset < shown && shown < failed);
	/* An answer that is not JSON counts as no answer. */
	g_assert_nonnull(strstr(parsed, "return { ok: false, answer: null };"));
	/* A network failure shows the same banner and names no field. */
	refused = strstr(failed, "refuse(null);");
	g_assert_nonnull(refused);
	g_assert_true(refused < strstr(failed, "}).then("));
	/* The confirmation hides the form, then takes focus. */
	g_assert_nonnull(strstr(endpoint, "form.hidden = true;\n\t\tsent.hidden = false;\n\t\tsent.focus("));
}

/* A refusal shows the error banner in the site's own words and marks the
 * field the host named, if this form has it; it never clears or hides the
 * form, so every entry is kept. A new send clears the marks first. */
static void
test_refusal(void){
	g_autofree gchar *js = script();
	g_autofree gchar *refuse = between(js, "function refuse(field) {", "\n\t}\n");
	g_autofree gchar *submit = between(js, "form.addEventListener(\"submit\", function (event) {\n\t\tevent.preventDefault();\n\t\tif (pending",
		"fetch(");
	const gchar *banner, *marked, *focused;
	banner = strstr(refuse, "summary.textContent = form.getAttribute(\"data-failed\");");
	g_assert_nonnull(banner);
	g_assert_nonnull(strstr(refuse, "summary.hidden = false;"));
	/* The field is found by its name, never through a selector built from the address. */
	g_assert_nonnull(strstr(refuse, "control.name === field"));
	g_assert_null(strstr(refuse, "querySelector"));
	marked = strstr(refuse, "named.setAttribute(\"aria-invalid\", \"true\");");
	focused = strstr(refuse, "named.focus();");
	g_assert_nonnull(marked);
	g_assert_nonnull(focused);
	g_assert_true(banner < marked && marked < focused);
	g_assert_nonnull(strstr(refuse, "form.getAttribute(\"data-error-invalid\")"));
	/* The host's English message is not shown; the page speaks its own language. */
	g_assert_null(strstr(js, ".message"));
	g_assert_null(strstr(refuse, "reset"));
	g_assert_null(strstr(refuse, "form.hidden"));
	g_assert_nonnull(strstr(submit, "clear();"));
	g_assert_nonnull(strstr(js, "control.removeAttribute(\"aria-invalid\");"));
}

/* Back from a no-script send, the page's first live form shows the
 * confirmation on ?sent=1 or the error banner on ?error=CODE, marking the
 * field CODE names; this happens before the background sender is set up,
 * so it works in any browser. No fragment is read: the host adds none. */
static void
test_confirm_on_return(void){
	g_autofree gchar *js = script();
	const gchar *endpoint = strstr(js, "[data-endpoint-form]");
	const gchar *first, *sent, *error, *bail;
	g_assert_nonnull(endpoint);
	first = strstr(endpoint, "if (document.querySelector(\"[data-endpoint-form]\") === form) {");
	sent = strstr(endpoint, "if (/[?&]sent=1(&|$)/.test(window.location.search)) confirm();");
	error = strstr(endpoint, "else if (returned) refuse(returned[1]);");
	bail = strstr(endpoint, "typeof fetch === \"undefined\"");
	g_assert_nonnull(first);
	g_assert_nonnull(sent);
	g_assert_nonnull(error);
	g_assert_nonnull(bail);
	g_assert_true(first < sent && sent < error && error < bail);
	g_assert_nonnull(strstr(endpoint, "returned = /[?&]error=([^&]*)/.exec(window.location.search);"));
	g_assert_null(strstr(js, "#sent"));
	g_assert_null(strstr(js, "location.hash"));
	g_assert_null(strstr(js, "sent.id"));
	/* Scrolling respects reduced motion. */
	g_assert_nonnull(strstr(endpoint, "(prefers-reduced-motion: reduce)"));
	g_assert_nonnull(strstr(endpoint, "behavior: calm ? \"auto\" : \"smooth\""));
	/* Sending another brings the form back. */
	g_assert_nonnull(strstr(endpoint, "[data-form-again]"));
}

int
main(int argc, char **argv){
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/form/confirm-in-place", test_confirm_in_place);
	g_test_add_func("/form/refusal", test_refusal);
	g_test_add_func("/form/confirm-on-return", test_confirm_on_return);
	return g_test_run();
}
