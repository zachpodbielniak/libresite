/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "core/ls-pipeline.h"
#include <glib/gstdio.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
struct _LsPipeline {
	GObject parent_instance;
	LsProvider *provider;
	LsPrompts *prompts;
	GHashTable *types;
	gboolean running;
};
G_DEFINE_TYPE(LsPipeline, ls_pipeline, G_TYPE_OBJECT)
static guint signals[4];
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
	g_autoptr(LsContext) empty = ls_context_new();
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
				ls_prompts_render(self->prompts, recipe, config, empty, error);
			if (rendered == NULL)
				return FALSE;
		} else if (type == LS_TYPE_VALIDATE_STEP) {
			JsonObject *options = json_object_has_member(config, "options")
									  ? json_object_get_object_member(config, "options")
									  : NULL;
			JsonNode *node = options != NULL ? json_object_get_member(options, "required") : NULL;
			JsonArray *paths;
			guint j;
			if (options != NULL && !ls_object_keys(options, "|required|", error))
				return FALSE;
			if (node == NULL || !JSON_NODE_HOLDS_ARRAY(node) ||
				json_array_get_length(json_node_get_array(node)) == 0) {
				g_set_error_literal(
					error,
					LS_ERROR,
					LS_ERROR_INVALID,
					"validate requires nonempty options.required array"
				);
				return FALSE;
			}
			paths = json_node_get_array(node);
			for (j = 0; j < json_array_get_length(paths); j++) {
				node = json_array_get_element(paths, j);
				if (json_node_get_value_type(node) != G_TYPE_STRING ||
					!ls_path_valid(json_node_get_string(node))) {
					g_set_error_literal(
						error,
						LS_ERROR,
						LS_ERROR_INVALID,
						"validate required paths must be safe relative strings"
					);
					return FALSE;
				}
			}
		}
	}
	return TRUE;
}
/* The checkpoint contains the entire last successful project; failed stage
 * output never becomes input on resume. Fingerprints bind recipe and catalog. */
static gchar *
checkpoint_text(LsContext *context, const gchar *fingerprint, guint next){
	g_autoptr(JsonParser) parser = json_parser_new();
	g_autofree gchar *snapshot = ls_context_snapshot(context);
	JsonObject *object;
	json_parser_load_from_data(parser, snapshot, -1, NULL);
	object = json_node_get_object(json_parser_get_root(parser));
	json_object_set_string_member(object, "fingerprint", fingerprint);
	json_object_set_int_member(object, "next", next);
	json_object_set_int_member(object, "version", 1);
	return ls_json_encode(json_parser_get_root(parser));
}
static gboolean
save_checkpoint(
	const gchar *path, LsContext *context, const gchar *fingerprint, guint next, GError **error
){
	g_autofree gchar *text = checkpoint_text(context, fingerprint, next);
	if (strlen(text) > LS_MAX_DOCUMENT) {
		g_set_error_literal(
			error, LS_ERROR, LS_ERROR_LIMIT, "Checkpoint exceeds 16 MiB; split the recipe"
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
	if (!ls_object_keys(object, "|version|fingerprint|next|files|", error))
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
	json_object_remove_member(object, "fingerprint");
	json_object_remove_member(object, "next");
	json_object_remove_member(object, "version");
	bundle = ls_json_encode(parsed);
	if (!ls_context_apply(context, bundle, error))
		return FALSE;
	*next = (guint)index;
	return TRUE;
invalid:
	g_set_error_literal(
		error,
		LS_ERROR,
		LS_ERROR_CONFLICT,
		"Checkpoint is corrupt or recipe/prompts changed; use a new output directory"
	);
	return FALSE;
}
/* Lock one workspace through publication. All generated code remains inert;
 * publication is a same-filesystem directory move with no overwrite flag. */
gboolean
ls_pipeline_run(
	LsPipeline *self,
	LsRecipe *recipe,
	const gchar *output,
	gboolean resume,
	GCancellable *cancellable,
	GError **error
){
	g_autoptr(LsContext) context = ls_context_new();
	g_autofree gchar *absolute = NULL, *work = NULL, *lock_path = NULL, *checkpoint = NULL;
	g_autofree gchar *recipe_json = NULL, *fingerprint_input = NULL, *fingerprint = NULL;
	g_autofree gchar *export_path = NULL, *uuid = NULL;
	g_autoptr(JsonNode) recipe_node = json_node_new(JSON_NODE_OBJECT);
	g_autoptr(GFile) source = NULL, destination = NULL;
	JsonArray *steps = ls_recipe_get_steps(recipe);
	GStatBuf info;
	guint next = 0, i;
	gint lock_fd = -1;
	gboolean success = FALSE;
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
			LS_ERROR_CONFLICT,
			"Workspace is locked by another run or lock is invalid"
		);
		goto out;
	}
	json_node_set_object(recipe_node, ls_recipe_get_root(recipe));
	recipe_json = ls_json_encode(recipe_node);
	fingerprint_input = g_strconcat(
		"libresite-checkpoint-v1\n", recipe_json, "\n", ls_prompts_get_digest(self->prompts), NULL
	);
	fingerprint = g_compute_checksum_for_string(G_CHECKSUM_SHA256, fingerprint_input, -1);
	if (resume && g_lstat(checkpoint, &info) == 0) {
		if (!S_ISREG(info.st_mode)) {
			g_set_error_literal(
				error, LS_ERROR, LS_ERROR_CONFLICT, "Checkpoint must be a regular file"
			);
			goto out;
		}
		if (!load_checkpoint(
				checkpoint, fingerprint, json_array_get_length(steps), context, &next, error
			))
			goto out;
	} else if (!save_checkpoint(checkpoint, context, fingerprint, 0, error))
		goto out;
	for (i = next; i < json_array_get_length(steps); i++) {
		JsonObject *config = json_array_get_object_element(steps, i);
		const gchar *id = ls_string(config, "id");
		GType type =
			(GType)GPOINTER_TO_SIZE(g_hash_table_lookup(self->types, ls_string(config, "type")));
		g_autoptr(LsStep) step = g_object_new(type, NULL);
		g_signal_emit(self, signals[0], 0, id, i);
		if (!ls_step_execute(
				step, recipe, config, self->prompts, context, self->provider, cancellable, error
			)) {
			if (error != NULL && *error != NULL)
				g_prefix_error(error, "Step '%s': ", id);
			goto out;
		}
		if (g_cancellable_set_error_if_cancelled(cancellable, error) ||
			!save_checkpoint(checkpoint, context, fingerprint, i + 1, error))
			goto out;
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
		if (!save_checkpoint(metadata, context, fingerprint, json_array_get_length(steps), error))
			goto out;
	}
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
