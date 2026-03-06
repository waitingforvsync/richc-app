/*
 * buffer_gl33.c - OpenGL 3.3 implementation of buffer.h.
 */

#include "richc/gfx/buffer.h"
#include "richc/debug.h"
#include <glad/gl.h>
#include <stdint.h>

/* ---- internal helpers ---- */

static GLenum to_gl_usage_(rc_buffer_usage usage)
{
    switch (usage) {
        case RC_BUFFER_STATIC:  return GL_STATIC_DRAW;
        case RC_BUFFER_DYNAMIC: return GL_DYNAMIC_DRAW;
        default: RC_PANIC(0); return 0; /* unreachable */
    }
}

static GLenum to_gl_type_(rc_attrib_type type)
{
    switch (type) {
        case RC_ATTRIB_FLOAT: return GL_FLOAT;
        default: RC_PANIC(0); return 0; /* unreachable */
    }
}

/* ---- buffer ---- */

/*
 * We store the usage hint so rc_buffer_upload can pass it to glBufferData.
 * A small lookup table keyed on buffer id is one option; here we simply
 * re-derive it by storing it in a parallel table.  For the number of buffers
 * typical in a small app, a linear scan over a fixed array is fine.
 *
 * Simpler: tag the high byte of the GL name — GL names are implementation-
 * defined but in practice small integers; packing usage into user data is
 * fragile.  Instead, keep a flat table of (id, usage) pairs.
 */
#define MAX_BUFFERS_ 256u

static struct {
    GLuint  id;
    GLenum  usage;
} buf_table_[MAX_BUFFERS_];

static uint32_t buf_count_ = 0;

rc_buffer rc_buffer_make(rc_buffer_usage usage)
{
    RC_PANIC(buf_count_ < MAX_BUFFERS_);

    GLuint id = 0;
    glGenBuffers(1, &id);

    buf_table_[buf_count_].id    = id;
    buf_table_[buf_count_].usage = to_gl_usage_(usage);
    buf_count_++;

    return (rc_buffer) { id };
}

void rc_buffer_upload(rc_buffer buf, const void *data, uint32_t size)
{
    GLenum usage = GL_STATIC_DRAW;
    for (uint32_t i = 0; i < buf_count_; i++) {
        if (buf_table_[i].id == buf.id) {
            usage = buf_table_[i].usage;
            break;
        }
    }
    glBindBuffer(GL_ARRAY_BUFFER, buf.id);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)size, data, usage);
}

void rc_buffer_update(rc_buffer buf, const void *data, uint32_t size)
{
    glBindBuffer(GL_ARRAY_BUFFER, buf.id);
    glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)size, data);
}

void rc_buffer_destroy(rc_buffer buf)
{
    for (uint32_t i = 0; i < buf_count_; i++) {
        if (buf_table_[i].id == buf.id) {
            buf_table_[i] = buf_table_[--buf_count_];
            break;
        }
    }
    glDeleteBuffers(1, &buf.id);
}

/* ---- vertex array ---- */

rc_vertex_array rc_vertex_array_make(const rc_attrib_desc *attribs, uint32_t count)
{
    GLuint id = 0;
    glGenVertexArrays(1, &id);
    glBindVertexArray(id);

    for (uint32_t i = 0; i < count; i++) {
        const rc_attrib_desc *a = &attribs[i];
        glBindBuffer(GL_ARRAY_BUFFER, a->buffer.id);
        glVertexAttribPointer(
            a->location,
            (GLint)a->count,
            to_gl_type_(a->type),
            GL_FALSE,
            (GLsizei)a->stride,
            (const void *)(uintptr_t)a->offset
        );
        glEnableVertexAttribArray(a->location);
        glVertexAttribDivisor(a->location, a->divisor);
    }

    glBindVertexArray(0);
    return (rc_vertex_array) { id };
}

void rc_vertex_array_bind(rc_vertex_array va)
{
    glBindVertexArray(va.id);
}

void rc_vertex_array_destroy(rc_vertex_array va)
{
    glDeleteVertexArrays(1, &va.id);
}

