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
	if (self->mode == 7) {
		if (self->calls == 1)
			return g_strdup("{\"files\":[{\"path\":\"run.sh\",\"content\":"
				"\"#!/usr/bin/env bash\\necho $1\\n\"}]}");
		g_assert_nonnull(strstr(prompt, "PREVIOUS ATTEMPT REJECTED"));
		g_assert_nonnull(strstr(prompt, "run.sh"));
		g_assert_nonnull(strstr(prompt, "SC2086"));
		return g_strdup("{\"files\":[{\"path\":\"run.sh\",\"content\":"
			"\"#!/usr/bin/env bash\\nprintf '%s\\\\n' \\\"$1\\\"\\n\"}]}");
	}
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
			g_assert_error(error, LS_ERROR, LS_ERROR_VALIDATION);
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
		g_assert_error(error, LS_ERROR, LS_ERROR_VALIDATION);
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
			g_assert_error(error, LS_ERROR, LS_ERROR_VALIDATION);
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
		g_assert_error(error, LS_ERROR, LS_ERROR_VALIDATION);
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
	const gchar *form = "{\"name\":\"studio\","
		"\"seed_artifacts\":[{\"path\":\"assets/brand.txt\",\"content\":\"supplied brand asset\"}],\"site\":{"
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
	g_clear_object(&context);
	context = ls_recipe_create_context(reloaded, &error);
	g_assert_no_error(error);
	rendered = ls_prompts_render(prompts, reloaded,
		json_array_get_object_element(ls_recipe_get_steps(reloaded), 1), context, &error);
	g_assert_no_error(error);
	g_assert_nonnull(strstr(rendered, "copy_lock"));
	g_assert_nonnull(strstr(rendered, "001234"));
	g_assert_nonnull(strstr(rendered, "supplied brand asset"));
	g_assert_nonnull(strstr(ls_prompts_get_system(prompts), "STRUCTURED SITE CONTRACT"));
	/* The default form pipeline delivers a static site: no developer files in
	 * acceptance, and build notes pruned after it. */
	{
		JsonArray *steps = ls_recipe_get_steps(reloaded);
		guint count = json_array_get_length(steps);
		JsonObject *last = json_array_get_object_element(steps, count - 1);
		JsonObject *acceptance = json_array_get_object_element(steps, count - 2);
		g_autofree gchar *required = ls_json_encode(json_object_get_member(
			json_object_get_object_member(acceptance, "options"), "required"));
		g_assert_cmpstr(ls_string(last, "type"), ==, "prune");
		g_assert_cmpstr(ls_string(acceptance, "type"), ==, "validate");
		g_assert_null(strstr(required, "AGENTS.md"));
		g_assert_null(strstr(required, "README.org"));
		g_assert_null(strstr(required, "tests/"));
		g_assert_nonnull(strstr(required, "robots.txt"));
	}
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
/* Exercise native emission directly with a transport that fails if called.
 * Seeds, cancellation and a directory collision must never partially mutate it. */
static void
scaffold_contract(void){
	g_autoptr(GError) error = NULL;
	g_autoptr(LsRecipe) recipe = ls_recipe_new(
		"version: 1\nname: scaffold\nsite: {brief: Static site}\n"
		"pipeline: {steps: [{id: scaffold, type: scaffold}]}\n", &error);
	g_autoptr(LsPrompts) prompts = ls_prompts_new(NULL, &error);
	g_autoptr(LsProvider) provider = g_object_new(contract_provider_get_type(), NULL);
	g_autoptr(LsStep) step = g_object_new(LS_TYPE_SCAFFOLD_STEP, NULL);
	g_autoptr(LsContext) context = ls_context_new();
	g_autoptr(GCancellable) cancel = g_cancellable_new();
	g_autoptr(JsonObject) options = json_object_new();
	g_autofree gchar *before = NULL;
	g_autofree gchar *after = NULL;
	JsonObject *config = json_array_get_object_element(ls_recipe_get_steps(recipe), 0);
	const gchar *paths[] = {"404.html", "privacy.html", "terms.html"};
	/* Static sites ship only visitor-facing files; no build, test or agent docs. */
	const gchar *absent[] = {"Makefile", "AGENTS.md", "docs/contact.org", "tests/scaffold.sh"};
	guint i;
	g_assert_no_error(error);
	g_assert_true(ls_context_apply(context, "{\"files\":["
		"{\"path\":\"Makefile\",\"content\":\"client build\\n\",\"executable\":true},"
		"{\"path\":\"terms.html\",\"content\":\"Approved client terms\"}]}", &error));
	g_assert_true(ls_step_execute(step, recipe, config, prompts, context, provider, NULL, &error));
	g_assert_no_error(error);
	for (i = 0; i < G_N_ELEMENTS(paths); i++)
		g_assert_nonnull(ls_context_lookup(context, paths[i]));
	for (i = 1; i < G_N_ELEMENTS(absent); i++)
		g_assert_null(ls_context_lookup(context, absent[i]));
	/* A seeded file at a formerly scaffolded path is still the client's. */
	g_assert_cmpstr(ls_artifact_get_content(ls_context_lookup(context, "Makefile")), ==, "client build\n");
	g_assert_cmpstr(ls_artifact_get_content(ls_context_lookup(context, "terms.html")), ==, "Approved client terms");
	before = ls_context_snapshot(context);
	g_assert_true(ls_step_execute(step, recipe, config, prompts, context, provider, NULL, &error));
	after = ls_context_snapshot(context);
	g_assert_cmpstr(before, ==, after);
	g_assert_cmpuint(((ContractProvider *)provider)->calls, ==, 0);

	/* A cancelled call emits nothing even when every template is missing. */
	g_clear_object(&context);
	context = ls_context_new();
	g_cancellable_cancel(cancel);
	g_assert_false(ls_step_execute(step, recipe, config, prompts, context, provider, cancel, &error));
	g_assert_error(error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
	g_clear_error(&error);
	g_assert_null(ls_context_lookup(context, "Makefile"));
	g_assert_true(ls_context_apply(context,
		"{\"files\":[{\"path\":\"404.html/client\",\"content\":\"client file\"}]}", &error));
	g_clear_pointer(&before, g_free);
	before = ls_context_snapshot(context);
	g_assert_false(ls_step_execute(step, recipe, config, prompts, context, provider, NULL, &error));
	g_assert_nonnull(error);
	g_clear_error(&error);
	g_clear_pointer(&after, g_free);
	after = ls_context_snapshot(context);
	g_assert_cmpstr(before, ==, after);

	/* Options have a closed schema and do not coerce scalar types. */
	g_assert_true(ls_scaffold_step_check_options(NULL, &error));
	json_object_set_string_member(options, "profile", "static");
	g_assert_true(ls_scaffold_step_check_options(options, &error));
	json_object_set_string_member(options, "profile", "application");
	g_assert_false(ls_scaffold_step_check_options(options, &error));
	g_assert_error(error, LS_ERROR, LS_ERROR_INVALID);
	g_clear_error(&error);
	json_object_set_boolean_member(options, "profile", TRUE);
	g_assert_false(ls_scaffold_step_check_options(options, &error));
	g_clear_error(&error);
	json_object_remove_member(options, "profile");
	json_object_set_boolean_member(options, "overwrite", TRUE);
	g_assert_false(ls_scaffold_step_check_options(options, &error));
	g_assert_error(error, LS_ERROR, LS_ERROR_INVALID);
}

/* Every scaffold page carries its own nonempty meta description: generated SEO
 * contracts check each HTML route, including noindex drafts and 404. */
static void
scaffold_metadata(void){
	g_autoptr(GError) error = NULL;
	g_autoptr(LsRecipe) recipe = ls_recipe_new(
		"version: 1\nname: scaffold\nsite: {brief: Static site}\n"
		"pipeline: {steps: [{id: scaffold, type: scaffold}]}\n", &error);
	g_autoptr(LsPrompts) prompts = ls_prompts_new(NULL, &error);
	g_autoptr(LsProvider) provider = g_object_new(contract_provider_get_type(), NULL);
	g_autoptr(LsStep) step = g_object_new(LS_TYPE_SCAFFOLD_STEP, NULL);
	g_autoptr(LsContext) context = ls_context_new();
	g_autoptr(GHashTable) seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	JsonObject *config = json_array_get_object_element(ls_recipe_get_steps(recipe), 0);
	const gchar *pages[] = {"404.html", "privacy.html", "terms.html"};
	const gchar *marker = "<meta name=\"description\" content=\"";
	guint i;
	g_assert_no_error(error);
	g_assert_true(ls_step_execute(step, recipe, config, prompts, context, provider, NULL, &error));
	g_assert_no_error(error);
	for (i = 0; i < G_N_ELEMENTS(pages); i++) {
		const gchar *html = ls_artifact_get_content(ls_context_lookup(context, pages[i]));
		const gchar *start = strstr(html, marker);
		const gchar *end;
		g_autofree gchar *description = NULL;
		g_assert_nonnull(start);
		start += strlen(marker);
		end = strchr(start, '"');
		g_assert_nonnull(end);
		description = g_strndup(start, (gsize)(end - start));
		g_strstrip(description);
		g_assert_cmpuint(strlen(description), >, 0);
		g_assert_true(g_hash_table_add(seen, g_steal_pointer(&description)));
		g_assert_nonnull(strstr(html, "<meta name=\"robots\" content=\"noindex\">"));
	}
}

/* Run a validate step with one ShellCheck rule against a single artifact. */
static gboolean
shellcheck_one(const gchar *bundle, GError **error){
	g_autoptr(JsonNode) parsed = ls_json_parse(
		"{\"options\":{\"required\":[\"run.sh\"],"
		"\"checks\":[{\"path\":\"run.sh\",\"shellcheck\":true}]}}", NULL);
	g_autoptr(LsStep) step = g_object_new(LS_TYPE_VALIDATE_STEP, NULL);
	g_autoptr(LsContext) context = ls_context_new();
	g_assert_true(ls_context_apply(context, bundle, NULL));
	return ls_step_execute(step, NULL, json_node_get_object(parsed), NULL, context, NULL, NULL, error);
}

/* ShellCheck reads the emitted script, never runs it, and reports findings
 * with the artifact path so a retry prompt can name the defect. */
static void
shellcheck_checks(void){
	g_autoptr(GError) error = NULL;
	g_assert_true(shellcheck_one("{\"files\":[{\"path\":\"run.sh\",\"content\":"
		"\"#!/usr/bin/env bash\\nset -euo pipefail\\nprintf '%s\\\\n' \\\"$1\\\"\\n\"}]}", &error));
	g_assert_no_error(error);
	g_assert_false(shellcheck_one("{\"files\":[{\"path\":\"run.sh\",\"content\":"
		"\"#!/usr/bin/env bash\\necho $1\\n\"}]}", &error));
	g_assert_error(error, LS_ERROR, LS_ERROR_VALIDATION);
	g_assert_nonnull(strstr(error->message, "run.sh"));
	g_assert_nonnull(strstr(error->message, "SC2086"));
	g_clear_error(&error);
	/* No shebang: the shell dialect is unknown, which is itself a defect. */
	g_assert_false(shellcheck_one("{\"files\":[{\"path\":\"run.sh\",\"content\":\"echo hi\\n\"}]}", &error));
	g_assert_error(error, LS_ERROR, LS_ERROR_VALIDATION);
	g_assert_nonnull(strstr(error->message, "SC2148"));
	g_clear_error(&error);
	g_assert_false(shellcheck_one("{\"files\":[{\"path\":\"run.sh\",\"content\":\"AA==\","
		"\"encoding\":\"base64\"}]}", &error));
	g_assert_error(error, LS_ERROR, LS_ERROR_VALIDATION);
}

/* Neither a .shellcheckrc in the working directory nor SHELLCHECK_OPTS may
 * relax the check. Runs in a child so chdir/setenv precede any thread. */
static void
shellcheck_isolated(void){
	g_autoptr(GError) error = NULL;
	g_autofree gchar *temp = NULL;
	g_autofree gchar *rc = NULL;
	if (!g_test_subprocess()) {
		g_test_trap_subprocess(NULL, 0, G_TEST_SUBPROCESS_INHERIT_STDERR);
		g_test_trap_assert_passed();
		return;
	}
	temp = g_dir_make_tmp("libresite-shellcheck-XXXXXX", NULL);
	rc = g_build_filename(temp, ".shellcheckrc", NULL);
	g_assert_true(g_file_set_contents(rc, "disable=SC2086\n", -1, NULL));
	g_assert_cmpint(g_chdir(temp), ==, 0);
	g_assert_true(g_setenv("SHELLCHECK_OPTS", "--exclude=SC2086", TRUE));
	g_assert_false(shellcheck_one("{\"files\":[{\"path\":\"run.sh\",\"content\":"
		"\"#!/usr/bin/env bash\\necho $1\\n\"}]}", &error));
	g_assert_error(error, LS_ERROR, LS_ERROR_VALIDATION);
	(void)g_unlink(rc);
	(void)g_rmdir(temp);
}

/* A missing ShellCheck fails preflight, before any paid generation. */
static void
shellcheck_missing(void){
	g_autoptr(GError) error = NULL;
	g_autoptr(JsonNode) options = NULL;
	g_autofree gchar *empty = NULL;
	if (!g_test_subprocess()) {
		g_test_trap_subprocess(NULL, 0, G_TEST_SUBPROCESS_INHERIT_STDERR);
		g_test_trap_assert_passed();
		return;
	}
	empty = g_dir_make_tmp("libresite-nopath-XXXXXX", NULL);
	g_assert_true(g_setenv("PATH", empty, TRUE));
	options = ls_json_parse("{\"required\":[\"run.sh\"],"
		"\"checks\":[{\"path\":\"run.sh\",\"shellcheck\":true}]}", NULL);
	g_assert_false(ls_validate_step_check_options(json_node_get_object(options), &error));
	g_assert_error(error, LS_ERROR, LS_ERROR_INVALID);
	g_assert_nonnull(strstr(error->message, "ShellCheck"));
	(void)g_rmdir(empty);
}

/* ShellCheck findings reach the model through the existing corrective retry. */
static void
shellcheck_retry(void){
	g_autoptr(LsRecipe) recipe = ls_recipe_new(
		"version: 1\nname: lint\nsite: {brief: contract}\nai: {retries: 1}\npipeline:\n  steps:\n"
		"    - {id: generate, type: frontend}\n"
		"    - id: check\n      type: validate\n      options:\n        required: [run.sh]\n"
		"        checks: [{path: run.sh, shellcheck: true}]\n", NULL);
	g_autoptr(LsPrompts) prompts = ls_prompts_new(NULL, NULL);
	g_autoptr(GObject) provider = g_object_new(contract_provider_get_type(), NULL);
	g_autoptr(LsPipeline) pipeline = ls_pipeline_new(LS_PROVIDER(provider), prompts);
	g_autoptr(GError) error = NULL;
	g_autofree gchar *temp = g_dir_make_tmp("libresite-lint-XXXXXX", NULL);
	g_autofree gchar *output = g_build_filename(temp, "site", NULL);
	g_autofree gchar *script = g_build_filename(output, "run.sh", NULL);
	g_autofree gchar *text = NULL;
	g_assert_nonnull(recipe);
	((ContractProvider *)provider)->mode = 7;
	g_assert_true(ls_pipeline_run(pipeline, recipe, output, FALSE, NULL, &error));
	g_assert_no_error(error);
	g_assert_cmpuint(((ContractProvider *)provider)->calls, ==, 2);
	g_assert_true(g_file_get_contents(script, &text, NULL, NULL));
	g_assert_null(strstr(text, "echo $1"));
}

/* Test-only evaluator for the structural keywords used by the bundle schema.
 * Artifact validation still owns paths, bytes and context-dependent operations. */
static gboolean
schema_accepts(JsonObject *schema, JsonNode *value){
	const gchar *type = ls_string(schema, "type");
	guint i;
	if (g_str_equal(type, "object")) {
		JsonObject *object, *properties;
		JsonArray *required;
		g_autoptr(GList) keys = NULL;
		GList *item;
		if (!JSON_NODE_HOLDS_OBJECT(value))
			return FALSE;
		object = json_node_get_object(value);
		properties = json_object_get_object_member(schema, "properties");
		required = json_object_get_array_member(schema, "required");
		g_assert_false(json_object_get_boolean_member(schema, "additionalProperties"));
		for (i = 0; i < json_array_get_length(required); i++)
			if (!json_object_has_member(object, json_array_get_string_element(required, i)))
				return FALSE;
		keys = json_object_get_members(object);
		for (item = keys; item != NULL; item = item->next)
			if (!json_object_has_member(properties, item->data) ||
				!schema_accepts(json_object_get_object_member(properties, item->data),
					json_object_get_member(object, item->data)))
				return FALSE;
		return TRUE;
	}
	if (g_str_equal(type, "array")) {
		JsonArray *array;
		if (!JSON_NODE_HOLDS_ARRAY(value))
			return FALSE;
		array = json_node_get_array(value);
		if (json_array_get_length(array) > json_object_get_int_member(schema, "maxItems"))
			return FALSE;
		for (i = 0; i < json_array_get_length(array); i++)
			if (!schema_accepts(json_object_get_object_member(schema, "items"), json_array_get_element(array, i)))
				return FALSE;
		return TRUE;
	}
	if (g_str_equal(type, "boolean"))
		return json_node_get_value_type(value) == G_TYPE_BOOLEAN;
	g_assert_cmpstr(type, ==, "string");
	if (json_node_get_value_type(value) != G_TYPE_STRING)
		return FALSE;
	if (json_object_has_member(schema, "minLength") &&
		g_utf8_strlen(json_node_get_string(value), -1) < json_object_get_int_member(schema, "minLength"))
		return FALSE;
	if (json_object_has_member(schema, "enum")) {
		JsonArray *values = json_object_get_array_member(schema, "enum");
		for (i = 0; i < json_array_get_length(values); i++)
			if (g_str_equal(json_node_get_string(value), json_array_get_string_element(values, i)))
				return TRUE;
		return FALSE;
	}
	return TRUE;
}
static void
response_schema(void){
	g_autoptr(LsPrompts) prompts = ls_prompts_new(NULL, NULL);
	g_autoptr(GBytes) bytes = g_resources_lookup_data("/org/libresite/file-bundle.schema.json", 0, NULL);
	g_autoptr(JsonNode) schema = NULL;
	const gchar *cases[] = {
		"{\"files\":[]}",
		"{\"files\":[{\"path\":\"a\",\"content\":\"text\",\"encoding\":\"utf8\",\"executable\":true}],"
		"\"delete\":[\"old\"],\"rename\":[{\"from\":\"a\",\"to\":\"b\"}],\"summary\":\"Done\"}",
		"{\"files\":[{\"path\":\"image.png\",\"content\":\"AA==\",\"encoding\":\"base64\"}]}",
		"Prepared your files. {\"files\":[]}", "{}", "{\"files\":null}",
		"{\"files\":[{\"path\":\"a\"}]}", "{\"files\":[],\"summary\":false}",
		"{\"files\":[],\"rename\":[{\"from\":\"a\"}]}", "{\"files\":[],\"delete\":[1]}",
		"{\"files\":[{\"path\":\"a\",\"content\":\"x\",\"encoding\":\"gzip\"}]}",
		"{\"files\":[],\"unexpected\":true}"
	};
	guint i;
	g_assert_nonnull(prompts);
	g_assert_nonnull(bytes);
	schema = ls_json_parse(g_bytes_get_data(bytes, NULL), NULL);
	g_assert_nonnull(schema);
	for (i = 0; i < G_N_ELEMENTS(cases); i++) {
		g_autoptr(JsonNode) value = ls_json_parse(cases[i], NULL);
		g_autoptr(LsContext) context = ls_context_new();
		g_assert_true(ls_context_apply(context, "{\"files\":[{\"path\":\"old\",\"content\":\"old\"},"
			"{\"path\":\"a\",\"content\":\"original\"}]}", NULL));
		g_assert_cmpint(ls_context_apply(context, cases[i], NULL), ==, i < 3);
		g_assert_cmpint(value != NULL && schema_accepts(json_node_get_object(schema), value), ==, i < 3);
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
		"{required: [a], 'required|checks': []}",
		"{required: [a], checks: [{path: a, shellcheck: false}]}",
		"{required: [a], checks: [{path: a, shellcheck: 'yes'}]}",
		"{required: [a], checks: [{path: a, shellcheck: 1}]}"
	};
	guint i;
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/contracts/response-schema", response_schema);
	g_test_add_func("/contracts/scaffold", scaffold_contract);
	g_test_add_func("/contracts/scaffold-metadata", scaffold_metadata);
	g_test_add_func("/contracts/structured-form", structured_form);
	g_test_add_func("/contracts/structured-invalid", structured_invalid);
	g_test_add_func("/contracts/extensions", extension_contracts);
	g_test_add_func("/contracts/content", content_checks);
	g_test_add_func("/contracts/shellcheck/findings", shellcheck_checks);
	g_test_add_func("/contracts/shellcheck/isolated", shellcheck_isolated);
	g_test_add_func("/contracts/shellcheck/missing", shellcheck_missing);
	g_test_add_func("/contracts/shellcheck/retry", shellcheck_retry);
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
