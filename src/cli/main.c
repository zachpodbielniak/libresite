/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "libresite.h"
#include <glib-unix.h>
#include <gmodule.h>
#include <signal.h>
#include <stdio.h>
GResource *ls_resources_get_resource(void);
/* Execute on a worker thread so the main context can deliver SIGINT/SIGTERM. */
typedef struct {
	GMainLoop *loop;
	LsPipeline *pipeline;
	LsRecipe *recipe;
	GCancellable *cancellable;
	const gchar *output;
	const gchar *project;
	gboolean resume;
	gboolean okay;
	GError *error;
} Run;
/* Worker signals and the main-context heartbeat share only this locked state. */
typedef struct {
	GMutex mutex;
	gchar *id;
	gchar *message;
	guint index;
	gint64 since;
} Progress;
static gboolean
quit_loop(gpointer data){
	g_main_loop_quit((GMainLoop *)data);
	return G_SOURCE_REMOVE;
}
static gpointer
run_thread(gpointer data){
	Run *run = (Run *)data;
	run->okay = run->project != NULL
		? ls_pipeline_revise(run->pipeline, run->recipe, run->project, run->output,
			run->resume, run->cancellable, &run->error)
		: ls_pipeline_run(run->pipeline, run->recipe, run->output, run->resume,
			run->cancellable, &run->error);
	g_idle_add_full(G_PRIORITY_DEFAULT, quit_loop, run->loop, NULL);
	return NULL;
}
static gboolean
cancel_run(gpointer data){
	g_cancellable_cancel(G_CANCELLABLE(data));
	return G_SOURCE_CONTINUE;
}
/* Progress is stderr; stdout remains useful for plans and prompt inspection. */
static void
print_progress(const gchar *id, guint index, const gchar *message){
	g_autoptr(GDateTime) now = g_date_time_new_now_local();
	g_autofree gchar *timestamp = g_date_time_format(now, "%Y-%m-%dT%H:%M:%S%z");
	/* Capture each event's local wall time and offset on the emitting thread. */
	g_printerr("[%s] [%u] %s: %s\n", timestamp, index + 1, id, message);
}
static void
step_started(LsPipeline *pipeline, const gchar *id, guint index, gpointer data){
	Progress *progress = (Progress *)data;
	(void)pipeline;
	g_mutex_lock(&progress->mutex);
	g_free(progress->id);
	g_free(progress->message);
	progress->id = g_strdup(id);
	progress->message = g_strdup("executing step");
	progress->index = index;
	progress->since = g_get_monotonic_time();
	print_progress(id, index, "started");
	g_mutex_unlock(&progress->mutex);
}
static void
step_completed(LsPipeline *pipeline, const gchar *id, guint index, gpointer data){
	Progress *progress = (Progress *)data;
	(void)pipeline;
	g_mutex_lock(&progress->mutex);
	g_clear_pointer(&progress->id, g_free);
	print_progress(id, index, "checkpoint saved");
	g_mutex_unlock(&progress->mutex);
}
/* Reset elapsed time at each attempt or retry, without touching checkpoints. */
static void
step_progress(LsPipeline *pipeline, const gchar *id, guint index,
	const gchar *message, gpointer data){
	Progress *progress = (Progress *)data;
	(void)pipeline;
	g_mutex_lock(&progress->mutex);
	g_free(progress->message);
	progress->message = g_strdup(message);
	progress->since = g_get_monotonic_time();
	print_progress(id, index, message);
	g_mutex_unlock(&progress->mutex);
}
/* This is elapsed waiting time, not a claim that the provider is making progress. */
static gboolean
heartbeat(gpointer data){
	Progress *progress = (Progress *)data;
	g_mutex_lock(&progress->mutex);
	if (progress->id != NULL) {
		g_autofree gchar *message = g_strdup_printf(
			"still waiting (%" G_GINT64_FORMAT "s); %s; checkpoint unchanged until step succeeds",
			(g_get_monotonic_time() - progress->since) / G_USEC_PER_SEC,
			progress->message);
		print_progress(progress->id, progress->index, message);
	}
	g_mutex_unlock(&progress->mutex);
	return G_SOURCE_CONTINUE;
}
/* GModule extensions are explicit user-supplied native code; keep modules
 * resident because their registered GTypes outlive the entrypoint call. */
static gboolean
load_plugins(LsPipeline *pipeline, gchar **paths, GError **error){
	guint i;
	if (paths == NULL)
		return TRUE;
	for (i = 0; paths[i] != NULL; i++) {
		GModule *module = g_module_open(paths[i], G_MODULE_BIND_LOCAL);
		gboolean (*register_plugin)(LsPipeline *, GError **) = NULL;
		g_autoptr(GError) local_error = NULL;
		gboolean registered;
		if (module == NULL) {
			g_set_error(
				error, LS_ERROR, LS_ERROR_INVALID, "Cannot load plugin: %s", g_module_error()
			);
			return FALSE;
		}
		if (!g_module_symbol(module, "ls_plugin_register", (gpointer *)&register_plugin)) {
			g_set_error(
				error,
				LS_ERROR,
				LS_ERROR_INVALID,
				"Plugin needs ls_plugin_register: %s",
				g_module_error()
			);
			g_module_close(module);
			return FALSE;
		}
		g_module_make_resident(module);
		registered = register_plugin(pipeline, &local_error);
		g_module_close(module);
		/* A plugin can register types before failing, so it remains resident.
		 * Match step/provider dispatch: never continue on success plus error. */
		if (!registered && local_error == NULL)
			g_set_error_literal(&local_error, LS_ERROR, LS_ERROR_INVALID,
				"Plugin registration failed without an error");
		if (local_error != NULL) {
			g_prefix_error(&local_error, "Plugin '%s': ", paths[i]);
			g_propagate_error(error, g_steal_pointer(&local_error));
			return FALSE;
		}
	}
	return TRUE;
}
/* A single option parser supports manual use and deterministic agent calls. */
int
main(int argc, char **argv){
	g_autofree gchar *recipe_path = NULL, *output = NULL, *catalog = NULL, *preview = NULL;
	g_autofree gchar *form_path = NULL;
	gboolean emit_yaml = FALSE;
	g_auto(GStrv) plugins = NULL;
	gint64 max_prompt_bytes = 0;
	gint timeout = -1;
	gboolean unlimited = FALSE;
	gboolean validate = FALSE, plan = FALSE, resume = FALSE, license = FALSE, version = FALSE,
			 list = FALSE, debug = FALSE;
	g_autoptr(GOptionContext) options = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(LsRecipe) recipe = NULL;
	g_autoptr(LsPrompts) prompts = NULL;
	g_autoptr(LsAiProvider) provider = NULL;
	g_autoptr(LsPipeline) pipeline = NULL;
	g_autofree gchar *yaml = NULL;
	g_autofree gchar *project = NULL;
	g_autoptr(LsContext) preview_context = NULL;
	GOptionEntry entries[] = {
		{"revise", 0, 0, G_OPTION_ARG_FILENAME, &project,
		 "Load an existing text project; site.brief describes the changes", "DIR"},


		{"from-form", 0, 0, G_OPTION_ARG_FILENAME, &form_path,
		 "Validate structured JSON form data and emit a YAML recipe", "FILE"},
		{"emit-yaml", 0, 0, G_OPTION_ARG_NONE, &emit_yaml,
		 "Emit the validated recipe as YAML without generation", NULL},
		{"unlimited", 0, 0, G_OPTION_ARG_NONE, &unlimited,
		 "Disable provider time and requested output budgets for this run", NULL},
		{"timeout", 0, 0, G_OPTION_ARG_INT, &timeout,
		 "Override each attempt deadline (0 = unlimited, omitted = recipe)", "SECONDS"},
		{"debug", 0, 0, G_OPTION_ARG_NONE, &debug,
		 "Persist provider sessions and log diagnostic metadata to stderr", NULL},
		{"recipe", 'r', 0, G_OPTION_ARG_FILENAME, &recipe_path, "YAML recipe", "FILE"},
		{"max-prompt-bytes", 0, 0, G_OPTION_ARG_INT64, &max_prompt_bytes,
		 "System plus task byte limit (0 = unlimited, the default)", "BYTES"},
		{"output",
		 'o',
		 0,
		 G_OPTION_ARG_FILENAME,
		 &output,
		 "New output directory (parent must exist)",
		 "DIR"},
		{"prompts",
		 0,
		 0,
		 G_OPTION_ARG_FILENAME,
		 &catalog,
		 "Replace embedded prompt catalog",
		 "FILE"},
		{"plugin",
		 0,
		 0,
		 G_OPTION_ARG_FILENAME_ARRAY,
		 &plugins,
		 "Load a native step plugin (repeatable)",
		 "FILE"},
		{"validate",
		 0,
		 0,
		 G_OPTION_ARG_NONE,
		 &validate,
		 "Validate without calling providers",
		 NULL},
		{"plan",
		 0,
		 0,
		 G_OPTION_ARG_NONE,
		 &plan,
		 "Print resolved steps, providers and models",
		 NULL},
		{"preview",
		 0,
		 0,
		 G_OPTION_ARG_STRING,
		 &preview,
		 "Print system and step prompt (loads --revise source when supplied)",
		 "STEP-ID"},
		{"resume", 0, 0, G_OPTION_ARG_NONE, &resume, "Resume OUTPUT.libresite-work", NULL},
		{"list-modules", 0, 0, G_OPTION_ARG_NONE, &list, "List built-in step modules", NULL},
		{"license", 0, 0, G_OPTION_ARG_NONE, &license, "Print the AGPLv3 license", NULL},
		{"version", 0, 0, G_OPTION_ARG_NONE, &version, "Print version", NULL},
		{NULL, 0, 0, G_OPTION_ARG_NONE, NULL, NULL, NULL}
	};
	options = g_option_context_new("- recipe-driven AI site builder");
	g_option_context_add_main_entries(options, entries, NULL);
	g_option_context_set_description(
		options,
		"Examples:\n  libresite --from-form form.json > site.yaml\n"
		"  libresite -r site.yaml --emit-yaml\n  libresite -r recipes/landing.yaml --validate\n  libresite -r "
		"recipes/landing.yaml --plan\n  libresite -r recipes/landing.yaml --preview design\n  "
		"libresite -r recipes/landing.yaml -o ./my-site\n  libresite -r recipes/landing.yaml -o "
		"./my-site --resume\n  libresite -r recipes/revision.yaml --revise ./my-site "
		"-o ./my-site-v2\n\nProviders: codex, claude-code, grok-build, cursor, "
		"antigravity.\nGeneration uses your installed and authenticated provider CLI.\nGenerated "
		"code is published as files; no build, deployment or hosting is automatic."
	);
	if (!g_option_context_parse(options, &argc, &argv, &error))
		goto fail;
	if (argc != 1) {
		g_set_error_literal(
			&error, LS_ERROR, LS_ERROR_INVALID, "Unexpected positional argument; use --recipe FILE"
		);
		goto fail;
	}
	if (license) {
		g_autoptr(GBytes) bytes = g_resource_lookup_data(
			ls_resources_get_resource(), "/org/libresite/LICENSE", 0, &error
		);
		if (bytes == NULL)
			goto fail;
		(void)fwrite(g_bytes_get_data(bytes, NULL), 1, g_bytes_get_size(bytes), stdout);
		return 0;
	}
	if (version) {
		g_print("libresite %s\n", LS_VERSION);
		return 0;
	}
	if (list) {
		g_print("brief\ndesign\nfrontend\nbackend\ndatabase\nreview\ngenerate\nvalidate\nscaffold\n");
		return 0;
	}
	if (form_path != NULL) {
		if (recipe_path != NULL || output != NULL || project != NULL ||
			resume || validate || plan || preview != NULL) {
			g_set_error_literal(&error, LS_ERROR, LS_ERROR_INVALID,
				"--from-form emits YAML; do not combine with --recipe or generation/inspection modes");
			goto fail;
		}
		emit_yaml = TRUE;
	}
	if ((guint)validate + (guint)plan + (guint)(preview != NULL) + (guint)emit_yaml > 1 ||
		(emit_yaml && (output != NULL || project != NULL || resume))) {
		g_set_error_literal(
			&error, LS_ERROR, LS_ERROR_INVALID, "Choose one of --validate, --plan, --preview or --emit-yaml"
		);
		goto fail;
	}
	if (recipe_path == NULL && form_path == NULL) {
		g_set_error_literal(&error, LS_ERROR, LS_ERROR_INVALID, "--recipe is required; see --help");
		goto fail;
	}
	yaml = ls_read_text(form_path != NULL ? form_path : recipe_path, &error);
	if (yaml == NULL)
		goto fail;
	if (timeout < -1 || timeout > 86400) {
		g_set_error_literal(&error, LS_ERROR, LS_ERROR_INVALID,
			"--timeout must be between 0 and 86400 seconds");
		goto fail;
	}
	recipe = form_path != NULL ? ls_recipe_from_form(yaml, &error) : ls_recipe_new(yaml, &error);
	if (recipe != NULL) {
		ls_recipe_set_timeout(recipe, timeout);
		ls_recipe_set_unlimited(recipe, unlimited);
	}
	prompts = recipe != NULL ? ls_prompts_new(catalog, &error) : NULL;
	if (prompts == NULL)
		goto fail;
	if (max_prompt_bytes < 0 || (guint64)max_prompt_bytes > G_MAXSIZE) {
		g_set_error_literal(&error, LS_ERROR, LS_ERROR_INVALID,
			"--max-prompt-bytes must be nonnegative and fit the platform size type");
		goto fail;
	}
	ls_prompts_set_max_bytes(prompts, (gsize)max_prompt_bytes);
	provider = ls_ai_provider_new();
	ls_ai_provider_set_debug(provider, debug);
	pipeline = ls_pipeline_new(LS_PROVIDER(provider), prompts);
	if (!load_plugins(pipeline, plugins, &error) || !ls_pipeline_validate(pipeline, recipe, &error))
		goto fail;
	/* Inspection uses the same import and rendering boundaries as execution. */
	if (project != NULL && (validate || plan || preview != NULL)) {
		preview_context = ls_context_import(project, NULL, &error);
		if (preview_context == NULL ||
			!ls_recipe_seed_context(recipe, preview_context, &error) ||
			!ls_pipeline_validate_context(pipeline, recipe, preview_context, &error))
			goto fail;
	}


	if (emit_yaml) {
		g_autofree gchar *emitted = ls_recipe_to_yaml(recipe, &error);
		if (emitted == NULL)
			goto fail;
		if (fputs(emitted, stdout) == EOF || fflush(stdout) == EOF) {
			g_set_error_literal(&error, LS_ERROR, LS_ERROR_INVALID, "Cannot write YAML to stdout");
			goto fail;
		}
		return 0;
	}
	if (validate) {
		g_print("Recipe valid\n");
		return 0;
	}
	if (plan || preview != NULL) {
		JsonArray *steps = ls_recipe_get_steps(recipe);
		guint i;
		for (i = 0; i < json_array_get_length(steps); i++) {
			JsonObject *step = json_array_get_object_element(steps, i);
			g_autoptr(JsonObject) settings = ls_recipe_resolve_ai(recipe, step);
			gboolean native = g_str_equal(ls_string(step, "type"), "validate") ||
				g_str_equal(ls_string(step, "type"), "scaffold");
			/* Preview deterministic predecessors without running AI stages. This
			 * is the same bundle the native step commits during execution. */
			if (preview != NULL && g_str_equal(ls_string(step, "type"), "scaffold")) {
				JsonNode *node = json_object_get_member(step, "options");
				g_autofree gchar *bundle = NULL;
				if (preview_context == NULL)
					preview_context = ls_recipe_create_context(recipe, &error);
				if (preview_context == NULL)
					goto fail;
				bundle = ls_scaffold_step_render(node != NULL ? json_node_get_object(node) : NULL,
					preview_context, &error);
				if (bundle == NULL || !ls_context_apply(preview_context, bundle, &error))
					goto fail;
				if (g_str_equal(preview, ls_string(step, "id"))) {
					g_print("%s\n", bundle);
					return 0;
				}
				continue;
			}
			if (preview != NULL && g_str_equal(preview, ls_string(step, "id"))) {
				g_autofree gchar *text = NULL;
				if (preview_context == NULL)
					preview_context = ls_recipe_create_context(recipe, &error);
				if (preview_context == NULL)
					goto fail;
				text = ls_prompts_render(prompts, recipe, step, preview_context, &error);
				if (text == NULL)
					goto fail;
				g_print("%s\n\n%s", ls_prompts_get_system(prompts), text);
				return 0;
			}
			if (plan)
				g_print(
					"%u\t%s\t%s\t%s\t%s\n",
					i + 1,
					ls_string(step, "id"),
					ls_string(step, "type"),
					native
						? "native"
						: ls_string(settings, "provider"),
					native ? "(none)" : ls_string(settings, "model") != NULL ? ls_string(settings, "model")
														 : "(provider default)"
				);
		}
		if (plan)
			return 0;
		g_set_error(&error, LS_ERROR, LS_ERROR_INVALID, "No step with id '%s'", preview);
		goto fail;
	}
	if (output == NULL) {
		g_set_error_literal(
			&error, LS_ERROR, LS_ERROR_INVALID, "--output is required for generation"
		);
		goto fail;
	}
	{
		g_autoptr(GMainLoop) loop = g_main_loop_new(NULL, FALSE);
		g_autoptr(GCancellable) cancellable = g_cancellable_new();
		Run run = {loop, pipeline, recipe, cancellable, output, project, resume, FALSE, NULL};
		Progress progress = {0};
		guint pulse;
		guint interrupt = g_unix_signal_add(SIGINT, cancel_run, cancellable);
		guint terminate = g_unix_signal_add(SIGTERM, cancel_run, cancellable);
		GThread *thread;
		g_mutex_init(&progress.mutex);
		g_signal_connect(pipeline, "step-started", G_CALLBACK(step_started), &progress);
		g_signal_connect(pipeline, "step-completed", G_CALLBACK(step_completed), &progress);
		g_signal_connect(pipeline, "step-progress", G_CALLBACK(step_progress), &progress);
		pulse = g_timeout_add_seconds(30, heartbeat, &progress);
		thread = g_thread_new("libresite-run", run_thread, &run);
		g_main_loop_run(loop);
		g_thread_join(thread);
		g_source_remove(pulse);
		g_signal_handlers_disconnect_by_data(pipeline, &progress);
		g_free(progress.id);
		g_free(progress.message);
		g_mutex_clear(&progress.mutex);
		g_source_remove(interrupt);
		g_source_remove(terminate);
		if (!run.okay) {
			error = run.error;
			goto fail;
		}
	}
	g_print("Published %s\n", output);
	return 0;
fail:
	g_printerr("libresite: %s\n", error != NULL ? error->message : "operation failed");
	return error != NULL && g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED) ? 130 : 1;
}
