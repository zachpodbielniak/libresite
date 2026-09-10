/* SPDX-License-Identifier: AGPL-3.0-or-later */
#pragma once
#include "core/ls-util.h"
G_BEGIN_DECLS
/**
 * ls_qa_check:
 * @directory: published project directory
 * @report: (out) (optional) (transfer full): human-readable findings
 * @cancellable: (nullable): cancellation token
 * @error: return location for an error
 *
 * Inspect a published tree without executing generated code. Native in-process
 * validate remains literal-content/byte predicates; this runner looks at HTML
 * structure, local asset references and binary-asset presence on disk.
 * Returns: whether the tree was readable and had no structural findings
 */
gboolean ls_qa_check(const gchar *directory, gchar **report, GCancellable *cancellable,
	GError **error);
G_END_DECLS
