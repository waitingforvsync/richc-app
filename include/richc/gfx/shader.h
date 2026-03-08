/*
 * shader.h - GLSL shader program creation and uniform access.
 *
 * Usage
 * -----
 *   rc_arena scratch = rc_arena_make_default();
 *   rc_str vert = rc_load_text(RC_STR("my.vert"), &scratch).text;
 *   rc_str frag = rc_load_text(RC_STR("my.frag"), &scratch).text;
 *
 *   rc_shader sh         = rc_shader_make(vert, frag, scratch);
 *   rc_uniform_loc u_mvp = rc_shader_loc(sh, "u_mvp");
 *   rc_uniform_loc u_tex = rc_shader_loc(sh, "u_texture");
 *
 *   // each frame
 *   rc_gfx_apply_pipeline(pip);
 *   rc_shader_set_mat44(u_mvp, proj);
 *   rc_shader_set_i32  (u_tex, 0);
 *
 * Error handling
 * --------------
 * Compile or link failure prints the GL info log to stderr then calls RC_PANIC.
 *
 * Uniform setters
 * ---------------
 * rc_gfx_apply_pipeline must be called before any rc_shader_set_* call.
 * A setter whose loc.loc == -1 (uniform not found) is a silent no-op.
 */

#ifndef RC_GFX_SHADER_H_
#define RC_GFX_SHADER_H_

#include <stdint.h>
#include "richc/str.h"
#include "richc/arena.h"
#include "richc/math/vec2f.h"
#include "richc/math/vec3f.h"
#include "richc/math/vec4f.h"
#include "richc/math/mat44f.h"

/* ---- types ---- */

/* Opaque handle to a linked GL program object.  id == 0 is invalid. */
typedef struct { uint32_t id; } rc_shader;

/* Location of a uniform variable within a shader program.
 * loc == -1 indicates the uniform was not found. */
typedef struct { int32_t loc; } rc_uniform_loc;

/* ---- creation ---- */

/*
 * Compile vert_src and frag_src, link into a program, and return it.
 * scratch is used for the GL info log buffer on failure.
 * RC_PANICs (after printing the GL info log to stderr) on compile or link error.
 */
rc_shader rc_shader_make(rc_str vert_src, rc_str frag_src, rc_arena scratch);

/* Delete the GL program object.  sh must not be used after this call. */
void rc_shader_destroy(rc_shader sh);

/* ---- uniform access ---- */

/* Query a uniform location by name.  Returns { -1 } if not found.
 * Call once at startup and cache the result. */
rc_uniform_loc rc_shader_loc(rc_shader sh, const char *name);

/* Bind the shader program.  Must be called before any rc_shader_set_* call. */
void rc_shader_bind(rc_shader sh);

/* Set scalar float uniform.  No-op when loc.loc == -1. */
void rc_shader_set_f32 (rc_uniform_loc loc, float v);

/* Set scalar int uniform (e.g. sampler2D texture unit).  No-op when loc.loc == -1. */
void rc_shader_set_i32 (rc_uniform_loc loc, int32_t v);

/* Set vec2 uniform.  No-op when loc.loc == -1. */
void rc_shader_set_vec2(rc_uniform_loc loc, rc_vec2f v);

/* Set vec3 uniform.  No-op when loc.loc == -1. */
void rc_shader_set_vec3(rc_uniform_loc loc, rc_vec3f v);

/* Set vec4 uniform.  No-op when loc.loc == -1. */
void rc_shader_set_vec4(rc_uniform_loc loc, rc_vec4f v);

/* Set mat4 uniform.  No-op when loc.loc == -1. */
void rc_shader_set_mat44(rc_uniform_loc loc, rc_mat44f m);

/* Set sampler2D uniform to a texture unit slot.  No-op when loc.loc == -1. */
void rc_shader_set_texture(rc_uniform_loc loc, int32_t slot);

#endif /* RC_GFX_SHADER_H_ */
