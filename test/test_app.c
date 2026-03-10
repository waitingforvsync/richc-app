/*
 * test_app.c - pentagram + textured owl quad.
 *
 * Left side: owl.png rendered as a filtered 128×128-NDC-unit square.
 * Right side: the animated pentagram (5 anti-aliased instanced lines).
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
#include "richc/image/image_pack.h"
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
    "    frag_color = col * vec4(0.85, 0.85, 0.85, 1.0);\n"
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

    /* --- packed atlas quad --- */
    rc_texture     atlas_tex;
    rc_buffer      atlas_quad_buf;

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
 * Blur rect: a vertical strip through the centre of the screen.
 * NDC x in [-0.6, 0.1], y in [-0.4, 0.35] — overlaps the right edge of the owl.
 * UVs computed as (ndc + 1) / 2, matching the RT's bottom-up layout.
 */
static const quad_vert_t k_blur_rect_verts[] = {
    /* triangle 1 */
    { -0.6f, -0.4f,  0.2f,   0.3f   },
    {  0.1f, -0.4f,  0.55f,  0.3f   },
    {  0.1f,  0.35f, 0.55f,  0.675f },
    /* triangle 2 */
    { -0.6f, -0.4f,  0.2f,   0.3f   },
    {  0.1f,  0.35f, 0.55f,  0.675f },
    { -0.6f,  0.35f, 0.2f,   0.675f },
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

    /* --- packed atlas --- */

    /* 24 source images: varied sizes and solid RGB8 fill colours. */
    static const struct { rc_vec2i size; uint8_t r, g, b; } k_items[24] = {
        { {80, 40},  220,  50,  50 },  /* red          */
        { {50, 60},   50, 200,  50 },  /* green        */
        { {70, 30},   50,  50, 220 },  /* blue         */
        { {40, 80},  220, 180,  50 },  /* yellow       */
        { {60, 50},  180,  50, 220 },  /* purple       */
        { {30, 30},   50, 200, 200 },  /* cyan         */
        { {90, 20},  220, 120,  50 },  /* orange       */
        { {25, 70},  180, 220,  50 },  /* lime         */
        { {55, 45},   50, 120, 220 },  /* sky blue     */
        { {45, 55},  220,  50, 140 },  /* pink         */
        { {35, 65},  140, 200, 180 },  /* teal         */
        { {65, 35},  200, 140,  80 },  /* tan          */
        { {20, 90},  100,  80, 200 },  /* indigo       */
        { {75, 25},  200,  80, 100 },  /* rose         */
        { {50, 50},  100, 200, 100 },  /* mint         */
        { {40, 40},  220, 200, 160 },  /* cream        */
        { {60, 20},  160,  60, 100 },  /* maroon       */
        { {20, 60},   80, 160,  80 },  /* forest       */
        { {30, 50},  160, 140, 200 },  /* lavender     */
        { {50, 30},  200, 160,  60 },  /* gold         */
        { {35, 20},  240, 100, 180 },  /* hot pink     */
        { {20, 35},   80, 200, 240 },  /* aqua         */
        { {45, 25},  120,  80,  40 },  /* brown        */
        { {25, 45},  180, 180, 180 },  /* silver       */
    };

    rc_arena pack_arena   = rc_arena_make_default();
    rc_arena pack_scratch = rc_arena_make_default();

    rc_image src_images[24];
    for (int i = 0; i < 24; i++) {
        uint8_t px[3] = {k_items[i].r, k_items[i].g, k_items[i].b};
        src_images[i] = rc_image_make(k_items[i].size,
                                       RC_PIXEL_FORMAT_RGB8, px, &pack_arena);
    }

    rc_view_image src_view = {src_images, 24};
    rc_image_pack_result pack = rc_image_pack(
        src_view, rc_vec2i_make(256, 256), 1, &pack_arena, pack_scratch);
    rc_arena_destroy(&pack_scratch);
    RC_PANIC(pack.image.data.data != NULL);

    app->atlas_tex = rc_texture_make(&(rc_texture_desc) {
        .size   = pack.image.size,
        .format = (rc_texture_format)pack.image.format,
        .usage  = RC_TEXTURE_USAGE_STATIC,
        .filter = RC_TEXTURE_FILTER_NEAREST,
        .wrap   = RC_TEXTURE_WRAP_CLAMP,
        .data   = pack.image.data.data,
    });
    /* Atlas pixels were the last allocation; reclaim them now the GPU has a copy. */
    rc_arena_free(&pack_arena, pack.image.data.data, pack.image.data.num);
    rc_arena_destroy(&pack_arena);

    app->atlas_quad_buf = rc_buffer_make(RC_BUFFER_STATIC);
    rc_buffer_upload(app->atlas_quad_buf,
                     k_atlas_quad_verts, (uint32_t)sizeof(k_atlas_quad_verts));

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

    app->blur_rect_buf = rc_buffer_make(RC_BUFFER_STATIC);
    rc_buffer_upload(app->blur_rect_buf,
                     k_blur_rect_verts, (uint32_t)sizeof(k_blur_rect_verts));

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

    /* Vertical blur pass: sample blur_h_rt and draw directly to screen
     * at the blur rect position, completing the two-pass separable Gaussian. */
    rc_gfx_apply_pipeline(app->blur_pipeline);
    rc_shader_set_vec2(app->u_blur_dir, rc_vec2f_make(0.0f, 3.0f));
    rc_gfx_apply_bindings(&(rc_bindings) {
        .vertex_buffers = { app->blur_rect_buf },
        .textures       = { rc_render_target_color(app->blur_h_rt, 0) },
    });
    rc_gfx_draw(0, 6, 1);
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
