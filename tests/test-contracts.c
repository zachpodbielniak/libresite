/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "libresite.h"
#include <glib/gstdio.h>
#include <string.h>

/* Deliberately broken extensions exercise the public boundaries, including
 * callers that discard errors. Production wrappers must normalize all modes. */
typedef struct {
	GObject parent_instance;
	guint mode;
	guint calls;
} ContractProvider;
typedef struct { GObjectClass parent_class; } ContractProviderClass;
static void provider_init(LsProviderInterface *iface);
GType contract_provider_get_type(void);
G_DEFINE_TYPE_WITH_CODE(ContractProvider, contract_provider, G_TYPE_OBJECT,
	G_IMPLEMENT_INTERFACE(LS_TYPE_PROVIDER, provider_init))
static gchar *
generate(LsProvider *provider, JsonObject *settings, const gchar *system,
	const gchar *prompt, GCancellable *cancellable, GError **error){
	ContractProvider *self = (ContractProvider *)provider;
	(void)settings;
	(void)system;
	self->calls++;
	/* Copy locks must reach the transport unchanged and reject a single wrong
	 * digit even when the rest of the sentence and telephone URI are correct. */
	if (self->mode >= 5) {
		g_assert_nonnull(strstr(prompt, "Call Example Plumbing at (202) 555-0147."));
		g_assert_nonnull(strstr(system, "CLIENT COPY LOCK"));
		if (self->calls == 1)
			return g_strdup("{\"files\":[{\"path\":\"index.html\",\"content\":"
				"\"Call Example Plumbing at (202) 555-0148. href=\\\"tel:+12025550147\\\"\"},"
				"{\"path\":\"rejected\",\"content\":\"draft\"}]}");
		if (self->mode == 5) {
			g_assert_nonnull(strstr(prompt, "PREVIOUS ATTEMPT REJECTED"));
			g_assert_nonnull(strstr(prompt, "failed contains predicate 1"));
		}
		g_assert_null(strstr(prompt, "\"path\":\"rejected\""));
		return g_strdup("{\"files\":[{\"path\":\"index.html\",\"content\":"
			"\"Call Example Plumbing at (202) 555-0147. href=\\\"tel:+12025550147\\\"\"}]}");
	}
	if (self->mode >= 3) {
		if (self->calls == 1)
			return g_strdup("{\"files\":[{\"path\":\"index.html\",\"content\":\"TODO\"},{\"path\":\"rejected\",\"content\":\"draft\"}]}");
		if (self->mode == 3)
			g_assert_nonnull(strstr(prompt, "PREVIOUS ATTEMPT REJECTED"));
		g_assert_nonnull(strstr(prompt, "contains"));
		g_assert_null(strstr(prompt, "\"path\":\"rejected\""));
		return g_strdup("{\"files\":[{\"path\":\"index.html\",\"content\":\"Ready\"}]}");
	}
	if (self->mode == 0)
		return NULL;
	g_set_error_literal(error, LS_ERROR, LS_ERROR_PROVIDER, "Extension error");
	if (self->mode == 2)
		g_cancellable_cancel(cancellable);
	return g_strdup("{\"files\":[]}");
}
static void
provider_init(LsProviderInterface *iface){
	iface->generate = generate;
}
static void
contract_provider_class_init(ContractProviderClass *klass){
	(void)klass;
}
static void
contract_provider_init(ContractProvider *self){
	(void)self;
}

typedef struct { LsStep parent_instance; } ContractStep;
typedef struct { LsStepClass parent_class; } ContractStepClass;
GType contract_step_get_type(void);
G_DEFINE_TYPE(ContractStep, contract_step, LS_TYPE_STEP)
static gboolean
execute(LsStep *self, LsRecipe *recipe, JsonObject *config, LsPrompts *prompts,
	LsContext *context, LsProvider *provider, GCancellable *cancellable, GError **error){
	gint64 mode = json_object_get_int_member(config, "mode");
	(void)self;
	(void)recipe;
	(void)prompts;
	(void)context;
	(void)provider;
	if (mode == 0)
		return FALSE;
	g_set_error_literal(error, LS_ERROR, LS_ERROR_INVALID, "Extension error");
	if (mode == 2)
		g_cancellable_cancel(cancellable);
	return TRUE;
}
static void
contract_step_class_init(ContractStepClass *klass){
	LS_STEP_CLASS(klass)->execute = execute;
}
static void
contract_step_init(ContractStep *self){
	(void)self;
}

/* Errors take precedence over success; cancellation takes precedence over
 * extension-specific errors. Verify the exact same result with error == NULL. */
static void
extension_contracts(void){
	g_autoptr(GObject) provider = g_object_new(contract_provider_get_type(), NULL);
	g_autoptr(LsStep) step = g_object_new(contract_step_get_type(), NULL);
	g_autoptr(JsonObject) config = json_object_new();
	guint mode, discard;
	for (mode = 0; mode < 3; mode++) {
		for (discard = 0; discard < 2; discard++) {
			g_autoptr(GError) error = NULL;
			g_autoptr(GCancellable) cancel = g_cancellable_new();
			g_autofree gchar *response = NULL;
			((ContractProvider *)provider)->mode = mode;
			response = ls_provider_generate(LS_PROVIDER(provider), config, "", "", cancel,
				discard ? NULL : &error);
			g_assert_null(response);
			if (!discard) {
				if (mode == 2)
					g_assert_error(error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
				else
					g_assert_error(error, LS_ERROR, LS_ERROR_PROVIDER);
			}
			g_clear_error(&error);
			g_cancellable_reset(cancel);
			json_object_set_int_member(config, "mode", mode);
			g_assert_false(ls_step_execute(step, NULL, config, NULL, NULL,
				LS_PROVIDER(provider), cancel, discard ? NULL : &error));
			if (!discard) {
				if (mode == 2)
					g_assert_error(error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
				else
					g_assert_error(error, LS_ERROR, LS_ERROR_INVALID);
			}
		}
	}
}

/* JSON schema validation must not accept permissive JavaScript syntax or
 * delimiter-containing member names. Every failure leaves the store intact. */
static void
invalid_json(gconstpointer data){
	g_autoptr(LsContext) context = ls_context_new();
	g_autoptr(GError) error = NULL;
	g_autofree gchar *snapshot = NULL;
	g_assert_true(ls_context_apply(context, "{\"files\":[]}", NULL));
	g_assert_false(ls_context_apply(context, (const gchar *)data, &error));
	g_assert_nonnull(error);
	snapshot = ls_context_snapshot(context);
	g_assert_cmpstr(snapshot, ==, "{\"files\":[]}");
}

/* Malformed native predicates must fail before even the first transport call. */
static void
invalid_options(gconstpointer data){
	g_autofree gchar *yaml = g_strdup_printf(
		"version: 1\nname: contract\nsite: {brief: contract}\npipeline:\n  steps:\n"
		"    - {id: generate, type: frontend}\n"
		"    - {id: check, type: validate, options: %s}\n", (const gchar *)data);
	g_autoptr(GError) error = NULL;
	g_autoptr(LsRecipe) recipe = ls_recipe_new(yaml, &error);
	g_autoptr(LsPrompts) prompts = ls_prompts_new(NULL, NULL);
	g_autoptr(GObject) provider = g_object_new(contract_provider_get_type(), NULL);
	g_autoptr(LsPipeline) pipeline = ls_pipeline_new(LS_PROVIDER(provider), prompts);
	g_assert_no_error(error);
	g_assert_nonnull(recipe);
	g_assert_false(ls_pipeline_validate(pipeline, recipe, &error));
	g_assert_error(error, LS_ERROR, LS_ERROR_INVALID);
	g_assert_cmpuint(((ContractProvider *)provider)->calls, ==, 0);
}

/* Boundaries are byte-based, not character counts; predicates are exact,
 * case-sensitive literals. Check every failure against a passing candidate. */
static void
content_checks(void){
	const gchar *invalid[] = {
		"{\"files\":[{\"path\":\"index.html\",\"content\":\"short\"}]}",
		"{\"files\":[{\"path\":\"index.html\",\"content\":\"Ready but TODO\"}]}",
		"{\"files\":[{\"path\":\"index.html\",\"content\":\"ready     \"}]}",
		"{\"files\":[{\"path\":\"index.html\",\"content\":\"Ready long text\"}]}",
		"{\"files\":[{\"path\":\"index.html\",\"content\":\"ReadyTODO \"}]}",
		"{\"files\":[{\"path\":\"index.html\",\"content\":\"          \"}]}",
		"{\"files\":[]}"
	};
	g_autoptr(JsonNode) parsed = ls_json_parse(
		"{\"options\":{\"required\":[\"index.html\"],\"checks\":[{\"path\":\"index.html\","
		"\"contains\":[\"Ready\"],\"excludes\":[\"TODO\"],\"min_bytes\":10,\"max_bytes\":10}]}}", NULL);
	g_autoptr(LsStep) step = g_object_new(LS_TYPE_VALIDATE_STEP, NULL);
	guint i;
	for (i = 0; i <= G_N_ELEMENTS(invalid); i++) {
		g_autoptr(LsContext) context = ls_context_new();
		g_autoptr(GError) error = NULL;
		g_assert_true(ls_context_apply(context, i == G_N_ELEMENTS(invalid)
			? "{\"files\":[{\"path\":\"index.html\",\"content\":\"Ready éé\"}]}" : invalid[i], NULL));
		if (i == G_N_ELEMENTS(invalid)) {
			g_assert_true(ls_step_execute(step, NULL, json_node_get_object(parsed),
				NULL, context, NULL, NULL, &error));
			g_assert_no_error(error);
		} else {
			g_assert_false(ls_step_execute(step, NULL, json_node_get_object(parsed),
				NULL, context, NULL, NULL, &error));
			g_assert_error(error, LS_ERROR, LS_ERROR_INVALID);
		}
	}
	/* Unicode spaces also fail required-only validation, without byte rules. */
	{
		g_autoptr(LsContext) context = ls_context_new();
		g_autoptr(GError) error = NULL;
		JsonObject *options = json_object_get_object_member(json_node_get_object(parsed), "options");
		json_object_remove_member(options, "checks");
		g_assert_true(ls_context_apply(context,
			"{\"files\":[{\"path\":\"index.html\",\"content\":\"\\u2003\\t\\n\"}]}", NULL));
		g_assert_false(ls_step_execute(step, NULL, json_node_get_object(parsed),
			NULL, context, NULL, NULL, &error));
		g_assert_error(error, LS_ERROR, LS_ERROR_INVALID);
	}
}

/* A sentence lock covers words, punctuation and digits; a separate URI lock
 * catches a correct display number linked to the wrong destination. */
static void
copy_lock_checks(void){
	const gchar *contents[] = {
		"Call Example Plumbing at (202) 555-0147. href=\"tel:+12025550147\"",
		"Call Example Plumbing at (202) 555-0148. href=\"tel:+12025550147\"",
		"Phone Example Plumbing at (202) 555-0147. href=\"tel:+12025550147\"",
		"Call Example Plumbing at (202) 555-0147! href=\"tel:+12025550147\"",
		"Call Example Plumbing at (202) 555-0147. href=\"tel:+12025550148\"",
		"href=\"tel:+12025550147\""
	};
	g_autoptr(JsonNode) config = ls_yaml_parse(
		"options:\n  required: [index.html]\n  checks:\n    - path: index.html\n"
		"      contains:\n        - 'Call Example Plumbing at (202) 555-0147.'\n"
		"        - 'href=\"tel:+12025550147\"'\n", NULL);
	g_autoptr(LsStep) step = g_object_new(LS_TYPE_VALIDATE_STEP, NULL);
	guint i;
	for (i = 0; i < G_N_ELEMENTS(contents); i++) {
		g_autoptr(LsContext) context = ls_context_new();
		g_autoptr(GError) error = NULL;
		g_autofree gchar *escaped = g_strescape(contents[i], NULL);
		g_autofree gchar *bundle = g_strdup_printf(
			"{\"files\":[{\"path\":\"index.html\",\"content\":\"%s\"}]}", escaped);
		g_assert_true(ls_context_apply(context, bundle, &error));
		g_assert_no_error(error);
		g_assert_cmpint(ls_step_execute(step, NULL, json_node_get_object(config),
			NULL, context, NULL, NULL, &error), ==, i == 0);
		if (i == 0)
			g_assert_no_error(error);
		else {
			g_assert_error(error, LS_ERROR, LS_ERROR_INVALID);
			g_assert_nonnull(strstr(error->message, i == 4
				? "contains predicate 2" : "contains predicate 1"));
		}
	}
}

/* Remove only this test's private temporary tree, including the Git repository
 * created during publication. Never follow links while descending. */
static void
remove_tree(const gchar *path){
	if (g_file_test(path, G_FILE_TEST_IS_DIR) && !g_file_test(path, G_FILE_TEST_IS_SYMLINK)) {
		g_autoptr(GDir) directory = g_dir_open(path, 0, NULL);
		const gchar *name;
		g_assert_nonnull(directory);
		while ((name = g_dir_read_name(directory)) != NULL) {
			g_autofree gchar *child = g_build_filename(path, name, NULL);
			remove_tree(child);
		}
		g_clear_pointer(&directory, g_dir_close);
		g_assert_cmpint(g_rmdir(path), ==, 0);
	} else
		g_assert_cmpint(g_unlink(path), ==, 0);
}

/* A syntactically valid but contract-invalid bundle is retried transactionally
 * and only the corrected candidate reaches the published project/checkpoint. */
static void
content_retry(gconstpointer data){
	guint scenario = GPOINTER_TO_UINT(data);
	gboolean resume = (scenario % 2) != 0;
	gboolean copy_lock = scenario >= 2;
	const gchar *expected = copy_lock
		? "Call Example Plumbing at (202) 555-0147. href=\"tel:+12025550147\"" : "Ready";
	g_autofree gchar *yaml = g_strdup_printf(
		"version: 1\nname: retry\nsite: {brief: contract}\nai: {retries: %u}\npipeline:\n  steps:\n"
		"    - {id: generate, type: frontend}\n"
		"    - id: check\n      type: validate\n      options:\n        required: [index.html]\n"
		"        checks: [{path: index.html, contains: %s, excludes: [TODO]}]\n", resume ? 0 : 1,
		copy_lock ? "['Call Example Plumbing at (202) 555-0147.', 'href=\"tel:+12025550147\"']"
			: "[Ready]");
	g_autoptr(LsRecipe) recipe = ls_recipe_new(yaml, NULL);
	g_autoptr(LsPrompts) prompts = ls_prompts_new(NULL, NULL);
	g_autoptr(GObject) provider = g_object_new(contract_provider_get_type(), NULL);
	g_autoptr(LsPipeline) pipeline = ls_pipeline_new(LS_PROVIDER(provider), prompts);
	g_autoptr(GError) error = NULL;
	g_autofree gchar *temp = g_dir_make_tmp("libresite-contract-XXXXXX", NULL);
	g_autofree gchar *output = g_build_filename(temp, "site", NULL);
	g_autofree gchar *index = g_build_filename(output, "index.html", NULL);
	g_autofree gchar *rejected = g_build_filename(output, "rejected", NULL);
	g_autofree gchar *metadata_dir = g_build_filename(output, ".libresite", NULL);
	g_autofree gchar *metadata = g_build_filename(metadata_dir, "run.json", NULL);
	g_autofree gchar *git_head = g_build_filename(output, ".git", "HEAD", NULL);
	g_autofree gchar *work = g_strconcat(output, ".libresite-work", NULL);
	g_autofree gchar *checkpoint = g_build_filename(work, "checkpoint.json", NULL);
	g_autofree gchar *text = NULL;
	((ContractProvider *)provider)->mode = (resume ? 4 : 3) + (copy_lock ? 2 : 0);
	if (resume) {
		g_autoptr(JsonNode) saved = NULL;
		g_assert_false(ls_pipeline_run(pipeline, recipe, output, FALSE, NULL, &error));
		g_assert_error(error, LS_ERROR, LS_ERROR_INVALID);
		g_clear_error(&error);
		g_assert_false(g_file_test(output, G_FILE_TEST_EXISTS));
		g_assert_true(g_file_get_contents(checkpoint, &text, NULL, NULL));
		saved = ls_json_parse(text, NULL);
		g_assert_cmpint(json_object_get_int_member(json_node_get_object(saved), "next"), ==, 0);
		g_assert_cmpuint(json_array_get_length(json_object_get_array_member(
			json_node_get_object(saved), "files")), ==, 0);
		g_clear_pointer(&text, g_free);
	}
	g_assert_true(ls_pipeline_run(pipeline, recipe, output, resume, NULL, &error));
	g_assert_no_error(error);
	g_assert_cmpuint(((ContractProvider *)provider)->calls, ==, 2);
	g_assert_false(g_file_test(work, G_FILE_TEST_EXISTS));
	g_assert_true(g_file_get_contents(index, &text, NULL, NULL));
	g_assert_cmpstr(text, ==, expected);
	g_assert_false(g_file_test(rejected, G_FILE_TEST_EXISTS));
	g_clear_pointer(&text, g_free);
	g_assert_true(g_file_get_contents(metadata, &text, NULL, NULL));
	g_assert_null(strstr(text, "rejected"));
	g_assert_null(strstr(text, "TODO"));
	g_assert_true(g_file_test(git_head, G_FILE_TEST_IS_REGULAR));
	remove_tree(temp);
}

/* Quoted and block scalars retain string semantics through nested conversion;
 * plain typed values still resolve as before. This covers discarded null text. */
static void
yaml_strings(void){
	g_autoptr(GError) error = NULL;
	g_autoptr(JsonNode) parsed = ls_yaml_parse(
		"strings: ['1', \"true\", 'null', '', '~', '1.5']\n"
		"block: |-\n  123\nnested: {value: 'false'}\nnumber: 123\nboolean: true\n", &error);
	const gchar *expected[] = {"1", "true", "null", "", "~", "1.5"};
	JsonObject *root;
	JsonArray *strings;
	guint i;
	g_assert_no_error(error);
	root = json_node_get_object(parsed);
	strings = json_object_get_array_member(root, "strings");
	for (i = 0; i < G_N_ELEMENTS(expected); i++) {
		JsonNode *node = json_array_get_element(strings, i);
		g_assert_cmpuint(json_node_get_value_type(node), ==, G_TYPE_STRING);
		g_assert_cmpstr(json_node_get_string(node), ==, expected[i]);
	}
	g_assert_cmpstr(ls_string(root, "block"), ==, "123");
	g_assert_cmpstr(ls_string(json_object_get_object_member(root, "nested"), "value"), ==, "false");
	g_assert_cmpint(json_object_get_int_member(root, "number"), ==, 123);
	g_assert_true(json_object_get_boolean_member(root, "boolean"));
}

/* Individually reported negative cases keep preflight coverage inspectable. */
/* Form emission must preserve exact text and types through the same parser
 * used by generation, including punctuation and YAML-looking copy. */
static void
structured_form(void){
	const gchar *form = "{\"name\":\"studio\",\"site\":{"
		"\"pages\":[{\"path\":\"/\",\"title\":\"Home\",\"sections\":[\"Hello\"]}],"
		"\"offers\":[{\"id\":\"one\",\"name\":\"Support\",\"price\":\"001.00\"}],"
		"\"contact\":{\"phone\":\"001234\"},\"brand\":{\"name\":\"Oui\"},"
		"\"copy_lock\":[\"null\",\"true\",\"001\",\" café: \\\"oui\\\"\\nline 2 \",\"A & B\"],"
		"\"languages\":{\"default\":\"en\",\"supported\":[\"en\",\"fr-CA\"]}}}";
	g_autoptr(GError) error = NULL;
	g_autoptr(LsRecipe) recipe = ls_recipe_from_form(form, &error);
	g_autoptr(LsRecipe) reloaded = NULL;
	g_autoptr(LsPrompts) prompts = ls_prompts_new(NULL, &error);
	g_autoptr(LsContext) context = ls_context_new();
	g_autofree gchar *yaml = NULL;
	g_autofree gchar *rendered = NULL;
	JsonObject *site;
	g_assert_no_error(error);
	g_assert_nonnull(recipe);
	ls_recipe_set_unlimited(recipe, TRUE);
	yaml = ls_recipe_to_yaml(recipe, &error);
	g_assert_no_error(error);
	g_assert_nonnull(strstr(yaml, "pages:"));
	reloaded = ls_recipe_new(yaml, &error);
	g_assert_no_error(error);
	g_assert_true(json_object_equal(ls_recipe_get_root(recipe), ls_recipe_get_root(reloaded)));
	site = json_object_get_object_member(ls_recipe_get_root(reloaded), "site");
	g_assert_false(json_object_has_member(site, "brief"));
	g_assert_cmpstr(json_array_get_string_element(json_object_get_array_member(site, "copy_lock"), 3),
		==, " café: \"oui\"\nline 2 ");
	rendered = ls_prompts_render(prompts, reloaded,
		json_array_get_object_element(ls_recipe_get_steps(reloaded), 0), context, &error);
	g_assert_no_error(error);
	g_assert_nonnull(strstr(rendered, "copy_lock"));
	g_assert_nonnull(strstr(rendered, "001234"));
	g_assert_nonnull(strstr(ls_prompts_get_system(prompts), "STRUCTURED SITE CONTRACT"));
}
/* Mutate one valid form at a time so every failure exercises a schema rule. */
static void
structured_invalid(void){
	const gchar *mutations[] = {
		"pages: []", "pages: null", "pages: [false]", "pages: [{path: relative, title: Home}]",
		"pages: [{path: /../a, title: Home}]", "pages: [{path: '//host', title: Home}]",
		"pages: [{path: /, title: Home}, {path: /, title: Again}]",
		"pages: [{path: /, title: Home, sections: [a, a]}]",
		"pages: [{path: /, title: Home, typo: no}]",
		"offers: [{id: x, name: X, price: 100}]",
		"offers: [{id: x, name: X, cta_label: Buy}]",
		"offers: [{id: x, name: X}, {id: x, name: Y}]",
		"offers: {}", "contact: {email: null}", "contact: {typo: x}",
		"brand: {}", "brand: {name: true}", "copy_lock: [x, x]", "copy_lock: [false]",
		"languages: {default: en, supported: [fr]}",
		"languages: {default: en, supported: [en, EN]}",
		"languages: {default: en_US, supported: [en_US]}",
		"languages: {default: en, supported: []}", "languages: null"
	};
	guint i;
	for (i = 0; i < G_N_ELEMENTS(mutations); i++) {
		g_autoptr(GError) error = NULL;
		g_autoptr(JsonNode) base = ls_yaml_parse(
			"name: studio\nsite: {pages: [{path: /, title: Home}], offers: [], contact: {}, "
			"brand: {name: Studio}, copy_lock: [], languages: {default: en, supported: [en]}}", &error);
		g_autoptr(JsonNode) change = ls_yaml_parse(mutations[i], &error);
		g_autoptr(GList) keys = json_object_get_members(json_node_get_object(change));
		JsonObject *site = json_object_get_object_member(json_node_get_object(base), "site");
		const gchar *key = (const gchar *)keys->data;
		g_autofree gchar *encoded = NULL;
		g_autoptr(LsRecipe) recipe = NULL;
		json_object_set_member(site, key,
			json_node_copy(json_object_get_member(json_node_get_object(change), key)));
		encoded = ls_json_encode(base);
		recipe = ls_recipe_from_form(encoded, &error);
		g_assert_null(recipe);
		g_assert_error(error, LS_ERROR, LS_ERROR_INVALID);
	}
	{
		g_autoptr(GError) error = NULL;
		g_autoptr(LsRecipe) recipe = ls_recipe_from_form(
			"{\"name\":\"x\",\"site\":{\"brief\":\"prose\"}}", &error);
		g_assert_null(recipe);
		g_assert_error(error, LS_ERROR, LS_ERROR_INVALID);
	}
}
int
main(int argc, char **argv){
	const gchar *json[] = {
		"", "   ", "/* empty */", "// comment\n{\"files\":[]}",
		"var bundle = {\"files\":[]};", "{\"files\":[],}",
		"{\"files\":[],\"files|summary\":0}",
		"{\"files\":[{\"path\":\"a\",\"content\":\"b\",\"path|content\":0}]}"
	};
	const gchar *options[] = {
		"{}", "{required: []}", "{required: [a, a]}", "{required: [../a]}",
		"{required: [a], checks: null}", "{required: [a], checks: []}",
		"{required: [a], checks: [null]}", "{required: [a], checks: [{path: a}]}",
		"{required: [a], checks: [{path: ../a, min_bytes: 1}]}",
		"{required: [a], checks: [{path: a, contains: []}]}",
		"{required: [a], checks: [{path: a, contains: ['']}]}",
		"{required: [a], checks: [{path: a, contains: [x, x]}]}",
		"{required: [a], checks: [{path: a, contains: [true]}]}",
		"{required: [a], checks: [{path: a, contains: x}]}",
		"{required: [a], checks: [{path: a, excludes: [null]}]}",
		"{required: [a], checks: [{path: a, min_bytes: -1}]}",
		"{required: [a], checks: [{path: a, max_bytes: 16777217}]}",
		"{required: [a], checks: [{path: a, min_bytes: 2, max_bytes: 1}]}",
		"{required: [a], checks: [{path: a, min_bytes: 1.5}]}",
		"{required: [a], checks: [{path: a, max_bytes: '1'}]}",
		"{required: [a], checks: [{path: a, max_bytes: 1}, {path: a, max_bytes: 2}]}",
		"{required: [a], checks: [{path: a, contain: [x]}]}",
		"{required: [a], 'required|checks': []}"
	};
	guint i;
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/contracts/structured-form", structured_form);
	g_test_add_func("/contracts/structured-invalid", structured_invalid);
	g_test_add_func("/contracts/extensions", extension_contracts);
	g_test_add_func("/contracts/content", content_checks);
	g_test_add_func("/contracts/copy-lock/literals", copy_lock_checks);
	g_test_add_data_func("/contracts/copy-lock/retry", GUINT_TO_POINTER(2), content_retry);
	g_test_add_data_func("/contracts/copy-lock/resume", GUINT_TO_POINTER(3), content_retry);
	g_test_add_func("/contracts/yaml-strings", yaml_strings);
	g_test_add_data_func("/contracts/retry", GINT_TO_POINTER(FALSE), content_retry);
	g_test_add_data_func("/contracts/resume", GINT_TO_POINTER(TRUE), content_retry);
	for (i = 0; i < G_N_ELEMENTS(json); i++) {
		g_autofree gchar *name = g_strdup_printf("/contracts/json/%u", i);
		g_test_add_data_func(name, json[i], invalid_json);
	}
	for (i = 0; i < G_N_ELEMENTS(options); i++) {
		g_autofree gchar *name = g_strdup_printf("/contracts/options/%u", i);
		g_test_add_data_func(name, options[i], invalid_options);
	}
	return g_test_run();
}
