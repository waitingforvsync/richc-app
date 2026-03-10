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
    .size      = { 1280, 720 },
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
void           rc_shader_set_mat44   (rc_uniform_loc loc, rc_mat44f m);
void           rc_shader_set_texture (rc_uniform_loc loc, int32_t slot);
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

Each frame, apply the pipeline first (binds shader and blend state), supply buffer and texture handles via bindings, then draw. Use `instances == 1` for non-instanced draws:

```c
rc_gfx_apply_pipeline(pip);
rc_shader_set_mat44(u_mvp, proj);
rc_gfx_apply_bindings(&(rc_bindings) {
    .vertex_buffers = { quad_buf, inst_buf },
    .textures       = { albedo_tex },        /* slot 0 */
});
rc_gfx_draw(0, 6, 5);   /* 6 vertices, 5 instances */
```

`rc_bindings` holds up to four vertex buffer handles, one index buffer handle, and up to eight texture handles (slots 0–7). Slots left as `{0}` are not touched. All draws emit `GL_TRIANGLES`. When an index type other than `NONE` is set, `rc_gfx_draw` uses `rc_bindings.index_buffer`.

### `include/richc/gfx/texture.h` — GPU textures

```c
rc_texture rc_texture_make   (const rc_texture_desc *desc);
void       rc_texture_update (rc_texture tex, const void *data);
void       rc_texture_destroy(rc_texture tex);
```

`rc_texture_desc` fields:

| Field | Type | Notes |
|-------|------|-------|
| `size` | `rc_vec2i` | Width (`.x`) and height (`.y`) in pixels |
| `format` | `rc_texture_format` | `R8`, `RGB8`, or `RGBA8` — numeric values match `rc_pixel_format` so a cast is valid |
| `usage` | `rc_texture_usage` | `STATIC` or `DYNAMIC` |
| `wrap` | `rc_texture_wrap` | `REPEAT`, `CLAMP`, or `MIRROR` — applies to both U and V |
| `filter` | `rc_texture_filter` | `NEAREST` or `LINEAR` — applies to both min and mag |
| `gen_mipmaps` | `bool` | Generate full mip chain on upload (and on `rc_texture_update`) |
| `data` | `const void *` | Initial pixel data; `NULL` allocates an empty texture |

`rc_texture_update` replaces the full image (same dimensions and format) and regenerates mipmaps if `gen_mipmaps` was set at creation time.

Typical usage with an `rc_image`:

```c
rc_texture tex = rc_texture_make(&(rc_texture_desc) {
    .size        = img.size,
    .format      = (rc_texture_format)img.format,   /* cast valid: same values */
    .usage       = RC_TEXTURE_USAGE_STATIC,
    .filter      = RC_TEXTURE_FILTER_LINEAR,
    .wrap        = RC_TEXTURE_WRAP_CLAMP,
    .gen_mipmaps = true,
    .data        = img.data.data,
});

/* Tell the shader which unit the sampler reads from (set once at startup): */
rc_shader_bind(sh);
rc_shader_set_texture(u_tex_loc, 0);   /* sampler2D → texture unit 0 */
```

Bind the texture each frame via `rc_bindings.textures[]`:

```c
rc_gfx_apply_bindings(&(rc_bindings) {
    .vertex_buffers = { vbuf },
    .textures       = { tex },   /* binds tex to GL_TEXTURE0 */
});
```

### `include/richc/gfx/render_target.h` — Off-screen render targets

```c
rc_render_target rc_render_target_make   (const rc_render_target_desc *desc);
void             rc_render_target_destroy(rc_render_target rt);
rc_texture       rc_render_target_color  (rc_render_target rt, uint32_t index);
void             rc_gfx_begin_render_target(rc_render_target rt);
void             rc_gfx_end_render_target  (void);
```

A render target wraps an OpenGL framebuffer with up to `RC_MAX_COLOR_ATTACHMENTS` (4) colour textures and an optional depth renderbuffer. Colour textures are first-class `rc_texture` handles and can be bound in `rc_bindings.textures[]` for a subsequent pass.

`rc_render_target_desc` fields:

| Field | Notes |
|-------|-------|
| `size` | `rc_vec2i` — fixed at creation time; recreate on window resize |
| `color[]` | Array of `rc_color_attachment_desc` terminated by `format == 0`; at least one required |
| `depth` | `bool` — allocate a depth renderbuffer |

`rc_gfx_begin_render_target` binds the FBO and sets the viewport to the RT size. `rc_gfx_end_render_target` restores FBO 0 and the viewport to `rc_app_size()`. RC_PANICs on nested or unmatched begin/end.

```c
rc_render_target rt = rc_render_target_make(&(rc_render_target_desc) {
    .size  = rc_app_size(),
    .color = {{ .format = RC_TEXTURE_FORMAT_RGBA8,
                .filter = RC_TEXTURE_FILTER_LINEAR,
                .wrap   = RC_TEXTURE_WRAP_CLAMP }},
    .depth = true,
});

rc_gfx_begin_render_target(rt);
    rc_gfx_clear(...);
    /* draw calls ... */
rc_gfx_end_render_target();

/* bind the colour result for a subsequent pass */
rc_gfx_apply_bindings(&(rc_bindings) {
    .vertex_buffers = { quad_buf },
    .textures       = { rc_render_target_color(rt, 0) },
});
```

### `include/richc/image/image.h` — CPU-side image loading

```c
/* construction */
rc_image rc_image_make          (rc_vec2i size, rc_pixel_format format,
                                  const uint8_t *fill_pixel, rc_arena *arena);
rc_image rc_image_make_subimage (rc_image img, rc_box2i region);

/* copy */
bool     rc_image_blit          (rc_image dst, rc_vec2i dst_pos, rc_image src);

/* PNG loading */
rc_image_result rc_image_from_png(rc_view_bytes png_data,
                                   rc_arena *arena, rc_arena scratch);
rc_image_result rc_image_load_png(const char *path,
                                   rc_arena *arena, rc_arena scratch);
```

`rc_image` is a mutable non-owning descriptor for pixel data held in an arena:

```c
typedef struct {
    rc_span_bytes   data;    /* mutable non-owning span into arena memory */
    rc_vec2i        size;
    uint32_t        stride;  /* bytes per row */
    rc_pixel_format format;  /* RC_PIXEL_FORMAT_R8 / RGB8 / RGBA8 */
} rc_image;
```

Row `y` starts at `data.data + y * stride`. Images are always stored top-row-first. `rc_pixel_format` values equal bytes-per-pixel, so `rc_pixel_format_bytes_per_pixel(fmt)` is a plain cast.

**`rc_image_make`** allocates a zero-filled (or pattern-filled) image from `arena`. Pass `NULL` for `fill_pixel` to zero-fill; otherwise `fill_pixel` points to `bytes_per_pixel` bytes that are tiled across every pixel.

**`rc_image_make_subimage`** returns a view into `img` sharing the same pixel data and stride. `region` is clamped to `img.size`; returns a zero-size image if the clamped region is empty.

**`rc_image_blit`** copies `src` into `dst` at `dst_pos` (top-left corner in `dst` coordinates), clipped to `dst` bounds. Format-expanding conversions are applied automatically:

| src → dst | Conversion |
|-----------|------------|
| R8 → RGB8 | `(r, r, r)` |
| R8 → RGBA8 | `(r, r, r, 255)` |
| RGB8 → RGBA8 | `(r, g, b, 255)` |

Returns `false` (no copy performed) if `src.format > dst.format` (narrowing).

**PNG loading** decodes into a flat pixel buffer allocated from `arena`. Supports 8-bit greyscale (→ R8), RGB (→ RGB8), RGBA (→ RGBA8), greyscale+alpha (→ RGBA8), and indexed/palette (→ RGB8). 16-bit and interlaced images return `RC_IMAGE_ERROR_UNSUPPORTED`. `scratch` holds the inflate buffer and raw file bytes — pass a fresh arena and destroy it after the call.

```c
rc_arena arena   = rc_arena_make_default();
rc_arena scratch = rc_arena_make_default();
rc_image_result r = rc_image_load_png("tex.png", &arena, scratch);
rc_arena_destroy(&scratch);
if (r.error != RC_IMAGE_OK) { /* handle error */ }
/* r.image.data, .size, .stride, .format */
```

Image loading is part of `richc_app`; unused functions will be dead-stripped by the linker.

### `include/richc/image/image_pack.h` — Atlas bin packing

Packs multiple images into a single atlas texture using Maximal Rectangles with Best Short Side Fit (BSSF). Images are sorted by decreasing `max(width, height)` before placement for better packing density. No rotation is attempted.

```c
rc_image_pack_result rc_image_pack(rc_view_image images,
                                    rc_vec2i      size,
                                    int32_t       spacing,
                                    rc_arena     *arena,
                                    rc_arena      scratch);
```

| Parameter | Notes |
|-----------|-------|
| `images` | Input images; all must have `data.data != NULL` |
| `size` | Atlas dimensions in pixels |
| `spacing` | Minimum pixel gap maintained between packed images |
| `arena` | Receives atlas pixel data and placements array on success |
| `scratch` | Temporary working state — discarded regardless of outcome |

Returns an `rc_image_pack_result`:

```c
typedef struct {
    rc_image      image;       /* packed atlas, allocated from arena */
    rc_span_box2i placements;  /* placements[i] is the rc_box2i for images[i] in the atlas */
} rc_image_pack_result;
```

On failure (any image does not fit), returns a zero-initialised result and writes nothing to `arena`. The intended usage is to retry with successively larger atlas sizes:

```c
rc_arena arena   = rc_arena_make_default();
rc_arena scratch = rc_arena_make_default();

rc_image_pack_result r = {0};
for (int32_t side = 256; !r.image.data.data; side *= 2) {
    r = rc_image_pack(src_view, rc_vec2i_make(side, side), 1, &arena, scratch);
}
rc_arena_destroy(&scratch);

/* r.placements.data[i] is the rc_box2i for src_images[i] in r.image. */
```

The atlas format is the widest format among the inputs (R8 → RGB8 → RGBA8); `rc_image_blit`'s expanding conversions handle up-casting automatically.

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
        .size      = { 1280, 720 },
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
| `extern/richc` | V0.4 | Core types (`rc_str`, `rc_arena`, `rc_vec2i`, …) |
| `extern/glfw`  | 3.4 | Window creation, input, GL context |
| `extern/glad`  | glad2 | OpenGL 3.3 core loader (generated at configure time) |
| `extern/miniz` | HEAD | zlib/DEFLATE for PNG decompression |

## Building

Requires CMake ≥ 3.21, Python 3 on PATH (for glad code generation), and a C17-capable compiler.

```sh
# Clone with submodules
git clone --recurse-submodules <url>

# Configure and build
cmake -B build -G Ninja -DCMAKE_C_COMPILER=clang
cmake --build build
```
