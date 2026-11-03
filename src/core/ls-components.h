/* SPDX-License-Identifier: AGPL-3.0-or-later */
#pragma once
#include <gio/gio.h>
#include <json-glib/json-glib.h>
G_BEGIN_DECLS
#define LS_TYPE_COMPONENTS (ls_components_get_type())
G_DECLARE_FINAL_TYPE(LsComponents, ls_components, LS, COMPONENTS, GObject)
/**
 * LsComponents:
 *
 * The embedded component library for composed sites: a validated catalog of
 * section components (Mustache-subset templates, CSS and optional scripts),
 * the shared page layout and the built-in utility pages. The library turns a
 * site plan and one small page specification per route into complete static
 * pages. Instances are immutable after construction and safe to share
 * between threads. See docs/components.org.
 */

/**
 * ls_components_new:
 * @error: return location for an error
 *
 * Loads and validates the catalog embedded in the libresite GResource.
 * Returns: (transfer full) (nullable): the component library
 */
LsComponents *ls_components_new(GError **error);
/**
 * ls_components_new_from_snapshot:
 * @snapshot: (nullable): archived library from ls_components_get_snapshot(),
 *   or NULL for the embedded library
 * @error: return location for an error
 *
 * Loads and validates an archived library with the same checks as the
 * embedded one. Missing files are errors; embedded bytes are never mixed in,
 * so a sealed order keeps the components, themes and identities it was sold
 * with. Equal bytes produce an equal digest.
 * Returns: (transfer full) (nullable): the component library
 */
LsComponents *ls_components_new_from_snapshot(const gchar *snapshot, GError **error);
/**
 * ls_components_get_snapshot:
 * @self: a component library
 *
 * Serializes every library file this instance loaded, with sorted names, for
 * archiving beside a sealed recipe.
 * Returns: (transfer full): JSON text accepted by ls_components_new_from_snapshot()
 */
gchar *ls_components_get_snapshot(LsComponents *self);
/**
 * ls_components_get_digest:
 * @self: a component library
 * Returns: (transfer none): SHA-256 over every embedded component byte, used
 *   to bind checkpoints to the library that rendered them
 */
const gchar *ls_components_get_digest(LsComponents *self);
/**
 * ls_components_get_catalog:
 * @self: a component library
 *
 * The catalog as loaded: extra and wider entries already joined to the
 * axes, components and blocks they extend.
 * Returns: (transfer none): the catalog's root node
 */
JsonNode *ls_components_get_catalog(LsComponents *self);
/**
 * ls_components_get_plan_schema:
 * @self: a component library
 * Returns: (transfer none): JSON Schema for a site plan response
 */
const gchar *ls_components_get_plan_schema(LsComponents *self);
/**
 * ls_components_dup_page_schema:
 * @self: a component library
 * Returns: (transfer full): JSON Schema for a page specification response,
 *   with the catalog's component names as an enumeration
 */
gchar *ls_components_dup_page_schema(LsComponents *self);
/**
 * ls_components_dup_art_schema:
 * @self: a component library
 * Returns: (transfer full): JSON Schema for an artwork response: {"svg": text}
 */
gchar *ls_components_dup_art_schema(LsComponents *self);
/**
 * ls_components_get_look_description:
 * @self: a component library
 * @axis: a look axis, such as "art"
 * @value: one of its values
 *
 * Returns: (transfer none) (nullable): the catalog's description of the
 *   value, which prompts quote, or %NULL for an unknown axis or value
 */
const gchar *ls_components_get_look_description(LsComponents *self, const gchar *axis, const gchar *value);
/**
 * ls_components_apply_default:
 * @self: a component library
 * @plan: (transfer none): plan to change in place
 * @themed: whether a theme already set the plan's design
 *
 * Makes the builder, not the model, decide the site's design: one calm
 * default from the catalog's default block (docs/components.org, "The
 * default design"). Sets structure to the default home opening, or the
 * photo opening when the plan has supplied photos, and keeps only the
 * brand mark in the plan's art. Without a theme, it also sets every look
 * axis to its default, the colour tokens, font tokens and web fonts to
 * ls_components_get_default_identity(), and a visual sequence's layout to
 * the default scene. A plan without web fonts keeps its own font tokens,
 * so an order that forbids remote fonts still gets none. With @themed, the
 * theme's look, colours and type stay; builder axes it leaves open take
 * their defaults.
 * Returns: whether @plan was an object and changed
 */
gboolean ls_components_apply_default(LsComponents *self, JsonNode *plan, gboolean themed);
/**
 * ls_components_apply_opening:
 * @self: a component library
 * @plan: (transfer none): a plan whose structure names an opening
 * @path: the home page's route
 * @page: (transfer none): the home page specification, changed in place
 *
 * Rewrites the page's opening section (a hero or cover) as the component
 * and variant in the plan's structure.opening, carrying over its kicker,
 * heading, lede, actions and art; a supplied photo leads any opening that
 * shows a picture. When that opening cannot hold the words or needs a
 * picture the site does not have, the catalog's default opening is used.
 * Custom openings, pages that do not open with an opening component and
 * plans without structure.opening are left alone.
 * Returns: whether @page changed
 */
gboolean ls_components_apply_opening(LsComponents *self, JsonNode *plan, const gchar *path, JsonNode *page);
/**
 * ls_components_spread_photos:
 * @self: a component library
 * @plan: (transfer none): a plan with its supplied photos
 * @page: (transfer none): a page specification, changed in place
 *
 * Shows each supplied photo at most once per page. Image slots claim photos
 * in turn: the opening first, then sections with words of their own, then
 * photo-only sections (media, gallery, carousel and the like), each in page
 * order. A slot repeating a photo already claimed takes a supplied photo
 * the page has not shown yet, preferring one no later slot asks for, with
 * its alt text. With none left the repeat goes: a carousel or gallery item
 * is dropped, an optional image removed, and a section that cannot stand
 * without its image (or its items) is dropped. Run it after
 * ls_visual_apply_page(), which adds the opening and craft photos.
 * Returns: whether @page changed
 */
gboolean ls_components_spread_photos(LsComponents *self, JsonNode *plan, JsonNode *page);
/**
 * ls_components_ensure_photo:
 * @self: a component library
 * @plan: (transfer none): a plan with its supplied photos
 * @path: the page's route, such as services.html
 * @page: (transfer none): a page specification, changed in place
 *
 * Gives a page of a photo site that shows no supplied photograph one: a
 * full-width photo section right after its opening section, the photo
 * picked from the route so inner pages differ. Legal and error pages and
 * sites without photos are left alone.
 * Returns: whether @page changed
 */
gboolean ls_components_ensure_photo(LsComponents *self, JsonNode *plan, const gchar *path, JsonNode *page);
/**
 * ls_components_check_taste:
 * @self: a component library
 * @plan: (transfer none): the plan the site was built from
 * @bundle: (transfer none): the composed {"files": [...]} bundle
 * @problems: one line is appended per problem found
 *
 * Checks a built site for what the catalog's taste block forbids: the word
 * "illustrative" in visible copy, a page headline that is a question, a
 * page of a photo site without a photograph, letter chips, a gimmick
 * variant, a look value other than the default design's on an unthemed
 * site without a visual sequence, a footer that says the business is an
 * example on a site that is not a demo build, a page or surface colour
 * louder than the chroma limit, and a page that
 * shows one supplied photo twice (a picture's phone crop counts when it is
 * another photo).
 * Returns: whether the site is clean
 */
gboolean ls_components_check_taste(LsComponents *self, JsonNode *plan, JsonNode *bundle, GString *problems);
/**
 * ls_components_count_photos:
 * @self: a component library
 * @plan: (transfer none): the plan the site was built from
 * @bundle: (transfer none): the composed {"files": [...]} bundle
 *
 * Counts the distinct supplied photos a built site shows across its pages
 * (a picture's phone crop counts when it is another photo). A site given
 * four or more should show at least four; the showcase gate checks it.
 * Returns: the number of distinct supplied photos shown
 */
guint ls_components_count_photos(LsComponents *self, JsonNode *plan, JsonNode *bundle);
/**
 * ls_components_list_sectors:
 * @self: a component library
 *
 * Returns: (transfer full) (array zero-terminated=1): the business sectors
 *   that identities fit, in catalog order
 */
gchar **ls_components_list_sectors(LsComponents *self);
/**
 * ls_components_get_default_identity:
 * @self: a component library
 *
 * The palette, type pairing and home-page section sequence the catalog's
 * default block names from identities.yaml. Every unthemed site wears them.
 * Returns: (transfer full): {palette: {name, summary, tokens},
 *   type: {name, summary, tokens, fonts}, home: {name, sections}}
 */
JsonObject *ls_components_get_default_identity(LsComponents *self);
/**
 * ls_components_check_identity:
 * @self: a component library
 * @plan: a site plan
 * @identity: (nullable): the identity the site wears
 * @error: return location for an error
 *
 * Rejects a plan whose tokens fall back on the house default (warm cream
 * paper with a rust accent, or Fraunces over Source Sans) unless @identity
 * itself is it. The error, %LS_ERROR_VALIDATION, is written to be sent
 * back to the model.
 * Returns: whether the plan's identity is its own
 */
gboolean ls_components_check_identity(LsComponents *self, JsonNode *plan, JsonObject *identity, GError **error);
/**
 * ls_components_list_themes:
 * @self: a component library
 *
 * Returns: (transfer full) (array zero-terminated=1): theme names in catalog order
 */
gchar **ls_components_list_themes(LsComponents *self);
/**
 * ls_components_get_theme_summary:
 * @self: a component library
 * @name: a theme name
 *
 * Returns: (transfer none) (nullable): the theme's one-line description, or
 *   %NULL for an unknown theme
 */
const gchar *ls_components_get_theme_summary(LsComponents *self, const gchar *name);
/**
 * ls_components_get_themes:
 * @self: a component library
 *
 * Returns: (transfer none): the theme catalog as loaded, theme name to
 *   theme; presets are the themes with a design block
 */
JsonObject *ls_components_get_themes(LsComponents *self);
/**
 * ls_components_get_library:
 * @self: a component library
 *
 * Returns: (transfer none) (nullable): the catalog's library block that
 *   the design picker reads (see ls-design.h), or %NULL for an archived
 *   library without one
 */
JsonObject *ls_components_get_library(LsComponents *self);
/**
 * ls_components_apply_design:
 * @self: a component library
 * @plan: (transfer none): a site plan object, modified in place
 * @design: (transfer none): a design from ls_design_new(), copied
 *
 * Writes @design into the plan as its builder-owned design member, sets
 * the plan's working colour tokens to the design's repaired colour roles
 * and the home opening in structure to the design's hero variant. Apply
 * the design's preset as a theme first.
 *
 * Returns: whether @plan was an object and changed
 */
gboolean ls_components_apply_design(LsComponents *self, JsonNode *plan, JsonObject *design);
/**
 * ls_components_design_home:
 * @self: a component library
 * @plan: (transfer none): the site plan, borrowed
 * @page: (transfer none): the home page specification, modified in place
 *
 * Makes the home page the design's ordered list of sections: each designed
 * section takes the writer's section of the same type (a cover counts as a
 * hero, voices as reviews, a contact card or call as contact) in the
 * designed variant, keeping only the slots that variant has; reviews take
 * the order's supplied quotes; photo cards take a supplied photo each that
 * the opening does not show; every other section is dropped. A designed
 * section the writer did not write is left out. Without a plan design the
 * page is unchanged.
 *
 * Returns: whether @page changed
 */
gboolean ls_components_design_home(LsComponents *self, JsonNode *plan, JsonNode *page);
/**
 * ls_components_home_questions:
 * @plan: (transfer none) (nullable): the site plan, borrowed
 *
 * The fewest questions the FAQ on a designed home must hold: eight, or twelve
 * when the home shows none of the order's own facts (no trust strip,
 * process, about, area, gallery or reviews). The page check holds the
 * home to it and the home writer is told it.
 *
 * Returns: the minimum, or 0 without a design that fixes the pages
 */
guint ls_components_home_questions(JsonNode *plan);
/**
 * ls_components_apply_theme:
 * @self: a component library
 * @plan: (transfer none): a site plan object, modified in place
 * @name: a theme name
 * @error: return location for an error
 *
 * Replaces the plan's tokens, fonts, look, css and restyle with the theme's,
 * so every component on every page takes the theme when the site is
 * composed. Content, routes, navigation and art are untouched. Every bundled
 * theme passes the plan checks on its own.
 * Returns: whether the theme was applied; %FALSE for an unknown theme
 */
gboolean ls_components_apply_theme(LsComponents *self, JsonNode *plan, const gchar *name, GError **error);
/**
 * ls_components_get_themed_plan_schema:
 * @self: a component library
 *
 * The plan response schema without the parts a theme supplies (tokens,
 * fonts, look, css and restyle), for plan steps that name a theme.
 * Returns: (transfer none): JSON schema text
 */
const gchar *ls_components_get_themed_plan_schema(LsComponents *self);
/**
 * ls_components_describe:
 * @self: a component library
 * Returns: (transfer full): model-facing reference for page specifications:
 *   rich-text and link syntax plus every component, variant and slot
 */
gchar *ls_components_describe(LsComponents *self);
/**
 * ls_components_check_plan:
 * @self: a component library
 * @plan: (transfer none): candidate site plan; borrowed, never modified
 * @error: return location for an error
 *
 * Checks the plan contract: tokens (including WCAG AA text contrast), pages,
 * navigation, art and site-wide CSS. On failure the %LS_ERROR_INVALID message
 * lists every problem found, one per line, so a model can fix them at once.
 * Returns: whether the plan is valid
 */
gboolean ls_components_check_plan(LsComponents *self, JsonNode *plan, GError **error);
/**
 * ls_components_repair_plan:
 * @self: a component library
 * @plan: (transfer none): a plan object to repair in place
 *
 * Fixes purely mechanical token misses so they never cost a model retry:
 * text colors move toward black or white only as far as WCAG AA needs, and
 * numeric tokens are clamped into their documented ranges. Nothing else in
 * the plan changes. Run ls_components_check_plan() afterwards.
 * Returns: (transfer full) (nullable): one line per repair, or NULL when
 *   nothing needed repair
 */
gchar *ls_components_repair_plan(LsComponents *self, JsonNode *plan);
/**
 * ls_components_check_page:
 * @self: a component library
 * @plan: (transfer none): a plan accepted by ls_components_check_plan()
 * @path: the planned route this specification must describe
 * @page: (transfer none): candidate page specification; never modified
 * @error: return location for an error
 *
 * Checks sections against the catalog: known components and variants, slot
 * types and limits, links to planned routes or existing anchors, planned art
 * and sanitized custom markup and page CSS. Problems are listed one per line.
 * Returns: whether the page specification is valid
 */
gboolean ls_components_check_page(LsComponents *self, JsonNode *plan,
	const gchar *path, JsonNode *page, GError **error);
/**
 * ls_components_check_art:
 * @svg: candidate standalone SVG document
 * @error: return location for an error
 *
 * Requires a well-formed SVG root with the SVG namespace and a viewBox, and
 * rejects scripts, foreign content, event handlers, external references and
 * documents over 256 KiB.
 * Returns: whether the artwork is safe to publish
 */
gboolean ls_components_check_art(const gchar *svg, GError **error);
/**
 * ls_components_social:
 * @self: a component library
 * @links: (transfer none) (nullable): supplied profile links, each
 *   {url, label?} with an https:// url
 *
 * Recognises the network of each supplied profile address from the
 * library's network list (a listed host or its subdomain, a federated
 * @name path, otherwise "web") and derives the handle visitors see. Entries
 * without a valid https:// address are skipped. The result is the plan's
 * builder-owned social member; nothing is looked up online.
 * Returns: (transfer full): [{url, network, label, handle}]
 */
JsonArray *ls_components_social(LsComponents *self, JsonArray *links);
/**
 * ls_components_check_signature:
 * @css: (nullable): a site's signature layer
 * @error: return location for an error
 *
 * Checks the per-site signature layer written beside the pages: CSS of at
 * most 8000 bytes whose colours are only the site's custom properties, whose
 * url() values are only percent-encoded SVG data images, and which never
 * hides, pins or forces content (no display:none, visibility:hidden,
 * position:fixed or !important) and holds no markup, imports or scripts.
 * Returns: whether the layer may be published
 */
gboolean ls_components_check_signature(const gchar *css, GError **error);

/**
 * LS_COMPONENTS_SIGNATURE_TOO_CLOSE:
 *
 * The ls_components_signature_overlap() share at or above which two
 * signature layers are the same flourish.
 */
#define LS_COMPONENTS_SIGNATURE_TOO_CLOSE 0.5
/**
 * ls_components_fingerprint:
 * @self: a component library
 * @plan: (transfer none): a site plan
 * @home: (transfer none) (nullable): the home page specification
 *
 * Summarises what a visitor notices about a composed site: every look
 * axis, the paper, ink and accent colours, the display and body families,
 * the home page's opening and section sequence, and hashed shape-carrying
 * declarations of its signature layer. It holds no copy, facts or
 * photographs, so an embedder can keep one per delivered site and pass
 * earlier ones back as site.avoid, where the signature layer is checked
 * for clones.
 * Returns: (transfer full): a version 1 fingerprint object
 */
JsonObject *ls_components_fingerprint(LsComponents *self, JsonNode *plan, JsonNode *home);
/**
 * ls_components_signature_overlap:
 * @a: (transfer none): a fingerprint
 * @b: (transfer none): another fingerprint
 *
 * Returns: the Jaccard share of the two signature layers' hashed
 *   declarations, 0 when either has none
 */
gdouble ls_components_signature_overlap(JsonObject *a, JsonObject *b);
/**
 * ls_components_compose:
 * @self: a component library
 * @plan: (transfer none): a valid plan
 * @pages: (transfer none): planned route path to page specification
 * @base_url: (nullable): absolute https:// site address; enables canonical
 *   URLs, sitemap.xml and the robots.txt sitemap line
 * @error: return location for an error
 *
 * Renders every planned page, the built-in 404, privacy and terms pages,
 * styles.css, site.js, robots.txt and, with @base_url, sitemap.xml. Every
 * page specification is re-checked first; nothing is returned on failure.
 * Returns: (transfer full) (nullable): a JSON file bundle for ls_context_apply()
 */
gchar *ls_components_compose(LsComponents *self, JsonNode *plan, JsonObject *pages,
	const gchar *base_url, GError **error);
/**
 * ls_components_render_rich:
 * @text: rich text in the documented subset
 * @error: return location for an error
 *
 * Renders paragraphs, lists, strong, emphasis, code and links to escaped
 * HTML. Link schemes are checked; routes are not checked against a plan.
 * Returns: (transfer full) (nullable): HTML, or NULL for an unsafe link
 */
gchar *ls_components_render_rich(const gchar *text, GError **error);
G_END_DECLS
