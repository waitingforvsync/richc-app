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

/* ---- private struct definition ---- */

struct rc_app_ {
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
    rc_app *app = (rc_app *)glfwGetWindowUserPointer(w);
    if (!app->on_key) return;
    rc_key_event ev = { (rc_key)key, (rc_action)action, (rc_mod)mods };
    app->on_key(app->ctx, ev);
}

static void mouse_button_callback_(GLFWwindow *w, int button, int action, int mods)
{
    rc_app *app = (rc_app *)glfwGetWindowUserPointer(w);
    if (!app->on_mouse) return;
    rc_mouse_event ev = { (rc_mouse_button)button, (rc_action)action, (rc_mod)mods };
    app->on_mouse(app->ctx, ev);
}

static void scroll_callback_(GLFWwindow *w, double xoff, double yoff)
{
    rc_app *app = (rc_app *)glfwGetWindowUserPointer(w);
    if (!app->on_scroll) return;
    rc_scroll_event ev = { xoff, yoff };
    app->on_scroll(app->ctx, ev);
}

static void cursor_pos_callback_(GLFWwindow *w, double x, double y)
{
    rc_app *app = (rc_app *)glfwGetWindowUserPointer(w);
    if (!app->on_cursor) return;
    rc_cursor_event ev = { x, y };
    app->on_cursor(app->ctx, ev);
}

static void framebuffer_size_callback_(GLFWwindow *w, int width, int height)
{
    rc_app *app = (rc_app *)glfwGetWindowUserPointer(w);
    if (!app->on_resize) return;
    rc_resize_event ev = { width, height };
    app->on_resize(app->ctx, ev);
}

static void char_callback_(GLFWwindow *w, unsigned int codepoint)
{
    rc_app *app = (rc_app *)glfwGetWindowUserPointer(w);
    if (!app->on_char) return;
    rc_char_event ev = { codepoint };
    app->on_char(app->ctx, ev);
}

static void window_close_callback_(GLFWwindow *w)
{
    rc_app *app = (rc_app *)glfwGetWindowUserPointer(w);
    if (!app->on_close) return;
    app->on_close(app->ctx);
}

/* ---- public API ---- */

rc_app *rc_app_make(const rc_app_desc *desc)
{
    RC_PANIC(glfwInit());

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_RESIZABLE, desc->resizable ? GLFW_TRUE : GLFW_FALSE);

    if (desc->srgb)
        glfwWindowHint(GLFW_SRGB_CAPABLE, GLFW_TRUE);
    if (desc->depth_bits > 0)
        glfwWindowHint(GLFW_DEPTH_BITS, desc->depth_bits);
    if (desc->msaa_samples > 1)
        glfwWindowHint(GLFW_SAMPLES, desc->msaa_samples);

    char title_buf[256];
    const char *title_cstr = rc_str_as_cstr(desc->title, title_buf, (uint32_t)sizeof(title_buf));

    GLFWwindow *window = glfwCreateWindow(desc->width, desc->height,
                                          title_cstr ? title_cstr : "", NULL, NULL);
    RC_PANIC(window != NULL);

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    RC_PANIC(gladLoadGL(glad_get_proc_));

    rc_app *app = (rc_app *)malloc(sizeof(rc_app));
    RC_PANIC(app != NULL);

    app->window    = window;
    app->ctx       = desc->ctx;
    app->on_key    = desc->on_key;
    app->on_mouse  = desc->on_mouse;
    app->on_scroll = desc->on_scroll;
    app->on_cursor = desc->on_cursor;
    app->on_resize = desc->on_resize;
    app->on_char   = desc->on_char;
    app->on_close  = desc->on_close;

    glfwSetWindowUserPointer       (window, app);
    glfwSetKeyCallback             (window, key_callback_);
    glfwSetMouseButtonCallback     (window, mouse_button_callback_);
    glfwSetScrollCallback          (window, scroll_callback_);
    glfwSetCursorPosCallback       (window, cursor_pos_callback_);
    glfwSetFramebufferSizeCallback (window, framebuffer_size_callback_);
    glfwSetCharCallback            (window, char_callback_);
    glfwSetWindowCloseCallback     (window, window_close_callback_);

    return app;
}

void rc_app_destroy(rc_app *app)
{
    glfwDestroyWindow(app->window);
    glfwTerminate();
    free(app);
}

void rc_app_poll(rc_app *app)
{
    (void)app;
    glfwPollEvents();
}

void rc_app_swap(rc_app *app)
{
    glfwSwapBuffers(app->window);
}

bool rc_app_is_running(const rc_app *app)
{
    return !glfwWindowShouldClose(app->window);
}

rc_vec2i rc_app_size(const rc_app *app)
{
    int fw, fh;
    glfwGetFramebufferSize(app->window, &fw, &fh);
    return rc_vec2i_make(fw, fh);
}
