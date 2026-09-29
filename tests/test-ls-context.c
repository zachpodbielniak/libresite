/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "libresite.h"
#include <string.h>

static LsContext *
context_with(const gchar *bundle){
	g_autoptr(GError) error = NULL;
	LsContext *context = ls_context_new();
	g_assert_true(ls_context_apply(context, bundle, &error));
	g_assert_no_error(error);
	return context;
}

/* Archived files leave the published tree but survive a snapshot round trip,
 * so a checkpoint or run record keeps them. */
static void
archive_round_trip(void){
	g_autoptr(LsContext) context = context_with("{\"files\":["
		"{\"path\":\"index.html\",\"content\":\"<main>Home</main>\"},"
		"{\"path\":\"site/plan.json\",\"content\":\"{\\\"name\\\":\\\"x\\\"}\"}]}");
	g_autoptr(LsContext) restored = ls_context_new();
	g_autoptr(GError) error = NULL;
	g_autofree gchar *snapshot = NULL;
	g_autofree gchar *again = NULL;
	const LsArtifact *kept;
	g_assert_true(ls_context_archive(context, "site/plan.json", &error));
	g_assert_no_error(error);
	g_assert_null(ls_context_lookup(context, "site/plan.json"));
	kept = ls_context_lookup_archived(context, "site/plan.json");
	g_assert_nonnull(kept);
	g_assert_cmpstr(ls_artifact_get_content(kept), ==, "{\"name\":\"x\"}");
	g_assert_null(ls_context_lookup_archived(context, "index.html"));
	snapshot = ls_context_snapshot(context);
	g_assert_nonnull(strstr(snapshot, "\"archive\":[{\"path\":\"site/plan.json\""));
	g_assert_true(ls_context_restore(restored, snapshot, &error));
	g_assert_no_error(error);
	g_assert_nonnull(ls_context_lookup(restored, "index.html"));
	g_assert_null(ls_context_lookup(restored, "site/plan.json"));
	g_assert_nonnull(ls_context_lookup_archived(restored, "site/plan.json"));
	again = ls_context_snapshot(restored);
	g_assert_cmpstr(snapshot, ==, again);
}

/* A context without an archive keeps the original snapshot grammar. */
static void
archive_absent(void){
	g_autoptr(LsContext) context = context_with("{\"files\":[{\"path\":\"a.html\",\"content\":\"a\"}]}");
	g_autofree gchar *snapshot = ls_context_snapshot(context);
	g_assert_cmpstr(snapshot, ==, "{\"files\":[{\"path\":\"a.html\",\"content\":\"a\"}]}");
}

/* Only native steps archive: generated responses cannot write the archive, a
 * missing file cannot be archived, and restore rejects unsafe archives. */
static void
archive_errors(void){
	g_autoptr(LsContext) context = context_with("{\"files\":[{\"path\":\"a.html\",\"content\":\"a\"}]}");
	const gchar *bad[] = {
		"{\"files\":[],\"archive\":[{\"path\":\"../x\",\"content\":\"x\"}]}",
		"{\"files\":[],\"archive\":[{\"path\":\"x\",\"content\":\"x\"},{\"path\":\"x\",\"content\":\"y\"}]}",
		"{\"files\":[],\"archive\":{\"path\":\"x\"}}",
		"{\"files\":[],\"archive\":[{\"path\":\"x\"}]}"};
	g_autoptr(GError) error = NULL;
	guint i;
	g_assert_false(ls_context_apply(context,
		"{\"files\":[],\"archive\":[{\"path\":\"x\",\"content\":\"x\"}]}", &error));
	g_assert_nonnull(error);
	g_clear_error(&error);
	g_assert_false(ls_context_archive(context, "missing.html", &error));
	g_assert_error(error, LS_ERROR, LS_ERROR_INVALID);
	g_clear_error(&error);
	for (i = 0; i < G_N_ELEMENTS(bad); i++) {
		g_autofree gchar *before = ls_context_snapshot(context);
		g_autofree gchar *after = NULL;
		g_assert_false(ls_context_restore(context, bad[i], &error));
		g_assert_nonnull(error);
		g_clear_error(&error);
		after = ls_context_snapshot(context);
		g_assert_cmpstr(before, ==, after);
	}
}

/* Archiving the same path twice keeps the newest content. */
static void
archive_replace(void){
	g_autoptr(LsContext) context = context_with("{\"files\":[{\"path\":\"s/a.json\",\"content\":\"1\"}]}");
	g_autoptr(GError) error = NULL;
	g_assert_true(ls_context_archive(context, "s/a.json", &error));
	g_assert_true(ls_context_apply(context, "{\"files\":[{\"path\":\"s/a.json\",\"content\":\"2\"}]}", &error));
	g_assert_true(ls_context_archive(context, "s/a.json", &error));
	g_assert_no_error(error);
	g_assert_cmpstr(ls_artifact_get_content(ls_context_lookup_archived(context, "s/a.json")), ==, "2");
}

int
main(int argc, char **argv){
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/context/archive/round-trip", archive_round_trip);
	g_test_add_func("/context/archive/absent", archive_absent);
	g_test_add_func("/context/archive/errors", archive_errors);
	g_test_add_func("/context/archive/replace", archive_replace);
	return g_test_run();
}
