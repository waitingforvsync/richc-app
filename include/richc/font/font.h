/*
 * font/font.h - TTF font loader with SDF atlas generation.
 *
 * Loads a TrueType (.ttf) font file and rasterises glyphs for ASCII
 * codepoints 33–126 as signed-distance-field images packed into a
 * single R8 atlas texture.
 *
 * Coordinate conventions
 * ----------------------
 * atlas_rect   — position in the atlas including SDF padding on all sides.
 * offset_x     — horizontal pixels from glyph cursor to left edge of atlas quad.
 * offset_y     — vertical pixels from baseline to TOP edge of atlas quad
 *                (positive means above the baseline).
 * advance      — horizontal cursor advance in pixels.
 *
 * To render glyph g at (cursor_x, baseline_y):
 *   quad_x = cursor_x  + g.offset_x
 *   quad_y = baseline_y - g.offset_y    (screen Y increases downwards)
 *   quad_w = rc_box2i_size(g.atlas_rect).x
 *   quad_h = rc_box2i_size(g.atlas_rect).y
 *   uv from g.atlas_rect in the atlas image.
 *
 * SDF encoding
 * ------------
 * value = clamp(128 + 127 * signed_dist / spread, 0, 255)
 * 128 = on the contour; 255 = well inside; 0 = well outside.
 *
 * Typical usage
 * -------------
 *   rc_arena arena   = rc_arena_make_default();
 *   rc_arena scratch = rc_arena_make_default();
 *   rc_font_atlas_result r = rc_font_atlas_make(
 *       "fonts/Roboto-Regular.ttf", 32, 4, &arena, scratch);
 *   rc_arena_destroy(&scratch);
 *   if (r.error != RC_FONT_OK) { ... }
 *
 *   // r.atlas.atlas is a R8 rc_image in arena.
 */

#ifndef RC_FONT_H_
#define RC_FONT_H_

#include <stdint.h>
#include <stdbool.h>
#include "richc/math/box2i.h"
#include "richc/image/image.h"
#include "richc/arena.h"

/* ---- glyph range ---- */

#define RC_FONT_FIRST_GLYPH  33    /* '!' */
#define RC_FONT_LAST_GLYPH  126    /* '~' */
#define RC_FONT_GLYPH_COUNT  94    /* 126 − 33 + 1 */

/* ---- per-glyph metrics ---- */

typedef struct {
    rc_box2i atlas_rect;   /* position in atlas (includes SDF padding) */
    int32_t  offset_x;     /* pixels: cursor → left edge of atlas quad */
    int32_t  offset_y;     /* pixels: baseline → top edge (positive = above) */
    int32_t  advance;      /* horizontal advance in pixels */
} rc_glyph_metrics;

/* ---- font atlas ---- */

typedef struct {
    rc_image         atlas;                        /* R8 image in caller's arena */
    rc_glyph_metrics glyphs[RC_FONT_GLYPH_COUNT]; /* indexed by codepoint − 33 */
    int32_t          ascender;                     /* pixels above baseline */
    int32_t          descender;                    /* pixels below baseline (negative) */
    int32_t          sdf_spread;                   /* SDF padding in pixels */
    int32_t          pixel_height;                 /* requested pixel height */
} rc_font_atlas;

/* ---- error codes ---- */

typedef enum {
    RC_FONT_OK             = 0,
    RC_FONT_ERROR_NOT_FOUND,    /* file not found */
    RC_FONT_ERROR_INVALID,      /* file is not a valid TTF */
} rc_font_error;

/* ---- result ---- */

typedef struct {
    rc_font_atlas atlas;
    rc_font_error error;
} rc_font_atlas_result;

/* ---- API ---- */

/*
 * Load a TTF font from disk and build a SDF atlas for ASCII 33–126.
 *
 * pixel_height — desired glyph height in pixels (0 → default 32).
 * sdf_spread   — SDF padding in pixels; controls how many pixels of smooth
 *                gradient are generated beyond the glyph edge (0 → default 4).
 * arena        — atlas image and glyph metrics are allocated here.
 * scratch      — all temporary data (font bytes, parsed outlines, per-glyph
 *                SDF buffers) are allocated here and can be freed after the
 *                call returns.
 */
rc_font_atlas_result rc_font_atlas_make(const char *path,
                                         int32_t     pixel_height,
                                         int32_t     sdf_spread,
                                         rc_arena   *arena,
                                         rc_arena    scratch);

#endif /* RC_FONT_H_ */
