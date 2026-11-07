/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "core/ls-taste.h"
#include <string.h>

/* Whether @phrase sits at @at in @lower as whole words: no letter or digit
 * just before it, nor after it unless @prefix (a stem such as "stock photo"
 * that also covers photograph and photography). */
static gboolean
whole(const gchar *lower, const gchar *at, const gchar *phrase, gboolean prefix){
	gchar after = at[strlen(phrase)];
	return (at == lower || !g_ascii_isalnum(at[-1])) && (prefix || !g_ascii_isalnum(after));
}

/* Stems end in '*': they match longer words too. */
static const gchar *const stock_talk[] = {"stock photo*", "stock image*", "stock picture*", "visual reference*",
	"mood reference*", NULL};
static const gchar *const example_talk[] = {"invented business", "fictional business", "fictitious business",
	"not a real business", "does not represent a real", "example business", "sample business", "fictional demo",
	"demo site", "demo business", "demo website", NULL};

/* The first entry of @list found in @lower as whole words, or NULL. */
static const gchar *
find_talk(const gchar *lower, const gchar *const *list){
	guint i;
	for (i = 0; list != NULL && list[i] != NULL; i++) {
		const gchar *entry = list[i];
		gsize length = strlen(entry);
		gboolean prefix = entry[length - 1] == '*';
		g_autofree gchar *phrase = g_strndup(entry, prefix ? length - 1 : length);
		const gchar *at;
		for (at = strstr(lower, phrase); at != NULL; at = strstr(at + 1, phrase))
			if (whole(lower, at, phrase, prefix))
				return entry;
	}
	return NULL;
}

const gchar *
ls_taste_disclosure(const gchar *text, gboolean footer){
	static const gchar *const anywhere[] = {"illustrative", NULL};
	g_autofree gchar *lower = NULL;
	const gchar *found;
	if (text == NULL)
		return NULL;
	lower = g_ascii_strdown(text, -1);
	if ((found = find_talk(lower, anywhere)) != NULL || footer)
		return found;
	if ((found = find_talk(lower, stock_talk)) != NULL)
		return found;
	return find_talk(lower, example_talk);
}

const gchar *
ls_taste_example(const gchar *text){
	g_autofree gchar *lower = NULL;
	if (text == NULL)
		return NULL;
	lower = g_ascii_strdown(text, -1);
	return find_talk(lower, example_talk);
}

static gboolean
parse_hex(const gchar *hex, guint rgb[3]){
	guint i;
	if (hex == NULL || hex[0] != '#' || strlen(hex) != 7)
		return FALSE;
	for (i = 1; i < 7; i++)
		if (!g_ascii_isxdigit(hex[i]))
			return FALSE;
	for (i = 0; i < 3; i++)
		rgb[i] = (guint)(g_ascii_xdigit_value(hex[1 + i * 2]) * 16 + g_ascii_xdigit_value(hex[2 + i * 2]));
	return TRUE;
}

gdouble
ls_taste_chroma(const gchar *hex){
	guint rgb[3];
	if (!parse_hex(hex, rgb))
		return 0;
	return (MAX(MAX(rgb[0], rgb[1]), rgb[2]) - MIN(MIN(rgb[0], rgb[1]), rgb[2])) / 255.0;
}

gchar *
ls_taste_calm(const gchar *hex, gdouble limit){
	guint rgb[3], i;
	gdouble chroma = ls_taste_chroma(hex), mean, scale;
	if (!parse_hex(hex, rgb) || chroma <= limit)
		return g_strdup(hex);
	mean = (rgb[0] + rgb[1] + rgb[2]) / 3.0;
	/* Rounding can add a step, so aim a little under the limit. */
	scale = MAX(limit - 0.01, 0) / chroma;
	for (i = 0; i < 3; i++)
		rgb[i] = (guint)CLAMP(mean + ((gdouble)rgb[i] - mean) * scale + 0.5, 0, 255);
	return g_strdup_printf("#%02x%02x%02x", rgb[0], rgb[1], rgb[2]);
}

gchar *
ls_taste_mix(const gchar *hex, const gchar *toward, gdouble amount){
	guint a[3], b[3], i;
	if (!parse_hex(hex, a) || !parse_hex(toward, b))
		return g_strdup(hex);
	amount = CLAMP(amount, 0, 1);
	for (i = 0; i < 3; i++)
		a[i] = (guint)CLAMP(a[i] + ((gdouble)b[i] - a[i]) * amount + 0.5, 0, 255);
	return g_strdup_printf("#%02x%02x%02x", a[0], a[1], a[2]);
}
