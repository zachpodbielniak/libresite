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

/* ---- Supplied facts ------------------------------------------------------- */

/* Labels of the sentences the order form writes into site.requirements,
 * and the openings of its fixed instruction sentences: a labelled value
 * runs to the next of either. */
static const gchar *const fact_labels[] = {"Hours:", "Service area:", "Colours:", "Colors:", "Style:",
	"process, in order:", NULL};
static const gchar *const sentence_openings[] = {". Use ", ". Show ", ". Apply ", ". Do not ", ". If ", ". Make ",
	". The owner", NULL};

/* The text after @label in @text up to the next label, instruction
 * sentence or doubled full stop, trimmed; NULL when @label is absent. */
static gchar *
labelled_value(const gchar *text, const gchar *label){
	const gchar *start = text != NULL ? strstr(text, label) : NULL;
	const gchar *end;
	gchar *value;
	guint i;
	if (start == NULL)
		return NULL;
	start += strlen(label);
	end = start + strlen(start);
	for (i = 0; fact_labels[i] != NULL; i++) {
		const gchar *next = strstr(start, fact_labels[i]);
		if (next != NULL && next < end)
			end = next;
	}
	for (i = 0; sentence_openings[i] != NULL; i++) {
		const gchar *next = strstr(start, sentence_openings[i]);
		if (next != NULL && next + 1 < end)
			end = next + 1;
	}
	{
		const gchar *doubled = strstr(start, "..");
		if (doubled != NULL && doubled + 1 < end)
			end = doubled + 1;
	}
	value = g_strstrip(g_strndup(start, (gsize)(end - start)));
	if (value[0] == '\0') {
		g_free(value);
		return NULL;
	}
	return value;
}

/* The towns of "A, B, C and D", in order. */
static JsonArray *
split_places(const gchar *text){
	JsonArray *places = json_array_new();
	g_autofree gchar *list = g_strdup(text);
	g_auto(GStrv) parts = NULL;
	guint i;
	g_strdelimit(list, ";", ',');
	{
		gsize length = strlen(list);
		while (length > 0 && list[length - 1] == '.')
			list[--length] = '\0';
	}
	{
		g_autoptr(GRegex) joiner = g_regex_new("\\s+and\\s+", 0, 0, NULL);
		g_autofree gchar *commas = g_regex_replace_literal(joiner, list, -1, 0, ",", 0, NULL);
		parts = g_strsplit(commas, ",", -1);
	}
	for (i = 0; parts[i] != NULL; i++) {
		gchar *place = g_strstrip(parts[i]);
		if (place[0] != '\0' && strlen(place) <= 60)
			json_array_add_string_element(places, place);
	}
	return places;
}

/* The numbered steps of "1. A. 2. B. 3. C.", word for word. */
static JsonArray *
split_steps(const gchar *text){
	JsonArray *steps = json_array_new();
	g_autoptr(GRegex) number = g_regex_new("(?:^|\\s)\\d{1,2}\\.\\s+", 0, 0, NULL);
	g_auto(GStrv) parts = g_regex_split(number, text, 0);
	guint i;
	for (i = 0; parts[i] != NULL; i++) {
		gchar *step = g_strstrip(parts[i]);
		if (step[0] != '\0' && strlen(step) <= 160)
			json_array_add_string_element(steps, step);
	}
	return steps;
}

JsonObject *
ls_design_facts(JsonObject *site){
	JsonObject *facts = json_object_new();
	JsonObject *brand = object_of(site, "brand"), *contact = object_of(site, "contact");
	const gchar *requirements = str_of(site, "requirements");
	const gchar *sources[3];
	guint i;
	sources[0] = str_of(brand, "tagline");
	sources[1] = str_of(site, "brief");
	sources[2] = requirements;
	/* The year after "since", from the tagline first. */
	{
		g_autoptr(GRegex) since = g_regex_new("\\b[Ss]ince\\s+((?:18|19|20)\\d\\d)\\b", 0, 0, NULL);
		for (i = 0; i < G_N_ELEMENTS(sources) && !json_object_has_member(facts, "since"); i++) {
			g_autoptr(GMatchInfo) match = NULL;
			if (sources[i] != NULL && g_regex_match(since, sources[i], 0, &match)) {
				g_autofree gchar *year = g_match_info_fetch(match, 1);
				json_object_set_string_member(facts, "since", year);
			}
		}
	}
	if (str_of(contact, "address") != NULL)
		json_object_set_string_member(facts, "address", str_of(contact, "address"));
	{
		g_autofree gchar *hours = labelled_value(requirements, "Hours:");
		g_autofree gchar *area = labelled_value(requirements, "Service area:");
		g_autofree gchar *process = labelled_value(requirements, "process, in order:");
		if (hours != NULL)
			json_object_set_string_member(facts, "hours", hours);
		if (area != NULL) {
			JsonArray *places = split_places(area);
			if (json_array_get_length(places) > 0)
				json_object_set_array_member(facts, "area", places);
			else
				json_array_unref(places);
		}
		if (process != NULL) {
			JsonArray *steps = split_steps(process);
			if (json_array_get_length(steps) >= 2)
				json_object_set_array_member(facts, "process", steps);
			else
				json_array_unref(steps);
		}
	}
	/* Press and certification names, the rating and the shops are copied
	 * as supplied; the recipe has already checked their shape. */
	{
		const gchar *copied[] = {"press", "rating", "locations"};
		for (i = 0; i < G_N_ELEMENTS(copied); i++) {
			JsonNode *node = site != NULL ? json_object_get_member(site, copied[i]) : NULL;
			if (node != NULL && (JSON_NODE_HOLDS_ARRAY(node) || JSON_NODE_HOLDS_OBJECT(node)))
				json_object_set_member(facts, copied[i], json_node_copy(node));
		}
	}
	return facts;
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

/* Whether a nearby site in site.avoid wears @value as its @key, or, for
 * the opening, as the first entry of its home. */
static gboolean
avoided(JsonObject *site, const gchar *key, const gchar *value){
	JsonArray *avoid = array_of(site, "avoid");
	guint a;
	for (a = 0; avoid != NULL && a < json_array_get_length(avoid); a++) {
		JsonObject *near = JSON_NODE_HOLDS_OBJECT(json_array_get_element(avoid, a))
			? json_array_get_object_element(avoid, a) : NULL;
		JsonArray *home = array_of(near, "home");
		if (g_str_equal(key, "opening")) {
			if (home != NULL && json_array_get_length(home) > 0 &&
				JSON_NODE_HOLDS_VALUE(json_array_get_element(home, 0)) &&
				g_strcmp0(json_array_get_string_element(home, 0), value) == 0)
				return TRUE;
		} else if (g_strcmp0(str_of(near, key), value) == 0)
			return TRUE;
	}
	return FALSE;
}

/* The choices no nearby site wears, or all of them when every one is worn. */
static GArray *
unworn(GArray *choices, JsonObject *site, const gchar *key, const gchar *prefix){
	GArray *fresh = g_array_new(FALSE, FALSE, sizeof(Choice));
	guint i;
	for (i = 0; i < choices->len; i++) {
		Choice *choice = &g_array_index(choices, Choice, i);
		g_autofree gchar *value = g_strconcat(prefix, choice->name, NULL);
		if (!avoided(site, key, value))
			g_array_append_val(fresh, *choice);
	}
	if (fresh->len == 0)
		g_array_append_vals(fresh, choices->data, choices->len);
	return fresh;
}

gchar *
ls_design_pick_preset(JsonObject *library, JsonObject *themes, JsonObject *site){
	g_autoptr(GList) names = themes != NULL ? json_object_get_members(themes) : NULL;
	g_autoptr(GPtrArray) words = ls_design_owner_words(site);
	g_autoptr(GArray) matched = g_array_new(FALSE, FALSE, sizeof(Choice));
	g_autoptr(GArray) all = g_array_new(FALSE, FALSE, sizeof(Choice));
	g_autoptr(GArray) fresh = NULL;
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
		/* Neutral colour words (white, cream, light ...) name no preset. */
		for (i = 0; list != NULL && i < json_array_get_length(list); i++)
			score += has_word(words, json_array_get_string_element(list, i)) &&
				!list_has(array_of(library, "neutral"), json_array_get_string_element(list, i)) ? 1 : 0;
		choice.name = name->data;
		choice.weight = list_has(array_of(design, "fits"), sector) ? weight(library, "fit") : weight(library, "other");
		g_array_append_val(all, choice);
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
	/* The owner's words win; among presets they fit equally, and otherwise
	 * among all by the hash, the presets nearby sites (site.avoid) wear
	 * leave the pick unless every one is worn. */
	fresh = unworn(matched->len > 0 ? matched : all, site, "preset", "");
	return g_strdup(weighted(fresh, business_name(site), "preset"));
}

typedef struct {
	guint photos;	/* supplied photos */
	guint items;	/* supplied offers */
	guint quotes;	/* supplied reviews */
	gboolean phone;
	gboolean booking;
	guint facts;	/* supplied trust facts: year, towns, hours, address */
	guint steps;	/* the owner's process steps */
	guint towns;	/* towns served */
	gboolean history;	/* a supplied year the business started */
	gboolean place;	/* supplied hours or an address */
	guint own_photos;	/* supplied photos that are not stock */
	guint press;	/* supplied press and certification names */
	gboolean rating;	/* a supplied rating */
	guint locations;	/* supplied shops */
	guint free;	/* supplied photos no earlier home section shows */
	gboolean form;	/* a supplied live form (site.form) */
	gboolean gallery;	/* the home shows the owner's own work, once chosen */
	const gchar *sector;	/* the business's sector */
	const gchar *opening;	/* the opening's layout family, once chosen */
	const gchar *action;	/* the design's primary action, once chosen */
} Content;

static Content
content_of(JsonObject *site, JsonArray *photos, JsonObject *facts){
	Content content;
	JsonObject *contact = object_of(site, "contact");
	JsonObject *booking = object_of(site, "booking");
	guint i;
	content.photos = photos != NULL ? json_array_get_length(photos) : 0;
	content.items = array_of(site, "offers") != NULL ? json_array_get_length(array_of(site, "offers")) : 0;
	content.quotes = array_of(site, "reviews") != NULL ? json_array_get_length(array_of(site, "reviews")) : 0;
	content.phone = str_of(contact, "phone") != NULL;
	content.booking = booking != NULL && str_of(booking, "href") != NULL;
	content.towns = array_of(facts, "area") != NULL ? json_array_get_length(array_of(facts, "area")) : 0;
	content.steps = array_of(facts, "process") != NULL ? json_array_get_length(array_of(facts, "process")) : 0;
	content.history = str_of(facts, "since") != NULL;
	content.place = str_of(facts, "hours") != NULL || str_of(facts, "address") != NULL;
	content.facts = (content.history ? 1 : 0) + (content.towns > 0 ? 1 : 0) + (str_of(facts, "hours") != NULL ? 1 : 0) +
		(str_of(facts, "address") != NULL ? 1 : 0);
	content.own_photos = 0;
	for (i = 0; photos != NULL && i < json_array_get_length(photos); i++) {
		JsonObject *photo = JSON_NODE_HOLDS_OBJECT(json_array_get_element(photos, i))
			? json_array_get_object_element(photos, i) : NULL;
		JsonNode *flag = photo != NULL ? json_object_get_member(photo, "stock") : NULL;
		if (photo != NULL && !(flag != NULL && JSON_NODE_HOLDS_VALUE(flag) && json_node_get_boolean(flag)))
			content.own_photos++;
	}
	content.press = array_of(facts, "press") != NULL ? json_array_get_length(array_of(facts, "press")) : 0;
	content.rating = object_of(facts, "rating") != NULL;
	content.locations = array_of(facts, "locations") != NULL ? json_array_get_length(array_of(facts, "locations")) : 0;
	content.place = content.place || content.locations > 0;
	content.free = content.photos;
	content.form = object_of(site, "form") != NULL;
	content.gallery = FALSE;
	content.sector = NULL;
	content.opening = NULL;
	content.action = NULL;
	return content;
}

/* Whether @needs asks for at least @key of something @have does not reach. */
static gboolean
short_of(JsonObject *needs, const gchar *key, guint have){
	return json_object_has_member(needs, key) && have < (guint)json_object_get_int_member(needs, key);
}

static gboolean
wants(JsonObject *needs, const gchar *key, gboolean have){
	return json_object_has_member(needs, key) && json_object_get_boolean_member(needs, key) && !have;
}

static gboolean
in_range(JsonObject *needs, const gchar *key, guint value){
	JsonArray *range = array_of(needs, key);
	if (range == NULL || json_array_get_length(range) != 2)
		return TRUE;
	return value >= (guint)json_array_get_int_element(range, 0) && value <= (guint)json_array_get_int_element(range, 1);
}

/* Whether content meets @needs: photos, items, quotes, a phone, facts. */
static gboolean
needs_met(JsonObject *needs, Content *content){
	if (needs == NULL)
		return TRUE;
	if (json_object_has_member(needs, "photos") &&
		content->photos < (guint)json_object_get_int_member(needs, "photos"))
		return FALSE;
	if (json_object_has_member(needs, "phone") && json_object_get_boolean_member(needs, "phone") && !content->phone)
		return FALSE;
	/* Sections built from the order's facts show only with those facts. */
	if (short_of(needs, "facts", content->facts) || short_of(needs, "steps", content->steps) ||
		short_of(needs, "towns", content->towns) || short_of(needs, "own_photos", content->own_photos) ||
		wants(needs, "history", content->history) || wants(needs, "place", content->place))
		return FALSE;
	/* Supplied press names, a rating and two or more shops. */
	if (short_of(needs, "press", content->press) || wants(needs, "rating", content->rating) ||
		short_of(needs, "locations", content->locations) || short_of(needs, "spare_photos", content->free))
		return FALSE;
	/* The header lies over the opening only when the opening is a
	 * full-bleed photo. */
	if (wants(needs, "over_photo", g_strcmp0(content->opening, "bleed") == 0))
		return FALSE;
	/* A layout built around one action shows only where that action leads. */
	if (json_object_has_member(needs, "action") && g_strcmp0(str_of(needs, "action"), content->action) != 0)
		return FALSE;
	/* One photo for every card, after the opening's. */
	if (json_object_has_member(needs, "item_photos") && json_object_get_boolean_member(needs, "item_photos") &&
		content->free < content->items)
		return FALSE;
	/* A supplied live form; the owner's own work on the home. */
	if (wants(needs, "form", content->form) || wants(needs, "gallery", content->gallery))
		return FALSE;
	/* A badge needs a fact to carry (the year or the rating) and an
	 * opening photo to sit on. */
	if (wants(needs, "badge", (content->history || content->rating) && content->opening != NULL &&
		g_strcmp0(content->opening, "stack") != 0 && g_strcmp0(content->opening, "plain") != 0))
		return FALSE;
	return in_range(needs, "items", content->items) && in_range(needs, "quotes", content->quotes) &&
		(!json_object_has_member(needs, "quotes") || content->quotes > 0);
}

/* Whether content lets @pair show. */
static gboolean
eligible(JsonObject *library, const gchar *pair, Content *content){
	JsonObject *entry = object_of(object_of(library, "variants"), pair);
	return entry != NULL && needs_met(object_of(entry, "needs"), content);
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
	/* The preset's preference, or the first eligible one of its list. The
	 * header and footer are weighted by trade alone. */
	if (g_strcmp0(type, "header") == 0 || g_strcmp0(type, "footer") == 0)
		wish = NULL;
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
		/* Variants that suit the trade weigh more; none is forbidden. */
		if (list_has(array_of(object_of(variants, name), "fits"), content->sector))
			choice.weight *= weight(library, "variant_fit");
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

static const gchar *
own_tone(JsonObject *library, JsonObject *section){
	g_autofree gchar *pair = g_strdup_printf("%s/%s", str_of(section, "component"), str_of(section, "variant"));
	/* Library strings outlive the pair. */
	return str_of(object_of(object_of(library, "variants"), pair), "tone");
}

/* At most one brand band in any three sections: a brand-toned section
 * close before another takes the heaviest eligible variant of another
 * tone; the closing call keeps its band. */
static void
space_brands(JsonObject *library, JsonObject *design, JsonArray *home, Content *content){
	guint i, j, n = json_array_get_length(home);
	for (i = 0; i < n; i++) {
		JsonObject *here = json_array_get_object_element(home, i);
		gboolean crowded = FALSE;
		if (g_strcmp0(own_tone(library, here), "brand") != 0 || g_strcmp0(str_of(here, "component"), "cta") == 0)
			continue;
		for (j = i + 1; j < MIN(n, i + 3); j++)
			crowded = crowded || g_strcmp0(own_tone(library, json_array_get_object_element(home, j)), "brand") == 0;
		for (j = i >= 2 ? i - 2 : 0; j < i; j++)
			crowded = crowded || g_strcmp0(own_tone(library, json_array_get_object_element(home, j)), "brand") == 0;
		if (crowded) {
			g_autoptr(GArray) choices = choices_for(library, design, str_of(here, "component"), content);
			const gchar *pick = NULL;
			gint64 heaviest = 0;
			for (j = 0; j < choices->len; j++) {
				Choice *choice = &g_array_index(choices, Choice, j);
				g_autofree gchar *pair = g_strdup_printf("%s/%s", str_of(here, "component"), choice->name);
				if (g_strcmp0(str_of(object_of(object_of(library, "variants"), pair), "tone"), "brand") != 0 &&
					choice->weight > heaviest) {
					heaviest = choice->weight;
					pick = choice->name;
				}
			}
			if (pick != NULL)
				json_object_set_string_member(here, "variant", pick);
		}
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
		g_autofree gchar *pair = g_strdup_printf("%s/%s", str_of(section, "component"), str_of(section, "variant"));
		const gchar *own = str_of(object_of(object_of(library, "variants"), pair), "tone");
		if (i == 0)
			tone = g_strcmp0(family_of(library, str_of(section, "component"), str_of(section, "variant")),
				"bleed") == 0 ? "photo" : "default";
		else if (own != NULL)
			tone = own;
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
build_roles(JsonObject *library, JsonObject *tokens, JsonObject *given, JsonObject *site, JsonArray *applied){
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

/* A flat lowercase route for @name: ASCII letters and digits joined by
 * hyphens, at most 48 characters. */
static gchar *
route_slug(const gchar *name){
	GString *out = g_string_new(NULL);
	const gchar *p;
	for (p = name != NULL ? name : ""; *p != '\0' && out->len < 48; p++) {
		if (g_ascii_isalnum(*p))
			g_string_append_c(out, g_ascii_tolower(*p));
		else if (*p == '&' && out->len > 0 && out->str[out->len - 1] == '-')
			g_string_append(out, "and-");
		else if (out->len > 0 && out->str[out->len - 1] != '-')
			g_string_append_c(out, '-');
	}
	while (out->len > 0 && out->str[out->len - 1] == '-')
		g_string_truncate(out, out->len - 1);
	return g_string_free(out, FALSE);
}

static void
add_page(JsonArray *pages, const gchar *path, const gchar *kind, const gchar *title, const gchar *offer){
	JsonObject *page = json_object_new();
	json_object_set_string_member(page, "path", path);
	json_object_set_string_member(page, "kind", kind);
	json_object_set_string_member(page, "title", title);
	if (offer != NULL)
		json_object_set_string_member(page, "offer", offer);
	json_array_add_object_element(pages, page);
}

#define MAX_SERVICE_PAGES 16

/* The pages of an order with offers: home, the services, one page for every supplied
 * service, About with a supplied year, Service area with supplied towns,
 * Questions, Booking with a supplied booking page, and contact. */
static JsonArray *
site_pages(JsonObject *site, Content *content){
	JsonArray *pages = json_array_new();
	JsonArray *offers = array_of(site, "offers");
	g_autoptr(GHashTable) taken = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	const gchar *fixed[] = {"index", "services", "about", "service-area", "questions", "book", "contact", "404",
		"privacy", "terms"};
	guint i, count = 0;
	for (i = 0; i < G_N_ELEMENTS(fixed); i++)
		g_hash_table_add(taken, g_strdup(fixed[i]));
	add_page(pages, "index.html", "home", "Home", NULL);
	add_page(pages, "services.html", "services", "Services", NULL);
	for (i = 0; offers != NULL && i < json_array_get_length(offers) && count < MAX_SERVICE_PAGES; i++) {
		JsonObject *offer = JSON_NODE_HOLDS_OBJECT(json_array_get_element(offers, i))
			? json_array_get_object_element(offers, i) : NULL;
		const gchar *name = str_of(offer, "name");
		g_autofree gchar *slug = route_slug(name);
		g_autofree gchar *path = NULL;
		guint n = 2;
		if (name == NULL)
			continue;
		if (slug[0] == '\0' || g_hash_table_contains(taken, slug)) {
			gchar *prefixed = g_strconcat("service-", slug[0] != '\0' ? slug : "page", NULL);
			g_free(slug);
			slug = prefixed;
		}
		while (g_hash_table_contains(taken, slug)) {
			gchar *numbered = g_strdup_printf("%.40s-%u", slug, n++);
			if (!g_hash_table_contains(taken, numbered)) {
				g_free(slug);
				slug = numbered;
			} else
				g_free(numbered);
		}
		g_hash_table_add(taken, g_strdup(slug));
		path = g_strconcat(slug, ".html", NULL);
		add_page(pages, path, "detail", name, str_of(offer, "id"));
		/* Whether the owner described it: a bare name needs general words. */
		if (str_of(offer, "description") != NULL && strspn(str_of(offer, "description"), " \t\n") <
			strlen(str_of(offer, "description")))
			json_object_set_boolean_member(json_array_get_object_element(pages, json_array_get_length(pages) - 1),
				"described", TRUE);
		count++;
	}
	if (content->history)
		add_page(pages, "about.html", "about", "About", NULL);
	if (content->towns > 0)
		add_page(pages, "service-area.html", "visit", "Service area", NULL);
	/* Questions about the services, answered from supplied facts and
	 * general truths; and the supplied booking calendar on a page of its own. */
	add_page(pages, "questions.html", "general", "Questions", NULL);
	if (content->booking)
		add_page(pages, "book.html", "form", "Booking", NULL);
	add_page(pages, "contact.html", "contact", "Contact", NULL);
	return pages;
}

/* How many of @entry's words the owner used; neutral words never count. */
static guint
word_score(JsonObject *library, JsonObject *entry, GPtrArray *words){
	JsonArray *list = array_of(entry, "words");
	guint i, score = 0;
	for (i = 0; list != NULL && i < json_array_get_length(list); i++)
		score += has_word(words, json_array_get_string_element(list, i)) &&
			!list_has(array_of(library, "neutral"), json_array_get_string_element(list, i)) ? 1 : 0;
	return score;
}

/* The choices whose @entries the owner's words name most, or NULL when
 * the words name none. */
static GArray *
by_words(JsonObject *library, JsonObject *entries, const gchar *prefix, GArray *choices, GPtrArray *words){
	GArray *matched = g_array_new(FALSE, FALSE, sizeof(Choice));
	guint i, best = 0;
	for (i = 0; i < choices->len; i++) {
		Choice *choice = &g_array_index(choices, Choice, i);
		g_autofree gchar *key = g_strconcat(prefix, choice->name, NULL);
		guint score = word_score(library, object_of(entries, key), words);
		if (score == 0 || score < best)
			continue;
		if (score > best)
			g_array_set_size(matched, 0);
		best = score;
		g_array_append_val(matched, *choice);
	}
	if (matched->len == 0) {
		g_array_unref(matched);
		return NULL;
	}
	return matched;
}

/* The choices no nearby site wears as its @key, or NULL when all are worn. */
static GArray *
unworn_only(GArray *choices, JsonObject *site, const gchar *key, const gchar *prefix){
	GArray *fresh = g_array_new(FALSE, FALSE, sizeof(Choice));
	guint i;
	for (i = 0; i < choices->len; i++) {
		Choice *choice = &g_array_index(choices, Choice, i);
		g_autofree gchar *value = g_strconcat(prefix, choice->name, NULL);
		if (!avoided(site, key, value))
			g_array_append_val(fresh, *choice);
	}
	if (fresh->len == 0) {
		g_array_unref(fresh);
		return NULL;
	}
	return fresh;
}

/* One of @choices for @salt: the owner's words first; then, for a layout
 * a nearby site wears (@key in site.avoid), another one, even when the
 * owner's words named it, so two businesses of one trade in one town
 * never share it while another fits; then the stable hash. */
static const gchar *
pick_from(JsonObject *library, JsonObject *entries, const gchar *prefix, GArray *choices, JsonObject *site,
	const gchar *salt, const gchar *key, const gchar *avoid_prefix){
	g_autoptr(GPtrArray) words = ls_design_owner_words(site);
	g_autoptr(GArray) matched = by_words(library, entries, prefix, choices, words);
	g_autoptr(GArray) fresh = NULL;
	GArray *pool = matched != NULL ? matched : choices;
	if (key != NULL) {
		fresh = unworn_only(pool, site, key, avoid_prefix);
		if (fresh == NULL && pool != choices)
			fresh = unworn_only(choices, site, key, avoid_prefix);
		if (fresh != NULL)
			pool = fresh;
	}
	return weighted(pool, business_name(site), salt);
}

/* The variant the hash picks for @type; the owner's words first, and an
 * opening or header nearby sites wear (site.avoid) leaves the pick unless
 * every eligible one is worn. */
static const gchar *
layout_pick(JsonObject *library, JsonObject *design, const gchar *type, Content *content, JsonObject *site){
	g_autoptr(GArray) choices = choices_for(library, design, type, content);
	g_autofree gchar *prefix = g_strconcat(type, "/", NULL);
	JsonObject *variants = object_of(library, "variants");
	if (g_str_equal(type, "hero"))
		return pick_from(library, variants, prefix, choices, site, type, "opening", "hero/");
	if (g_str_equal(type, "header"))
		return pick_from(library, variants, prefix, choices, site, type, "header", "");
	if (g_str_equal(type, "footer"))
		return pick_from(library, variants, prefix, choices, site, type, NULL, NULL);
	return weighted(choices, business_name(site), type);
}

/* A named layout of a library block (skeletons, motion): the eligible
 * entries, those that fit the sector weighted higher; the owner's words
 * first; a skeleton a nearby site wears leaves the pick. Entry names
 * outlive the result. */
static const gchar *
block_pick(JsonObject *library, const gchar *block, const gchar *salt, Content *content, JsonObject *site,
	const gchar *avoid_key){
	JsonObject *entries = object_of(library, block);
	g_autoptr(GList) names = entries != NULL ? json_object_get_members(entries) : NULL;
	g_autoptr(GArray) choices = g_array_new(FALSE, FALSE, sizeof(Choice));
	GList *name;
	for (name = names; name != NULL; name = name->next) {
		JsonObject *entry = object_of(entries, name->data);
		Choice choice;
		if (!needs_met(object_of(entry, "needs"), content))
			continue;
		choice.name = name->data;
		choice.weight = weight(library, "other");
		if (list_has(array_of(entry, "fits"), content->sector))
			choice.weight *= weight(library, "variant_fit");
		g_array_append_val(choices, choice);
	}
	return pick_from(library, entries, "", choices, site, salt, avoid_key, "");
}

/* The overlays a site wears: each eligible one by its share of a stable
 * hash; placement components are section variants and never listed. An
 * overlay skips the headers it names in not_headers. */
static JsonArray *
overlays_pick(JsonObject *library, Content *content, JsonObject *site, const gchar *header){
	JsonObject *entries = object_of(library, "overlays");
	g_autoptr(GList) names = entries != NULL ? json_object_get_members(entries) : NULL;
	JsonArray *chosen = json_array_new();
	GList *name;
	for (name = names; name != NULL; name = name->next) {
		JsonObject *entry = object_of(entries, name->data);
		g_autofree gchar *key = g_strconcat(business_name(site), "/overlay/", (const gchar *)name->data, NULL);
		gint64 share = entry != NULL && json_object_has_member(entry, "share") ? json_object_get_int_member(entry, "share") : 50;
		JsonArray *not_headers = entry != NULL ? array_of(entry, "not_headers") : NULL;
		gboolean clashes = FALSE;
		guint h;
		if (str_of(entry, "variant") != NULL || !needs_met(object_of(entry, "needs"), content))
			continue;
		for (h = 0; not_headers != NULL && h < json_array_get_length(not_headers); h++)
			clashes = clashes || g_strcmp0(json_array_get_string_element(not_headers, h), header) == 0;
		if (clashes)
			continue;
		if ((gint64)(ls_design_hash(key) % 100) < share)
			json_array_add_string_element(chosen, name->data);
	}
	return chosen;
}

/* How many supplied photos a home section shows: the opening one or two, a
 * card or row per service, service groups while photos last, the closing
 * band one, and the owner's own in the gallery and the story. */
static guint
photos_used(const gchar *component, const gchar *variant, Content *content){
	if (g_strcmp0(component, "hero") == 0)
		return g_strcmp0(variant, "photo-pair") == 0 ? MIN(2, content->free)
			: g_strcmp0(variant, "collage") == 0 ? MIN(3, content->free)
			: g_strcmp0(variant, "thumb-strip") == 0 ? MIN(4, content->free) : MIN(1, content->free);
	if (g_strcmp0(component, "areas") == 0 && g_strcmp0(variant, "card-over-photo") == 0)
		return MIN(1, content->free);
	if (g_strcmp0(component, "services") == 0 && (g_strcmp0(variant, "photo-cards") == 0 ||
		g_strcmp0(variant, "round-photos") == 0 || g_strcmp0(variant, "feature-rows") == 0 ||
		g_strcmp0(variant, "sublinks") == 0))
		return MIN(content->items, content->free);
	if (g_strcmp0(component, "cta") == 0 && g_strcmp0(variant, "photo-band") == 0)
		return MIN(1, content->free);
	/* The owner's own photos: up to four in the gallery, one in a story. */
	if (g_strcmp0(component, "gallery") == 0)
		return MIN(4, content->free);
	if (g_strcmp0(component, "about") == 0 && (g_strcmp0(variant, "photo-story") == 0 ||
		g_strcmp0(variant, "owner-letter") == 0))
		return MIN(1, content->free);
	return 0;
}

/* Walks the home as the builder fills it: each section must still have the
 * photos it shows after the ones before it, else it takes the heaviest
 * eligible variant that does. Leaves content->free at what is left. */
static void
settle_photos(JsonObject *library, JsonObject *design, JsonArray *home, Content *content){
	guint i, j;
	content->free = content->photos;
	for (i = 0; i < json_array_get_length(home); i++) {
		JsonObject *section = json_array_get_object_element(home, i);
		const gchar *component = str_of(section, "component");
		g_autofree gchar *pair = g_strdup_printf("%s/%s", component, str_of(section, "variant"));
		if (i > 0 && object_of(object_of(library, "variants"), pair) != NULL && !eligible(library, pair, content)) {
			g_autoptr(GArray) choices = choices_for(library, design, component, content);
			const gchar *before = i > 0 ? family_of(library, str_of(json_array_get_object_element(home, i - 1),
				"component"), str_of(json_array_get_object_element(home, i - 1), "variant")) : NULL;
			const gchar *pick = NULL;
			gint64 heaviest = 0;
			for (j = 0; j < choices->len; j++) {
				Choice *choice = &g_array_index(choices, Choice, j);
				gint64 w = choice->weight * (g_strcmp0(family_of(library, component, choice->name), before) != 0 ? 2 : 1);
				if (w > heaviest) {
					heaviest = w;
					pick = choice->name;
				}
			}
			if (pick != NULL)
				json_object_set_string_member(section, "variant", pick);
		}
		content->free -= photos_used(component, str_of(section, "variant"), content);
	}
}

/* The home's section types in order: the sector's home or one of the other
 * orders it allows, by the hash. */
static JsonArray *
home_order(JsonObject *sector, JsonObject *site){
	JsonArray *orders = array_of(sector, "orders");
	g_autoptr(GArray) choices = g_array_new(FALSE, FALSE, sizeof(Choice));
	g_autofree gchar **labels = NULL;
	const gchar *picked;
	guint i, n = orders != NULL ? json_array_get_length(orders) : 0;
	if (n == 0)
		return array_of(sector, "home");
	labels = g_new0(gchar *, n + 2);
	for (i = 0; i <= n; i++) {
		Choice choice;
		labels[i] = g_strdup_printf("%u", i);
		choice.name = labels[i];
		choice.weight = 1;
		g_array_append_val(choices, choice);
	}
	picked = weighted(choices, business_name(site), "order");
	i = (guint)g_ascii_strtoull(picked, NULL, 10);
	{
		JsonArray *out = i == 0 ? array_of(sector, "home") : json_array_get_array_element(orders, i - 1);
		guint k;
		for (k = 0; k <= n; k++)
			g_free(labels[k]);
		return out;
	}
}

/* A copy of @base with every member of @over set over it. */
static JsonObject *
merged(JsonObject *base, JsonObject *over){
	JsonObject *out = json_object_new();
	JsonObject *layers[2];
	guint l;
	layers[0] = base;
	layers[1] = over;
	for (l = 0; l < 2; l++) {
		g_autoptr(GList) keys = layers[l] != NULL ? json_object_get_members(layers[l]) : NULL;
		GList *key;
		for (key = keys; key != NULL; key = key->next)
			json_object_set_member(out, key->data, json_node_copy(json_object_get_member(layers[l], key->data)));
	}
	return out;
}

/* The preset's own dark scheme (docs/components.org, "Light and dark"):
 * its hand-written colour tokens over the light ones, the colour roles
 * built from them and repaired to the same AA pairs, under the same owner
 * colour words, and its photo scrim. */
static JsonObject *
dark_scheme(JsonObject *library, JsonObject *theme, JsonObject *site){
	JsonObject *given = object_of(object_of(theme, "design"), "dark");
	JsonObject *dark = json_object_new();
	g_autoptr(JsonObject) tokens = merged(object_of(theme, "tokens"), object_of(given, "tokens"));
	g_autoptr(JsonObject) colors = json_object_new();
	g_autoptr(JsonArray) ignored = json_array_new();
	const gchar *keys[] = {"paper", "ink", "muted", "surface", "line", "accent", "accent_ink"};
	guint i;
	for (i = 0; i < G_N_ELEMENTS(keys); i++)
		if (str_of(tokens, keys[i]) != NULL)
			json_object_set_string_member(colors, keys[i], str_of(tokens, keys[i]));
	json_object_set_object_member(dark, "tokens", json_object_ref(colors));
	json_object_set_object_member(dark, "roles", build_roles(library, tokens, object_of(given, "roles"), site, ignored));
	json_object_set_double_member(dark, "scrim", given != NULL && json_object_has_member(given, "scrim")
		? json_object_get_double_member(given, "scrim") : 0.65);
	return dark;
}

/* Whether the order offers urgent work: an offer, the tagline or the
 * requirements name emergencies, urgent calls or round-the-clock answers. */
static gboolean
offers_urgent(JsonObject *site){
	static const gchar *const words[] = {"emergency", "emergencies", "urgent", "24/7", "24 hours", "around the clock",
		"out of hours", "after-hours", "after hours", NULL};
	JsonArray *offers = array_of(site, "offers");
	g_autoptr(GString) text = g_string_new(str_of(site, "requirements"));
	guint i;
	g_string_append_printf(text, " %s", str_of(object_of(site, "brand"), "tagline") != NULL
		? str_of(object_of(site, "brand"), "tagline") : "");
	for (i = 0; offers != NULL && i < json_array_get_length(offers); i++) {
		JsonNode *node = json_array_get_element(offers, i);
		JsonObject *offer = JSON_NODE_HOLDS_OBJECT(node) ? json_node_get_object(node) : NULL;
		g_string_append_printf(text, " %s %s", str_of(offer, "name") != NULL ? str_of(offer, "name") : "",
			str_of(offer, "description") != NULL ? str_of(offer, "description") : "");
	}
	{
		g_autofree gchar *lower = g_utf8_strdown(text->str, -1);
		for (i = 0; words[i] != NULL; i++)
			if (strstr(lower, words[i]) != NULL)
				return TRUE;
	}
	return FALSE;
}

/* What the builder's enquiry and booking forms ask (docs/components.org,
 * "Controls"): an urgent switch where the order offers urgent work, and a
 * photo upload where the trade benefits from seeing the job. */
static JsonObject *
form_controls(JsonObject *site, const gchar *sector){
	JsonObject *controls = json_object_new();
	const gchar *visual[] = {"trades", "outdoor", "cleaning", "auto"};
	gboolean photos = FALSE;
	guint i;
	for (i = 0; i < G_N_ELEMENTS(visual); i++)
		photos = photos || g_strcmp0(sector, visual[i]) == 0;
	json_object_set_boolean_member(controls, "urgent", offers_urgent(site));
	json_object_set_boolean_member(controls, "photos", photos);
	return controls;
}

JsonObject *
ls_design_new(JsonObject *library, JsonObject *themes, JsonObject *site, const gchar *preset,
	JsonArray *photos, GError **error){
	JsonObject *theme = preset != NULL ? object_of(themes, preset) : NULL;
	JsonObject *preset_design = object_of(theme, "design");
	JsonObject *design, *sector;
	JsonArray *home, *types, *applied;
	g_autoptr(JsonObject) facts = ls_design_facts(site);
	Content content = content_of(site, photos, facts);
	const gchar *sector_name = sector_of(library, site), *action;
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
	content.sector = sector_name;
	action = str_of(sector, "action") != NULL ? str_of(sector, "action") : "quote";
	if (g_str_equal(action, "book") && !content.booking)
		action = "call";
	if (g_str_equal(action, "call") && !content.phone)
		action = "quote";
	content.action = action;
	json_object_set_string_member(design, "action", action);
	/* The footer first, as before; the header after the opening, which it
	 * may lie over. */
	json_object_set_string_member(design, "footer", layout_pick(library, preset_design, "footer", &content, site));
	home = json_array_new();
	types = home_order(sector, site);
	for (i = 0; types != NULL && i < json_array_get_length(types); i++) {
		const gchar *type = json_array_get_string_element(types, i);
		const gchar *variant = layout_pick(library, preset_design, type, &content, site);
		if (variant == NULL && g_str_equal(type, "hero")) {
			/* Without a photo the home opens on the plain opening. */
			const gchar *fallback = str_of(library, "fallback_opening");
			const gchar *slash = fallback != NULL ? strchr(fallback, '/') : NULL;
			if (slash != NULL)
				add_section(home, "hero", slash + 1);
			continue;
		}
		if (variant != NULL) {
			add_section(home, type, variant);
			content.free -= photos_used(type, variant, &content);
		}
	}
	/* An opening that carries the trust facts in its own bar leaves no
	 * trust strip under it. */
	if (g_strcmp0(str_of(json_array_get_length(home) > 0 ? json_array_get_object_element(home, 0) : NULL, "variant"),
		"stats-bar") == 0) {
		guint k = json_array_get_length(home);
		while (k-- > 1)
			if (g_strcmp0(str_of(json_array_get_object_element(home, k), "component"), "facts") == 0)
				json_array_remove_element(home, k);
	}
	/* Families apart and every section's photos there, twice, since each
	 * move may unsettle the other. */
	for (i = 0; i < 2; i++) {
		separate_families(library, preset_design, home, &content);
		space_brands(library, preset_design, home, &content);
		settle_photos(library, preset_design, home, &content);
	}
	if (json_array_get_length(home) > 0) {
		JsonObject *first = json_array_get_object_element(home, 0);
		content.opening = family_of(library, str_of(first, "component"), str_of(first, "variant"));
	}
	json_object_set_string_member(design, "header", layout_pick(library, preset_design, "header", &content, site));
	/* The page skeleton, the motion level and the overlays. */
	for (i = 0; i < json_array_get_length(home); i++)
		content.gallery = content.gallery ||
			g_strcmp0(str_of(json_array_get_object_element(home, i), "component"), "gallery") == 0;
	{
		const gchar *skeleton = block_pick(library, "skeletons", "skeleton", &content, site, "skeleton");
		const gchar *motion = block_pick(library, "motion", "motion", &content, site, NULL);
		if (skeleton != NULL)
			json_object_set_string_member(design, "skeleton", skeleton);
		if (motion != NULL)
			json_object_set_string_member(design, "motion", motion);
	}
	json_object_set_array_member(design, "overlays", overlays_pick(library, &content, site, str_of(design, "header")));
	assign_tones(library, home, str_of(design, "footer"));
	json_object_set_array_member(design, "home", home);
	json_object_set_object_member(design, "facts", json_object_ref(facts));
	/* A structured order's offers fix the pages; a brief alone leaves them
	 * to the plan. */
	if (content.items > 0)
		json_object_set_array_member(design, "pages", site_pages(site, &content));
	applied = json_array_new();
	json_object_set_object_member(design, "roles", build_roles(library, object_of(theme, "tokens"),
		object_of(preset_design, "roles"), site, applied));
	/* Both schemes; the order may lock one, else the visitor's system and
	 * the toggle choose. */
	json_object_set_string_member(design, "scheme", str_of(site, "scheme") != NULL ? str_of(site, "scheme") : "auto");
	json_object_set_object_member(design, "dark", dark_scheme(library, theme, site));
	json_object_set_object_member(design, "controls", form_controls(site, sector_name));
	json_object_set_array_member(design, "colors", applied);
	json_object_set_string_member(design, "kicker", str_of(preset_design, "kicker") != NULL
		? str_of(preset_design, "kicker") : "caps");
	json_object_set_string_member(design, "accent", str_of(preset_design, "accent") != NULL
		? str_of(preset_design, "accent") : "colour");
	if (str_of(preset_design, "font_accent") != NULL)
		json_object_set_string_member(design, "font_accent", str_of(preset_design, "font_accent"));
	if (str_of(preset_design, "font_label") != NULL)
		json_object_set_string_member(design, "font_label", str_of(preset_design, "font_label"));
	{
		JsonObject *shape = json_object_new(), *given = object_of(preset_design, "shape");
		const gchar *keys[] = {"control", "box", "badge", "depth"};
		guint k;
		for (k = 0; k < G_N_ELEMENTS(keys); k++)
			json_object_set_int_member(shape, keys[k], given != NULL && json_object_has_member(given, keys[k])
				? json_object_get_int_member(given, keys[k]) : 0);
		json_object_set_double_member(shape, "scrim", given != NULL && json_object_has_member(given, "scrim")
			? json_object_get_double_member(given, "scrim") : 0.55);
		json_object_set_string_member(shape, "frame", str_of(given, "frame") != NULL ? str_of(given, "frame") : "box");
		json_object_set_object_member(design, "shape", shape);
	}
	return design;
}

/* Whether the header layout @header lies over the opening photo. */
static gboolean
over_photo(JsonObject *library, const gchar *header){
	g_autofree gchar *pair = g_strdup_printf("header/%s", header != NULL ? header : "");
	JsonObject *needs = object_of(object_of(object_of(library, "variants"), pair), "needs");
	return needs != NULL && json_object_has_member(needs, "over_photo") && json_object_get_boolean_member(needs, "over_photo");
}

gboolean
ls_design_set_layout(JsonObject *library, JsonObject *design, const gchar *type, const gchar *variant,
	GError **error){
	g_autofree gchar *pair = NULL;
	g_autoptr(JsonNode) before = NULL;
	g_autoptr(GString) why = g_string_new(NULL);
	JsonArray *home = array_of(design, "home");
	JsonObject *first = home != NULL && json_array_get_length(home) > 0 ? json_array_get_object_element(home, 0) : NULL;
	gboolean layout = g_strcmp0(type, "header") == 0 || g_strcmp0(type, "footer") == 0;
	guint i;
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);
	/* The skeleton, the motion level and the overlays are named entries of
	 * their own library blocks. */
	if (g_strcmp0(type, "skeleton") == 0 || g_strcmp0(type, "motion") == 0 || g_strcmp0(type, "overlay") == 0) {
		const gchar *block = g_str_equal(type, "skeleton") ? "skeletons" : g_str_equal(type, "motion") ? "motion" : "overlays";
		JsonObject *entry = object_of(object_of(library, block), variant != NULL ? variant : "");
		if (design == NULL || entry == NULL || (g_str_equal(type, "overlay") && str_of(entry, "variant") != NULL)) {
			g_set_error(error, LS_ERROR, LS_ERROR_INVALID, "'%s' is not a library %s", variant != NULL ? variant : "", type);
			return FALSE;
		}
		if (g_str_equal(type, "overlay")) {
			JsonArray *overlays = array_of(design, "overlays");
			if (overlays == NULL) {
				overlays = json_array_new();
				json_object_set_array_member(design, "overlays", overlays);
			}
			for (i = 0; i < json_array_get_length(overlays); i++)
				if (g_strcmp0(json_array_get_string_element(overlays, i), variant) == 0)
					return TRUE;
			json_array_add_string_element(overlays, variant);
		} else
			json_object_set_string_member(design, type, variant);
		return TRUE;
	}
	pair = g_strdup_printf("%s/%s", type != NULL ? type : "", variant != NULL ? variant : "");
	if (design == NULL || home == NULL || object_of(object_of(library, "variants"), pair) == NULL) {
		g_set_error(error, LS_ERROR, LS_ERROR_INVALID, "'%s' is not a library layout", pair);
		return FALSE;
	}
	/* A deep copy to restore when a rule breaks. */
	{
		g_autoptr(JsonNode) wrap = json_node_new(JSON_NODE_OBJECT);
		g_autofree gchar *text = NULL;
		json_node_set_object(wrap, design);
		text = ls_json_encode(wrap);
		before = ls_json_parse(text, NULL);
	}
	if (layout)
		json_object_set_string_member(design, type, variant);
	else {
		JsonObject *section = NULL;
		for (i = 0; section == NULL && i < json_array_get_length(home); i++)
			if (g_strcmp0(str_of(json_array_get_object_element(home, i), "component"), type) == 0)
				section = json_array_get_object_element(home, i);
		if (section == NULL) {
			g_set_error(error, LS_ERROR, LS_ERROR_INVALID, "the home has no %s section", type);
			return FALSE;
		}
		json_object_set_string_member(section, "variant", variant);
	}
	/* A header over a photo needs a full-bleed opening. */
	if (over_photo(library, str_of(design, "header")) && (first == NULL ||
		g_strcmp0(family_of(library, str_of(first, "component"), str_of(first, "variant")), "bleed") != 0))
		g_string_append(why, "the header lies over the opening only when the opening is a full-bleed photo\n");
	assign_tones(library, home, str_of(design, "footer"));
	if (why->len == 0)
		ls_design_check_home(library, home, str_of(design, "footer"), why);
	if (why->len > 0) {
		JsonObject *old = json_node_get_object(before);
		g_autoptr(GList) keys = json_object_get_members(old);
		GList *key;
		for (key = keys; key != NULL; key = key->next)
			json_object_set_member(design, key->data, json_node_copy(json_object_get_member(old, key->data)));
		g_set_error(error, LS_ERROR, LS_ERROR_INVALID, "%s/%s breaks a design rule: %s", type, variant,
			g_strstrip(why->str));
		return FALSE;
	}
	return TRUE;
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
		"|preset|source|sector|action|header|footer|home|roles|colors|kicker|accent|font_accent|font_label|shape|facts|pages|"
		"skeleton|motion|overlays|scheme|dark|controls|", NULL)) {
		g_string_append(why, "design may only have preset, source, sector, action, header, footer, home, roles, "
			"colors, kicker, accent, font_accent, font_label, shape, facts, pages, skeleton, motion, overlays, scheme, "
			"dark and controls\n");
		return FALSE;
	}
	/* Older designs have one scheme: they read as light only. */
	if (json_object_has_member(design, "scheme") && g_strcmp0(str_of(design, "scheme"), "auto") != 0 &&
		g_strcmp0(str_of(design, "scheme"), "light") != 0 && g_strcmp0(str_of(design, "scheme"), "dark") != 0)
		g_string_append(why, "design.scheme must be auto, light or dark\n");
	if (json_object_has_member(design, "dark")) {
		JsonObject *dark = object_of(design, "dark"), *dark_roles = object_of(dark, "roles");
		JsonObject *dark_tokens = object_of(dark, "tokens");
		const gchar *keys[] = {"paper", "ink", "muted", "surface", "line", "accent", "accent_ink"};
		gboolean whole = TRUE;
		if (dark == NULL || !ls_object_keys(dark, "|tokens|roles|scrim|", NULL) || dark_roles == NULL || dark_tokens == NULL ||
			!json_object_has_member(dark, "scrim") || json_object_get_double_member(dark, "scrim") < 0.2 ||
			json_object_get_double_member(dark, "scrim") > 0.85) {
			g_string_append(why, "design.dark needs {tokens, roles, scrim} with a scrim from 0.2 to 0.85\n");
			whole = FALSE;
		}
		for (i = 0; whole && i < G_N_ELEMENTS(keys); i++)
			if (!hex_valid(str_of(dark_tokens, keys[i]))) {
				g_string_append_printf(why, "design.dark.tokens.%s must be #rrggbb\n", keys[i]);
				whole = FALSE;
			}
		for (i = 0; whole && i < G_N_ELEMENTS(role_names); i++)
			if (!hex_valid(str_of(dark_roles, role_names[i]))) {
				g_string_append_printf(why, "design.dark.roles.%s must be #rrggbb\n", role_names[i]);
				whole = FALSE;
			}
		for (i = 0; whole && i < G_N_ELEMENTS(text_pairs); i++)
			if (ls_design_contrast(str_of(dark_roles, text_pairs[i][0]), str_of(dark_roles, text_pairs[i][1])) < 4.5)
				g_string_append_printf(why, "design.dark.roles.%s on %s is under 4.5:1\n", text_pairs[i][1], text_pairs[i][0]);
	}
	if (json_object_has_member(design, "controls")) {
		JsonObject *controls = object_of(design, "controls");
		if (controls == NULL || !ls_object_keys(controls, "|urgent|photos|", NULL) ||
			!json_object_has_member(controls, "urgent") || !json_object_has_member(controls, "photos") ||
			json_node_get_value_type(json_object_get_member(controls, "urgent")) != G_TYPE_BOOLEAN ||
			json_node_get_value_type(json_object_get_member(controls, "photos")) != G_TYPE_BOOLEAN)
			g_string_append(why, "design.controls needs {urgent, photos}, each true or false\n");
	}
	/* Older designs have no skeleton, motion or overlays: they read as the
	 * stack, no motion and none. */
	if (json_object_has_member(design, "skeleton") && object_of(object_of(library, "skeletons"),
		str_of(design, "skeleton") != NULL ? str_of(design, "skeleton") : "") == NULL)
		g_string_append(why, "design.skeleton must be a library skeleton\n");
	if (json_object_has_member(design, "motion") && object_of(object_of(library, "motion"),
		str_of(design, "motion") != NULL ? str_of(design, "motion") : "") == NULL)
		g_string_append(why, "design.motion must be none, calm or lively\n");
	if (json_object_has_member(design, "overlays")) {
		JsonArray *overlays = array_of(design, "overlays");
		if (overlays == NULL)
			g_string_append(why, "design.overlays must be a list\n");
		for (i = 0; overlays != NULL && i < json_array_get_length(overlays); i++) {
			JsonNode *node = json_array_get_element(overlays, i);
			JsonObject *entry = JSON_NODE_HOLDS_VALUE(node) && json_node_get_value_type(node) == G_TYPE_STRING
				? object_of(object_of(library, "overlays"), json_node_get_string(node)) : NULL;
			if (entry == NULL || str_of(entry, "variant") != NULL)
				g_string_append_printf(why, "design.overlays[%u] must be a library overlay\n", i);
		}
	}
	/* Older designs have no pages or facts; newer ones list routes the
	 * plan checks again as planned pages. */
	{
		JsonArray *pages = array_of(design, "pages");
		JsonObject *facts = object_of(design, "facts");
		if (json_object_has_member(design, "pages") && (pages == NULL || json_array_get_length(pages) == 0 ||
			json_array_get_length(pages) > 24))
			g_string_append(why, "design.pages must list 1 to 24 pages\n");
		for (i = 0; pages != NULL && i < json_array_get_length(pages); i++) {
			JsonNode *node = json_array_get_element(pages, i);
			JsonObject *page = JSON_NODE_HOLDS_OBJECT(node) ? json_node_get_object(node) : NULL;
			if (page == NULL || !ls_object_keys(page, "|path|kind|title|offer|described|", NULL) || str_of(page, "path") == NULL ||
				!g_str_has_suffix(str_of(page, "path"), ".html") || str_of(page, "kind") == NULL ||
				str_of(page, "title") == NULL)
				g_string_append_printf(why, "design.pages[%u] must be {path, kind, title, optional offer and described}\n", i);
		}
		if (json_object_has_member(design, "facts") && (facts == NULL ||
			!ls_object_keys(facts, "|since|hours|address|area|process|press|rating|locations|", NULL)))
			g_string_append(why, "design.facts may only have since, hours, address, area, process, press, rating and "
				"locations\n");
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
		!g_str_equal(str_of(design, "accent"), "italic") && !g_str_equal(str_of(design, "accent"), "script")))
		g_string_append(why, "design.accent must be colour, italic or script\n");
	{
		const gchar *stacks[] = {"font_accent", "font_label"};
		guint f;
		for (f = 0; f < G_N_ELEMENTS(stacks); f++)
			if (str_of(design, stacks[f]) != NULL && (strpbrk(str_of(design, stacks[f]), "{};<>\\") != NULL ||
				strlen(str_of(design, stacks[f])) > 120))
				g_string_append_printf(why, "design.%s must be a plain font stack\n", stacks[f]);
	}
	if (over_photo(library, str_of(design, "header")) && home != NULL && json_array_get_length(home) > 0 &&
		JSON_NODE_HOLDS_OBJECT(json_array_get_element(home, 0)) &&
		g_strcmp0(family_of(library, str_of(json_array_get_object_element(home, 0), "component"),
			str_of(json_array_get_object_element(home, 0), "variant")), "bleed") != 0)
		g_string_append_printf(why, "design.header %s needs a full-bleed photo opening\n", str_of(design, "header"));
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
		json_object_get_double_member(shape, "scrim") > 0.8 ||
		(json_object_has_member(shape, "frame") && g_strcmp0(str_of(shape, "frame"), "circle") != 0 &&
		g_strcmp0(str_of(shape, "frame"), "box") != 0))
		g_string_append(why, "design.shape needs control 0 to 999, box 0 to 32, badge 0 to 999, depth 0 or 1, "
			"scrim 0.2 to 0.8 and an optional frame of circle or box\n");
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

/* The colour roles of one scheme as custom properties, with the hover and
 * soft tints derived from them. */
static void
roles_css(GString *out, JsonObject *roles){
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
}

/* Raised boxes: soft shadows in light; in dark the box's lighter surface
 * gives the depth and a faint light edge replaces the black shadow. */
static void
shadows_css(GString *out, JsonObject *shape, gboolean dark){
	if (shape == NULL || json_object_get_int_member(shape, "depth") == 0)
		g_string_append(out, "\t--shadow-1: none;\n\t--shadow-2: none;\n\t--shadow-3: none;\n");
	else if (dark)
		g_string_append(out, "\t--shadow-1: 0 0 0 1px rgb(255 255 255 / 0.06);\n"
			"\t--shadow-2: 0 0 0 1px rgb(255 255 255 / 0.08);\n\t--shadow-3: 0 0 0 1px rgb(255 255 255 / 0.1);\n");
	else
		g_string_append(out, "\t--shadow-1: 0 1px 2px rgb(16 24 40 / 0.06);\n"
			"\t--shadow-2: 0 4px 16px rgb(16 24 40 / 0.08);\n\t--shadow-3: 0 12px 32px rgb(16 24 40 / 0.12);\n");
}

gchar *
ls_design_dark_css(JsonObject *design){
	JsonObject *dark = object_of(design, "dark");
	GString *out = g_string_new(NULL);
	gchar scrim[G_ASCII_DTOSTR_BUF_SIZE];
	if (object_of(dark, "roles") == NULL)
		return g_string_free(out, FALSE);
	roles_css(out, object_of(dark, "roles"));
	shadows_css(out, object_of(design, "shape"), TRUE);
	g_string_append_printf(out, "\t--scrim: %s;\n", g_ascii_formatd(scrim, sizeof scrim, "%.2f",
		json_object_get_double_member(dark, "scrim")));
	return g_string_free(out, FALSE);
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
	roles_css(out, roles);
	g_string_append_printf(out, "\t--font-accent: %s;\n", str_of(design, "font_accent") != NULL
		? str_of(design, "font_accent") : "var(--font-display)");
	g_string_append_printf(out, "\t--font-label: %s;\n", str_of(design, "font_label") != NULL
		? str_of(design, "font_label") : "var(--font-body)");
	for (i = 0; i < G_N_ELEMENTS(steps); i++)
		g_string_append_printf(out, "\t--text-%u: %s;\n", i + 1, steps[i]);
	for (i = 0; i < G_N_ELEMENTS(space); i++)
		g_string_append_printf(out, "\t--space-%u: %s;\n", i + 1, space[i]);
	g_string_append(out, "\t--container: 72rem;\n\t--container-narrow: 44rem;\n"
		"\t--section-y: clamp(3.5rem, 7cqi, 6rem);\n");
	add_px(out, "radius-control", shape != NULL ? json_object_get_int_member(shape, "control") : 4);
	add_px(out, "radius-box", shape != NULL ? json_object_get_int_member(shape, "box") : 8);
	add_px(out, "radius-badge", shape != NULL ? json_object_get_int_member(shape, "badge") : 999);
	/* A framed photo is a circle on some presets, else the box radius. */
	if (g_strcmp0(str_of(shape, "frame"), "circle") == 0)
		g_string_append(out, "\t--radius-frame: 50%;\n");
	else
		g_string_append(out, "\t--radius-frame: var(--radius-box);\n");
	shadows_css(out, shape, FALSE);
	g_string_append_printf(out, "\t--depth: %" G_GINT64_FORMAT ";\n", shape != NULL
		? json_object_get_int_member(shape, "depth") : 0);
	g_string_append_printf(out, "\t--scrim: %s;\n", g_ascii_formatd(scrim, sizeof scrim, "%.2f",
		shape != NULL ? json_object_get_double_member(shape, "scrim") : 0.55));
	g_string_append(out, "}\n");
	return g_string_free(out, FALSE);
}
