/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* Deterministic raster fixtures, drawn at test time so no large binary is
 * committed: colour gradients with fixed-seed grain, like a photograph, and
 * optionally a transparent left half. */
#pragma once
#include <vips/vips.h>
#include <string.h>

/* @suffix is a libvips save suffix such as ".png", ".jpg[Q=90]" or ".webp". */
static GBytes *
image_fixture(gint width, gint height, gboolean alpha, const gchar *suffix){
	gint bands = alpha ? 4 : 3;
	gsize size = (gsize)width * (gsize)height * (gsize)bands;
	guint8 *pixels = g_malloc(size);
	guint32 state = 20261003u;
	VipsImage *raw, *image = NULL;
	void *buffer = NULL;
	size_t length = 0;
	gint x, y;
	g_assert_cmpint(VIPS_INIT("libresite-test"), ==, 0);
	for (y = 0; y < height; y++)
		for (x = 0; x < width; x++) {
			guint8 *pixel = pixels + ((gsize)y * (gsize)width + (gsize)x) * (gsize)bands;
			gint grain;
			state = state * 1103515245u + 12345u;
			grain = (gint)((state >> 16) & 0x1f) - 16;
			pixel[0] = (guint8)CLAMP(x * 200 / width + 30 + grain, 0, 255);
			pixel[1] = (guint8)CLAMP(y * 180 / height + 40 + grain, 0, 255);
			pixel[2] = (guint8)CLAMP((x + y) * 120 / (width + height) + 60 + grain, 0, 255);
			if (alpha)
				pixel[3] = x < width / 2 ? 0 : 255;
		}
	raw = vips_image_new_from_memory_copy(pixels, size, width, height, bands, VIPS_FORMAT_UCHAR);
	g_free(pixels);
	g_assert_nonnull(raw);
	g_assert_cmpint(vips_copy(raw, &image, "interpretation", VIPS_INTERPRETATION_sRGB, NULL), ==, 0);
	g_object_unref(raw);
	g_assert_cmpint(vips_image_write_to_buffer(image, suffix, &buffer, &length, NULL), ==, 0);
	g_object_unref(image);
	return g_bytes_new_take(buffer, length);
}

/* Whether @bytes is a WebP file. */
static G_GNUC_UNUSED gboolean
is_webp(GBytes *bytes){
	gsize size = 0;
	const guint8 *data = g_bytes_get_data(bytes, &size);
	return size > 12 && memcmp(data, "RIFF", 4) == 0 && memcmp(data + 8, "WEBP", 4) == 0;
}
