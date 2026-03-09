# richc-app — Project Notes for Claude Code

## What this is
A C17 library providing windowed application scaffolding, input handling, and
OpenGL 3.3 graphics helpers.  It sits on top of richc (extern/richc) and uses
GLFW (extern/glfw) and glad (extern/glad) as private implementation details.

Consumers only ever include `<richc_app/…>` headers; GLFW and OpenGL types
never appear in the public API.

## Dependencies
| Submodule | Tag | Role |
|-----------|-----|------|
| `extern/richc` | V0.4 | Core types (rc_str, rc_vec2i, RC_ASSERT, …) |
| `extern/glfw`  | 3.4  | Window creation, input events, GL context |
| `extern/glad`  | HEAD (glad2) | OpenGL 3.3 core loader (generated at configure time via Python) |
| `extern/miniz` | HEAD | zlib/DEFLATE implementation (PNG decompression) |

## Header guards
Same convention as richc: `#ifndef RC_APP_<FILENAME>_H_` / `#define` / `#endif`.
Examples: `RC_APP_H_`, `RC_APP_KEYS_H_`, `RC_APP_GFX_H_`.

## Library targets
| Target | Depends on | Role |
|--------|-----------|------|
| `richc_app` | richc, glfw, glad, miniz | Window, input, OpenGL pipeline, PNG image loading |

## Backend abstraction
The compile-time backend is selected by which `.c` file is compiled.
- `src/app/app_glfw.c`     — the only file that includes `<GLFW/glfw3.h>` and `<glad/gl.h>`
- `src/gfx/gfx_gl33.c`    — GL 3.3 clear/viewport
- `src/gfx/shader_gl33.c` — GL 3.3 shader compilation
- `src/gfx/buffer_gl33.c` — GL 3.3 buffer upload/update
- `src/gfx/pipeline_gl33.c` — GL 3.3 pipeline, bindings, draw
- `src/gfx/texture_gl33.c` — GL 3.3 texture upload/sampler/destroy

Public headers must never include GLFW or glad headers.

## Ground rules
- **Language standard: C17** — compile with `/std:c17` (MSVC) or `-std=c17` (clang/gcc)
- **No vtable.** Backend abstraction is architectural (compile-time selection),
  not runtime polymorphism.
- `RC_ASSERT` / `RC_PANIC` from richc/debug.h for all assertions.
  Use `RC_PANIC` for unrecoverable failures (OOM, GL/GLFW init failure).
- Follow richc naming conventions: `rc_` prefix, snake_case, container-first.

## Build directories
| Directory    | Compiler | Generator |
|--------------|----------|-----------|
| `build/`     | clang    | Ninja     |
| `build_msvc/`| MSVC     | VS 2022   |

Always use the matching directory — never mix compilers into the same build tree.

## Build commands
Requires: CMake ≥ 3.21, Python 3 on PATH (for glad code generation), and a
C17-capable compiler.  Run from `C:\Users\richard.talbotwatkin\richc-app\`.

Configure and build with Ninja + clang (primary):
```
"C:/Program Files/Microsoft Visual Studio/2022/Professional/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" \
    -B build -G Ninja \
    -DCMAKE_C_COMPILER=C:/clang/bin/clang.exe \
    -DCMAKE_MAKE_PROGRAM="C:/Program Files/Microsoft Visual Studio/2022/Professional/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe"

"C:/Program Files/Microsoft Visual Studio/2022/Professional/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" \
    --build build
```

Configure and build with VS 2022 (MSVC):
```
"C:/Program Files/Microsoft Visual Studio/2022/Professional/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" \
    -B build_msvc -G "Visual Studio 17 2022"

"C:/Program Files/Microsoft Visual Studio/2022/Professional/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" \
    --build build_msvc
```

## File inventory
```
CMakeLists.txt                    — build system
CLAUDE.md                         — this file
extern/
  richc/                          — richc V0.4 submodule
  glfw/                           — GLFW 3.4 submodule
  glad/                           — glad2 submodule (Python-based GL loader)
include/richc/app/
  keys.h                          — rc_scancode, rc_mod, rc_mouse_button
  app.h                           — rc_app_callbacks, rc_app_desc,
                                    rc_app_init/destroy/poll/is_running/size/
                                    request_update/request_render/swap_buffers
include/richc/image/
  image.h                         — rc_image (data: rc_span_bytes, mutable), rc_pixel_format,
                                    rc_pixel_format_bytes_per_pixel,
                                    rc_image_make, rc_image_make_subimage, rc_image_blit,
                                    rc_image_from_png, rc_image_load_png
  array_image.h                   — rc_view_image, rc_span_image, rc_array_image (template instantiation)
  image_pack.h                    — rc_image_pack_result, rc_image_pack (Maximal Rectangles / BSSF atlas packer)
include/richc/gfx/
  gfx.h                           — rc_color, rc_gfx_viewport/clear/clear_depth
  shader.h                        — rc_shader, rc_uniform_loc, rc_shader_make/destroy/bind/loc/set_*
  buffer.h                        — rc_buffer, rc_buffer_make/upload/update/destroy
  pipeline.h                      — rc_pipeline, rc_bindings, rc_attrib_format, rc_index_type,
                                    rc_pipeline_make/destroy, rc_gfx_apply_pipeline/bindings/draw
  texture.h                       — rc_texture, rc_texture_desc, rc_texture_format/usage/wrap/filter,
                                    rc_texture_make/update/destroy
src/image/
  image.c                         — PNG decoder (miniz inflate + filter reconstruction);
                                    rc_image_make/make_subimage/blit
  image_pack.c                    — rc_image_pack implementation; indirect sort + swap-remove free-rect splitting
src/app/
  app_glfw.c                      — GLFW + glad backend; defines struct rc_app_ and implements rc_app_* functions
src/gfx/
  gfx_gl33.c                      — GL 3.3 implementation of gfx.h
  shader_gl33.c                   — GL 3.3 implementation of shader.h
  buffer_gl33.c                   — GL 3.3 implementation of buffer.h
  pipeline_gl33.c                 — GL 3.3 implementation of pipeline.h (global VAO, pipeline table)
  texture_gl33.c                  — GL 3.3 implementation of texture.h (flat table, swap-remove)
  texture_gl33_internal.h         — internal helper rc_texture_gl_() used by pipeline_gl33.c
test/
  test_app.c                      — pentagram + owl texture quad + packed atlas quad (24 solid-colour images)
```
