/*
 * texture_gl33_internal.h - internal linkage helper for texture_gl33.c.
 *
 * Not part of the public API.  Only pipeline_gl33.c should include this.
 */

#ifndef RC_GFX_TEXTURE_GL33_INTERNAL_H_
#define RC_GFX_TEXTURE_GL33_INTERNAL_H_

#include "richc/gfx/texture.h"
#include <glad/gl.h>

/*
 * Look up the GL texture object for a texture handle.
 * Returns 0 if tex.id == 0 or the handle is not found.
 */
GLuint rc_texture_gl_(rc_texture tex);

#endif /* RC_GFX_TEXTURE_GL33_INTERNAL_H_ */
