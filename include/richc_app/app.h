/*
 * app.h - template header: single-window application, event loop, input.
 *
 * Provides a typed application handle whose callbacks receive a concrete
 * context pointer.  The backend (app_glfw.c) stores callbacks as void*
 * variants internally; the casts are safe because void* and T* share
 * representation for data pointers on all supported platforms.
 *
 * Define before including:
 *   APP_CTX_T    context type (required)
 *   APP_NAME     name for the generated app struct
 *                (optional; default: rc_app_##APP_CTX_T)
 *
 * All macros defined before inclusion are undefined by this header.
 * If APP_CTX_T is not defined the header is still safe to include:
 * only the once-only declarations are emitted and the per-type section
 * is skipped.  This allows backend source files to include app.h for
 * the impl type and hint declarations without triggering an instantiation.
 *
 * Usage
 * -----
 *   #define APP_CTX_T  MyState
 *   #include "richc_app/app.h"
 *
 *   rc_app_MyState_desc desc = {0};
 *   desc.title  = RC_STR("My App");
 *   desc.width  = 1280;
 *   desc.height = 720;
 *   desc.on_key = my_key_handler;    // void my_key_handler(MyState*, rc_key_event)
 *   desc.ctx    = &state;
 *
 *   rc_app_MyState app = rc_app_MyState_make(&desc);
 *   while (rc_app_MyState_is_running(&app)) {
 *       rc_app_MyState_poll(&app);
 *       render();
 *       rc_app_MyState_swap(&app);
 *   }
 *   rc_app_MyState_destroy(&app);
 *
 * Generated types
 * ---------------
 *   APP_NAME_desc  — descriptor filled by the caller before make()
 *   APP_NAME       — app handle (contains opaque impl pointer)
 *
 * Generated functions (all static inline)
 * ----------------------------------------
 *   APP_NAME   APP_NAME_make       (const APP_NAME_desc *desc)
 *   void       APP_NAME_destroy    (APP_NAME *app)
 *   void       APP_NAME_poll       (APP_NAME *app)
 *   void       APP_NAME_swap       (APP_NAME *app)
 *   bool       APP_NAME_is_running (const APP_NAME *app)
 *   rc_vec2i   APP_NAME_size       (const APP_NAME *app)
 */

#include "richc/template_util.h"
#include "richc/str.h"
#include "richc/math/vec2i.h"
#include "richc_app/events.h"

/* ---- once-only section ---- */

#ifndef RC_APP_H_
#define RC_APP_H_

#include <stdbool.h>
#include <stdint.h>

/*
 * rc_app_impl_: opaque handle owned by the backend (app_glfw.c).
 * Callbacks are stored as void* variants to avoid template instantiation
 * inside the backend.
 */
typedef struct rc_app_impl_ rc_app_impl_;

/* Graphics context hints forwarded to the backend at window creation. */
typedef struct {
    bool    srgb;
    int32_t depth_bits;     /* 0 = no depth buffer */
    int32_t msaa_samples;   /* 0 or 1 = no MSAA    */
} rc_gfx_hints_;

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
    void (*on_close) (void *));

void     rc_app_impl_destroy_    (rc_app_impl_ *impl);
void     rc_app_impl_poll_       (rc_app_impl_ *impl);
void     rc_app_impl_swap_       (rc_app_impl_ *impl);
bool     rc_app_impl_is_running_ (const rc_app_impl_ *impl);
rc_vec2i rc_app_impl_size_       (const rc_app_impl_ *impl);

#endif /* RC_APP_H_ */

/* ---- per-type section (skipped if APP_CTX_T is not defined) ---- */

#ifdef APP_CTX_T

#ifndef APP_NAME
#  define APP_NAME RC_CONCAT(rc_app_, APP_CTX_T)
#endif

#define APP_DESC_    RC_CONCAT(APP_NAME, _desc)
#define APP_MAKE_    RC_CONCAT(APP_NAME, _make)
#define APP_DESTROY_ RC_CONCAT(APP_NAME, _destroy)
#define APP_POLL_    RC_CONCAT(APP_NAME, _poll)
#define APP_SWAP_    RC_CONCAT(APP_NAME, _swap)
#define APP_RUNNING_ RC_CONCAT(APP_NAME, _is_running)
#define APP_SIZE_    RC_CONCAT(APP_NAME, _size)

/* ---- generated descriptor ---- */

typedef struct {
    rc_str   title;
    int32_t  width;
    int32_t  height;
    bool     resizable;

    /* Graphics hints. */
    bool     srgb;
    int32_t  depth_bits;
    int32_t  msaa_samples;

    /* Callbacks — all optional (NULL = ignored). */
    void (*on_key)   (APP_CTX_T *, rc_key_event);
    void (*on_mouse) (APP_CTX_T *, rc_mouse_event);
    void (*on_scroll)(APP_CTX_T *, rc_scroll_event);
    void (*on_cursor)(APP_CTX_T *, rc_cursor_event);
    void (*on_resize)(APP_CTX_T *, rc_resize_event);
    void (*on_char)  (APP_CTX_T *, rc_char_event);
    void (*on_close) (APP_CTX_T *);

    APP_CTX_T *ctx;
} APP_DESC_;

/* ---- generated app handle ---- */

typedef struct {
    rc_app_impl_ *impl_;
} APP_NAME;

/* ---- generated functions ---- */

static inline APP_NAME APP_MAKE_(const APP_DESC_ *desc)
{
    rc_gfx_hints_ hints = { desc->srgb, desc->depth_bits, desc->msaa_samples };
    APP_NAME app;
    app.impl_ = rc_app_impl_create_(
        desc->title, desc->width, desc->height, desc->resizable, hints,
        (void *)desc->ctx,
        (void (*)(void *, rc_key_event))    desc->on_key,
        (void (*)(void *, rc_mouse_event))  desc->on_mouse,
        (void (*)(void *, rc_scroll_event)) desc->on_scroll,
        (void (*)(void *, rc_cursor_event)) desc->on_cursor,
        (void (*)(void *, rc_resize_event)) desc->on_resize,
        (void (*)(void *, rc_char_event))   desc->on_char,
        (void (*)(void *))                  desc->on_close);
    return app;
}

static inline void     APP_DESTROY_(APP_NAME *app)      { rc_app_impl_destroy_(app->impl_);          }
static inline void     APP_POLL_   (APP_NAME *app)       { rc_app_impl_poll_(app->impl_);             }
static inline void     APP_SWAP_   (APP_NAME *app)       { rc_app_impl_swap_(app->impl_);             }
static inline bool     APP_RUNNING_(const APP_NAME *app) { return rc_app_impl_is_running_(app->impl_); }
static inline rc_vec2i APP_SIZE_   (const APP_NAME *app) { return rc_app_impl_size_(app->impl_);      }

/* ---- cleanup ---- */

#undef APP_DESC_
#undef APP_MAKE_
#undef APP_DESTROY_
#undef APP_POLL_
#undef APP_SWAP_
#undef APP_RUNNING_
#undef APP_SIZE_

#undef APP_NAME
#undef APP_CTX_T

#endif /* APP_CTX_T */
