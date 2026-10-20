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
 * ls_components_suggest_look:
 * @self: a component library
 * @seed: stable text identifying the site, such as the recipe name and brief
 *
 * Picks one value per look axis from a hash of @seed, so different sites
 * start from different skeletons while one site always gets the same
 * suggestion. The plan may keep or change any axis.
 * Returns: (transfer full): axis name to look value
 */
JsonObject *ls_components_suggest_look(LsComponents *self, const gchar *seed);
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
 * ls_components_deal_structure:
 * @self: a component library
 * @seed: stable text identifying the site
 *
 * Deals the site's structure genes from a hash of @seed: continuous column
 * width, edge gutter, section pace, inner padding, card width and display
 * scale, how often sections narrow, bleed, band, shift, loosen or tighten,
 * and one variant per dealable component. The same seed always deals the
 * same structure. See docs/components.org.
 * Returns: (transfer full): the structure object a plan carries
 */
JsonObject *ls_components_deal_structure(LsComponents *self, const gchar *seed);
/**
 * ls_components_apply_structure:
 * @self: a component library
 * @plan: (transfer none): plan to change in place
 * @seed: stable text identifying the site
 * @themed: whether a theme already set the plan's look
 *
 * Makes the builder authoritative for page structure: replaces any
 * structure in @plan with ls_components_deal_structure(), and sets every
 * look axis the catalog marks builder-owned (the page frame, heading
 * position, header, rhythm, alignment, footer and motion) to the value
 * dealt from @seed. With @themed, axes the theme already named keep the
 * theme's value.
 * Returns: whether @plan was an object and changed
 */
gboolean ls_components_apply_structure(LsComponents *self, JsonNode *plan, const gchar *seed, gboolean themed);
/**
 * ls_components_apply_seed:
 * @self: a component library
 * @plan: (transfer none): plan to change in place
 * @seed: stable text identifying the site
 * @sector: (nullable): the business sector identities are dealt by
 * @themed: whether a theme already set the plan's design
 *
 * Makes the seed, not the model, decide the site's design: applies
 * ls_components_apply_structure() (which also deals the home opening as
 * structure.opening), then, without a theme, sets every remaining look axis
 * to ls_components_suggest_look() except the stock flourishes, and the
 * colour tokens, font tokens and web fonts to the identity
 * ls_components_suggest_identity() deals. A plan without web fonts keeps
 * its own font tokens, so an order that forbids remote fonts still gets
 * none. The same seed always gives the same design.
 * Returns: whether @plan was an object and changed
 */
gboolean ls_components_apply_seed(LsComponents *self, JsonNode *plan, const gchar *seed, const gchar *sector,
	gboolean themed);
/**
 * ls_components_apply_opening:
 * @self: a component library
 * @plan: (transfer none): a plan whose structure names an opening
 * @path: the home page's route
 * @page: (transfer none): the home page specification, changed in place
 *
 * Rewrites the page's opening section (a hero, cover, poster or manifesto)
 * as the component and variant in the plan's structure.opening, carrying
 * over its kicker, heading, lede, actions and art; a cover or a split hero
 * without art borrows the plan's first artwork other than the mark. When
 * the dealt opening cannot hold the words (a heading too long for a
 * poster, too few lines for a manifesto, no art at all), the next opening
 * in a fixed order from the dealt one that can is used. Custom openings,
 * pages that do not open with an opening component and plans without
 * structure.opening are left alone.
 * Returns: whether @page changed
 */
gboolean ls_components_apply_opening(LsComponents *self, JsonNode *plan, const gchar *path, JsonNode *page);
/**
 * ls_components_suggest_opening:
 * @self: a component library
 * @seed: stable text identifying the site
 *
 * Picks one home-page opening as "component/variant" from a hash of @seed,
 * so home pages rotate between heroes, covers, posters and manifestos.
 * Returns: (transfer full) (nullable): the suggestion, or %NULL when the
 *   home kind lists no component openings
 */
gchar *ls_components_suggest_opening(LsComponents *self, const gchar *seed);
/**
 * ls_components_list_sectors:
 * @self: a component library
 *
 * Returns: (transfer full) (array zero-terminated=1): the business sectors
 *   that identities are dealt by, in catalog order
 */
gchar **ls_components_list_sectors(LsComponents *self);
/**
 * ls_components_suggest_identity:
 * @self: a component library
 * @seed: stable text identifying the site
 * @sector: (nullable): a sector from ls_components_list_sectors(), or %NULL
 *   (or an unknown name) to deal from every entry
 *
 * Deals a palette, a type pairing and a home-page section sequence that fit
 * @sector, picked from a hash of @seed, so sites start from designs suited to
 * their business instead of one default. The plan adapts them.
 * Returns: (transfer full): {palette: {name, summary, tokens},
 *   type: {name, summary, tokens, fonts}, home: {name, sections}}
 */
JsonObject *ls_components_suggest_identity(LsComponents *self, const gchar *seed, const gchar *sector);
/**
 * ls_components_check_identity:
 * @self: a component library
 * @plan: a site plan
 * @identity: (nullable): the identity dealt to this site
 * @error: return location for an error
 *
 * Rejects a plan whose tokens fall back on the house default (warm cream
 * paper with a rust accent, or Fraunces over Source Sans) unless @identity
 * itself dealt it. The error, %LS_ERROR_VALIDATION, is written to be sent
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
 * LS_COMPONENTS_TOO_CLOSE:
 *
 * The ls_components_similarity() score at or above which two sites read
 * as the same design.
 */
#define LS_COMPONENTS_TOO_CLOSE 0.6
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
 * axis, the column and spacing genes, the paper, ink and accent colours,
 * the display and body families, the home page's opening and section
 * sequence, and hashed shape-carrying declarations of its signature layer.
 * It holds no copy, facts or photographs, so an embedder can keep one per
 * delivered site and pass earlier ones back as site.avoid.
 * Returns: (transfer full): a version 1 fingerprint object
 */
JsonObject *ls_components_fingerprint(LsComponents *self, JsonNode *plan, JsonNode *home);
/**
 * ls_components_similarity:
 * @a: (transfer none): a fingerprint
 * @b: (transfer none): another fingerprint
 *
 * Scores how alike two sites look from their layout, colours, type and
 * home page, ignoring the signature layer. Parts missing from either side
 * are left out of the score; malformed parts count as different.
 * Returns: 0 (nothing shared) to 1 (the same design)
 */
gdouble ls_components_similarity(JsonObject *a, JsonObject *b);
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
 * ls_components_choose_seed:
 * @self: a component library
 * @seed: the site seed (recipe name and brief)
 * @sector: (nullable): the business sector for identity dealing
 * @avoid: (transfer none) (nullable): earlier sites' fingerprints
 * @closest: (out) (optional): the chosen deal's highest similarity to @avoid
 *
 * Deals the structure, look and identity from @seed and, when that deal
 * lands at or above %LS_COMPONENTS_TOO_CLOSE to any fingerprint in @avoid,
 * re-deals from up to 31 derived seeds, keeping the first clear deal or
 * else the least similar. Without @avoid the seed is returned unchanged,
 * so existing sites keep their deal.
 * Returns: (transfer full): the seed to deal this site from
 */
gchar *ls_components_choose_seed(LsComponents *self, const gchar *seed, const gchar *sector, JsonArray *avoid,
	gdouble *closest);
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
