/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* Type range: a curated library of font pairings that fit each sector,
 * every one naming its families in its stacks. See docs/components.org,
 * "The default design". */
#include "libresite.h"
#include "core/ls-components.h"
#include "core/ls-util.h"
#include <string.h>

static JsonNode *
yaml(const gchar *path){
	g_autofree gchar *text = NULL;
	g_assert_true(g_file_get_contents(path, &text, NULL, NULL));
	return ls_yaml_parse(text, NULL);
}

/* The library offers at least eight pairings to every sector, 40 in all,
 * and every pairing loads only open families it names first in a stack. */
static void
test_pairings(void){
	g_autoptr(JsonNode) identities = yaml("data/components/identities.yaml");
	g_autoptr(JsonNode) pairings = yaml("data/components/pairings.yaml");
	JsonObject *typefaces = json_object_get_object_member(json_node_get_object(identities), "typefaces");
	JsonObject *added = json_object_get_object_member(json_node_get_object(pairings), "pairings");
	JsonObject *sectors = json_object_get_object_member(json_node_get_object(identities), "sectors");
	g_autoptr(GList) names = json_object_get_members(sectors);
	g_autoptr(GList) entries = json_object_get_members(added);
	GList *name, *entry;
	g_assert_cmpuint(json_object_get_size(typefaces) + json_object_get_size(added), >=, 40);
	for (entry = entries; entry != NULL; entry = entry->next) {
		JsonObject *pairing = json_object_get_object_member(added, entry->data);
		JsonArray *fonts = json_object_get_array_member(pairing, "fonts");
		JsonObject *tokens = json_object_get_object_member(pairing, "tokens");
		guint i;
		g_assert_false(json_object_has_member(typefaces, entry->data));
		g_assert_cmpuint(json_array_get_length(fonts), <=, 3);
		for (i = 0; i < json_array_get_length(fonts); i++) {
			const gchar *family = json_object_get_string_member(json_array_get_object_element(fonts, i), "family");
			g_autofree gchar *quoted = g_strdup_printf("'%s'", family);
			const gchar *display = json_object_get_string_member(tokens, "font_display");
			const gchar *body = json_object_get_string_member(tokens, "font_body");
			g_assert_true(g_str_has_prefix(display, quoted) || g_str_has_prefix(display, family) ||
				g_str_has_prefix(body, quoted) || g_str_has_prefix(body, family));
			g_assert_null(strstr(family, "Fraunces"));
		}
	}
	for (name = names; name != NULL; name = name->next) {
		guint count = 0;
		JsonObject *groups[2] = {typefaces, added};
		guint g;
		for (g = 0; g < 2; g++) {
			g_autoptr(GList) all = json_object_get_members(groups[g]);
			GList *item;
			for (item = all; item != NULL; item = item->next) {
				JsonArray *fits = json_object_get_array_member(json_object_get_object_member(groups[g], item->data), "fits");
				guint i;
				for (i = 0; i < json_array_get_length(fits); i++)
					count += g_strcmp0(json_array_get_string_element(fits, i), name->data) == 0;
			}
		}
		if (count < 8)
			g_error("sector %s has only %u pairings", (const gchar *)name->data, count);
	}
}

int
main(int argc, char **argv){
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/type-colour/pairings", test_pairings);
	return g_test_run();
}
