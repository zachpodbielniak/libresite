/* SPDX-License-Identifier: AGPL-3.0-or-later */
#ifndef LS_TASTE_H
#define LS_TASTE_H
#include <glib.h>

G_BEGIN_DECLS

/**
 * ls_taste_disclosure:
 * @text: (nullable): visible copy
 * @footer: whether @text is the footer note, the one place a site may
 *   disclose that its business or photographs are examples
 *
 * Finds talk about the site itself in visible copy: the word "illustrative"
 * anywhere, and outside the footer any line about stock photographs,
 * visual references, an invented business or a demo site.
 *
 * Returns: (nullable) (transfer none): the matched phrase, or %NULL
 */
const gchar *ls_taste_disclosure(const gchar *text, gboolean footer);

/**
 * ls_taste_chroma:
 * @hex: (nullable): a #rrggbb colour
 *
 * Returns: how coloured @hex is, its largest channel minus its smallest,
 *   from 0 (grey) to 1; 0 for anything that is not #rrggbb
 */
gdouble ls_taste_chroma(const gchar *hex);

/**
 * ls_taste_calm:
 * @hex: a #rrggbb colour
 * @limit: the largest chroma allowed, 0 to 1
 *
 * Returns: (transfer full): @hex moved toward its own grey until its chroma
 *   is at most @limit, keeping its average lightness; @hex unchanged when
 *   already calm or not #rrggbb
 */
gchar *ls_taste_calm(const gchar *hex, gdouble limit);

/**
 * ls_taste_mix:
 * @hex: a #rrggbb colour
 * @toward: a #rrggbb colour
 * @amount: how far to move, 0 (@hex) to 1 (@toward)
 *
 * Returns: (transfer full): the mixed colour; @hex unchanged when either
 *   is not #rrggbb
 */
gchar *ls_taste_mix(const gchar *hex, const gchar *toward, gdouble amount);

G_END_DECLS
#endif
