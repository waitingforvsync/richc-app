/*
 * app_glfw.c - GLFW + glad backend for rc_app.
 *
 * This is the only file that includes GLFW and glad headers; all
 * GLFW/OpenGL types are kept out of the public headers.
 *
 * GLFW key codes, action values, modifier flags, and mouse button values
 * are intentionally chosen to match the rc_* enums in keys.h exactly, so
 * the callbacks cast directly without a lookup table.
 */

#include "richc_app/app.h"
#include "richc/debug.h"

#include <glad/gl.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <stdlib.h>
#include <stdint.h>

/* ---- private impl struct ---- */

struct rc_app_impl_ {
    GLFWwindow *window;
    void       *ctx;
    void (*on_key)   (void *, rc_key_event);
    void (*on_mouse) (void *, rc_mouse_event);
    void (*on_scroll)(void *, rc_scroll_event);
    void (*on_cursor)(void *, rc_cursor_event);
    void (*on_resize)(void *, rc_resize_event);
    void (*on_char)  (void *, rc_char_event);
    void (*on_close) (void *);
};

/* ---- glad proc loader ---- */

/*
 * Bridge between glfwGetProcAddress (returns GLFWglproc = void (*)(void))
 * and GLADloadfunc (returns GLADapiproc = void (*)(void)).
 * Both are the same underlying type; the cast is a no-op in practice.
 */
static GLADapiproc glad_get_proc_(const char *name)
{
    return (GLADapiproc)glfwGetProcAddress(name);
}

/* ---- GLFW callbacks ---- */

static void key_callback_(GLFWwindow *w, int key, int scancode, int action, int mods)
{
    (void)scancode;
    rc_app_impl_ *impl = (rc_app_impl_ *)glfwGetWindowUserPointer(w);
    if (!impl->on_key) return;
    rc_key_event ev = { (rc_key)key, (rc_action)action, (rc_mod)mods };
    impl->on_key(impl->ctx, ev);
}

static void mouse_button_callback_(GLFWwindow *w, int button, int action, int mods)
{
    rc_app_impl_ *impl = (rc_app_impl_ *)glfwGetWindowUserPointer(w);
    if (!impl->on_mouse) return;
    rc_mouse_event ev = { (rc_mouse_button)button, (rc_action)action, (rc_mod)mods };
    impl->on_mouse(impl->ctx, ev);
}

static void scroll_callback_(GLFWwindow *w, double xoff, double yoff)
{
    rc_app_impl_ *impl = (rc_app_impl_ *)glfwGetWindowUserPointer(w);
    if (!impl->on_scroll) return;
    rc_scroll_event ev = { xoff, yoff };
    impl->on_scroll(impl->ctx, ev);
}

static void cursor_pos_callback_(GLFWwindow *w, double x, double y)
{
    rc_app_impl_ *impl = (rc_app_impl_ *)glfwGetWindowUserPointer(w);
    if (!impl->on_cursor) return;
    rc_cursor_event ev = { x, y };
    impl->on_cursor(impl->ctx, ev);
}

static void framebuffer_size_callback_(GLFWwindow *w, int width, int height)
{
    rc_app_impl_ *impl = (rc_app_impl_ *)glfwGetWindowUserPointer(w);
    if (!impl->on_resize) return;
    rc_resize_event ev = { width, height };
    impl->on_resize(impl->ctx, ev);
}

static void char_callback_(GLFWwindow *w, unsigned int codepoint)
{
    rc_app_impl_ *impl = (rc_app_impl_ *)glfwGetWindowUserPointer(w);
    if (!impl->on_char) return;
    rc_char_event ev = { codepoint };
    impl->on_char(impl->ctx, ev);
}

static void window_close_callback_(GLFWwindow *w)
{
    rc_app_impl_ *impl = (rc_app_impl_ *)glfwGetWindowUserPointer(w);
    if (!impl->on_close) return;
    impl->on_close(impl->ctx);
}

/* ---- public impl functions ---- */

rc_app_impl_ *rc_app_impl_create_(
    rc_str title, int32_t w, int32_t h, bool resizable,
    rc_gfx_hints_ hints,
    void *ctx,
    void (*on_key)   (void *, rc_key_event),
    void (*on_mouse) (void *, rc_mouse_event),
    void (*on_scroll)(void *, rc_scroll_event),
    void (*on_cursor)(void *, rc_cursor_event),
    void (*on_resize)(void *, rc_resize_event),
    void (*on_char)  (void *, rc_char_event),
    void (*on_close) (void *))
{
    RC_PANIC(glfwInit());

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_RESIZABLE, resizable ? GLFW_TRUE : GLFW_FALSE);

    if (hints.srgb)
        glfwWindowHint(GLFW_SRGB_CAPABLE, GLFW_TRUE);
    if (hints.depth_bits > 0)
        glfwWindowHint(GLFW_DEPTH_BITS, hints.depth_bits);
    if (hints.msaa_samples > 1)
        glfwWindowHint(GLFW_SAMPLES, hints.msaa_samples);

    char title_buf[256];
    const char *title_cstr = rc_str_as_cstr(title, title_buf, (uint32_t)sizeof(title_buf));

    GLFWwindow *window = glfwCreateWindow(w, h, title_cstr ? title_cstr : "", NULL, NULL);
    RC_PANIC(window != NULL);

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    RC_PANIC(gladLoadGL(glad_get_proc_));

    rc_app_impl_ *impl = (rc_app_impl_ *)malloc(sizeof(rc_app_impl_));
    RC_PANIC(impl != NULL);

    impl->window    = window;
    impl->ctx       = ctx;
    impl->on_key    = on_key;
    impl->on_mouse  = on_mouse;
    impl->on_scroll = on_scroll;
    impl->on_cursor = on_cursor;
    impl->on_resize = on_resize;
    impl->on_char   = on_char;
    impl->on_close  = on_close;

    glfwSetWindowUserPointer        (window, impl);
    glfwSetKeyCallback              (window, key_callback_);
    glfwSetMouseButtonCallback      (window, mouse_button_callback_);
    glfwSetScrollCallback           (window, scroll_callback_);
    glfwSetCursorPosCallback        (window, cursor_pos_callback_);
    glfwSetFramebufferSizeCallback  (window, framebuffer_size_callback_);
    glfwSetCharCallback             (window, char_callback_);
    glfwSetWindowCloseCallback      (window, window_close_callback_);

    return impl;
}

void rc_app_impl_destroy_(rc_app_impl_ *impl)
{
    glfwDestroyWindow(impl->window);
    glfwTerminate();
    free(impl);
}

void rc_app_impl_poll_(rc_app_impl_ *impl)
{
    (void)impl;
    glfwPollEvents();
}

void rc_app_impl_swap_(rc_app_impl_ *impl)
{
    glfwSwapBuffers(impl->window);
}

bool rc_app_impl_is_running_(const rc_app_impl_ *impl)
{
    return !glfwWindowShouldClose(impl->window);
}

rc_vec2i rc_app_impl_size_(const rc_app_impl_ *impl)
{
    int fw, fh;
    glfwGetFramebufferSize(impl->window, &fw, &fh);
    return rc_vec2i_make(fw, fh);
}
