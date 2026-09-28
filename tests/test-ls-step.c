/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "libresite.h"
#include <string.h>

/* A minimal plugin-style step to exercise the base class contract. */
typedef struct {
	LsStep parent_instance;
	gint mode;	/* 0 succeed, 1 fail silently, 2 call provider */
} ProbeStep;
typedef struct { LsStepClass parent_class; } ProbeStepClass;
GType probe_step_get_type(void);
G_DEFINE_TYPE(ProbeStep, probe_step, LS_TYPE_STEP)

static gboolean
probe_execute(LsStep *self, LsRecipe *recipe, JsonObject *config, LsPrompts *prompts,
	LsContext *context, LsProvider *provider, GCancellable *cancellable, GError **error){
	ProbeStep *probe = (ProbeStep *)self;
	(void)recipe;
	(void)prompts;
	(void)context;
	if (probe->mode == 1)
		return FALSE;
	if (probe->mode == 2) {
		g_autofree gchar *first = ls_provider_generate(provider, config, "s", "prompt", cancellable, error);
		g_autofree gchar *second = ls_provider_generate(provider, config, "s", "prompt", cancellable, error);
		return first != NULL && second != NULL;
	}
	return TRUE;
}
static void
probe_step_class_init(ProbeStepClass *klass){
	LS_STEP_CLASS(klass)->execute = probe_execute;
}
static void
probe_step_init(ProbeStep *self){
	(void)self;
}

typedef struct { GObject parent_instance; } EchoProvider;
typedef struct { GObjectClass parent_class; } EchoProviderClass;
static void echo_iface_init(LsProviderInterface *iface);
GType echo_provider_get_type(void);
G_DEFINE_TYPE_WITH_CODE(EchoProvider, echo_provider, G_TYPE_OBJECT,
	G_IMPLEMENT_INTERFACE(LS_TYPE_PROVIDER, echo_iface_init))
static gchar *
echo_generate(LsProvider *provider, JsonObject *settings, const gchar *system, const gchar *prompt,
	GCancellable *cancellable, GError **error){
	(void)provider;
	(void)settings;
	(void)system;
	(void)cancellable;
	(void)error;
	return g_strdup(prompt);
}
static void
echo_iface_init(LsProviderInterface *iface){
	iface->generate = echo_generate;
}
static void
echo_provider_class_init(EchoProviderClass *klass){
	(void)klass;
}
static void
echo_provider_init(EchoProvider *self){
	(void)self;
}

static JsonObject *
config(void){
	JsonObject *object = json_object_new();
	json_object_set_string_member(object, "id", "probe");
	json_object_set_string_member(object, "provider", "echo");
	return object;
}

/* Usage records every provider call of one execution, including byte totals. */
static void
test_usage(void){
	g_autoptr(LsStep) base = g_object_new(probe_step_get_type(), NULL);
	ProbeStep *step = (ProbeStep *)base;
	g_autoptr(GObject) provider = g_object_new(echo_provider_get_type(), NULL);
	g_autoptr(JsonObject) settings = config();
	g_autoptr(JsonObject) usage = NULL;
	g_autoptr(GError) error = NULL;
	g_assert_null(ls_step_dup_usage(LS_STEP(step)));
	step->mode = 2;
	g_assert_true(ls_step_execute(LS_STEP(step), NULL, settings, NULL, NULL, LS_PROVIDER(provider), NULL, &error));
	g_assert_no_error(error);
	usage = ls_step_dup_usage(LS_STEP(step));
	g_assert_cmpstr(ls_string(usage, "step"), ==, "probe");
	g_assert_cmpstr(ls_string(usage, "provider"), ==, "echo");
	g_assert_cmpint(json_object_get_int_member(usage, "attempts"), ==, 2);
	g_assert_cmpint(json_object_get_int_member(usage, "retries"), ==, 1);
	g_assert_cmpint(json_object_get_int_member(usage, "bytes_in"), ==, 14);
	g_assert_cmpint(json_object_get_int_member(usage, "bytes_out"), ==, 12);
	g_assert_true(json_object_get_boolean_member(usage, "success"));
}

/* A step that fails without diagnostics still reports an error, never success. */
static void
test_silent_failure(void){
	g_autoptr(LsStep) base = g_object_new(probe_step_get_type(), NULL);
	ProbeStep *step = (ProbeStep *)base;
	g_autoptr(JsonObject) settings = config();
	g_autoptr(GError) error = NULL;
	g_autoptr(JsonObject) usage = NULL;
	step->mode = 1;
	g_assert_false(ls_step_execute(LS_STEP(step), NULL, settings, NULL, NULL, NULL, NULL, &error));
	g_assert_error(error, LS_ERROR, LS_ERROR_INVALID);
	usage = ls_step_dup_usage(LS_STEP(step));
	g_assert_false(json_object_get_boolean_member(usage, "success"));
}

/* Cancellation wins over a step that claims success. */
static void
test_cancelled(void){
	g_autoptr(LsStep) base = g_object_new(probe_step_get_type(), NULL);
	ProbeStep *step = (ProbeStep *)base;
	g_autoptr(JsonObject) settings = config();
	g_autoptr(GError) error = NULL;
	g_autoptr(GCancellable) cancel = g_cancellable_new();
	g_cancellable_cancel(cancel);
	g_assert_false(ls_step_execute(LS_STEP(step), NULL, settings, NULL, NULL, NULL, cancel, &error));
	g_assert_error(error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
}

/* Built-in component steps are concrete, registered types. */
static void
test_types(void){
	g_assert_true(g_type_is_a(LS_TYPE_PLAN_STEP, LS_TYPE_STEP));
	g_assert_true(g_type_is_a(LS_TYPE_PAGES_STEP, LS_TYPE_STEP));
	g_assert_false(G_TYPE_IS_ABSTRACT(LS_TYPE_PAGES_STEP));
}

int
main(int argc, char **argv){
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/step/usage", test_usage);
	g_test_add_func("/step/silent-failure", test_silent_failure);
	g_test_add_func("/step/cancelled", test_cancelled);
	g_test_add_func("/step/types", test_types);
	return g_test_run();
}
