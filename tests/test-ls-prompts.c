/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "core/ls-prompts.h"
#include "core/ls-recipe.h"
#include "core/ls-util.h"
#include <string.h>

static const gchar *catalog =
	"version: 1\n"
	"system: bundle system\n"
	"spec_system: spec system\n"
	"styles: {editorial: editorial direction}\n"
	"stages: {plan: plan contract, page: page contract}\n";

static LsRecipe *
recipe(void){
	g_autoptr(GError) error = NULL;
	LsRecipe *result = ls_recipe_new(
		"version: 1\nname: t\nsite: {brief: A studio site, style: editorial}\n"
		"pipeline:\n  steps:\n    - {id: plan, type: plan, instructions: Keep it short.}\n", &error);
	g_assert_no_error(error);
	return result;
}

static JsonObject *
first_step(LsRecipe *value){
	return json_array_get_object_element(ls_recipe_get_steps(value), 0);
}

/* Component steps have their own short system prompt; it is optional so
 * older replacement catalogs still load for bundle recipes. */
static void
test_spec_system(void){
	g_autoptr(GError) error = NULL;
	g_autoptr(LsPrompts) prompts = ls_prompts_new_from_yaml(catalog, &error);
	g_autoptr(LsPrompts) legacy = NULL;
	g_autoptr(LsPrompts) empty = NULL;
	g_assert_no_error(error);
	g_assert_cmpstr(ls_prompts_get_spec_system(prompts), ==, "spec system");
	legacy = ls_prompts_new_from_yaml("version: 1\nsystem: s\nstyles: {a: b}\nstages: {c: d}\n", &error);
	g_assert_no_error(error);
	g_assert_null(ls_prompts_get_spec_system(legacy));
	empty = ls_prompts_new_from_yaml("version: 1\nsystem: s\nspec_system: ''\nstyles: {a: b}\nstages: {c: d}\n", &error);
	g_assert_null(empty);
	g_assert_error(error, LS_ERROR, LS_ERROR_INVALID);
}

/* Instructions, reference and task data arrive in fixed, labeled sections. */
static void
test_render_spec(void){
	g_autoptr(GError) error = NULL;
	g_autoptr(LsPrompts) prompts = ls_prompts_new_from_yaml(catalog, &error);
	g_autoptr(LsRecipe) site = recipe();
	g_autofree gchar *text = ls_prompts_render_spec(prompts, site, first_step(site), "plan",
		"COMPONENTS\nhero", "{\"route\":\"index.html\"}", &error);
	const gchar *order[] = {"STAGE CONTRACT\nplan contract", "DESIGN DIRECTION\neditorial direction",
		"SITE DATA (JSON)\n", "A studio site", "STEP INSTRUCTIONS\nKeep it short.",
		"REFERENCE\nCOMPONENTS\nhero", "TASK (JSON)\n{\"route\":\"index.html\"}"};
	const gchar *cursor;
	guint i;
	g_assert_no_error(error);
	cursor = text;
	for (i = 0; i < G_N_ELEMENTS(order); i++) {
		const gchar *found = strstr(cursor, order[i]);
		if (found == NULL)
			g_error("missing or out of order: %s\n%s", order[i], text);
		cursor = found;
	}
}

static void
test_render_errors(void){
	g_autoptr(GError) error = NULL;
	g_autoptr(LsPrompts) prompts = ls_prompts_new_from_yaml(catalog, &error);
	g_autoptr(LsPrompts) legacy = ls_prompts_new_from_yaml(
		"version: 1\nsystem: s\nstyles: {editorial: b}\nstages: {plan: d}\n", NULL);
	g_autoptr(LsRecipe) site = recipe();
	gchar *text = ls_prompts_render_spec(prompts, site, first_step(site), "missing", NULL, "{}", &error);
	g_assert_null(text);
	g_assert_error(error, LS_ERROR, LS_ERROR_INVALID);
	g_clear_error(&error);
	text = ls_prompts_render_spec(legacy, site, first_step(site), "plan", NULL, "{}", &error);
	g_assert_null(text);
	g_assert_error(error, LS_ERROR, LS_ERROR_INVALID);
	g_assert_nonnull(strstr(error->message, "spec_system"));
	g_clear_error(&error);
	ls_prompts_set_max_bytes(prompts, 40);
	text = ls_prompts_render_spec(prompts, site, first_step(site), "plan", NULL, "{}", &error);
	g_assert_null(text);
	g_assert_error(error, LS_ERROR, LS_ERROR_LIMIT);
}

/* The embedded catalog ships every component stage. */
static void
test_embedded(void){
	g_autoptr(GError) error = NULL;
	g_autoptr(LsPrompts) prompts = ls_prompts_new(NULL, &error);
	g_autoptr(LsRecipe) site = recipe();
	const gchar *stages[] = {"plan", "page", "art", "page-review"};
	guint i;
	g_assert_no_error(error);
	g_assert_nonnull(ls_prompts_get_spec_system(prompts));
	for (i = 0; i < G_N_ELEMENTS(stages); i++) {
		g_autofree gchar *text = ls_prompts_render_spec(prompts, site, first_step(site), stages[i],
			NULL, "{}", &error);
		g_assert_no_error(error);
		g_assert_nonnull(text);
	}
}

int
main(int argc, char **argv){
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/prompts/spec-system", test_spec_system);
	g_test_add_func("/prompts/render-spec", test_render_spec);
	g_test_add_func("/prompts/render-errors", test_render_errors);
	g_test_add_func("/prompts/embedded", test_embedded);
	return g_test_run();
}
