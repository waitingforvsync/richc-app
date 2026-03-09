cmake_minimum_required(VERSION 3.21)
project(richc_app C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# ---- richc ----

add_subdirectory(extern/richc)

# ---- GLFW ----

set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(extern/glfw)

# ---- glad (OpenGL 3.3 Core loader) ----
# Requires Python at configure time.  REPRODUCIBLE avoids fetching the
# Khronos spec from the internet; EXTENSIONS NONE keeps the loader minimal.

add_subdirectory(extern/glad/cmake)
glad_add_library(glad STATIC REPRODUCIBLE API gl:core=3.3 EXTENSIONS NONE)

# ---- OpenGL ----

find_package(OpenGL REQUIRED)

# ---- miniz (zlib/DEFLATE for PNG decompression) ----
# Uses miniz's own CMakeLists so generate_export_header produces miniz_export.h.
# BUILD_SHARED_LIBS=OFF forces a static library; examples/tests/install are OFF
# because MINIZ_STANDALONE_PROJECT is auto-detected as OFF (we are the top project).

add_subdirectory(extern/miniz)

# ---- richc_app ----

add_library(richc_app STATIC
    src/app/app_glfw.c
    src/gfx/gfx_gl33.c
    src/gfx/shader_gl33.c
    src/gfx/buffer_gl33.c
    src/gfx/pipeline_gl33.c
    src/gfx/texture_gl33.c
    src/image/image.c
    src/image/image_pack.c
    include/richc/app/app.h
    include/richc/app/keys.h
    include/richc/gfx/gfx.h
    include/richc/gfx/shader.h
    include/richc/gfx/buffer.h
    include/richc/gfx/pipeline.h
    include/richc/gfx/texture.h
    include/richc/image/image.h
    include/richc/image/array_image.h
    include/richc/image/image_pack.h
)

target_include_directories(richc_app PUBLIC include)

target_link_libraries(richc_app
    PUBLIC  richc
    PRIVATE glfw glad OpenGL::GL miniz
)

target_compile_definitions(richc_app PRIVATE _CRT_SECURE_NO_WARNINGS)

if(MSVC)
    target_compile_options(richc_app PRIVATE /W3)
else()
    target_compile_options(richc_app PRIVATE -Wall -Wextra)
endif()

# ---- test application ----

add_executable(richc_app_test
    test/test_app.c
)

target_link_libraries(richc_app_test
    PRIVATE richc_app
)

if(MSVC)
    target_compile_options(richc_app_test PRIVATE /W3)
else()
    target_compile_options(richc_app_test PRIVATE -Wall -Wextra)
endif()

# When launched from the VS IDE the working directory defaults to the build
# folder; set it to the source root so "test/owl.png" resolves correctly.
set_target_properties(richc_app_test PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)
