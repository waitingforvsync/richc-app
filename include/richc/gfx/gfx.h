/*
 * gfx.h - minimal OpenGL 3.3 graphics helpers.
 *
 * Provides a linear RGBA colour type and basic per-frame operations.
 * No OpenGL or GLFW headers are included here; call sites only see richc types.
 *
 * The OpenGL context is created and owned by the app backend (app_glfw.c).
 * These functions operate on the current context and are safe to call after
 * the rc_app handle has been successfully created.
 *
 * Types
 * -----
 *   rc_color  — linear RGBA float colour; each component in [0, 1].
 *
 * Frame helpers
 * -------------
 *   rc_gfx_viewport(size)       — set the GL viewport to (0, 0, w, h).
 *   rc_gfx_clear(color)         — clear the colour buffer.
 *   rc_gfx_clear_depth()        — clear the depth buffer.
 */

#ifndef RC_APP_GFX_H_
#define RC_APP_GFX_H_

#include <stdint.h>
#include "richc/math/vec2i.h"

/* ---- colour type ---- */

typedef struct {
    float r, g, b, a;
} rc_color;

static inline rc_color rc_color_make(float r, float g, float b, float a)
{
    return (rc_color) {r, g, b, a};
}

static inline rc_color rc_color_make_rgb(float r, float g, float b)
{
    return (rc_color) {r, g, b, 1.0f};
}

static inline rc_color rc_color_from_rgba8(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    return (rc_color) {
        (float)r / 255.0f,
        (float)g / 255.0f,
        (float)b / 255.0f,
        (float)a / 255.0f
    };
}

/* ---- frame helpers ---- */

/* Set the GL viewport to cover (0, 0) .. (size.x, size.y). */
void rc_gfx_viewport(rc_vec2i size);

/* Clear the colour buffer to the given colour. */
void rc_gfx_clear(rc_color color);

/* Clear the depth buffer. */
void rc_gfx_clear_depth(void);

/* ---- draw calls ---- */

/* Draw count vertices (triangles) starting at first. */
void rc_gfx_draw_arrays(uint32_t first, uint32_t count);

/* Draw count vertices (triangles), repeated instances times. */
void rc_gfx_draw_arrays_instanced(uint32_t first, uint32_t count, uint32_t instances);

/* ---- blend state ---- */

/* Enable alpha blending: src=SRC_ALPHA, dst=ONE_MINUS_SRC_ALPHA. */
void rc_gfx_blend_enable(void);

/* Disable blending. */
void rc_gfx_blend_disable(void);

#endif /* RC_APP_GFX_H_ */
