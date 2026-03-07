/*
 * buffer.h - GPU buffer objects.
 *
 * Provides:
 *   rc_buffer — GPU buffer (wraps a GL VBO or EBO).
 *
 * Vertex layout and draw calls are in pipeline.h.
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
 *   // per-frame update (no reallocation)
 *   rc_buffer_update(inst, data, sizeof(data));
 */

#ifndef RC_GFX_BUFFER_H_
#define RC_GFX_BUFFER_H_

#include <stdint.h>

/* Hint describing how a buffer's data will be used. */
typedef enum {
    RC_BUFFER_STATIC,   /* set once, drawn many times */
    RC_BUFFER_DYNAMIC,  /* updated frequently */
} rc_buffer_usage;

/* Opaque handle to a GPU buffer (VBO/EBO).  id == 0 is invalid. */
typedef struct { uint32_t id; } rc_buffer;

/* Allocate a new GPU buffer.  No data is uploaded yet. */
rc_buffer rc_buffer_make(rc_buffer_usage usage);

/* Upload (or replace) the buffer's data.  Calls glBufferData, which
 * allocates or reallocates GPU storage.  Use for the initial upload
 * or whenever the size changes. */
void rc_buffer_upload(rc_buffer buf, const void *data, uint32_t size);

/* Update the buffer's contents in-place.  Calls glBufferSubData — no
 * reallocation.  size must be <= the size passed to rc_buffer_upload. */
void rc_buffer_update(rc_buffer buf, const void *data, uint32_t size);

/* Delete the GPU buffer.  buf must not be used after this call. */
void rc_buffer_destroy(rc_buffer buf);

#endif /* RC_GFX_BUFFER_H_ */
