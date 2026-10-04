/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* Recipe contact facts: the business phone and email the builder links. */
#include "libresite.h"
#include <string.h>

static gchar *
recipe_with(const gchar *contact){
	return g_strdup_printf("version: 1\nname: test\nsite: {brief: hi, contact: %s}\n"
		"pipeline: {steps: [{id: a, type: frontend}]}", contact);
}

/* Phone numbers and addresses as a person writes them are accepted. */
static void
contact_valid(void){
	const gchar *contacts[] = {"{phone: '+1 (555) 014-2270', email: hello@harbor.example}",
		"{phone: '0161 496.0000'}", "{email: a.b+c@x.example}", "{address: 1 Quay Street}", "{}"};
	guint i;
	for (i = 0; i < G_N_ELEMENTS(contacts); i++) {
		g_autofree gchar *text = recipe_with(contacts[i]);
		g_autoptr(GError) error = NULL;
		g_autoptr(LsRecipe) recipe = ls_recipe_new(text, &error);
		g_assert_no_error(error);
		g_assert_nonnull(recipe);
	}
}

/* Anything the builder could not turn into a call or mail link is refused
 * with the rule, including through the form adapter. */
static void
contact_invalid(void){
	const gchar *contacts[] = {"{phone: call us}", "{phone: '+1 555 0100 ext 2'}", "{phone: '12'}",
		"{phone: '1+2 345'}", "{phone: '0123456789012345678'}", "{email: not-an-address}",
		"{email: 'a b@x.example'}", "{email: 'a@b@x.example'}", "{email: '<a@x.example>'}"};
	guint i;
	for (i = 0; i < G_N_ELEMENTS(contacts); i++) {
		g_autofree gchar *text = recipe_with(contacts[i]);
		g_autoptr(GError) error = NULL;
		g_autoptr(LsRecipe) recipe = ls_recipe_new(text, &error);
		g_assert_null(recipe);
		g_assert_error(error, LS_ERROR, LS_ERROR_INVALID);
		if (strstr(error->message, strstr(contacts[i], "phone") != NULL ? "contact.phone" : "contact.email") == NULL)
			g_error("%s: unexpected message %s", contacts[i], error->message);
	}
	{
		g_autoptr(GError) error = NULL;
		g_autoptr(LsRecipe) recipe = ls_recipe_from_form(
			"{\"name\":\"t\",\"site\":{\"pages\":[{\"path\":\"/\",\"title\":\"Home\"}],\"offers\":[],"
			"\"contact\":{\"phone\":\"call us\"},\"brand\":{\"name\":\"T\"},\"copy_lock\":[],"
			"\"languages\":{\"default\":\"en\",\"supported\":[\"en\"]}}}", &error);
		g_assert_null(recipe);
		g_assert_nonnull(error);
		g_assert_nonnull(strstr(error->message, "contact.phone"));
	}
}

int
main(int argc, char **argv){
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/recipe/contact-valid", contact_valid);
	g_test_add_func("/recipe/contact-invalid", contact_invalid);
	return g_test_run();
}
