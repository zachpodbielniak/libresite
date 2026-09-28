/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "libresite.h"
#include <glib/gstdio.h>
#include <string.h>

/* Drives the real ai-glib adapters against the controlled protocol fixture
 * and asserts on the argv each CLI would receive. No provider is contacted.
 * Each case runs in a fresh child so the fixture environment is set before
 * any thread exists; setenv() is not thread-safe. */
typedef struct {
	const gchar *provider;
	const gchar *bypass;
	gint64 max_tokens;
	const gchar *output_cap;
} AdapterCase;

static const AdapterCase adapter_cases[] = {
	{"claude-code", NULL, 0, "128000"},
	{"claude-code", NULL, 5000, "32000"},
	{"claude-code", NULL, 64000, "64000"},
	{"codex", "--dangerously-bypass-approvals-and-sandbox", 0, "unset"},
	{"cursor", "--force", 0, "unset"},
	{"grok-build", "bypassPermissions", 0, "unset"},
	{"antigravity", "--dangerously-skip-permissions", 0, "unset"}
};

static gint
argv_index(GStrv argv, const gchar *word){
	guint i;
	for (i = 0; argv[i] != NULL; i++)
		if (g_str_equal(argv[i], word))
			return (gint)i;
	return -1;
}

/* Run one generation through @adapter; returns the recorded argv and stores
 * the child's CLAUDE_CODE_MAX_OUTPUT_TOKENS ("unset" if absent) in @output_cap. */
static GStrv
generate_argv(const AdapterCase *adapter, const gchar *schema, gchar **output_cap){
	g_autoptr(GError) error = NULL;
	g_autoptr(LsAiProvider) transport = NULL;
	g_autoptr(JsonObject) settings = json_object_new();
	g_autofree gchar *script = g_canonicalize_filename("tests/fixtures/provider.sh", NULL);
	g_autofree gchar *directory = g_dir_make_tmp("libresite-provider-test-XXXXXX", &error);
	g_autofree gchar *args_path = NULL;
	g_autofree gchar *env_path = NULL;
	g_autofree gchar *response = NULL;
	g_autofree gchar *contents = NULL;
	g_assert_no_error(error);
	g_assert_true(g_file_test(script, G_FILE_TEST_IS_EXECUTABLE));
	args_path = g_build_filename(directory, "args", NULL);
	env_path = g_build_filename(directory, "env", NULL);
	/* The operator's own value must not leak into the assertion. */
	g_unsetenv("CLAUDE_CODE_MAX_OUTPUT_TOKENS");
	g_assert_true(g_setenv("LS_TEST_ENV", env_path, TRUE));
	g_assert_true(g_setenv("CLAUDE_CODE_PATH", script, TRUE));
	g_assert_true(g_setenv("CODEX_PATH", script, TRUE));
	g_assert_true(g_setenv("CURSOR_AGENT_PATH", script, TRUE));
	g_assert_true(g_setenv("GROK_PATH", script, TRUE));
	g_assert_true(g_setenv("AGY_PATH", script, TRUE));
	g_assert_true(g_setenv("LS_TEST_ARGS", args_path, TRUE));
	g_assert_true(g_setenv("LS_TEST_INPUT", "/dev/null", TRUE));
	g_assert_true(g_setenv("LS_TEST_PROVIDER", adapter->provider, TRUE));
	transport = ls_ai_provider_new();
	json_object_set_string_member(settings, "provider", adapter->provider);
	json_object_set_int_member(settings, "timeout", 30);
	json_object_set_int_member(settings, "max_tokens", adapter->max_tokens);
	if (schema != NULL)
		json_object_set_string_member(settings, "json_schema", schema);
	response = ls_provider_generate(LS_PROVIDER(transport), settings, "system", "prompt", NULL, &error);
	g_assert_no_error(error);
	g_assert_nonnull(strstr(response, "\"files\""));
	g_assert_true(g_file_get_contents(args_path, &contents, NULL, &error));
	g_assert_no_error(error);
	g_assert_true(g_file_get_contents(env_path, output_cap, NULL, &error));
	g_assert_no_error(error);
	(void)g_unlink(env_path);
	(void)g_unlink(args_path);
	(void)g_rmdir(directory);
	return g_strsplit(g_strchomp(contents), "\n", -1);
}

/* Claude Code gets only the answer tool: no built-in tools, customizations,
 * MCP servers, slash commands or permission bypass. */
static void
assert_answer_only(GStrv argv){
	gint tools = argv_index(argv, "--tools");
	g_assert_cmpint(tools, >=, 0);
	g_assert_cmpstr(argv[tools + 1], ==, "StructuredOutput");
	g_assert_nonnull(argv[tools + 2]);
	g_assert_true(g_str_has_prefix(argv[tools + 2], "--"));
	g_assert_cmpint(argv_index(argv, "--safe-mode"), >=, 0);
	g_assert_cmpint(argv_index(argv, "--strict-mcp-config"), >=, 0);
	g_assert_cmpint(argv_index(argv, "--disable-slash-commands"), >=, 0);
	g_assert_cmpint(argv_index(argv, "--json-schema"), >=, 0);
	g_assert_cmpint(argv_index(argv, "--dangerously-skip-permissions"), ==, -1);
	g_assert_cmpint(argv_index(argv, "--permission-mode"), ==, -1);
	g_assert_cmpint(argv_index(argv, "--mcp-config"), ==, -1);
	g_assert_cmpint(argv_index(argv, "--allowedTools"), ==, -1);
}

/* Adapters that cannot drop their tools keep the unattended bypass and must
 * not inherit Claude-only flags. */
static void
adapter_argv(gconstpointer data){
	const AdapterCase *adapter = data;
	g_auto(GStrv) argv = NULL;
	g_autofree gchar *output_cap = NULL;
	if (!g_test_subprocess()) {
		g_test_trap_subprocess(NULL, 0, G_TEST_SUBPROCESS_INHERIT_STDERR);
		g_test_trap_assert_passed();
		return;
	}
	argv = generate_argv(adapter, NULL, &output_cap);
	g_assert_cmpstr(output_cap, ==, adapter->output_cap);
	if (adapter->bypass == NULL) {
		assert_answer_only(argv);
		return;
	}
	g_assert_cmpint(argv_index(argv, adapter->bypass), >=, 0);
	g_assert_cmpint(argv_index(argv, "--safe-mode"), ==, -1);
	g_assert_cmpint(argv_index(argv, "--tools"), ==, -1);
}

/* Spec steps pass their own response schema; bundle steps keep the default. */
static void
schema_override(void){
	g_auto(GStrv) argv = NULL;
	g_auto(GStrv) plain = NULL;
	g_autofree gchar *cap = NULL, *cap_plain = NULL;
	const gchar *schema = "{\"type\":\"object\",\"required\":[\"sections\"]}";
	gint index;
	if (!g_test_subprocess()) {
		g_test_trap_subprocess(NULL, 0, G_TEST_SUBPROCESS_INHERIT_STDERR);
		g_test_trap_assert_passed();
		return;
	}
	argv = generate_argv(&adapter_cases[0], schema, &cap);
	index = argv_index(argv, "--json-schema");
	g_assert_cmpint(index, >=, 0);
	g_assert_cmpstr(argv[index + 1], ==, schema);
	plain = generate_argv(&adapter_cases[0], NULL, &cap_plain);
	index = argv_index(plain, "--json-schema");
	g_assert_cmpint(index, >=, 0);
	g_assert_cmpstr(plain[index + 1], !=, schema);
}

int
main(int argc, char **argv){
	guint i;
	g_test_init(&argc, &argv, NULL);
	for (i = 0; i < G_N_ELEMENTS(adapter_cases); i++) {
		g_autofree gchar *path = g_strdup_printf("/ai-provider/argv/%s/%" G_GINT64_FORMAT,
			adapter_cases[i].provider, adapter_cases[i].max_tokens);
		g_test_add_data_func(path, &adapter_cases[i], adapter_argv);
	}
	g_test_add_func("/ai-provider/schema-override", schema_override);
	return g_test_run();
}
