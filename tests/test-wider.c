/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* The wider block joins extra look values and home openings to the
 * catalog; a block that reuses a name, or holds a member that is not
 * documented for it, does not load. See docs/components.org, "Look". */
#include "libresite.h"
#include "core/ls-components.h"
#include "core/ls-util.h"
#include <string.h>

static LsComponents *
library(void){
	g_autoptr(GError) error = NULL;
	LsComponents *components = ls_components_new(&error);
	g_assert_no_error(error);
	return components;
}

/* A catalog whose wider block reuses a name, or holds a retired pool such
 * as home layouts, section forms or palette modes, does not load. */
static void
test_rejects(void){
	g_autoptr(LsComponents) components = library();
	g_autofree gchar *snapshot = ls_components_get_snapshot(components);
	const gchar *bad[] = {
		"  layouts:\n    split:\n      summary: Again.\n      openings: [hero/split]\n      order: [offer, process, proof, story, practical]\n",
		"  looks:\n    header:\n      bar: Again.\n",
		"  forms:\n    nowhere: [faq]\n",
		"  modes: {sepia: 10}\n",
		"  colours: {}\n"
	};
	guint i;
	for (i = 0; i < G_N_ELEMENTS(bad); i++) {
		g_autoptr(JsonNode) node = ls_json_parse(snapshot, NULL);
		JsonObject *files = json_object_get_object_member(json_node_get_object(node), "files");
		g_autofree gchar *catalog = g_strdup(json_object_get_string_member(files, "catalog.yaml"));
		g_autofree gchar *changed = NULL, *text = NULL;
		gchar *at = strstr(catalog, "\nwider:\n");
		g_autoptr(GError) error = NULL;
		g_autoptr(LsComponents) broken = NULL;
		g_assert_nonnull(at);
		at[strlen("\nwider:\n")] = '\0';
		changed = g_strconcat(catalog, bad[i], NULL);
		json_object_set_string_member(files, "catalog.yaml", changed);
		text = ls_json_encode(node);
		broken = ls_components_new_from_snapshot(text, &error);
		g_assert_null(broken);
		g_assert_error(error, LS_ERROR, LS_ERROR_INVALID);
	}
}

int
main(int argc, char **argv){
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/wider/rejects", test_rejects);
	return g_test_run();
}
