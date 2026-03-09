/*
 * image_pack.c - Maximal Rectangles bin packing, Best Short Side Fit.
 *
 * All temporary state (free-rect list, working placements) lives in scratch.
 * On success the atlas pixels and final placements are written to arena.
 * On failure nothing is written to arena.
 */

#include "richc/image/image_pack.h"
#include <limits.h>

/* ---- index array + indirect sort by decreasing area ---- */

#define ARRAY_T uint32_t
#include "richc/template/array.h"

#define SORT_T   uint32_t
#define SORT_CTX rc_view_image
#define SORT_CMP(ctx, a, b) \
    ((ctx->data[a].size.x > ctx->data[a].size.y ? ctx->data[a].size.x : ctx->data[a].size.y) > \
     (ctx->data[b].size.x > ctx->data[b].size.y ? ctx->data[b].size.x : ctx->data[b].size.y))
#include "richc/template/sort.h"

/* ---- free-rect splitting ---- */

/*
 * Remove every free rect that intersects the inflated placed region, split
 * each one into up to four axis-aligned strips around the inflated region,
 * and append any strip not already contained within an existing free rect.
 */
static void split_free_rects_(rc_array_box2i *free, rc_box2i placed,
                               int32_t spacing, rc_arena *scratch)
{
    rc_box2i infl = rc_box2i_make_with_margin(placed.min, placed.max, spacing);

    /*
     * Swap-remove pass: iterate while i < free->num.
     * - Non-intersecting rects: i++.
     * - Intersecting rects: swap-remove (swap with last, free->num--), then
     *   append any valid splits not already contained by an existing free rect.
     *   Do not increment i so the swapped-in element is checked next.
     *
     * Splits lie strictly outside infl by construction, so they never trigger
     * the intersect branch — they are simply skipped with i++ when reached.
     * Using free->num (not a fixed orig_count) as the bound ensures we stop
     * correctly if all rects are removed and no splits are added.
     */
    uint32_t i = 0;
    while (i < free->num) {
        rc_box2i f = free->data[i];

        if (!rc_box2i_intersects(f, infl)) {
            i++;
            continue;
        }

        /* Swap-remove: replace slot i with the last element. */
        free->data[i] = rc_array_box2i_pop(free);

        /* Generate up to four strips (one per side of infl inside f). */
        rc_box2i splits[4];
        uint32_t sc = 0;

        if (infl.min.x > f.min.x)
            splits[sc++] = (rc_box2i) {.min = f.min,
                                        .max = rc_vec2i_make(infl.min.x, f.max.y)};
        if (infl.max.x < f.max.x)
            splits[sc++] = (rc_box2i) {.min = rc_vec2i_make(infl.max.x, f.min.y),
                                        .max = f.max};
        if (infl.min.y > f.min.y)
            splits[sc++] = (rc_box2i) {.min = f.min,
                                        .max = rc_vec2i_make(f.max.x, infl.min.y)};
        if (infl.max.y < f.max.y)
            splits[sc++] = (rc_box2i) {.min = rc_vec2i_make(f.min.x, infl.max.y),
                                        .max = f.max};

        /* Append each split not already contained by any current free rect. */
        for (uint32_t j = 0; j < sc; j++) {
            bool dominated = false;
            for (uint32_t k = 0; k < free->num && !dominated; k++)
                dominated = rc_box2i_contains(free->data[k], splits[j]);
            if (!dominated)
                rc_array_box2i_push(free, splits[j], scratch);
        }
    }
}

/* ---- public API ---- */

rc_image_pack_result rc_image_pack(rc_view_image images,
                                    rc_vec2i      size,
                                    int32_t       spacing,
                                    rc_arena     *arena,
                                    rc_arena      scratch)
{
    if (images.num == 0)
        return (rc_image_pack_result) {0};

    /* Widest format across all inputs becomes the atlas format. */
    rc_pixel_format fmt = RC_PIXEL_FORMAT_R8;
    for (uint32_t i = 0; i < images.num; i++) {
        if (images.data[i].format > fmt)
            fmt = images.data[i].format;
    }

    /* All working state goes in scratch — nothing touches arena until success.
     * The free-rect list is allocated last so it can grow without copying. */
    rc_array_box2i place = rc_array_box2i_make(images.num, &scratch);
    rc_array_box2i_resize(&place, images.num, &scratch);

    /* Sort images by decreasing max(width, height): larger images placed first
     * produces denser packings by leaving less awkward leftover space. */
    rc_array_uint32_t order = rc_array_uint32_t_make(images.num, &scratch);
    rc_array_uint32_t_resize(&order, images.num, &scratch);
    for (uint32_t i = 0; i < images.num; i++)
        order.data[i] = i;
    rc_sort_uint32_t(order.span, &images);

    rc_array_box2i free = rc_array_box2i_make(16, &scratch);
    rc_array_box2i_push(&free,
        rc_box2i_make_pos_size(rc_vec2i_make_zero(), size), &scratch);

    for (uint32_t i = 0; i < images.num; i++) {
        uint32_t idx        = order.data[i];
        rc_vec2i img_size   = images.data[idx].size;
        int32_t  best_score = INT32_MAX;
        int32_t  best_idx   = -1;
        rc_box2i best_place = {0};

        for (uint32_t j = 0; j < free.num; j++) {
            rc_vec2i fs = rc_box2i_size(free.data[j]);
            if (img_size.x <= fs.x && img_size.y <= fs.y) {
                int32_t lx    = fs.x - img_size.x;
                int32_t ly    = fs.y - img_size.y;
                int32_t score = lx < ly ? lx : ly;
                if (score < best_score) {
                    best_score = score;
                    best_idx   = (int32_t)j;
                    best_place = rc_box2i_make_pos_size(free.data[j].min, img_size);
                }
            }
        }

        if (best_idx < 0)
            return (rc_image_pack_result) {0};   /* image does not fit */

        place.data[idx] = best_place;
        split_free_rects_(&free, best_place, spacing, &scratch);
    }

    /* Success: write atlas and placements to arena. */
    rc_image atlas = rc_image_make(size, fmt, NULL, arena);
    for (uint32_t i = 0; i < images.num; i++)
        rc_image_blit(atlas, place.data[i].min, images.data[i]);

    rc_array_box2i result = {0};
    rc_array_box2i_push_n(&result, place.data, images.num, arena);

    return (rc_image_pack_result) {
        .image      = atlas,
        .placements = result.span,
    };
}
