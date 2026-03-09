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
#include "richc/math/mat44f.h"
#include "richc/math/math.h"
#include "richc/math/vec2f.h"
#include "richc/image/image.h"
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

    rc_arena_destroy(&scratch);
}

static void teardown(App *app)
{
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

    /* Linear mid-grey: sRGB 0.5 linearised via ((0.5 + 0.055) / 1.055)^2.4. */
    rc_gfx_clear(rc_color_make_rgb(0.214f, 0.214f, 0.214f));

    /* --- pentagram --- */
    make_pentagram_lines(app->lines, 280.0f, 1.75f, (float)rc_app_time());
    rc_buffer_update(app->line_inst_buf, app->lines, (uint32_t)sizeof(app->lines));

    rc_vec2i sz = rc_app_size();
    float hw = (float)sz.x * 0.5f;
    float hh = (float)sz.y * 0.5f;
    rc_mat44f proj = rc_mat44f_make_ortho(-hw, hw, hh, -hh, -1.0f, 1.0f);

    rc_gfx_apply_pipeline(app->line_pipeline);
    rc_shader_set_mat44(app->u_mvp, proj);
    rc_gfx_apply_bindings(&(rc_bindings) {
        .vertex_buffers = { app->line_quad_buf, app->line_inst_buf },
    });
    rc_gfx_draw(0, 6, 5);

    /* --- textured owl quad --- */
    rc_gfx_apply_pipeline(app->tex_pipeline);
    rc_gfx_apply_bindings(&(rc_bindings) {
        .vertex_buffers = { app->tex_quad_buf },
        .textures       = { app->owl_tex },
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
        .width     = 1280,
        .height    = 720,
        .resizable = true,
        .srgb      = true,
        .callbacks = {
            .ctx       = &g_app,
            .on_render = on_render,
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
