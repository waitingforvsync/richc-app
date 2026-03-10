/*
 * font.c - TTF font loader with analytic SDF atlas generation.
 *
 * Parses TrueType binary tables (big-endian), extracts simple-glyph outlines
 * for ASCII 33–126, rasterises each glyph as a signed-distance-field R8 image
 * into scratch memory, then packs all images into a single R8 atlas using
 * rc_image_pack.
 *
 * Composite glyphs (numberOfContours < 0) are silently skipped.
 * Glyph metrics (atlas_rect, offset_x/y, advance) are populated for all glyphs
 * including skipped ones; advance is still read from hmtx.
 *
 * SDF encoding: value = clamp(128 + 127 * signed_dist / spread, 0, 255)
 *   128 = on the contour; 255 = inside; 0 = outside.
 */

#include "richc/font/font.h"
#include "richc/math/solve.h"
#include "richc/image/image.h"
#include "richc/image/image_pack.h"
#include "richc/file.h"
#include "richc/arena.h"
#include "richc/debug.h"
#include <math.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

/* rc_array_bytes (rc_view_bytes / rc_span_bytes) comes from image.h → bytes.h */

/* ======================================================================== */
/* Big-endian read helpers                                                   */
/* ======================================================================== */

static uint8_t  read_u8_ (const uint8_t *p) { return p[0]; }
static uint16_t read_u16_(const uint8_t *p) { return (uint16_t)((uint16_t)p[0] << 8 | p[1]); }
static int16_t  read_i16_(const uint8_t *p) { return (int16_t)read_u16_(p); }
static uint32_t read_u32_(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] <<  8) |  (uint32_t)p[3];
}

/* ======================================================================== */
/* Table directory                                                           */
/* ======================================================================== */

typedef struct {
    const uint8_t *data;
    uint32_t       len;
} table_view_;

/*
 * Locate a named table within a TTF blob.
 * TTF offset table: sfVersion[4] numTables[2] searchRange[2] entrySelector[2] rangeShift[2]
 * Each 16-byte record: tag[4] checkSum[4] offset[4] length[4]
 */
static table_view_ find_table_(const uint8_t *font, uint32_t font_len, const char tag[4])
{
    if (font_len < 12) return (table_view_) {NULL, 0};
    uint16_t num = read_u16_(font + 4);
    for (uint32_t i = 0; i < num; i++) {
        uint32_t rec = 12u + i * 16u;
        if (rec + 16u > font_len) break;
        if (memcmp(font + rec, tag, 4) == 0) {
            uint32_t off = read_u32_(font + rec + 8);
            uint32_t len = read_u32_(font + rec + 12);
            if (off + len > font_len) break;
            return (table_view_) {font + off, len};
        }
    }
    return (table_view_) {NULL, 0};
}

/* ======================================================================== */
/* cmap format-4 lookup                                                      */
/* ======================================================================== */

/*
 * Look up a Unicode codepoint in a format-4 cmap subtable.
 * Returns 0 (.notdef) if unmapped.
 *
 * Format-4 layout:
 *   [0..1]  format (4)
 *   [2..3]  length
 *   [4..5]  language
 *   [6..7]  segCountX2
 *   [8..13] searchRange / entrySelector / rangeShift
 *   [14 ..]  endCode[segCount]
 *   [14 + segCount*2] reservedPad
 *   [14 + segCount*2 + 2] startCode[segCount]
 *   [14 + segCount*4 + 2] idDelta[segCount]
 *   [14 + segCount*6 + 2] idRangeOffset[segCount]
 *   [14 + segCount*8 + 2] glyphIdArray[]
 */
static uint16_t cmap_lookup_(const uint8_t *sub, uint32_t sub_len, uint32_t cp)
{
    if (sub_len < 14u || read_u16_(sub) != 4u) return 0;
    uint16_t seg_count = read_u16_(sub + 6) / 2u;
    if (seg_count == 0) return 0;

    uint32_t end_base   = 14u;
    uint32_t start_base = 14u + (uint32_t)seg_count * 2u + 2u;
    uint32_t delta_base = 14u + (uint32_t)seg_count * 4u + 2u;
    uint32_t iro_base   = 14u + (uint32_t)seg_count * 6u + 2u;
    if (iro_base + (uint32_t)seg_count * 2u > sub_len) return 0;

    /* Binary search for the segment covering cp. */
    uint32_t lo = 0, hi = seg_count;
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2u;
        if (read_u16_(sub + end_base + mid * 2u) < (uint16_t)cp) lo = mid + 1u;
        else hi = mid;
    }
    if (lo >= seg_count) return 0;

    uint16_t start = read_u16_(sub + start_base + lo * 2u);
    uint16_t end   = read_u16_(sub + end_base   + lo * 2u);
    if (cp < start || cp > end) return 0;

    int16_t  delta = read_i16_(sub + delta_base + lo * 2u);
    uint16_t iro   = read_u16_(sub + iro_base   + lo * 2u);

    if (iro == 0) {
        return (uint16_t)((cp + (uint32_t)(uint16_t)delta) & 0xFFFFu);
    } else {
        /* iro is a byte offset FROM the position of idRangeOffset[i] TO
         * the glyphIdArray entry for startCode[i]. */
        uint32_t iro_pos   = iro_base + lo * 2u;
        uint32_t glyph_off = iro_pos + iro + (cp - start) * 2u;
        if (glyph_off + 2u > sub_len) return 0;
        uint16_t glyph_idx = read_u16_(sub + glyph_off);
        if (glyph_idx == 0) return 0;
        return (uint16_t)((glyph_idx + (uint32_t)(uint16_t)delta) & 0xFFFFu);
    }
}

/* ======================================================================== */
/* loca offset                                                               */
/* ======================================================================== */

/* Byte offset of glyph idx in the glyf table. loca_fmt 0=short(×2), 1=long. */
static uint32_t loca_offset_(const uint8_t *loca, uint32_t loca_len,
                              int16_t loca_fmt, uint32_t idx)
{
    if (loca_fmt == 0) {
        uint32_t off = idx * 2u;
        if (off + 2u > loca_len) return 0;
        return (uint32_t)read_u16_(loca + off) * 2u;
    } else {
        uint32_t off = idx * 4u;
        if (off + 4u > loca_len) return 0;
        return read_u32_(loca + off);
    }
}

/* ======================================================================== */
/* Outline parsing                                                           */
/* ======================================================================== */

/* A single outline segment: line (is_curve=false) or quadratic Bézier. */
typedef struct {
    float x0, y0;           /* start point */
    float x1, y1;           /* control point (quadratic Bézier only) */
    float x2, y2;           /* end point */
    bool  is_curve;
} seg_;

/* Array type for dynamic segment accumulation. */
#define ARRAY_T seg_
#include "richc/template/array.h"

#define FLAG_ON_CURVE_   0x01u
#define FLAG_X_SHORT_    0x02u
#define FLAG_Y_SHORT_    0x04u
#define FLAG_REPEAT_     0x08u
#define FLAG_X_SAME_POS_ 0x10u
#define FLAG_Y_SAME_POS_ 0x20u

/*
 * Parse a simple TrueType glyph into a dynamic array of segments.
 *
 * Points are converted to pixel-space coordinates:
 *   px_x =  (font_x - x_min_font) * scale
 *   px_y = -(font_y - y_max_font) * scale   (Y-up → Y-down)
 *
 * Returns an rc_array_seg_ allocated from scratch.
 * On error or composite/empty glyph returns a zero-initialised array.
 */
static rc_array_seg_ parse_glyph_(const uint8_t *gd, uint32_t gd_len,
                                    float scale, float x_min_font, float y_max_font,
                                    rc_arena *scratch)
{
    rc_array_seg_ result = {0};
    if (gd_len < 10u) return result;

    int16_t n_contours_i = read_i16_(gd);
    if (n_contours_i <= 0) return result;   /* empty or composite */

    uint32_t nc = (uint32_t)n_contours_i;

    /* endPtsOfContours[nc] @ byte 10 */
    if (10u + nc * 2u + 2u > gd_len) return result;
    uint16_t *end_pts = rc_arena_alloc_type(scratch, uint16_t, nc);
    if (!end_pts) return result;
    for (uint32_t k = 0; k < nc; k++)
        end_pts[k] = read_u16_(gd + 10u + k * 2u);

    uint32_t n_pts = (uint32_t)end_pts[nc - 1] + 1u;

    /* instructionLength @ byte (10 + nc*2) */
    uint32_t instr_len_off = 10u + nc * 2u;
    uint16_t instr_len     = read_u16_(gd + instr_len_off);
    uint32_t pos           = instr_len_off + 2u + instr_len; /* pos = start of flags */
    if (pos > gd_len) return result;

    /* ---- decode flags (with RLE repeat) ---- */
    uint8_t *flags = rc_arena_alloc_type(scratch, uint8_t, n_pts);
    if (!flags) return result;
    for (uint32_t fi = 0; fi < n_pts; ) {
        if (pos >= gd_len) return result;
        uint8_t f = read_u8_(gd + pos++);
        flags[fi++] = f;
        if (f & FLAG_REPEAT_) {
            if (pos >= gd_len) return result;
            uint8_t rep = read_u8_(gd + pos++);
            while (rep-- && fi < n_pts) flags[fi++] = f;
        }
    }

    /* ---- decode x coordinates ---- */
    float *fx = rc_arena_alloc_type(scratch, float, n_pts);
    if (!fx) return result;
    {
        int32_t cur = 0;
        for (uint32_t i = 0; i < n_pts; i++) {
            uint8_t f = flags[i];
            if (f & FLAG_X_SHORT_) {
                if (pos >= gd_len) return result;
                int32_t d = (int32_t)read_u8_(gd + pos++);
                if (!(f & FLAG_X_SAME_POS_)) d = -d;
                cur += d;
            } else if (!(f & FLAG_X_SAME_POS_)) {
                if (pos + 2u > gd_len) return result;
                cur += (int32_t)read_i16_(gd + pos);
                pos += 2u;
            }
            fx[i] = ((float)cur - x_min_font) * scale;
        }
    }

    /* ---- decode y coordinates ---- */
    float *fy = rc_arena_alloc_type(scratch, float, n_pts);
    if (!fy) return result;
    {
        int32_t cur = 0;
        for (uint32_t i = 0; i < n_pts; i++) {
            uint8_t f = flags[i];
            if (f & FLAG_Y_SHORT_) {
                if (pos >= gd_len) return result;
                int32_t d = (int32_t)read_u8_(gd + pos++);
                if (!(f & FLAG_Y_SAME_POS_)) d = -d;
                cur += d;
            } else if (!(f & FLAG_Y_SAME_POS_)) {
                if (pos + 2u > gd_len) return result;
                cur += (int32_t)read_i16_(gd + pos);
                pos += 2u;
            }
            /* Flip Y: font Y-up → pixel Y-down */
            fy[i] = (y_max_font - (float)cur) * scale;
        }
    }

    /* ---- extract segments via dynamic array ---- */
    rc_array_seg_ segs = {0};
    rc_array_seg__reserve(&segs, n_pts, scratch);   /* upper bound */

    uint32_t cont_start = 0u;
    for (uint32_t c = 0; c < nc; c++) {
        uint32_t cont_end = (uint32_t)end_pts[c];
        uint32_t cn       = cont_end - cont_start + 1u;
        if (cn < 2u) { cont_start = cont_end + 1u; continue; }

        /* Find the first on-curve point as the contour start. */
        uint32_t s0 = 0u;
        bool found_on = false;
        for (uint32_t k = 0u; k < cn; k++) {
            if (flags[cont_start + k] & FLAG_ON_CURVE_) {
                s0 = k; found_on = true; break;
            }
        }

        /* Starting on-curve position (real or synthetic midpoint). */
        float sx, sy;
        if (found_on) {
            sx = fx[cont_start + s0];
            sy = fy[cont_start + s0];
        } else {
            /* All off-curve: synthetic start = midpoint of last and first pts. */
            float lx = fx[cont_start + cn - 1u], ly = fy[cont_start + cn - 1u];
            sx = (fx[cont_start] + lx) * 0.5f;
            sy = (fy[cont_start] + ly) * 0.5f;
            s0 = 0u;
        }

        float    cur_x = sx, cur_y = sy;
        uint32_t remaining = cn;
        uint32_t i = (s0 + 1u) % cn;

        while (remaining > 0u) {
            uint32_t abs_i = cont_start + i;

            if (flags[abs_i] & FLAG_ON_CURVE_) {
                rc_array_seg__push(&segs, (seg_) {cur_x, cur_y, 0, 0, fx[abs_i], fy[abs_i], false}, scratch);
                cur_x = fx[abs_i]; cur_y = fy[abs_i];
                remaining--;
                i = (i + 1u) % cn;
            } else {
                float    ctrl_x = fx[abs_i], ctrl_y = fy[abs_i];
                uint32_t j      = (i + 1u) % cn;
                uint32_t abs_j  = cont_start + j;
                float    p2x, p2y;
                uint32_t advance;

                if (remaining == 1u) {
                    /* Last point; close back to start. */
                    p2x = sx; p2y = sy; advance = 1u;
                } else if (flags[abs_j] & FLAG_ON_CURVE_) {
                    p2x = fx[abs_j]; p2y = fy[abs_j]; advance = 2u;
                } else {
                    /* Two consecutive off-curve: implied on-curve midpoint. */
                    p2x = (ctrl_x + fx[abs_j]) * 0.5f;
                    p2y = (ctrl_y + fy[abs_j]) * 0.5f;
                    advance = 1u;
                }

                rc_array_seg__push(&segs, (seg_) {cur_x, cur_y, ctrl_x, ctrl_y, p2x, p2y, true}, scratch);
                cur_x = p2x; cur_y = p2y;
                remaining -= advance;
                i = (i + advance) % cn;
            }
        }

        cont_start = cont_end + 1u;
    }

    return segs;
}

/* ======================================================================== */
/* Distance helpers                                                          */
/* ======================================================================== */

/* Squared distance from (px,py) to line segment P0→P1. */
static float line_dist_sqr_(float p0x, float p0y, float p1x, float p1y,
                              float px, float py)
{
    float ax = p1x - p0x, ay = p1y - p0y;
    float wx = p0x - px,  wy = p0y - py;
    float len2 = ax * ax + ay * ay;
    if (len2 < 1e-12f) return wx * wx + wy * wy;
    float t = -(wx * ax + wy * ay) / len2;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    float dx = wx + t * ax, dy = wy + t * ay;
    return dx * dx + dy * dy;
}

/*
 * Squared distance from (px,py) to quadratic Bézier P0→(P1 control)→P2.
 * B(t) = P0 + 2t*A + t²*C,  A = P1-P0,  C = P0-2P1+P2.
 * Minimise |B(t)-X|² → cubic: |C|²t³ + 3(A·C)t² + (2|A|²+W·C)t + W·A = 0.
 */
static float qbez_dist_sqr_(float p0x, float p0y,
                              float p1x, float p1y,
                              float p2x, float p2y,
                              float px,  float py)
{
    float ax = p1x - p0x, ay = p1y - p0y;
    float cx = p0x - 2.0f * p1x + p2x;
    float cy = p0y - 2.0f * p1y + p2y;
    float wx = p0x - px, wy = p0y - py;

    float cc  = cx * cx + cy * cy;
    float ac  = ax * cx + ay * cy;
    float aa  = ax * ax + ay * ay;
    float wc  = wx * cx + wy * cy;
    float wa  = wx * ax + wy * ay;

    rc_cubic_roots cr = rc_solve_cubic(cc, 3.0f * ac, 2.0f * aa + wc, wa);

    float min_dsq = 1e30f;
    for (int r = 0; r < cr.num_roots; r++) {
        float t = cr.root[r];
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
        float bx = p0x + 2.0f * t * ax + t * t * cx - px;
        float by = p0y + 2.0f * t * ay + t * t * cy - py;
        float dsq = bx * bx + by * by;
        if (dsq < min_dsq) min_dsq = dsq;
    }
    /* Always check endpoints (they aren't guaranteed to be roots). */
    { float dx = p0x - px, dy = p0y - py; float dsq = dx*dx+dy*dy; if (dsq < min_dsq) min_dsq = dsq; }
    { float dx = p2x - px, dy = p2y - py; float dsq = dx*dx+dy*dy; if (dsq < min_dsq) min_dsq = dsq; }
    return min_dsq;
}

/* ======================================================================== */
/* Winding helpers                                                           */
/* ======================================================================== */

/*
 * Winding contribution from line P0→P1 at query (px,py).
 * Ray in +x direction.
 *
 * Half-open interval is direction-dependent to handle shared vertices:
 *   going down (dy > 0): include t=0, exclude t=1
 *   going up   (dy < 0): exclude t=0, include t=1 — but t=1 (p2y==py) is
 *                         never counted; the next segment's t=0 handles it
 *
 * t=1 (p2y==py) is always skipped so we never double-count a vertex shared
 * with the next segment.  For t=0 (p0y==py), an "arch" pattern is detected
 * via prev_dy: if the previous non-horizontal segment approached from the
 * same side that this one is leaving to, the contour merely touches the
 * scanline and should contribute 0 net winding.
 */
static int line_winding_(float p0x, float p0y, float p2x, float p2y,
                          float px, float py, float prev_dy)
{
    float dy = p2y - p0y;
    if (dy == 0.0f) return 0;
    if (p2y == py) return 0;   /* never count t=1; next seg's t=0 handles it */
    float t = (py - p0y) / dy;
    if (t < 0.0f || t > 1.0f) return 0;
    if (p0y == py) {           /* t=0 endpoint — check for arch pattern */
        if (dy > 0.0f && prev_dy < 0.0f) return 0;  /* arch from below */
        if (dy < 0.0f && prev_dy > 0.0f) return 0;  /* arch from above */
    }
    float x_cross = p0x + t * (p2x - p0x);
    if (x_cross <= px) return 0;
    return (dy > 0.0f) ? 1 : -1;
}

/*
 * Winding contribution from quadratic Bézier P0→P1→P2 at query (px,py).
 * Solves B_y(t) = py to find crossings.
 *
 * Endpoint convention mirrors line_winding_:
 *   t=1 (p2y==py): never counted; next segment's t=0 handles the vertex.
 *   t=0 (p0y==py): arch-pattern check via prev_dy — skip if the contour
 *                  merely touches the scanline from one side (dyt and prev_dy
 *                  indicate the same side: below→touch→below or above→touch→above).
 *   dyt = 0 (tangent at interior): skip.
 */
static int qbez_winding_(float p0x, float p0y,
                          float p1x, float p1y,
                          float p2x, float p2y,
                          float px, float py, float prev_dy)
{
    float ay = p1y - p0y;
    float cy = p0y - 2.0f * p1y + p2y;
    float ax = p1x - p0x;
    float cx = p0x - 2.0f * p1x + p2x;
    int winding = 0;

    rc_quadratic_roots qr = rc_solve_quadratic(cy, 2.0f * ay, p0y - py);
    for (int r = 0; r < qr.num_roots; r++) {
        float t = qr.root[r];
        if (t < -1e-6f || t > 1.0f + 1e-6f) continue;
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
        float dyt = 2.0f * ay + 2.0f * cy * t;
        if (dyt == 0.0f) continue;
        float x_cross = p0x + 2.0f * t * ax + t * t * cx;
        if (x_cross <= px) continue;
        if (t >= 1.0f) continue;   /* never count t=1 endpoint */
        if (t <= 0.0f) {           /* t=0 endpoint — check for arch pattern */
            if (dyt > 0.0f && prev_dy < 0.0f) continue;  /* arch from below */
            if (dyt < 0.0f && prev_dy > 0.0f) continue;  /* arch from above */
        }
        winding += (dyt > 0.0f) ? 1 : -1;
    }
    return winding;
}

/* ======================================================================== */
/* SDF rasterisation                                                         */
/* ======================================================================== */

/*
 * Generate an R8 SDF image for the glyph described by segs.
 * Image dimensions are img_w × img_h pixels.  Pixels are allocated from scratch.
 */
static rc_image rasterise_sdf_(rc_array_seg_ segs,
                                 uint32_t img_w, uint32_t img_h, float spread,
                                 rc_arena *scratch)
{
    uint32_t n_pixels = img_w * img_h;
    rc_array_bytes pixels = {0};
    rc_array_bytes_reserve(&pixels, n_pixels, scratch);

    for (uint32_t row = 0; row < img_h; row++) {
        for (uint32_t col = 0; col < img_w; col++) {
            float px = (float)col + 0.5f;
            float py = (float)row + 0.5f;

            float min_dsq = 1e30f;
            int   winding = 0;
            float prev_nonhoriz_dy = 0.0f;

            for (uint32_t s = 0; s < segs.num; s++) {
                const seg_ *sg = &segs.data[s];
                float seg_dy = sg->y2 - sg->y0;
                if (!sg->is_curve) {
                    float dsq = line_dist_sqr_(sg->x0, sg->y0, sg->x2, sg->y2, px, py);
                    if (dsq < min_dsq) min_dsq = dsq;
                    winding += line_winding_(sg->x0, sg->y0, sg->x2, sg->y2,
                                             px, py, prev_nonhoriz_dy);
                } else {
                    float dsq = qbez_dist_sqr_(sg->x0, sg->y0, sg->x1, sg->y1,
                                               sg->x2, sg->y2, px, py);
                    if (dsq < min_dsq) min_dsq = dsq;
                    winding += qbez_winding_(sg->x0, sg->y0, sg->x1, sg->y1,
                                             sg->x2, sg->y2, px, py, prev_nonhoriz_dy);
                }
                if (seg_dy != 0.0f) prev_nonhoriz_dy = seg_dy;
            }

            float dist = sqrtf(min_dsq);
            if (winding != 0) dist = -dist;   /* negative = inside */

            /* Encode: 128 = contour, 255 = inside, 0 = outside */
            float encoded = 128.0f - 127.0f * dist / spread;
            if (encoded < 0.0f)   encoded = 0.0f;
            if (encoded > 255.0f) encoded = 255.0f;
            rc_array_bytes_push(&pixels, (uint8_t)encoded, scratch);
        }
    }

    return (rc_image) {
        .data   = pixels.span,
        .size   = {(int32_t)img_w, (int32_t)img_h},
        .stride = img_w,
        .format = RC_PIXEL_FORMAT_R8,
    };
}

/* ======================================================================== */
/* Atlas sizing helper                                                       */
/* ======================================================================== */

static uint32_t next_pow2_(uint32_t v)
{
    if (v == 0) return 1;
    v--;
    v |= v >> 1; v |= v >> 2; v |= v >> 4; v |= v >> 8; v |= v >> 16;
    return v + 1;
}

/* ======================================================================== */
/* Public API                                                                */
/* ======================================================================== */

rc_font_atlas_result rc_font_atlas_make(const char *path,
                                         int32_t     pixel_height,
                                         int32_t     sdf_spread,
                                         rc_arena   *arena,
                                         rc_arena    scratch)
{
    rc_font_atlas_result result;
    memset(&result, 0, sizeof(result));

    if (pixel_height <= 0) pixel_height = 32;
    if (sdf_spread   <= 0) sdf_spread   = 4;

    /* ---- load font file into scratch ---- */
    rc_load_binary_result file = rc_load_binary(path, &scratch);
    if (file.error != RC_FILE_OK) {
        result.error = RC_FONT_ERROR_NOT_FOUND;
        return result;
    }
    const uint8_t *font     = file.data.data;
    uint32_t       font_len = file.data.num;

    /* ---- locate required tables ---- */
    table_view_ head = find_table_(font, font_len, "head");
    table_view_ hhea = find_table_(font, font_len, "hhea");
    table_view_ cmap = find_table_(font, font_len, "cmap");
    table_view_ hmtx = find_table_(font, font_len, "hmtx");
    table_view_ loca = find_table_(font, font_len, "loca");
    table_view_ glyf = find_table_(font, font_len, "glyf");

    if (!head.data || !hhea.data || !cmap.data ||
        !hmtx.data || !loca.data || !glyf.data) {
        result.error = RC_FONT_ERROR_INVALID;
        return result;
    }
    if (head.len < 54u || hhea.len < 36u) {
        result.error = RC_FONT_ERROR_INVALID;
        return result;
    }

    /* ---- read scalar metrics ---- */
    int16_t  loca_fmt    = read_i16_(head.data + 50);
    int16_t  ascent      = read_i16_(hhea.data + 4);
    int16_t  descent     = read_i16_(hhea.data + 6);
    uint16_t n_h_metrics = read_u16_(hhea.data + 34);

    int32_t line_height = (int32_t)ascent - (int32_t)descent;
    if (line_height <= 0) { result.error = RC_FONT_ERROR_INVALID; return result; }

    float scale = (float)pixel_height / (float)line_height;

    result.atlas.sdf_spread   = sdf_spread;
    result.atlas.pixel_height = pixel_height;
    result.atlas.ascender     = (int32_t)((float)ascent  * scale + 0.5f);
    result.atlas.descender    = (int32_t)((float)descent * scale - 0.5f);

    /* ---- locate cmap format-4 subtable ---- */
    const uint8_t *cmap_sub     = NULL;
    uint32_t       cmap_sub_len = 0u;
    if (cmap.len >= 4u) {
        uint16_t n_cmap = read_u16_(cmap.data + 2);
        /* Prefer platform=3 (Windows) / encoding=1 (BMP Unicode). */
        for (int pass = 0; pass < 2 && !cmap_sub; pass++) {
            for (uint16_t ci = 0; ci < n_cmap; ci++) {
                uint32_t rec = 4u + (uint32_t)ci * 8u;
                if (rec + 8u > cmap.len) break;
                uint16_t plat = read_u16_(cmap.data + rec);
                uint16_t enc  = read_u16_(cmap.data + rec + 2);
                uint32_t off  = read_u32_(cmap.data + rec + 4);
                bool want = (pass == 0)
                    ? ((plat == 3 && enc == 1) || (plat == 0 && enc == 3))
                    : true;
                if (!want || off + 2u > cmap.len) continue;
                if (read_u16_(cmap.data + off) == 4u) {
                    cmap_sub     = cmap.data + off;
                    cmap_sub_len = cmap.len  - off;
                    break;
                }
            }
        }
    }
    if (!cmap_sub) { result.error = RC_FONT_ERROR_INVALID; return result; }

    /* ---- parse glyphs 33–126, generate SDF images ---- */

    rc_image glyph_images[RC_FONT_GLYPH_COUNT];
    memset(glyph_images, 0, sizeof(glyph_images));

    for (int cp = RC_FONT_FIRST_GLYPH; cp <= RC_FONT_LAST_GLYPH; cp++) {
        int gi = cp - RC_FONT_FIRST_GLYPH;

        uint16_t glyph_idx = cmap_lookup_(cmap_sub, cmap_sub_len, (uint32_t)cp);

        /* Advance width from hmtx (hmtx entry = [advanceWidth(2), lsb(2)] per glyph). */
        uint32_t aw_idx  = (glyph_idx < n_h_metrics) ? (uint32_t)glyph_idx
                                                      : (uint32_t)(n_h_metrics - 1u);
        uint32_t hmtx_off = aw_idx * 4u;
        uint16_t adv_fw  = (hmtx_off + 2u <= hmtx.len)
                           ? read_u16_(hmtx.data + hmtx_off) : 0u;
        result.atlas.glyphs[gi].advance = (int32_t)((float)adv_fw * scale + 0.5f);

        if (glyph_idx == 0u) continue;

        /* Glyph byte range in glyf. */
        uint32_t g_off  = loca_offset_(loca.data, loca.len, loca_fmt, (uint32_t)glyph_idx);
        uint32_t g_off2 = loca_offset_(loca.data, loca.len, loca_fmt, (uint32_t)glyph_idx + 1u);
        if (g_off >= g_off2 || g_off >= glyf.len) continue;

        uint32_t g_len = g_off2 - g_off;
        if (g_len < 10u) continue;

        const uint8_t *gd = glyf.data + g_off;

        /* Glyph bounding box in font units. */
        int16_t x_min_i = read_i16_(gd + 2);
        int16_t y_min_i = read_i16_(gd + 4);
        int16_t x_max_i = read_i16_(gd + 6);
        int16_t y_max_i = read_i16_(gd + 8);

        int32_t dx = (int32_t)x_max_i - (int32_t)x_min_i;
        int32_t dy = (int32_t)y_max_i - (int32_t)y_min_i;
        if (dx <= 0 || dy <= 0) continue;

        uint32_t glyph_w = (uint32_t)((float)dx * scale + 0.5f);
        uint32_t glyph_h = (uint32_t)((float)dy * scale + 0.5f);
        if (glyph_w == 0u || glyph_h == 0u) continue;

        uint32_t img_w = glyph_w + (uint32_t)(sdf_spread * 2);
        uint32_t img_h = glyph_h + (uint32_t)(sdf_spread * 2);

        /* Parse outline into scratch. */
        rc_array_seg_ segs = parse_glyph_(gd, g_len, scale,
                                           (float)x_min_i, (float)y_max_i,
                                           &scratch);
        if (segs.num == 0u) continue;

        /* Shift segments by the SDF padding so the glyph sits centred in the image. */
        float pad = (float)sdf_spread;
        for (uint32_t s = 0; s < segs.num; s++) {
            segs.data[s].x0 += pad; segs.data[s].y0 += pad;
            segs.data[s].x1 += pad; segs.data[s].y1 += pad;
            segs.data[s].x2 += pad; segs.data[s].y2 += pad;
        }

        glyph_images[gi] = rasterise_sdf_(segs, img_w, img_h,
                                           (float)sdf_spread, &scratch);

        /* Metrics: position of the SDF quad relative to cursor / baseline. */
        result.atlas.glyphs[gi].offset_x = (int32_t)((float)x_min_i * scale + 0.5f) - sdf_spread;
        result.atlas.glyphs[gi].offset_y = (int32_t)((float)y_max_i * scale + 0.5f) + sdf_spread;
    }

    /* ---- pack non-empty glyph SDF images into atlas ---- */

    rc_image packed_images[RC_FONT_GLYPH_COUNT];
    int      gi_for_packed[RC_FONT_GLYPH_COUNT];
    int      n_packed = 0;
    for (int gi = 0; gi < RC_FONT_GLYPH_COUNT; gi++) {
        if (glyph_images[gi].data.data) {
            packed_images[n_packed] = glyph_images[gi];
            gi_for_packed[n_packed] = gi;
            n_packed++;
        }
    }

    if (n_packed == 0) {
        result.error = RC_FONT_ERROR_INVALID;
        return result;
    }

    rc_view_image src_view = {packed_images, (uint32_t)n_packed};

    /* Auto-size: sum glyph areas with half the packing spacing per edge,
     * take the square root, round up to the next power-of-two square.
     * If packing fails, alternate doubling width then height until it fits. */
    static const int32_t pack_spacing = 4;
    uint64_t total_area = 0;
    for (int k = 0; k < n_packed; k++) {
        uint64_t w = (uint64_t)(packed_images[k].size.x + pack_spacing);
        uint64_t h = (uint64_t)(packed_images[k].size.y + pack_spacing);
        total_area += w * h;
    }
    uint32_t side = next_pow2_((uint32_t)sqrtf((float)total_area) + 1u);
    rc_vec2i atlas_size = {(int32_t)side, (int32_t)side};

    rc_image_pack_result pack = rc_image_pack(src_view, atlas_size, pack_spacing, arena, scratch);
    while (!pack.image.data.data) {
        if (atlas_size.x <= atlas_size.y)
            atlas_size.x *= 2;
        else
            atlas_size.y *= 2;
        if (atlas_size.x > 16384 || atlas_size.y > 16384) {
            result.error = RC_FONT_ERROR_INVALID;
            return result;
        }
        pack = rc_image_pack(src_view, atlas_size, pack_spacing, arena, scratch);
    }

    result.atlas.atlas = pack.image;
    for (int k = 0; k < n_packed; k++) {
        int gi = gi_for_packed[k];
        result.atlas.glyphs[gi].atlas_rect = pack.placements.data[k];
    }

    result.error = RC_FONT_OK;
    return result;
}
