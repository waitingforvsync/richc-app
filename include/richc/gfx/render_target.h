/*
 * render_target.h - off-screen render target (FBO + attachments).
 *
 * A render target wraps an OpenGL framebuffer object with one or more
 * colour attachments (textures) and an optional depth renderbuffer.  The
 * colour textures are first-class rc_texture handles and can be bound in
 * rc_bindings.textures[] for sampling in a subsequent pass.
 *
 * Multiple colour attachments (MRT)
 * ----------------------------------
 * Fill rc_render_target_desc.color[] with one entry per attachment,
 * terminated by an entry with format == 0.  Attachment i is written by
 * layout(location = i) out vec4 in the fragment shader.
 *
 * Viewport
 * --------
 * rc_gfx_begin_render_target sets the GL viewport to the RT size.
 * rc_gfx_end_render_target restores it to rc_app_size().
 *
 * Usage
 * -----
 *   rc_render_target rt = rc_render_target_make(&(rc_render_target_desc) {
 *       .size  = rc_app_size(),
 *       .color = {{ .format = RC_TEXTURE_FORMAT_RGBA8,
 *                   .filter = RC_TEXTURE_FILTER_LINEAR,
 *                   .wrap   = RC_TEXTURE_WRAP_CLAMP }},
 *       .depth = true,
 *   });
 *
 *   rc_gfx_begin_render_target(rt);
 *       rc_gfx_clear(...);
 *       // draw calls ...
 *   rc_gfx_end_render_target();
 *
 *   rc_texture color = rc_render_target_color(rt, 0);
 *   // bind color in rc_bindings.textures[slot] for the next pass
 *
 * Resize
 * ------
 * Render targets are fixed-size.  Recreate on window resize:
 *   rc_render_target_destroy(rt);
 *   rt = rc_render_target_make(&desc_with_new_size);
 */

#ifndef RC_GFX_RENDER_TARGET_H_
#define RC_GFX_RENDER_TARGET_H_

#include "richc/gfx/texture.h"   /* rc_texture, rc_texture_format/filter/wrap */
#include "richc/math/vec2i.h"
#include <stdbool.h>
#include <stdint.h>

/* ---- constants ---- */

#define RC_MAX_COLOR_ATTACHMENTS 4

/* ---- types ---- */

/*
 * Descriptor for one colour attachment.  format == 0 terminates the array.
 */
typedef struct {
    rc_texture_format format;   /* 0 terminates the color[] list */
    rc_texture_filter filter;
    rc_texture_wrap   wrap;
} rc_color_attachment_desc;

/*
 * Full render target descriptor.
 * color[] is a zero-terminated array of up to RC_MAX_COLOR_ATTACHMENTS
 * entries.  At least one entry with a non-zero format must be present.
 */
typedef struct {
    rc_vec2i                 size;
    rc_color_attachment_desc color[RC_MAX_COLOR_ATTACHMENTS];
    bool                     depth;   /* create a depth renderbuffer */
} rc_render_target_desc;

/* Opaque handle.  id == 0 is invalid. */
typedef struct { uint32_t id; } rc_render_target;

/* ---- resource API ---- */

/* Create a render target.  RC_PANICs if the FBO is incomplete. */
rc_render_target rc_render_target_make   (const rc_render_target_desc *desc);

/* Destroy the render target and all its attached textures. */
void             rc_render_target_destroy(rc_render_target rt);

/* Return the texture for colour attachment index.  RC_PANICs if out of range. */
rc_texture       rc_render_target_color  (rc_render_target rt, uint32_t index);

/* ---- render-state API ---- */

/*
 * Bind the render target as the active framebuffer and set the viewport to
 * the RT's size.  RC_PANICs if already inside a render target.
 */
void rc_gfx_begin_render_target(rc_render_target rt);

/*
 * Restore the default framebuffer (FBO 0) and set the viewport to
 * rc_app_size().  RC_PANICs if not inside a render target.
 */
void rc_gfx_end_render_target(void);

#endif /* RC_GFX_RENDER_TARGET_H_ */
