/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "steps/ls-step.h"
#include "core/ls-template.h"
#include <string.h>
/* Bound on ShellCheck output quoted back into an error and retry prompt. */
#define LS_SHELLCHECK_REPORT 400
struct _LsValidateStep {
	LsStep parent_instance;
};
G_DEFINE_TYPE(LsValidateStep, ls_validate_step, LS_TYPE_STEP)

/* One array grammar serves paths and literal content predicates. Reject
 * duplicates and empty entries so accidental redundant rules fail preflight. */
static gboolean
check_strings(JsonObject *object, const gchar *key, gboolean paths, GError **error){
	g_autoptr(GHashTable) seen = g_hash_table_new(g_str_hash, g_str_equal);
	JsonNode *node = json_object_get_member(object, key);
	JsonArray *array;
	guint i;
	if (node == NULL || !JSON_NODE_HOLDS_ARRAY(node))
		goto invalid;
	array = json_node_get_array(node);
	if (json_array_get_length(array) == 0 || json_array_get_length(array) > 4096)
		goto invalid;
	for (i = 0; i < json_array_get_length(array); i++) {
		const gchar *text;
		node = json_array_get_element(array, i);
		text = json_node_get_value_type(node) == G_TYPE_STRING ? json_node_get_string(node) : NULL;
		if (text == NULL || *text == '\0' || (paths && !ls_path_valid(text)) ||
			!g_hash_table_add(seen, (gpointer)text))
			goto invalid;
	}
	return TRUE;
invalid:
	g_set_error(error, LS_ERROR, LS_ERROR_INVALID,
		"validate %s must contain 1..4096 unique nonempty %s", key,
		paths ? "safe relative paths" : "literal strings");
	return FALSE;
}

/* This schema validator is shared by preflight and execution; paid generation
 * must not discover misspelled predicates or impossible byte ranges afterward. */
gboolean
ls_validate_step_check_options(JsonObject *options, GError **error){
	g_autoptr(GHashTable) seen = g_hash_table_new(g_str_hash, g_str_equal);
	JsonNode *node;
	JsonArray *checks;
	gboolean shellcheck = FALSE;
	guint i;
	if (options == NULL) {
		g_set_error_literal(error, LS_ERROR, LS_ERROR_INVALID,
			"validate requires options.required");
		return FALSE;
	}
	if (!ls_object_keys(options, "|required|checks|", error) ||
		!check_strings(options, "required", TRUE, error))
		return FALSE;
	if (!json_object_has_member(options, "checks"))
		return TRUE;
	node = json_object_get_member(options, "checks");
	if (!JSON_NODE_HOLDS_ARRAY(node))
		goto invalid;
	checks = json_node_get_array(node);
	if (json_array_get_length(checks) == 0 || json_array_get_length(checks) > 4096)
		goto invalid;
	for (i = 0; i < json_array_get_length(checks); i++) {
		JsonObject *check;
		const gchar *path;
		const gchar *bounds[] = {"min_bytes", "max_bytes"};
		gint64 values[] = {0, LS_MAX_DOCUMENT};
		guint j;
		node = json_array_get_element(checks, i);
		if (!JSON_NODE_HOLDS_OBJECT(node))
			goto invalid;
		check = json_node_get_object(node);
		if (!ls_object_keys(check, "|path|contains|excludes|min_bytes|max_bytes|shellcheck|", error))
			return FALSE;
		node = json_object_get_member(check, "shellcheck");
		if (node != NULL) {
			if (json_node_get_value_type(node) != G_TYPE_BOOLEAN || !json_node_get_boolean(node)) {
				g_set_error_literal(error, LS_ERROR, LS_ERROR_INVALID,
					"validate shellcheck must be true when present");
				return FALSE;
			}
			shellcheck = TRUE;
		}
		path = ls_string(check, "path");
		if (!ls_path_valid(path) || !g_hash_table_add(seen, (gpointer)path) ||
			json_object_get_size(check) < 2)
			goto invalid;
		if ((json_object_has_member(check, "contains") &&
			!check_strings(check, "contains", FALSE, error)) ||
			(json_object_has_member(check, "excludes") &&
			!check_strings(check, "excludes", FALSE, error)))
			return FALSE;
		for (j = 0; j < G_N_ELEMENTS(bounds); j++) {
			node = json_object_get_member(check, bounds[j]);
			if (node == NULL)
				continue;
			if (json_node_get_value_type(node) != G_TYPE_INT64)
				goto invalid;
			values[j] = json_node_get_int(node);
			if (values[j] < 0 || values[j] > LS_MAX_DOCUMENT)
				goto invalid;
		}
		if (values[0] > values[1])
			goto invalid;
	}
	/* Missing host tools fail preflight, not after a paid provider call. */
	if (shellcheck) {
		g_autofree gchar *program = g_find_program_in_path("shellcheck");
		if (program == NULL) {
			g_set_error_literal(error, LS_ERROR, LS_ERROR_INVALID,
				"validate shellcheck requires ShellCheck on PATH (Fedora package: ShellCheck)");
			return FALSE;
		}
	}
	return TRUE;
invalid:
	g_set_error_literal(error, LS_ERROR, LS_ERROR_INVALID,
		"validate checks must contain 1..4096 rules with unique safe paths and at least one "
		"predicate; byte bounds must be integers with 0 <= min_bytes <= max_bytes <= 16777216");
	return FALSE;
}

/* Required artifacts must have visible text, including when their whitespace
 * is Unicode. Artifact construction already guarantees valid UTF-8. */
static gboolean
has_text(const gchar *content){
	const gchar *cursor;
	for (cursor = content; *cursor != '\0'; cursor = g_utf8_next_char(cursor))
		if (!g_unichar_isspace(g_utf8_get_char(cursor)))
			return TRUE;
	return FALSE;
}

/* Static analysis only: ShellCheck reads the script on stdin and never runs it.
 * --norc and a cleared SHELLCHECK_OPTS keep the verdict independent of the
 * caller's directory and environment. The shebang selects the dialect; a
 * script without one is rejected (SC2148). */
static gboolean
check_shell(const gchar *path, const gchar *content, GCancellable *cancellable, GError **error){
	g_autoptr(GSubprocessLauncher) launcher = g_subprocess_launcher_new(
		G_SUBPROCESS_FLAGS_STDIN_PIPE | G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_MERGE);
	g_autoptr(GSubprocess) process = NULL;
	g_autofree gchar *output = NULL;
	g_autofree gchar *report = NULL;
	g_auto(GStrv) lines = NULL;
	g_autoptr(GString) summary = NULL;
	guint i;
	g_subprocess_launcher_unsetenv(launcher, "SHELLCHECK_OPTS");
	process = g_subprocess_launcher_spawn(launcher, error,
		"shellcheck", "--norc", "--format=gcc", "-", NULL);
	if (process == NULL)
		return FALSE;
	if (!g_subprocess_communicate_utf8(process, content, cancellable, &output, NULL, error)) {
		/* Cancellation does not stop the child; never leave it running. */
		g_subprocess_force_exit(process);
		return FALSE;
	}
	if (g_subprocess_get_if_exited(process) && g_subprocess_get_exit_status(process) == 0)
		return TRUE;
	/* gcc format is "-:LINE:COL: severity: text [SCnnnn]"; name the artifact. */
	summary = g_string_new(NULL);
	lines = g_strsplit(output != NULL ? output : "", "\n", -1);
	for (i = 0; lines[i] != NULL && summary->len < LS_SHELLCHECK_REPORT; i++) {
		if (*lines[i] == '\0')
			continue;
		if (summary->len > 0)
			g_string_append(summary, "; ");
		g_string_append(summary, g_str_has_prefix(lines[i], "-:") ? lines[i] + 2 : lines[i]);
	}
	report = g_utf8_make_valid(summary->str, MIN(summary->len, LS_SHELLCHECK_REPORT));
	g_set_error(error, LS_ERROR, LS_ERROR_VALIDATION, "Artifact '%s' failed ShellCheck: %s",
		path, *report != '\0' ? report : "no diagnostics");
	return FALSE;
}

/* Whether @content holds @literal. Markup pages hold visible text with &, <,
 * > and quotes written as entities, so an HTML, SVG or XML artifact also
 * matches the escaped forms the builder's templates write. */
static gboolean
holds_literal(const gchar *path, const gchar *content, const gchar *literal){
	g_autofree gchar *lower = g_ascii_strdown(path, -1);
	g_autofree gchar *escaped = NULL;
	GString *text_only;
	const gchar *p;
	gboolean found;
	if (strstr(content, literal) != NULL)
		return TRUE;
	if (!g_str_has_suffix(lower, ".html") && !g_str_has_suffix(lower, ".htm") &&
		!g_str_has_suffix(lower, ".svg") && !g_str_has_suffix(lower, ".xml"))
		return FALSE;
	escaped = ls_template_escape(literal);
	if (strstr(content, escaped) != NULL)
		return TRUE;
	/* Text nodes need only &, < and > escaped. */
	text_only = g_string_new(NULL);
	for (p = literal; *p != '\0'; p++)
		if (*p == '&')
			g_string_append(text_only, "&amp;");
		else if (*p == '<')
			g_string_append(text_only, "&lt;");
		else if (*p == '>')
			g_string_append(text_only, "&gt;");
		else
			g_string_append_c(text_only, *p);
	found = strstr(content, text_only->str) != NULL;
	g_string_free(text_only, TRUE);
	return found;
}

/* Literal predicates are deterministic and never execute generated code.
 * Report the predicate index instead of dumping arbitrary project content. */
static gboolean
check_content(JsonObject *check, LsContext *context, GCancellable *cancellable, GError **error){
	const gchar *path = ls_string(check, "path");
	const LsArtifact *artifact = ls_context_lookup(context, path);
	const gchar *content;
	const gchar *predicates[] = {"contains", "excludes"};
	gsize size;
	guint i, j;
	if (artifact == NULL) {
		g_set_error(error, LS_ERROR, LS_ERROR_VALIDATION, "Checked artifact is missing: %s", path);
		return FALSE;
	}
	size = ls_artifact_get_size(artifact);
	if ((json_object_has_member(check, "min_bytes") &&
		size < (gsize)json_object_get_int_member(check, "min_bytes")) ||
		(json_object_has_member(check, "max_bytes") &&
		size > (gsize)json_object_get_int_member(check, "max_bytes"))) {
		g_set_error(error, LS_ERROR, LS_ERROR_VALIDATION,
			"Artifact '%s' has %" G_GSIZE_FORMAT " bytes, outside its configured byte bounds", path, size);
		return FALSE;
	}
	if (ls_artifact_is_binary(artifact) &&
		(json_object_has_member(check, "contains") || json_object_has_member(check, "excludes") ||
		json_object_has_member(check, "shellcheck"))) {
		g_set_error(error, LS_ERROR, LS_ERROR_VALIDATION,
			"Artifact '%s' is binary; contains/excludes/shellcheck apply to UTF-8 text", path);
		return FALSE;
	}
	content = ls_artifact_get_content(artifact);
	for (i = 0; i < G_N_ELEMENTS(predicates); i++) {
		JsonArray *strings;
		if (!json_object_has_member(check, predicates[i]))
			continue;
		strings = json_object_get_array_member(check, predicates[i]);
		for (j = 0; j < json_array_get_length(strings); j++) {
			gboolean found = holds_literal(path, content, json_array_get_string_element(strings, j));
			if ((i == 0 && !found) || (i == 1 && found)) {
				g_set_error(error, LS_ERROR, LS_ERROR_VALIDATION,
					"Artifact '%s' failed %s predicate %u", path, predicates[i], j + 1);
				return FALSE;
			}
		}
	}
	if (json_object_has_member(check, "shellcheck"))
		return check_shell(path, content, cancellable, error);
	return TRUE;
}

/* Full compiler/browser verification still belongs to the generated project's
 * inspected test suite. Native acceptance checks only the exact artifact text. */
static gboolean
execute(
	LsStep *self,
	LsRecipe *recipe,
	JsonObject *config,
	LsPrompts *prompts,
	LsContext *context,
	LsProvider *provider,
	GCancellable *cancellable,
	GError **error
){
	JsonNode *node = json_object_get_member(config, "options");
	JsonObject *options = node != NULL && JSON_NODE_HOLDS_OBJECT(node)
		? json_node_get_object(node) : NULL;
	JsonArray *required;
	guint i;
	(void)self;
	(void)recipe;
	(void)prompts;
	(void)provider;
	if (!ls_validate_step_check_options(options, error))
		return FALSE;
	required = json_object_get_array_member(options, "required");
	for (i = 0; i < json_array_get_length(required); i++) {
		const gchar *path = json_array_get_string_element(required, i);
		const LsArtifact *artifact;
		if (g_cancellable_set_error_if_cancelled(cancellable, error))
			return FALSE;
		artifact = ls_context_lookup(context, path);
		if (artifact == NULL ||
			(ls_artifact_is_binary(artifact) ? ls_artifact_get_size(artifact) == 0
				: !has_text(ls_artifact_get_content(artifact)))) {
			g_set_error(error, LS_ERROR, LS_ERROR_VALIDATION,
				"Required artifact is missing or empty: %s", path);
			return FALSE;
		}
	}
	if (json_object_has_member(options, "checks")) {
		JsonArray *checks = json_object_get_array_member(options, "checks");
		for (i = 0; i < json_array_get_length(checks); i++) {
			if (g_cancellable_set_error_if_cancelled(cancellable, error) ||
				!check_content(json_array_get_object_element(checks, i), context, cancellable, error))
				return FALSE;
		}
	}
	return TRUE;
}
static void
ls_validate_step_class_init(LsValidateStepClass *klass){
	LS_STEP_CLASS(klass)->execute = execute;
}
static void
ls_validate_step_init(LsValidateStep *self){
	(void)self;
}
