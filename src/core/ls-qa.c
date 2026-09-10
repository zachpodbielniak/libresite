/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "core/ls-qa.h"
#include "core/ls-context.h"
#include <string.h>
#include <ctype.h>
/* Native validate stays literal. This runner only reads a published tree. */

static gboolean
ascii_has(const gchar *text, const gchar *needle){
	g_autofree gchar *lower_text = g_ascii_strdown(text, -1);
	g_autofree gchar *lower_needle = g_ascii_strdown(needle, -1);
	return strstr(lower_text, lower_needle) != NULL;
}

static gboolean
is_external(const gchar *href){
	return href == NULL || *href == '\0' || *href == '#' || strstr(href, "://") != NULL ||
		g_str_has_prefix(href, "mailto:") || g_str_has_prefix(href, "tel:") ||
		g_str_has_prefix(href, "data:") || g_str_has_prefix(href, "javascript:");
}

static gchar *
strip_query(const gchar *href){
	g_autofree gchar *copy = g_strdup(href);
	gchar *cut = strpbrk(copy, "?#");
	if (cut != NULL)
		*cut = '\0';
	return g_steal_pointer(&copy);
}

static gchar *
normalize_relative(const gchar *path){
	g_auto(GStrv) parts = g_strsplit(path, "/", -1);
	g_autoptr(GPtrArray) stack = g_ptr_array_new_with_free_func(g_free);
	guint i;
	for (i = 0; parts[i] != NULL; i++) {
		if (parts[i][0] == '\0' || g_str_equal(parts[i], "."))
			continue;
		if (g_str_equal(parts[i], "..")) {
			if (stack->len == 0)
				return NULL;
			g_ptr_array_remove_index(stack, stack->len - 1);
			continue;
		}
		g_ptr_array_add(stack, g_strdup(parts[i]));
	}
	if (stack->len == 0)
		return NULL;
	g_ptr_array_add(stack, NULL);
	return g_strjoinv("/", (gchar **)stack->pdata);
}

static gchar *
resolve_href(const gchar *from, const gchar *href){
	g_autofree gchar *stripped = strip_query(href);
	g_autofree gchar *joined = NULL;
	if (stripped == NULL || *stripped == '\0')
		return NULL;
	if (stripped[0] == '/')
		joined = g_strdup(stripped + 1);
	else {
		g_autofree gchar *dir = g_path_get_dirname(from);
		joined = g_str_equal(dir, ".") ? g_strdup(stripped)
			: g_build_filename(dir, stripped, NULL);
	}
	return normalize_relative(joined);
}

static gchar *
attr_value(const gchar *tag, const gchar *name){
	g_autofree gchar *double_quote = g_strdup_printf("%s=\"", name);
	g_autofree gchar *single_quote = g_strdup_printf("%s='", name);
	const gchar *start = strstr(tag, double_quote);
	gchar quote = '"';
	const gchar *end;
	if (start == NULL) {
		start = strstr(tag, single_quote);
		quote = '\'';
		if (start == NULL)
			return NULL;
		start += strlen(single_quote);
	} else
		start += strlen(double_quote);
	end = strchr(start, quote);
	if (end == NULL || end == start)
		return NULL;
	return g_strndup(start, (gsize)(end - start));
}

static void
add_finding(GPtrArray *findings, const gchar *path, const gchar *detail){
	g_ptr_array_add(findings, g_strdup_printf("%s: %s", path, detail));
}

static void
missing_asset(LsContext *context, GPtrArray *findings, const gchar *from, const gchar *href){
	g_autofree gchar *resolved = NULL;
	g_autofree gchar *detail = NULL;
	if (is_external(href))
		return;
	resolved = resolve_href(from, href);
	if (resolved != NULL && ls_path_valid(resolved) && ls_context_lookup(context, resolved) != NULL)
		return;
	detail = g_strdup_printf("missing local asset %s", resolved != NULL ? resolved : href);
	add_finding(findings, from, detail);
}

static void
scan_tags(LsContext *context, GPtrArray *findings, const gchar *path, const gchar *html,
	const gchar *tag_name, const gchar *attr){
	g_autofree gchar *open = g_strdup_printf("<%s", tag_name);
	g_autofree gchar *lower = g_ascii_strdown(html, -1);
	const gchar *cursor = lower;
	gsize offset;
	while ((cursor = strstr(cursor, open)) != NULL) {
		const gchar *end = strchr(cursor, '>');
		g_autofree gchar *tag = NULL;
		g_autofree gchar *value = NULL;
		if (end == NULL)
			break;
		offset = (gsize)(cursor - lower);
		tag = g_ascii_strdown(html + offset, (gssize)(end - cursor + 1));
		value = attr_value(tag, attr);
		if (value != NULL)
			missing_asset(context, findings, path, value);
		cursor = end + 1;
	}
}

static void
scan_css_urls(LsContext *context, GPtrArray *findings, const gchar *path, const gchar *css){
	const gchar *cursor = css;
	while ((cursor = strstr(cursor, "url(")) != NULL) {
		const gchar *start = cursor + 4;
		const gchar *end;
		g_autofree gchar *raw = NULL;
		g_autofree gchar *trimmed = NULL;
		gsize length;
		while (*start == ' ' || *start == '\t' || *start == '\n' || *start == '\r')
			start++;
		if (*start == '"' || *start == '\'') {
			gchar quote = *start;
			start++;
			end = strchr(start, quote);
		} else
			end = strchr(start, ')');
		if (end == NULL)
			break;
		length = (gsize)(end - start);
		raw = g_strndup(start, length);
		trimmed = g_strstrip(raw);
		raw = NULL;
		missing_asset(context, findings, path, trimmed);
		cursor = end + 1;
	}
}

static void
check_html(LsContext *context, GPtrArray *findings, const gchar *path, const gchar *html){
	const gchar *attrs[][2] = {
		{"a", "href"}, {"link", "href"}, {"img", "src"}, {"script", "src"},
		{"source", "src"}, {"video", "src"}, {"audio", "src"}, {"use", "href"},
		{NULL, NULL}
	};
	guint i;
	if (!ascii_has(html, "<!doctype html"))
		add_finding(findings, path, "missing <!doctype html");
	if (!ascii_has(html, "lang="))
		add_finding(findings, path, "missing lang attribute");
	if (!ascii_has(html, "<title"))
		add_finding(findings, path, "missing <title>");
	if (!ascii_has(html, "viewport"))
		add_finding(findings, path, "missing viewport meta");
	if (!ascii_has(html, "charset"))
		add_finding(findings, path, "missing charset");
	for (i = 0; attrs[i][0] != NULL; i++)
		scan_tags(context, findings, path, html, attrs[i][0], attrs[i][1]);
	if (ascii_has(html, "og:image")) {
		g_autofree gchar *lower = g_ascii_strdown(html, -1);
		const gchar *og = strstr(lower, "og:image");
		if (og != NULL) {
			gsize offset = (gsize)(og - lower);
			gsize begin = offset > 80 ? offset - 80 : 0;
			g_autofree gchar *window = g_ascii_strdown(html + begin, 240);
			g_autofree gchar *content = attr_value(window, "content");
			if (content != NULL)
				missing_asset(context, findings, path, content);
		}
	}
}

gboolean
ls_qa_check(const gchar *directory, gchar **report, GCancellable *cancellable, GError **error){
	g_autoptr(LsContext) context = NULL;
	g_autoptr(JsonParser) parser = json_parser_new();
	g_autofree gchar *snapshot = NULL;
	g_autoptr(GPtrArray) findings = g_ptr_array_new_with_free_func(g_free);
	g_autoptr(GString) text = g_string_new(NULL);
	JsonArray *files;
	guint i, html_count = 0, binary_count = 0;
	if (report != NULL)
		*report = NULL;
	context = ls_context_import(directory, cancellable, error);
	if (context == NULL)
		return FALSE;
	snapshot = ls_context_snapshot(context);
	if (!json_parser_load_from_data(parser, snapshot, -1, error))
		return FALSE;
	files = json_object_get_array_member(json_node_get_object(json_parser_get_root(parser)), "files");
	g_string_append_printf(text, "QA %s\n", directory);
	for (i = 0; i < json_array_get_length(files); i++) {
		const gchar *path = ls_string(json_array_get_object_element(files, i), "path");
		const LsArtifact *artifact = ls_context_lookup(context, path);
		if (g_cancellable_set_error_if_cancelled(cancellable, error))
			return FALSE;
		if (ls_artifact_is_binary(artifact)) {
			binary_count++;
			continue;
		}
		if (g_str_has_suffix(path, ".html") || g_str_has_suffix(path, ".htm")) {
			html_count++;
			check_html(context, findings, path, ls_artifact_get_content(artifact));
		} else if (g_str_has_suffix(path, ".css"))
			scan_css_urls(context, findings, path, ls_artifact_get_content(artifact));
	}
	g_string_append_printf(text, "files: %u (%u binary, %u html)\n",
		json_array_get_length(files), binary_count, html_count);
	if (html_count == 0)
		g_ptr_array_add(findings, g_strdup("project: no HTML documents"));
	for (i = 0; i < findings->len; i++)
		g_string_append_printf(text, "FAIL %s\n", (const gchar *)g_ptr_array_index(findings, i));
	if (findings->len == 0)
		g_string_append(text, "OK structural checks; this is not an a11y, SEO, compile or browser audit\n");
	else
		g_string_append_printf(text, "%u finding(s); native validate remains literal in-process\n",
			findings->len);
	if (report != NULL)
		*report = g_string_free(g_steal_pointer(&text), FALSE);
	return findings->len == 0;
}
