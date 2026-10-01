/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "core/ls-edit.h"
#include "core/ls-components.h"
#include "core/ls-context.h"
#include "model/ls-artifact.h"
#include <glib/gstdio.h>
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#define RECORD ".libresite/run.json"
#define OUTLINES ".libresite/outlines.json"
#define PLAN_PATH "site/plan.json"
#define SPEC_DIR "site/pages/"
#define SIGNATURE_PATH "site/signature.css"
#define FINGERPRINT_PATH "site/fingerprint.json"

struct _LsEditor {
	GObject parent_instance;
	LsProvider *provider;
	LsPrompts *prompts;
	JsonObject *settings;
};
G_DEFINE_TYPE(LsEditor, ls_editor, G_TYPE_OBJECT)

static guint progress_signal;

static void
ls_editor_finalize(GObject *object){
	LsEditor *self = LS_EDITOR(object);
	g_clear_object(&self->provider);
	g_clear_object(&self->prompts);
	g_clear_pointer(&self->settings, json_object_unref);
	G_OBJECT_CLASS(ls_editor_parent_class)->finalize(object);
}

static void
ls_editor_class_init(LsEditorClass *klass){
	G_OBJECT_CLASS(klass)->finalize = ls_editor_finalize;
	/**
	 * LsEditor::progress:
	 * @editor: the editor
	 * @message: what the edit is doing
	 *
	 * Emitted on the thread that called ls_editor_apply().
	 */
	progress_signal = g_signal_new("progress", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL,
		NULL, G_TYPE_NONE, 1, G_TYPE_STRING);
}

static void
ls_editor_init(LsEditor *self){
	(void)self;
}

LsEditor *
ls_editor_new(LsProvider *provider, LsPrompts *prompts, JsonObject *settings){
	LsEditor *self;
	g_return_val_if_fail(LS_IS_PROVIDER(provider), NULL);
	g_return_val_if_fail(LS_IS_PROMPTS(prompts), NULL);
	g_return_val_if_fail(settings != NULL, NULL);
	self = g_object_new(LS_TYPE_EDITOR, NULL);
	self->provider = g_object_ref(provider);
	self->prompts = g_object_ref(prompts);
	self->settings = json_object_ref(settings);
	return self;
}

static void
say(LsEditor *self, const gchar *format, ...) G_GNUC_PRINTF(2, 3);
static void
say(LsEditor *self, const gchar *format, ...){
	va_list args;
	g_autofree gchar *message = NULL;
	va_start(args, format);
	message = g_strdup_vprintf(format, args);
	va_end(args);
	g_signal_emit(self, progress_signal, 0, message);
}

static JsonObject *
member_object(JsonObject *object, const gchar *name){
	JsonNode *node = object != NULL ? json_object_get_member(object, name) : NULL;
	return node != NULL && JSON_NODE_HOLDS_OBJECT(node) ? json_node_get_object(node) : NULL;
}

static JsonArray *
member_array(JsonObject *object, const gchar *name){
	JsonNode *node = object != NULL ? json_object_get_member(object, name) : NULL;
	return node != NULL && JSON_NODE_HOLDS_ARRAY(node) ? json_node_get_array(node) : NULL;
}

/* json_node_copy() shares objects and arrays; edits need private trees. */
static JsonNode *
deep_copy(JsonNode *node){
	g_autofree gchar *text = ls_json_encode(node);
	return ls_json_parse(text, NULL);
}

/* path -> entry object (borrowed) for a record's files or archive array. */
static GHashTable *
entry_map(JsonArray *entries){
	GHashTable *map = g_hash_table_new(g_str_hash, g_str_equal);
	guint i;
	for (i = 0; entries != NULL && i < json_array_get_length(entries); i++) {
		JsonNode *node = json_array_get_element(entries, i);
		JsonObject *entry = JSON_NODE_HOLDS_OBJECT(node) ? json_node_get_object(node) : NULL;
		if (entry != NULL && ls_string(entry, "path") != NULL)
			g_hash_table_insert(map, (gpointer)ls_string(entry, "path"), entry);
	}
	return map;
}

static gboolean
same_entry(JsonObject *a, JsonObject *b){
	return a != NULL && b != NULL && g_strcmp0(ls_string(a, "content"), ls_string(b, "content")) == 0 &&
		g_strcmp0(ls_string(a, "encoding"), ls_string(b, "encoding")) == 0;
}

/* The working tree must be committed: an edit becomes exactly one version. */
static gboolean
check_clean(const gchar *project, GCancellable *cancellable, GError **error){
	const gchar *status_args[] = {"status", "--porcelain", "--untracked-files=all", NULL};
	g_autofree gchar *status = NULL;
	if (!ls_git_run(project, status_args, &status, cancellable, error)) {
		g_prefix_error(error, "%s is not a Git working tree: ", project);
		return FALSE;
	}
	if (*g_strstrip(status) != '\0') {
		g_set_error(error, LS_ERROR, LS_ERROR_CONFLICT,
			"%s has uncommitted changes; commit or discard them before editing:\n%.400s", project, status);
		return FALSE;
	}
	return TRUE;
}

/* SHA-256 of an entry's decoded bytes. */
static gchar *
entry_hash(JsonObject *entry){
	const gchar *content = ls_string(entry, "content");
	if (content == NULL)
		return NULL;
	if (g_strcmp0(ls_string(entry, "encoding"), "base64") == 0) {
		gsize length = 0;
		g_autofree guchar *bytes = g_base64_decode(content, &length);
		return g_compute_checksum_for_data(G_CHECKSUM_SHA256, bytes, length);
	}
	return g_compute_checksum_for_string(G_CHECKSUM_SHA256, content, -1);
}

static gboolean
is_outline(const gchar *path){
	return g_str_equal(path, PLAN_PATH) || g_str_has_prefix(path, SPEC_DIR) || g_str_equal(path, SIGNATURE_PATH) ||
		g_str_equal(path, FINGERPRINT_PATH);
}

static gint
by_text(gconstpointer a, gconstpointer b){
	return g_strcmp0(*(const gchar *const *)a, *(const gchar *const *)b);
}

/* The committed outlines: the plan, page specifications, signature and
 * fingerprint, plus a hash of every published file, so a version restored
 * from history carries the outlines that produced it. */
static gchar *
outlines_from(GHashTable *archive, GHashTable *files){
	g_autoptr(JsonNode) node = json_node_new(JSON_NODE_OBJECT);
	g_autoptr(GPtrArray) paths = g_ptr_array_new();
	JsonObject *root = json_object_new();
	JsonArray *outlines = json_array_new();
	JsonObject *hashes = json_object_new();
	GHashTableIter iter;
	gpointer key, value;
	guint i;
	g_hash_table_iter_init(&iter, archive);
	while (g_hash_table_iter_next(&iter, &key, &value))
		if (is_outline(key))
			g_ptr_array_add(paths, key);
	g_ptr_array_sort(paths, by_text);
	for (i = 0; i < paths->len; i++) {
		JsonObject *entry = g_hash_table_lookup(archive, g_ptr_array_index(paths, i));
		JsonObject *copy = json_object_new();
		json_object_set_string_member(copy, "path", ls_string(entry, "path"));
		json_object_set_string_member(copy, "content", ls_string(entry, "content"));
		json_array_add_object_element(outlines, copy);
	}
	g_ptr_array_set_size(paths, 0);
	g_hash_table_iter_init(&iter, files);
	while (g_hash_table_iter_next(&iter, &key, &value))
		if (!g_str_equal(key, ".gitignore"))
			g_ptr_array_add(paths, key);
	g_ptr_array_sort(paths, by_text);
	for (i = 0; i < paths->len; i++) {
		g_autofree gchar *hash = entry_hash(g_hash_table_lookup(files, g_ptr_array_index(paths, i)));
		json_object_set_string_member(hashes, g_ptr_array_index(paths, i), hash != NULL ? hash : "");
	}
	json_object_set_int_member(root, "version", 1);
	json_object_set_array_member(root, "outlines", outlines);
	json_object_set_object_member(root, "files", hashes);
	json_node_take_object(node, root);
	return ls_json_encode(node);
}

gchar *
ls_outlines_text(LsContext *context){
	g_autofree gchar *snapshot = NULL;
	g_autoptr(JsonNode) parsed = NULL;
	g_autoptr(GHashTable) files = NULL, archive = NULL;
	g_return_val_if_fail(LS_IS_CONTEXT(context), NULL);
	snapshot = ls_context_snapshot(context);
	parsed = ls_json_parse(snapshot, NULL);
	if (parsed == NULL || !JSON_NODE_HOLDS_OBJECT(parsed))
		return NULL;
	files = entry_map(member_array(json_node_get_object(parsed), "files"));
	archive = entry_map(member_array(json_node_get_object(parsed), "archive"));
	if (!g_hash_table_contains(archive, PLAN_PATH))
		return NULL;
	return outlines_from(archive, files);
}

/* Every published file on disk must be what the outlines produced. */
static gboolean
check_hashes(GHashTable *files, JsonObject *hashes, GError **error){
	GHashTableIter iter;
	gpointer key, value;
	g_autoptr(GList) names = NULL;
	GList *name;
	g_hash_table_iter_init(&iter, files);
	while (g_hash_table_iter_next(&iter, &key, &value)) {
		g_autofree gchar *hash = NULL;
		if (g_str_equal(key, ".gitignore"))
			continue;
		hash = entry_hash(value);
		if (hashes == NULL || g_strcmp0(hash, ls_string(hashes, key)) != 0) {
			g_set_error(error, LS_ERROR, LS_ERROR_CONFLICT,
				"%s differs from the saved outlines: it was changed outside libresite, so they no longer "
				"describe this site", (const gchar *)key);
			return FALSE;
		}
	}
	names = hashes != NULL ? json_object_get_members(hashes) : NULL;
	for (name = names; name != NULL; name = name->next)
		if (!g_hash_table_contains(files, name->data)) {
			g_set_error(error, LS_ERROR, LS_ERROR_CONFLICT,
				"%s is in the saved outlines but missing from the site", (const gchar *)name->data);
			return FALSE;
		}
	return TRUE;
}

/* Older sites kept outlines only in the untracked record: track them from now on. */
static gchar *
tracked_gitignore(const gchar *text){
	g_auto(GStrv) lines = g_strsplit(text != NULL ? text : "", "\n", -1);
	GString *out = g_string_new(NULL);
	gboolean changed = FALSE;
	guint i;
	for (i = 0; lines[i] != NULL; i++) {
		if (g_str_equal(lines[i], "/.libresite/")) {
			g_string_append(out, "/.libresite/*\n!/.libresite/outlines.json");
			changed = TRUE;
		} else
			g_string_append(out, lines[i]);
		if (lines[i + 1] != NULL)
			g_string_append_c(out, '\n');
	}
	if (!changed) {
		g_string_free(out, TRUE);
		return NULL;
	}
	return g_string_free(out, FALSE);
}

static gchar *
route_spec_path(const gchar *route){
	return g_strdup_printf("%s%.*s.json", SPEC_DIR, (gint)(strlen(route) - 5), route);
}

static JsonArray *
plan_routes(JsonObject *plan){
	JsonArray *pages = member_array(plan, "pages");
	JsonArray *routes = json_array_new();
	guint i;
	for (i = 0; pages != NULL && i < json_array_get_length(pages); i++) {
		JsonNode *node = json_array_get_element(pages, i);
		JsonObject *page = JSON_NODE_HOLDS_OBJECT(node) ? json_node_get_object(node) : NULL;
		if (ls_string(page, "path") != NULL)
			json_array_add_string_element(routes, ls_string(page, "path"));
	}
	return routes;
}

static gboolean
in_routes(JsonArray *routes, const gchar *route){
	guint i;
	for (i = 0; route != NULL && i < json_array_get_length(routes); i++)
		if (g_str_equal(json_array_get_string_element(routes, i), route))
			return TRUE;
	return FALSE;
}

/* The site's address, as the last build wrote it into the home page. */
static gchar *
site_base_url(GHashTable *files){
	JsonObject *index = g_hash_table_lookup(files, "index.html");
	const gchar *html = ls_string(index, "content");
	const gchar *needle = "<link rel=\"canonical\" href=\"";
	const gchar *start = html != NULL ? strstr(html, needle) : NULL;
	const gchar *end;
	if (start == NULL)
		return NULL;
	start += strlen(needle);
	end = strchr(start, '"');
	return end != NULL && g_str_has_prefix(start, "https://") ? g_strndup(start, (gsize)(end - start)) : NULL;
}

/* Plan members the builder owns: dealt layout, supplied photos and profiles,
 * the separately written signature layer and the order's missing facts. */
static const gchar *const builder_members[] = {"structure", "photos", "social", "signature", "missing", NULL};

typedef struct {
	gchar *summary;
	JsonNode *plan;		/* the merged plan, without its signature */
	JsonObject *pages;	/* route -> specification */
	gchar *signature;	/* NULL: none */
} Edit;

static void
edit_clear(Edit *edit){
	g_clear_pointer(&edit->summary, g_free);
	g_clear_pointer(&edit->plan, json_node_unref);
	g_clear_pointer(&edit->pages, json_object_unref);
	g_clear_pointer(&edit->signature, g_free);
}

static gboolean
reject(GError **error, const gchar *format, ...) G_GNUC_PRINTF(2, 3);
static gboolean
reject(GError **error, const gchar *format, ...){
	va_list args;
	g_autofree gchar *message = NULL;
	va_start(args, format);
	message = g_strdup_vprintf(format, args);
	va_end(args);
	g_set_error_literal(error, LS_ERROR, LS_ERROR_VALIDATION, message);
	return FALSE;
}

/* Merge one answer into the current plan and pages, then check the result
 * as a whole: every page against the new plan, not only the changed ones. */
static gboolean
merge_answer(LsComponents *components, JsonNode *old_plan, JsonObject *old_pages, const gchar *old_signature,
	const gchar *text, Edit *edit, GError **error){
	g_autoptr(JsonNode) answer = ls_json_parse(text, error);
	JsonObject *root, *changes, *plan;
	JsonArray *pages, *remove, *art, *old_art;
	g_autoptr(JsonArray) planned = NULL, routes = NULL;
	g_autoptr(GList) specs = NULL;
	g_autoptr(GHashTable) old_art_paths = g_hash_table_new(g_str_hash, g_str_equal);
	g_autoptr(GString) problems = g_string_new(NULL);
	g_autofree gchar *repairs = NULL;
	g_autoptr(GList) members = NULL;
	GList *member;
	const gchar *summary;
	guint i;
	if (answer == NULL)
		return FALSE;
	root = JSON_NODE_HOLDS_OBJECT(answer) ? json_node_get_object(answer) : NULL;
	if (root == NULL || !ls_object_keys(root, "|summary|plan|pages|remove|signature|", NULL))
		return reject(error, "The answer must be an object with summary, pages and optionally plan, remove, signature");
	summary = ls_string(root, "summary");
	if (summary == NULL || *summary == '\0' || g_utf8_strlen(summary, -1) > 120 || strchr(summary, '\n') != NULL)
		return reject(error, "summary must be one line of at most 120 characters");
	edit->summary = g_strstrip(g_strdup(summary));
	/* A version note, not a sentence. */
	while (g_str_has_suffix(edit->summary, ".") && strlen(edit->summary) > 1)
		edit->summary[strlen(edit->summary) - 1] = '\0';
	edit->plan = deep_copy(old_plan);
	plan = json_node_get_object(edit->plan);
	changes = member_object(root, "plan");
	if (json_object_has_member(root, "plan") && changes == NULL)
		return reject(error, "plan must be an object of the plan members you change");
	members = changes != NULL ? json_object_get_members(changes) : NULL;
	for (member = members; member != NULL; member = member->next) {
		guint b;
		for (b = 0; builder_members[b] != NULL; b++)
			if (g_str_equal(member->data, builder_members[b]))
				return reject(error, "plan.%s is the builder's and cannot be edited; change only the "
					"members the request is about", (const gchar *)member->data);
		json_object_set_member(plan, member->data, deep_copy(json_object_get_member(changes, member->data)));
	}
	/* New artwork would need drawing; an edit reuses what the site has. */
	old_art = member_array(json_node_get_object(old_plan), "art");
	for (i = 0; old_art != NULL && i < json_array_get_length(old_art); i++) {
		JsonNode *node = json_array_get_element(old_art, i);
		if (JSON_NODE_HOLDS_OBJECT(node) && ls_string(json_node_get_object(node), "path") != NULL)
			g_hash_table_add(old_art_paths, (gpointer)ls_string(json_node_get_object(node), "path"));
	}
	art = member_array(plan, "art");
	for (i = 0; art != NULL && i < json_array_get_length(art); i++) {
		JsonNode *node = json_array_get_element(art, i);
		const gchar *path = JSON_NODE_HOLDS_OBJECT(node) ? ls_string(json_node_get_object(node), "path") : NULL;
		if (path == NULL || !g_hash_table_contains(old_art_paths, path))
			return reject(error, "art %s is new; an edit cannot draw new artwork, so reuse planned art or photos",
				path != NULL ? path : "(no path)");
	}
	/* Removed routes leave the plan; their pages and specifications go. */
	remove = member_array(root, "remove");
	if (json_object_has_member(root, "remove") && remove == NULL)
		return reject(error, "remove must be an array of planned routes");
	planned = plan_routes(json_node_get_object(old_plan));
	for (i = 0; remove != NULL && i < json_array_get_length(remove); i++) {
		JsonNode *node = json_array_get_element(remove, i);
		const gchar *route = JSON_NODE_HOLDS_VALUE(node) ? json_node_get_string(node) : NULL;
		JsonArray *list = member_array(plan, "pages");
		guint p;
		if (route == NULL || !in_routes(planned, route) || g_str_equal(route, "index.html"))
			return reject(error, "remove[%u] must be a planned route other than index.html", i);
		for (p = 0; list != NULL && p < json_array_get_length(list); p++) {
			JsonObject *entry = json_array_get_object_element(list, p);
			if (g_strcmp0(ls_string(entry, "path"), route) == 0) {
				json_array_remove_element(list, p);
				break;
			}
		}
	}
	/* Unchanged specifications carry over; changed and new ones replace them. */
	routes = plan_routes(plan);
	edit->pages = json_object_new();
	for (i = 0; i < json_array_get_length(routes); i++) {
		const gchar *route = json_array_get_string_element(routes, i);
		if (json_object_has_member(old_pages, route))
			json_object_set_member(edit->pages, route, deep_copy(json_object_get_member(old_pages, route)));
	}
	pages = member_array(root, "pages");
	if (pages == NULL)
		return reject(error, "pages must be an array of complete page specifications");
	{
		g_autoptr(GHashTable) seen = g_hash_table_new(g_str_hash, g_str_equal);
		for (i = 0; i < json_array_get_length(pages); i++) {
			JsonNode *node = json_array_get_element(pages, i);
			const gchar *path = JSON_NODE_HOLDS_OBJECT(node) ? ls_string(json_node_get_object(node), "path") : NULL;
			if (path == NULL || !in_routes(routes, path))
				return reject(error, "pages[%u] needs the path of a planned route", i);
			if (!g_hash_table_add(seen, (gpointer)path))
				return reject(error, "pages names %s twice", path);
			json_object_set_member(edit->pages, path, deep_copy(node));
		}
	}
	for (i = 0; i < json_array_get_length(routes); i++)
		if (!json_object_has_member(edit->pages, json_array_get_string_element(routes, i)))
			return reject(error, "the new page %s needs its complete specification in pages",
				json_array_get_string_element(routes, i));
	if (json_object_has_member(root, "signature")) {
		const gchar *css = ls_string(root, "signature");
		if (css == NULL || !ls_components_check_signature(css, error))
			return FALSE;
		edit->signature = *css != '\0' ? g_strdup(css) : NULL;
	} else
		edit->signature = g_strdup(old_signature);
	repairs = ls_components_repair_plan(components, edit->plan);
	{
		g_autoptr(GError) local = NULL;
		if (!ls_components_check_plan(components, edit->plan, &local))
			g_string_append_printf(problems, "%s\n", local->message);
	}
	specs = json_object_get_members(edit->pages);
	for (member = specs; member != NULL; member = member->next) {
		g_autoptr(GError) local = NULL;
		if (problems->len > 0)
			break;
		if (!ls_components_check_page(components, edit->plan, member->data,
			json_object_get_member(edit->pages, member->data), &local))
			g_string_append_printf(problems, "%s\n", local->message);
	}
	if (problems->len > 0)
		return reject(error, "%s", g_strchomp(problems->str));
	return TRUE;
}

/* Write one recorded entry to disk with the export's private modes. */
static gboolean
write_entry(const gchar *project, JsonObject *entry, GError **error){
	g_autofree gchar *path = g_build_filename(project, ls_string(entry, "path"), NULL);
	g_autofree gchar *parent = g_path_get_dirname(path);
	const gchar *content = ls_string(entry, "content");
	gboolean binary = g_strcmp0(ls_string(entry, "encoding"), "base64") == 0;
	g_autofree guchar *bytes = NULL;
	gsize length = 0;
	if (content == NULL)
		return reject(error, "%s has no content", ls_string(entry, "path"));
	if (g_mkdir_with_parents(parent, 0700) != 0) {
		g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno), "Cannot create %s: %s", parent,
			g_strerror(errno));
		return FALSE;
	}
	if (binary)
		bytes = g_base64_decode(content, &length);
	return g_file_set_contents_full(path, binary ? (const gchar *)bytes : content,
		binary ? (gssize)length : -1, G_FILE_SET_CONTENTS_CONSISTENT, 0600, error);
}

static gboolean
unlink_entry(const gchar *project, const gchar *relative, GError **error){
	g_autofree gchar *path = g_build_filename(project, relative, NULL);
	if (g_unlink(path) != 0 && errno != ENOENT) {
		g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno), "Cannot remove %s: %s", relative,
			g_strerror(errno));
		return FALSE;
	}
	return TRUE;
}

static gint
by_path(gconstpointer a, gconstpointer b){
	return g_strcmp0(ls_string(*(JsonObject *const *)a, "path"), ls_string(*(JsonObject *const *)b, "path"));
}

static JsonArray *
sorted_entries(GHashTable *map){
	g_autoptr(GPtrArray) list = g_ptr_array_new();
	JsonArray *out = json_array_new();
	GHashTableIter iter;
	gpointer key, value;
	guint i;
	g_hash_table_iter_init(&iter, map);
	while (g_hash_table_iter_next(&iter, &key, &value))
		g_ptr_array_add(list, value);
	g_ptr_array_sort(list, by_path);
	for (i = 0; i < list->len; i++)
		json_array_add_object_element(out, json_object_ref(g_ptr_array_index(list, i)));
	return out;
}

static JsonObject *
text_entry(const gchar *path, const gchar *content){
	JsonObject *entry = json_object_new();
	json_object_set_string_member(entry, "path", path);
	json_object_set_string_member(entry, "content", content);
	return entry;
}

gboolean
ls_editor_apply(LsEditor *self, const gchar *project, const gchar *request, gchar **summary,
	gchar **commit, GCancellable *cancellable, GError **error){
	g_autofree gchar *absolute = NULL, *record_path = NULL, *record_text = NULL, *lock_path = NULL;
	g_autofree gchar *outlines_path = NULL, *outlines_text = NULL, *new_outlines_text = NULL;
	g_autofree gchar *ignore_path = NULL, *ignore_text = NULL, *new_ignore_text = NULL;
	g_autoptr(JsonNode) outlines_node = NULL, disk_node = NULL;
	gboolean have_record, have_outlines;
	g_autofree gchar *base_url = NULL, *prompt = NULL, *feedback = NULL, *reference = NULL;
	g_autofree gchar *composed_text = NULL, *new_record_text = NULL, *message = NULL, *head = NULL;
	g_autoptr(JsonNode) record_node = NULL, old_plan = NULL, composed = NULL, record_copy = NULL;
	g_autoptr(JsonObject) old_pages = json_object_new();
	g_autoptr(JsonObject) settings = NULL;
	g_autoptr(LsComponents) components = NULL;
	g_autoptr(GHashTable) files = NULL, archive = NULL, new_files = NULL, new_archive = NULL;
	g_autoptr(GPtrArray) written = g_ptr_array_new(), deleted = g_ptr_array_new();
	g_autoptr(GPtrArray) owned = g_ptr_array_new_with_free_func((GDestroyNotify)json_object_unref);
	JsonObject *record, *plan_entry, *signature_entry;
	JsonArray *routes;
	const gchar *system;
	Edit edit = {NULL, NULL, NULL, NULL};
	gint64 retries, attempt;
	gint lock_fd = -1;
	gboolean okay = FALSE, touched = FALSE;
	guint i;
	g_return_val_if_fail(LS_IS_EDITOR(self), FALSE);
	g_return_val_if_fail(project != NULL && request != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);
	if (*request == '\0' || strspn(request, " \t\r\n") == strlen(request) || strlen(request) > 4000) {
		g_set_error_literal(error, LS_ERROR, LS_ERROR_INVALID, "The change request must be 1 to 4000 characters");
		return FALSE;
	}
	absolute = g_canonicalize_filename(project, NULL);
	record_path = g_build_filename(absolute, RECORD, NULL);
	outlines_path = g_build_filename(absolute, OUTLINES, NULL);
	ignore_path = g_build_filename(absolute, ".gitignore", NULL);
	have_record = g_file_test(record_path, G_FILE_TEST_IS_REGULAR);
	have_outlines = g_file_test(outlines_path, G_FILE_TEST_IS_REGULAR);
	if (!have_record && !have_outlines) {
		g_set_error(error, LS_ERROR, LS_ERROR_INVALID,
			"%s is not a built site: it has no %s or %s", absolute, OUTLINES, RECORD);
		return FALSE;
	}
	/* One edit at a time per site. */
	lock_path = g_build_filename(absolute, ".libresite", "edit.lock", NULL);
	lock_fd = g_open(lock_path, O_RDWR | O_CREAT | O_CLOEXEC, 0600);
	if (lock_fd < 0 || flock(lock_fd, LOCK_EX | LOCK_NB) != 0) {
		g_set_error(error, LS_ERROR, LS_ERROR_RECOVERY, "Another edit of %s is running", absolute);
		goto out;
	}
	if (!check_clean(absolute, cancellable, error))
		goto out;
	/* The published files as they are on disk now. */
	{
		g_autoptr(LsContext) disk = ls_context_import(absolute, cancellable, error);
		g_autofree gchar *snapshot = NULL;
		if (disk == NULL)
			goto out;
		snapshot = ls_context_snapshot(disk);
		disk_node = ls_json_parse(snapshot, error);
		if (disk_node == NULL)
			goto out;
		files = entry_map(member_array(json_node_get_object(disk_node), "files"));
	}
	if (have_record) {
		record_text = ls_read_text(record_path, error);
		if (record_text == NULL || (record_node = ls_json_parse(record_text, error)) == NULL ||
			!JSON_NODE_HOLDS_OBJECT(record_node))
			goto out;
	}
	record = record_node != NULL ? json_node_get_object(record_node) : NULL;
	/* The committed outlines follow the site through its history; a site
	 * built before they were committed falls back to the local record. */
	if (have_outlines) {
		JsonObject *root;
		outlines_text = ls_read_text(outlines_path, error);
		if (outlines_text == NULL || (outlines_node = ls_json_parse(outlines_text, error)) == NULL)
			goto out;
		root = JSON_NODE_HOLDS_OBJECT(outlines_node) ? json_node_get_object(outlines_node) : NULL;
		archive = entry_map(member_array(root, "outlines"));
		if (!check_hashes(files, member_object(root, "files"), error))
			goto out;
	} else {
		g_autoptr(GHashTable) recorded = entry_map(member_array(record, "files"));
		GHashTableIter iter;
		gpointer key, value;
		archive = entry_map(member_array(record, "archive"));
		g_hash_table_iter_init(&iter, files);
		while (g_hash_table_iter_next(&iter, &key, &value))
			if (!g_str_equal(key, ".gitignore") && !same_entry(value, g_hash_table_lookup(recorded, key))) {
				g_set_error(error, LS_ERROR, LS_ERROR_CONFLICT,
					"%s differs from the build record: it was changed outside libresite, so the saved "
					"outlines no longer describe this site", (const gchar *)key);
				goto out;
			}
		g_hash_table_iter_init(&iter, recorded);
		while (g_hash_table_iter_next(&iter, &key, &value))
			if (!g_hash_table_contains(files, key)) {
				g_set_error(error, LS_ERROR, LS_ERROR_CONFLICT,
					"%s is in the build record but missing from the site", (const gchar *)key);
				goto out;
			}
	}
	plan_entry = g_hash_table_lookup(archive, PLAN_PATH);
	if (plan_entry == NULL || ls_string(plan_entry, "content") == NULL) {
		g_set_error(error, LS_ERROR, LS_ERROR_INVALID,
			"%s has no saved plan: only sites built from components can be edited this way", absolute);
		goto out;
	}
	old_plan = ls_json_parse(ls_string(plan_entry, "content"), error);
	if (old_plan == NULL || !JSON_NODE_HOLDS_OBJECT(old_plan))
		goto out;
	routes = plan_routes(json_node_get_object(old_plan));
	for (i = 0; i < json_array_get_length(routes); i++) {
		const gchar *route = json_array_get_string_element(routes, i);
		g_autofree gchar *spec_path = route_spec_path(route);
		JsonObject *spec = g_hash_table_lookup(archive, spec_path);
		JsonNode *node = spec != NULL ? ls_json_parse(ls_string(spec, "content"), NULL) : NULL;
		if (node == NULL) {
			g_set_error(error, LS_ERROR, LS_ERROR_INVALID, "%s has no saved specification for %s", absolute, route);
			json_array_unref(routes);
			goto out;
		}
		json_object_set_member(old_pages, route, node);
	}
	json_array_unref(routes);
	signature_entry = g_hash_table_lookup(archive, SIGNATURE_PATH);
	components = ls_components_new(error);
	if (components == NULL)
		goto out;
	base_url = site_base_url(files);
	/* The model sees the request, the plan the builder did not deal and every
	 * page, so it can change exactly what was asked and keep the rest. */
	{
		g_autoptr(JsonObject) task = json_object_new();
		g_autoptr(JsonNode) node = json_node_new(JSON_NODE_OBJECT);
		g_autoptr(JsonNode) shown = deep_copy(old_plan);
		g_autofree gchar *task_text = NULL;
		JsonArray *list = json_array_new();
		g_autoptr(GList) names = json_object_get_members(old_pages);
		GList *name;
		json_object_remove_member(json_node_get_object(shown), "structure");
		json_object_set_string_member(task, "request", request);
		json_object_set_member(task, "plan", g_steal_pointer(&shown));
		for (name = names; name != NULL; name = name->next)
			json_array_add_element(list, json_node_copy(json_object_get_member(old_pages, name->data)));
		json_object_set_array_member(task, "pages", list);
		if (signature_entry != NULL && ls_string(signature_entry, "content") != NULL)
			json_object_set_string_member(task, "signature", ls_string(signature_entry, "content"));
		json_node_set_object(node, task);
		task_text = ls_json_encode(node);
		reference = ls_components_describe(components);
		prompt = ls_prompts_render_stage(self->prompts, "edit", reference, task_text, error);
		if (prompt == NULL)
			goto out;
	}
	system = ls_prompts_get_spec_system(self->prompts);
	settings = json_object_new();
	{
		g_autoptr(GList) names = json_object_get_members(self->settings);
		GList *name;
		for (name = names; name != NULL; name = name->next)
			json_object_set_member(settings, name->data, json_node_copy(json_object_get_member(self->settings, name->data)));
	}
	json_object_set_string_member(settings, "json_schema",
		"{\"type\": \"object\", \"required\": [\"summary\", \"pages\"], \"additionalProperties\": false, "
		"\"properties\": {\"summary\": {\"type\": \"string\"}, \"plan\": {\"type\": \"object\"}, "
		"\"pages\": {\"type\": \"array\", \"items\": {\"type\": \"object\"}}, "
		"\"remove\": {\"type\": \"array\", \"items\": {\"type\": \"string\"}}, \"signature\": {\"type\": \"string\"}}}\n");
	retries = json_object_has_member(settings, "retries") ? json_object_get_int_member(settings, "retries") : 1;
	for (attempt = 0; attempt <= MAX(retries, 0); attempt++) {
		g_autoptr(GError) local = NULL;
		g_autofree gchar *response = NULL, *retry = NULL;
		say(self, "asking for the change, attempt %" G_GINT64_FORMAT "/%" G_GINT64_FORMAT, attempt + 1,
			MAX(retries, 0) + 1);
		if (feedback != NULL)
			retry = g_strdup_printf("%s\n\nYOUR PREVIOUS ANSWER WAS REJECTED\n%.2000s\nAnswer again with the fix.\n",
				prompt, feedback);
		response = ls_provider_generate(self->provider, settings, system, retry != NULL ? retry : prompt,
			cancellable, &local);
		if (response != NULL) {
			edit_clear(&edit);
			if (merge_answer(components, old_plan, old_pages,
				signature_entry != NULL ? ls_string(signature_entry, "content") : NULL, response, &edit, &local))
				break;
		}
		if (g_cancellable_is_cancelled(cancellable) || attempt == MAX(retries, 0) || response == NULL) {
			g_propagate_error(error, g_steal_pointer(&local));
			goto out;
		}
		say(self, "answer rejected: %.300s", local->message);
		g_free(feedback);
		feedback = g_strdup(local->message);
	}
	/* Recompose the whole site from the merged plan and pages. */
	{
		g_autoptr(JsonNode) with_signature = deep_copy(edit.plan);
		if (edit.signature != NULL)
			json_object_set_string_member(json_node_get_object(with_signature), "signature", edit.signature);
		composed_text = ls_components_compose(components, with_signature, edit.pages, base_url, error);
		if (composed_text == NULL || (composed = ls_json_parse(composed_text, error)) == NULL)
			goto out;
		/* The new record: published files and the archived outlines. */
		new_files = g_hash_table_new(g_str_hash, g_str_equal);
		new_archive = g_hash_table_new(g_str_hash, g_str_equal);
		{
			GHashTableIter iter;
			gpointer key, value;
			g_autoptr(JsonArray) old_routes = plan_routes(json_node_get_object(old_plan));
			g_autoptr(JsonArray) new_routes = plan_routes(json_node_get_object(edit.plan));
			g_hash_table_iter_init(&iter, files);
			while (g_hash_table_iter_next(&iter, &key, &value))
				if (!in_routes(old_routes, key) || in_routes(new_routes, key))
					g_hash_table_insert(new_files, key, value);
			g_hash_table_iter_init(&iter, archive);
			while (g_hash_table_iter_next(&iter, &key, &value))
				if (!g_str_has_prefix(key, SPEC_DIR) && !g_str_equal(key, PLAN_PATH) &&
					!g_str_equal(key, SIGNATURE_PATH) && !g_str_equal(key, FINGERPRINT_PATH))
					g_hash_table_insert(new_archive, key, value);
		}
		{
			JsonArray *out = member_array(json_node_get_object(composed), "files");
			for (i = 0; out != NULL && i < json_array_get_length(out); i++) {
				JsonObject *entry = json_array_get_object_element(out, i);
				g_hash_table_insert(new_files, (gpointer)ls_string(entry, "path"), entry);
			}
		}
		{
			g_autofree gchar *plan_text = ls_json_encode(edit.plan);
			g_autoptr(GList) names = json_object_get_members(edit.pages);
			GList *name;
			g_autoptr(JsonNode) fingerprint = json_node_new(JSON_NODE_OBJECT);
			g_autofree gchar *fingerprint_text = NULL;
			JsonObject *entry = text_entry(PLAN_PATH, plan_text);
			g_ptr_array_add(owned, entry);
			g_hash_table_insert(new_archive, (gpointer)PLAN_PATH, entry);
			for (name = names; name != NULL; name = name->next) {
				g_autofree gchar *spec_path = route_spec_path(name->data);
				g_autofree gchar *spec_text = ls_json_encode(json_object_get_member(edit.pages, name->data));
				entry = text_entry(spec_path, spec_text);
				g_ptr_array_add(owned, entry);
				g_hash_table_insert(new_archive, (gpointer)ls_string(entry, "path"), entry);
			}
			if (edit.signature != NULL) {
				entry = text_entry(SIGNATURE_PATH, edit.signature);
				g_ptr_array_add(owned, entry);
				g_hash_table_insert(new_archive, (gpointer)SIGNATURE_PATH, entry);
			}
			json_node_take_object(fingerprint, ls_components_fingerprint(components, with_signature,
				json_object_get_member(edit.pages, "index.html")));
			fingerprint_text = ls_json_encode(fingerprint);
			entry = text_entry(FINGERPRINT_PATH, fingerprint_text);
			g_ptr_array_add(owned, entry);
			g_hash_table_insert(new_archive, (gpointer)FINGERPRINT_PATH, entry);
		}
	}
	/* The new version must load as a context: path, size and conflict checks. */
	{
		g_autoptr(LsContext) check = ls_context_new();
		g_autoptr(JsonNode) bundle = json_node_new(JSON_NODE_OBJECT);
		g_autofree gchar *bundle_text = NULL;
		JsonObject *object = json_object_new();
		json_object_set_array_member(object, "files", sorted_entries(new_files));
		json_object_set_array_member(object, "archive", sorted_entries(new_archive));
		json_node_take_object(bundle, object);
		bundle_text = ls_json_encode(bundle);
		if (!ls_context_restore(check, bundle_text, error))
			goto out;
	}
	if (record_node != NULL) {
		record_copy = deep_copy(record_node);
		json_object_set_array_member(json_node_get_object(record_copy), "files", sorted_entries(new_files));
		json_object_set_array_member(json_node_get_object(record_copy), "archive", sorted_entries(new_archive));
		new_record_text = ls_json_encode(record_copy);
	}
	new_outlines_text = outlines_from(new_archive, new_files);
	ignore_text = g_file_test(ignore_path, G_FILE_TEST_IS_REGULAR) ? ls_read_text(ignore_path, NULL) : NULL;
	new_ignore_text = tracked_gitignore(ignore_text);
	{
		GHashTableIter iter;
		gpointer key, value;
		g_hash_table_iter_init(&iter, new_files);
		while (g_hash_table_iter_next(&iter, &key, &value))
			if (!same_entry(value, g_hash_table_lookup(files, key)))
				touched = TRUE;
		g_hash_table_iter_init(&iter, files);
		while (g_hash_table_iter_next(&iter, &key, &value))
			if (!g_hash_table_contains(new_files, key))
				touched = TRUE;
	}
	if (!touched) {
		g_set_error_literal(error, LS_ERROR, LS_ERROR_VALIDATION,
			"The edit made no visible change to the site; nothing was committed");
		goto out;
	}
	if (g_cancellable_set_error_if_cancelled(cancellable, error))
		goto out;
	/* An older site first commits the outlines of the version it has, as a
	 * version of its own, so reverting this edit later restores them. */
	if (!have_outlines) {
		const gchar *add_outlines[] = {"add", "--", ".gitignore", OUTLINES, NULL};
		const gchar *track[] = {"commit", "--no-gpg-sign", "--quiet", "-m", "chore: track site outlines for editing",
			NULL};
		g_autofree gchar *current = outlines_from(archive, files);
		if (!g_file_set_contents_full(outlines_path, current, -1,
			G_FILE_SET_CONTENTS_CONSISTENT | G_FILE_SET_CONTENTS_DURABLE, 0600, error) ||
			(new_ignore_text != NULL && !g_file_set_contents_full(ignore_path, new_ignore_text, -1,
				G_FILE_SET_CONTENTS_CONSISTENT, 0600, error)) ||
			!ls_git_run(absolute, add_outlines, NULL, NULL, error) || !ls_git_run(absolute, track, NULL, NULL, error)) {
			const gchar *unstage[] = {"reset", "--quiet", NULL};
			(void)ls_git_run(absolute, unstage, NULL, NULL, NULL);
			(void)g_unlink(outlines_path);
			if (new_ignore_text != NULL && ignore_text != NULL)
				(void)g_file_set_contents_full(ignore_path, ignore_text, -1, G_FILE_SET_CONTENTS_CONSISTENT, 0600, NULL);
			goto out;
		}
		say(self, "now tracking this site's outlines in its history");
		outlines_text = g_steal_pointer(&current);
		have_outlines = TRUE;
		g_clear_pointer(&ignore_text, g_free);
		ignore_text = g_steal_pointer(&new_ignore_text);
	}
	/* Write, record and commit; undo every step on any failure. */
	{
		GHashTableIter iter;
		gpointer key, value;
		const gchar *add[] = {"add", "--all", "--", ".", NULL};
		const gchar *head_args[] = {"rev-parse", "HEAD", NULL};
		const gchar *unstage[] = {"reset", "--quiet", NULL};
		const gchar *commit_args[] = {"commit", "--no-gpg-sign", "--quiet", "-m", NULL, NULL};
		g_hash_table_iter_init(&iter, new_files);
		while (g_hash_table_iter_next(&iter, &key, &value)) {
			if (same_entry(value, g_hash_table_lookup(files, key)))
				continue;
			if (!write_entry(absolute, value, error))
				goto undo;
			g_ptr_array_add(written, key);
		}
		g_hash_table_iter_init(&iter, files);
		while (g_hash_table_iter_next(&iter, &key, &value)) {
			if (g_hash_table_contains(new_files, key))
				continue;
			if (!unlink_entry(absolute, key, error))
				goto undo;
			g_ptr_array_add(deleted, key);
		}
		if (new_record_text != NULL && !g_file_set_contents_full(record_path, new_record_text, -1,
			G_FILE_SET_CONTENTS_CONSISTENT | G_FILE_SET_CONTENTS_DURABLE, 0600, error))
			goto undo;
		if (!g_file_set_contents_full(outlines_path, new_outlines_text, -1,
			G_FILE_SET_CONTENTS_CONSISTENT | G_FILE_SET_CONTENTS_DURABLE, 0600, error))
			goto undo;
		if (new_ignore_text != NULL && !g_file_set_contents_full(ignore_path, new_ignore_text, -1,
			G_FILE_SET_CONTENTS_CONSISTENT, 0600, error))
			goto undo;
		message = g_strdup_printf("feat: %s", edit.summary);
		commit_args[4] = message;
		if (!ls_git_run(absolute, add, NULL, NULL, error) || !ls_git_run(absolute, commit_args, NULL, NULL, error)) {
			(void)ls_git_run(absolute, unstage, NULL, NULL, NULL);
			goto undo;
		}
		if (!ls_git_run(absolute, head_args, &head, NULL, error))
			goto out;
		g_strstrip(head);
		okay = TRUE;
		say(self, "committed %.12s: %s", head, edit.summary);
		goto out;
undo:
		for (i = 0; i < written->len; i++) {
			JsonObject *previous = g_hash_table_lookup(files, g_ptr_array_index(written, i));
			if (previous != NULL)
				(void)write_entry(absolute, previous, NULL);
			else
				(void)unlink_entry(absolute, g_ptr_array_index(written, i), NULL);
		}
		for (i = 0; i < deleted->len; i++) {
			JsonObject *previous = g_hash_table_lookup(files, g_ptr_array_index(deleted, i));
			if (previous != NULL)
				(void)write_entry(absolute, previous, NULL);
		}
		if (record_text != NULL)
			(void)g_file_set_contents_full(record_path, record_text, -1, G_FILE_SET_CONTENTS_CONSISTENT, 0600, NULL);
		if (outlines_text != NULL)
			(void)g_file_set_contents_full(outlines_path, outlines_text, -1, G_FILE_SET_CONTENTS_CONSISTENT, 0600, NULL);
		else
			(void)g_unlink(outlines_path);
		if (new_ignore_text != NULL && ignore_text != NULL)
			(void)g_file_set_contents_full(ignore_path, ignore_text, -1, G_FILE_SET_CONTENTS_CONSISTENT, 0600, NULL);
	}
out:
	if (okay) {
		if (summary != NULL)
			*summary = g_strdup(edit.summary);
		if (commit != NULL)
			*commit = g_steal_pointer(&head);
	}
	edit_clear(&edit);
	if (lock_fd >= 0) {
		(void)flock(lock_fd, LOCK_UN);
		(void)close(lock_fd);
	}
	return okay;
}
