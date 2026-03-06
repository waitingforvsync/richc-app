/*
 * gfx_gl33.c - OpenGL 3.3 implementation of gfx.h helpers.
 *
 * The OpenGL context is created by app_glfw.c; glad is initialised there
 * before any call to these functions can occur.
 */

#include "richc/gfx/gfx.h"
#include "richc/debug.h"
#include <glad/gl.h>

void rc_gfx_viewport(rc_vec2i size)
{
    glViewport(0, 0, size.x, size.y);
}

void rc_gfx_clear(rc_color color)
{
    glClearColor(color.r, color.g, color.b, color.a);
    glClear(GL_COLOR_BUFFER_BIT);
}

void rc_gfx_clear_depth(void)
{
    glClear(GL_DEPTH_BUFFER_BIT);
}

/* ---- draw calls ---- */

static GLenum to_gl_prim_(rc_primitive prim)
{
    switch (prim) {
        case RC_PRIMITIVE_TRIANGLES:      return GL_TRIANGLES;
        case RC_PRIMITIVE_TRIANGLE_STRIP: return GL_TRIANGLE_STRIP;
        default: RC_PANIC(0); return 0; /* unreachable */
    }
}

void rc_gfx_draw_arrays(rc_primitive prim, uint32_t first, uint32_t count)
{
    glDrawArrays(to_gl_prim_(prim), (GLint)first, (GLsizei)count);
}

void rc_gfx_draw_arrays_instanced(rc_primitive prim, uint32_t first,
                                   uint32_t count, uint32_t instances)
{
    glDrawArraysInstanced(to_gl_prim_(prim), (GLint)first, (GLsizei)count,
                          (GLsizei)instances);
}

/* ---- blend state ---- */

void rc_gfx_blend_enable(void)
{
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void rc_gfx_blend_disable(void)
{
    glDisable(GL_BLEND);
}
