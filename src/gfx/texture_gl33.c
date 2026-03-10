/*
 * texture_gl33.c - OpenGL 3.3 implementation of texture.h.
 *
 * Flat table pattern matching buffer_gl33.c and pipeline_gl33.c.
 * IDs are auto-incremented from 1; swap-remove on destroy.
 */

#include "richc/gfx/texture.h"
#include "texture_gl33_internal.h"
#include "richc/debug.h"
#include <glad/gl.h>
#include <stdint.h>

/* ---- table ---- */

#define MAX_TEXTURES_ 256u

typedef struct {
    uint32_t          id;
    GLuint            gl_tex;
    rc_vec2i          size;
    rc_texture_format format;
    bool              gen_mipmaps;
} tex_entry_;

static tex_entry_ tex_table_[MAX_TEXTURES_];
static uint32_t   tex_count_   = 0;
static uint32_t   tex_next_id_ = 1;   /* 0 is the invalid sentinel */

static tex_entry_ *tex_find_(uint32_t id)
{
    for (uint32_t i = 0; i < tex_count_; i++) {
        if (tex_table_[i].id == id)
            return &tex_table_[i];
    }
    return NULL;
}

/* ---- format helpers ---- */

static GLenum wrap_gl_(rc_texture_wrap w)
{
    switch (w) {
        case RC_TEXTURE_WRAP_REPEAT: return GL_REPEAT;
        case RC_TEXTURE_WRAP_CLAMP:  return GL_CLAMP_TO_EDGE;
        case RC_TEXTURE_WRAP_MIRROR: return GL_MIRRORED_REPEAT;
        default: RC_PANIC(0); return 0;
    }
}

/* mag filter: no mipmap level involved */
static GLint mag_filter_gl_(rc_texture_filter f)
{
    switch (f) {
        case RC_TEXTURE_FILTER_NEAREST: return GL_NEAREST;
        case RC_TEXTURE_FILTER_LINEAR:  return GL_LINEAR;
        default: RC_PANIC(0); return 0;
    }
}

/* min filter: selects mipmap variant when gen_mipmaps is true */
static GLint min_filter_gl_(rc_texture_filter f, bool mipmaps)
{
    if (!mipmaps) {
        return mag_filter_gl_(f);
    }
    switch (f) {
        case RC_TEXTURE_FILTER_NEAREST: return GL_NEAREST_MIPMAP_NEAREST;
        case RC_TEXTURE_FILTER_LINEAR:  return GL_LINEAR_MIPMAP_LINEAR;
        default: RC_PANIC(0); return 0;
    }
}

static void format_gl_(rc_texture_format fmt,
                        GLint *internal_fmt, GLenum *base_fmt)
{
    switch (fmt) {
        case RC_TEXTURE_FORMAT_R8:
            *internal_fmt = GL_R8;
            *base_fmt     = GL_RED;
            return;
        case RC_TEXTURE_FORMAT_RGB8:
            *internal_fmt = GL_RGB8;
            *base_fmt     = GL_RGB;
            return;
        case RC_TEXTURE_FORMAT_RGBA8:
            *internal_fmt = GL_RGBA8;
            *base_fmt     = GL_RGBA;
            return;
        default: RC_PANIC(0);
    }
}

/* ---- public API ---- */

rc_texture rc_texture_make(const rc_texture_desc *desc)
{
    RC_PANIC(tex_count_ < MAX_TEXTURES_);

    GLuint gl_tex = 0;
    glGenTextures(1, &gl_tex);
    glBindTexture(GL_TEXTURE_2D, gl_tex);

    /* Sampler state */
    GLenum wrap = wrap_gl_(desc->wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, (GLint)wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, (GLint)wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, mag_filter_gl_(desc->filter));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    min_filter_gl_(desc->filter, desc->gen_mipmaps));

    /* Upload pixel data */
    GLint  internal_fmt;
    GLenum base_fmt;
    format_gl_(desc->format, &internal_fmt, &base_fmt);

    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        internal_fmt,
        (GLsizei)desc->size.x,
        (GLsizei)desc->size.y,
        0,
        base_fmt,
        GL_UNSIGNED_BYTE,
        desc->data
    );

    if (desc->gen_mipmaps && desc->data != NULL)
        glGenerateMipmap(GL_TEXTURE_2D);

    /* R8 textures sample as (r,0,0,1) by default; swizzle G and B to RED so
     * the existing RGBA shader treats them as greyscale (r,r,r,1). */
    if (desc->format == RC_TEXTURE_FORMAT_R8) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_G, GL_RED);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, GL_RED);
    }

    uint32_t id = tex_next_id_++;
    tex_table_[tex_count_] = (tex_entry_) {
        .id          = id,
        .gl_tex      = gl_tex,
        .size        = desc->size,
        .format      = desc->format,
        .gen_mipmaps = desc->gen_mipmaps,
    };
    tex_count_++;

    return (rc_texture) { id };
}

void rc_texture_update(rc_texture tex, const void *data)
{
    tex_entry_ *e = tex_find_(tex.id);
    RC_PANIC(e);

    GLint  internal_fmt;
    GLenum base_fmt;
    format_gl_(e->format, &internal_fmt, &base_fmt);

    glBindTexture(GL_TEXTURE_2D, e->gl_tex);
    glTexSubImage2D(
        GL_TEXTURE_2D,
        0,
        0, 0,
        (GLsizei)e->size.x,
        (GLsizei)e->size.y,
        base_fmt,
        GL_UNSIGNED_BYTE,
        data
    );

    if (e->gen_mipmaps)
        glGenerateMipmap(GL_TEXTURE_2D);
}

void rc_texture_destroy(rc_texture tex)
{
    for (uint32_t i = 0; i < tex_count_; i++) {
        if (tex_table_[i].id == tex.id) {
            GLuint gl_tex = tex_table_[i].gl_tex;
            tex_table_[i] = tex_table_[--tex_count_];
            glDeleteTextures(1, &gl_tex);
            return;
        }
    }
}

/* ---- internal helper ---- */

GLuint rc_texture_gl_(rc_texture tex)
{
    if (tex.id == 0) return 0;
    tex_entry_ *e = tex_find_(tex.id);
    return e ? e->gl_tex : 0;
}
