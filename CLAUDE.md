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
| `extern/richc` | v0.1 | Core types (rc_str, rc_vec2i, RC_ASSERT, …) |
| `extern/glfw`  | 3.4  | Window creation, input events, GL context |
| `extern/glad`  | HEAD (glad2) | OpenGL 3.3 core loader (generated at configure time via Python) |

## Header guards
Same convention as richc: `#ifndef RC_APP_<FILENAME>_H_` / `#define` / `#endif`.
Examples: `RC_APP_GFX_H_`, `RC_APP_EVENTS_H_`, `RC_APP_KEYS_H_`.

## Backend abstraction
The compile-time backend is selected by which `.c` file is compiled.
- `src/app_glfw.c` — the only file that includes `<GLFW/glfw3.h>` and `<glad/gl.h>`
- `src/gfx_gl33.c` — the only file that calls raw GL functions

Public headers (`app.h`, `gfx.h`, `events.h`, `keys.h`) must never include
GLFW or glad headers.

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

## Architecture: app template (app.h)

`app.h` is a preprocessor template that generates a typed application handle.
Define `APP_CTX_T` (and optionally `APP_NAME`) before including:

```c
#define APP_CTX_T  MyState
#include "richc_app/app.h"
```

This generates:
- `rc_app_MyState_desc`  — descriptor with typed callbacks and a `ctx` pointer
- `rc_app_MyState`       — app handle (`{ rc_app_impl_ *impl_; }`)
- Inline wrappers: `_make`, `_destroy`, `_poll`, `_swap`, `_is_running`, `_size`

The template casts typed callbacks to `void *` variants when calling the
backend.  The casts are safe because `void *` and `T *` share data-pointer
representation on all supported platforms.

## File inventory
```
CMakeLists.txt                    — build system
CLAUDE.md                         — this file
extern/
  richc/                          — richc v0.1 submodule
  glfw/                           — GLFW 3.4 submodule
  glad/                           — glad2 submodule (Python-based GL loader)
include/richc_app/
  keys.h                          — rc_key, rc_action, rc_mod, rc_mouse_button enums
  events.h                        — rc_key_event, rc_mouse_event, rc_cursor_event,
                                    rc_scroll_event, rc_resize_event, rc_char_event
  app.h                           — typed app handle template (APP_CTX_T)
  gfx.h                           — rc_color, rc_gfx_viewport, rc_gfx_clear, rc_gfx_clear_depth
src/
  app_glfw.c                      — GLFW + glad backend; implements rc_app_impl_ functions
  gfx_gl33.c                      — GL 3.3 implementation of gfx.h helpers
```
