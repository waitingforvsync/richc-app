/*
 * pipeline.h - render pipeline and draw call API.
 *
 * A pipeline bakes together a shader, vertex buffer layout, and blend state.
 * Buffers are supplied separately at draw time via rc_bindings.
 *
 * Typical usage
 * -------------
 *   // create once
 *   rc_pipeline pip = rc_pipeline_make(&(rc_pipeline_desc) {
 *       .shader = sh,
 *       .buffer_layouts = {
 *           [0] = { .stride = sizeof(rc_vec2f), .divisor = 0 },
 *           [1] = { .stride = sizeof(MyInst),   .divisor = 1 },
 *       },
 *       .attribs = {
 *           { .location = 0, .buffer_slot = 0, .format = RC_ATTRIB_FORMAT_FLOAT2, .offset = 0 },
 *           { .location = 1, .buffer_slot = 1, .format = RC_ATTRIB_FORMAT_FLOAT2, .offset = offsetof(MyInst, pos) },
 *       },
 *       .blend = { .enabled = true },
 *   });
 *
 *   // each frame
 *   rc_gfx_apply_pipeline(pip);
 *   rc_gfx_apply_bindings(&(rc_bindings) {
 *       .vertex_buffers = { quad_buf, inst_buf },
 *   });
 *   rc_gfx_draw(0, 6, 5);
 */

#ifndef RC_GFX_PIPELINE_H_
#define RC_GFX_PIPELINE_H_

#include <stdbool.h>
#include <stdint.h>
#include "richc/gfx/buffer.h"
#include "richc/gfx/shader.h"
#include "richc/gfx/texture.h"

/* ---- vertex attribute format ---- */

/*
 * Combined type-and-count token for a vertex attribute.
 * Non-zero values allow format == 0 to act as a sentinel (end of attrib list).
 */
typedef enum {
    RC_ATTRIB_FORMAT_FLOAT  = 1,   /* 1 x float */
    RC_ATTRIB_FORMAT_FLOAT2 = 2,   /* 2 x float */
    RC_ATTRIB_FORMAT_FLOAT3 = 3,   /* 3 x float */
    RC_ATTRIB_FORMAT_FLOAT4 = 4,   /* 4 x float */
} rc_attrib_format;

/* ---- index type ---- */

typedef enum {
    RC_INDEX_TYPE_NONE   = 0,   /* no index buffer; use rc_gfx_draw with vertex count */
    RC_INDEX_TYPE_UINT16 = 1,   /* 16-bit indices */
    RC_INDEX_TYPE_UINT32 = 2,   /* 32-bit indices */
} rc_index_type;

/* ---- pipeline descriptor ---- */

/* Per-buffer-slot layout: stride and instancing divisor. */
typedef struct {
    uint32_t stride;    /* byte stride between consecutive elements */
    uint32_t divisor;   /* 0 = per-vertex, 1 = per-instance */
} rc_buffer_layout;

/* Per-attribute layout: where to find it and what format it is. */
typedef struct {
    uint32_t         location;     /* shader layout(location = N) */
    uint32_t         buffer_slot;  /* index into rc_bindings.vertex_buffers */
    rc_attrib_format format;       /* 0 = end of attrib list (sentinel) */
    uint32_t         offset;       /* byte offset within the buffer stride */
} rc_attrib_layout;

/* Blend state.  When enabled, uses SRC_ALPHA / ONE_MINUS_SRC_ALPHA. */
typedef struct {
    bool enabled;
} rc_blend_state;

#define RC_MAX_VERTEX_BUFFERS  4
#define RC_MAX_VERTEX_ATTRIBS 16
#define RC_MAX_TEXTURE_SLOTS   8

typedef struct {
    rc_shader        shader;
    rc_buffer_layout buffer_layouts[RC_MAX_VERTEX_BUFFERS];
    rc_attrib_layout attribs[RC_MAX_VERTEX_ATTRIBS]; /* terminated by format == 0 */
    rc_index_type    index_type;
    rc_blend_state   blend;
} rc_pipeline_desc;

/* Opaque handle to a compiled pipeline.  id == 0 is invalid. */
typedef struct { uint32_t id; } rc_pipeline;

/* ---- bindings ---- */

typedef struct {
    rc_buffer  vertex_buffers[RC_MAX_VERTEX_BUFFERS];
    rc_buffer  index_buffer;                         /* {0} = none */
    rc_texture textures[RC_MAX_TEXTURE_SLOTS];       /* {0} = unbound */
} rc_bindings;

/* ---- pipeline API ---- */

/* Create a pipeline from a descriptor.  RC_PANICs on failure. */
rc_pipeline rc_pipeline_make   (const rc_pipeline_desc *desc);

/* Destroy a pipeline.  pip must not be used after this call. */
void        rc_pipeline_destroy(rc_pipeline pip);

/* ---- draw API ---- */

/*
 * Apply a pipeline: binds its shader, sets blend state.  Must be called
 * before rc_gfx_apply_bindings and rc_gfx_draw each frame.
 */
void rc_gfx_apply_pipeline(rc_pipeline pip);

/*
 * Apply buffer bindings for the current pipeline.  Configures vertex
 * attribute pointers from the pipeline's layout and the supplied buffers.
 * Must be called after rc_gfx_apply_pipeline.
 */
void rc_gfx_apply_bindings(const rc_bindings *bind);

/*
 * Submit a draw call.  Draws count vertices (or indices) starting at first,
 * repeated instances times.  Use instances == 1 for non-instanced draws.
 * Uses glDrawArraysInstanced or glDrawElementsInstanced depending on the
 * current pipeline's index_type.
 */
void rc_gfx_draw(uint32_t first, uint32_t count, uint32_t instances);

#endif /* RC_GFX_PIPELINE_H_ */
