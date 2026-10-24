/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "core/ls-recipe.h"
#include "core/ls-context.h"
#include "core/ls-module.h"
#include "core/ls-visual.h"
#include <string.h>
#include <yaml-glib.h>
struct _LsRecipe {
	GObject parent_instance;
	JsonNode *root;
	gchar *source;
	gchar *scaffold_source;
	gchar *components_source;
	gint timeout;
	gboolean unlimited;
	gchar *provider;
	gchar *model;
	GHashTable *approvals;
	gboolean approve_all;
};
G_DEFINE_TYPE(LsRecipe, ls_recipe, G_TYPE_OBJECT)
/* Recipe owns the complete parsed tree, including every borrowed step. */
static void
ls_recipe_finalize(GObject *object){
	LsRecipe *self = LS_RECIPE(object);
	g_clear_pointer(&self->root, json_node_unref);
	g_free(self->source);
	g_free(self->scaffold_source);
	g_free(self->components_source);
	g_free(self->provider);
	g_free(self->model);
	g_clear_pointer(&self->approvals, g_hash_table_unref);
	G_OBJECT_CLASS(ls_recipe_parent_class)->finalize(object);
}
static void
ls_recipe_class_init(LsRecipeClass *klass){
	G_OBJECT_CLASS(klass)->finalize = ls_recipe_finalize;
}
static void
ls_recipe_init(LsRecipe *self){
	self->timeout = -1;
	self->approvals = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
}
/* The CLI agents that pinned ai-glib can drive. */
static gboolean
provider_supported(const gchar *provider){
	return g_str_equal(provider, "codex") || g_str_equal(provider, "claude-code") ||
		g_str_equal(provider, "cursor") || g_str_equal(provider, "grok-build") ||
		g_str_equal(provider, "antigravity");
}
/* Test node types before using JSON-GLib's typed accessors. */
static gboolean
object_member(JsonObject *object, const gchar *key, GError **error){
	JsonNode *node = json_object_get_member(object, key);
	if (node == NULL || !JSON_NODE_HOLDS_OBJECT(node)) {
		g_set_error(error, LS_ERROR, LS_ERROR_INVALID, "'%s' must be a mapping", key);
		return FALSE;
	}
	return TRUE;
}
static gboolean
text_member(JsonObject *object, const gchar *key, gboolean required, GError **error){
	const gchar *value;
	if (!required && !json_object_has_member(object, key))
		return TRUE;
	value = ls_string(object, key);
	if (value == NULL || *value == '\0') {
		g_set_error(error, LS_ERROR, LS_ERROR_INVALID, "'%s' must be a nonempty string", key);
		return FALSE;
	}
	return TRUE;
}
/* A CLI provider's model is intentionally opaque; ai-glib owns model routing. */
static gboolean
validate_ai(JsonObject *ai, GError **error){
	g_autoptr(GList) members = NULL;
	GList *item;
	const gchar *provider;
	if (!ls_object_keys(ai, "|provider|model|effort|timeout|retries|max_tokens|", error))
		return FALSE;
	members = json_object_get_members(ai);
	for (item = members; item != NULL; item = item->next) {
		const gchar *key = (const gchar *)item->data;
		if (g_str_equal(key, "timeout") || g_str_equal(key, "retries") ||
			g_str_equal(key, "max_tokens")) {
			JsonNode *node = json_object_get_member(ai, key);
			gint64 value;
			if (json_node_get_value_type(node) != G_TYPE_INT64)
				goto invalid_number;
			value = json_node_get_int(node);
			if ((g_str_equal(key, "retries") && (value < 0 || value > 5)) ||
				(g_str_equal(key, "timeout") && (value < 0 || value > 86400)) ||
				(g_str_equal(key, "max_tokens") && value != 0 && (value < 256 || value > 131072)))
				goto invalid_number;
		} else if (!text_member(ai, key, TRUE, error))
			return FALSE;
	}
	provider = ls_string(ai, "provider");
	if (provider != NULL && !provider_supported(provider)) {
		g_set_error(error, LS_ERROR, LS_ERROR_INVALID, "Unsupported CLI provider '%s'", provider);
		return FALSE;
	}
	return TRUE;
invalid_number:
	g_set_error_literal(
		error,
		LS_ERROR,
		LS_ERROR_INVALID,
		"AI settings require integer timeout 0..86400 seconds, retries 0..5 and max_tokens "
		"0 or 256..131072 (0 means unlimited)"
	);
	return FALSE;
}
/* Structured collections are bounded and reject duplicates before prompting. */
static gboolean
text_list(JsonObject *object, const gchar *key, guint minimum, GError **error){
	JsonNode *node = json_object_get_member(object, key);
	JsonArray *array;
	g_autoptr(GHashTable) seen = g_hash_table_new(g_str_hash, g_str_equal);
	guint i;
	if (node == NULL || !JSON_NODE_HOLDS_ARRAY(node))
		goto invalid;
	array = json_node_get_array(node);
	if (json_array_get_length(array) < minimum || json_array_get_length(array) > 4096)
		goto invalid;
	for (i = 0; i < json_array_get_length(array); i++) {
		const gchar *value;
		node = json_array_get_element(array, i);
		if (json_node_get_value_type(node) != G_TYPE_STRING)
			goto invalid;
		value = json_node_get_string(node);
		if (*value == '\0' || !g_hash_table_add(seen, (gpointer)value))
			goto invalid;
	}
	return TRUE;
invalid:
	g_set_error(error, LS_ERROR, LS_ERROR_INVALID,
		"'%s' must contain %u..4096 unique nonempty strings", key, minimum);
	return FALSE;
}
/* Fixed mapping keys give form clients stable fields and catch spelling errors. */
static gboolean
text_fields(JsonObject *object, const gchar *keys, GError **error){
	g_autoptr(GList) members = json_object_get_members(object);
	GList *item;
	if (!ls_object_keys(object, keys, error))
		return FALSE;
	for (item = members; item != NULL; item = item->next)
		if (!text_member(object, (const gchar *)item->data, TRUE, error))
			return FALSE;
	return TRUE;
}
/* Pages use URL paths, not artifact paths: root and directory routes are valid.
 * Reject ambiguous traversal, query, fragment and escaped route spellings. */
static gboolean
route_valid(const gchar *path){
	g_auto(GStrv) parts = NULL;
	guint i;
	if (path[0] != '/' || strstr(path, "//") != NULL ||
		strpbrk(path, "\\?#%") != NULL)
		return FALSE;
	for (i = 0; path[i] != '\0'; i++)
		if (g_ascii_isspace(path[i]) || g_ascii_iscntrl(path[i]))
			return FALSE;
	parts = g_strsplit(path, "/", -1);
	for (i = 0; parts[i] != NULL; i++)
		if (g_str_equal(parts[i], ".") || g_str_equal(parts[i], ".."))
			return FALSE;
	return TRUE;
}
/* Page paths and offer IDs remain unique identities across generation stages. */
static gboolean
records(JsonObject *site, const gchar *key, gboolean pages, GError **error){
	JsonNode *node = json_object_get_member(site, key);
	JsonArray *array;
	g_autoptr(GHashTable) seen = g_hash_table_new(g_str_hash, g_str_equal);
	guint i;
	if (!JSON_NODE_HOLDS_ARRAY(node))
		goto invalid;
	array = json_node_get_array(node);
	if (json_array_get_length(array) > 128 || (pages && json_array_get_length(array) == 0))
		goto invalid;
	for (i = 0; i < json_array_get_length(array); i++) {
		JsonObject *record;
		const gchar *identity;
		g_autoptr(GList) members = NULL;
		GList *item;
		node = json_array_get_element(array, i);
		if (!JSON_NODE_HOLDS_OBJECT(node))
			goto invalid;
		record = json_node_get_object(node);
		if (!ls_object_keys(record, pages ? "|path|title|purpose|sections|" :
			"|id|name|description|price|cta_label|cta_url|", error) ||
			!text_member(record, pages ? "path" : "id", TRUE, error) ||
			!text_member(record, pages ? "title" : "name", TRUE, error))
			return FALSE;
		identity = ls_string(record, pages ? "path" : "id");
		if ((pages && !route_valid(identity)) || !g_hash_table_add(seen, (gpointer)identity))
			goto invalid;
		members = json_object_get_members(record);
		for (item = members; item != NULL; item = item->next) {
			const gchar *field = (const gchar *)item->data;
			if (g_str_equal(field, "sections")) {
				if (!text_list(record, field, 1, error))
					return FALSE;
			} else if (!text_member(record, field, TRUE, error))
				return FALSE;
		}
		if (!pages && json_object_has_member(record, "cta_label") !=
			json_object_has_member(record, "cta_url"))
			goto invalid;
	}
	return TRUE;
invalid:
	g_set_error(error, LS_ERROR, LS_ERROR_INVALID,
		"Invalid site.%s: expected %s..128 records with unique %s; offer CTAs need label and URL",
		key, pages ? "1" : "0", pages ? "absolute local paths" : "IDs");
	return FALSE;
}
/* Language tags are portable ASCII subtags, not locale/encoding filenames. */
static gboolean
language_valid(const gchar *tag){
	g_auto(GStrv) parts = g_strsplit(tag, "-", -1);
	guint i, j;
	for (i = 0; parts[i] != NULL; i++) {
		gsize length = strlen(parts[i]);
		if (length == 0 || length > 8 || (i == 0 && length < 2))
			return FALSE;
		for (j = 0; j < length; j++)
			if (i == 0 ? !g_ascii_isalpha(parts[i][j]) : !g_ascii_isalnum(parts[i][j]))
				return FALSE;
	}
	return TRUE;
}
/* Supplied photographs: seeded raster images under assets/ with alt text.
 * The component steps read their pixel sizes from the seeds. */
static gboolean
photo_list(JsonObject *site, GError **error){
	JsonNode *node = json_object_get_member(site, "photos");
	JsonArray *photos = node != NULL && JSON_NODE_HOLDS_ARRAY(node) ? json_node_get_array(node) : NULL;
	g_autoptr(GHashTable) seen = g_hash_table_new(g_str_hash, g_str_equal);
	guint i;
	if (photos == NULL || json_array_get_length(photos) == 0 || json_array_get_length(photos) > 48)
		goto invalid;
	for (i = 0; i < json_array_get_length(photos); i++) {
		JsonNode *entry = json_array_get_element(photos, i);
		JsonObject *photo = JSON_NODE_HOLDS_OBJECT(entry) ? json_node_get_object(entry) : NULL;
		const gchar *path = photo != NULL ? ls_string(photo, "path") : NULL;
		g_autofree gchar *lower = path != NULL ? g_ascii_strdown(path, -1) : NULL;
		if (photo == NULL || !ls_object_keys(photo, "|path|alt|credit|focus|", NULL) ||
			(json_object_has_member(photo, "focus") && !ls_focus_valid(ls_string(photo, "focus"))) ||
			!text_member(photo, "alt", TRUE, NULL) || !text_member(photo, "credit", FALSE, NULL) ||
			path == NULL || !g_str_has_prefix(path, "assets/") || strstr(path, "..") != NULL ||
			strpbrk(path, " \"'<>\\") != NULL ||
			!(g_str_has_suffix(lower, ".jpg") || g_str_has_suffix(lower, ".jpeg") ||
			g_str_has_suffix(lower, ".png") || g_str_has_suffix(lower, ".webp")) ||
			g_utf8_strlen(ls_string(photo, "alt"), -1) > 300 || strspn(ls_string(photo, "alt"), " \t\r\n") ==
			strlen(ls_string(photo, "alt")) || !g_hash_table_add(seen, (gpointer)path))
			goto invalid;
	}
	return TRUE;
invalid:
	g_set_error_literal(error, LS_ERROR, LS_ERROR_INVALID,
		"site.photos needs 1 to 48 unique {path, alt, credit, focus} entries: path an assets/ .jpg, .jpeg, .png or "
		".webp seed without spaces or quotes, alt text up to 300 characters, credit optional text, focus an optional "
		"position such as 'top', 'bottom right' or '70% 40%'");
	return FALSE;
}
/* Supplied social profiles: https addresses the builder links and shows
 * with the network's icon. Never generated. */
static gboolean
social_list(JsonObject *site, GError **error){
	JsonNode *node = json_object_get_member(site, "social");
	JsonArray *links = node != NULL && JSON_NODE_HOLDS_ARRAY(node) ? json_node_get_array(node) : NULL;
	g_autoptr(GHashTable) seen = g_hash_table_new(g_str_hash, g_str_equal);
	guint i;
	if (links == NULL || json_array_get_length(links) == 0 || json_array_get_length(links) > 12)
		goto invalid;
	for (i = 0; i < json_array_get_length(links); i++) {
		JsonNode *entry = json_array_get_element(links, i);
		JsonObject *link = JSON_NODE_HOLDS_OBJECT(entry) ? json_node_get_object(entry) : NULL;
		const gchar *url = link != NULL ? ls_string(link, "url") : NULL;
		if (link == NULL || !ls_object_keys(link, "|url|label|", NULL) || url == NULL ||
			!g_str_has_prefix(url, "https://") || strlen(url) <= 8 || strlen(url) > 300 ||
			strpbrk(url, " \t\r\n\"'<>\\`") != NULL || !g_uri_is_valid(url, G_URI_FLAGS_NONE, NULL) ||
			!text_member(link, "label", FALSE, NULL) ||
			(ls_string(link, "label") != NULL && g_utf8_strlen(ls_string(link, "label"), -1) > 60) ||
			!g_hash_table_add(seen, (gpointer)url))
			goto invalid;
	}
	return TRUE;
invalid:
	g_set_error_literal(error, LS_ERROR, LS_ERROR_INVALID,
		"site.social needs 1 to 12 unique {url, label} entries: url an https:// profile address without "
		"spaces or quotes, up to 300 characters; label optional text up to 60 characters");
	return FALSE;
}
/* site.form: the order's one live form. The builder draws exactly these
 * fields and posts them to action, a same-site path or an https:// address. */
static gboolean
form_field_valid(JsonObject *field, GHashTable *seen){
	static const gchar *const types[] = {"text", "email", "tel", "url", "number", "date", "textarea", NULL};
	const gchar *name = ls_string(field, "name");
	const gchar *type = ls_string(field, "type");
	JsonNode *required = json_object_get_member(field, "required");
	JsonNode *maxlength = json_object_get_member(field, "maxlength");
	if (!ls_object_keys(field, "|name|label|type|required|autocomplete|maxlength|hint|", NULL) ||
		name == NULL || *name == '\0' || strlen(name) > 40 ||
		strspn(name, "abcdefghijklmnopqrstuvwxyz0123456789_-") != strlen(name) ||
		!text_member(field, "label", TRUE, NULL) || g_utf8_strlen(ls_string(field, "label"), -1) > 80 ||
		!text_member(field, "hint", FALSE, NULL) ||
		(ls_string(field, "hint") != NULL && g_utf8_strlen(ls_string(field, "hint"), -1) > 200) ||
		!text_member(field, "autocomplete", FALSE, NULL) ||
		(ls_string(field, "autocomplete") != NULL && (strlen(ls_string(field, "autocomplete")) > 40 ||
		strspn(ls_string(field, "autocomplete"), "abcdefghijklmnopqrstuvwxyz- ") != strlen(ls_string(field, "autocomplete")))) ||
		(type != NULL && !g_strv_contains(types, type)) ||
		(required != NULL && json_node_get_value_type(required) != G_TYPE_BOOLEAN) ||
		(maxlength != NULL && (json_node_get_value_type(maxlength) != G_TYPE_INT64 ||
		json_node_get_int(maxlength) < 1 || json_node_get_int(maxlength) > 20000)) ||
		!g_hash_table_add(seen, (gpointer)name))
		return FALSE;
	return TRUE;
}
static gboolean
form_spec(JsonObject *site, GError **error){
	JsonNode *node = json_object_get_member(site, "form");
	JsonObject *form = node != NULL && JSON_NODE_HOLDS_OBJECT(node) ? json_node_get_object(node) : NULL;
	JsonArray *fields;
	const gchar *action, *trap;
	g_autoptr(GHashTable) seen = g_hash_table_new(g_str_hash, g_str_equal);
	guint i;
	if (form == NULL || !ls_object_keys(form, "|action|fields|honeypot|", NULL))
		goto invalid;
	action = ls_string(form, "action");
	if (!ls_form_action_valid(action))
		goto invalid;
	node = json_object_get_member(form, "fields");
	fields = node != NULL && JSON_NODE_HOLDS_ARRAY(node) ? json_node_get_array(node) : NULL;
	if (fields == NULL || json_array_get_length(fields) == 0 || json_array_get_length(fields) > 12)
		goto invalid;
	for (i = 0; i < json_array_get_length(fields); i++) {
		JsonNode *entry = json_array_get_element(fields, i);
		if (!JSON_NODE_HOLDS_OBJECT(entry) || !form_field_valid(json_node_get_object(entry), seen))
			goto invalid;
	}
	trap = ls_string(form, "honeypot");
	if (json_object_has_member(form, "honeypot") && (trap == NULL || *trap == '\0' || strlen(trap) > 40 ||
		strspn(trap, "abcdefghijklmnopqrstuvwxyz0123456789_-") != strlen(trap) || g_hash_table_contains(seen, trap)))
		goto invalid;
	return TRUE;
invalid:
	g_set_error_literal(error, LS_ERROR, LS_ERROR_INVALID,
		"site.form needs {action, fields, honeypot}: action a same-site path such as /api/forms/contact or an "
		"https:// address, without spaces, quotes, ? or #; fields 1 to 12 unique {name, label, type, required, "
		"autocomplete, maxlength, hint} with names of lowercase letters, digits, - or _ and type text, email, tel, "
		"url, number, date or textarea; honeypot an optional field name not among the fields");
	return FALSE;
}
/* site.modules: podbay modules the order supplies, each the snippet and
 * stylesheet podbay drew for it; both are drawn into pages as they are, so
 * both pass podbay's restricted rules here. */
static gboolean
module_list(JsonObject *site, GError **error){
	JsonNode *node = json_object_get_member(site, "modules");
	JsonArray *list = node != NULL && JSON_NODE_HOLDS_ARRAY(node) ? json_node_get_array(node) : NULL;
	g_autoptr(GHashTable) seen = g_hash_table_new(g_str_hash, g_str_equal);
	guint i;
	if (list == NULL || json_array_get_length(list) == 0 || json_array_get_length(list) > 8)
		goto invalid;
	for (i = 0; i < json_array_get_length(list); i++) {
		JsonNode *entry = json_array_get_element(list, i);
		JsonObject *module = JSON_NODE_HOLDS_OBJECT(entry) ? json_node_get_object(entry) : NULL;
		const gchar *id = module != NULL ? ls_string(module, "id") : NULL;
		if (module == NULL || !ls_object_keys(module, "|id|title|snippet|css|", NULL) || !ls_module_id_valid(id) ||
			!g_hash_table_add(seen, (gpointer)id) || ls_string(module, "snippet") == NULL ||
			ls_string(module, "css") == NULL || !text_member(module, "title", FALSE, NULL) ||
			(ls_string(module, "title") != NULL && g_utf8_strlen(ls_string(module, "title"), -1) > 80))
			goto invalid;
		if (!ls_module_check_snippet(id, ls_string(module, "snippet"), error) ||
			!ls_module_check_css(id, ls_string(module, "css"), error))
			return FALSE;
	}
	return TRUE;
invalid:
	g_set_error_literal(error, LS_ERROR, LS_ERROR_INVALID,
		"site.modules needs 1 to 8 supplied podbay modules {id, title, snippet, css}: unique ids of lowercase words "
		"joined by hyphens, the snippet and stylesheet podbay drew for each, and an optional title of up to 80 "
		"characters");
	return FALSE;
}
/* site.avoid: fingerprints of earlier sites, as written by the builder, so
 * this one is dealt a design that is not too close to any of them. */
static gboolean
avoid_list(JsonObject *site, GError **error){
	JsonNode *node = json_object_get_member(site, "avoid");
	JsonArray *list = node != NULL && JSON_NODE_HOLDS_ARRAY(node) ? json_node_get_array(node) : NULL;
	guint i;
	if (list == NULL || json_array_get_length(list) > 500)
		goto invalid;
	for (i = 0; i < json_array_get_length(list); i++) {
		JsonNode *entry = json_array_get_element(list, i);
		JsonObject *fp = JSON_NODE_HOLDS_OBJECT(entry) ? json_node_get_object(entry) : NULL;
		JsonNode *version = fp != NULL ? json_object_get_member(fp, "v") : NULL;
		if (fp == NULL || !ls_object_keys(fp, "|v|look|genes|colors|fonts|home|signature|", NULL) ||
			version == NULL || !JSON_NODE_HOLDS_VALUE(version) ||
			json_node_get_value_type(version) != G_TYPE_INT64 || json_node_get_int(version) != 1)
			goto invalid;
	}
	return TRUE;
invalid:
	g_set_error_literal(error, LS_ERROR, LS_ERROR_INVALID,
		"site.avoid needs up to 500 site fingerprints as the builder writes them: objects with v: 1 and only "
		"look, genes, colors, fonts, home and signature");
	return FALSE;
}

/* No synthesized prose: structured facts remain in the immutable recipe tree. */
static gboolean
validate_site(JsonObject *site, GError **error){
	const gchar *structured[] = {"pages", "offers", "contact", "brand", "copy_lock", "languages"};
	g_autoptr(GList) members = json_object_get_members(site);
	GList *item;
	guint i;
	if (!ls_object_keys(site,
		"|brief|audience|style|stack|frontend|backend|database|requirements|pages|offers|contact|brand|copy_lock|languages|"
		"photos|media|social|avoid|form|modules|credit|booking|",
		error))
		return FALSE;
	if (!json_object_has_member(site, "brief")) {
		for (i = 0; i < G_N_ELEMENTS(structured); i++)
			if (!json_object_has_member(site, structured[i])) {
				g_set_error(error, LS_ERROR, LS_ERROR_INVALID,
					"Structured site requires '%s' (legacy recipes may use brief)", structured[i]);
				return FALSE;
			}
	}
	for (item = members; item != NULL; item = item->next) {
		const gchar *key = (const gchar *)item->data;
		if (g_str_equal(key, "pages") || g_str_equal(key, "offers")) {
			if (!records(site, key, g_str_equal(key, "pages"), error))
				return FALSE;
		} else if (g_str_equal(key, "media")) {
			if (!ls_visual_check_media(site, error))
				return FALSE;
		} else if (g_str_equal(key, "photos")) {
			if (!photo_list(site, error))
				return FALSE;
		} else if (g_str_equal(key, "avoid")) {
			if (!avoid_list(site, error))
				return FALSE;
		} else if (g_str_equal(key, "social")) {
			if (!social_list(site, error))
				return FALSE;
		} else if (g_str_equal(key, "form")) {
			if (!form_spec(site, error))
				return FALSE;
		} else if (g_str_equal(key, "modules")) {
			if (!module_list(site, error))
				return FALSE;
		} else if (g_str_equal(key, "credit")) {
			/* A caller's line in every page footer, such as a host's "Built with". */
			JsonNode *node = json_object_get_member(site, key);
			if (!JSON_NODE_HOLDS_OBJECT(node) || !ls_credit_valid(json_node_get_object(node))) {
				g_set_error_literal(error, LS_ERROR, LS_ERROR_INVALID,
					"site.credit needs {text, url}: text one line of 1 to 80 characters and url an https:// "
					"address without spaces, quotes or angle brackets");
				return FALSE;
			}
		} else if (g_str_equal(key, "booking")) {
			/* Where the "Book online" action leads: a host's booking page. */
			JsonNode *node = json_object_get_member(site, key);
			if (!JSON_NODE_HOLDS_OBJECT(node) || !ls_booking_valid(json_node_get_object(node))) {
				g_set_error_literal(error, LS_ERROR, LS_ERROR_INVALID,
					"site.booking needs {href}: a same-site path such as /book or an https:// address, "
					"without spaces, quotes, angle brackets, ? or #");
				return FALSE;
			}
		} else if (g_str_equal(key, "copy_lock")) {
			if (!text_list(site, key, 0, error))
				return FALSE;
		} else if (g_str_equal(key, "contact") || g_str_equal(key, "brand")) {
			JsonObject *object;
			if (!object_member(site, key, error))
				return FALSE;
			object = json_object_get_object_member(site, key);
			if (!text_fields(object, g_str_equal(key, "contact") ?
				"|email|phone|address|url|" : "|name|tagline|tone|colors|fonts|logo|", error) ||
				(g_str_equal(key, "brand") && !text_member(object, "name", TRUE, error)))
				return FALSE;
			/* The builder links these: a call button and a mail link. */
			if (g_str_equal(key, "contact")) {
				g_autofree gchar *tel = ls_string(object, "phone") != NULL ? ls_phone_tel(ls_string(object, "phone")) : NULL;
				if (ls_string(object, "phone") != NULL && tel == NULL) {
					g_set_error_literal(error, LS_ERROR, LS_ERROR_INVALID,
						"contact.phone needs a phone number: digits, spaces, + . - ( ) only, 3 to 15 digits, "
						"at most 40 characters, + only before the first digit");
					return FALSE;
				}
				if (ls_string(object, "email") != NULL && !ls_email_valid(ls_string(object, "email"))) {
					g_set_error_literal(error, LS_ERROR, LS_ERROR_INVALID,
						"contact.email needs one plain address such as hello@example.com, without spaces, quotes or brackets");
					return FALSE;
				}
			}
		} else if (g_str_equal(key, "languages")) {
			JsonObject *object;
			JsonArray *supported;
			gboolean found = FALSE;
			g_autoptr(GHashTable) seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
			if (!object_member(site, key, error))
				return FALSE;
			object = json_object_get_object_member(site, key);
			if (!ls_object_keys(object, "|default|supported|", error) ||
				!text_member(object, "default", TRUE, error) || !text_list(object, "supported", 1, error))
				return FALSE;
			supported = json_object_get_array_member(object, "supported");
			for (i = 0; i < json_array_get_length(supported); i++) {
				const gchar *tag = json_array_get_string_element(supported, i);
				if (!language_valid(tag) || !g_hash_table_add(seen, g_ascii_strdown(tag, -1))) {
					g_set_error_literal(error, LS_ERROR, LS_ERROR_INVALID,
						"languages.supported needs unique language tags (for example en, fr-CA)");
					return FALSE;
				}
				found |= g_str_equal(tag, ls_string(object, "default"));
			}
			if (!found) {
				g_set_error_literal(error, LS_ERROR, LS_ERROR_INVALID,
					"languages.default must occur exactly in languages.supported");
				return FALSE;
			}
		} else if (!text_member(site, key, TRUE, error))
			return FALSE;
	}
	return TRUE;
}
/* Structural validation happens before provider creation or output writes. */
LsRecipe *
ls_recipe_new(const gchar *yaml, GError **error){
	g_autoptr(LsRecipe) self = g_object_new(LS_TYPE_RECIPE, NULL);
	g_autoptr(GHashTable) ids = g_hash_table_new(g_str_hash, g_str_equal);
	JsonObject *root, *site, *pipeline;
	JsonNode *node;
	JsonArray *steps;
	guint i;
	self->root = ls_yaml_parse(yaml, error);
	if (self->root == NULL)
		return NULL;
	if (!JSON_NODE_HOLDS_OBJECT(self->root))
		goto invalid_structure;
	root = json_node_get_object(self->root);
	if (!ls_object_keys(root, "|version|name|site|ai|pipeline|seed_artifacts|package|", error) ||
		!text_member(root, "name", TRUE, error) || !object_member(root, "site", error) ||
		!object_member(root, "pipeline", error))
		return NULL;
	node = json_object_get_member(root, "version");
	if (node == NULL || json_node_get_value_type(node) != G_TYPE_INT64 ||
		json_node_get_int(node) != 1) {
		g_set_error_literal(error, LS_ERROR, LS_ERROR_INVALID, "Recipe version must be integer 1");
		return NULL;
	}
	site = json_object_get_object_member(root, "site");
	if (!validate_site(site, error))
		return NULL;
	/* Sold applications have one supported stack. Other generators belong at
	 * the plugin boundary, not in the application SKU's support contract. */
	if (json_object_has_member(root, "package")) {
		JsonObject *package;
		const gchar *kind;
		if (!object_member(root, "package", error))
			return NULL;
		package = json_object_get_object_member(root, "package");
		if (!ls_object_keys(package, "|sku|release|kind|", error) ||
			!text_member(package, "sku", TRUE, error) ||
			!text_member(package, "release", TRUE, error) ||
			!text_member(package, "kind", TRUE, error))
			return NULL;
		kind = ls_string(package, "kind");
		if (!g_regex_match_simple("^[a-z][a-z0-9-]{0,63}\\z", ls_string(package, "sku"), 0, 0) ||
			!g_regex_match_simple("^[0-9]+\\.[0-9]+\\.[0-9]+\\z", ls_string(package, "release"), 0, 0) ||
			(!g_str_equal(kind, "site") && !g_str_equal(kind, "application")) ||
			(g_str_equal(ls_string(package, "sku"), "application") && !g_str_equal(kind, "application")) ||
			(g_str_equal(kind, "application") && g_strcmp0(ls_string(site, "stack"), "c-glib") != 0) ||
			(g_str_equal(kind, "site") && ls_string(site, "stack") != NULL)) {
			g_set_error_literal(error, LS_ERROR, LS_ERROR_INVALID,
				"package requires a lowercase SKU, numeric major.minor.patch release, and kind site "
				"(no stack) or application (stack c-glib)");
			return NULL;
		}
	}
	if (json_object_has_member(root, "ai") &&
		(!object_member(root, "ai", error) ||
		 !validate_ai(json_object_get_object_member(root, "ai"), error)))
		return NULL;
	pipeline = json_object_get_object_member(root, "pipeline");
	if (!ls_object_keys(pipeline, "|steps|", error))
		return NULL;
	node = json_object_get_member(pipeline, "steps");
	if (node == NULL || !JSON_NODE_HOLDS_ARRAY(node))
		goto invalid_structure;
	steps = json_node_get_array(node);
	if (json_array_get_length(steps) == 0 || json_array_get_length(steps) > 128)
		goto invalid_structure;
	for (i = 0; i < json_array_get_length(steps); i++) {
		JsonObject *step;
		const gchar *id;
		node = json_array_get_element(steps, i);
		if (!JSON_NODE_HOLDS_OBJECT(node))
			goto invalid_structure;
		step = json_node_get_object(node);
		if (!ls_object_keys(step, "|id|type|prompt|instructions|ai|options|", error) ||
			!text_member(step, "id", TRUE, error) || !text_member(step, "type", TRUE, error) ||
			!text_member(step, "prompt", FALSE, error) ||
			!text_member(step, "instructions", FALSE, error))
			return NULL;
		id = ls_string(step, "id");
		if (!g_hash_table_add(ids, (gpointer)id)) {
			g_set_error(error, LS_ERROR, LS_ERROR_INVALID, "Duplicate step id '%s'", id);
			return NULL;
		}
		if (json_object_has_member(step, "ai") &&
			(!object_member(step, "ai", error) ||
			 !validate_ai(json_object_get_object_member(step, "ai"), error)))
			return NULL;
		if (json_object_has_member(step, "options") && !object_member(step, "options", error))
			return NULL;
	}
	/* A sold scope ends with actual native acceptance, not a model's review.
	 * Trailing prune steps may follow: they remove build notes and cannot add
	 * content. The pipeline validates the complete predicate schema later. */
	if (json_object_has_member(root, "package")) {
		guint tail = json_array_get_length(steps);
		JsonObject *last;
		JsonObject *options;
		JsonNode *checks;
		while (tail > 1 && g_strcmp0(ls_string(
				json_array_get_object_element(steps, tail - 1), "type"), "prune") == 0)
			tail--;
		last = json_array_get_object_element(steps, tail - 1);
		options = json_object_has_member(last, "options")
			? json_object_get_object_member(last, "options") : NULL;
		checks = options != NULL ? json_object_get_member(options, "checks") : NULL;
		if (g_strcmp0(ls_string(last, "type"), "validate") != 0 || checks == NULL ||
			!JSON_NODE_HOLDS_ARRAY(checks) || json_array_get_length(json_node_get_array(checks)) == 0) {
			g_set_error_literal(error, LS_ERROR, LS_ERROR_INVALID,
				"Sold package must end with native validate acceptance with nonempty checks, followed only by prune steps");
			return NULL;
		}
	}
	/* Reuse the artifact transaction validator, including path/size conflicts,
	 * before callers can create a workspace or contact a provider. */
	{
		g_autoptr(LsContext) seeds = ls_recipe_create_context(self, error);
		if (seeds == NULL)
			return NULL;
	}
	/* Retain supplied bytes instead of depending on future YAML serializers. */
	self->source = g_strdup(yaml);
	return (LsRecipe *)g_steal_pointer(&self);
invalid_structure:
	g_set_error_literal(
		error,
		LS_ERROR,
		LS_ERROR_INVALID,
		"Expected a recipe mapping and pipeline.steps with 1..128 step mappings"
	);
	return NULL;
}
/* Build independent initial artifacts without mutating the recipe tree. */
LsContext *
ls_recipe_create_context(LsRecipe *self, GError **error){
	g_autoptr(LsContext) context = ls_context_new();
	if (!ls_recipe_seed_context(self, context, error))
		return NULL;
	return (LsContext *)g_steal_pointer(&context);
}

/* Explicit recipe seeds overlay imported source once, before any execution. */
gboolean
ls_recipe_seed_context(LsRecipe *self, LsContext *context, GError **error){
	JsonNode *seeds = json_object_get_member(ls_recipe_get_root(self), "seed_artifacts");
	if (seeds != NULL) {
		g_autoptr(JsonNode) bundle = json_node_new(JSON_NODE_OBJECT);
		g_autoptr(JsonObject) object = json_object_new();
		g_autofree gchar *text = NULL;
		/* Copy the node so the parsed recipe remains the checkpoint identity. */
		json_object_set_member(object, "files", json_node_copy(seeds));
		json_node_set_object(bundle, object);
		text = ls_json_encode(bundle);
		if (!ls_context_apply(context, text, error)) {
			g_prefix_error(error, "seed_artifacts: ");
			return FALSE;
		}
	}
	return TRUE;
}

/* Accessors expose borrowed nodes; recipes are immutable by contract. */
JsonObject *
ls_recipe_get_root(LsRecipe *self){
	return json_node_get_object(self->root);
}
const gchar *
ls_recipe_get_source(LsRecipe *self){
	return self->source;
}
JsonArray *
ls_recipe_get_steps(LsRecipe *self){
	return json_object_get_array_member(
		json_object_get_object_member(ls_recipe_get_root(self), "pipeline"), "steps"
	);
}
/* Resolve independently per step to avoid settings leaking between providers. */
JsonObject *
ls_recipe_resolve_ai(LsRecipe *self, JsonObject *step){
	JsonObject *result = json_object_new();
	JsonObject *sources[2];
	guint i;
	json_object_set_string_member(result, "provider", "codex");
	json_object_set_int_member(result, "timeout", 600);
	json_object_set_int_member(result, "retries", 1);
	json_object_set_int_member(result, "max_tokens", 16384);
	sources[0] = json_object_has_member(ls_recipe_get_root(self), "ai")
					 ? json_object_get_object_member(ls_recipe_get_root(self), "ai")
					 : NULL;
	sources[1] =
		json_object_has_member(step, "ai") ? json_object_get_object_member(step, "ai") : NULL;
	for (i = 0; i < 2; i++) {
		g_autoptr(GList) members = sources[i] != NULL ? json_object_get_members(sources[i]) : NULL;
		GList *item;
		/* A changed provider uses its own default model unless explicitly set. */
		if (i == 1 && sources[i] != NULL && json_object_has_member(sources[i], "provider") &&
			g_strcmp0(ls_string(sources[i], "provider"), ls_string(result, "provider")) != 0) {
			json_object_remove_member(result, "model");
			json_object_remove_member(result, "effort");
		}
		for (item = members; item != NULL; item = item->next)
			json_object_set_member(
				result,
				(const gchar *)item->data,
				json_node_copy(json_object_get_member(sources[i], (const gchar *)item->data))
			);
	}
	/* A run-wide provider replaces every step's choice, with the same model
	 * reset as a step-level provider change. */
	if (self->provider != NULL) {
		if (!g_str_equal(ls_string(result, "provider"), self->provider)) {
			json_object_remove_member(result, "model");
			json_object_remove_member(result, "effort");
			json_object_set_string_member(result, "provider", self->provider);
		}
		if (self->model != NULL)
			json_object_set_string_member(result, "model", self->model);
	}
	if (self->timeout >= 0)
		json_object_set_int_member(result, "timeout", self->timeout);
	if (self->unlimited) {
		json_object_set_int_member(result, "timeout", 0);
		json_object_set_int_member(result, "max_tokens", 0);
	}
	return result;
}
/* Execution overrides must never mutate the fingerprinted YAML tree. */
void
ls_recipe_set_timeout(LsRecipe *self, gint seconds){
	g_return_if_fail(LS_IS_RECIPE(self));
	g_return_if_fail(seconds >= -1 && seconds <= 86400);
	self->timeout = seconds;
}
/* Testing budgets are execution settings and leave saved recipe identity intact. */
void
ls_recipe_set_unlimited(LsRecipe *self, gboolean unlimited){
	g_return_if_fail(LS_IS_RECIPE(self));
	self->unlimited = unlimited;
}

/* Provider choice is an execution setting, like the deadline: it never
 * enters the fingerprinted tree, so a run can resume on another agent. */
gboolean
ls_recipe_set_provider(LsRecipe *self, const gchar *provider, const gchar *model, GError **error){
	g_return_val_if_fail(LS_IS_RECIPE(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);
	if (provider != NULL && !provider_supported(provider)) {
		g_set_error(error, LS_ERROR, LS_ERROR_INVALID, "Unsupported CLI provider '%s'", provider);
		return FALSE;
	}
	if (model != NULL && (provider == NULL || *model == '\0' || !g_utf8_validate(model, -1, NULL) ||
			strpbrk(model, "\r\n\t") != NULL)) {
		g_set_error_literal(error, LS_ERROR, LS_ERROR_INVALID,
			"A model override needs a provider override and one line of text");
		return FALSE;
	}
	g_free(self->provider);
	g_free(self->model);
	self->provider = g_strdup(provider);
	self->model = g_strdup(model);
	return TRUE;
}

/* Gate approvals are CLI/library run flags, never recipe or catalog bytes. */
void
ls_recipe_approve(LsRecipe *self, const gchar *step_id){
	g_return_if_fail(LS_IS_RECIPE(self));
	g_return_if_fail(step_id != NULL && *step_id != '\0');
	g_hash_table_add(self->approvals, g_strdup(step_id));
}

void
ls_recipe_set_approve_all(LsRecipe *self, gboolean approve_all){
	g_return_if_fail(LS_IS_RECIPE(self));
	self->approve_all = approve_all;
}

gboolean
ls_recipe_is_approved(LsRecipe *self, const gchar *step_id){
	g_return_val_if_fail(LS_IS_RECIPE(self), FALSE);
	g_return_val_if_fail(step_id != NULL, FALSE);
	return self->approve_all || g_hash_table_contains(self->approvals, step_id);
}

/* Quote strings explicitly: prices, phone numbers, copy and null-like words
 * must not turn into booleans/numbers when another YAML reader opens the form. */
static void
quote_strings(YamlNode *yaml, JsonNode *json){
	if (JSON_NODE_HOLDS_OBJECT(json)) {
		JsonObject *object = json_node_get_object(json);
		g_autoptr(GList) members = json_object_get_members(object);
		GList *item;
		for (item = members; item != NULL; item = item->next) {
			const gchar *key = (const gchar *)item->data;
			quote_strings(yaml_mapping_get_member(yaml_node_get_mapping(yaml), key),
				json_object_get_member(object, key));
		}
	} else if (JSON_NODE_HOLDS_ARRAY(json)) {
		JsonArray *array = json_node_get_array(json);
		guint i;
		for (i = 0; i < json_array_get_length(array); i++)
			quote_strings(yaml_sequence_get_element(yaml_node_get_sequence(yaml), i),
				json_array_get_element(array, i));
	} else if (json_node_get_value_type(json) == G_TYPE_STRING)
		yaml_node_set_scalar_style(yaml, YAML_SCALAR_STYLE_DOUBLE_QUOTED);
}
/* Emission is data-only and deliberately excludes execution budget overrides. */
gchar *
ls_recipe_to_yaml(LsRecipe *self, GError **error){
	g_autoptr(YamlNode) node = yaml_node_from_json_node(self->root);
	g_autoptr(YamlGenerator) generator = yaml_generator_new();
	quote_strings(node, self->root);
	yaml_generator_set_root(generator, node);
	yaml_generator_set_indent(generator, 2);
	yaml_generator_set_unicode(generator, TRUE);
	return yaml_generator_to_data(generator, NULL, error);
}
/* Form clients supply facts, not generated prose or pipeline boilerplate.
 * JSON is the existing library transport; the durable result is YAML. */
LsRecipe *
ls_recipe_from_form(const gchar *json, GError **error){
	g_autoptr(JsonNode) node = ls_json_parse(json, error);
	g_autoptr(JsonNode) defaults = NULL;
	g_autofree gchar *encoded = NULL;
	JsonObject *root;
	JsonObject *site;
	if (node == NULL)
		return NULL;
	if (!JSON_NODE_HOLDS_OBJECT(node)) {
		g_set_error_literal(error, LS_ERROR, LS_ERROR_INVALID, "Form must be an object");
		return NULL;
	}
	root = json_node_get_object(node);
	if (!ls_object_keys(root, "|name|site|ai|pipeline|seed_artifacts|", error) ||
		!object_member(root, "site", error))
		return NULL;
	site = json_object_get_object_member(root, "site");
	if (json_object_has_member(site, "brief")) {
		g_set_error_literal(error, LS_ERROR, LS_ERROR_INVALID,
			"Form requires structured site fields, not site.brief");
		return NULL;
	}
	json_object_set_int_member(root, "version", 1);
	if (!json_object_has_member(root, "pipeline")) {
		/* Static-site defaults can be replaced with the normal pipeline field. */
		defaults = ls_yaml_parse(
			"steps:\n"
			"  - {id: scaffold, type: scaffold}\n"
			"  - {id: product, type: brief}\n"
			"  - {id: design, type: design}\n"
			"  - id: design-gate\n"
			"    type: gate\n"
			"    options:\n"
			"      message: Review docs/product.org and docs/design.org before implementing the frontend.\n"
			"      review: [docs/product.org, docs/design.org]\n"
			"  - {id: frontend, type: frontend}\n"
			"  - {id: review, type: review}\n"
			"  - id: acceptance\n"
			"    type: validate\n"
			"    options:\n"
			"      required: [index.html, 404.html, privacy.html, terms.html, robots.txt]\n"
			/* Design notes steer generation but are not delivered. */
			"  - id: build-notes\n"
			"    type: prune\n"
			"    options:\n"
			"      paths: [docs/product.org, docs/design.org, docs/polish.org, docs/review.org]\n",
			error);
		if (defaults == NULL)
			return NULL;
		json_object_set_member(root, "pipeline", g_steal_pointer(&defaults));
	}
	/* JSON is a YAML subset; use the one recipe boundary for schema checks. */
	encoded = ls_json_encode(node);
	return ls_recipe_new(encoded, error);
}
/* Preserve a validated native bundle independently of mutable embedded data.
 * This is an input pin, not an execution override: it participates in resume. */
gboolean
ls_recipe_set_scaffold_source(LsRecipe *self, const gchar *bundle, GError **error){
	g_autoptr(LsContext) context = ls_context_new();
	g_autoptr(JsonNode) node = NULL;
	gchar *copy;
	if (bundle != NULL) {
		node = ls_json_parse(bundle, error);
		if (node == NULL)
			return FALSE;
		if (!JSON_NODE_HOLDS_OBJECT(node) ||
			!ls_object_keys(json_node_get_object(node), "|files|", error)) {
			if (error != NULL && *error == NULL)
				g_set_error_literal(error, LS_ERROR, LS_ERROR_INVALID,
					"Archived scaffold must be a files bundle");
			return FALSE;
		}
		if (!ls_context_apply(context, bundle, error))
			return FALSE;
	}
	copy = g_strdup(bundle);
	g_free(self->scaffold_source);
	self->scaffold_source = copy;
	return TRUE;
}

/* Return exact bytes: reserialization must not change checkpoint identity. */
const gchar *
ls_recipe_get_scaffold_source(LsRecipe *self){
	return self->scaffold_source;
}

/* Like the scaffold pin: an input, so it binds checkpoints and never mixes
 * with newer embedded components. */
gboolean
ls_recipe_set_components_source(LsRecipe *self, const gchar *snapshot, GError **error){
	gchar *copy;
	if (snapshot != NULL) {
		g_autoptr(LsComponents) components = ls_components_new_from_snapshot(snapshot, error);
		if (components == NULL)
			return FALSE;
	}
	copy = g_strdup(snapshot);
	g_free(self->components_source);
	self->components_source = copy;
	return TRUE;
}

const gchar *
ls_recipe_get_components_source(LsRecipe *self){
	return self->components_source;
}

LsComponents *
ls_recipe_new_components(LsRecipe *self, GError **error){
	return ls_components_new_from_snapshot(self->components_source, error);
}
