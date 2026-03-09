/*
 * texture.h - GPU texture object creation and management.
 *
 * Typical usage
 * -------------
 *   rc_texture tex = rc_texture_make(&(rc_texture_desc) {
 *       .size        = img.size,
 *       .format      = RC_TEXTURE_FORMAT_RGBA8,
 *       .usage       = RC_TEXTURE_USAGE_STATIC,
 *       .filter      = RC_TEXTURE_FILTER_LINEAR,
 *       .wrap        = RC_TEXTURE_WRAP_CLAMP,
 *       .gen_mipmaps = true,
 *       .data        = img.data.data,
 *   });
 *
 *   // bind to a slot via rc_gfx_apply_bindings, then tell the shader:
 *   rc_shader_set_texture(u_tex_loc, 0);
 *
 * Note
 * ----
 * rc_texture_format shares numeric values with rc_pixel_format (1/3/4).
 * A cast is valid when the formats match, but this header does not include
 * image.h — they remain separate types.
 */

#ifndef RC_GFX_TEXTURE_H_
#define RC_GFX_TEXTURE_H_

#include <stdbool.h>
#include <stdint.h>
#include "richc/math/vec2i.h"

/* ---- format / usage / sampler enums ---- */

typedef enum {
    RC_TEXTURE_FORMAT_R8    = 1,
    RC_TEXTURE_FORMAT_RGB8  = 3,
    RC_TEXTURE_FORMAT_RGBA8 = 4,
} rc_texture_format;

typedef enum {
    RC_TEXTURE_USAGE_STATIC,
    RC_TEXTURE_USAGE_DYNAMIC,
} rc_texture_usage;

typedef enum {
    RC_TEXTURE_WRAP_REPEAT,
    RC_TEXTURE_WRAP_CLAMP,
    RC_TEXTURE_WRAP_MIRROR,
} rc_texture_wrap;

typedef enum {
    RC_TEXTURE_FILTER_NEAREST,
    RC_TEXTURE_FILTER_LINEAR,
} rc_texture_filter;

/* ---- descriptor ---- */

typedef struct {
    rc_vec2i          size;    /* width (.x) and height (.y) in pixels */
    rc_texture_format format;
    rc_texture_usage  usage;
    rc_texture_wrap   wrap;         /* applies to both U and V */
    rc_texture_filter filter;       /* applies to both min and mag */
    bool              gen_mipmaps;
    const void       *data;         /* initial pixels; NULL for empty */
} rc_texture_desc;

/* Opaque handle.  id == 0 is invalid. */
typedef struct { uint32_t id; } rc_texture;

/* ---- API ---- */

/* Create a texture from a descriptor.  RC_PANICs on failure. */
rc_texture rc_texture_make   (const rc_texture_desc *desc);

/* Replace the full image with new pixel data (same dimensions and format). */
void       rc_texture_update (rc_texture tex, const void *data);

/* Delete the texture.  tex must not be used after this call. */
void       rc_texture_destroy(rc_texture tex);

#endif /* RC_GFX_TEXTURE_H_ */
