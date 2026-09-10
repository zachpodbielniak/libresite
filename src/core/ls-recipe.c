/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "core/ls-recipe.h"
#include "core/ls-context.h"
#include <string.h>
struct _LsRecipe {
	GObject parent_instance;
	JsonNode *root;
	gint timeout;
	gboolean unlimited;
};
G_DEFINE_TYPE(LsRecipe, ls_recipe, G_TYPE_OBJECT)
/* Recipe owns the complete parsed tree, including every borrowed step. */
static void
ls_recipe_finalize(GObject *object){
	LsRecipe *self = LS_RECIPE(object);
	g_clear_pointer(&self->root, json_node_unref);
	G_OBJECT_CLASS(ls_recipe_parent_class)->finalize(object);
}
static void
ls_recipe_class_init(LsRecipeClass *klass){
	G_OBJECT_CLASS(klass)->finalize = ls_recipe_finalize;
}
static void
ls_recipe_init(LsRecipe *self){
	self->timeout = -1;
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
	if (provider != NULL && !g_str_equal(provider, "codex") &&
		!g_str_equal(provider, "claude-code") && !g_str_equal(provider, "cursor") &&
		!g_str_equal(provider, "grok-build") && !g_str_equal(provider, "antigravity")) {
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
	if (!ls_object_keys(root, "|version|name|site|ai|pipeline|seed_artifacts|", error) ||
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
	if (!ls_object_keys(
			site, "|brief|audience|style|stack|frontend|backend|database|requirements|", error
		) ||
		!text_member(site, "brief", TRUE, error))
		return NULL;
	{
		g_autoptr(GList) members = json_object_get_members(site);
		GList *item;
		for (item = members; item != NULL; item = item->next)
			if (!text_member(site, (const gchar *)item->data, TRUE, error))
				return NULL;
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
	/* Reuse the artifact transaction validator, including path/size conflicts,
	 * before callers can create a workspace or contact a provider. */
	{
		g_autoptr(LsContext) seeds = ls_recipe_create_context(self, error);
		if (seeds == NULL)
			return NULL;
	}
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
