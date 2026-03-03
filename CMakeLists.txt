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

# ---- richc_app ----

add_library(richc_app STATIC
    src/app_glfw.c
    src/gfx_gl33.c
    include/richc_app/app.h
    include/richc_app/gfx.h
    include/richc_app/events.h
    include/richc_app/keys.h
)

target_include_directories(richc_app PUBLIC include)

target_link_libraries(richc_app
    PUBLIC  richc
    PRIVATE glfw glad OpenGL::GL
)

target_compile_definitions(richc_app PRIVATE _CRT_SECURE_NO_WARNINGS)

if(MSVC)
    target_compile_options(richc_app PRIVATE /W3)
else()
    target_compile_options(richc_app PRIVATE -Wall -Wextra)
endif()
