/*
 * image.h - CPU-side image: decoded pixel data with dimensions and format.
 *
 * Supported pixel formats
 * -----------------------
 *   RC_PIXEL_FORMAT_R8    — 1 byte/pixel (greyscale or alpha mask)
 *   RC_PIXEL_FORMAT_RGB8  — 3 bytes/pixel
 *   RC_PIXEL_FORMAT_RGBA8 — 4 bytes/pixel
 *
 * Pixel format values equal the bytes-per-pixel count, so
 * rc_pixel_format_bytes_per_pixel is just a cast.
 *
 * PNG support
 * -----------
 * Supports 8-bit greyscale (→ R8), RGB (→ RGB8), RGBA (→ RGBA8),
 * greyscale+alpha (→ RGBA8), and indexed/palette (→ RGB8) PNGs.
 * 16-bit and interlaced images are not supported (RC_IMAGE_ERROR_UNSUPPORTED).
 *
 * Memory model
 * ------------
 * rc_image.data is a mutable non-owning span into arena memory.  Row y starts
 * at data.data + y * stride.  The arena that was passed to the load/make
 * function owns the pixel bytes; rc_image is a descriptor.
 *
 * Images are always stored top-row-first (y=0 is the top of the image).
 * stride >= size.x * bytes_per_pixel; use rc_image_make_subimage to slice
 * a region with a larger-than-tight stride.
 *
 * Both load functions accept a scratch arena by value for temporary allocations
 * (inflate buffer, concatenated file bytes).  These are not visible to the caller.
 *
 * Construction
 * ------------
 *   rc_image_make(size, format, arena)          — blank zero-filled image
 *   rc_image_make_subimage(img, region)         — view into img; region clamped
 *
 * Copy
 * ----
 *   rc_image_blit(dst, dst_pos, src)            — copy src into dst; clips to
 *                                                  dst bounds; returns false if
 *                                                  src.format > dst.format
 *
 * Usage
 * -----
 *   rc_arena scratch = rc_arena_make_default();
 *   rc_image_result r = rc_image_load_png("tex.png", &arena, scratch);
 *   rc_arena_destroy(&scratch);
 *   if (r.error) { ... }
 *   // r.image.data, .size, .stride, .format
 */

#ifndef RC_IMAGE_IMAGE_H_
#define RC_IMAGE_IMAGE_H_

#include <stdint.h>
#include "richc/bytes.h"
#include "richc/arena.h"
#include "richc/math/vec2i.h"
#include "richc/math/box2i.h"

/* ---- pixel format ---- */

typedef enum {
    RC_PIXEL_FORMAT_R8    = 1,   /* 1 byte/pixel */
    RC_PIXEL_FORMAT_RGB8  = 3,   /* 3 bytes/pixel */
    RC_PIXEL_FORMAT_RGBA8 = 4,   /* 4 bytes/pixel */
} rc_pixel_format;

/* Bytes per pixel; valid for all rc_pixel_format values. */
static inline uint32_t rc_pixel_format_bytes_per_pixel(rc_pixel_format fmt)
{
    return (uint32_t)fmt;
}

/* ---- image descriptor ---- */

/*
 * Mutable non-owning span of decoded pixel data.
 * Row y starts at data.data + y * stride.
 * data.num == size.y * stride for full images; for subimages it covers the
 * rows of the subimage within the parent stride.
 */
typedef struct {
    rc_span_bytes   data;    /* raw pixel bytes (not owned) */
    rc_vec2i        size;    /* width (x) and height (y) in pixels */
    uint32_t        stride;  /* bytes per row (>= size.x * bytes_per_pixel) */
    rc_pixel_format format;
} rc_image;

/* ---- error codes ---- */

typedef enum {
    RC_IMAGE_OK               = 0,
    RC_IMAGE_ERROR_NOT_PNG,        /* missing or invalid PNG signature */
    RC_IMAGE_ERROR_UNSUPPORTED,    /* 16-bit depth, interlacing, unknown colour type */
    RC_IMAGE_ERROR_CORRUPT,        /* malformed chunks or decompression failure */
    RC_IMAGE_ERROR_IO,             /* file not found or read error */
} rc_image_error;

typedef struct {
    rc_image       image;
    rc_image_error error;
} rc_image_result;

/* ---- construction ---- */

/*
 * Create an image filled with fill_pixel (pointer to bytes_per_pixel bytes).
 * Pass NULL for fill_pixel to zero-fill.
 * stride = size.x * bytes_per_pixel.
 */
rc_image rc_image_make(rc_vec2i size, rc_pixel_format format,
                        const uint8_t *fill_pixel, rc_arena *arena);

/*
 * View into a sub-region of img with the same stride.
 * region is clamped to img.size; returns a zero-size image if the clamped
 * region is empty.
 */
rc_image rc_image_make_subimage(rc_image img, rc_box2i region);

/* ---- copy ---- */

/*
 * Copy src into dst at dst_pos (top-left corner of src in dst coordinates).
 * The copy is clipped to dst bounds; out-of-range areas are silently skipped.
 * Format expanding conversions are applied automatically:
 *   R8   → RGB8:  (r, r, r)
 *   R8   → RGBA8: (r, r, r, 255)
 *   RGB8 → RGBA8: (r, g, b, 255)
 * Returns false (no copy performed) if src.format > dst.format (narrowing).
 */
bool rc_image_blit(rc_image dst, rc_vec2i dst_pos, rc_image src);

/* ---- PNG loading ---- */

/*
 * Decode a PNG from an in-memory buffer.
 * Pixel data is allocated from arena.  scratch is used for the inflate buffer
 * and any other temporary allocations.
 */
rc_image_result rc_image_from_png(rc_view_bytes png_data,
                                   rc_arena *arena, rc_arena scratch);

/*
 * Convenience: load a PNG file and decode it.
 * The raw file bytes are loaded into scratch (disposable); only the decoded
 * pixel data is written to arena.
 */
rc_image_result rc_image_load_png(const char *path,
                                   rc_arena *arena, rc_arena scratch);

#endif /* RC_IMAGE_IMAGE_H_ */
