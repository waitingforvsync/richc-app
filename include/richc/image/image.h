/*
 * image.h - CPU-side image: decoded pixel data with dimensions and format.
 *
 * Supported pixel formats
 * -----------------------
 *   RC_PIXEL_FORMAT_R8    — 1 byte/pixel (greyscale or alpha mask)
 *   RC_PIXEL_FORMAT_RGB8  — 3 bytes/pixel
 *   RC_PIXEL_FORMAT_RGBA8 — 4 bytes/pixel
 *
 * PNG support
 * -----------
 * Supports 8-bit greyscale (→ R8), RGB (→ RGB8), RGBA (→ RGBA8),
 * greyscale+alpha (→ RGBA8), and indexed/palette (→ RGB8) PNGs.
 * 16-bit and interlaced images are not supported (RC_IMAGE_ERROR_UNSUPPORTED).
 *
 * Memory model
 * ------------
 * rc_image.data is a non-owning view into arena memory.  The arena that was
 * passed to the load function owns the pixel bytes; rc_image is a descriptor.
 *
 * Both load functions accept a scratch arena by value for temporary allocations
 * (inflate buffer, concatenated file bytes).  These are not visible to the caller.
 *
 * Usage
 * -----
 *   rc_arena scratch = rc_arena_make_default();
 *   rc_image_result r = rc_image_load_png(RC_STR("tex.png"), &arena, scratch);
 *   rc_arena_destroy(&scratch);
 *   if (r.error) { ... }
 *   // r.image.data, .width, .height, .stride, .format
 */

#ifndef RC_IMAGE_IMAGE_H_
#define RC_IMAGE_IMAGE_H_

#include <stdint.h>
#include "richc/bytes.h"
#include "richc/str.h"
#include "richc/arena.h"

/* ---- pixel format ---- */

typedef enum {
    RC_PIXEL_FORMAT_R8    = 1,   /* 1 byte/pixel */
    RC_PIXEL_FORMAT_RGB8  = 3,   /* 3 bytes/pixel */
    RC_PIXEL_FORMAT_RGBA8 = 4,   /* 4 bytes/pixel */
} rc_pixel_format;

/* ---- image descriptor ---- */

/*
 * Non-owning view of decoded pixel data.
 * Row y starts at data.data + y * stride.
 */
typedef struct {
    rc_view_bytes   data;    /* raw pixel bytes (not owned) */
    int32_t         width;
    int32_t         height;
    int32_t         stride;  /* bytes per row (>= width * bytes_per_pixel) */
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

/* ---- API ---- */

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
rc_image_result rc_image_load_png(rc_str path,
                                   rc_arena *arena, rc_arena scratch);

#endif /* RC_IMAGE_IMAGE_H_ */
