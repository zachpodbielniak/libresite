/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "libresite.h"
#include <gmodule.h>
typedef struct {
	LsStep parent_instance;
} SeedStep;
typedef struct {
	LsStepClass parent_class;
} SeedStepClass;
GType seed_step_get_type(void);
G_MODULE_EXPORT gboolean ls_plugin_register(LsPipeline *pipeline, GError **error);
G_DEFINE_TYPE(SeedStep, seed_step, LS_TYPE_STEP)
/* A real out-of-tree module can generate artifacts without an AI transport. */
static gboolean
seed_execute(
	LsStep *self,
	LsRecipe *recipe,
	JsonObject *config,
	LsPrompts *prompts,
	LsContext *context,
	LsProvider *provider,
	GCancellable *cancellable,
	GError **error
){
	(void)self;
	(void)recipe;
	(void)config;
	(void)prompts;
	(void)provider;
	(void)cancellable;
	return ls_context_apply(
		context,
		"{\"files\":[{\"path\":\"index.html\",\"content\":\"<!doctype "
		"html><title>Plugin</title><h1>Native module</h1>\"},"
		"{\"path\":\".gitignore\",\"content\":\"/custom-cache/\\n\"},"
		"{\"path\":\".env\",\"content\":\"FIXTURE_SECRET=example\"},"
		"{\"path\":\".env.example\",\"content\":\"FIXTURE_SECRET=\"},"
		"{\"path\":\"build/output.o\",\"content\":\"fixture\"},"
		"{\"path\":\"custom-cache/item\",\"content\":\"fixture\"},"
		"{\"path\":\"package-lock.json\",\"content\":\"{}\"}]}",
		error
	);
}
static void
seed_step_class_init(SeedStepClass *klass){
	LS_STEP_CLASS(klass)->execute = seed_execute;
}
static void
seed_step_init(SeedStep *self){
	(void)self;
}
/**
 * ls_plugin_register:
 * @pipeline: host pipeline
 * @error: return location for an error
 * Returns: whether the native seed module was registered
 */
gboolean
ls_plugin_register(LsPipeline *pipeline, GError **error){
	return ls_pipeline_register(pipeline, "seed", seed_step_get_type(), error);
}
