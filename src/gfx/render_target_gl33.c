/*
 * render_target_gl33.c - GL 3.3 implementation of render_target.h.
 *
 * Flat table of up to MAX_RENDER_TARGETS_ entries; swap-remove on destroy.
 * IDs auto-increment from 1; id == 0 is the invalid sentinel.
 *
 * Each entry owns:
 *   - a GL framebuffer object (gl_fbo)
 *   - up to RC_MAX_COLOR_ATTACHMENTS rc_texture handles (created via
 *     rc_texture_make and destroyed via rc_texture_destroy)
 *   - an optional GL renderbuffer for depth (gl_depth_rbo)
 *
 * Draw-buffer state (glDrawBuffers) is set once at make time on the FBO
 * object; it persists with the FBO and does not need to be re-set in begin.
 *
 * The active-FBO sentinel (s_active_id) guards against nested begins and
 * unmatched ends.
 */

#include "richc/gfx/render_target.h"
#include "richc/app/app.h"
#include "richc/debug.h"
#include "texture_gl33_internal.h"
#include <glad/gl.h>

/* ---- table ---- */

#define MAX_RENDER_TARGETS_ 64

typedef struct {
    uint32_t   id;
    GLuint     gl_fbo;
    GLuint     gl_depth_rbo;                         /* 0 if no depth */
    rc_texture color[RC_MAX_COLOR_ATTACHMENTS];
    uint32_t   num_color;
    rc_vec2i   size;
} rt_entry_;

static rt_entry_ s_rts[MAX_RENDER_TARGETS_];
static uint32_t  s_num_rts;
static uint32_t  s_next_id   = 1;
static uint32_t  s_active_id = 0;   /* 0 = default framebuffer */

static rt_entry_ *find_(uint32_t id)
{
    for (uint32_t i = 0; i < s_num_rts; i++)
        if (s_rts[i].id == id) return &s_rts[i];
    return NULL;
}

/* ---- API ---- */

rc_render_target rc_render_target_make(const rc_render_target_desc *desc)
{
    RC_PANIC(s_num_rts < MAX_RENDER_TARGETS_);

    GLuint gl_fbo;
    glGenFramebuffers(1, &gl_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, gl_fbo);

    rc_texture colors[RC_MAX_COLOR_ATTACHMENTS] = {0};
    GLenum     draw_bufs[RC_MAX_COLOR_ATTACHMENTS];
    uint32_t   num_color = 0;

    for (uint32_t i = 0; i < RC_MAX_COLOR_ATTACHMENTS; i++) {
        if (desc->color[i].format == 0) break;

        colors[i] = rc_texture_make(&(rc_texture_desc) {
            .size   = desc->size,
            .format = desc->color[i].format,
            .filter = desc->color[i].filter,
            .wrap   = desc->color[i].wrap,
            .usage  = RC_TEXTURE_USAGE_STATIC,
            .data   = NULL,
        });

        glFramebufferTexture2D(GL_FRAMEBUFFER,
                               GL_COLOR_ATTACHMENT0 + (GLenum)i,
                               GL_TEXTURE_2D,
                               rc_texture_gl_(colors[i]), 0);
        draw_bufs[num_color] = GL_COLOR_ATTACHMENT0 + (GLenum)i;
        num_color++;
    }

    RC_PANIC(num_color > 0);
    glDrawBuffers((GLsizei)num_color, draw_bufs);

    GLuint gl_depth_rbo = 0;
    if (desc->depth) {
        glGenRenderbuffers(1, &gl_depth_rbo);
        glBindRenderbuffer(GL_RENDERBUFFER, gl_depth_rbo);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24,
                              desc->size.x, desc->size.y);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                  GL_RENDERBUFFER, gl_depth_rbo);
    }

    RC_PANIC(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    rt_entry_ *e    = &s_rts[s_num_rts++];
    e->id           = s_next_id++;
    e->gl_fbo       = gl_fbo;
    e->gl_depth_rbo = gl_depth_rbo;
    e->num_color    = num_color;
    e->size         = desc->size;
    for (uint32_t i = 0; i < num_color; i++)
        e->color[i] = colors[i];

    return (rc_render_target) {e->id};
}

void rc_render_target_destroy(rc_render_target rt)
{
    rt_entry_ *e = find_(rt.id);
    RC_PANIC(e);

    for (uint32_t i = 0; i < e->num_color; i++)
        rc_texture_destroy(e->color[i]);
    if (e->gl_depth_rbo)
        glDeleteRenderbuffers(1, &e->gl_depth_rbo);
    glDeleteFramebuffers(1, &e->gl_fbo);

    *e = s_rts[--s_num_rts];   /* swap-remove */
}

rc_texture rc_render_target_color(rc_render_target rt, uint32_t index)
{
    rt_entry_ *e = find_(rt.id);
    RC_PANIC(e);
    RC_PANIC(index < e->num_color);
    return e->color[index];
}

void rc_gfx_begin_render_target(rc_render_target rt)
{
    RC_PANIC(s_active_id == 0);
    rt_entry_ *e = find_(rt.id);
    RC_PANIC(e);

    s_active_id = e->id;
    glBindFramebuffer(GL_FRAMEBUFFER, e->gl_fbo);
    glViewport(0, 0, e->size.x, e->size.y);
}

void rc_gfx_end_render_target(void)
{
    RC_PANIC(s_active_id != 0);
    s_active_id = 0;

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    rc_vec2i sz = rc_app_size();
    glViewport(0, 0, sz.x, sz.y);
}
