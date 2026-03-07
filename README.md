# richc-app

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

A C17 library providing windowed application scaffolding, input handling, and OpenGL 3.3 graphics helpers. Built on top of [richc](https://github.com/waitingforvsync/richc).

GLFW and OpenGL are private implementation details — they never appear in the public API.

## Public API

### `include/richc/app/app.h` — Window and event loop

```c
void     rc_app_init           (const rc_app_desc *desc);
void     rc_app_destroy        (void);
void     rc_app_poll           (void);
bool     rc_app_is_running     (void);
rc_vec2i rc_app_size           (void);
void     rc_app_request_update (void);   /* fires on_update(ctx, dt) */
void     rc_app_request_render (void);   /* fires on_render(ctx), swaps */
void     rc_app_swap_buffers   (void);   /* for render-thread use */
double   rc_app_time           (void);   /* seconds since rc_app_init */
```

Callbacks are declared in `rc_app_callbacks` and passed via `rc_app_desc`:

```c
rc_app_init(&(rc_app_desc) {
    .title     = RC_STR("My App"),
    .width     = 1280,
    .height    = 720,
    .resizable = true,
    .srgb      = true,
    .callbacks = {
        .ctx       = &my_state,
        .on_render = my_render,
        .on_key_down = my_key_down,
    },
});
```

Available callbacks: `on_key_down`, `on_key_up`, `on_key_char`, `on_mouse_down`, `on_mouse_up`, `on_mouse_enter`, `on_mouse_leave`, `on_mouse_move`, `on_mouse_wheel`, `on_resize`, `on_focus_gained`, `on_focus_lost`, `on_minimize`, `on_maximize`, `on_update`, `on_render`.

### `include/richc/app/keys.h` — Input types

- `rc_scancode` — physical key codes (layout-independent, values match GLFW)
- `rc_mod` — modifier key flags (`RC_MOD_SHIFT`, `RC_MOD_CTRL`, `RC_MOD_ALT`, …)
- `rc_mouse_button` — `RC_MOUSE_BUTTON_LEFT/RIGHT/MIDDLE`

### `include/richc/gfx/gfx.h` — Frame operations

```c
void rc_gfx_viewport   (rc_vec2i size);
void rc_gfx_clear      (rc_color color);
void rc_gfx_clear_depth(void);
```

Colors are linear RGBA floats; when sRGB is enabled the GPU encodes them on write.

### `include/richc/gfx/shader.h` — GLSL shaders

```c
rc_shader      rc_shader_make     (rc_str vert_src, rc_str frag_src, rc_arena scratch);
void           rc_shader_destroy  (rc_shader sh);
rc_uniform_loc rc_shader_loc      (rc_shader sh, const char *name);
void           rc_shader_set_f32  (rc_uniform_loc loc, float v);
void           rc_shader_set_i32  (rc_uniform_loc loc, int32_t v);
void           rc_shader_set_vec2 (rc_uniform_loc loc, rc_vec2f v);
void           rc_shader_set_vec3 (rc_uniform_loc loc, rc_vec3f v);
void           rc_shader_set_vec4 (rc_uniform_loc loc, rc_vec4f v);
void           rc_shader_set_mat44(rc_uniform_loc loc, rc_mat44f m);
```

`rc_shader_make` RC_PANICs on compile or link failure, printing the GL info log to stderr. Query uniform locations once at startup and cache them; a loc with `.loc == -1` (not found) is silently ignored by the setters. Uniform setters must be called after `rc_gfx_apply_pipeline`.

### `include/richc/gfx/buffer.h` — GPU buffers

```c
rc_buffer rc_buffer_make  (rc_buffer_usage usage);          /* STATIC or DYNAMIC */
void      rc_buffer_upload(rc_buffer buf, const void *data, uint32_t size); /* glBufferData    */
void      rc_buffer_update(rc_buffer buf, const void *data, uint32_t size); /* glBufferSubData */
void      rc_buffer_destroy(rc_buffer buf);
```

`rc_buffer_upload` allocates or reallocates GPU storage — use for the initial upload or when the size changes. `rc_buffer_update` writes into existing storage without reallocation — use for per-frame updates of dynamic buffers.

### `include/richc/gfx/pipeline.h` — Pipeline, bindings, and draw calls

A pipeline bakes together a shader, vertex buffer layouts, attribute formats, index type, and blend state. Buffers are supplied separately at draw time via `rc_bindings`, so the same pipeline can draw different meshes.

```c
rc_pipeline rc_pipeline_make   (const rc_pipeline_desc *desc);
void        rc_pipeline_destroy(rc_pipeline pip);
void        rc_gfx_apply_pipeline(rc_pipeline pip);
void        rc_gfx_apply_bindings(const rc_bindings *bind);
void        rc_gfx_draw(uint32_t first, uint32_t count, uint32_t instances);
```

`rc_pipeline_desc` combines the shader with up to four buffer slot layouts and up to sixteen attribute descriptors. The attrib list is terminated by an entry with `format == 0`:

```c
rc_pipeline pip = rc_pipeline_make(&(rc_pipeline_desc) {
    .shader = sh,
    .buffer_layouts = {
        [0] = { .stride = sizeof(rc_vec2f), .divisor = 0 },  /* per-vertex  */
        [1] = { .stride = sizeof(MyInst),   .divisor = 1 },  /* per-instance */
    },
    .attribs = {
        { .location = 0, .buffer_slot = 0, .format = RC_ATTRIB_FORMAT_FLOAT2, .offset = 0 },
        { .location = 1, .buffer_slot = 1, .format = RC_ATTRIB_FORMAT_FLOAT2, .offset = offsetof(MyInst, pos) },
    },
    .index_type = RC_INDEX_TYPE_NONE,
    .blend      = { .enabled = true },
});
```

`rc_attrib_format` tokens: `RC_ATTRIB_FORMAT_FLOAT`, `FLOAT2`, `FLOAT3`, `FLOAT4`.
`rc_index_type` tokens: `RC_INDEX_TYPE_NONE`, `RC_INDEX_TYPE_UINT16`, `RC_INDEX_TYPE_UINT32`.

Each frame, apply the pipeline first (binds shader and blend state), supply buffer handles via bindings, then draw. Use `instances == 1` for non-instanced draws:

```c
rc_gfx_apply_pipeline(pip);
rc_shader_set_mat44(u_mvp, proj);
rc_gfx_apply_bindings(&(rc_bindings) {
    .vertex_buffers = { quad_buf, inst_buf },
});
rc_gfx_draw(0, 6, 5);   /* 6 vertices, 5 instances */
```

All draws emit `GL_TRIANGLES`. When an index type other than `NONE` is set, `rc_gfx_draw` uses the index buffer from `rc_bindings.index_buffer`.

## Minimal example

```c
#include "richc/app/app.h"
#include "richc/gfx/gfx.h"

static void on_render(void *ctx)
{
    (void)ctx;
    rc_gfx_clear(rc_color_make_rgb(0.214f, 0.214f, 0.214f));
}

int main(void)
{
    rc_app_init(&(rc_app_desc) {
        .title     = RC_STR("Hello"),
        .width     = 1280,
        .height    = 720,
        .resizable = true,
        .srgb      = true,
        .callbacks = { .on_render = on_render },
    });

    while (rc_app_is_running()) {
        rc_app_poll();
        rc_app_request_render();
    }

    rc_app_destroy();
    return 0;
}
```

## Dependencies

| Submodule | Version | Role |
|-----------|---------|------|
| `extern/richc` | v0.1 | Core types (`rc_str`, `rc_arena`, `rc_vec2i`, …) |
| `extern/glfw`  | 3.4 | Window creation, input, GL context |
| `extern/glad`  | glad2 | OpenGL 3.3 core loader (generated at configure time) |

## Building

Requires CMake ≥ 3.21, Python 3 on PATH (for glad code generation), and a C17-capable compiler.

```sh
# Clone with submodules
git clone --recurse-submodules <url>

# Configure and build
cmake -B build -G Ninja -DCMAKE_C_COMPILER=clang
cmake --build build
```
