/*
 * test_app.c - pentagram + textured owl quad + font SDF atlas.
 *
 * Left side: owl.png rendered as a filtered 128×128-NDC-unit square.
 * Centre: the animated pentagram (5 anti-aliased instanced lines).
 * Right side: Roboto-Regular SDF atlas (greyscale via R8 texture swizzle).
 *
 * No GL calls appear here; everything goes through the rc_gfx / rc_shader /
 * rc_buffer / rc_pipeline / rc_texture / rc_app APIs.
 */

#include "richc/app/app.h"
#include "richc/app/keys.h"
#include "richc/gfx/gfx.h"
#include "richc/gfx/shader.h"
#include "richc/gfx/buffer.h"
#include "richc/gfx/pipeline.h"
#include "richc/gfx/texture.h"
#include "richc/gfx/render_target.h"
#include "richc/math/mat44f.h"
#include "richc/math/math.h"
#include "richc/math/vec2f.h"
#include "richc/image/image.h"
#include "richc/font/font.h"
#include "richc/str.h"
#include "richc/arena.h"
#include "richc/debug.h"
#include <math.h>
#include <stddef.h>   /* offsetof */

/* ======================================================================== */
/* Pentagram shaders                                                         */
/* ======================================================================== */

/*
 * Vertex shader
 * -------------
 * a_uv        (location 0, per-vertex):   U in [-1, 1] (width), V in [0, 1] (length)
 * a_start     (location 1, per-instance): line start in pixel coords
 * a_end       (location 2, per-instance): line end in pixel coords
 * a_half_thick(location 3, per-instance): perpendicular half-width vector
 * a_color     (location 4, per-instance): RGBA colour
 *
 * Position = lerp(start, end, V) + half_thick * U
 */
static const char k_line_vert_src[] =
    "#version 330 core\n"
    "\n"
    "layout(location = 0) in vec2 a_uv;\n"
    "layout(location = 1) in vec2 a_start;\n"
    "layout(location = 2) in vec2 a_end;\n"
    "layout(location = 3) in float a_half_thick;\n"
    "layout(location = 4) in vec4 a_color;\n"
    "\n"
    "uniform mat4 u_mvp;\n"
    "\n"
    "out vec2 v_uv;\n"
    "out vec4 v_color;\n"
    "\n"
    "void main()\n"
    "{\n"
    "    vec2 dir    = normalize(a_end - a_start);\n"
    "    vec2 perp   = vec2(-dir.y, dir.x);\n"
    "    vec2 pos    = mix(a_start, a_end, a_uv.y) + perp * a_half_thick * a_uv.x;\n"
    "    gl_Position = u_mvp * vec4(pos, 0.0, 1.0);\n"
    "    v_uv        = a_uv;\n"
    "    v_color     = a_color;\n"
    "}\n";

/*
 * Fragment shader
 * ---------------
 * Derives alpha from fwidth(U): the edge falls off over one screen pixel on
 * each side, giving smooth anti-aliasing regardless of scale.
 */
static const char k_line_frag_src[] =
    "#version 330 core\n"
    "\n"
    "in  vec2 v_uv;\n"
    "in  vec4 v_color;\n"
    "\n"
    "out vec4 frag_color;\n"
    "\n"
    "void main()\n"
    "{\n"
    "    float edge  = fwidth(v_uv.x);\n"
    "    float alpha = smoothstep(1.0 + edge, 1.0 - edge, abs(v_uv.x));\n"
    "    frag_color  = vec4(v_color.rgb, v_color.a * alpha);\n"
    "}\n";

/* ======================================================================== */
/* Textured-quad shaders                                                     */
/* ======================================================================== */

/*
 * Vertex shader: NDC positions + UV passthrough.
 * a_pos (location 0): xy in NDC
 * a_uv  (location 1): texture coordinate
 */
static const char k_tex_vert_src[] =
    "#version 330 core\n"
    "\n"
    "layout(location = 0) in vec2 a_pos;\n"
    "layout(location = 1) in vec2 a_uv;\n"
    "\n"
    "out vec2 v_uv;\n"
    "\n"
    "void main()\n"
    "{\n"
    "    gl_Position = vec4(a_pos, 0.0, 1.0);\n"
    "    v_uv        = a_uv;\n"
    "}\n";

/*
 * Fragment shader: sample from texture unit 0.
 */
static const char k_tex_frag_src[] =
    "#version 330 core\n"
    "\n"
    "uniform sampler2D u_tex;\n"
    "\n"
    "in  vec2 v_uv;\n"
    "out vec4 frag_color;\n"
    "\n"
    "void main()\n"
    "{\n"
    "    frag_color = texture(u_tex, v_uv);\n"
    "}\n";

/* ======================================================================== */
/* Glyph SDF shaders                                                         */
/* ======================================================================== */

/*
 * Vertex shader (instanced unit quad)
 * ------------------------------------
 * Buffer 0 (per-vertex,   divisor=0): a_quad — [0,1]×[0,1] unit quad position
 * Buffer 1 (per-instance, divisor=1):
 *   a_pos      (location 1): top-left pixel position in screen coords (Y-down)
 *   a_size     (location 2): glyph size in texels (= atlas_rect size)
 *   a_scale    (location 3): scale factor; pixel size = a_size * a_scale
 *   a_atlas_uv (location 4): texel offset of this glyph in the atlas
 *
 * Uniforms:
 *   u_resolution — window size in pixels (vec2)
 *   u_atlas_size — atlas image size in texels (vec2)
 *
 * Screen → NDC: x = px/W*2−1, y = 1−px/H*2 (Y-down screen, Y-up NDC)
 * Atlas UV: (a_atlas_uv + a_quad * a_size) / u_atlas_size
 *   V is not flipped: GL t=0 maps to the first byte of our image data (row 0 =
 *   top), which is the same convention used by the existing atlas quad.
 */
static const char k_glyph_vert_src[] =
    "#version 330 core\n"
    "\n"
    "layout(location = 0) in vec2  a_quad;\n"
    "layout(location = 1) in vec2  a_pos;\n"
    "layout(location = 2) in vec2  a_size;\n"
    "layout(location = 3) in float a_scale;\n"
    "layout(location = 4) in vec2  a_atlas_uv;\n"
    "\n"
    "uniform vec2 u_resolution;\n"
    "uniform vec2 u_atlas_size;\n"
    "\n"
    "out vec2 v_uv;\n"
    "\n"
    "void main()\n"
    "{\n"
    "    vec2 pixel = a_pos + a_quad * (a_size * a_scale);\n"
    "    vec2 ndc   = vec2(pixel.x / u_resolution.x * 2.0 - 1.0,\n"
    "                      1.0 - pixel.y / u_resolution.y * 2.0);\n"
    "    gl_Position = vec4(ndc, 0.0, 1.0);\n"
    "    v_uv        = (a_atlas_uv + a_quad * a_size) / u_atlas_size;\n"
    "}\n";

/*
 * Fragment shader: SDF → alpha via geometry-invariant anti-aliasing.
 *
 * SDF encoding: 128/255 ≈ 0.502 is on the contour; values above → inside.
 *
 * Edge width is derived from UV-space derivatives (how many atlas texels
 * correspond to one screen pixel) rather than fwidth(d).  fwidth(d) varies
 * with local glyph geometry — it's larger at curves and corners — causing
 * non-uniform apparent stroke widths.  The UV-derivative measure is constant
 * across the glyph for a given rendering scale, giving uniform strokes.
 *
 * threshold < 0.5 renders strokes slightly bolder.
 * u_color: RGBA tint; alpha is multiplied by the SDF-derived alpha.
 */
static const char k_glyph_frag_src[] =
    "#version 330 core\n"
    "\n"
    "uniform sampler2D u_tex;\n"
    "uniform vec4      u_color;\n"
    "uniform vec2      u_atlas_size;\n"  /* shared with vertex shader */
    "uniform float     u_sdf_spread;\n"  /* SDF gradient radius in atlas texels */
    "\n"
    "in  vec2 v_uv;\n"
    "out vec4 frag_color;\n"
    "\n"
    "void main()\n"
    "{\n"
    "    float d = texture(u_tex, v_uv).r;\n"
    "\n"
    "    /* Atlas texels per screen pixel — geometry-invariant. */\n"
    "    float atlas_px = 0.5 * (length(dFdx(v_uv)) * u_atlas_size.x\n"
    "                          + length(dFdy(v_uv)) * u_atlas_size.y);\n"
    "    /* SDF value change over half a screen pixel at a straight edge. */\n"
    "    float edge  = atlas_px * (127.0 / (u_sdf_spread * 255.0)) * 0.5;\n"
    "\n"
    "    float alpha = d >= 0.5 ? 1.0 : smoothstep(0.5 - edge, 0.5, d);\n"
    "    frag_color  = vec4(u_color.rgb, u_color.a * alpha);\n"
    "}\n";

/*
 * Fragment shader: separable Gaussian blur.
 * u_blur_dir — pixel-space direction: (radius, 0) for horizontal,
 *              (0, radius) for vertical.  Divided by textureSize to get
 *              per-tap UV offset, so no uniform update is needed on resize.
 * 9-tap kernel with sigma ≈ 2.
 */
static const char k_blur_frag_src[] =
    "#version 330 core\n"
    "\n"
    "uniform sampler2D u_tex;\n"
    "uniform vec2      u_blur_dir;\n"
    "\n"
    "in  vec2 v_uv;\n"
    "out vec4 frag_color;\n"
    "\n"
    "void main()\n"
    "{\n"
    "    vec2        step = u_blur_dir / vec2(textureSize(u_tex, 0));\n"
    "    const float w[5] = float[](0.2270270, 0.1945946,\n"
    "                               0.1216216, 0.0540540, 0.0162162);\n"
    "    vec4 col = texture(u_tex, v_uv) * w[0];\n"
    "    for (int i = 1; i < 5; i++) {\n"
    "        col += texture(u_tex, v_uv + float(i) * step) * w[i];\n"
    "        col += texture(u_tex, v_uv - float(i) * step) * w[i];\n"
    "    }\n"
    "    frag_color = vec4(mix(col.rgb, vec3(0.625), 0.25), 1.0);\n"
    "}\n";

/* ======================================================================== */
/* Line instance struct                                                      */
/* ======================================================================== */

typedef struct {
    rc_vec2f start;
    rc_vec2f end;
    float    half_thickness;
    rc_color color;
} line_t;

#define MAX_GLYPH_INSTANCES 256

/* ======================================================================== */
/* Glyph instance struct                                                     */
/* ======================================================================== */

/*
 * Per-glyph instance data uploaded to the instance vertex buffer.
 * Layout must match the vertex attrib declarations in k_glyph_vert_src.
 */
typedef struct {
    float pos_x,    pos_y;     /* top-left pixel position (screen Y-down) */
    float size_x,   size_y;    /* glyph size in texels                     */
    float scale;               /* pixel_size = texel_size * scale          */
    float _pad;                /* explicit padding to 8-byte alignment     */
    float atlas_u,  atlas_v;   /* texel offset of this glyph in the atlas  */
} glyph_inst_t;

/* ======================================================================== */
/* Textured-quad vertex struct                                               */
/* ======================================================================== */

typedef struct {
    float x, y;    /* NDC position */
    float u, v;    /* texture coordinate */
} quad_vert_t;

/* ======================================================================== */
/* Application context                                                       */
/* ======================================================================== */

typedef struct {
    /* --- pentagram --- */
    rc_shader      line_shader;
    rc_uniform_loc u_mvp;
    rc_buffer      line_quad_buf;
    rc_buffer      line_inst_buf;
    rc_pipeline    line_pipeline;
    line_t         lines[5];

    /* --- textured quad --- */
    rc_shader      tex_shader;
    rc_uniform_loc u_tex;
    rc_buffer      tex_quad_buf;
    rc_pipeline    tex_pipeline;
    rc_texture     owl_tex;

    /* --- font atlas quad (shows the raw atlas) --- */
    rc_texture     atlas_tex;
    rc_buffer      atlas_quad_buf;

    /* --- text rendering --- */
    rc_font_atlas  font;           /* glyph metrics; atlas.data is freed, size/etc. are valid */
    rc_shader      glyph_shader;
    rc_uniform_loc u_glyph_tex;
    rc_uniform_loc u_glyph_resolution;
    rc_uniform_loc u_glyph_atlas_size;
    rc_uniform_loc u_glyph_color;
    rc_uniform_loc u_glyph_sdf_spread;
    rc_buffer      glyph_quad_buf;
    rc_buffer      glyph_inst_buf;
    rc_pipeline    glyph_pipeline;

    /* --- render targets --- */
    rc_render_target scene_rt;    /* full scene; depth enabled */
    rc_render_target blur_h_rt;   /* horizontally blurred scene; no depth */

    /* --- blur pass --- */
    rc_shader      blur_shader;
    rc_uniform_loc u_blur_tex;
    rc_uniform_loc u_blur_dir;
    rc_buffer      fullscreen_quad_buf;
    rc_buffer      blur_rect_buf;
    rc_pipeline    blur_pipeline;
} App;

static App g_app;

/* ======================================================================== */
/* Pentagram geometry                                                        */
/* ======================================================================== */

static void make_pentagram_lines(line_t *out, float radius, float half_width, float time)
{
    rc_vec2f pts[5];
    for (int i = 0; i < 5; i++) {
        float base  = rc_deg_to_rad(-90.0f + (float)i * 72.0f);
        float phase = rc_deg_to_rad((float)i * 72.0f);
        float angle = base + rc_deg_to_rad(15.0f) * sinf(2.0f * time + phase);
        pts[i] = rc_vec2f_scalar_mul(rc_vec2f_make_cossin(angle), radius);
    }

    static const int order[6] = {0, 2, 4, 1, 3, 0};
    for (int i = 0; i < 5; i++) {
        out[i] = (line_t) {
            .start          = pts[order[i]],
            .end            = pts[order[i + 1]],
            .half_thickness = half_width,
            .color          = rc_color_make(0.0f, 0.0f, 0.0f, 1.0f),
        };
    }
}

/* ======================================================================== */
/* Textured-quad geometry                                                    */
/* ======================================================================== */

/*
 * A 128×128-NDC-unit square to the left of the pentagram.
 * NDC extents: x in [-0.95, -0.35], y in [-0.45, 0.35].
 * UV: (0,0) top-left, (1,1) bottom-right (OpenGL convention: V=0 is bottom).
 * We flip V so the image reads top-to-bottom: top of image → V=1.
 */
static const quad_vert_t k_tex_quad_verts[] = {
    /* triangle 1 */
    { -0.95f, -0.45f,  0.0f, 1.0f },
    { -0.35f, -0.45f,  1.0f, 1.0f },
    { -0.35f,  0.35f,  1.0f, 0.0f },
    /* triangle 2 */
    { -0.95f, -0.45f,  0.0f, 1.0f },
    { -0.35f,  0.35f,  1.0f, 0.0f },
    { -0.95f,  0.35f,  0.0f, 0.0f },
};

/* ======================================================================== */
/* Atlas quad vertices                                                       */
/* ======================================================================== */

/*
 * Displays the packed atlas to the right of the owl.
 * NDC extents: x in [0.35, 0.95], y in [-0.3, 0.3] — a 0.6×0.6 square.
 * UV: V-flipped so that image row 0 (top) maps to V=1 (GL bottom).
 */
static const quad_vert_t k_atlas_quad_verts[] = {
    /* triangle 1 */
    {  0.35f, -0.3f,  0.0f, 1.0f },
    {  0.95f, -0.3f,  1.0f, 1.0f },
    {  0.95f,  0.3f,  1.0f, 0.0f },
    /* triangle 2 */
    {  0.35f, -0.3f,  0.0f, 1.0f },
    {  0.95f,  0.3f,  1.0f, 0.0f },
    {  0.35f,  0.3f,  0.0f, 0.0f },
};

/* ======================================================================== */
/* Fullscreen quad and blur rect vertices                                    */
/* ======================================================================== */

/*
 * Fullscreen quad covering NDC [-1,1]x[-1,1].
 * UVs are NOT flipped: render-target pixels are stored bottom-up (GL
 * convention) so UV (0,0) = bottom-left maps directly to NDC (-1,-1).
 */
static const quad_vert_t k_fullscreen_quad_verts[] = {
    /* triangle 1 */
    { -1.0f, -1.0f,  0.0f, 0.0f },
    {  1.0f, -1.0f,  1.0f, 0.0f },
    {  1.0f,  1.0f,  1.0f, 1.0f },
    /* triangle 2 */
    { -1.0f, -1.0f,  0.0f, 0.0f },
    {  1.0f,  1.0f,  1.0f, 1.0f },
    { -1.0f,  1.0f,  0.0f, 1.0f },
};

/*
 * Glass-effect rect in fixed screen-pixel coordinates.
 * Pixel origin is top-left, Y increases downward.
 * At 1280×720 these correspond to NDC x∈[-0.6,0.1], y∈[-0.4,0.35].
 */
#define GLASS_X0 256.0f
#define GLASS_X1 704.0f
#define GLASS_Y0 234.0f   /* top edge   */
#define GLASS_Y1 504.0f   /* bottom edge */

/*
 * Build blur-rect vertices from fixed pixel coords and the current window size.
 * NDC:  nx = px/w*2−1,  ny = 1−py/h*2  (screen Y-down → NDC Y-up)
 * UV:   u  = px/w,      v  = 1−py/h
 * (render-target UVs are not flipped: GL v=0 is screen bottom)
 */
static void make_glass_rect_verts_(quad_vert_t *v, float w, float h)
{
    float nx0 = GLASS_X0 / w * 2.0f - 1.0f,  nx1 = GLASS_X1 / w * 2.0f - 1.0f;
    float ny1 = 1.0f - GLASS_Y0 / h * 2.0f,  ny0 = 1.0f - GLASS_Y1 / h * 2.0f;
    float ux0 = GLASS_X0 / w,                 ux1 = GLASS_X1 / w;
    float uy1 = 1.0f - GLASS_Y0 / h,          uy0 = 1.0f - GLASS_Y1 / h;
    v[0] = (quad_vert_t) { nx0, ny0,  ux0, uy0 };
    v[1] = (quad_vert_t) { nx1, ny0,  ux1, uy0 };
    v[2] = (quad_vert_t) { nx1, ny1,  ux1, uy1 };
    v[3] = (quad_vert_t) { nx0, ny0,  ux0, uy0 };
    v[4] = (quad_vert_t) { nx1, ny1,  ux1, uy1 };
    v[5] = (quad_vert_t) { nx0, ny1,  ux0, uy1 };
}

/* ======================================================================== */
/* Glyph unit-quad vertices                                                  */
/* ======================================================================== */

/*
 * Unit quad [0,1]×[0,1] as two triangles, six vertices.
 * a_quad.xy spans (0,0)→(1,1); the vertex shader maps this into screen
 * pixels via the per-instance pos/size/scale attributes.
 */
static const float k_glyph_quad_verts[] = {
    0.0f, 0.0f,   1.0f, 0.0f,   1.0f, 1.0f,
    0.0f, 0.0f,   1.0f, 1.0f,   0.0f, 1.0f,
};

/* ======================================================================== */
/* Pentagram unit-quad vertices                                              */
/* ======================================================================== */

/*
 * Unit quad: 6 vertices (two triangles).
 *   (-1, 0) --- (+1, 0)    V=0  (line start)
 *   (-1, 1) --- (+1, 1)    V=1  (line end)
 */
static const float k_line_quad_verts[] = {
    -1.0f, 0.0f,   +1.0f, 0.0f,   +1.0f, 1.0f,   /* triangle 1 */
    -1.0f, 0.0f,   +1.0f, 1.0f,   -1.0f, 1.0f,   /* triangle 2 */
};

/* ======================================================================== */
/* Render target helpers                                                     */
/* ======================================================================== */

static rc_render_target make_scene_rt_(rc_vec2i size)
{
    return rc_render_target_make(&(rc_render_target_desc) {
        .size  = size,
        .color = {{ .format = RC_TEXTURE_FORMAT_RGBA8,
                    .filter = RC_TEXTURE_FILTER_LINEAR,
                    .wrap   = RC_TEXTURE_WRAP_CLAMP }},
        .depth = true,
    });
}

static rc_render_target make_blur_h_rt_(rc_vec2i size)
{
    return rc_render_target_make(&(rc_render_target_desc) {
        .size  = size,
        .color = {{ .format = RC_TEXTURE_FORMAT_RGBA8,
                    .filter = RC_TEXTURE_FILTER_LINEAR,
                    .wrap   = RC_TEXTURE_WRAP_CLAMP }},
        .depth = false,
    });
}

static void on_resize(void *ctx, rc_vec2i size)
{
    App *app = ctx;
    rc_render_target_destroy(app->scene_rt);
    rc_render_target_destroy(app->blur_h_rt);
    app->scene_rt  = make_scene_rt_(size);
    app->blur_h_rt = make_blur_h_rt_(size);
}

/* ======================================================================== */
/* Setup / teardown                                                          */
/* ======================================================================== */

static void setup(App *app, const rc_image_result *owl)
{
    rc_arena scratch = rc_arena_make_default();

    /* --- pentagram --- */
    app->line_shader = rc_shader_make(
        rc_str_make(k_line_vert_src), rc_str_make(k_line_frag_src), scratch);
    app->u_mvp = rc_shader_loc(app->line_shader, "u_mvp");

    app->line_quad_buf = rc_buffer_make(RC_BUFFER_STATIC);
    rc_buffer_upload(app->line_quad_buf,
                     k_line_quad_verts, (uint32_t)sizeof(k_line_quad_verts));

    make_pentagram_lines(app->lines, 280.0f, 1.75f, 0.0f);
    app->line_inst_buf = rc_buffer_make(RC_BUFFER_DYNAMIC);
    rc_buffer_upload(app->line_inst_buf, app->lines, (uint32_t)sizeof(app->lines));

    app->line_pipeline = rc_pipeline_make(&(rc_pipeline_desc) {
        .shader = app->line_shader,
        .buffer_layouts = {
            [0] = { .stride = sizeof(rc_vec2f), .divisor = 0 },
            [1] = { .stride = sizeof(line_t),   .divisor = 1 },
        },
        .attribs = {
            { .location = 0, .buffer_slot = 0, .format = RC_ATTRIB_FORMAT_FLOAT2, .offset = 0 },
            { .location = 1, .buffer_slot = 1, .format = RC_ATTRIB_FORMAT_FLOAT2, .offset = (uint32_t)offsetof(line_t, start)          },
            { .location = 2, .buffer_slot = 1, .format = RC_ATTRIB_FORMAT_FLOAT2, .offset = (uint32_t)offsetof(line_t, end)            },
            { .location = 3, .buffer_slot = 1, .format = RC_ATTRIB_FORMAT_FLOAT,  .offset = (uint32_t)offsetof(line_t, half_thickness) },
            { .location = 4, .buffer_slot = 1, .format = RC_ATTRIB_FORMAT_FLOAT4, .offset = (uint32_t)offsetof(line_t, color)          },
        },
        .blend = { .enabled = true },
    });

    /* --- textured quad --- */
    app->tex_shader = rc_shader_make(
        rc_str_make(k_tex_vert_src), rc_str_make(k_tex_frag_src), scratch);
    app->u_tex = rc_shader_loc(app->tex_shader, "u_tex");

    app->tex_quad_buf = rc_buffer_make(RC_BUFFER_STATIC);
    rc_buffer_upload(app->tex_quad_buf,
                     k_tex_quad_verts, (uint32_t)sizeof(k_tex_quad_verts));

    app->tex_pipeline = rc_pipeline_make(&(rc_pipeline_desc) {
        .shader = app->tex_shader,
        .buffer_layouts = {
            [0] = { .stride = sizeof(quad_vert_t), .divisor = 0 },
        },
        .attribs = {
            { .location = 0, .buffer_slot = 0, .format = RC_ATTRIB_FORMAT_FLOAT2,
              .offset = (uint32_t)offsetof(quad_vert_t, x) },
            { .location = 1, .buffer_slot = 0, .format = RC_ATTRIB_FORMAT_FLOAT2,
              .offset = (uint32_t)offsetof(quad_vert_t, u) },
        },
        .index_type = RC_INDEX_TYPE_NONE,
        .blend      = { .enabled = false },
    });

    /* Upload owl texture and set the sampler uniform once. */
    app->owl_tex = rc_texture_make(&(rc_texture_desc) {
        .size        = owl->image.size,
        .format      = RC_TEXTURE_FORMAT_RGBA8,
        .usage       = RC_TEXTURE_USAGE_STATIC,
        .filter      = RC_TEXTURE_FILTER_LINEAR,
        .wrap        = RC_TEXTURE_WRAP_CLAMP,
        .gen_mipmaps = true,
        .data        = owl->image.data.data,
    });

    /* Bind tex shader and set sampler to slot 0 (only needs to be done once). */
    rc_shader_bind(app->tex_shader);
    rc_shader_set_texture(app->u_tex, 0);

    /* --- font SDF atlas --- */

    {
        rc_arena font_arena   = rc_arena_make_default();
        rc_arena font_scratch = rc_arena_make_default();
        rc_font_atlas_result font = rc_font_atlas_make(
            "test/Roboto-Regular.ttf", 48, 12, &font_arena, font_scratch);
        rc_arena_destroy(&font_scratch);
        RC_PANIC(font.error == RC_FONT_OK);

        app->atlas_tex = rc_texture_make(&(rc_texture_desc) {
            .size        = font.atlas.atlas.size,
            .format      = RC_TEXTURE_FORMAT_R8,
            .usage       = RC_TEXTURE_USAGE_STATIC,
            .filter      = RC_TEXTURE_FILTER_LINEAR,
            .wrap        = RC_TEXTURE_WRAP_CLAMP,
            .gen_mipmaps = true,
            .data        = font.atlas.atlas.data.data,
        });

        /* Copy the atlas metrics (all plain values — no arena pointers).
         * atlas.atlas.data will dangle after font_arena is freed, but we
         * only use the glyph metrics and atlas.size from here on. */
        app->font = font.atlas;
        rc_arena_destroy(&font_arena);
    }

    app->atlas_quad_buf = rc_buffer_make(RC_BUFFER_STATIC);
    rc_buffer_upload(app->atlas_quad_buf,
                     k_atlas_quad_verts, (uint32_t)sizeof(k_atlas_quad_verts));

    /* --- glyph text pipeline --- */

    app->glyph_shader = rc_shader_make(
        rc_str_make(k_glyph_vert_src), rc_str_make(k_glyph_frag_src), scratch);
    app->u_glyph_tex        = rc_shader_loc(app->glyph_shader, "u_tex");
    app->u_glyph_resolution = rc_shader_loc(app->glyph_shader, "u_resolution");
    app->u_glyph_atlas_size = rc_shader_loc(app->glyph_shader, "u_atlas_size");
    app->u_glyph_color      = rc_shader_loc(app->glyph_shader, "u_color");
    app->u_glyph_sdf_spread = rc_shader_loc(app->glyph_shader, "u_sdf_spread");

    app->glyph_quad_buf = rc_buffer_make(RC_BUFFER_STATIC);
    rc_buffer_upload(app->glyph_quad_buf,
                     k_glyph_quad_verts, (uint32_t)sizeof(k_glyph_quad_verts));

    app->glyph_inst_buf = rc_buffer_make(RC_BUFFER_DYNAMIC);
    rc_buffer_upload(app->glyph_inst_buf, NULL,
                     MAX_GLYPH_INSTANCES * (uint32_t)sizeof(glyph_inst_t));

    app->glyph_pipeline = rc_pipeline_make(&(rc_pipeline_desc) {
        .shader = app->glyph_shader,
        .buffer_layouts = {
            [0] = { .stride = sizeof(float) * 2,    .divisor = 0 },
            [1] = { .stride = sizeof(glyph_inst_t), .divisor = 1 },
        },
        .attribs = {
            { .location = 0, .buffer_slot = 0, .format = RC_ATTRIB_FORMAT_FLOAT2,
              .offset = 0 },
            { .location = 1, .buffer_slot = 1, .format = RC_ATTRIB_FORMAT_FLOAT2,
              .offset = (uint32_t)offsetof(glyph_inst_t, pos_x) },
            { .location = 2, .buffer_slot = 1, .format = RC_ATTRIB_FORMAT_FLOAT2,
              .offset = (uint32_t)offsetof(glyph_inst_t, size_x) },
            { .location = 3, .buffer_slot = 1, .format = RC_ATTRIB_FORMAT_FLOAT,
              .offset = (uint32_t)offsetof(glyph_inst_t, scale) },
            { .location = 4, .buffer_slot = 1, .format = RC_ATTRIB_FORMAT_FLOAT2,
              .offset = (uint32_t)offsetof(glyph_inst_t, atlas_u) },
        },
        .blend = { .enabled = true },
    });

    /* Set static uniforms once — sampler slot and SDF spread. */
    rc_shader_bind(app->glyph_shader);
    rc_shader_set_texture(app->u_glyph_tex, 0);
    rc_shader_set_f32(app->u_glyph_sdf_spread, (float)app->font.sdf_spread);

    /* --- render targets --- */
    rc_vec2i win_size  = rc_app_size();
    app->scene_rt  = make_scene_rt_(win_size);
    app->blur_h_rt = make_blur_h_rt_(win_size);

    /* --- blur pass --- */
    app->blur_shader = rc_shader_make(
        rc_str_make(k_tex_vert_src), rc_str_make(k_blur_frag_src), scratch);
    app->u_blur_tex = rc_shader_loc(app->blur_shader, "u_tex");
    app->u_blur_dir = rc_shader_loc(app->blur_shader, "u_blur_dir");

    app->fullscreen_quad_buf = rc_buffer_make(RC_BUFFER_STATIC);
    rc_buffer_upload(app->fullscreen_quad_buf,
                     k_fullscreen_quad_verts, (uint32_t)sizeof(k_fullscreen_quad_verts));

    app->blur_rect_buf = rc_buffer_make(RC_BUFFER_DYNAMIC);
    {
        quad_vert_t verts[6];
        make_glass_rect_verts_(verts, (float)win_size.x, (float)win_size.y);
        rc_buffer_upload(app->blur_rect_buf, verts, sizeof(verts));
    }

    app->blur_pipeline = rc_pipeline_make(&(rc_pipeline_desc) {
        .shader = app->blur_shader,
        .buffer_layouts = {
            [0] = { .stride = sizeof(quad_vert_t), .divisor = 0 },
        },
        .attribs = {
            { .location = 0, .buffer_slot = 0, .format = RC_ATTRIB_FORMAT_FLOAT2,
              .offset = (uint32_t)offsetof(quad_vert_t, x) },
            { .location = 1, .buffer_slot = 0, .format = RC_ATTRIB_FORMAT_FLOAT2,
              .offset = (uint32_t)offsetof(quad_vert_t, u) },
        },
        .index_type = RC_INDEX_TYPE_NONE,
        .blend      = { .enabled = false },
    });

    /* Set sampler uniform once — slot 0. */
    rc_shader_bind(app->blur_shader);
    rc_shader_set_texture(app->u_blur_tex, 0);

    rc_arena_destroy(&scratch);
}

static void teardown(App *app)
{
    rc_pipeline_destroy(app->blur_pipeline);
    rc_buffer_destroy(app->blur_rect_buf);
    rc_buffer_destroy(app->fullscreen_quad_buf);
    rc_shader_destroy(app->blur_shader);
    rc_render_target_destroy(app->blur_h_rt);
    rc_render_target_destroy(app->scene_rt);

    rc_pipeline_destroy(app->glyph_pipeline);
    rc_buffer_destroy(app->glyph_inst_buf);
    rc_buffer_destroy(app->glyph_quad_buf);
    rc_shader_destroy(app->glyph_shader);

    rc_buffer_destroy(app->atlas_quad_buf);
    rc_texture_destroy(app->atlas_tex);
    rc_texture_destroy(app->owl_tex);
    rc_pipeline_destroy(app->tex_pipeline);
    rc_buffer_destroy(app->tex_quad_buf);
    rc_shader_destroy(app->tex_shader);

    rc_pipeline_destroy(app->line_pipeline);
    rc_buffer_destroy(app->line_inst_buf);
    rc_buffer_destroy(app->line_quad_buf);
    rc_shader_destroy(app->line_shader);
}

/* ======================================================================== */
/* Text rendering                                                            */
/* ======================================================================== */

/*
 * Render a null-terminated ASCII string using the glyph SDF pipeline.
 *
 * cursor_x    — pixel X of the glyph cursor (left edge + advance tracking)
 * baseline_y  — pixel Y of the text baseline (screen Y-down)
 * scale       — uniform scale; rendered pixel size = atlas texel size × scale
 * color       — RGBA tint; alpha is multiplied by the SDF-derived alpha
 *
 * Characters outside the atlas range (ASCII 33–126) are treated as spaces
 * except for the literal space (ASCII 32) which advances the cursor by
 * 0.25 × pixel_height × scale.
 *
 * The function updates the dynamic instance buffer and issues one draw call.
 * Call rc_gfx_apply_pipeline(glyph_pipeline) is handled internally.
 */
static void render_string(App *app, const char *str,
                          float cursor_x, float baseline_y,
                          float scale, rc_color color)
{
    glyph_inst_t insts[MAX_GLYPH_INSTANCES];
    uint32_t count = 0;

    const rc_font_atlas *f = &app->font;
    for (const char *p = str; *p && count < MAX_GLYPH_INSTANCES; p++) {
        uint8_t c = (uint8_t)*p;
        if (c == ' ') {
            cursor_x += (float)f->pixel_height * 0.25f * scale;
            continue;
        }
        if (c < RC_FONT_FIRST_GLYPH || c > RC_FONT_LAST_GLYPH) continue;

        const rc_glyph_metrics *g = &f->glyphs[c - RC_FONT_FIRST_GLYPH];
        rc_vec2i sz = rc_box2i_size(g->atlas_rect);

        insts[count++] = (glyph_inst_t) {
            .pos_x   = cursor_x + (float)g->offset_x * scale,
            .pos_y   = baseline_y - (float)g->offset_y * scale,
            .size_x  = (float)sz.x,
            .size_y  = (float)sz.y,
            .scale   = scale,
            .atlas_u = (float)g->atlas_rect.min.x,
            .atlas_v = (float)g->atlas_rect.min.y,
        };
        cursor_x += (float)g->advance * scale;
    }

    if (count == 0) return;

    rc_vec2i win_sz   = rc_app_size();
    rc_vec2i atlas_sz = app->font.atlas.size;

    rc_buffer_update(app->glyph_inst_buf,
                     insts, count * (uint32_t)sizeof(glyph_inst_t));

    rc_gfx_apply_pipeline(app->glyph_pipeline);
    rc_shader_set_vec2(app->u_glyph_resolution,
                       rc_vec2f_make((float)win_sz.x, (float)win_sz.y));
    rc_shader_set_vec2(app->u_glyph_atlas_size,
                       rc_vec2f_make((float)atlas_sz.x, (float)atlas_sz.y));
    rc_shader_set_vec4(app->u_glyph_color,
                       (rc_vec4f) {color.r, color.g, color.b, color.a});
    rc_gfx_apply_bindings(&(rc_bindings) {
        .vertex_buffers = { app->glyph_quad_buf, app->glyph_inst_buf },
        .textures       = { app->atlas_tex },
    });
    rc_gfx_draw(0, 6, count);
}

/* ======================================================================== */
/* Render callback                                                           */
/* ======================================================================== */

static void on_render(void *ctx)
{
    App *app = ctx;

    rc_vec2i sz = rc_app_size();
    float hw = (float)sz.x * 0.5f;
    float hh = (float)sz.y * 0.5f;
    rc_mat44f proj = rc_mat44f_make_ortho(-hw, hw, hh, -hh, -1.0f, 1.0f);

    make_pentagram_lines(app->lines, 280.0f, 1.75f, (float)rc_app_time());
    rc_buffer_update(app->line_inst_buf, app->lines, (uint32_t)sizeof(app->lines));

    /* ---- pass 1: render scene to scene_rt ---- */
    rc_gfx_begin_render_target(app->scene_rt);

    /* Linear mid-grey: sRGB 0.5 linearised via ((0.5 + 0.055) / 1.055)^2.4. */
    rc_gfx_clear(rc_color_make_rgb(0.214f, 0.214f, 0.214f));
    rc_gfx_clear_depth();

    rc_gfx_apply_pipeline(app->line_pipeline);
    rc_shader_set_mat44(app->u_mvp, proj);
    rc_gfx_apply_bindings(&(rc_bindings) {
        .vertex_buffers = { app->line_quad_buf, app->line_inst_buf },
    });
    rc_gfx_draw(0, 6, 5);

    rc_gfx_apply_pipeline(app->tex_pipeline);
    rc_gfx_apply_bindings(&(rc_bindings) {
        .vertex_buffers = { app->tex_quad_buf },
        .textures       = { app->owl_tex },
    });
    rc_gfx_draw(0, 6, 1);

    rc_gfx_apply_bindings(&(rc_bindings) {
        .vertex_buffers = { app->atlas_quad_buf },
        .textures       = { app->atlas_tex },
    });
    rc_gfx_draw(0, 6, 1);

    rc_gfx_end_render_target();

    /* ---- pass 2: horizontal Gaussian blur scene_rt → blur_h_rt ---- */
    rc_gfx_begin_render_target(app->blur_h_rt);

    rc_gfx_apply_pipeline(app->blur_pipeline);
    rc_shader_set_vec2(app->u_blur_dir, rc_vec2f_make(3.0f, 0.0f));
    rc_gfx_apply_bindings(&(rc_bindings) {
        .vertex_buffers = { app->fullscreen_quad_buf },
        .textures       = { rc_render_target_color(app->scene_rt, 0) },
    });
    rc_gfx_draw(0, 6, 1);

    rc_gfx_end_render_target();

    /* ---- composite to default framebuffer ---- */

    /* Blit scene_rt to screen. */
    rc_gfx_apply_pipeline(app->tex_pipeline);
    rc_gfx_apply_bindings(&(rc_bindings) {
        .vertex_buffers = { app->fullscreen_quad_buf },
        .textures       = { rc_render_target_color(app->scene_rt, 0) },
    });
    rc_gfx_draw(0, 6, 1);

    /* Recompute blur rect for the current window size (fixed pixel coords). */
    {
        quad_vert_t verts[6];
        make_glass_rect_verts_(verts, (float)sz.x, (float)sz.y);
        rc_buffer_update(app->blur_rect_buf, verts, sizeof(verts));
    }

    /* Vertical blur pass: sample blur_h_rt and draw directly to screen
     * at the blur rect position, completing the two-pass separable Gaussian. */
    rc_gfx_apply_pipeline(app->blur_pipeline);
    rc_shader_set_vec2(app->u_blur_dir, rc_vec2f_make(0.0f, 3.0f));
    rc_gfx_apply_bindings(&(rc_bindings) {
        .vertex_buffers = { app->blur_rect_buf },
        .textures       = { rc_render_target_color(app->blur_h_rt, 0) },
    });
    rc_gfx_draw(0, 6, 1);

    /* ---- text overlay (inside glass rect, top-left) ---- */

    render_string(app, "Crappy Birthday!",
                  GLASS_X0 + 15.0f, GLASS_Y0 + 28.0f, 0.375f,
                  rc_color_make(0.0f, 0.0f, 0.0f, 1.0f));
}

/* ======================================================================== */
/* main                                                                      */
/* ======================================================================== */

int main(void)
{
    rc_app_init(&(rc_app_desc) {
        .title     = RC_STR("richc-app test"),
        .size      = { 1280, 720 },
        .resizable = true,
        .srgb      = true,
        .callbacks = {
            .ctx       = &g_app,
            .on_render = on_render,
            .on_resize = on_resize,
        },
    });

    /* Load owl.png and verify it decoded correctly. */
    rc_arena image_arena  = rc_arena_make_default();
    rc_arena image_scratch = rc_arena_make_default();
    rc_image_result owl = rc_image_load_png(
        "test/owl.png", &image_arena, image_scratch);
    rc_arena_destroy(&image_scratch);
    RC_PANIC(owl.error == RC_IMAGE_OK);
    RC_PANIC(owl.image.size.x == 512);
    RC_PANIC(owl.image.size.y == 512);
    RC_PANIC(owl.image.format == RC_PIXEL_FORMAT_RGBA8);
    RC_PANIC(owl.image.data.num == 512 * 512 * 4);

    setup(&g_app, &owl);

    while (rc_app_is_running()) {
        rc_app_poll();
        rc_app_request_render();
    }

    teardown(&g_app);
    rc_app_destroy();
    rc_arena_destroy(&image_arena);
    return 0;
}
