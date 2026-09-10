/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "steps/ls-step.h"
#include "core/ls-recipe.h"
#include <string.h>
struct _LsGateStep {
	LsStep parent_instance;
};
G_DEFINE_TYPE(LsGateStep, ls_gate_step, LS_TYPE_STEP)

/* A gate is a human stop, not a provider call. Unknown keys must fail preflight
 * so a misspelled review list cannot skip the intended files. */
gboolean
ls_gate_step_check_options(JsonObject *options, GError **error){
	JsonNode *node;
	JsonArray *review;
	g_autoptr(GHashTable) seen = NULL;
	guint i;
	if (options == NULL)
		return TRUE;
	if (!ls_object_keys(options, "|message|review|", error))
		return FALSE;
	if (json_object_has_member(options, "message") &&
		(ls_string(options, "message") == NULL || *ls_string(options, "message") == '\0')) {
		g_set_error_literal(error, LS_ERROR, LS_ERROR_INVALID,
			"gate message must be a nonempty string");
		return FALSE;
	}
	if (!json_object_has_member(options, "review"))
		return TRUE;
	node = json_object_get_member(options, "review");
	if (!JSON_NODE_HOLDS_ARRAY(node) || json_array_get_length(json_node_get_array(node)) == 0 ||
		json_array_get_length(json_node_get_array(node)) > 4096) {
		g_set_error_literal(error, LS_ERROR, LS_ERROR_INVALID,
			"gate review must contain 1..4096 unique safe relative paths");
		return FALSE;
	}
	review = json_node_get_array(node);
	seen = g_hash_table_new(g_str_hash, g_str_equal);
	for (i = 0; i < json_array_get_length(review); i++) {
		const gchar *path;
		node = json_array_get_element(review, i);
		path = json_node_get_value_type(node) == G_TYPE_STRING ? json_node_get_string(node) : NULL;
		if (path == NULL || *path == '\0' || !ls_path_valid(path) ||
			!g_hash_table_add(seen, (gpointer)path)) {
			g_set_error_literal(error, LS_ERROR, LS_ERROR_INVALID,
				"gate review must contain 1..4096 unique safe relative paths");
			return FALSE;
		}
	}
	return TRUE;
}

gchar *
ls_gate_step_preview(JsonObject *options, GError **error){
	GString *text;
	const gchar *message = "Review the current design before implementation.";
	if (!ls_gate_step_check_options(options, error))
		return NULL;
	if (options != NULL && json_object_has_member(options, "message"))
		message = ls_string(options, "message");
	text = g_string_new("GATE\n");
	g_string_append_printf(text, "%s\n", message);
	if (options != NULL && json_object_has_member(options, "review")) {
		JsonArray *review = json_object_get_array_member(options, "review");
		guint i;
		g_string_append(text, "review:\n");
		for (i = 0; i < json_array_get_length(review); i++)
			g_string_append_printf(text, "- %s\n", json_array_get_string_element(review, i));
	}
	g_string_append(text,
		"Resume with --approve STEP-ID after accepting this direction, or --approve-all to skip gates.\n");
	return g_string_free(text, FALSE);
}

/* Stop before paid implementation unless this run carries an explicit approval. */
static gboolean
execute(LsStep *self, LsRecipe *recipe, JsonObject *config, LsPrompts *prompts,
	LsContext *context, LsProvider *provider, GCancellable *cancellable, GError **error){
	JsonNode *node = json_object_get_member(config, "options");
	JsonObject *options = node != NULL && JSON_NODE_HOLDS_OBJECT(node)
		? json_node_get_object(node) : NULL;
	const gchar *id = ls_string(config, "id");
	const gchar *message = "Review the current design before implementation.";
	(void)prompts;
	(void)provider;
	if (g_cancellable_set_error_if_cancelled(cancellable, error) ||
		!ls_gate_step_check_options(options, error))
		return FALSE;
	if (options != NULL && json_object_has_member(options, "message"))
		message = ls_string(options, "message");
	if (options != NULL && json_object_has_member(options, "review")) {
		JsonArray *review = json_object_get_array_member(options, "review");
		guint i;
		for (i = 0; i < json_array_get_length(review); i++) {
			const gchar *path = json_array_get_string_element(review, i);
			if (ls_context_lookup(context, path) == NULL) {
				g_set_error(error, LS_ERROR, LS_ERROR_INVALID,
					"Gate review file is missing: %s", path);
				return FALSE;
			}
		}
	}
	g_signal_emit_by_name(self, "progress", message);
	if (ls_recipe_is_approved(recipe, id))
		return TRUE;
	g_set_error(error, LS_ERROR, LS_ERROR_CONFLICT,
		"Gate '%s' needs approval. Inspect workspace/review/%s, then resume with --approve %s "
		"(or --approve-all)",
		id, id, id);
	return FALSE;
}

static void
ls_gate_step_class_init(LsGateStepClass *klass){
	LS_STEP_CLASS(klass)->execute = execute;
}
static void
ls_gate_step_init(LsGateStep *self){
	(void)self;
}
