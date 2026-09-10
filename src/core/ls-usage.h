/* SPDX-License-Identifier: AGPL-3.0-or-later */
#pragma once
#include "core/ls-util.h"
G_BEGIN_DECLS
/**
 * ls_usage_validate:
 * @records: (transfer none): array of step execution records
 * @error: return location for an error
 * Returns: whether every record has valid typed fields and consistent counters
 */
gboolean ls_usage_validate(JsonArray *records, GError **error);
/**
 * ls_usage_price:
 * @usage_json: checkpoint or run metadata containing complete usage
 * @rates_yaml: explicit currency and provider/model byte/time rates
 * @error: return location for an error
 *
 * Prices all executions, including failures, without consulting diagnostics.
 * Missing rates or incomplete historical usage are errors, never zero estimates.
 * Returns: (transfer full) (nullable): JSON report with per-execution and total prices
 */
gchar *ls_usage_price(const gchar *usage_json, const gchar *rates_yaml, GError **error);
G_END_DECLS
