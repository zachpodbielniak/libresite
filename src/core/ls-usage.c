/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "core/ls-usage.h"
#include <math.h>

/* JSON getters warn on wrong types; validate before accessing untrusted records. */
static gboolean
nonnegative_integer(JsonObject *object, const gchar *key){
	JsonNode *node = json_object_get_member(object, key);
	return node != NULL && json_node_get_value_type(node) == G_TYPE_INT64 &&
		json_node_get_int(node) >= 0;
}

gboolean
ls_usage_validate(JsonArray *records, GError **error){
	guint i, j;
	const gchar *counters[] = {"attempts", "retries", "bytes_in", "bytes_out", "elapsed_us"};
	for (i = 0; i < json_array_get_length(records); i++) {
		JsonNode *node = json_array_get_element(records, i);
		JsonObject *record;
		gint64 attempts;
		if (!JSON_NODE_HOLDS_OBJECT(node))
			goto invalid;
		record = json_node_get_object(node);
		if (!ls_object_keys(record,
			"|step|provider|model|success|attempts|retries|bytes_in|bytes_out|elapsed_us|", error))
			return FALSE;
		if (ls_string(record, "step") == NULL || *ls_string(record, "step") == '\0' ||
			ls_string(record, "provider") == NULL || *ls_string(record, "provider") == '\0' ||
			ls_string(record, "model") == NULL || *ls_string(record, "model") == '\0')
			goto invalid;
		node = json_object_get_member(record, "success");
		if (node == NULL || json_node_get_value_type(node) != G_TYPE_BOOLEAN)
			goto invalid;
		for (j = 0; j < G_N_ELEMENTS(counters); j++) {
			if (!nonnegative_integer(record, counters[j]))
				goto invalid;
		}
		attempts = json_object_get_int_member(record, "attempts");
		if (json_object_get_int_member(record, "retries") != (attempts > 0 ? attempts - 1 : 0))
			goto invalid;
		if (attempts == 0 && (json_object_get_int_member(record, "bytes_in") != 0 ||
			json_object_get_int_member(record, "bytes_out") != 0))
			goto invalid;
	}
	return TRUE;
invalid:
	g_set_error(error, LS_ERROR, LS_ERROR_INVALID, "Invalid usage record at index %u", i);
	return FALSE;
}

/* Rates must be explicit finite nonnegative numbers, including deliberate zeros. */
static gboolean
valid_rate(JsonObject *rate, const gchar *key){
	JsonNode *node = json_object_get_member(rate, key);
	GType type = node != NULL ? json_node_get_value_type(node) : G_TYPE_INVALID;
	gdouble value;
	if (type != G_TYPE_DOUBLE && type != G_TYPE_INT64)
		return FALSE;
	value = json_node_get_double(node);
	return isfinite(value) && value >= 0;
}

gchar *
ls_usage_price(const gchar *usage_json, const gchar *rates_yaml, GError **error){
	g_autoptr(JsonNode) usage = ls_json_parse(usage_json, error);
	g_autoptr(JsonNode) rates = NULL;
	g_autoptr(JsonNode) result = json_node_new(JSON_NODE_OBJECT);
	g_autoptr(JsonObject) report = json_object_new();
	g_autoptr(JsonArray) prices = json_array_new();
	JsonObject *root, *card;
	JsonNode *node;
	JsonArray *records, *entries;
	gdouble total = 0;
	guint i, j;
	if (usage == NULL)
		return NULL;
	rates = ls_yaml_parse(rates_yaml, error);
	if (rates == NULL)
		return NULL;
	if (!JSON_NODE_HOLDS_OBJECT(usage) || !JSON_NODE_HOLDS_OBJECT(rates))
		goto invalid;
	root = json_node_get_object(usage);
	card = json_node_get_object(rates);
	if (!nonnegative_integer(root, "version") || json_object_get_int_member(root, "version") != 1)
		goto invalid;
	node = json_object_get_member(root, "usage_complete");
	if (node == NULL || json_node_get_value_type(node) != G_TYPE_BOOLEAN || !json_node_get_boolean(node)) {
		g_set_error_literal(error, LS_ERROR, LS_ERROR_INVALID,
			"Historical usage is incomplete; refusing to price the job");
		return NULL;
	}
	node = json_object_get_member(root, "usage");
	if (node == NULL || !JSON_NODE_HOLDS_ARRAY(node))
		goto invalid;
	records = json_node_get_array(node);
	if (!ls_usage_validate(records, error))
		return NULL;
	if (!ls_object_keys(card, "|version|currency|rates|", error))
		return NULL;
	if (!nonnegative_integer(card, "version") || json_object_get_int_member(card, "version") != 1 ||
		ls_string(card, "currency") == NULL || *ls_string(card, "currency") == '\0')
		goto invalid;
	node = json_object_get_member(card, "rates");
	if (node == NULL || !JSON_NODE_HOLDS_ARRAY(node))
		goto invalid;
	entries = json_node_get_array(node);
	/* Reject duplicate identities even when the job never used that provider. */
	for (i = 0; i < json_array_get_length(entries); i++) {
		JsonObject *rate;
		node = json_array_get_element(entries, i);
		if (!JSON_NODE_HOLDS_OBJECT(node))
			goto invalid;
		rate = json_node_get_object(node);
		if (!ls_object_keys(rate,
			"|provider|model|per_million_input_bytes|per_million_output_bytes|per_second|", error))
			return NULL;
		if (ls_string(rate, "provider") == NULL || *ls_string(rate, "provider") == '\0' ||
			ls_string(rate, "model") == NULL || *ls_string(rate, "model") == '\0' ||
			!valid_rate(rate, "per_million_input_bytes") ||
			!valid_rate(rate, "per_million_output_bytes") || !valid_rate(rate, "per_second"))
			goto invalid;
		for (j = 0; j < i; j++) {
			JsonObject *previous = json_array_get_object_element(entries, j);
			if (g_str_equal(ls_string(previous, "provider"), ls_string(rate, "provider")) &&
				g_str_equal(ls_string(previous, "model"), ls_string(rate, "model")))
				goto invalid;
		}
	}
	for (i = 0; i < json_array_get_length(records); i++) {
		JsonObject *record = json_array_get_object_element(records, i);
		JsonObject *rate = NULL;
		JsonObject *priced;
		gdouble amount;
		for (j = 0; j < json_array_get_length(entries); j++) {
			JsonObject *candidate = json_array_get_object_element(entries, j);
			if (g_str_equal(ls_string(candidate, "provider"), ls_string(record, "provider")) &&
				g_str_equal(ls_string(candidate, "model"), ls_string(record, "model"))) {
				rate = candidate;
				break;
			}
		}
		if (rate == NULL || g_str_equal(ls_string(record, "provider"), "(mixed)") ||
			g_str_equal(ls_string(record, "model"), "(mixed)")) {
			g_set_error(error, LS_ERROR, LS_ERROR_INVALID, "No unambiguous rate for %s / %s",
				ls_string(record, "provider"), ls_string(record, "model"));
			return NULL;
		}
		amount = ((gdouble)json_object_get_int_member(record, "bytes_in") / 1000000.0) *
			json_object_get_double_member(rate, "per_million_input_bytes") +
			((gdouble)json_object_get_int_member(record, "bytes_out") / 1000000.0) *
			json_object_get_double_member(rate, "per_million_output_bytes") +
			((gdouble)json_object_get_int_member(record, "elapsed_us") / 1000000.0) *
			json_object_get_double_member(rate, "per_second");
		total += amount;
		if (!isfinite(total))
			goto invalid;
		priced = json_object_new();
		json_object_set_object_member(priced, "usage", json_object_ref(record));
		json_object_set_double_member(priced, "amount", amount);
		json_array_add_object_element(prices, priced);
	}
	json_object_set_string_member(report, "currency", ls_string(card, "currency"));
	json_object_set_double_member(report, "total", total);
	json_object_set_array_member(report, "steps", json_array_ref(prices));
	json_object_set_object_member(report, "rate_card", json_object_ref(card));
	json_node_set_object(result, report);
	return ls_json_encode(result);
invalid:
	g_set_error_literal(error, LS_ERROR, LS_ERROR_INVALID,
		"Invalid usage metadata or rate card (version, schema, duplicate rates or numeric overflow)");
	return NULL;
}
