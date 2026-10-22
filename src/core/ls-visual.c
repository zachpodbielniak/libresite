/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "core/ls-visual.h"
#include "core/ls-util.h"
#include <string.h>

static JsonObject *
obj(JsonObject *root, const gchar *key){
	JsonNode *n = root != NULL ? json_object_get_member(root, key) : NULL;
	return n != NULL && JSON_NODE_HOLDS_OBJECT(n) ? json_node_get_object(n) : NULL;
}
static JsonArray *
arr(JsonObject *root, const gchar *key){
	JsonNode *n = root != NULL ? json_object_get_member(root, key) : NULL;
	return n != NULL && JSON_NODE_HOLDS_ARRAY(n) ? json_node_get_array(n) : NULL;
}
static gboolean
fail(GError **error, const gchar *message){
	g_set_error_literal(error, LS_ERROR, LS_ERROR_INVALID, message);
	return FALSE;
}
static gboolean
text_ok(JsonObject *o, const gchar *key, guint max){
	const gchar *s = o != NULL ? ls_string(o, key) : NULL;
	return s != NULL && *s != '\0' && g_utf8_strlen(s, -1) <= max && strspn(s, " \t\r\n") != strlen(s);
}
static JsonObject *
photo(JsonObject *root, const gchar *path){
	JsonArray *photos = arr(root, "photos");
	guint i;
	for (i = 0; path != NULL && photos != NULL && i < json_array_get_length(photos); i++) {
		JsonNode *n = json_array_get_element(photos, i);
		JsonObject *p = JSON_NODE_HOLDS_OBJECT(n) ? json_node_get_object(n) : NULL;
		if (p != NULL && g_strcmp0(ls_string(p, "path"), path) == 0)
			return p;
	}
	return NULL;
}

gboolean
ls_visual_check_media(JsonObject *owner, GError **error){
	JsonObject *m = obj(owner, "media");
	const gchar *roles[] = {"hero", "portrait", "detail", "proof"};
	const gchar *clips[] = {"clip", "clip_portrait"};
	const gchar *kinds[] = {"project", "stock", "illustration", NULL};
	JsonArray *facts;
	guint i;
	if (!json_object_has_member(owner, "media"))
		return TRUE;
	if (m == NULL || !ls_object_keys(m, "|hero|portrait|detail|proof|clip|clip_portrait|kind|source|facts|", NULL) ||
		!text_ok(m, "source", 300) || ls_string(m, "kind") == NULL || !g_strv_contains(kinds, ls_string(m, "kind")))
		return fail(error, "media needs an asset source and kind: project, stock or illustration");
	for (i = 0; i < G_N_ELEMENTS(roles); i++)
		if ((i < 3 || json_object_has_member(m, roles[i])) && photo(owner, ls_string(m, roles[i])) == NULL)
			return fail(error, "media hero, portrait, detail and optional proof must name supplied photos");
	for (i = 0; i < G_N_ELEMENTS(clips); i++) {
		const gchar *p = ls_string(m, clips[i]);
		if (!json_object_has_member(m, clips[i]))
			continue;
		if (p == NULL || !g_str_has_prefix(p, "assets/") || strstr(p, "..") != NULL ||
			strspn(p, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789/_-.") != strlen(p) ||
			!(g_str_has_suffix(p, ".mp4") || g_str_has_suffix(p, ".webm")))
			return fail(error, "media clips must be local assets/ MP4 or WebM files");
	}
	if (json_object_has_member(m, "clip_portrait") && !json_object_has_member(m, "clip"))
		return fail(error, "a portrait clip needs a landscape clip");
	facts = arr(m, "facts");
	if (json_object_has_member(m, "facts") && (facts == NULL || json_array_get_length(facts) > 3 ||
		g_strcmp0(ls_string(m, "kind"), "project") != 0 || !json_object_has_member(m, "proof")))
		return fail(error, "up to three attributable facts belong only to a supplied proof project");
	for (i = 0; facts != NULL && i < json_array_get_length(facts); i++) {
		JsonNode *n = json_array_get_element(facts, i);
		JsonObject *f = JSON_NODE_HOLDS_OBJECT(n) ? json_node_get_object(n) : NULL;
		if (f == NULL || !ls_object_keys(f, "|value|label|source|", NULL) || !text_ok(f, "value", 60) ||
			!text_ok(f, "label", 100) || !text_ok(f, "source", 300))
			return fail(error, "every project fact needs value, label and source");
	}
	return TRUE;
}

gboolean
ls_visual_check_assets(JsonObject *owner, LsContext *context, GError **error){
	JsonObject *m = obj(owner, "media");
	const gchar *keys[] = {"clip", "clip_portrait"};
	guint i;
	if (!ls_visual_check_media(owner, error))
		return FALSE;
	for (i = 0; m != NULL && context != NULL && i < G_N_ELEMENTS(keys); i++) {
		const gchar *path = ls_string(m, keys[i]);
		const LsArtifact *artifact;
		const guint8 *bytes;
		gsize size = 0;
		if (path == NULL)
			continue;
		artifact = ls_context_lookup(context, path);
		bytes = artifact != NULL && ls_artifact_is_binary(artifact) ? ls_artifact_get_data(artifact, &size) : NULL;
		if (bytes == NULL || size < 12 || size > 2 * 1024 * 1024 ||
			(g_str_has_suffix(path, ".mp4") ? memcmp(bytes + 4, "ftyp", 4) != 0 :
			memcmp(bytes, "\x1a\x45\xdf\xa3", 4) != 0))
			return fail(error, "media clip must be a seeded MP4/WebM binary of at most 2 MiB");
	}
	return TRUE;
}

gboolean
ls_visual_check_plan(JsonObject *plan, GError **error){
	JsonObject *v = obj(plan, "visual");
	const gchar *worlds[] = {"chalk", "copper", "forest", "electric", "ceramic", "oven", NULL};
	const gchar *layouts[] = {"full", "split", "object", NULL};
	const gchar *headings[] = {"headline", "craft_heading", "proof_heading", "close_heading"};
	const gchar *copy[] = {"intro", "craft_copy", "proof_copy"};
	guint i;
	if (!ls_visual_check_media(plan, error))
		return FALSE;
	if (!json_object_has_member(plan, "visual"))
		return obj(plan, "media") == NULL ? TRUE : fail(error, "supplied media needs a visual sequence plan");
	if (v == NULL || obj(plan, "media") == NULL)
		return fail(error, "visual must be an object backed by supplied media");
	if (!ls_object_keys(v,
		"|idea|world|layout|headline|intro|craft_heading|craft_copy|proof_heading|proof_copy|close_heading|", NULL))
		return fail(error, "visual has an unknown field; use only the visual schema properties");
	if (!text_ok(v, "idea", 100))
		return fail(error, "visual.idea must be a nonempty internal note of at most 100 characters");
	if (ls_string(v, "world") == NULL || !g_strv_contains(worlds, ls_string(v, "world")))
		return fail(error, "visual.world must be chalk, copper, forest, electric, ceramic or oven");
	if (ls_string(v, "layout") == NULL || !g_strv_contains(layouts, ls_string(v, "layout")))
		return fail(error, "visual.layout must be full, split or object");
	/* Full-height phone scenes need a deliberate portrait source. Otherwise
	 * the general crop guard correctly mats a wide image, leaving a void.
	 * The planner can choose split/object when no portrait was supplied. */
	if (g_strcmp0(ls_string(v, "layout"), "full") == 0) {
		JsonObject *p = photo(plan, ls_string(obj(plan, "media"), "portrait"));
		JsonNode *w = p != NULL ? json_object_get_member(p, "width") : NULL;
		JsonNode *h = p != NULL ? json_object_get_member(p, "height") : NULL;
		if (w == NULL || h == NULL || !JSON_NODE_HOLDS_VALUE(w) || !JSON_NODE_HOLDS_VALUE(h) ||
			(json_node_get_value_type(w) != G_TYPE_INT64 && json_node_get_value_type(w) != G_TYPE_DOUBLE) ||
			(json_node_get_value_type(h) != G_TYPE_INT64 && json_node_get_value_type(h) != G_TYPE_DOUBLE) ||
			json_node_get_double(h) <= 0 || json_node_get_double(w) <= 0 ||
			json_node_get_double(w) / json_node_get_double(h) > 1.2)
			return fail(error, "visual.layout full needs a supplied portrait crop with width/height <=1.2; choose split or object when no suitable crop exists");
	}
	for (i = 0; i < G_N_ELEMENTS(headings); i++) {
		const gchar *s = ls_string(v, headings[i]);
		g_auto(GStrv) words = s != NULL ? g_strsplit_set(s, " \t\r\n", -1) : NULL;
		guint j, count = 0;
		gboolean long_word = FALSE;
		for (j = 0; words != NULL && words[j] != NULL; j++) {
			if (*words[j] != '\0') count++;
			if (g_utf8_strlen(words[j], -1) > 18) long_word = TRUE;
		}
		if (!text_ok(v, headings[i], 52) || count > 8 || long_word) {
			g_set_error(error, LS_ERROR, LS_ERROR_INVALID,
				"visual.%s needs 1-8 words, at most 52 characters and no word over 18 characters; rewrite the complete thought",
				headings[i]);
			return FALSE;
		}
	}
	for (i = 0; i < G_N_ELEMENTS(copy); i++)
		if (!text_ok(v, copy[i], 180)) {
			g_set_error(error, LS_ERROR, LS_ERROR_INVALID,
				"visual.%s needs one nonempty sentence of at most 180 characters", copy[i]);
			return FALSE;
		}
	return TRUE;
}

void
ls_visual_apply_plan(JsonObject *plan){
	JsonObject *v = obj(plan, "visual"), *tokens = obj(plan, "tokens"), *look = obj(plan, "look");
	const gchar *worlds[] = {"chalk", "copper", "forest", "electric", "ceramic", "oven"};
	const gchar *faces[] = {"Playfair Display", "Space Grotesk", "Fraunces", "Bricolage Grotesque", "Manrope", "DM Serif Display"};
	const gchar *palette[][5] = {
		{"#f3f0e9", "#202222", "#e8e4db", "#3a5365", "#ffffff"},
		{"#171b1d", "#f4eee4", "#23292b", "#edac75", "#171b1d"},
		{"#eff0e6", "#20362c", "#e1e6d7", "#315a3b", "#ffffff"},
		{"#111722", "#f3f5fa", "#202938", "#b8f34b", "#111722"},
		{"#f2f6f7", "#19383c", "#e0ecee", "#18646c", "#ffffff"},
		{"#f7eedc", "#3b271e", "#ebdbc1", "#62483e", "#ffffff"}
	};
	guint i;
	if (v == NULL || tokens == NULL || !ls_visual_check_plan(plan, NULL))
		return;
	for (i = 0; i < G_N_ELEMENTS(worlds); i++) {
		if (g_strcmp0(ls_string(v, "world"), worlds[i]) != 0)
			continue;
		json_object_set_string_member(tokens, "paper", palette[i][0]);
		json_object_set_string_member(tokens, "ink", palette[i][1]);
		json_object_set_string_member(tokens, "muted", palette[i][1]);
		json_object_set_string_member(tokens, "surface", palette[i][2]);
		json_object_set_string_member(tokens, "accent", palette[i][3]);
		json_object_set_string_member(tokens, "accent_ink", palette[i][4]);
		if (arr(plan, "fonts") != NULL && json_array_get_length(arr(plan, "fonts")) > 0) {
			JsonArray *fonts = json_array_new();
			JsonObject *face = json_object_new();
			JsonArray *weights = json_array_new();
			g_autofree gchar *stack = g_strdup_printf("'%s', sans-serif", faces[i]);
			json_array_add_int_element(weights, 400);
			json_object_set_string_member(face, "family", faces[i]);
			json_object_set_array_member(face, "weights", weights);
			json_array_add_object_element(fonts, face);
			json_object_set_array_member(plan, "fonts", fonts);
			json_object_set_string_member(tokens, "font_display", stack);
			json_object_set_string_member(tokens, "font_body", "system-ui, sans-serif");
			json_object_set_int_member(tokens, "heading_weight", 400);
		}
	}
	if (look != NULL) {
		json_object_set_string_member(look, "scheme", "fixed");
		json_object_set_string_member(look, "texture", "none");
		json_object_set_string_member(look, "header", "bar");
		json_object_set_string_member(look, "nav", "underline");
		json_object_set_string_member(look, "frame", "wide");
		json_object_set_string_member(look, "heads", "plain");
		json_object_set_string_member(look, "lettering", "plain");
		json_object_set_string_member(look, "buttons", "square");
		json_object_set_string_member(look, "edges", "straight");
		json_object_set_string_member(look, "photos", "natural");
	}
	/* A visual sequence owns the flourishes. All navigation destinations
	 * remain available: the existing menu handles smaller screens. */
	json_object_remove_member(plan, "signature");
}

static JsonObject *
image_slot(JsonObject *plan, const gchar *path){
	JsonObject *p = photo(plan, path), *out = json_object_new();
	json_object_set_string_member(out, "src", path);
	json_object_set_string_member(out, "alt", ls_string(p, "alt"));
	return out;
}

void
ls_visual_apply_page(JsonObject *plan, JsonObject *page){
	JsonObject *v = obj(plan, "visual"), *m = obj(plan, "media");
	JsonArray *old = arr(page, "sections"), *sections;
	const gchar *roles[] = {"hero", "detail", "proof"};
	const gchar *heads[] = {"headline", "craft_heading", "proof_heading"};
	const gchar *copy[] = {"intro", "craft_copy", "proof_copy"};
	const gchar *beats[] = {"outcome", "craft", "proof"};
	guint i, start = 1;
	if (v == NULL || old == NULL || !ls_visual_check_plan(plan, NULL) ||
		g_strcmp0(ls_string(page, "path"), "index.html") != 0)
		return;
	if (json_array_get_length(old) > 0 && g_strcmp0(ls_string(json_array_get_object_element(old, 0), "component"), "scene") == 0)
		while (start < json_array_get_length(old) && start < 3 &&
			g_strcmp0(ls_string(json_array_get_object_element(old, start), "component"), "material") == 0)
			start++;
	sections = json_array_new();
	for (i = 0; i < 3; i++) {
		JsonObject *section, *content;
		const gchar *path = ls_string(m, roles[i]);
		if (path == NULL)
			continue;
		section = json_object_new(); content = json_object_new();
		json_object_set_string_member(section, "component", i == 0 ? "scene" : "material");
		json_object_set_string_member(section, "variant", i == 0 ? ls_string(v, "layout") : i == 1 ? "detail" : "proof");
		json_object_set_string_member(section, "id", beats[i]);
		json_object_set_string_member(content, "heading", ls_string(v, heads[i]));
		json_object_set_string_member(content, "body", ls_string(v, copy[i]));

		json_object_set_object_member(content, "art", image_slot(plan, path));
		if (i == 0) {
			JsonObject *action = obj(plan, "action");
			JsonNode *first = json_array_get_length(old) > 0 ? json_array_get_element(old, 0) : NULL;
			JsonArray *prior = first != NULL && JSON_NODE_HOLDS_OBJECT(first)
				? arr(obj(json_node_get_object(first), "content"), "actions") : NULL;
			/* Replacing the opening must retain its primary action when
			 * there is no site-wide action. The normal page check validates it. */
			if (action == NULL && prior != NULL && json_array_get_length(prior) > 0) {
				JsonNode *link = json_array_get_element(prior, 0);
				if (JSON_NODE_HOLDS_OBJECT(link))
					action = json_node_get_object(link);
			}
			json_object_set_object_member(content, "portrait", image_slot(plan, ls_string(m, "portrait")));
			if (action != NULL) {
				JsonArray *actions = json_array_new();
				json_array_add_object_element(actions, json_object_ref(action));
				json_object_set_array_member(content, "actions", actions);
			}
		}
		json_object_set_object_member(section, "content", content);
		json_array_add_object_element(sections, section);
	}
	/* Preserve the writer's services, answers, contact and anchors after
	 * the three beats. Skip only its superseded home opening. */
	for (i = start; i < json_array_get_length(old); i++)
		{
			JsonNode *copy_node = json_node_copy(json_array_get_element(old, i));
			JsonObject *section = json_node_get_object(copy_node);
			if (g_strcmp0(ls_string(section, "component"), "cta") == 0 && obj(section, "content") != NULL)
				json_object_set_string_member(obj(section, "content"), "heading", ls_string(v, "close_heading"));
			json_array_add_element(sections, copy_node);
		}
	json_object_set_array_member(page, "sections", sections);
}

void
ls_visual_view(JsonObject *plan, JsonObject *view, guint index){
	JsonObject *m = obj(plan, "media");
	if (m == NULL)
		return;
	json_object_set_string_member(view, "source", ls_string(m, "source"));
	json_object_set_string_member(view, "media_kind", ls_string(m, "kind"));
	if (index == 0 && ls_string(m, "clip") != NULL) {
		json_object_set_string_member(view, "clip", ls_string(m, "clip"));
		if (ls_string(m, "clip_portrait") != NULL)
			json_object_set_string_member(view, "clip_portrait", ls_string(m, "clip_portrait"));
	}
	if (index == 2 && arr(m, "facts") != NULL)
		json_object_set_array_member(view, "facts", json_array_ref(arr(m, "facts")));
}
