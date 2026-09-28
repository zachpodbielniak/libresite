/* SPDX-License-Identifier: AGPL-3.0-or-later */
#pragma once
#include <json-glib/json-glib.h>
G_BEGIN_DECLS
/**
 * ls_template_render:
 * @source: UTF-8 template in the documented Mustache subset
 * @view: (transfer none): data root; borrowed and never modified
 * @error: return location for an error
 *
 * Supports {{name}} (HTML-escaped), {{{name}}} and {{&name}} (verbatim),
 * {{#name}}...{{/name}}, {{^name}}...{{/name}}, {{! comments }}, {{.}} and
 * dotted names. Names resolve outward through the section context stack.
 * Missing values render as nothing. There are no partials, lambdas or
 * delimiter changes. See docs/components.org.
 *
 * Returns: (transfer full) (nullable): rendered text, or NULL for a malformed
 *   template with %LS_ERROR_INVALID
 */
gchar *ls_template_render(const gchar *source, JsonNode *view, GError **error);
/**
 * ls_template_escape:
 * @text: (nullable): UTF-8 text
 * Returns: (transfer full): @text with &, <, >, " and ' escaped for HTML
 */
gchar *ls_template_escape(const gchar *text);
G_END_DECLS
