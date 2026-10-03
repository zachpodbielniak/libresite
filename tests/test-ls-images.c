/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* Supplied raster images are published in phone-sized WebP variants under a
 * long-edge cap: never upscaled, alpha kept, small images untouched and the
 * output identical from run to run. See docs/images.org. */
#include "libresite.h"
#include "test-image-fixtures.h"

static JsonObject *
options(const gchar *text){
	g_autoptr(JsonNode) node = ls_json_parse(text, NULL);
	g_assert_nonnull(node);
	return json_object_ref(json_node_get_object(node));
}

static VipsImage *
decode(GBytes *bytes){
	gsize size = 0;
	const void *data = g_bytes_get_data(bytes, &size);
	VipsImage *image = vips_image_new_from_buffer(data, size, "", NULL);
	g_assert_nonnull(image);
	return image;
}

static gsize
bytes_size(GBytes *bytes){
	return g_bytes_get_size(bytes);
}

/* A large photograph yields 480, 800, 1200 and a capped 1600 WebP, each at
 * its aspect, decoding to its stated size, and far lighter than the PNG. */
static void
test_large_png(void){
	g_autoptr(GError) error = NULL;
	g_autoptr(GBytes) png = image_fixture(2400, 1600, FALSE, ".png");
	g_autoptr(GPtrArray) variants = ls_images_variants("assets/photos/harbor.png", png, NULL, &error);
	static const gint64 widths[] = {480, 800, 1200, 1600};
	static const gint64 heights[] = {320, 533, 800, 1067};
	guint i;
	g_assert_no_error(error);
	g_assert_nonnull(variants);
	g_assert_cmpuint(variants->len, ==, 4);
	for (i = 0; i < variants->len; i++) {
		LsImageVariant *variant = g_ptr_array_index(variants, i);
		g_autofree gchar *path = g_strdup_printf("assets/photos/harbor-%" G_GINT64_FORMAT "w.webp", widths[i]);
		VipsImage *image = decode(variant->data);
		g_assert_cmpstr(variant->path, ==, path);
		g_assert_cmpint(variant->width, ==, widths[i]);
		g_assert_cmpint(variant->height, ==, heights[i]);
		g_assert_cmpint(vips_image_get_width(image), ==, widths[i]);
		g_assert_cmpint(vips_image_get_height(image), ==, heights[i]);
		g_assert_cmpint(MAX(variant->width, variant->height), <=, LS_IMAGES_DEFAULT_MAX);
		g_assert_true(is_webp(variant->data));
		g_assert_false(vips_image_hasalpha(image));
		g_object_unref(image);
	}
	g_test_message("2400x1600 PNG %" G_GSIZE_FORMAT " bytes; 1600w WebP %" G_GSIZE_FORMAT "; 800w WebP %" G_GSIZE_FORMAT,
		bytes_size(png), bytes_size(((LsImageVariant *)g_ptr_array_index(variants, 3))->data),
		bytes_size(((LsImageVariant *)g_ptr_array_index(variants, 1))->data));
	g_assert_cmpuint(bytes_size(((LsImageVariant *)g_ptr_array_index(variants, 3))->data), <, bytes_size(png) / 4);
}

/* The same input gives the same bytes every time. */
static void
test_deterministic(void){
	g_autoptr(GBytes) png = image_fixture(1700, 1100, FALSE, ".png");
	g_autoptr(GPtrArray) first = ls_images_variants("assets/a.png", png, NULL, NULL);
	g_autoptr(GPtrArray) second = ls_images_variants("assets/a.png", png, NULL, NULL);
	guint i;
	g_assert_nonnull(first);
	g_assert_nonnull(second);
	g_assert_cmpuint(first->len, ==, second->len);
	for (i = 0; i < first->len; i++)
		g_assert_true(g_bytes_equal(((LsImageVariant *)g_ptr_array_index(first, i))->data,
			((LsImageVariant *)g_ptr_array_index(second, i))->data));
}

/* A transparent PNG stays transparent where it was and opaque elsewhere. */
static void
test_alpha(void){
	g_autoptr(GError) error = NULL;
	g_autoptr(GBytes) png = image_fixture(1000, 600, TRUE, ".png");
	g_autoptr(GPtrArray) variants = ls_images_variants("assets/logo-wall.png", png, NULL, &error);
	guint i;
	g_assert_no_error(error);
	g_assert_cmpuint(variants->len, ==, 3);
	for (i = 0; i < variants->len; i++) {
		LsImageVariant *variant = g_ptr_array_index(variants, i);
		VipsImage *image = decode(variant->data);
		double *values = NULL;
		gint n = 0;
		g_assert_true(is_webp(variant->data));
		g_assert_true(vips_image_hasalpha(image));
		g_assert_cmpint(vips_getpoint(image, &values, &n, 5, 5, NULL), ==, 0);
		g_assert_cmpint(n, ==, 4);
		g_assert_cmpfloat(values[3], ==, 0);
		g_free(values);
		g_assert_cmpint(vips_getpoint(image, &values, &n, (gint)variant->width - 5, 5, NULL), ==, 0);
		g_assert_cmpfloat(values[3], ==, 255);
		g_free(values);
		g_object_unref(image);
	}
}

/* A small image is published as supplied, and nothing is ever upscaled: a
 * portrait photo is capped by its height and only narrower widths are made. */
static void
test_small_and_portrait(void){
	g_autoptr(GBytes) small = image_fixture(400, 300, FALSE, ".png");
	g_autoptr(GBytes) portrait = image_fixture(1000, 3000, FALSE, ".jpg[Q=90]");
	g_autoptr(GPtrArray) none = ls_images_variants("assets/small.png", small, NULL, NULL);
	g_autoptr(GPtrArray) tall = ls_images_variants("assets/tall.jpg", portrait, NULL, NULL);
	LsImageVariant *largest;
	g_assert_nonnull(none);
	g_assert_cmpuint(none->len, ==, 0);
	g_assert_nonnull(tall);
	g_assert_cmpuint(tall->len, ==, 2);
	g_assert_cmpint(((LsImageVariant *)g_ptr_array_index(tall, 0))->width, ==, 480);
	largest = g_ptr_array_index(tall, 1);
	g_assert_cmpstr(largest->path, ==, "assets/tall-533w.webp");
	g_assert_cmpint(largest->width, ==, 533);
	g_assert_cmpint(largest->height, ==, 1600);
}

/* Caps and quality come from options.images; bad options are refused. */
static void
test_options(void){
	g_autoptr(GError) error = NULL;
	g_autoptr(GBytes) png = image_fixture(1500, 1000, FALSE, ".png");
	g_autoptr(JsonObject) custom = options("{\"max\": 1000, \"widths\": [400], \"quality\": 50}");
	g_autoptr(JsonObject) finer = options("{\"quality\": 95}");
	g_autoptr(GPtrArray) variants = NULL;
	g_autoptr(GPtrArray) coarse = NULL, fine = NULL;
	static const gchar *const bad[] = {
		"{\"max\": 100}", "{\"max\": 9000}", "{\"max\": \"1600\"}", "{\"widths\": []}",
		"{\"widths\": [800, 480]}", "{\"widths\": [480, 480]}", "{\"widths\": [1800]}",
		"{\"widths\": [40]}", "{\"widths\": [1, 2, 3, 4, 5, 6, 7, 8, 9]}", "{\"quality\": 0}",
		"{\"quality\": 101}", "{\"format\": \"avif\"}", NULL
	};
	guint i;
	g_assert_true(ls_images_check_options(NULL, &error));
	g_assert_true(ls_images_check_options(custom, &error));
	g_assert_no_error(error);
	for (i = 0; bad[i] != NULL; i++) {
		g_autoptr(JsonObject) object = options(bad[i]);
		g_assert_false(ls_images_check_options(object, &error));
		g_assert_error(error, LS_ERROR, LS_ERROR_INVALID);
		g_assert_nonnull(strstr(error->message, "images"));
		g_clear_error(&error);
	}
	variants = ls_images_variants("assets/x.png", png, custom, &error);
	g_assert_no_error(error);
	g_assert_cmpuint(variants->len, ==, 2);
	g_assert_cmpint(((LsImageVariant *)g_ptr_array_index(variants, 0))->width, ==, 400);
	g_assert_cmpint(((LsImageVariant *)g_ptr_array_index(variants, 1))->width, ==, 1000);
	g_assert_cmpint(((LsImageVariant *)g_ptr_array_index(variants, 1))->height, ==, 667);
	coarse = ls_images_variants("assets/x.png", png, NULL, NULL);
	fine = ls_images_variants("assets/x.png", png, finer, NULL);
	g_assert_cmpuint(bytes_size(((LsImageVariant *)g_ptr_array_index(coarse, 0))->data), <,
		bytes_size(((LsImageVariant *)g_ptr_array_index(fine, 0))->data));
}

/* A file that is not a decodable image is an error, never a variant. */
static void
test_broken(void){
	g_autoptr(GError) error = NULL;
	g_autoptr(GBytes) png = image_fixture(1200, 800, FALSE, ".png");
	g_autoptr(GBytes) cut = g_bytes_new_from_bytes(png, 0, g_bytes_get_size(png) / 2);
	g_autoptr(GBytes) text = g_bytes_new_static("hello", 5);
	g_autoptr(GPtrArray) variants = ls_images_variants("assets/cut.png", cut, NULL, &error);
	g_assert_null(variants);
	g_assert_error(error, LS_ERROR, LS_ERROR_INVALID);
	g_assert_nonnull(strstr(error->message, "assets/cut.png"));
	g_assert_nonnull(strstr(error->message, "truncated"));
	g_clear_error(&error);
	variants = ls_images_variants("assets/text.png", text, NULL, &error);
	g_assert_null(variants);
	g_assert_error(error, LS_ERROR, LS_ERROR_INVALID);
	g_assert_nonnull(strstr(error->message, "not a PNG, JPEG or WebP image"));
	g_clear_error(&error);
	/* Other formats libvips could read never reach a loader. */
	{
		g_autoptr(GBytes) gif = image_fixture(600, 400, FALSE, ".gif");
		variants = ls_images_variants("assets/anim.png", gif, NULL, &error);
		g_assert_null(variants);
		g_assert_error(error, LS_ERROR, LS_ERROR_INVALID);
		g_assert_nonnull(strstr(error->message, "not a PNG, JPEG or WebP image"));
	}
}

/* Plan photos gain variants and their capped size; the bundle gets the WebP
 * files and the caller learns which originals no page uses any more. */
static void
test_prepare_photos(void){
	g_autoptr(GError) error = NULL;
	g_autoptr(GBytes) big = image_fixture(2000, 1000, FALSE, ".png");
	g_autoptr(GBytes) small = image_fixture(300, 200, FALSE, ".png");
	g_autofree gchar *big64 = g_base64_encode(g_bytes_get_data(big, NULL), g_bytes_get_size(big));
	g_autofree gchar *small64 = g_base64_encode(g_bytes_get_data(small, NULL), g_bytes_get_size(small));
	g_autofree gchar *seeds = g_strdup_printf("{\"files\": [{\"path\": \"assets/big.png\", \"encoding\": \"base64\", "
		"\"content\": \"%s\"}, {\"path\": \"assets/small.png\", \"encoding\": \"base64\", \"content\": \"%s\"}]}",
		big64, small64);
	g_autoptr(LsContext) context = ls_context_new();
	g_autoptr(JsonNode) photos_node = ls_json_parse("[{\"path\": \"assets/big.png\", \"alt\": \"Big\", \"width\": 2000, "
		"\"height\": 1000}, {\"path\": \"assets/small.png\", \"alt\": \"Small\", \"width\": 300, \"height\": 200}]", NULL);
	g_autoptr(JsonArray) files = json_array_new();
	g_autoptr(GPtrArray) retired = g_ptr_array_new_with_free_func(g_free);
	JsonArray *photos = json_node_get_array(photos_node);
	JsonObject *first = json_array_get_object_element(photos, 0);
	JsonArray *variants;
	g_assert_true(ls_context_apply(context, seeds, &error));
	g_assert_no_error(error);
	g_assert_true(ls_images_prepare_photos(context, photos, NULL, files, retired, NULL, &error));
	g_assert_no_error(error);
	g_assert_cmpint(json_object_get_int_member(first, "width"), ==, 1600);
	g_assert_cmpint(json_object_get_int_member(first, "height"), ==, 800);
	variants = json_object_get_array_member(first, "variants");
	g_assert_cmpuint(json_array_get_length(variants), ==, 4);
	g_assert_cmpstr(json_object_get_string_member(json_array_get_object_element(variants, 3), "path"), ==,
		"assets/big-1600w.webp");
	g_assert_false(json_object_has_member(json_array_get_object_element(photos, 1), "variants"));
	g_assert_cmpuint(json_array_get_length(files), ==, 4);
	g_assert_cmpstr(json_object_get_string_member(json_array_get_object_element(files, 0), "encoding"), ==, "base64");
	g_assert_cmpuint(retired->len, ==, 1);
	g_assert_cmpstr(g_ptr_array_index(retired, 0), ==, "assets/big.png");
	/* An archived original is still found when the plan is written again. */
	g_assert_true(ls_context_archive(context, "assets/big.png", &error));
	g_ptr_array_set_size(retired, 0);
	g_clear_pointer(&files, json_array_unref);
	files = json_array_new();
	json_object_remove_member(first, "variants");
	g_assert_true(ls_images_prepare_photos(context, photos, NULL, files, retired, NULL, &error));
	g_assert_no_error(error);
	g_assert_cmpuint(json_array_get_length(files), ==, 4);
	/* Two photos whose variants would share names are refused. */
	{
		g_autoptr(JsonNode) clash = ls_json_parse("[{\"path\": \"assets/big.png\", \"alt\": \"a\", \"width\": 2000, "
			"\"height\": 1000}, {\"path\": \"assets/big.jpg\", \"alt\": \"b\", \"width\": 2000, \"height\": 1000}]", NULL);
		g_autoptr(JsonArray) more = json_array_new();
		g_autofree gchar *jpg = g_strdup_printf("{\"files\": [{\"path\": \"assets/big.jpg\", \"encoding\": \"base64\", "
			"\"content\": \"%s\"}]}", big64);
		g_assert_true(ls_context_apply(context, jpg, &error));
		g_assert_false(ls_images_prepare_photos(context, json_node_get_array(clash), NULL, more, retired, NULL, &error));
		g_assert_error(error, LS_ERROR, LS_ERROR_INVALID);
		g_assert_nonnull(strstr(error->message, "assets/big.jpg"));
	}
}

int
main(int argc, char **argv){
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/images/large-png", test_large_png);
	g_test_add_func("/images/deterministic", test_deterministic);
	g_test_add_func("/images/alpha", test_alpha);
	g_test_add_func("/images/small-and-portrait", test_small_and_portrait);
	g_test_add_func("/images/options", test_options);
	g_test_add_func("/images/broken", test_broken);
	g_test_add_func("/images/prepare-photos", test_prepare_photos);
	return g_test_run();
}
