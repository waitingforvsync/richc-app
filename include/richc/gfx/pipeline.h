/*
 * pipeline.h - render pipeline and draw call API.
 *
 * A pipeline bakes together a shader, vertex buffer layouts, attribute
 * formats, index type, depth, blend, and cull state.  Buffers and textures
 * are supplied separately at draw time via rc_bindings.
 *
 * Zero-initialisation is a valid "use defaults" value for all state fields:
 *   depth  — disabled
 *   blend  — disabled; when enabled, defaults to SRC_ALPHA / ONE_MINUS_SRC_ALPHA / ADD
 *   cull   — no culling; front face = CCW
 *
 * Typical usage
 * -------------
 *   rc_pipeline pip = rc_pipeline_make(&(rc_pipeline_desc) {
 *       .shader = sh,
 *       .buffer_layouts = {
 *           [0] = { .stride = sizeof(Vertex), .divisor = 0 },
 *       },
 *       .attribs = {
 *           { .location = 0, .buffer_slot = 0,
 *             .format = RC_ATTRIB_FORMAT_FLOAT2, .offset = 0 },
 *       },
 *       .depth = { .enabled = true,
 *                  .compare = RC_COMPARE_LESS_EQUAL,
 *                  .write_enabled = true },
 *       .blend = { .enabled = true },
 *       .cull  = { .face = RC_CULL_BACK },
 *   });
 *
 *   // each frame
 *   rc_gfx_apply_pipeline(pip);
 *   rc_gfx_apply_bindings(&(rc_bindings) { .vertex_buffers = { vbuf } });
 *   rc_gfx_draw(0, 6, 1);
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
 * Non-zero values allow format == 0 to terminate the attrib list.
 */
typedef enum {
    RC_ATTRIB_FORMAT_FLOAT  = 1,   /* 1 x float */
    RC_ATTRIB_FORMAT_FLOAT2 = 2,   /* 2 x float */
    RC_ATTRIB_FORMAT_FLOAT3 = 3,   /* 3 x float */
    RC_ATTRIB_FORMAT_FLOAT4 = 4,   /* 4 x float */
} rc_attrib_format;

/* ---- index type ---- */

typedef enum {
    RC_INDEX_TYPE_NONE   = 0,   /* no index buffer */
    RC_INDEX_TYPE_UINT16 = 1,   /* 16-bit indices  */
    RC_INDEX_TYPE_UINT32 = 2,   /* 32-bit indices  */
} rc_index_type;

/* ---- compare function ---- */

/*
 * Comparison function for depth and stencil tests.
 * 0 (zero-init default) resolves to RC_COMPARE_LESS_EQUAL.
 */
typedef enum {
    RC_COMPARE_DEFAULT       = 0,   /* resolves to RC_COMPARE_LESS_EQUAL */
    RC_COMPARE_NEVER         = 1,
    RC_COMPARE_LESS          = 2,
    RC_COMPARE_EQUAL         = 3,
    RC_COMPARE_LESS_EQUAL    = 4,
    RC_COMPARE_GREATER       = 5,
    RC_COMPARE_NOT_EQUAL     = 6,
    RC_COMPARE_GREATER_EQUAL = 7,
    RC_COMPARE_ALWAYS        = 8,
} rc_compare_func;

/* ---- blend state ---- */

/*
 * Blend factor.  0 (zero-init default) resolves to:
 *   src_factor_rgb / src_factor_alpha  →  RC_BLEND_FACTOR_SRC_ALPHA
 *   dst_factor_rgb / dst_factor_alpha  →  RC_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA
 *
 * src_factor_alpha and dst_factor_alpha also fall back to the resolved
 * rgb factor when left at 0, so { .enabled = true } gives standard
 * alpha blending for both colour and alpha channels.
 */
typedef enum {
    RC_BLEND_FACTOR_DEFAULT             = 0,   /* src → SRC_ALPHA, dst → ONE_MINUS_SRC_ALPHA */
    RC_BLEND_FACTOR_ZERO                = 1,
    RC_BLEND_FACTOR_ONE                 = 2,
    RC_BLEND_FACTOR_SRC_COLOR           = 3,
    RC_BLEND_FACTOR_ONE_MINUS_SRC_COLOR = 4,
    RC_BLEND_FACTOR_SRC_ALPHA           = 5,
    RC_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA = 6,
    RC_BLEND_FACTOR_DST_COLOR           = 7,
    RC_BLEND_FACTOR_ONE_MINUS_DST_COLOR = 8,
    RC_BLEND_FACTOR_DST_ALPHA           = 9,
    RC_BLEND_FACTOR_ONE_MINUS_DST_ALPHA = 10,
    RC_BLEND_FACTOR_SRC_ALPHA_SATURATE  = 11,
} rc_blend_factor;

/* Blend equation.  0 (zero-init default) resolves to RC_BLEND_OP_ADD. */
typedef enum {
    RC_BLEND_OP_DEFAULT          = 0,   /* resolves to RC_BLEND_OP_ADD */
    RC_BLEND_OP_ADD              = 1,
    RC_BLEND_OP_SUBTRACT         = 2,
    RC_BLEND_OP_REVERSE_SUBTRACT = 3,
    RC_BLEND_OP_MIN              = 4,
    RC_BLEND_OP_MAX              = 5,
} rc_blend_op;

typedef struct {
    bool            enabled;
    rc_blend_factor src_factor_rgb;    /* 0 → SRC_ALPHA */
    rc_blend_factor dst_factor_rgb;    /* 0 → ONE_MINUS_SRC_ALPHA */
    rc_blend_factor src_factor_alpha;  /* 0 → resolved src_factor_rgb */
    rc_blend_factor dst_factor_alpha;  /* 0 → resolved dst_factor_rgb */
    rc_blend_op     op_rgb;            /* 0 → ADD */
    rc_blend_op     op_alpha;          /* 0 → ADD */
} rc_blend_state;

/* ---- depth state ---- */

typedef struct {
    bool            enabled;
    rc_compare_func compare;       /* 0 → LESS_EQUAL */
    bool            write_enabled;
} rc_depth_state;

/* ---- cull state ---- */

typedef enum {
    RC_CULL_NONE  = 0,   /* no face culling (default) */
    RC_CULL_FRONT = 1,
    RC_CULL_BACK  = 2,
} rc_cull_face;

/*
 * Which winding order is considered front-facing.
 * 0 (default) = CCW, matching the OpenGL default.
 */
typedef enum {
    RC_WINDING_CCW = 0,
    RC_WINDING_CW  = 1,
} rc_winding;

typedef struct {
    rc_cull_face face;        /* 0 → no culling */
    rc_winding   front_face;  /* 0 → CCW */
} rc_cull_state;

/* ---- pipeline descriptor ---- */

/* Per-buffer-slot layout: stride and instancing divisor. */
typedef struct {
    uint32_t stride;    /* byte stride between consecutive elements */
    uint32_t divisor;   /* 0 = per-vertex, 1 = per-instance */
} rc_buffer_layout;

/* Per-attribute layout. */
typedef struct {
    uint32_t         location;     /* shader layout(location = N) */
    uint32_t         buffer_slot;  /* index into rc_bindings.vertex_buffers */
    rc_attrib_format format;       /* 0 = end of attrib list (sentinel) */
    uint32_t         offset;       /* byte offset within the buffer stride */
} rc_attrib_layout;

#define RC_MAX_VERTEX_BUFFERS  4
#define RC_MAX_VERTEX_ATTRIBS 16
#define RC_MAX_TEXTURE_SLOTS   8

typedef struct {
    rc_shader        shader;
    rc_buffer_layout buffer_layouts[RC_MAX_VERTEX_BUFFERS];
    rc_attrib_layout attribs[RC_MAX_VERTEX_ATTRIBS]; /* terminated by format == 0 */
    rc_index_type    index_type;
    rc_depth_state   depth;
    rc_blend_state   blend;
    rc_cull_state    cull;
} rc_pipeline_desc;

/* Opaque handle.  id == 0 is invalid. */
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

/* Destroy a pipeline. */
void        rc_pipeline_destroy(rc_pipeline pip);

/* ---- draw API ---- */

/*
 * Apply a pipeline: binds its shader and sets all render state (depth,
 * blend, cull).  Must be called before rc_gfx_apply_bindings and
 * rc_gfx_draw each frame.
 */
void rc_gfx_apply_pipeline(rc_pipeline pip);

/*
 * Apply buffer bindings for the current pipeline.  Must be called after
 * rc_gfx_apply_pipeline.
 */
void rc_gfx_apply_bindings(const rc_bindings *bind);

/*
 * Submit a draw call.  Draws count vertices (or indices) starting at first,
 * repeated instances times.  Use instances == 1 for non-instanced draws.
 */
void rc_gfx_draw(uint32_t first, uint32_t count, uint32_t instances);

#endif /* RC_GFX_PIPELINE_H_ */
