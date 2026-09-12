/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include <json-glib/json-glib.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <unistd.h>
/* Parse every complete line, checking order and structured field types. */
int
main(int argc, char **argv){
	g_autofree gchar *text = NULL;
	g_auto(GStrv) lines = NULL, expected = NULL;
	guint i;
	if (argc > 3 && g_str_equal(argv[1], "--lock")) {
		gint fd = open(argv[2], O_CREAT | O_RDWR | O_CLOEXEC, 0600);
		gint status;
		g_autofree gchar *diagnostic = NULL;
		g_assert_cmpint(fd, >=, 0);
		g_assert_cmpint(flock(fd, LOCK_EX), ==, 0);
		g_assert_true(g_spawn_sync(NULL, argv + 3, NULL, 0, NULL, NULL, NULL,
			&diagnostic, &status, NULL));
		g_assert_true(WIFEXITED(status));
		g_assert_cmpint(WEXITSTATUS(status), ==, 6);
		close(fd);
		return 0;
	}
	g_assert_cmpint(argc, ==, 4);
	g_assert_true(g_file_get_contents(argv[1], &text, NULL, NULL));
	lines = g_strsplit(text, "\n", -1);
	expected = g_strsplit(argv[2], ",", -1);
	for (i = 0; expected[i] != NULL; i++) {
		g_autoptr(JsonParser) parser = json_parser_new();
		JsonObject *object;
		const gchar *event;
		json_parser_set_strict(parser, TRUE);
		g_assert_nonnull(lines[i]);
		g_assert_true(json_parser_load_from_data(parser, lines[i], -1, NULL));
		g_assert_true(JSON_NODE_HOLDS_OBJECT(json_parser_get_root(parser)));
		object = json_node_get_object(json_parser_get_root(parser));
		event = json_object_get_string_member(object, "event");
		g_assert_cmpstr(event, ==, expected[i]);
		g_assert_cmpint(json_object_get_int_member(object, "elapsed_ms"), >=, 0);
		if (g_str_has_prefix(event, "step.")) {
			const gchar *numbers[] = {"index", "attempt", "bytes_in", "bytes_out"};
			guint j;
			g_assert_nonnull(json_object_get_string_member(object, "step"));
			g_assert_nonnull(json_object_get_string_member(object, "provider"));
			g_assert_nonnull(json_object_get_string_member(object, "model"));
			for (j = 0; j < G_N_ELEMENTS(numbers); j++) {
				g_assert_cmpuint(json_node_get_value_type(json_object_get_member(object, numbers[j])), ==, G_TYPE_INT64);
				g_assert_cmpint(json_object_get_int_member(object, numbers[j]), >=, 0);
			}
			if (g_str_equal(event, "step.completed") &&
				!g_str_equal(json_object_get_string_member(object, "provider"), "native")) {
				g_assert_cmpint(json_object_get_int_member(object, "attempt"), >=, 1);
				g_assert_cmpint(json_object_get_int_member(object, "bytes_in"), >, 0);
				g_assert_cmpint(json_object_get_int_member(object, "bytes_out"), >, 0);
			}
		}
		if (g_str_equal(event, "run.finished"))
			g_assert_cmpint(json_object_get_int_member(object, "exit_code"), ==, g_ascii_strtoll(argv[3], NULL, 10));
	}
	g_assert_cmpstr(lines[i], ==, "");
	g_assert_null(lines[i + 1]);
	return 0;
}
