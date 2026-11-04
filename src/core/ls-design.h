/* SPDX-License-Identifier: AGPL-3.0-or-later */
#pragma once
#include <glib.h>
#include <json-glib/json-glib.h>
G_BEGIN_DECLS

/*
 * The design picker (docs/components.org, "Presets and the picker"). A
 * site's design is a theme preset read from one real site, plus one layout
 * variant per home section. The picker chooses both from the order: the
 * owner's words first, then a stable hash of the business name, so the same
 * order always gives the same site. Variants carry layout only; colour,
 * type, radius and shadow come from the preset's tokens.
 *
 * @library is the catalog's library block and @themes the theme catalog;
 * both are borrowed and never changed.
 */

/**
 * ls_design_hash:
 * @text: (nullable): any text
 *
 * A stable 32-bit FNV-1a hash of @text folded to ASCII lower case. The
 * value never depends on the process, the platform or GLib's string hash.
 *
 * Returns: the hash; that of the empty string for %NULL
 */
guint32 ls_design_hash(const gchar *text);

/**
 * ls_design_contrast:
 * @a: (nullable): a #rrggbb colour
 * @b: (nullable): a #rrggbb colour
 *
 * Returns: the WCAG contrast ratio of @a and @b, from 1 to 21; 1 when
 *   either is not #rrggbb
 */
gdouble ls_design_contrast(const gchar *a, const gchar *b);

/**
 * ls_design_repair:
 * @color: a #rrggbb colour to keep as close as possible
 * @against: the #rrggbb colour it must contrast with
 * @minimum: the contrast ratio to reach, such as 4.5
 *
 * Moves @color's HSL lightness away from @against, in the smallest steps,
 * until the pair reaches @minimum; hue and saturation stay. A pair that
 * already passes keeps @color exactly.
 *
 * Returns: (transfer full): the repaired colour; black or white when no
 *   lightness of @color reaches @minimum; a copy of @color when either is
 *   not #rrggbb
 */
gchar *ls_design_repair(const gchar *color, const gchar *against, gdouble minimum);

/**
 * ls_design_owner_words:
 * @site: (nullable): the recipe's site object
 *
 * The owner's own colour and style words: site.style, site.brand.colors,
 * site.brand.tone and the "Colours:", "Colors:" and "Style:" sentences of
 * site.requirements, folded to lower case and split into words. Other text
 * in the requirements (process steps, rules) never counts.
 *
 * Returns: (transfer full) (element-type utf8): the words, possibly none
 */
GPtrArray *ls_design_owner_words(JsonObject *site);

/**
 * ls_design_pick_preset:
 * @library: the catalog's library block
 * @themes: the theme catalog; presets are themes with a design block
 * @site: (nullable): the recipe's site object
 *
 * Chooses the preset for @site. The preset whose words best match the
 * owner's words wins; ties go to the stable hash of the business name.
 * Without a match, that hash picks one, the presets that fit the
 * business's sector weighted higher and every other preset still possible;
 * presets that site.avoid fingerprints name (nearby sites) leave that pick
 * unless every preset is avoided.
 *
 * Returns: (transfer full) (nullable): a preset name, or %NULL when the
 *   theme catalog has no presets
 */
/**
 * ls_design_facts:
 * @site: (nullable): the recipe's site object, borrowed
 *
 * Reads the supplied facts the builder places itself, word for word:
 * the year after "since", the hours, the address, the towns served and
 * the owner's process steps, from structured members first and else from
 * the labelled sentences of site.requirements.
 *
 * Returns: (transfer full): the facts; a member is absent when not supplied
 */
JsonObject *ls_design_facts(JsonObject *site);

gchar *ls_design_pick_preset(JsonObject *library, JsonObject *themes, JsonObject *site);

/**
 * ls_design_new:
 * @library: the catalog's library block
 * @themes: the theme catalog
 * @site: (nullable): the recipe's site object
 * @preset: a preset name, such as from ls_design_pick_preset()
 * @photos: (nullable): the supplied photos with their pixel sizes
 * @error: return location for an error
 *
 * Builds a site's design: the preset, its sector and primary action, the
 * header and footer layouts, the home sections with one eligible variant
 * and one tone each, and the colour roles with the owner's named colour
 * applied and every text pair repaired to WCAG AA. Content decides which
 * variants are eligible (photos, items, quotes, a phone); the preset's
 * preferred variant weighs most; the stable hash picks; the clash rules of
 * ls_design_check_home() then hold.
 *
 * Returns: (transfer full) (nullable): the object for plan.design, or
 *   %NULL with @error set when @preset is not a preset
 */
JsonObject *ls_design_new(JsonObject *library, JsonObject *themes, JsonObject *site, const gchar *preset,
	JsonArray *photos, GError **error);

/**
 * ls_design_set_layout:
 * @library: the catalog's library block
 * @design: a design from ls_design_new(), changed in place
 * @type: a home section type, such as "hero", or "header" or "footer"
 * @variant: a library variant of @type
 * @error: return location for an error
 *
 * Sets the layout of the home section of @type (or the header or footer
 * layout) to @variant, then gives the home its tones again. Content is not
 * checked again: the caller chooses a layout its content suits. The header
 * over a photo needs a full-bleed opening, and the clash rules of
 * ls_design_check_home() must still hold.
 *
 * Returns: whether @design now uses @variant; %FALSE with @error set, and
 *   @design unchanged, when @variant is unknown, @type is not on the home
 *   or a rule breaks
 */
gboolean ls_design_set_layout(JsonObject *library, JsonObject *design, const gchar *type, const gchar *variant,
	GError **error);

/**
 * ls_design_check_home:
 * @library: the catalog's library block
 * @home: a design's home sections
 * @footer: (nullable): the footer layout, such as "columns"
 * @why: (nullable): receives each broken rule, one per line
 *
 * The clash rules: no two adjacent sections share the surface or inverse
 * tone (the footer counts as the section after the last), at most one
 * brand tone in any three sections, and no two adjacent sections of one
 * layout family.
 *
 * Returns: whether every rule holds
 */
gboolean ls_design_check_home(JsonObject *library, JsonArray *home, const gchar *footer, GString *why);

/**
 * ls_design_check:
 * @library: the catalog's library block
 * @design: a plan's design object
 * @why: receives each problem, one per line
 *
 * Checks a plan.design object: a known preset name, known layouts, colour
 * roles as #rrggbb with AA text pairs, shape values in range and the clash
 * rules.
 *
 * Returns: whether @design is valid
 */
gboolean ls_design_check(JsonObject *library, JsonObject *design, GString *why);

/**
 * ls_design_css:
 * @design: a plan's design object
 *
 * The design's tokens as CSS custom properties on :root: the colour roles
 * and tints derived from them as literal colours, the type and space
 * scales, container widths, radii, shadows and the photo scrim.
 *
 * Returns: (transfer full): CSS text
 */
gchar *ls_design_css(JsonObject *design);

G_END_DECLS
