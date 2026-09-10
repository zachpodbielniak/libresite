/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "core/ls-package.h"
#include <errno.h>
#include <glib/gstdio.h>
#include <string.h>

/* These fixed names are never supplied by a manifest. Keep cleanup bounded to
 * our private staging directory, even when publication fails. */
static const gchar *package_files[] = {"recipe.yaml", "prompts.yaml", "package.yaml"};

/* A package is a snapshot, not a pointer to mutable installed data. */
gboolean
ls_package_save(
	const gchar *directory, LsRecipe *recipe, LsPrompts *prompts, GError **error
){
	JsonObject *root = ls_recipe_get_root(recipe);
	JsonObject *metadata;
	g_autofree gchar *recipe_digest = NULL, *manifest = NULL;
	g_autofree gchar *absolute = NULL, *staging = NULL;
	g_autoptr(GFile) source = NULL, target = NULL;
	const gchar *texts[3];
	gboolean okay = FALSE;
	guint i;
	if (!json_object_has_member(root, "package") || directory == NULL || *directory == '\0') {
		g_set_error_literal(error, LS_ERROR, LS_ERROR_INVALID,
			"Sealing requires recipe package metadata and a new directory");
		return FALSE;
	}
	metadata = json_object_get_object_member(root, "package");
	recipe_digest = g_compute_checksum_for_string(G_CHECKSUM_SHA256,
		ls_recipe_get_source(recipe), -1);
	/* Metadata tokens were restricted by recipe validation; quoted YAML scalar
	 * values prevent numeric releases or digest prefixes from changing types. */
	manifest = g_strdup_printf(
		"version: 1\nsku: '%s'\nrelease: '%s'\nkind: '%s'\n"
		"recipe_sha256: '%s'\ncatalog_sha256: '%s'\n",
		ls_string(metadata, "sku"), ls_string(metadata, "release"),
		ls_string(metadata, "kind"), recipe_digest, ls_prompts_get_digest(prompts));
	texts[0] = ls_recipe_get_source(recipe);
	texts[1] = ls_prompts_get_source(prompts);
	texts[2] = manifest;
	absolute = g_canonicalize_filename(directory, NULL);
	staging = g_strconcat(absolute, ".seal-XXXXXX", NULL);
	if (g_mkdtemp_full(staging, 0700) == NULL) {
		g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
			"Cannot create package staging directory: %s", g_strerror(errno));
		return FALSE;
	}
	for (i = 0; i < G_N_ELEMENTS(package_files); i++) {
		g_autofree gchar *path = g_build_filename(staging, package_files[i], NULL);
		if (!g_file_set_contents_full(path, texts[i], -1,
			G_FILE_SET_CONTENTS_CONSISTENT | G_FILE_SET_CONTENTS_DURABLE, 0600, error))
			goto out;
	}
	/* A same-parent move publishes complete inputs without replacing a sale. */
	source = g_file_new_for_path(staging);
	target = g_file_new_for_path(absolute);
	okay = g_file_move(source, target, G_FILE_COPY_NONE, NULL, NULL, NULL, error);
out:
	if (!okay) {
		for (i = 0; i < G_N_ELEMENTS(package_files); i++) {
			g_autofree gchar *path = g_build_filename(staging, package_files[i], NULL);
			(void)g_unlink(path);
		}
		(void)g_rmdir(staging);
	}
	return okay;
}

/* Strict lowercase SHA-256 syntax avoids accepting truncated or ambiguous pins. */
static gboolean
check_digest(const gchar *text, const gchar *expected, const gchar *name, GError **error){
	g_autofree gchar *actual = NULL;
	guint i;
	if (expected == NULL || strlen(expected) != 64)
		goto mismatch;
	for (i = 0; i < 64; i++) {
		if (!g_ascii_isdigit(expected[i]) && (expected[i] < 'a' || expected[i] > 'f'))
			goto mismatch;
	}
	actual = g_compute_checksum_for_string(G_CHECKSUM_SHA256, text, -1);
	if (g_str_equal(actual, expected))
		return TRUE;
mismatch:
	g_set_error(error, LS_ERROR, LS_ERROR_CONFLICT,
		"Package %s SHA-256 mismatch; restore the archived inputs", name);
	return FALSE;
}

/* Load into local owners so callers never receive one half of a valid pair. */
gboolean
ls_package_load(
	const gchar *directory, LsRecipe **recipe, LsPrompts **prompts, GError **error
){
	g_autofree gchar *recipe_text = NULL, *catalog_text = NULL, *manifest_text = NULL;
	g_autoptr(JsonNode) manifest = NULL;
	g_autoptr(LsRecipe) parsed_recipe = NULL;
	g_autoptr(LsPrompts) parsed_prompts = NULL;
	JsonObject *object, *metadata;
	JsonNode *version;
	const gchar *keys[] = {"sku", "release", "kind"};
	gchar **texts[] = {&recipe_text, &catalog_text, &manifest_text};
	guint i;
	g_return_val_if_fail(recipe != NULL && prompts != NULL, FALSE);
	*recipe = NULL;
	*prompts = NULL;
	if (directory == NULL || *directory == '\0') {
		g_set_error_literal(error, LS_ERROR, LS_ERROR_INVALID, "Package directory is required");
		return FALSE;
	}
	for (i = 0; i < G_N_ELEMENTS(package_files); i++) {
		g_autofree gchar *path = g_build_filename(directory, package_files[i], NULL);
		*texts[i] = ls_read_text(path, error);
		if (*texts[i] == NULL)
			return FALSE;
	}
	manifest = ls_yaml_parse(manifest_text, error);
	if (manifest == NULL)
		return FALSE;
	if (!JSON_NODE_HOLDS_OBJECT(manifest))
		goto invalid;
	object = json_node_get_object(manifest);
	if (!ls_object_keys(object, "|version|sku|release|kind|recipe_sha256|catalog_sha256|", error))
		return FALSE;
	version = json_object_get_member(object, "version");
	if (version == NULL || json_node_get_value_type(version) != G_TYPE_INT64 ||
		json_node_get_int(version) != 1)
		goto invalid;
	if (!check_digest(recipe_text, ls_string(object, "recipe_sha256"), "recipe", error) ||
		!check_digest(catalog_text, ls_string(object, "catalog_sha256"), "catalog", error))
		return FALSE;
	parsed_recipe = ls_recipe_new(recipe_text, error);
	if (parsed_recipe == NULL)
		return FALSE;
	parsed_prompts = ls_prompts_new_from_yaml(catalog_text, error);
	if (parsed_prompts == NULL)
		return FALSE;
	if (!json_object_has_member(ls_recipe_get_root(parsed_recipe), "package"))
		goto invalid;
	metadata = json_object_get_object_member(ls_recipe_get_root(parsed_recipe), "package");
	for (i = 0; i < G_N_ELEMENTS(keys); i++) {
		if (g_strcmp0(ls_string(object, keys[i]), ls_string(metadata, keys[i])) != 0)
			goto invalid;
	}
	*recipe = (LsRecipe *)g_steal_pointer(&parsed_recipe);
	*prompts = (LsPrompts *)g_steal_pointer(&parsed_prompts);
	return TRUE;
invalid:
	g_set_error_literal(error, LS_ERROR, LS_ERROR_INVALID,
		"Package manifest needs version 1 and matching sku, release and kind");
	return FALSE;
}
