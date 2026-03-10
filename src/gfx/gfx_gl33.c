/*
 * gfx_gl33.c - OpenGL 3.3 implementation of gfx.h helpers.
 *
 * The OpenGL context is created by app_glfw.c; glad is initialised there
 * before any call to these functions can occur.
 */

#include "richc/gfx/gfx.h"
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

void rc_gfx_set_scissor(rc_box2i rect)
{
    glEnable(GL_SCISSOR_TEST);
    glScissor(rect.min.x, rect.min.y,
              (GLsizei)(rect.max.x - rect.min.x),
              (GLsizei)(rect.max.y - rect.min.y));
}

void rc_gfx_clear_scissor(void)
{
    glDisable(GL_SCISSOR_TEST);
}

