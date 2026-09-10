/* SPDX-License-Identifier: AGPL-3.0-or-later */
#pragma once
#include "core/ls-prompts.h"
G_BEGIN_DECLS
/**
 * ls_package_save:
 * @directory: new package directory; parent must exist
 * @recipe: immutable recipe with package SKU metadata
 * @prompts: exact catalog selected for this sale
 * @error: return location for an error
 *
 * Publish recipe.yaml, prompts.yaml and package.yaml together, plus a pinned
 * scaffold.json when the recipe uses native scaffolding. Never overwrite
 * an existing directory. Call ls_pipeline_validate() first to check modules
 * and prompt names against the pipeline that will execute this package.
 * Returns: whether the complete package was published
 */
gboolean ls_package_save(
	const gchar *directory, LsRecipe *recipe, LsPrompts *prompts, GError **error
);
/**
 * ls_package_load:
 * @directory: archived package directory
 * @recipe: (out) (transfer full): verified recipe; NULL on failure
 * @prompts: (out) (transfer full): verified catalog; NULL on failure
 * @error: return location for an error
 *
 * Verify recipe/catalog SHA-256 digests before parsing either input, and verify
 * any required native scaffold snapshot before returning. Never substitute
 * embedded defaults or fetch missing inputs. A digest detects accidental drift,
 * not an attacker who can rewrite the manifest. Pin the compatible generator
 * and any plugin binaries separately for long-term execution compatibility.
 * Returns: whether both inputs and their SKU identity were verified
 */
gboolean ls_package_load(
	const gchar *directory, LsRecipe **recipe, LsPrompts **prompts, GError **error
);
G_END_DECLS
