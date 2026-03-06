/*
 * buffer.h - GPU buffer objects and vertex array configuration.
 *
 * Provides:
 *   rc_buffer        — GPU vertex buffer (wraps a GL VBO).
 *   rc_vertex_array  — vertex attribute layout (wraps a GL VAO).
 *
 * Typical usage
 * -------------
 *   // static geometry
 *   rc_buffer quad = rc_buffer_make(RC_BUFFER_STATIC);
 *   rc_buffer_upload(quad, verts, sizeof(verts));
 *
 *   // dynamic per-instance data
 *   rc_buffer inst = rc_buffer_make(RC_BUFFER_DYNAMIC);
 *   rc_buffer_upload(inst, data, sizeof(data));
 *
 *   // describe the vertex layout once
 *   rc_attrib_desc attribs[] = {
 *       { 0, quad, RC_ATTRIB_FLOAT, 2, 8,           0,                    0 }, // per-vertex UV
 *       { 1, inst, RC_ATTRIB_FLOAT, 2, sizeof(T),   offsetof(T, start),   1 }, // per-instance
 *       ...
 *   };
 *   rc_vertex_array va = rc_vertex_array_make(attribs, sizeof(attribs) / sizeof(attribs[0]));
 *
 *   // each frame — draw calls and blend state are in gfx.h
 *   rc_buffer_upload(inst, data, sizeof(data));
 *   rc_vertex_array_bind(va);
 */

#ifndef RC_GFX_BUFFER_H_
#define RC_GFX_BUFFER_H_

#include <stdint.h>

/* ---- buffer ---- */

/* Hint describing how a buffer's data will be used. */
typedef enum {
    RC_BUFFER_STATIC,   /* set once, drawn many times */
    RC_BUFFER_DYNAMIC,  /* updated frequently */
} rc_buffer_usage;

/* Opaque handle to a GPU vertex buffer (VBO).  id == 0 is invalid. */
typedef struct { uint32_t id; } rc_buffer;

/* Allocate a new GPU buffer.  No data is uploaded yet. */
rc_buffer rc_buffer_make(rc_buffer_usage usage);

/* Upload (or replace) the buffer's data.
 * Calls glBufferData with the usage hint supplied at rc_buffer_make. */
void rc_buffer_upload(rc_buffer buf, const void *data, uint32_t size);

/* Delete the GPU buffer.  buf must not be used after this call. */
void rc_buffer_destroy(rc_buffer buf);

/* ---- vertex array ---- */

/* Component type for a vertex attribute. */
typedef enum {
    RC_ATTRIB_FLOAT,  /* float (GLfloat) */
} rc_attrib_type;

/* Description of a single vertex attribute binding. */
typedef struct {
    uint32_t       location;  /* shader attribute location (layout(location = N)) */
    rc_buffer      buffer;    /* source buffer */
    rc_attrib_type type;      /* component type */
    uint32_t       count;     /* number of components: 1, 2, 3, or 4 */
    uint32_t       stride;    /* stride in bytes between consecutive elements */
    uint32_t       offset;    /* byte offset of this attribute within the stride */
    uint32_t       divisor;   /* 0 = per-vertex,  1 = per-instance */
} rc_attrib_desc;

/* Opaque handle to a vertex array object (VAO).  id == 0 is invalid. */
typedef struct { uint32_t id; } rc_vertex_array;

/* Create a VAO from an array of attribute descriptors.
 * The referenced rc_buffer objects must exist for the lifetime of the VAO. */
rc_vertex_array rc_vertex_array_make(const rc_attrib_desc *attribs, uint32_t count);

/* Bind the VAO.  Must be called before any rc_gfx_draw_* call. */
void rc_vertex_array_bind(rc_vertex_array va);

/* Delete the VAO.  va must not be used after this call. */
void rc_vertex_array_destroy(rc_vertex_array va);

#endif /* RC_GFX_BUFFER_H_ */
