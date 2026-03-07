/*
 * test_app.c - pentagram drawn with anti-aliased instanced lines.
 *
 * Each of the 5 line segments is submitted as one instance of a unit quad.
 * The vertex shader maps the quad's (U, V) UVs to world-space positions using
 * the per-instance start/end and half-thickness vectors.  The fragment shader
 * derives alpha from fwidth(U) to produce a 1-pixel soft edge.
 *
 * No GL calls appear here; everything goes through the rc_gfx / rc_shader /
 * rc_buffer / rc_vertex_array / rc_app APIs.
 */

#include "richc/app/app.h"
#include "richc/app/keys.h"
#include "richc/gfx/gfx.h"
#include "richc/gfx/shader.h"
#include "richc/gfx/buffer.h"
#include "richc/gfx/pipeline.h"
#include "richc/math/mat44f.h"
#include "richc/math/math.h"
#include "richc/math/vec2f.h"
#include "richc/image/image.h"
#include "richc/str.h"
#include "richc/arena.h"
#include "richc/debug.h"
#include <math.h>
#include <stddef.h>   /* offsetof */

/* ---- GLSL shaders ---- */

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
static const char k_vert_src[] =
    "#version 330 core\n"
    "\n"
    "layout(location = 0) in vec2 a_uv;\n"
    "layout(location = 1) in vec2 a_start;\n"
    "layout(location = 2) in vec2 a_end;\n"
    "layout(location = 3) in vec2 a_half_thick;\n"
    "layout(location = 4) in vec4 a_color;\n"
    "\n"
    "uniform mat4 u_mvp;\n"
    "\n"
    "out vec2 v_uv;\n"
    "out vec4 v_color;\n"
    "\n"
    "void main()\n"
    "{\n"
    "    vec2 pos    = mix(a_start, a_end, a_uv.y) + a_half_thick * a_uv.x;\n"
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
static const char k_frag_src[] =
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

/* ---- line instance struct ---- */

/*
 * One entry per line segment.  Layout must match the attribute descriptors
 * given to rc_vertex_array_make and the per-instance inputs in k_vert_src.
 */
typedef struct {
    rc_vec2f start;           /* world-space start point */
    rc_vec2f end;             /* world-space end point   */
    rc_vec2f half_thickness;  /* perpendicular half-width vector */
    rc_color color;           /* RGBA (float) */
} line_t;

/* ---- application context ---- */

typedef struct {
    rc_shader       shader;
    rc_uniform_loc  u_mvp;
    rc_buffer       quad_buf;
    rc_buffer       line_buf;
    rc_pipeline     pipeline;
    line_t          lines[5];
} App;

static App g_app;

/* ---- pentagram geometry ---- */

/*
 * Build the 5 line segments of a pentagram inscribed in a circle of the given
 * radius (pixels).  half_width is the perpendicular half-thickness in pixels.
 * The star is drawn by connecting every other vertex: 0->2->4->1->3->0.
 */
static void make_pentagram_lines(line_t *out, float radius, float half_width, float time)
{
    /* 5 vertices oscillating along the circle, each 72 degrees out of phase. */
    rc_vec2f pts[5];
    for (int i = 0; i < 5; i++) {
        float base  = rc_deg_to_rad(-90.0f + (float)i * 72.0f);
        float phase = rc_deg_to_rad((float)i * 72.0f);
        float angle = base + rc_deg_to_rad(15.0f) * sinf(2.0f * time + phase);
        pts[i] = rc_vec2f_scalar_mul(rc_vec2f_make_cossin(angle), radius);
    }

    /* Connect alternating vertices to form the star. */
    static const int order[6] = {0, 2, 4, 1, 3, 0};
    for (int i = 0; i < 5; i++) {
        rc_vec2f start = pts[order[i]];
        rc_vec2f end   = pts[order[i + 1]];
        rc_vec2f dir   = rc_vec2f_sub(end, start);
        rc_vec2f perp  = rc_vec2f_scalar_mul(
                             rc_vec2f_normalize(rc_vec2f_perp(dir)),
                             half_width);
        out[i] = (line_t) {
            .start          = start,
            .end            = end,
            .half_thickness = perp,
            .color          = rc_color_make(0.0f, 0.0f, 0.0f, 1.0f),
        };
    }
}

/* ---- setup / teardown ---- */

/*
 * Unit quad: 6 vertices (two triangles).
 *   (-1, 0) --- (+1, 0)    V=0  (line start)
 *   (-1, 1) --- (+1, 1)    V=1  (line end)
 * U spans [-1, +1] across the line width; the vertex shader offsets
 * by half_thick * U to produce the actual screen position.
 */
static const float k_quad_verts[] = {
    -1.0f, 0.0f,   +1.0f, 0.0f,   +1.0f, 1.0f,   /* triangle 1 */
    -1.0f, 0.0f,   +1.0f, 1.0f,   -1.0f, 1.0f,   /* triangle 2 */
};

static void setup(App *app)
{
    /* compile and link the line shader */
    rc_arena scratch = rc_arena_make_default();
    app->shader = rc_shader_make(rc_str_make(k_vert_src), rc_str_make(k_frag_src), scratch);
    rc_arena_destroy(&scratch);

    app->u_mvp = rc_shader_loc(app->shader, "u_mvp");

    /* static quad buffer (shared template for all line instances) */
    app->quad_buf = rc_buffer_make(RC_BUFFER_STATIC);
    rc_buffer_upload(app->quad_buf, k_quad_verts, (uint32_t)sizeof(k_quad_verts));

    /* pentagram line instances — initial upload to size the buffer */
    make_pentagram_lines(app->lines, 280.0f, 1.75f, 0.0f);
    app->line_buf = rc_buffer_make(RC_BUFFER_DYNAMIC);
    rc_buffer_upload(app->line_buf, app->lines, (uint32_t)sizeof(app->lines));

    /* pipeline: shader + vertex layout + blend state */
    app->pipeline = rc_pipeline_make(&(rc_pipeline_desc) {
        .shader = app->shader,
        .buffer_layouts = {
            [0] = { .stride = sizeof(rc_vec2f),  .divisor = 0 },   /* slot 0: per-vertex quad UV */
            [1] = { .stride = sizeof(line_t),    .divisor = 1 },   /* slot 1: per-instance line data */
        },
        .attribs = {
            { .location = 0, .buffer_slot = 0, .format = RC_ATTRIB_FORMAT_FLOAT2, .offset = 0 },
            { .location = 1, .buffer_slot = 1, .format = RC_ATTRIB_FORMAT_FLOAT2, .offset = (uint32_t)offsetof(line_t, start)          },
            { .location = 2, .buffer_slot = 1, .format = RC_ATTRIB_FORMAT_FLOAT2, .offset = (uint32_t)offsetof(line_t, end)            },
            { .location = 3, .buffer_slot = 1, .format = RC_ATTRIB_FORMAT_FLOAT2, .offset = (uint32_t)offsetof(line_t, half_thickness) },
            { .location = 4, .buffer_slot = 1, .format = RC_ATTRIB_FORMAT_FLOAT4, .offset = (uint32_t)offsetof(line_t, color)          },
        },
        .blend = { .enabled = true },
    });
}

static void teardown(App *app)
{
    rc_pipeline_destroy(app->pipeline);
    rc_buffer_destroy(app->line_buf);
    rc_buffer_destroy(app->quad_buf);
    rc_shader_destroy(app->shader);
}

/* ---- render callback ---- */

static void on_render(void *ctx)
{
    App *app = ctx;

    /* Linear mid-grey: sRGB 0.5 linearised via ((0.5 + 0.055) / 1.055)^2.4. */
    rc_gfx_clear(rc_color_make_rgb(0.214f, 0.214f, 0.214f));

    /* Recompute and upload animated line instances. */
    make_pentagram_lines(app->lines, 280.0f, 1.75f, (float)rc_app_time());
    rc_buffer_update(app->line_buf, app->lines, (uint32_t)sizeof(app->lines));

    /* orthographic projection: pixel coords, origin at window centre, y up */
    rc_vec2i sz = rc_app_size();
    float hw = (float)sz.x * 0.5f;
    float hh = (float)sz.y * 0.5f;
    rc_mat44f proj = rc_mat44f_make_ortho(-hw, hw, hh, -hh, -1.0f, 1.0f);

    rc_gfx_apply_pipeline(app->pipeline);
    rc_shader_set_mat44(app->u_mvp, proj);
    rc_gfx_apply_bindings(&(rc_bindings) {
        .vertex_buffers = { app->quad_buf, app->line_buf },
    });
    rc_gfx_draw(0, 6, 5);
}

/* ---- main ---- */

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

    setup(&g_app);

    /* Load owl.png from the test directory and verify it decoded correctly. */
    rc_arena image_arena  = rc_arena_make_default();
    rc_arena image_scratch = rc_arena_make_default();
    rc_image_result owl = rc_image_load_png(
        "test/owl.png", &image_arena, image_scratch);
    rc_arena_destroy(&image_scratch);
    RC_PANIC(owl.error == RC_IMAGE_OK);
    RC_PANIC(owl.image.width  == 512);
    RC_PANIC(owl.image.height == 512);
    RC_PANIC(owl.image.format == RC_PIXEL_FORMAT_RGBA8);
    RC_PANIC(owl.image.data.num == 512 * 512 * 4);

    while (rc_app_is_running()) {
        rc_app_poll();
        rc_app_request_render();
    }

    teardown(&g_app);
    rc_app_destroy();
    rc_arena_destroy(&image_arena);
    return 0;
}
