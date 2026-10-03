/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* Published-tree QA: every srcset candidate must be published as well as the
 * src, since a phone may pick any of them. tests/qa.sh covers the CLI. */
#include "libresite.h"
#include <glib/gstdio.h>
#include <string.h>

static const gchar *page =
	"<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" "
	"content=\"width=device-width\"><title>Hi</title></head><body><img src=\"assets/bench-800w.webp\" "
	"srcset=\"assets/bench-480w.webp 480w,assets/bench-800w.webp  800w\" sizes=\"100vw\" alt=\"\" "
	"width=\"800\" height=\"533\"><picture><source srcset=\"assets/bench-1200w.webp 1200w\"></picture></body></html>";

static void
write_file(const gchar *directory, const gchar *name, const gchar *text){
	g_autofree gchar *path = g_build_filename(directory, name, NULL);
	g_autoptr(GError) error = NULL;
	g_assert_true(g_file_set_contents(path, text, -1, &error));
	g_assert_no_error(error);
}

static void
test_srcset(void){
	g_autoptr(GError) error = NULL;
	g_autofree gchar *directory = g_dir_make_tmp("libresite-qa-XXXXXX", &error);
	g_autofree gchar *assets = NULL;
	g_autofree gchar *report = NULL;
	const gchar *const names[] = {"bench-480w.webp", "bench-800w.webp", "bench-1200w.webp"};
	guint i;
	g_assert_no_error(error);
	assets = g_build_filename(directory, "assets", NULL);
	g_assert_cmpint(g_mkdir(assets, 0700), ==, 0);
	write_file(directory, "index.html", page);
	write_file(assets, "bench-800w.webp", "RIFF");
	g_assert_false(ls_qa_check(directory, &report, NULL, &error));
	g_assert_no_error(error);
	g_assert_nonnull(strstr(report, "missing local asset assets/bench-480w.webp"));
	g_assert_nonnull(strstr(report, "missing local asset assets/bench-1200w.webp"));
	g_assert_null(strstr(report, "missing local asset assets/bench-800w.webp"));
	g_clear_pointer(&report, g_free);
	write_file(assets, "bench-480w.webp", "RIFF");
	write_file(assets, "bench-1200w.webp", "RIFF");
	g_assert_true(ls_qa_check(directory, &report, NULL, &error));
	g_assert_no_error(error);
	for (i = 0; i < G_N_ELEMENTS(names); i++) {
		g_autofree gchar *path = g_build_filename(assets, names[i], NULL);
		g_assert_cmpint(g_remove(path), ==, 0);
	}
	{
		g_autofree gchar *index = g_build_filename(directory, "index.html", NULL);
		g_assert_cmpint(g_remove(index), ==, 0);
	}
	g_assert_cmpint(g_rmdir(assets), ==, 0);
	g_assert_cmpint(g_rmdir(directory), ==, 0);
}

int
main(int argc, char **argv){
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/qa/srcset", test_srcset);
	return g_test_run();
}
