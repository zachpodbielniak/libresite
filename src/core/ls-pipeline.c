/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "core/ls-usage.h"
#include "core/ls-pipeline.h"
#include <glib/gstdio.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
struct _LsPipeline {
	GObject parent_instance;
	LsProvider *provider;
	LsPrompts *prompts;
	GHashTable *types;
	gboolean running;
};
G_DEFINE_TYPE(LsPipeline, ls_pipeline, G_TYPE_OBJECT)
static guint signals[6];
/* The forwarding data lives for the synchronous execution of one step. */
typedef struct {
	LsPipeline *pipeline;
	const gchar *id;
	guint index;
} StepProgress;
static void
forward_progress(LsStep *step, const gchar *message, gpointer data){
	StepProgress *progress = (StepProgress *)data;
	(void)step;
	g_signal_emit(progress->pipeline, signals[4], 0,
		progress->id, progress->index, message);
}
/* Hold providers/catalogs for the pipeline lifetime; callbacks borrow self. */
static void
ls_pipeline_finalize(GObject *object){
	LsPipeline *self = LS_PIPELINE(object);
	g_clear_object(&self->provider);
	g_clear_object(&self->prompts);
	g_hash_table_unref(self->types);
	G_OBJECT_CLASS(ls_pipeline_parent_class)->finalize(object);
}
static void
ls_pipeline_class_init(LsPipelineClass *klass){
	G_OBJECT_CLASS(klass)->finalize = ls_pipeline_finalize;
	/**
	 * LsPipeline::step-failed:
	 * @self: the pipeline
	 * @id: (transfer none): step ID
	 * @index: zero-based step index
	 * @error: (transfer none) (nullable): execution or checkpoint error
	 *
	 * Emitted on the calling thread after a started step fails. No artifacts
	 * are published; the run still returns its detailed GError to the caller.
	 * A native gate waiting for approval is reported here with
	 * %LS_ERROR_APPROVAL: a pause, with the previous step checkpointed.
	 */
	signals[5] = g_signal_new("step-failed", G_TYPE_FROM_CLASS(klass),
		G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 3,
		G_TYPE_STRING, G_TYPE_UINT, G_TYPE_ERROR);
	/**
	 * LsPipeline::step-progress:
	 * @self: the pipeline
	 * @id: (transfer none): step ID
	 * @index: zero-based step index
	 * @message: (transfer none): attempt or retry status
	 *
	 * Forwarded on the calling thread; copy strings to retain them.
	 */
	signals[4] = g_signal_new("step-progress", G_TYPE_FROM_CLASS(klass),
		G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 3,
		G_TYPE_STRING, G_TYPE_UINT, G_TYPE_STRING);
	signals[0] = g_signal_new(
		"step-started",
		G_TYPE_FROM_CLASS(klass),
		G_SIGNAL_RUN_LAST,
		0,
		NULL,
		NULL,
		NULL,
		G_TYPE_NONE,
		2,
		G_TYPE_STRING,
		G_TYPE_UINT
	);
	signals[1] = g_signal_new(
		"step-completed",
		G_TYPE_FROM_CLASS(klass),
		G_SIGNAL_RUN_LAST,
		0,
		NULL,
		NULL,
		NULL,
		G_TYPE_NONE,
		2,
		G_TYPE_STRING,
		G_TYPE_UINT
	);
	signals[2] = g_signal_new(
		"artifact-written",
		G_TYPE_FROM_CLASS(klass),
		G_SIGNAL_RUN_LAST,
		0,
		NULL,
		NULL,
		NULL,
		G_TYPE_NONE,
		1,
		LS_TYPE_ARTIFACT
	);
	signals[3] = g_signal_new(
		"completed",
		G_TYPE_FROM_CLASS(klass),
		G_SIGNAL_RUN_LAST,
		0,
		NULL,
		NULL,
		NULL,
		G_TYPE_NONE,
		1,
		G_TYPE_STRING
	);
}
static void
ls_pipeline_init(LsPipeline *self){
	self->types = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
}
LsPipeline *
ls_pipeline_new(LsProvider *provider, LsPrompts *prompts){
	LsPipeline *self;
	const gchar *names[] = {
		"brief", "design", "frontend", "backend", "database", "review", "generate"
	};
	guint i;
	g_return_val_if_fail(LS_IS_PROVIDER(provider) && LS_IS_PROMPTS(prompts), NULL);
	self = g_object_new(LS_TYPE_PIPELINE, NULL);
	self->provider = g_object_ref(provider);
	self->prompts = g_object_ref(prompts);
	for (i = 0; i < G_N_ELEMENTS(names); i++)
		ls_pipeline_register(self, names[i], LS_TYPE_GENERATE_STEP, NULL);
	ls_pipeline_register(self, "validate", LS_TYPE_VALIDATE_STEP, NULL);
	ls_pipeline_register(self, "scaffold", LS_TYPE_SCAFFOLD_STEP, NULL);
	ls_pipeline_register(self, "gate", LS_TYPE_GATE_STEP, NULL);
	ls_pipeline_register(self, "prune", LS_TYPE_PRUNE_STEP, NULL);
	return self;
}
/* Registration is instance-local, avoiding global initialization ordering. */
gboolean
ls_pipeline_register(LsPipeline *self, const gchar *name, GType step_type, GError **error){
	if (self->running || name == NULL || *name == '\0' || !g_type_is_a(step_type, LS_TYPE_STEP) ||
		G_TYPE_IS_ABSTRACT(step_type) || g_hash_table_contains(self->types, name)) {
		g_set_error_literal(
			error,
			LS_ERROR,
			LS_ERROR_INVALID,
			"Step registration requires a unique name and concrete LsStep type on an idle pipeline"
		);
		return FALSE;
	}
	g_hash_table_insert(self->types, g_strdup(name), GSIZE_TO_POINTER(step_type));
	return TRUE;
}
/* Native validation options are checked before AI calls, not at the final step. */
gboolean
ls_pipeline_validate(LsPipeline *self, LsRecipe *recipe, GError **error){
	g_autoptr(LsContext) initial = ls_recipe_create_context(recipe, error);
	return initial != NULL && ls_pipeline_validate_context(self, recipe, initial, error);
}

/* Share preflight across fresh generation, imported projects and CLI inspection. */
gboolean
ls_pipeline_validate_context(LsPipeline *self, LsRecipe *recipe,
	LsContext *context, GError **error){
	JsonArray *steps = ls_recipe_get_steps(recipe);
	guint i;
	for (i = 0; i < json_array_get_length(steps); i++) {
		JsonObject *config = json_array_get_object_element(steps, i);
		const gchar *name = ls_string(config, "type");
		GType type = (GType)GPOINTER_TO_SIZE(g_hash_table_lookup(self->types, name));
		if (type == 0) {
			g_set_error(error, LS_ERROR, LS_ERROR_INVALID, "Unknown step type '%s'", name);
			return FALSE;
		}
		if (g_type_is_a(type, LS_TYPE_GENERATE_STEP)) {
			g_autofree gchar *rendered =
				ls_prompts_render(self->prompts, recipe, config, context, error);
			if (rendered == NULL)
				return FALSE;
		} else if (type == LS_TYPE_VALIDATE_STEP || type == LS_TYPE_SCAFFOLD_STEP ||
			type == LS_TYPE_GATE_STEP || type == LS_TYPE_PRUNE_STEP) {
			JsonNode *node = json_object_get_member(config, "options");
			JsonObject *options = node != NULL && JSON_NODE_HOLDS_OBJECT(node)
				? json_node_get_object(node) : NULL;
			gboolean okay = type == LS_TYPE_VALIDATE_STEP
				? ls_validate_step_check_options(options, error)
				: type == LS_TYPE_SCAFFOLD_STEP
					? ls_scaffold_step_check_options(options, error)
					: type == LS_TYPE_PRUNE_STEP
						? ls_prune_step_check_options(options, error)
						: ls_gate_step_check_options(options, error);
			if (!okay) {
				g_prefix_error(error, "Step '%s': ", ls_string(config, "id"));
				return FALSE;
			}
		}
	}
	return TRUE;
}
/* The checkpoint contains the entire last successful project; failed stage
 * output never becomes input on resume. Fingerprints bind recipe and catalog. */
static gchar *
checkpoint_text(LsContext *context, const gchar *fingerprint, guint next,
	JsonArray *usage, gboolean usage_complete){
	g_autoptr(JsonParser) parser = json_parser_new();
	g_autofree gchar *snapshot = ls_context_snapshot(context);
	JsonObject *object;
	json_parser_load_from_data(parser, snapshot, -1, NULL);
	object = json_node_get_object(json_parser_get_root(parser));
	json_object_set_string_member(object, "fingerprint", fingerprint);
	json_object_set_int_member(object, "next", next);
	json_object_set_int_member(object, "version", 1);
	json_object_set_array_member(object, "usage", json_array_ref(usage));
	json_object_set_boolean_member(object, "usage_complete", usage_complete);
	return ls_json_encode(json_parser_get_root(parser));
}
static gboolean
save_checkpoint(
	const gchar *path, LsContext *context, const gchar *fingerprint, guint next,
	JsonArray *usage, gboolean usage_complete, GError **error
){
	g_autofree gchar *text = checkpoint_text(context, fingerprint, next, usage, usage_complete);
	if (strlen(text) > (gsize)LS_MAX_PROJECT * 2) {
		g_set_error_literal(
			error, LS_ERROR, LS_ERROR_LIMIT,
			"Checkpoint exceeds 128 MiB encoded size; split the recipe or reduce binary assets"
		);
		return FALSE;
	}
	return g_file_set_contents_full(
		path, text, -1, G_FILE_SET_CONTENTS_CONSISTENT | G_FILE_SET_CONTENTS_DURABLE, 0600, error
	);
}
static gboolean
load_checkpoint(
	const gchar *path,
	const gchar *fingerprint,
	guint count,
	LsContext *context,
	guint *next,
	JsonArray **usage,
	gboolean *usage_complete,
	GError **error
){
	g_autofree gchar *text = ls_read_text(path, error);
	g_autoptr(JsonNode) parsed = NULL;
	g_autofree gchar *bundle = NULL;
	JsonObject *object;
	JsonNode *node;
	gint64 index;
	if (text == NULL)
		return FALSE;
	parsed = ls_json_parse(text, error);
	if (parsed == NULL)
		return FALSE;
	node = parsed;
	if (!JSON_NODE_HOLDS_OBJECT(node))
		goto invalid;
	object = json_node_get_object(node);
	if (!ls_object_keys(object, "|version|fingerprint|next|files|usage|usage_complete|", error))
		return FALSE;
	if (g_strcmp0(ls_string(object, "fingerprint"), fingerprint) != 0)
		goto invalid;
	node = json_object_get_member(object, "version");
	if (node == NULL || json_node_get_value_type(node) != G_TYPE_INT64 ||
		json_node_get_int(node) != 1)
		goto invalid;
	node = json_object_get_member(object, "next");
	if (node == NULL || json_node_get_value_type(node) != G_TYPE_INT64)
		goto invalid;
	index = json_node_get_int(node);
	if (index < 0 || index > count)
		goto invalid;
	/* Old checkpoints remain resumable, but their missing history cannot be priced. */
	node = json_object_get_member(object, "usage");
	if (node != NULL) {
		JsonNode *complete = json_object_get_member(object, "usage_complete");
		if (!JSON_NODE_HOLDS_ARRAY(node) || complete == NULL ||
			json_node_get_value_type(complete) != G_TYPE_BOOLEAN)
			goto invalid;
		if (!ls_usage_validate(json_node_get_array(node), error))
			return FALSE;
		g_clear_pointer(usage, json_array_unref);
		*usage = json_array_ref(json_node_get_array(node));
		*usage_complete = json_node_get_boolean(complete);
	} else {
		if (json_object_has_member(object, "usage_complete"))
			goto invalid;
		*usage_complete = FALSE;
	}
	json_object_remove_member(object, "usage");
	json_object_remove_member(object, "usage_complete");
	json_object_remove_member(object, "fingerprint");
	json_object_remove_member(object, "next");
	json_object_remove_member(object, "version");
	bundle = ls_json_encode(parsed);
	if (!ls_context_restore(context, bundle, error))
		return FALSE;
	*next = (guint)index;
	return TRUE;
invalid:
	g_set_error_literal(
		error,
		LS_ERROR,
		LS_ERROR_CONFLICT,
		"Checkpoint is corrupt or recipe/prompts/source changed; use a new output directory"
	);
	return FALSE;
}
/* Run Git without inherited repository pointers, user hooks, filters or signing.
 * Cancellation must reap the child before the private export is abandoned. */
static gboolean
run_git(const gchar *directory, const gchar *const *arguments,
	GCancellable *cancellable, GError **error){
	g_autoptr(GSubprocessLauncher) launcher = g_subprocess_launcher_new(
		G_SUBPROCESS_FLAGS_STDOUT_SILENCE | G_SUBPROCESS_FLAGS_STDERR_PIPE);
	g_autoptr(GSubprocess) process = NULL;
	g_auto(GStrv) environment = g_get_environ();
	g_autoptr(GPtrArray) argv = g_ptr_array_new();
	g_autofree gchar *diagnostic = NULL;
	guint i;
	if (g_cancellable_set_error_if_cancelled(cancellable, error))
		return FALSE;
	g_subprocess_launcher_set_cwd(launcher, directory);
	for (i = 0; environment[i] != NULL; i++) {
		if (g_str_has_prefix(environment[i], "GIT_")) {
			g_autofree gchar *name = g_strndup(environment[i],
				(gsize)(strchr(environment[i], '=') - environment[i]));
			g_subprocess_launcher_unsetenv(launcher, name);
		}
	}
	g_subprocess_launcher_setenv(launcher, "GIT_CONFIG_NOSYSTEM", "1", TRUE);
	g_subprocess_launcher_setenv(launcher, "GIT_CONFIG_GLOBAL", "/dev/null", TRUE);
	g_subprocess_launcher_setenv(launcher, "GIT_ATTR_NOSYSTEM", "1", TRUE);
	g_ptr_array_add(argv, (gpointer)"git");
	g_ptr_array_add(argv, (gpointer)"-c");
	g_ptr_array_add(argv, (gpointer)"core.hooksPath=/dev/null");
	g_ptr_array_add(argv, (gpointer)"-c");
	g_ptr_array_add(argv, (gpointer)"core.attributesFile=/dev/null");
	g_ptr_array_add(argv, (gpointer)"-c");
	g_ptr_array_add(argv, (gpointer)"core.excludesFile=/dev/null");
	g_ptr_array_add(argv, (gpointer)"-c");
	g_ptr_array_add(argv, (gpointer)"user.name=Libresite");
	g_ptr_array_add(argv, (gpointer)"-c");
	g_ptr_array_add(argv, (gpointer)"user.email=libresite@localhost");
	for (i = 0; arguments[i] != NULL; i++)
		g_ptr_array_add(argv, (gpointer)arguments[i]);
	g_ptr_array_add(argv, NULL);
	process = g_subprocess_launcher_spawnv(launcher, (const gchar *const *)argv->pdata, error);
	if (process == NULL) {
		g_prefix_error(error, "Cannot run git %s: ", arguments[0]);
		return FALSE;
	}
	if (!g_subprocess_communicate_utf8(process, NULL, cancellable, NULL, &diagnostic, error)) {
		g_subprocess_force_exit(process);
		g_subprocess_wait(process, NULL, NULL);
		return FALSE;
	}
	if (!g_subprocess_get_successful(process)) {
		g_set_error(error, LS_ERROR, LS_ERROR_CONFLICT, "git %s failed: %s",
			arguments[0], diagnostic != NULL ? diagnostic : "no diagnostic");
		return FALSE;
	}
	return TRUE;
}

/* Preserve project-specific ignore rules and append baseline local-file rules.
 * Keep generated sources and lockfiles eligible for the initial snapshot. */
static gboolean
initialize_repository(const gchar *directory, LsContext *context,
	GCancellable *cancellable, GError **error){
	const LsArtifact *existing = ls_context_lookup(context, ".gitignore");
	g_autofree gchar *path = g_build_filename(directory, ".gitignore", NULL);
	const gchar *defaults =
		"\n# Libresite: local metadata, secrets, build output and caches\n"
		"/.libresite/\n*.libresite-work/\n"
		".env\n.env.*\n!.env.example\n!.env.sample\n!.env.template\n"
		"/build/\n/dist/\n/coverage/\nnode_modules/\n.venv/\nvenv/\n"
		"__pycache__/\n*.py[cod]\n.pytest_cache/\n.next/\n.nuxt/\n"
		"*.o\n*.a\n*.so\n*.so.*\n*.log\n*.sqlite\n*.sqlite3\n"
		"*.sqlite-*\n*.sqlite3-*\n*.db\n*.db-*\n*~\n*.swp\n"
		"# Always retain the repository ignore policy\n!/.gitignore\n";
	const gchar *original = existing != NULL && !ls_artifact_is_binary(existing)
		? ls_artifact_get_content(existing) : "";
	g_autofree gchar *contents = g_str_has_suffix(original, defaults)
		? g_strdup(original) : g_strconcat(original, defaults, NULL);
	const gchar *init[] = {"init", "--quiet", "--initial-branch=master", "--template=", NULL};
	const gchar *add[] = {"add", "--all", "--", ".", NULL};
	const gchar *commit[] = {"commit", "--no-gpg-sign", "--quiet",
		"-m", ls_context_is_revision(context) ? "feat: revise existing project"
			: "chore: initial generated project", NULL};
	if (!g_file_set_contents(path, contents, -1, error))
		return FALSE;
	return run_git(directory, init, cancellable, error) &&
		run_git(directory, add, cancellable, error) &&
		run_git(directory, commit, cancellable, error);
}

/* Drop a previous failed review export so the next stop uses a stable path. */
static gboolean
remove_tree(const gchar *path, GError **error){
	g_autoptr(GFile) file = g_file_new_for_path(path);
	g_autoptr(GFileEnumerator) enumerator = NULL;
	g_autoptr(GError) local_error = NULL;
	if (!g_file_test(path, G_FILE_TEST_EXISTS))
		return TRUE;
	enumerator = g_file_enumerate_children(file, "standard::name,standard::type",
		G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, &local_error);
	if (enumerator != NULL) {
		GFileInfo *info;
		while ((info = g_file_enumerator_next_file(enumerator, NULL, NULL)) != NULL) {
			g_autoptr(GFile) child = g_file_get_child(file, g_file_info_get_name(info));
			g_autofree gchar *child_path = g_file_get_path(child);
			if (g_file_info_get_file_type(info) == G_FILE_TYPE_DIRECTORY) {
				if (!remove_tree(child_path, error)) {
					g_object_unref(info);
					return FALSE;
				}
			} else if (!g_file_delete(child, NULL, error)) {
				g_object_unref(info);
				return FALSE;
			}
			g_object_unref(info);
		}
	} else if (!g_error_matches(local_error, G_IO_ERROR, G_IO_ERROR_NOT_DIRECTORY)) {
		g_propagate_error(error, g_steal_pointer(&local_error));
		return FALSE;
	}
	return g_file_delete(file, NULL, error);
}

/* Write inspectable design files when a gate stops the run. The project itself
 * is unchanged; this is workspace output for the human reviewer. */
static gboolean
export_review(const gchar *work, const gchar *id, LsContext *context,
	JsonObject *config, GError **error){
	g_autofree gchar *parent = g_build_filename(work, "review", NULL);
	g_autofree gchar *directory = g_build_filename(parent, id, NULL);
	JsonNode *node = json_object_get_member(config, "options");
	JsonObject *options = node != NULL && JSON_NODE_HOLDS_OBJECT(node)
		? json_node_get_object(node) : NULL;
	g_autoptr(LsContext) selected = NULL;
	if (g_mkdir_with_parents(parent, 0700) != 0) {
		g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
			"Cannot create review directory: %s", g_strerror(errno));
		return FALSE;
	}
	if (!remove_tree(directory, error))
		return FALSE;
	if (options != NULL && json_object_has_member(options, "review")) {
		JsonArray *review = json_object_get_array_member(options, "review");
		g_autoptr(JsonBuilder) builder = json_builder_new();
		g_autoptr(JsonNode) root = NULL;
		g_autofree gchar *bundle = NULL;
		guint i;
		selected = ls_context_new();
		json_builder_begin_object(builder);
		json_builder_set_member_name(builder, "files");
		json_builder_begin_array(builder);
		for (i = 0; i < json_array_get_length(review); i++) {
			const gchar *path = json_array_get_string_element(review, i);
			const LsArtifact *artifact = ls_context_lookup(context, path);
			gsize size;
			const guint8 *data;
			if (artifact == NULL)
				continue;
			data = ls_artifact_get_data(artifact, &size);
			json_builder_begin_object(builder);
			json_builder_set_member_name(builder, "path");
			json_builder_add_string_value(builder, path);
			json_builder_set_member_name(builder, "content");
			if (ls_artifact_is_binary(artifact)) {
				g_autofree gchar *encoded = g_base64_encode(data, size);
				json_builder_add_string_value(builder, encoded);
				json_builder_set_member_name(builder, "encoding");
				json_builder_add_string_value(builder, "base64");
			} else
				json_builder_add_string_value(builder, ls_artifact_get_content(artifact));
			json_builder_end_object(builder);
		}
		json_builder_end_array(builder);
		json_builder_end_object(builder);
		root = json_builder_get_root(builder);
		bundle = ls_json_encode(root);
		if (!ls_context_apply(selected, bundle, error))
			return FALSE;
		return ls_context_export(selected, directory, NULL, error);
	}
	return ls_context_export(context, directory, NULL, error);
}

/* Lock one workspace through publication. All generated code remains inert;
 * publication is a same-filesystem directory move with no overwrite flag. */
static gboolean
run_pipeline(
	LsPipeline *self,
	LsRecipe *recipe,
	const gchar *project,
	const gchar *output,
	gboolean resume,
	GCancellable *cancellable,
	GError **error
){
	g_autoptr(LsContext) context = ls_context_new();
	g_autofree gchar *absolute = NULL, *work = NULL, *lock_path = NULL, *checkpoint = NULL;
	g_autofree gchar *recipe_json = NULL, *fingerprint_input = NULL, *fingerprint = NULL;
	g_autofree gchar *export_path = NULL, *uuid = NULL;
	g_autofree gchar *initial = NULL;
	g_autofree gchar *scaffold = NULL;
	g_autoptr(JsonNode) recipe_node = json_node_new(JSON_NODE_OBJECT);
	g_autoptr(GFile) source = NULL, destination = NULL;
	JsonArray *steps = ls_recipe_get_steps(recipe);
	GStatBuf info;
	guint next = 0, i;
	gint lock_fd = -1;
	gboolean success = FALSE;
	g_autoptr(JsonArray) usage = json_array_new();
	gboolean usage_complete = TRUE;
	if (self->running) {
		g_set_error_literal(error, LS_ERROR, LS_ERROR_CONFLICT, "Pipeline is already running");
		return FALSE;
	}
	if (!ls_pipeline_validate(self, recipe, error) ||
		g_cancellable_set_error_if_cancelled(cancellable, error))
		return FALSE;
	if (output == NULL || *output == '\0') {
		g_set_error_literal(error, LS_ERROR, LS_ERROR_INVALID, "An output directory is required");
		return FALSE;
	}
	/* Import before creating a workspace, and bind resume to actual source bytes.
	 * The imported snapshot, never the previous run's metadata, is the baseline. */
	if (project != NULL) {
		g_autofree gchar *source_real = realpath(project, NULL);
		g_autofree gchar *output_clean = g_canonicalize_filename(output, NULL);
		g_autofree gchar *output_parent = g_path_get_dirname(output_clean);
		g_autofree gchar *output_name = g_path_get_basename(output_clean);
		g_autofree gchar *work_name = g_strconcat(output_name, ".libresite-work", NULL);
		g_autofree gchar *parent_real = realpath(output_parent, NULL);
		g_autofree gchar *work_real = parent_real != NULL
			? g_build_filename(parent_real, work_name, NULL) : NULL;
		g_autofree gchar *work_prefix = work_real != NULL
			? g_strconcat(work_real, "/", NULL) : NULL;
		g_autofree gchar *source_prefix = source_real != NULL
			? g_strconcat(source_real, "/", NULL) : NULL;
		if (source_real == NULL || parent_real == NULL) {
			g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
				"Source and output parent must exist: %s", g_strerror(errno));
			return FALSE;
		}
		if (g_str_equal(source_real, "/") || g_str_equal(source_real, parent_real) ||
			g_str_has_prefix(parent_real, source_prefix) ||
			g_str_equal(source_real, work_real) || g_str_has_prefix(source_real, work_prefix)) {
			g_set_error_literal(error, LS_ERROR, LS_ERROR_CONFLICT,
				"Revision output and workspace must not overlap the source project");
			return FALSE;
		}
		g_clear_object(&context);
		context = ls_context_import(project, cancellable, error);
		if (context == NULL)
			return FALSE;
		initial = ls_context_snapshot(context);
		if (!ls_recipe_seed_context(recipe, context, error) ||
			!ls_pipeline_validate_context(self, recipe, context, error))
			return FALSE;
	}
	self->running = TRUE;
	absolute = g_canonicalize_filename(output, NULL);
	work = g_strconcat(absolute, ".libresite-work", NULL);
	lock_path = g_build_filename(work, "lock", NULL);
	checkpoint = g_build_filename(work, "checkpoint.json", NULL);
	if (g_lstat(absolute, &info) == 0) {
		g_set_error(error, LS_ERROR, LS_ERROR_CONFLICT, "Output already exists: %s", absolute);
		goto out;
	}
	if (g_mkdir(work, 0700) != 0) {
		if (errno != EEXIST || !resume || g_lstat(work, &info) != 0 || !S_ISDIR(info.st_mode) ||
			info.st_uid != getuid() || (info.st_mode & 0077) != 0) {
			g_set_error(
				error,
				LS_ERROR,
				LS_ERROR_CONFLICT,
				"Cannot create private workspace %s; create its parent, or use --resume for "
				"existing work",
				work
			);
			goto out;
		}
	}
	lock_fd = g_open(lock_path, O_CREAT | O_RDWR | O_NOFOLLOW | O_CLOEXEC, 0600);
	if (lock_fd < 0 || flock(lock_fd, LOCK_EX | LOCK_NB) != 0) {
		g_set_error_literal(
			error,
			LS_ERROR,
			LS_ERROR_RECOVERY,
			"Workspace is locked by another run or lock is invalid"
		);
		goto out;
	}
	json_node_set_object(recipe_node, ls_recipe_get_root(recipe));
	recipe_json = ls_json_encode(recipe_node);
	/* Template bytes are native inputs too: upgrading them must invalidate a
	 * partially completed scaffold recipe, including with custom catalogs. */
	for (i = 0; i < json_array_get_length(steps); i++) {
		JsonObject *step = json_array_get_object_element(steps, i);
		if (g_str_equal(ls_string(step, "type"), "scaffold")) {
			g_autoptr(LsContext) empty = ls_context_new();
			/* A sold package retains exact native bytes as well as its catalog.
			 * Unsold recipes keep the existing embedded-template fingerprint. */
			scaffold = ls_recipe_get_scaffold_source(recipe) != NULL
				? g_strdup(ls_recipe_get_scaffold_source(recipe))
				: ls_scaffold_step_render(NULL, empty, error);
			if (scaffold == NULL)
				goto out;
			break;
		}
	}
	fingerprint_input = g_strconcat(
		"libresite-checkpoint-v1\n", recipe_json, "\n", ls_prompts_get_digest(self->prompts),
		project != NULL ? "\nrevision-v1\n" : "", initial != NULL ? initial : "",
		scaffold != NULL ? "\nscaffold-v1\n" : "", scaffold != NULL ? scaffold : "", NULL
	);
	fingerprint = g_compute_checksum_for_string(G_CHECKSUM_SHA256, fingerprint_input, -1);
	if (resume && g_lstat(checkpoint, &info) == 0) {
		g_autoptr(GError) checkpoint_error = NULL;
		if (!S_ISREG(info.st_mode)) {
			g_set_error_literal(
				error, LS_ERROR, LS_ERROR_RECOVERY, "Checkpoint must be a regular file"
			);
			goto out;
		}
		if (!load_checkpoint(
				checkpoint, fingerprint, json_array_get_length(steps), context, &next, &usage, &usage_complete, &checkpoint_error
			)) {
			g_set_error_literal(error, LS_ERROR, LS_ERROR_RECOVERY, checkpoint_error->message);
			goto out;
		}
	} else {
		/* Seeds precede every step and the initial checkpoint. Never reapply
		 * them over a resumed project's accepted replacements. */
		if (project == NULL) {
			g_clear_object(&context);
			context = ls_recipe_create_context(recipe, error);
		}
		if (context == NULL || !save_checkpoint(checkpoint, context, fingerprint, 0, usage, usage_complete, error))
			goto out;
	}
	for (i = next; i < json_array_get_length(steps); i++) {
		JsonObject *config = json_array_get_object_element(steps, i);
		const gchar *id = ls_string(config, "id");
		GType type =
			(GType)GPOINTER_TO_SIZE(g_hash_table_lookup(self->types, ls_string(config, "type")));
		g_autoptr(LsStep) step = g_object_new(type, NULL);
		StepProgress progress = {self, id, i};
		gulong handler = g_signal_connect(step, "progress", G_CALLBACK(forward_progress), &progress);
		gboolean executed;
		g_autofree gchar *before = ls_context_snapshot(context);
		g_autoptr(JsonObject) record = NULL;
		/* A crash during execution leaves an explicit accounting gap. */
		if (!save_checkpoint(checkpoint, context, fingerprint, i, usage, FALSE, error))
			goto out;
		g_signal_emit(self, signals[0], 0, id, i);
		executed = ls_step_execute(
				step, recipe, config, self->prompts, context, self->provider, cancellable, error
			);
		g_signal_handler_disconnect(step, handler);
		record = ls_step_dup_usage(step);
		json_array_add_object_element(usage, g_steal_pointer(&record));
		if (!executed) {
			g_autoptr(GError) save_error = NULL;
			if (type == LS_TYPE_GATE_STEP) {
				g_autoptr(GError) review_error = NULL;
				if (!export_review(work, id, context, config, &review_error) &&
					error != NULL && *error != NULL)
					g_prefix_error(error, "%s; ", review_error != NULL
						? review_error->message : "review export failed");
			}
			/* Failed output is never checkpointed; consumed work still is. */
			if (!ls_context_restore(context, before, &save_error) ||
				!save_checkpoint(checkpoint, context, fingerprint, i, usage, usage_complete, &save_error)) {
				if (error != NULL && *error != NULL)
					g_prefix_error(error, "Usage could not be persisted (%s): ", save_error->message);
			}

			if (error != NULL && *error != NULL)
				g_prefix_error(error, "Step '%s': ", id);
			g_signal_emit(self, signals[5], 0, id, i, error != NULL ? *error : NULL);
			goto out;
		}
		if (!save_checkpoint(checkpoint, context, fingerprint, i + 1, usage, usage_complete, error) ||
			g_cancellable_set_error_if_cancelled(cancellable, error)) {
			g_signal_emit(self, signals[5], 0, id, i, error != NULL ? *error : NULL);
			goto out;
		}
		g_signal_emit(self, signals[1], 0, id, i);
	}
	{
		g_autofree gchar *snapshot = ls_context_snapshot(context);
		g_autoptr(JsonParser) parser = json_parser_new();
		JsonArray *files;
		json_parser_load_from_data(parser, snapshot, -1, NULL);
		files = json_object_get_array_member(
			json_node_get_object(json_parser_get_root(parser)), "files"
		);
		if (json_array_get_length(files) == 0) {
			g_set_error_literal(error, LS_ERROR, LS_ERROR_INVALID, "Recipe produced no files");
			goto out;
		}
	}
	uuid = g_uuid_string_random();
	export_path = g_build_filename(work, uuid, NULL);
	if (!ls_context_export(context, export_path, cancellable, error))
		goto out;
	{
		g_autofree gchar *metadata_dir = g_build_filename(export_path, ".libresite", NULL);
		g_autofree gchar *metadata = g_build_filename(metadata_dir, "run.json", NULL);
		if (g_mkdir(metadata_dir, 0700) != 0) {
			g_set_error(
				error,
				G_IO_ERROR,
				g_io_error_from_errno(errno),
				"Cannot create run metadata: %s",
				g_strerror(errno)
			);
			goto out;
		}
		if (!save_checkpoint(metadata, context, fingerprint, json_array_get_length(steps),
			usage, usage_complete, error))
			goto out;
	}
	if (!initialize_repository(export_path, context, cancellable, error))
		goto out;
	source = g_file_new_for_path(export_path);
	destination = g_file_new_for_path(absolute);
	if (!g_file_move(
			source, destination, G_FILE_COPY_NO_FALLBACK_FOR_MOVE, cancellable, NULL, NULL, error
		))
		goto out;
	/* Report artifacts only after the entire directory becomes visible. */
	{
		g_autofree gchar *snapshot = ls_context_snapshot(context);
		g_autoptr(JsonParser) parser = json_parser_new();
		JsonArray *files;
		json_parser_load_from_data(parser, snapshot, -1, NULL);
		files = json_object_get_array_member(
			json_node_get_object(json_parser_get_root(parser)), "files"
		);
		for (i = 0; i < json_array_get_length(files); i++) {
			const gchar *path = ls_string(json_array_get_object_element(files, i), "path");
			g_signal_emit(self, signals[2], 0, ls_context_lookup(context, path));
		}
	}
	success = TRUE;
	(void)g_unlink(checkpoint);
	g_signal_emit(self, signals[3], 0, absolute);
out:
	if (lock_fd >= 0) {
		/* Keep the lock inode stable during failures; unlink only after success. */
		if (success)
			(void)g_unlink(lock_path);
		(void)flock(lock_fd, LOCK_UN);
		(void)close(lock_fd);
	}
	if (success)
		(void)g_rmdir(work);
	self->running = FALSE;
	return success;
}

/* Preserve the existing generation entry point and its checkpoint identity. */
gboolean
ls_pipeline_run(LsPipeline *self, LsRecipe *recipe, const gchar *output,
	gboolean resume, GCancellable *cancellable, GError **error){
	return run_pipeline(self, recipe, NULL, output, resume, cancellable, error);
}

/* Revision publishes a new standalone repository; the source is read-only. */
gboolean
ls_pipeline_revise(LsPipeline *self, LsRecipe *recipe, const gchar *project,
	const gchar *output, gboolean resume, GCancellable *cancellable, GError **error){
	if (project == NULL || *project == '\0') {
		g_set_error_literal(error, LS_ERROR, LS_ERROR_INVALID, "A source directory is required");
		return FALSE;
	}
	return run_pipeline(self, recipe, project, output, resume, cancellable, error);
}
