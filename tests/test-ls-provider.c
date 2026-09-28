/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "interfaces/ls-provider.h"
#include "core/ls-util.h"
#include <string.h>

typedef struct {
	GObject parent_instance;
	gint mode;	/* 0 echo, 1 NULL without error, 2 wait for cancellation */
	gint calls;
} FakeProvider;
typedef struct { GObjectClass parent_class; } FakeProviderClass;
static void fake_iface_init(LsProviderInterface *iface);
GType fake_provider_get_type(void);
G_DEFINE_TYPE_WITH_CODE(FakeProvider, fake_provider, G_TYPE_OBJECT,
	G_IMPLEMENT_INTERFACE(LS_TYPE_PROVIDER, fake_iface_init))

static gchar *
fake_generate(LsProvider *provider, JsonObject *settings, const gchar *system,
	const gchar *prompt, GCancellable *cancellable, GError **error){
	FakeProvider *self = (FakeProvider *)provider;
	(void)settings;
	(void)system;
	(void)error;
	g_atomic_int_inc(&self->calls);
	if (self->mode == 1)
		return NULL;
	if (self->mode == 2) {
		while (!g_cancellable_is_cancelled(cancellable))
			g_usleep(1000);
		return g_strdup("late");
	}
	return g_strdup_printf("echo:%s", prompt);
}
static void
fake_iface_init(LsProviderInterface *iface){
	iface->generate = fake_generate;
}
static void
fake_provider_class_init(FakeProviderClass *klass){
	(void)klass;
}
static void
fake_provider_init(FakeProvider *self){
	(void)self;
}

typedef struct {
	gint count;
	gint64 bytes_in;
	gint64 bytes_out;
	GThread *thread;
} UsageSeen;

static void
on_usage(LsProvider *provider, const gchar *name, const gchar *model,
	gint64 bytes_in, gint64 bytes_out, gpointer data){
	UsageSeen *seen = data;
	(void)provider;
	(void)name;
	(void)model;
	seen->count++;
	seen->bytes_in = bytes_in;
	seen->bytes_out = bytes_out;
	seen->thread = g_thread_self();
}

static JsonObject *
settings(void){
	JsonObject *object = json_object_new();
	json_object_set_string_member(object, "provider", "fake");
	return object;
}

/* Dispatch measures but never emits; recording emits once on the caller. */
static void
test_dispatch_record(void){
	g_autoptr(GObject) provider = g_object_new(fake_provider_get_type(), NULL);
	g_autoptr(JsonObject) config = settings();
	g_autoptr(GError) error = NULL;
	UsageSeen seen = {0, 0, 0, NULL};
	gint64 in = -1, out = -1;
	g_autofree gchar *response = NULL, *again = NULL;
	g_signal_connect(provider, "usage", G_CALLBACK(on_usage), &seen);
	response = ls_provider_dispatch(LS_PROVIDER(provider), config, "sys", "abc", &in, &out, NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpstr(response, ==, "echo:abc");
	g_assert_cmpint(in, ==, 6);
	g_assert_cmpint(out, ==, 8);
	g_assert_cmpint(seen.count, ==, 0);
	ls_provider_record_usage(LS_PROVIDER(provider), config, in, out);
	g_assert_cmpint(seen.count, ==, 1);
	g_assert_cmpint(seen.bytes_in, ==, 6);
	g_assert_cmpint(seen.bytes_out, ==, 8);
	g_assert_true(seen.thread == g_thread_self());
	again = ls_provider_generate(LS_PROVIDER(provider), config, "sys", "abc", NULL, &error);
	g_assert_cmpstr(again, ==, "echo:abc");
	g_assert_cmpint(seen.count, ==, 2);
}

/* Dispatch keeps generate's normalization: no silent NULL, cancellation wins. */
static void
test_dispatch_errors(void){
	g_autoptr(GObject) provider = g_object_new(fake_provider_get_type(), NULL);
	g_autoptr(JsonObject) config = settings();
	g_autoptr(GError) error = NULL;
	g_autoptr(GCancellable) cancel = g_cancellable_new();
	gint64 in = -1, out = -1;
	gchar *response;
	((FakeProvider *)provider)->mode = 1;
	response = ls_provider_dispatch(LS_PROVIDER(provider), config, "", "x", &in, &out, NULL, &error);
	g_assert_null(response);
	g_assert_error(error, LS_ERROR, LS_ERROR_PROVIDER);
	g_assert_cmpint(in, ==, 1);
	g_assert_cmpint(out, ==, 0);
	g_clear_error(&error);
	g_cancellable_cancel(cancel);
	((FakeProvider *)provider)->mode = 0;
	response = ls_provider_dispatch(LS_PROVIDER(provider), config, "", "x", &in, &out, cancel, &error);
	g_assert_null(response);
	g_assert_error(error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
	g_assert_cmpint(((FakeProvider *)provider)->calls, ==, 1);
}

typedef struct {
	LsProvider *provider;
	JsonObject *settings;
	gchar *response;
} Worker;

static gpointer
worker(gpointer data){
	Worker *job = data;
	gint64 in, out;
	job->response = ls_provider_dispatch(job->provider, job->settings, "", "w", &in, &out, NULL, NULL);
	return NULL;
}

/* Parallel steps dispatch from worker threads; no signal may fire there. */
static void
test_dispatch_threads(void){
	g_autoptr(GObject) provider = g_object_new(fake_provider_get_type(), NULL);
	g_autoptr(JsonObject) config = settings();
	UsageSeen seen = {0, 0, 0, NULL};
	Worker jobs[6];
	GThread *threads[6];
	guint i;
	g_signal_connect(provider, "usage", G_CALLBACK(on_usage), &seen);
	for (i = 0; i < G_N_ELEMENTS(jobs); i++) {
		jobs[i].provider = LS_PROVIDER(provider);
		jobs[i].settings = config;
		jobs[i].response = NULL;
		threads[i] = g_thread_new("dispatch", worker, &jobs[i]);
	}
	for (i = 0; i < G_N_ELEMENTS(jobs); i++) {
		g_thread_join(threads[i]);
		g_assert_cmpstr(jobs[i].response, ==, "echo:w");
		g_free(jobs[i].response);
	}
	g_assert_cmpint(seen.count, ==, 0);
	g_assert_cmpint(((FakeProvider *)provider)->calls, ==, 6);
}

int
main(int argc, char **argv){
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/provider/dispatch-record", test_dispatch_record);
	g_test_add_func("/provider/dispatch-errors", test_dispatch_errors);
	g_test_add_func("/provider/dispatch-threads", test_dispatch_threads);
	return g_test_run();
}
