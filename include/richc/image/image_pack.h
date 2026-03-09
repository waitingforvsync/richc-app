/*
 * image_pack.h - bin packing: pack multiple images into a single atlas.
 *
 * Algorithm
 * ---------
 * Maximal Rectangles with Best Short Side Fit (BSSF).  The free-rect list
 * is split around each placed image (inflated by spacing), and any free rect
 * fully contained within another is pruned.  No rotation is attempted.
 *
 * Output format
 * -------------
 * The atlas format is the widest format among the inputs:
 *   all R8          → R8
 *   any RGB8, no RGBA8  → RGB8
 *   any RGBA8       → RGBA8
 * rc_image_blit's expanding conversions handle the up-casting automatically.
 *
 * Spacing
 * -------
 * Each placed image's region is inflated by spacing in all directions when
 * carving the free-rect list.  This maintains a gap of at least spacing pixels
 * between adjacent images.  No border gap is enforced at the bin edges.
 *
 * Failure
 * -------
 * Returns a zero-initialised result if any image cannot be placed.  On
 * failure nothing is written to arena (all temporary work is in scratch).
 *
 * Usage
 * -----
 *   rc_image sources[N] = { ... };
 *   rc_view_image view  = {sources, N};
 *
 *   rc_arena arena   = rc_arena_make_default();
 *   rc_arena scratch = rc_arena_make_default();
 *   rc_image_pack_result r = rc_image_pack(view, rc_vec2i_make(512, 512),
 *                                           4, &arena, scratch);
 *   rc_arena_destroy(&scratch);
 *   if (!r.image.data.data) { ... }   // failure: retry with larger size
 *
 *   // r.placements.data[i] is the rc_box2i for sources[i] in the atlas.
 *   // r.image can be uploaded directly to rc_texture_make.
 */

#ifndef RC_IMAGE_IMAGE_PACK_H_
#define RC_IMAGE_IMAGE_PACK_H_

#include "richc/image/array_image.h"
#include "richc/math/array_box2i.h"
#include "richc/arena.h"

/* ---- result ---- */

/*
 * On success: image holds the packed atlas (allocated from arena);
 *             placements[i] is the bounding box of images[i] in the atlas
 *             (also allocated from arena).
 * On failure: zero-initialised — image.data.data == NULL, placements.num == 0.
 */
typedef struct {
    rc_image      image;
    rc_span_box2i placements;
} rc_image_pack_result;

/* ---- API ---- */

/*
 * Pack all images into an atlas of the given size.
 *
 * images   — input images; must all have data.data != NULL.
 * size     — atlas dimensions in pixels.
 * spacing  — minimum pixel gap maintained between packed images.
 * arena    — receives atlas pixel data and placements array on success.
 * scratch  — temporary free-rect list and working placements (discarded).
 *
 * Returns zero-initialised on failure; nothing written to arena in that case.
 */
rc_image_pack_result rc_image_pack(rc_view_image images,
                                    rc_vec2i      size,
                                    int32_t       spacing,
                                    rc_arena     *arena,
                                    rc_arena      scratch);

#endif /* RC_IMAGE_IMAGE_PACK_H_ */
