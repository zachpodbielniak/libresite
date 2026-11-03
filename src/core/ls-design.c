/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* The design picker: presets, eligible variants, the clash rules and the
 * design tokens (docs/components.org, "Presets and the picker"). */
#include "core/ls-design.h"
#include "core/ls-util.h"
#include "libresite.h"
#include <math.h>
#include <string.h>

static const gchar *const role_names[] = {"bg", "fg", "surface", "surface_fg", "muted_fg", "line", "primary",
	"primary_fg", "primary_text", "brand", "brand_fg", "brand_muted", "brand_line", "brand_accent", "inverse", "inverse_fg",
	"inverse_muted", "inverse_line", "highlight", "photo_primary", "photo_primary_fg", "focus"};

/* Text pairs every design keeps at AA: {ground, text}. */
static const gchar *const text_pairs[][2] = {{"bg", "fg"}, {"bg", "muted_fg"}, {"surface", "surface_fg"},
	{"surface", "muted_fg"}, {"bg", "primary_text"}, {"surface", "primary_text"}, {"primary", "primary_fg"},
	{"brand", "brand_fg"}, {"brand", "brand_muted"}, {"inverse", "inverse_fg"}, {"inverse", "inverse_muted"},
	{"photo_primary", "photo_primary_fg"}, {"brand", "brand_accent"}};

/* ls_string() for a member of an object that may be missing. */
static const gchar *
str_of(JsonObject *object, const gchar *key){
	return object != NULL ? ls_string(object, key) : NULL;
}

static JsonObject *
object_of(JsonObject *object, const gchar *key){
	JsonNode *node = object != NULL ? json_object_get_member(object, key) : NULL;
	return node != NULL && JSON_NODE_HOLDS_OBJECT(node) ? json_node_get_object(node) : NULL;
}

static JsonArray *
array_of(JsonObject *object, const gchar *key){
	JsonNode *node = object != NULL ? json_object_get_member(object, key) : NULL;
	return node != NULL && JSON_NODE_HOLDS_ARRAY(node) ? json_node_get_array(node) : NULL;
}

/* ---- Colour --------------------------------------------------------------- */

static gboolean
parse_hex(const gchar *text, gdouble rgb[3]){
	guint i;
	if (text == NULL || strlen(text) != 7 || text[0] != '#')
		return FALSE;
	for (i = 1; i < 7; i++)
		if (!g_ascii_isxdigit(text[i]))
			return FALSE;
	for (i = 0; i < 3; i++)
		rgb[i] = (g_ascii_xdigit_value(text[1 + i * 2]) * 16 + g_ascii_xdigit_value(text[2 + i * 2])) / 255.0;
	return TRUE;
}

static gchar *
format_hex(const gdouble rgb[3]){
	guint v[3], i;
	for (i = 0; i < 3; i++)
		v[i] = (guint)CLAMP(lround(rgb[i] * 255.0), 0, 255);
	return g_strdup_printf("#%02x%02x%02x", v[0], v[1], v[2]);
}

static gdouble
luminance(const gdouble rgb[3]){
	gdouble out[3];
	guint i;
	for (i = 0; i < 3; i++)
		out[i] = rgb[i] <= 0.04045 ? rgb[i] / 12.92 : pow((rgb[i] + 0.055) / 1.055, 2.4);
	return 0.2126 * out[0] + 0.7152 * out[1] + 0.0722 * out[2];
}

gdouble
ls_design_contrast(const gchar *a, const gchar *b){
	gdouble x[3], y[3], lx, ly;
	if (!parse_hex(a, x) || !parse_hex(b, y))
		return 1;
	lx = luminance(x);
	ly = luminance(y);
	return (MAX(lx, ly) + 0.05) / (MIN(lx, ly) + 0.05);
}

static void
to_hsl(const gdouble rgb[3], gdouble *h, gdouble *s, gdouble *l){
	gdouble max = MAX(rgb[0], MAX(rgb[1], rgb[2])), min = MIN(rgb[0], MIN(rgb[1], rgb[2]));
	gdouble d = max - min;
	*l = (max + min) / 2;
	*h = 0;
	*s = 0;
	if (d <= 0)
		return;
	*s = *l > 0.5 ? d / (2 - max - min) : d / (max + min);
	if (max == rgb[0])
		*h = fmod((rgb[1] - rgb[2]) / d + 6, 6);
	else if (max == rgb[1])
		*h = (rgb[2] - rgb[0]) / d + 2;
	else
		*h = (rgb[0] - rgb[1]) / d + 4;
	*h /= 6;
}

static gdouble
hue_part(gdouble p, gdouble q, gdouble t){
	if (t < 0)
		t += 1;
	if (t > 1)
		t -= 1;
	if (t < 1.0 / 6)
		return p + (q - p) * 6 * t;
	if (t < 0.5)
		return q;
	if (t < 2.0 / 3)
		return p + (q - p) * (2.0 / 3 - t) * 6;
	return p;
}

static void
from_hsl(gdouble h, gdouble s, gdouble l, gdouble rgb[3]){
	gdouble q, p;
	if (s <= 0) {
		rgb[0] = rgb[1] = rgb[2] = l;
		return;
	}
	q = l < 0.5 ? l * (1 + s) : l + s - l * s;
	p = 2 * l - q;
	rgb[0] = hue_part(p, q, h + 1.0 / 3);
	rgb[1] = hue_part(p, q, h);
	rgb[2] = hue_part(p, q, h - 1.0 / 3);
}

gchar *
ls_design_repair(const gchar *color, const gchar *against, gdouble minimum){
	gdouble rgb[3], other[3], h, s, l, step;
	gboolean darker;
	guint pass;
	if (!parse_hex(color, rgb) || !parse_hex(against, other))
		return g_strdup(color);
	if (ls_design_contrast(color, against) >= minimum)
		return g_ascii_strdown(color, -1);
	to_hsl(rgb, &h, &s, &l);
	/* Away from the other colour first: darker against a lighter one. */
	darker = luminance(other) >= luminance(rgb);
	for (pass = 0; pass < 2; pass++, darker = !darker)
		for (step = 0.005; step <= 1.0; step += 0.005) {
			gdouble moved[3];
			g_autofree gchar *hex = NULL;
			from_hsl(h, s, CLAMP(darker ? l - step : l + step, 0, 1), moved);
			hex = format_hex(moved);
			if (ls_design_contrast(hex, against) >= minimum)
				return g_steal_pointer(&hex);
		}
	return g_strdup(luminance(other) >= 0.18 ? "#000000" : "#ffffff");
}

/* @a moved toward @b by @t, as #rrggbb. */
static gchar *
mix(const gchar *a, const gchar *b, gdouble t){
	gdouble x[3], y[3], out[3];
	guint i;
	if (!parse_hex(a, x) || !parse_hex(b, y))
		return g_strdup(a);
	for (i = 0; i < 3; i++)
		out[i] = x[i] + (y[i] - x[i]) * t;
	return format_hex(out);
}

static gdouble
lightness_of(const gchar *hex){
	gdouble rgb[3];
	return parse_hex(hex, rgb) ? luminance(rgb) : 0;
}

/* ---- Words and the hash --------------------------------------------------- */

guint32
ls_design_hash(const gchar *text){
	guint32 hash = 2166136261u;
	const gchar *p;
	for (p = text != NULL ? text : ""; *p != '\0'; p++) {
		hash ^= (guint8)g_ascii_tolower(*p);
		hash *= 16777619u;
	}
	return hash;
}

static void
add_words(GPtrArray *words, const gchar *text){
	g_autofree gchar *lower = NULL;
	const gchar *p, *start = NULL;
	if (text == NULL)
		return;
	lower = g_ascii_strdown(text, -1);
	for (p = lower;; p++) {
		if (*p != '\0' && g_ascii_isalpha(*p)) {
			if (start == NULL)
				start = p;
			continue;
		}
		if (start != NULL)
			g_ptr_array_add(words, g_strndup(start, (gsize)(p - start)));
		start = NULL;
		if (*p == '\0')
			break;
	}
}

/* The sentences of @text that open with @label (such as "Style:"). */
static void
add_labelled(GPtrArray *words, const gchar *text, const gchar *label){
	const gchar *at = text;
	gsize length = strlen(label);
	while (at != NULL && (at = strstr(at, label)) != NULL) {
		const gchar *end = strchr(at + length, '.');
		g_autofree gchar *sentence = end != NULL ? g_strndup(at + length, (gsize)(end - at - length))
			: g_strdup(at + length);
		add_words(words, sentence);
		at += length;
	}
}

GPtrArray *
ls_design_owner_words(JsonObject *site){
	GPtrArray *words = g_ptr_array_new_with_free_func(g_free);
	JsonObject *brand = object_of(site, "brand");
	const gchar *requirements = site != NULL ? str_of(site, "requirements") : NULL;
	const gchar *labels[] = {"Colours:", "Colors:", "Style:"};
	guint i;
	if (site == NULL)
		return words;
	add_words(words, str_of(site, "style"));
	add_words(words, str_of(brand, "colors"));
	add_words(words, str_of(brand, "tone"));
	for (i = 0; requirements != NULL && i < G_N_ELEMENTS(labels); i++)
		add_labelled(words, requirements, labels[i]);
	return words;
}

static gboolean
has_word(GPtrArray *words, const gchar *word){
	guint i;
	for (i = 0; i < words->len; i++)
		if (g_str_equal(g_ptr_array_index(words, i), word))
			return TRUE;
	return FALSE;
}

static gboolean
list_has(JsonArray *list, const gchar *word){
	guint i;
	for (i = 0; list != NULL && i < json_array_get_length(list); i++)
		if (g_strcmp0(json_array_get_string_element(list, i), word) == 0)
			return TRUE;
	return FALSE;
}

static const gchar *
business_name(JsonObject *site){
	const gchar *name = str_of(object_of(site, "brand"), "name");
	return name != NULL ? name : "";
}

/* The business's sector: the one whose words its name, tagline and offers
 * use most; "any" when none does. */
static const gchar *
sector_of(JsonObject *library, JsonObject *site){
	JsonObject *sectors = object_of(library, "sectors");
	g_autoptr(GPtrArray) words = g_ptr_array_new_with_free_func(g_free);
	g_autoptr(GList) names = sectors != NULL ? json_object_get_members(sectors) : NULL;
	JsonArray *offers = array_of(site, "offers");
	const gchar *best = "any";
	guint best_score = 0, i;
	GList *name;
	add_words(words, business_name(site));
	add_words(words, str_of(object_of(site, "brand"), "tagline"));
	add_words(words, site != NULL ? str_of(site, "brief") : NULL);
	for (i = 0; offers != NULL && i < json_array_get_length(offers); i++)
		if (JSON_NODE_HOLDS_OBJECT(json_array_get_element(offers, i)))
			add_words(words, str_of(json_array_get_object_element(offers, i), "name"));
	for (name = names; name != NULL; name = name->next) {
		JsonArray *list = array_of(object_of(sectors, name->data), "words");
		guint score = 0;
		for (i = 0; i < words->len; i++)
			score += list_has(list, g_ptr_array_index(words, i)) ? 1 : 0;
		if (score > best_score) {
			best_score = score;
			best = name->data;
		}
	}
	return best;
}

/* ---- Picking -------------------------------------------------------------- */

static gint64
weight(JsonObject *library, const gchar *key){
	JsonObject *weights = object_of(library, "weights");
	return weights != NULL && json_object_has_member(weights, key)
		? MAX(1, json_object_get_int_member(weights, key)) : 1;
}

typedef struct {
	const gchar *name;
	gint64 weight;
} Choice;

/* One of @choices, by a stable hash of @name and @salt over their weights. */
static const gchar *
weighted(GArray *choices, const gchar *name, const gchar *salt){
	g_autofree gchar *key = g_strconcat(name, "/", salt, NULL);
	guint64 total = 0, at;
	guint i;
	if (choices->len == 0)
		return NULL;
	for (i = 0; i < choices->len; i++)
		total += (guint64)g_array_index(choices, Choice, i).weight;
	at = ls_design_hash(key) % total;
	for (i = 0; i < choices->len; i++) {
		guint64 w = (guint64)g_array_index(choices, Choice, i).weight;
		if (at < w)
			return g_array_index(choices, Choice, i).name;
		at -= w;
	}
	return g_array_index(choices, Choice, choices->len - 1).name;
}

gchar *
ls_design_pick_preset(JsonObject *library, JsonObject *themes, JsonObject *site){
	g_autoptr(GList) names = themes != NULL ? json_object_get_members(themes) : NULL;
	g_autoptr(GPtrArray) words = ls_design_owner_words(site);
	g_autoptr(GArray) matched = g_array_new(FALSE, FALSE, sizeof(Choice));
	g_autoptr(GArray) all = g_array_new(FALSE, FALSE, sizeof(Choice));
	g_autoptr(GArray) fresh = g_array_new(FALSE, FALSE, sizeof(Choice));
	JsonArray *avoid = array_of(site, "avoid");
	const gchar *sector = sector_of(library, site);
	guint best = 0;
	GList *name;
	for (name = names; name != NULL; name = name->next) {
		JsonObject *design = object_of(object_of(themes, name->data), "design");
		JsonArray *list = array_of(design, "words");
		Choice choice;
		guint score = 0, i;
		if (design == NULL)
			continue;
		for (i = 0; list != NULL && i < json_array_get_length(list); i++)
			score += has_word(words, json_array_get_string_element(list, i)) ? 1 : 0;
		choice.name = name->data;
		choice.weight = list_has(array_of(design, "fits"), sector) ? weight(library, "fit") : weight(library, "other");
		g_array_append_val(all, choice);
		/* Nearby sites' presets (site.avoid) leave the hash pick. */
		{
			gboolean avoided = FALSE;
			guint a;
			for (a = 0; avoid != NULL && a < json_array_get_length(avoid); a++)
				if (JSON_NODE_HOLDS_OBJECT(json_array_get_element(avoid, a)) &&
					g_strcmp0(str_of(json_array_get_object_element(avoid, a), "preset"), name->data) == 0)
					avoided = TRUE;
			if (!avoided)
				g_array_append_val(fresh, choice);
		}
		if (score > 0 && score >= best) {
			if (score > best)
				g_array_set_size(matched, 0);
			best = score;
			choice.weight = 1;
			g_array_append_val(matched, choice);
		}
	}
	if (all->len == 0)
		return NULL;
	/* The owner's words win; otherwise the hash picks among the presets no
	 * nearby site wears, or among all when every one is worn. */
	return g_strdup(weighted(matched->len > 0 ? matched : fresh->len > 0 ? fresh : all, business_name(site), "preset"));
}

typedef struct {
	guint photos;	/* supplied photos */
	guint items;	/* supplied offers */
	guint quotes;	/* supplied reviews */
	gboolean phone;
	gboolean booking;
	const gchar *action;	/* the design's primary action, once chosen */
} Content;

static Content
content_of(JsonObject *site, JsonArray *photos){
	Content content;
	JsonObject *contact = object_of(site, "contact");
	JsonObject *booking = object_of(site, "booking");
	content.photos = photos != NULL ? json_array_get_length(photos) : 0;
	content.items = array_of(site, "offers") != NULL ? json_array_get_length(array_of(site, "offers")) : 0;
	content.quotes = array_of(site, "reviews") != NULL ? json_array_get_length(array_of(site, "reviews")) : 0;
	content.phone = str_of(contact, "phone") != NULL;
	content.booking = booking != NULL && str_of(booking, "href") != NULL;
	content.action = NULL;
	return content;
}

static gboolean
in_range(JsonObject *needs, const gchar *key, guint value){
	JsonArray *range = array_of(needs, key);
	if (range == NULL || json_array_get_length(range) != 2)
		return TRUE;
	return value >= (guint)json_array_get_int_element(range, 0) && value <= (guint)json_array_get_int_element(range, 1);
}

/* Whether content lets @pair show: its photos, items, quotes and phone. */
static gboolean
eligible(JsonObject *library, const gchar *pair, Content *content){
	JsonObject *needs = object_of(object_of(object_of(library, "variants"), pair), "needs");
	if (object_of(object_of(library, "variants"), pair) == NULL)
		return FALSE;
	if (needs == NULL)
		return TRUE;
	if (json_object_has_member(needs, "photos") &&
		content->photos < (guint)json_object_get_int_member(needs, "photos"))
		return FALSE;
	if (json_object_has_member(needs, "phone") && json_object_get_boolean_member(needs, "phone") && !content->phone)
		return FALSE;
	/* A layout built around one action shows only where that action leads. */
	if (json_object_has_member(needs, "action") && g_strcmp0(str_of(needs, "action"), content->action) != 0)
		return FALSE;
	/* One photo for every card, after the opening's. */
	if (json_object_has_member(needs, "item_photos") && json_object_get_boolean_member(needs, "item_photos") &&
		(content->photos < 1 || content->photos - 1 < content->items))
		return FALSE;
	return in_range(needs, "items", content->items) && in_range(needs, "quotes", content->quotes) &&
		(!json_object_has_member(needs, "quotes") || content->quotes > 0);
}

static const gchar *
family_of(JsonObject *library, const gchar *component, const gchar *variant){
	g_autofree gchar *pair = g_strdup_printf("%s/%s", component, variant);
	const gchar *family = str_of(object_of(object_of(library, "variants"), pair), "family");
	/* Library strings outlive the pair; an unlisted opening is plain. */
	return family != NULL ? family : "plain";
}

/* The eligible variants of @type with their weights; the preset's
 * preferred one weighs most. Member names outlive the list. */
static GArray *
choices_for(JsonObject *library, JsonObject *design, const gchar *type, Content *content){
	JsonObject *variants = object_of(library, "variants");
	g_autoptr(GList) pairs = variants != NULL ? json_object_get_members(variants) : NULL;
	GArray *choices = g_array_new(FALSE, FALSE, sizeof(Choice));
	JsonNode *wish = object_of(design, "prefer") != NULL ? json_object_get_member(object_of(design, "prefer"), type) : NULL;
	const gchar *prefer = NULL;
	gsize length = strlen(type);
	GList *pair;
	/* The preset's preference, or the first eligible one of its list. */
	if (wish != NULL && JSON_NODE_HOLDS_VALUE(wish))
		prefer = json_node_get_string(wish);
	else if (wish != NULL && JSON_NODE_HOLDS_ARRAY(wish)) {
		JsonArray *list = json_node_get_array(wish);
		guint i;
		for (i = 0; prefer == NULL && i < json_array_get_length(list); i++) {
			g_autofree gchar *candidate = g_strdup_printf("%s/%s", type, json_array_get_string_element(list, i));
			if (eligible(library, candidate, content))
				prefer = json_array_get_string_element(list, i);
		}
	}
	for (pair = pairs; pair != NULL; pair = pair->next) {
		const gchar *name = pair->data;
		Choice choice;
		if (strncmp(name, type, length) != 0 || name[length] != '/' || !eligible(library, name, content))
			continue;
		choice.name = name + length + 1;
		choice.weight = g_strcmp0(choice.name, prefer) == 0 ? weight(library, "prefer") : weight(library, "other");
		g_array_append_val(choices, choice);
	}
	return choices;
}

static void
add_section(JsonArray *home, const gchar *component, const gchar *variant){
	JsonObject *section = json_object_new();
	json_object_set_string_member(section, "component", component);
	json_object_set_string_member(section, "variant", variant);
	json_array_add_object_element(home, section);
}

/* Two sections of one family never touch: a later section moves to the
 * heaviest eligible variant of another family, when it has one. */
static void
separate_families(JsonObject *library, JsonObject *design, JsonArray *home, Content *content){
	guint i, j;
	for (i = 1; i < json_array_get_length(home); i++) {
		JsonObject *before = json_array_get_object_element(home, i - 1);
		JsonObject *here = json_array_get_object_element(home, i);
		const gchar *component = str_of(here, "component");
		g_autoptr(GArray) choices = NULL;
		const gchar *pick = NULL;
		gint64 heaviest = 0;
		if (g_strcmp0(family_of(library, str_of(before, "component"), str_of(before, "variant")),
			family_of(library, component, str_of(here, "variant"))) != 0)
			continue;
		choices = choices_for(library, design, component, content);
		for (j = 0; j < choices->len; j++) {
			Choice *choice = &g_array_index(choices, Choice, j);
			if (g_strcmp0(family_of(library, str_of(before, "component"), str_of(before, "variant")),
				family_of(library, component, choice->name)) != 0 && choice->weight > heaviest) {
				heaviest = choice->weight;
				pick = choice->name;
			}
		}
		if (pick != NULL)
			json_object_set_string_member(here, "variant", pick);
	}
}

/* Tones: the opening keeps its own ground, then the sections alternate
 * between the page ground and the surface, and the last one differs from
 * the footer's. */
static void
assign_tones(JsonObject *library, JsonArray *home, const gchar *footer){
	g_autofree gchar *footer_pair = g_strdup_printf("footer/%s", footer != NULL ? footer : "");
	const gchar *footer_tone = str_of(object_of(object_of(library, "variants"), footer_pair), "tone");
	guint i, n = json_array_get_length(home);
	for (i = 0; i < n; i++) {
		JsonObject *section = json_array_get_object_element(home, i);
		const gchar *tone;
		if (i == 0)
			tone = g_strcmp0(family_of(library, str_of(section, "component"), str_of(section, "variant")),
				"bleed") == 0 ? "photo" : "default";
		else
			tone = i % 2 == 1 ? "default" : "surface";
		if (i == n - 1 && i > 0 && g_strcmp0(tone, footer_tone) == 0)
			tone = "default";
		json_object_set_string_member(section, "tone", tone);
	}
}

/* The colour roles: the preset's tokens and roles, the owner's named colour
 * on every role tied to the brand colour, then every text pair at AA. */
static JsonObject *
build_roles(JsonObject *library, JsonObject *theme, JsonObject *site, JsonArray *applied){
	JsonObject *tokens = object_of(theme, "tokens"), *given = object_of(object_of(theme, "design"), "roles");
	JsonObject *roles = json_object_new();
	g_autoptr(GPtrArray) words = ls_design_owner_words(site);
	JsonObject *colors = object_of(library, "colors");
	JsonArray *neutral = array_of(library, "neutral");
	const gchar *named = NULL, *word = NULL;
	g_autofree gchar *brand = NULL;
	guint i;
	json_object_set_string_member(roles, "bg", str_of(tokens, "paper"));
	json_object_set_string_member(roles, "fg", str_of(tokens, "ink"));
	json_object_set_string_member(roles, "surface", str_of(tokens, "surface"));
	json_object_set_string_member(roles, "surface_fg", str_of(tokens, "ink"));
	json_object_set_string_member(roles, "muted_fg", str_of(tokens, "muted"));
	json_object_set_string_member(roles, "line", str_of(tokens, "line"));
	json_object_set_string_member(roles, "primary", str_of(tokens, "accent"));
	json_object_set_string_member(roles, "primary_fg", str_of(tokens, "accent_ink"));
	{
		g_autoptr(GList) keys = given != NULL ? json_object_get_members(given) : NULL;
		GList *key;
		for (key = keys; key != NULL; key = key->next)
			json_object_set_string_member(roles, key->data, str_of(given, key->data));
	}
	/* The owner's first named colour that is not a neutral. */
	for (i = 0; i < words->len && named == NULL; i++) {
		const gchar *candidate = g_ptr_array_index(words, i);
		if (!list_has(neutral, candidate) && colors != NULL && str_of(colors, candidate) != NULL) {
			named = str_of(colors, candidate);
			word = candidate;
		}
	}
	brand = g_ascii_strdown(str_of(roles, "brand") != NULL ? str_of(roles, "brand") : "", -1);
	if (named != NULL) {
		g_autoptr(GList) keys = json_object_get_members(roles);
		GList *key;
		for (key = keys; key != NULL; key = key->next) {
			g_autofree gchar *value = g_ascii_strdown(str_of(roles, key->data), -1);
			if (g_str_equal(value, brand))
				json_object_set_string_member(roles, key->data, named);
		}
		json_array_add_string_element(applied, word);
	}
	{
		/* Text moves for text roles; a coloured ground moves under fixed text. */
		const gchar *moves[][3] = {{"fg", "bg", "text"}, {"surface_fg", "surface", "text"},
			{"primary", "primary_fg", "ground"}, {"brand", "brand_fg", "ground"}, {"inverse", "inverse_fg", "ground"},
			{"photo_primary", "photo_primary_fg", "ground"}};
		guint m;
		for (m = 0; m < G_N_ELEMENTS(moves); m++) {
			g_autofree gchar *fixed = ls_design_repair(str_of(roles, moves[m][0]), str_of(roles, moves[m][1]), 4.5);
			json_object_set_string_member(roles, moves[m][0], fixed);
		}
	}
	{
		g_autofree gchar *muted = ls_design_repair(str_of(roles, "muted_fg"), str_of(roles, "bg"), 4.5);
		g_autofree gchar *both = ls_design_repair(muted, str_of(roles, "surface"), 4.5);
		g_autofree gchar *text = ls_design_repair(str_of(roles, "primary"), str_of(roles, "bg"), 4.5);
		g_autofree gchar *text_both = ls_design_repair(text, str_of(roles, "surface"), 4.5);
		g_autofree gchar *focus = ls_design_repair(str_of(roles, "primary"), str_of(roles, "bg"), 3);
		g_autofree gchar *brand_muted = mix(str_of(roles, "brand_fg"), str_of(roles, "brand"), 0.22);
		g_autofree gchar *brand_muted_ok = ls_design_repair(brand_muted, str_of(roles, "brand"), 4.5);
		g_autofree gchar *brand_line = mix(str_of(roles, "brand_fg"), str_of(roles, "brand"), 0.72);
		g_autofree gchar *inverse_muted = mix(str_of(roles, "inverse_fg"), str_of(roles, "inverse"), 0.22);
		g_autofree gchar *inverse_muted_ok = ls_design_repair(inverse_muted, str_of(roles, "inverse"), 4.5);
		g_autofree gchar *inverse_line = mix(str_of(roles, "inverse_fg"), str_of(roles, "inverse"), 0.75);
		json_object_set_string_member(roles, "muted_fg", both);
		json_object_set_string_member(roles, "primary_text", text_both);
		json_object_set_string_member(roles, "focus", focus);
		json_object_set_string_member(roles, "brand_muted", brand_muted_ok);
		json_object_set_string_member(roles, "brand_line", brand_line);
		json_object_set_string_member(roles, "inverse_muted", inverse_muted_ok);
		json_object_set_string_member(roles, "inverse_line", inverse_line);
	}
	if (str_of(roles, "highlight") == NULL)
		json_object_set_string_member(roles, "highlight", str_of(roles, "primary"));
	/* The accent phrase on the brand colour: the highlight, lightened or
	 * deepened until it reads there. */
	{
		g_autofree gchar *accent = ls_design_repair(str_of(roles, "highlight"), str_of(roles, "brand"), 4.5);
		json_object_set_string_member(roles, "brand_accent", accent);
	}
	return roles;
}

static const gchar *
layout_pick(JsonObject *library, JsonObject *design, const gchar *type, Content *content, const gchar *name){
	g_autoptr(GArray) choices = choices_for(library, design, type, content);
	return weighted(choices, name, type);
}

JsonObject *
ls_design_new(JsonObject *library, JsonObject *themes, JsonObject *site, const gchar *preset,
	JsonArray *photos, GError **error){
	JsonObject *theme = preset != NULL ? object_of(themes, preset) : NULL;
	JsonObject *preset_design = object_of(theme, "design");
	JsonObject *design, *sector;
	JsonArray *home, *types, *applied;
	Content content = content_of(site, photos);
	const gchar *sector_name = sector_of(library, site), *action, *name = business_name(site);
	guint i;
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);
	if (preset_design == NULL) {
		g_set_error(error, LS_ERROR, LS_ERROR_INVALID, "'%s' is not a design preset", preset != NULL ? preset : "");
		return NULL;
	}
	sector = object_of(object_of(library, "sectors"), sector_name);
	design = json_object_new();
	json_object_set_string_member(design, "preset", preset);
	json_object_set_string_member(design, "source", str_of(preset_design, "source"));
	json_object_set_string_member(design, "sector", sector_name);
	/* One primary action that fits the trade, from what the order supplies. */
	action = str_of(sector, "action") != NULL ? str_of(sector, "action") : "quote";
	if (g_str_equal(action, "book") && !content.booking)
		action = "call";
	if (g_str_equal(action, "call") && !content.phone)
		action = "quote";
	content.action = action;
	json_object_set_string_member(design, "action", action);
	json_object_set_string_member(design, "header", layout_pick(library, preset_design, "header", &content, name));
	json_object_set_string_member(design, "footer", layout_pick(library, preset_design, "footer", &content, name));
	home = json_array_new();
	types = array_of(sector, "home");
	for (i = 0; types != NULL && i < json_array_get_length(types); i++) {
		const gchar *type = json_array_get_string_element(types, i);
		const gchar *variant = layout_pick(library, preset_design, type, &content, name);
		if (variant == NULL && g_str_equal(type, "hero")) {
			/* Without a photo the home opens on the plain opening. */
			const gchar *fallback = str_of(library, "fallback_opening");
			const gchar *slash = fallback != NULL ? strchr(fallback, '/') : NULL;
			if (slash != NULL)
				add_section(home, "hero", slash + 1);
			continue;
		}
		if (variant != NULL)
			add_section(home, type, variant);
	}
	separate_families(library, preset_design, home, &content);
	assign_tones(library, home, str_of(design, "footer"));
	json_object_set_array_member(design, "home", home);
	applied = json_array_new();
	json_object_set_object_member(design, "roles", build_roles(library, theme, site, applied));
	json_object_set_array_member(design, "colors", applied);
	json_object_set_string_member(design, "kicker", str_of(preset_design, "kicker") != NULL
		? str_of(preset_design, "kicker") : "caps");
	json_object_set_string_member(design, "accent", str_of(preset_design, "accent") != NULL
		? str_of(preset_design, "accent") : "colour");
	if (str_of(preset_design, "font_accent") != NULL)
		json_object_set_string_member(design, "font_accent", str_of(preset_design, "font_accent"));
	{
		JsonObject *shape = json_object_new(), *given = object_of(preset_design, "shape");
		const gchar *keys[] = {"control", "box", "badge", "depth"};
		guint k;
		for (k = 0; k < G_N_ELEMENTS(keys); k++)
			json_object_set_int_member(shape, keys[k], given != NULL && json_object_has_member(given, keys[k])
				? json_object_get_int_member(given, keys[k]) : 0);
		json_object_set_double_member(shape, "scrim", given != NULL && json_object_has_member(given, "scrim")
			? json_object_get_double_member(given, "scrim") : 0.55);
		json_object_set_object_member(design, "shape", shape);
	}
	return design;
}

/* ---- Checks --------------------------------------------------------------- */

gboolean
ls_design_check_home(JsonObject *library, JsonArray *home, const gchar *footer, GString *why){
	g_autofree gchar *footer_pair = g_strdup_printf("footer/%s", footer != NULL ? footer : "");
	const gchar *footer_tone = str_of(object_of(object_of(library, "variants"), footer_pair), "tone");
	guint i, n = home != NULL ? json_array_get_length(home) : 0;
	gboolean ok = TRUE;
	for (i = 0; i < n; i++) {
		JsonObject *here = json_array_get_object_element(home, i);
		const gchar *tone = str_of(here, "tone");
		const gchar *next = i + 1 < n ? str_of(json_array_get_object_element(home, i + 1), "tone") : footer_tone;
		guint brands = 0, j;
		if ((g_strcmp0(tone, "surface") == 0 || g_strcmp0(tone, "inverse") == 0) && g_strcmp0(tone, next) == 0) {
			ok = FALSE;
			if (why != NULL)
				g_string_append_printf(why, "sections %u and %u share the %s tone\n", i + 1, i + 2, tone);
		}
		for (j = i; j < MIN(n, i + 3); j++)
			brands += g_strcmp0(str_of(json_array_get_object_element(home, j), "tone"), "brand") == 0 ? 1 : 0;
		if (brands > 1) {
			ok = FALSE;
			if (why != NULL)
				g_string_append_printf(why, "more than one brand tone in sections %u to %u\n", i + 1, MIN(n, i + 3));
		}
		if (i + 1 < n) {
			JsonObject *after = json_array_get_object_element(home, i + 1);
			if (g_strcmp0(family_of(library, str_of(here, "component"), str_of(here, "variant")),
				family_of(library, str_of(after, "component"), str_of(after, "variant"))) == 0) {
				ok = FALSE;
				if (why != NULL)
					g_string_append_printf(why, "sections %u and %u are both of the %s layout family\n", i + 1, i + 2,
						family_of(library, str_of(here, "component"), str_of(here, "variant")));
			}
		}
	}
	return ok;
}

static gboolean
hex_valid(const gchar *text){
	gdouble rgb[3];
	return parse_hex(text, rgb);
}

gboolean
ls_design_check(JsonObject *library, JsonObject *design, GString *why){
	JsonObject *roles = object_of(design, "roles"), *shape = object_of(design, "shape");
	JsonArray *home = array_of(design, "home");
	const gchar *tones[] = {"default", "surface", "inverse", "brand", "photo"};
	gsize start = why->len;
	guint i;
	if (design == NULL || !ls_object_keys(design,
		"|preset|source|sector|action|header|footer|home|roles|colors|kicker|accent|font_accent|shape|", NULL)) {
		g_string_append(why, "design may only have preset, source, sector, action, header, footer, home, roles, "
			"colors, kicker, accent, font_accent and shape\n");
		return FALSE;
	}
	if (str_of(design, "preset") == NULL)
		g_string_append(why, "design.preset must name a preset\n");
	if (str_of(design, "action") == NULL || (!g_str_equal(str_of(design, "action"), "call") &&
		!g_str_equal(str_of(design, "action"), "book") && !g_str_equal(str_of(design, "action"), "quote")))
		g_string_append(why, "design.action must be call, book or quote\n");
	{
		const gchar *layouts[] = {"header", "footer"};
		guint l;
		for (l = 0; l < G_N_ELEMENTS(layouts); l++) {
			g_autofree gchar *pair = g_strdup_printf("%s/%s", layouts[l], str_of(design, layouts[l]) != NULL
				? str_of(design, layouts[l]) : "");
			if (object_of(object_of(library, "variants"), pair) == NULL)
				g_string_append_printf(why, "design.%s must be a library %s layout\n", layouts[l], layouts[l]);
		}
	}
	if (str_of(design, "kicker") == NULL || (!g_str_equal(str_of(design, "kicker"), "caps") &&
		!g_str_equal(str_of(design, "kicker"), "pill")))
		g_string_append(why, "design.kicker must be caps or pill\n");
	if (str_of(design, "accent") == NULL || (!g_str_equal(str_of(design, "accent"), "colour") &&
		!g_str_equal(str_of(design, "accent"), "italic")))
		g_string_append(why, "design.accent must be colour or italic\n");
	if (str_of(design, "font_accent") != NULL && (strpbrk(str_of(design, "font_accent"), "{};<>\\") != NULL ||
		strlen(str_of(design, "font_accent")) > 120))
		g_string_append(why, "design.font_accent must be a plain font stack\n");
	if (home == NULL || json_array_get_length(home) == 0 || json_array_get_length(home) > 12)
		g_string_append(why, "design.home must list 1 to 12 sections\n");
	for (i = 0; home != NULL && i < json_array_get_length(home); i++) {
		JsonNode *node = json_array_get_element(home, i);
		JsonObject *section = JSON_NODE_HOLDS_OBJECT(node) ? json_node_get_object(node) : NULL;
		g_autofree gchar *pair = section != NULL ? g_strdup_printf("%s/%s", str_of(section, "component"),
			str_of(section, "variant")) : NULL;
		gboolean known_tone = FALSE;
		guint t;
		for (t = 0; section != NULL && t < G_N_ELEMENTS(tones); t++)
			known_tone = known_tone || g_strcmp0(str_of(section, "tone"), tones[t]) == 0;
		if (section == NULL || !ls_object_keys(section, "|component|variant|tone|", NULL) || !known_tone ||
			(object_of(object_of(library, "variants"), pair) == NULL &&
			g_strcmp0(pair, str_of(library, "fallback_opening")) != 0))
			g_string_append_printf(why, "design.home[%u] must be {component, variant, tone} of the library\n", i);
	}
	for (i = 0; i < G_N_ELEMENTS(role_names); i++)
		if (!hex_valid(str_of(roles, role_names[i])))
			g_string_append_printf(why, "design.roles.%s must be #rrggbb\n", role_names[i]);
	if (why->len == start)
		for (i = 0; i < G_N_ELEMENTS(text_pairs); i++)
			if (ls_design_contrast(str_of(roles, text_pairs[i][0]), str_of(roles, text_pairs[i][1])) < 4.5)
				g_string_append_printf(why, "design.roles.%s on %s is under 4.5:1\n", text_pairs[i][1], text_pairs[i][0]);
	if (shape == NULL || !json_object_has_member(shape, "control") || !json_object_has_member(shape, "box") ||
		!json_object_has_member(shape, "badge") || !json_object_has_member(shape, "depth") ||
		!json_object_has_member(shape, "scrim") || json_object_get_int_member(shape, "control") < 0 ||
		json_object_get_int_member(shape, "control") > 999 || json_object_get_int_member(shape, "box") < 0 ||
		json_object_get_int_member(shape, "box") > 32 || json_object_get_int_member(shape, "badge") < 0 ||
		json_object_get_int_member(shape, "badge") > 999 || json_object_get_int_member(shape, "depth") < 0 ||
		json_object_get_int_member(shape, "depth") > 1 || json_object_get_double_member(shape, "scrim") < 0.2 ||
		json_object_get_double_member(shape, "scrim") > 0.8)
		g_string_append(why, "design.shape needs control 0 to 999, box 0 to 32, badge 0 to 999, depth 0 or 1 and "
			"scrim 0.2 to 0.8\n");
	if (why->len == start && home != NULL) {
		g_autoptr(GString) clash = g_string_new(NULL);
		if (!ls_design_check_home(library, home, str_of(design, "footer"), clash))
			g_string_append(why, clash->str);
	}
	return why->len == start;
}

/* ---- CSS ------------------------------------------------------------------ */

static void
add_px(GString *out, const gchar *name, gint64 value){
	g_string_append_printf(out, "\t--%s: %" G_GINT64_FORMAT "px;\n", name, value);
}

/* A tint for pressed and hover states that keeps @text readable on it. */
static gchar *
hover_of(const gchar *ground, const gchar *text){
	gboolean dark = lightness_of(ground) < 0.12;
	g_autofree gchar *moved = mix(ground, dark ? "#ffffff" : "#000000", dark ? 0.14 : 0.12);
	if (ls_design_contrast(moved, text) >= 4.5)
		return g_steal_pointer(&moved);
	g_free(moved);
	moved = mix(ground, dark ? "#000000" : "#ffffff", 0.1);
	return ls_design_contrast(moved, text) >= 4.5 ? g_steal_pointer(&moved) : g_strdup(ground);
}

gchar *
ls_design_css(JsonObject *design){
	JsonObject *roles = object_of(design, "roles"), *shape = object_of(design, "shape");
	GString *out = g_string_new(":root {\n");
	const gchar *steps[] = {"0.75rem", "0.875rem", "1rem", "1.125rem", "1.25rem", "1.5rem",
		"clamp(1.5rem, 1.3rem + 0.9cqi, 1.75rem)", "clamp(1.75rem, 1.35rem + 1.8cqi, 2.1875rem)",
		"clamp(2.25rem, 1.45rem + 3.6cqi, 3.75rem)"};
	const gchar *space[] = {"0.25rem", "0.5rem", "0.75rem", "1rem", "1.5rem", "2rem", "2.5rem", "3rem", "4rem"};
	gchar scrim[G_ASCII_DTOSTR_BUF_SIZE];
	guint i;
	for (i = 0; i < G_N_ELEMENTS(role_names); i++) {
		g_autofree gchar *name = g_strdelimit(g_strdup(role_names[i]), "_", '-');
		g_string_append_printf(out, "\t--%s: %s;\n", name, str_of(roles, role_names[i]));
	}
	{
		g_autofree gchar *primary_hover = hover_of(str_of(roles, "primary"), str_of(roles, "primary_fg"));
		g_autofree gchar *brand_hover = hover_of(str_of(roles, "brand"), str_of(roles, "brand_fg"));
		g_autofree gchar *photo_hover = hover_of(str_of(roles, "photo_primary"), str_of(roles, "photo_primary_fg"));
		g_autofree gchar *primary_soft = mix(str_of(roles, "primary"), str_of(roles, "bg"), 0.9);
		g_autofree gchar *brand_soft = mix(str_of(roles, "brand"), str_of(roles, "bg"), 0.9);
		g_autofree gchar *line_strong = mix(str_of(roles, "line"), str_of(roles, "fg"), 0.35);
		g_string_append_printf(out, "\t--primary-hover: %s;\n\t--brand-hover: %s;\n\t--photo-primary-hover: %s;\n"
			"\t--primary-soft: %s;\n\t--brand-soft: %s;\n\t--line-firm: %s;\n", primary_hover, brand_hover, photo_hover,
			primary_soft, brand_soft, line_strong);
	}
	g_string_append_printf(out, "\t--font-accent: %s;\n", str_of(design, "font_accent") != NULL
		? str_of(design, "font_accent") : "var(--font-display)");
	for (i = 0; i < G_N_ELEMENTS(steps); i++)
		g_string_append_printf(out, "\t--text-%u: %s;\n", i + 1, steps[i]);
	for (i = 0; i < G_N_ELEMENTS(space); i++)
		g_string_append_printf(out, "\t--space-%u: %s;\n", i + 1, space[i]);
	g_string_append(out, "\t--container: 72rem;\n\t--container-narrow: 44rem;\n"
		"\t--section-y: clamp(3.5rem, 7cqi, 6rem);\n");
	add_px(out, "radius-control", shape != NULL ? json_object_get_int_member(shape, "control") : 4);
	add_px(out, "radius-box", shape != NULL ? json_object_get_int_member(shape, "box") : 8);
	add_px(out, "radius-badge", shape != NULL ? json_object_get_int_member(shape, "badge") : 999);
	if (shape != NULL && json_object_get_int_member(shape, "depth") > 0)
		g_string_append(out, "\t--shadow-1: 0 1px 2px rgb(16 24 40 / 0.06);\n"
			"\t--shadow-2: 0 4px 16px rgb(16 24 40 / 0.08);\n\t--shadow-3: 0 12px 32px rgb(16 24 40 / 0.12);\n");
	else
		g_string_append(out, "\t--shadow-1: none;\n\t--shadow-2: none;\n\t--shadow-3: none;\n");
	g_string_append_printf(out, "\t--depth: %" G_GINT64_FORMAT ";\n", shape != NULL
		? json_object_get_int_member(shape, "depth") : 0);
	g_string_append_printf(out, "\t--scrim: %s;\n", g_ascii_formatd(scrim, sizeof scrim, "%.2f",
		shape != NULL ? json_object_get_double_member(shape, "scrim") : 0.55));
	g_string_append(out, "}\n");
	return g_string_free(out, FALSE);
}
