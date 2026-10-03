/* SPDX-License-Identifier: AGPL-3.0-or-later */
#pragma once
#include <gio/gio.h>
#include <json-glib/json-glib.h>
#include "core/ls-context.h"
G_BEGIN_DECLS

/* Defaults for the plan step's options.images; see docs/images.org. */
#define LS_IMAGES_DEFAULT_MAX 1600
#define LS_IMAGES_DEFAULT_QUALITY 80

/**
 * LsImageVariant:
 * @path: the variant's site path, such as assets/bench-800w.webp
 * @width: pixel width
 * @height: pixel height
 * @data: the encoded WebP bytes
 *
 * One published size of a supplied raster image. A boxed value: copy with
 * ls_image_variant_copy() and free with ls_image_variant_free().
 */
typedef struct {
	gchar *path;
	gint64 width;
	gint64 height;
	GBytes *data;
} LsImageVariant;

#define LS_TYPE_IMAGE_VARIANT (ls_image_variant_get_type())
GType ls_image_variant_get_type(void);
/**
 * ls_image_variant_copy:
 * @variant: a variant
 * Returns: (transfer full): a deep copy
 */
LsImageVariant *ls_image_variant_copy(const LsImageVariant *variant);
/**
 * ls_image_variant_free:
 * @variant: (nullable): a variant
 */
void ls_image_variant_free(LsImageVariant *variant);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(LsImageVariant, ls_image_variant_free)

/**
 * ls_images_check_options:
 * @options: (nullable): the plan step's options.images
 * @error: return location for an error
 *
 * Accepts %NULL or {max, widths, quality}: max the long-edge cap in pixels
 * (160..8192), widths 1..8 strictly ascending sizes (80..max), and quality
 * the WebP quality (1..100). Unknown keys are rejected.
 * Returns: whether the options are valid
 */
gboolean ls_images_check_options(JsonObject *options, GError **error);

/**
 * ls_images_variant_path:
 * @path: a source image path such as assets/bench.png
 * @width: the variant width
 * Returns: (transfer full): the variant path, such as assets/bench-480w.webp
 */
gchar *ls_images_variant_path(const gchar *path, gint64 width);

/**
 * ls_images_variants:
 * @path: the source image's site path; names the variants and errors
 * @source: PNG, JPEG or WebP bytes
 * @options: (nullable): checked options.images; %NULL uses the defaults
 * @error: return location for an error
 *
 * Decodes @source (turned upright by its EXIF orientation), caps its long
 * edge at max and encodes that size plus every smaller configured width as
 * WebP at the configured quality, alpha kept, metadata stripped and colour
 * in sRGB. Nothing is upscaled. Output bytes depend only on the inputs. An
 * image no wider than the smallest width and within the cap is published as
 * it is, and the result is empty. Untrusted libvips loaders stay blocked.
 *
 * Returns: (transfer full) (element-type LsImageVariant) (nullable):
 *   variants by ascending width, the last one the capped full size; empty
 *   when the image stays as supplied; %NULL with @error when @source cannot
 *   be decoded
 */
GPtrArray *ls_images_variants(const gchar *path, GBytes *source, JsonObject *options, GError **error);

/**
 * ls_images_prepare_photos:
 * @context: the run's artifact store; seeds are read from its tree or archive
 * @photos: (transfer none): plan photo entries {path, alt, width, height, ...}
 * @options: (nullable): checked options.images
 * @files: (transfer none): a bundle files array; each variant is appended
 *   as a base64 file
 * @retired: (transfer none) (element-type utf8): receives the paths of the
 *   originals no page uses any more, for the caller to archive once @files
 *   is stored
 * @cancellable: (nullable): cancellation token, checked between images
 * @error: return location for an error
 *
 * Sizes every supplied photograph for phones. Each processed entry's width
 * and height become the capped size and it gains variants: [{path, width,
 * height}] by ascending width. Small images are left alone. @context is not
 * changed; on failure @photos, @files and @retired must be discarded.
 * Returns: whether every photo was prepared
 */
/**
 * ls_images_strip_variants:
 * @plan: (transfer none): a private copy of a plan, about to be shown to a model
 *
 * Removes every photo's variants. Pages name a photo by its supplied path;
 * the sized files are the builder's, and a model that saw them might use one.
 */
void ls_images_strip_variants(JsonObject *plan);

gboolean ls_images_prepare_photos(LsContext *context, JsonArray *photos, JsonObject *options, JsonArray *files,
	GPtrArray *retired, GCancellable *cancellable, GError **error);
G_END_DECLS
