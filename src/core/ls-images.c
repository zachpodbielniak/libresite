/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* Supplied photographs, sized for phones with libvips: a long-edge cap,
 * smaller widths for srcset, WebP throughout. See docs/images.org. */
#include "core/ls-images.h"
#include "core/ls-util.h"
#include <vips/vips.h>
#include <string.h>

#define MAX_WIDTHS 8
#define MAX_PIXELS 100000000		/* 100 megapixels: refuse decompression bombs */

static const gint64 default_widths[] = {480, 800, 1200};

typedef struct {
	gint64 max;
	gint64 quality;
	gint64 widths[MAX_WIDTHS];
	guint n_widths;
} Settings;

LsImageVariant *
ls_image_variant_copy(const LsImageVariant *variant){
	LsImageVariant *copy = g_new0(LsImageVariant, 1);
	copy->path = g_strdup(variant->path);
	copy->width = variant->width;
	copy->height = variant->height;
	copy->data = variant->data != NULL ? g_bytes_ref(variant->data) : NULL;
	return copy;
}

void
ls_image_variant_free(LsImageVariant *variant){
	if (variant == NULL)
		return;
	g_free(variant->path);
	g_clear_pointer(&variant->data, g_bytes_unref);
	g_free(variant);
}

G_DEFINE_BOXED_TYPE(LsImageVariant, ls_image_variant, ls_image_variant_copy, ls_image_variant_free)

/* libvips starts once per process. Only the PNG, JPEG and WebP loaders are
 * needed, so the loaders libvips marks untrusted stay blocked, and its
 * operation cache is off: a long-running embedder should not keep decoded
 * photographs alive after a build. */
static gpointer
start_vips(gpointer data){
	(void)data;
	if (vips_init("libresite") != 0)
		return GINT_TO_POINTER(FALSE);
	vips_block_untrusted_set(TRUE);
	vips_cache_set_max(0);
	return GINT_TO_POINTER(TRUE);
}

static gboolean
vips_ready(GError **error){
	static GOnce once = G_ONCE_INIT;
	if (GPOINTER_TO_INT(g_once(&once, start_vips, NULL)))
		return TRUE;
	g_set_error_literal(error, LS_ERROR, LS_ERROR_INVALID, "libvips could not start; images cannot be sized");
	return FALSE;
}

static gboolean
int_member(JsonObject *object, const gchar *name, gint64 low, gint64 high, gint64 *value){
	JsonNode *node = json_object_get_member(object, name);
	if (node == NULL)
		return TRUE;
	if (!JSON_NODE_HOLDS_VALUE(node) || json_node_get_value_type(node) != G_TYPE_INT64 ||
		json_node_get_int(node) < low || json_node_get_int(node) > high)
		return FALSE;
	*value = json_node_get_int(node);
	return TRUE;
}

static gboolean
read_settings(JsonObject *options, Settings *settings){
	JsonNode *node;
	guint i;
	settings->max = LS_IMAGES_DEFAULT_MAX;
	settings->quality = LS_IMAGES_DEFAULT_QUALITY;
	settings->n_widths = G_N_ELEMENTS(default_widths);
	memcpy(settings->widths, default_widths, sizeof(default_widths));
	if (options == NULL)
		return TRUE;
	if (!ls_object_keys(options, "|max|widths|quality|", NULL) ||
		!int_member(options, "max", 160, 8192, &settings->max) ||
		!int_member(options, "quality", 1, 100, &settings->quality))
		return FALSE;
	node = json_object_get_member(options, "widths");
	if (node != NULL) {
		JsonArray *widths = JSON_NODE_HOLDS_ARRAY(node) ? json_node_get_array(node) : NULL;
		if (widths == NULL || json_array_get_length(widths) < 1 || json_array_get_length(widths) > MAX_WIDTHS)
			return FALSE;
		settings->n_widths = json_array_get_length(widths);
		for (i = 0; i < settings->n_widths; i++) {
			JsonNode *element = json_array_get_element(widths, i);
			if (!JSON_NODE_HOLDS_VALUE(element) || json_node_get_value_type(element) != G_TYPE_INT64)
				return FALSE;
			settings->widths[i] = json_node_get_int(element);
			if (settings->widths[i] < 80 || (i > 0 && settings->widths[i] <= settings->widths[i - 1]))
				return FALSE;
		}
	}
	/* Every width is smaller than the cap; the cap itself is always made. */
	for (i = 0; i < settings->n_widths; i++)
		if (settings->widths[i] >= settings->max)
			return FALSE;
	return TRUE;
}

gboolean
ls_images_check_options(JsonObject *options, GError **error){
	Settings settings;
	if (read_settings(options, &settings))
		return TRUE;
	g_set_error_literal(error, LS_ERROR, LS_ERROR_INVALID,
		"plan options.images allows max (long-edge cap, 160-8192 pixels), widths (1-8 ascending widths, "
		"80 pixels up to below max) and quality (WebP quality 1-100), and no other keys");
	return FALSE;
}

gchar *
ls_images_variant_path(const gchar *path, gint64 width){
	const gchar *slash = strrchr(path, '/');
	const gchar *dot = strrchr(slash != NULL ? slash : path, '.');
	gsize stem = dot != NULL ? (gsize)(dot - path) : strlen(path);
	return g_strdup_printf("%.*s-%" G_GINT64_FORMAT "w.webp", (gint)stem, path, width);
}

/* Takes the pending libvips error text as a GError naming @path. */
static void
vips_failure(GError **error, const gchar *path, const gchar *what){
	g_autofree gchar *detail = vips_error_buffer_copy();
	g_strstrip(detail);
	g_set_error(error, LS_ERROR, LS_ERROR_INVALID, "photo %s %s%s%s", path, what,
		*detail != '\0' ? ": " : "", detail);
}

/* Whether a PNG or WebP file is complete. A cut file is refused here with a
 * clear message instead of reaching the decoder, which warns as it fails.
 * JPEG is left to libvips: valid camera files may carry long trailers. */
static gboolean
complete(const guint8 *data, gsize size){
	static const guint8 iend[] = {'I', 'E', 'N', 'D', 0xae, 0x42, 0x60, 0x82};
	gsize i, from;
	if (size >= 8 && memcmp(data, "\x89PNG\r\n\x1a\n", 8) == 0) {
		from = size > 64 ? size - 64 : 0;
		for (i = from; i + sizeof(iend) <= size; i++)
			if (memcmp(data + i, iend, sizeof(iend)) == 0)
				return TRUE;
		return FALSE;
	}
	if (size >= 12 && memcmp(data, "RIFF", 4) == 0 && memcmp(data + 8, "WEBP", 4) == 0)
		return (gsize)(data[4] | data[5] << 8 | data[6] << 16 | (guint32)data[7] << 24) + 8 <= size;
	return TRUE;
}

/* One size of @source: @width wide (and @height high, a bounding box) at most,
 * never larger than the source, encoded as WebP. */
static LsImageVariant *
encode(const gchar *path, GBytes *source, gint width, gint height, gboolean icc, const Settings *settings,
	GError **error){
	VipsImage *image = NULL;
	void *buffer = NULL;
	size_t length = 0;
	gsize size = 0;
	const void *data = g_bytes_get_data(source, &size);
	LsImageVariant *variant;
	gint status;
	/* A broken or truncated file fails rather than publishing grey pixels.
	 * An embedded colour profile is converted to sRGB before it is dropped. */
	if (icc)
		status = vips_thumbnail_buffer((void *)data, size, &image, width, "height", height, "size", VIPS_SIZE_DOWN,
			"option_string", "fail_on=error", "export_profile", "srgb", NULL);
	else
		status = vips_thumbnail_buffer((void *)data, size, &image, width, "height", height, "size", VIPS_SIZE_DOWN,
			"option_string", "fail_on=error", NULL);
	if (status != 0 || vips_webpsave_buffer(image, &buffer, &length, "Q", (gint)settings->quality,
		"keep", VIPS_FOREIGN_KEEP_NONE, NULL) != 0) {
		if (image != NULL)
			g_object_unref(image);
		vips_failure(error, path, "could not be sized");
		return NULL;
	}
	variant = g_new0(LsImageVariant, 1);
	variant->width = vips_image_get_width(image);
	variant->height = vips_image_get_height(image);
	variant->path = ls_images_variant_path(path, variant->width);
	variant->data = g_bytes_new_with_free_func(buffer, length, g_free, buffer);
	g_object_unref(image);
	return variant;
}

GPtrArray *
ls_images_variants(const gchar *path, GBytes *source, JsonObject *options, GError **error){
	g_autoptr(GPtrArray) variants = g_ptr_array_new_with_free_func((GDestroyNotify)ls_image_variant_free);
	Settings settings;
	VipsImage *header;
	const gchar *loader = NULL;
	gsize size = 0;
	const void *data;
	gint64 width, height;
	gboolean icc;
	LsImageVariant *largest;
	guint i;
	g_return_val_if_fail(path != NULL && source != NULL, NULL);
	if (!read_settings(options, &settings)) {
		ls_images_check_options(options, error);
		return NULL;
	}
	if (!vips_ready(error))
		return NULL;
	data = g_bytes_get_data(source, &size);
	/* Only PNG, JPEG and WebP bytes ever reach a libvips loader. */
	if (!(size >= 8 && memcmp(data, "\x89PNG\r\n\x1a\n", 8) == 0) && !(size >= 3 && memcmp(data, "\xff\xd8\xff", 3) == 0) &&
		!(size >= 12 && memcmp(data, "RIFF", 4) == 0 && memcmp((const guint8 *)data + 8, "WEBP", 4) == 0)) {
		g_set_error(error, LS_ERROR, LS_ERROR_INVALID, "photo %s is not a PNG, JPEG or WebP image", path);
		return NULL;
	}
	if (!complete(data, size)) {
		g_set_error(error, LS_ERROR, LS_ERROR_INVALID, "photo %s is truncated; supply the whole file", path);
		return NULL;
	}
	/* The header alone: pixels are decoded only while sizing. */
	header = size > 0 ? vips_image_new_from_buffer(data, size, "", NULL) : NULL;
	if (header == NULL) {
		vips_failure(error, path, "is not a readable PNG, JPEG or WebP image");
		return NULL;
	}
	if (vips_image_get_string(header, "vips-loader", &loader) != 0 || (g_strcmp0(loader, "pngload_buffer") != 0 &&
		g_strcmp0(loader, "jpegload_buffer") != 0 && g_strcmp0(loader, "webpload_buffer") != 0)) {
		vips_error_clear();
		g_object_unref(header);
		g_set_error(error, LS_ERROR, LS_ERROR_INVALID, "photo %s is not a PNG, JPEG or WebP image", path);
		return NULL;
	}
	width = vips_image_get_width(header);
	height = vips_image_get_height(header);
	if (vips_image_get_orientation_swap(header)) {
		gint64 swap = width;
		width = height;
		height = swap;
	}
	icc = vips_image_get_typeof(header, VIPS_META_ICC_NAME) != 0;
	g_object_unref(header);
	if (width * height > MAX_PIXELS) {
		g_set_error(error, LS_ERROR, LS_ERROR_INVALID, "photo %s is %" G_GINT64_FORMAT "x%" G_GINT64_FORMAT
			" pixels; supply at most 100 megapixels", path, width, height);
		return NULL;
	}
	/* Small images are published as supplied. */
	if (width <= settings.widths[0] && MAX(width, height) <= settings.max)
		return g_steal_pointer(&variants);
	largest = encode(path, source, (gint)settings.max, (gint)settings.max, icc, &settings, error);
	if (largest == NULL)
		return NULL;
	for (i = 0; i < settings.n_widths && settings.widths[i] < largest->width; i++) {
		LsImageVariant *variant = encode(path, source, (gint)settings.widths[i], VIPS_MAX_COORD, icc, &settings, error);
		if (variant == NULL) {
			ls_image_variant_free(largest);
			return NULL;
		}
		g_ptr_array_add(variants, variant);
	}
	g_ptr_array_add(variants, largest);
	return g_steal_pointer(&variants);
}

static void
add_file(JsonArray *files, const LsImageVariant *variant){
	JsonObject *file = json_object_new();
	gsize size = 0;
	const guint8 *data = g_bytes_get_data(variant->data, &size);
	g_autofree gchar *content = g_base64_encode(data, size);
	json_object_set_string_member(file, "path", variant->path);
	json_object_set_string_member(file, "encoding", "base64");
	json_object_set_string_member(file, "content", content);
	json_array_add_object_element(files, file);
}

gboolean
ls_images_prepare_photos(LsContext *context, JsonArray *photos, JsonObject *options, JsonArray *files,
	GPtrArray *retired, GCancellable *cancellable, GError **error){
	g_autoptr(GHashTable) names = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	guint i, j;
	g_return_val_if_fail(context != NULL && photos != NULL && files != NULL && retired != NULL, FALSE);
	if (!ls_images_check_options(options, error))
		return FALSE;
	/* No variant may land on a supplied photograph. */
	for (i = 0; i < json_array_get_length(photos); i++) {
		const gchar *path = ls_string(json_array_get_object_element(photos, i), "path");
		if (path != NULL)
			g_hash_table_add(names, g_strdup(path));
	}
	for (i = 0; i < json_array_get_length(photos); i++) {
		JsonObject *photo = json_array_get_object_element(photos, i);
		const gchar *path = ls_string(photo, "path");
		const LsArtifact *artifact;
		g_autoptr(GBytes) source = NULL;
		g_autoptr(GPtrArray) variants = NULL;
		LsImageVariant *largest;
		JsonArray *list;
		const guint8 *data;
		gsize size = 0;
		if (g_cancellable_set_error_if_cancelled(cancellable, error))
			return FALSE;
		/* A plan written again finds an original archived by the first. */
		artifact = path != NULL ? ls_context_lookup(context, path) : NULL;
		if (artifact == NULL && path != NULL)
			artifact = ls_context_lookup_archived(context, path);
		data = artifact != NULL && ls_artifact_is_binary(artifact) ? ls_artifact_get_data(artifact, &size) : NULL;
		if (data == NULL) {
			g_set_error(error, LS_ERROR, LS_ERROR_INVALID,
				"site.photos %s must be a seeded binary PNG, JPEG or WebP image", path != NULL ? path : "(missing)");
			return FALSE;
		}
		source = g_bytes_new_static(data, size);
		variants = ls_images_variants(path, source, options, error);
		if (variants == NULL)
			return FALSE;
		if (variants->len == 0)
			continue;
		list = json_array_new();
		for (j = 0; j < variants->len; j++) {
			LsImageVariant *variant = g_ptr_array_index(variants, j);
			JsonObject *entry;
			if (!g_hash_table_add(names, g_strdup(variant->path))) {
				json_array_unref(list);
				g_set_error(error, LS_ERROR, LS_ERROR_INVALID,
					"site.photos %s would publish %s, which another photo already uses; rename one of them",
					path, variant->path);
				return FALSE;
			}
			entry = json_object_new();
			json_object_set_string_member(entry, "path", variant->path);
			json_object_set_int_member(entry, "width", variant->width);
			json_object_set_int_member(entry, "height", variant->height);
			json_array_add_object_element(list, entry);
			add_file(files, variant);
		}
		/* Pages show the capped size; the original no longer ships. */
		largest = g_ptr_array_index(variants, variants->len - 1);
		json_object_set_int_member(photo, "width", largest->width);
		json_object_set_int_member(photo, "height", largest->height);
		json_object_set_array_member(photo, "variants", list);
		if (ls_context_lookup(context, path) != NULL)
			g_ptr_array_add(retired, g_strdup(path));
	}
	return TRUE;
}

void
ls_images_strip_variants(JsonObject *plan){
	JsonNode *node = plan != NULL ? json_object_get_member(plan, "photos") : NULL;
	JsonArray *photos = node != NULL && JSON_NODE_HOLDS_ARRAY(node) ? json_node_get_array(node) : NULL;
	guint i;
	for (i = 0; photos != NULL && i < json_array_get_length(photos); i++) {
		JsonNode *element = json_array_get_element(photos, i);
		if (JSON_NODE_HOLDS_OBJECT(element) && json_object_has_member(json_node_get_object(element), "variants"))
			json_object_remove_member(json_node_get_object(element), "variants");
	}
}
