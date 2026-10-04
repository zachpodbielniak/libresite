/* SPDX-License-Identifier: AGPL-3.0-or-later */
#pragma once
#include <glib.h>
#include <json-glib/json-glib.h>
G_BEGIN_DECLS

/* Size limit of a supplied module snippet or stylesheet, as in podbay. */
#define LS_MODULE_MAX_TEXT 65536

/**
 * ls_module_id_valid:
 * @id: (nullable): a podbay module id, such as contact-form
 *
 * podbay's id grammar: 1 to 64 characters of lowercase letters and digits in
 * words joined by single hyphens, starting with a letter.
 * Returns: whether @id is a module id
 */
gboolean ls_module_id_valid(const gchar *id);
/**
 * ls_module_check_snippet:
 * @id: the module id the snippet belongs to
 * @snippet: the module's default HTML snippet, as podbay's pb_module_render()
 *   draws it
 * @error: return location for an error
 *
 * Checks that @snippet is exactly one form of podbay's restricted markup, so
 * it can be placed in a page as it is: class "podbay podbay-ID", an hx-post
 * action that is a same-site path or an https:// address, an hx-target naming
 * an element of the snippet, an hx-swap from podbay's eight modes, and inside
 * it only labelled fields (div, label, input, textarea, select, option), a
 * submit button and outcome regions. Attributes come from a fixed list and
 * are double-quoted; text and values use only &amp;, &lt;, &gt;, &quot; and
 * numeric references; ids are unique. Scripts, styles, event handlers,
 * links, images and comments are refused. At most %LS_MODULE_MAX_TEXT bytes.
 * Returns: whether @snippet may be drawn; on failure %LS_ERROR_INVALID names
 *   the module and the reason
 */
gboolean ls_module_check_snippet(const gchar *id, const gchar *snippet, GError **error);
/**
 * ls_module_check_css:
 * @id: the module id the stylesheet belongs to
 * @css: the module's default stylesheet (podbay's surfaces/web.css)
 * @error: return location for an error
 *
 * podbay's stylesheet rules: at least one rule, only flat rules, every
 * selector starting with .podbay-ID followed by a character that cannot
 * extend a class name, none of the characters @ \ < " ' ! &, and only the
 * functions var, calc, min, max, clamp, rgb, rgba, hsl, hsla, not, is, where,
 * nth-child and nth-of-type. Comments are allowed. At most
 * %LS_MODULE_MAX_TEXT bytes.
 * Returns: whether @css may be published; on failure %LS_ERROR_INVALID
 */
gboolean ls_module_check_css(const gchar *id, const gchar *css, GError **error);
/**
 * ls_module_snippet_html:
 * @snippet: a snippet accepted by ls_module_check_snippet()
 * @failed: the site's words for a send that did not go through
 *
 * The snippet as a page carries it: the form also gets action and method
 * from its hx-post, so it posts without scripts, plus data-podbay-form and
 * data-failed (@failed, escaped) for the site's script. Nothing else changes.
 * Returns: (transfer full) (nullable): the markup, or %NULL when @snippet
 *   does not start with a form carrying hx-post
 */
gchar *ls_module_snippet_html(const gchar *snippet, const gchar *failed);
/**
 * ls_module_strip_markup:
 * @plan: (transfer none): a private copy of a plan, about to be shown to a model
 *
 * Reduces every supplied module in the plan's modules to its id and title. A
 * page names a module by its id; the snippet and stylesheet are the
 * builder's, and a model has no use for them.
 */
void ls_module_strip_markup(JsonObject *plan);

G_END_DECLS
