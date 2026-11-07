/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* The scheme script and the head script agree (docs/components.org, "Light
 * and dark"): one storage key, the same two stored states, system removes
 * the choice, the toggles show only when the script runs. The behaviour in
 * a browser is tests/controls.sh; composed pages are /design/scheme-page. */
#include <glib.h>
#include <string.h>

static gchar *
read_data(const gchar *path){
	gchar *text = NULL;
	g_autoptr(GError) error = NULL;
	if (!g_file_get_contents(path, &text, NULL, &error))
		g_error("%s: %s", path, error->message);
	return text;
}

static void
test_scheme_contract(void){
	g_autofree gchar *script = read_data("data/components/scheme.js");
	g_autofree gchar *layout = read_data("data/components/layout.html");
	const gchar *head = strstr(layout, "<script>");
	g_autofree gchar *inline_script = NULL;
	g_assert_nonnull(head);
	inline_script = g_strndup(head, (gsize)(strstr(head, "</script>") - head));
	/* The head reads the key the toggle writes, and only light or dark. */
	g_assert_nonnull(strstr(inline_script, "localStorage.getItem(\"ls-scheme\")"));
	g_assert_nonnull(strstr(inline_script, "s===\"light\"||s===\"dark\""));
	g_assert_nonnull(strstr(inline_script, "try{"));
	g_assert_nonnull(strstr(script, "localStorage.setItem(\"ls-scheme\", state)"));
	g_assert_nonnull(strstr(script, "localStorage.removeItem(\"ls-scheme\")"));
	/* Three states in one order, starting from the system setting. */
	g_assert_nonnull(strstr(script, "[\"system\", \"light\", \"dark\"]"));
	g_assert_nonnull(strstr(script, "delete root.dataset.scheme"));
	/* The toggles are hidden in the markup and shown by the script. */
	g_assert_nonnull(strstr(layout, "data-scheme-toggle hidden"));
	g_assert_nonnull(strstr(script, "toggle.hidden = false"));
	/* Nothing animates the change of scheme. */
	g_assert_null(strstr(script, "transition"));
	g_assert_null(strstr(script, "animate"));
}

int
main(int argc, char **argv){
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/scheme/contract", test_scheme_contract);
	return g_test_run();
}
