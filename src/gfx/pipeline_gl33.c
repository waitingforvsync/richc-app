/*
 * pipeline_gl33.c - OpenGL 3.3 implementation of pipeline.h.
 *
 * Uses a single global VAO (created lazily).  Attribute pointers are
 * re-applied in rc_gfx_apply_bindings every frame, so no per-pipeline
 * VAO is needed — matching the conceptual model of D3D/Metal/Vulkan.
 *
 * Pipeline state (shader program, blend, layout) is stored in a flat
 * table keyed by an auto-incremented id.  The current pipeline's id is
 * remembered to avoid dangling-pointer issues on swap-remove.
 */

#include "richc/gfx/pipeline.h"
#include "texture_gl33_internal.h"
#include "richc/debug.h"
#include <glad/gl.h>
#include <string.h>

/* ---- singleton VAO ---- */

static GLuint   vao_             = 0;
static uint32_t enabled_attribs_ = 0;   /* bitmask: bit N set ↔ location N enabled */

static void ensure_vao_(void)
{
    if (!vao_)
        glGenVertexArrays(1, &vao_);
    glBindVertexArray(vao_);
}

/* ---- attrib format helpers ---- */

static void attrib_format_gl_(rc_attrib_format fmt, GLint *count, GLenum *type)
{
    *type = GL_FLOAT;
    switch (fmt) {
        case RC_ATTRIB_FORMAT_FLOAT:  *count = 1; return;
        case RC_ATTRIB_FORMAT_FLOAT2: *count = 2; return;
        case RC_ATTRIB_FORMAT_FLOAT3: *count = 3; return;
        case RC_ATTRIB_FORMAT_FLOAT4: *count = 4; return;
        default: RC_PANIC(0);
    }
}

static GLenum index_type_gl_(rc_index_type t)
{
    switch (t) {
        case RC_INDEX_TYPE_UINT16: return GL_UNSIGNED_SHORT;
        case RC_INDEX_TYPE_UINT32: return GL_UNSIGNED_INT;
        default: RC_PANIC(0); return 0;
    }
}

/* ---- pipeline table ---- */

#define MAX_PIPELINES_ 64u

typedef struct {
    uint32_t         id;
    rc_pipeline_desc desc;
} pip_entry_;

static pip_entry_ pip_table_[MAX_PIPELINES_];
static uint32_t   pip_count_ = 0;
static uint32_t   pip_next_id_ = 1;   /* 0 is the invalid sentinel */

/* Currently applied pipeline id; 0 = none. */
static uint32_t   current_pip_id_ = 0;

static pip_entry_ *pip_find_(uint32_t id)
{
    for (uint32_t i = 0; i < pip_count_; i++) {
        if (pip_table_[i].id == id)
            return &pip_table_[i];
    }
    return NULL;
}

/* ---- public API ---- */

rc_pipeline rc_pipeline_make(const rc_pipeline_desc *desc)
{
    RC_PANIC(pip_count_ < MAX_PIPELINES_);
    uint32_t id = pip_next_id_++;
    pip_table_[pip_count_].id   = id;
    pip_table_[pip_count_].desc = *desc;
    pip_count_++;
    return (rc_pipeline) { id };
}

void rc_pipeline_destroy(rc_pipeline pip)
{
    for (uint32_t i = 0; i < pip_count_; i++) {
        if (pip_table_[i].id == pip.id) {
            pip_table_[i] = pip_table_[--pip_count_];
            if (current_pip_id_ == pip.id)
                current_pip_id_ = 0;
            return;
        }
    }
}

void rc_gfx_apply_pipeline(rc_pipeline pip)
{
    pip_entry_ *e = pip_find_(pip.id);
    RC_PANIC(e);
    current_pip_id_ = pip.id;

    /* Shader */
    glUseProgram(e->desc.shader.id);

    /* Blend state */
    if (e->desc.blend.enabled) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    } else {
        glDisable(GL_BLEND);
    }
}

void rc_gfx_apply_bindings(const rc_bindings *bind)
{
    pip_entry_ *e = pip_find_(current_pip_id_);
    RC_PANIC(e);

    ensure_vao_();

    /* Bind index buffer if the pipeline uses one. */
    if (e->desc.index_type != RC_INDEX_TYPE_NONE && bind->index_buffer.id != 0)
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, bind->index_buffer.id);
    else
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    /* Compute which attribute locations the new pipeline uses. */
    uint32_t new_attribs = 0;
    for (uint32_t i = 0; i < RC_MAX_VERTEX_ATTRIBS; i++) {
        const rc_attrib_layout *a = &e->desc.attribs[i];
        if (a->format == 0) break;   /* sentinel */
        new_attribs |= (1u << a->location);
    }

    /* Disable any locations enabled by the previous pipeline but not the new one. */
    uint32_t to_disable = enabled_attribs_ & ~new_attribs;
    for (uint32_t loc = 0; to_disable; loc++, to_disable >>= 1) {
        if (to_disable & 1)
            glDisableVertexAttribArray(loc);
    }
    enabled_attribs_ = new_attribs;

    /* Enable and configure each attribute. */
    for (uint32_t i = 0; i < RC_MAX_VERTEX_ATTRIBS; i++) {
        const rc_attrib_layout *a = &e->desc.attribs[i];
        if (a->format == 0) break;   /* sentinel */

        const rc_buffer_layout *bl = &e->desc.buffer_layouts[a->buffer_slot];
        uint32_t buf_id = bind->vertex_buffers[a->buffer_slot].id;

        GLint  count;
        GLenum type;
        attrib_format_gl_(a->format, &count, &type);

        glBindBuffer(GL_ARRAY_BUFFER, buf_id);
        glVertexAttribPointer(
            a->location,
            count,
            type,
            GL_FALSE,
            (GLsizei)bl->stride,
            (const void *)(uintptr_t)a->offset
        );
        glEnableVertexAttribArray(a->location);
        glVertexAttribDivisor(a->location, bl->divisor);
    }

    /* Bind textures to their assigned texture units. */
    for (int i = 0; i < RC_MAX_TEXTURE_SLOTS; i++) {
        if (bind->textures[i].id != 0) {
            glActiveTexture(GL_TEXTURE0 + i);
            glBindTexture(GL_TEXTURE_2D, rc_texture_gl_(bind->textures[i]));
        }
    }
}

void rc_gfx_draw(uint32_t first, uint32_t count, uint32_t instances)
{
    pip_entry_ *e = pip_find_(current_pip_id_);
    RC_PANIC(e);

    if (e->desc.index_type == RC_INDEX_TYPE_NONE) {
        glDrawArraysInstanced(GL_TRIANGLES, (GLint)first, (GLsizei)count, (GLsizei)instances);
    } else {
        GLenum idx_type = index_type_gl_(e->desc.index_type);
        uint32_t idx_size = (e->desc.index_type == RC_INDEX_TYPE_UINT16) ? 2u : 4u;
        glDrawElementsInstanced(
            GL_TRIANGLES,
            (GLsizei)count,
            idx_type,
            (const void *)(uintptr_t)(first * idx_size),
            (GLsizei)instances
        );
    }
}
