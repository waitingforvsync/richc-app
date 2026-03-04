/*
 * app.h - single-window application handle, event loop and input.
 *
 * Usage
 * -----
 *   rc_app_desc desc = {0};
 *   desc.title     = RC_STR("My App");
 *   desc.width     = 1280;
 *   desc.height    = 720;
 *   desc.resizable = true;
 *   desc.on_key    = my_key_handler;   // void my_key_handler(void*, rc_key_event)
 *   desc.ctx       = &state;
 *
 *   rc_app *app = rc_app_make(&desc);
 *   while (rc_app_is_running(app)) {
 *       rc_app_poll(app);
 *       render();
 *       rc_app_swap(app);
 *   }
 *   rc_app_destroy(app);
 */

#ifndef RC_APP_H_
#define RC_APP_H_

#include <stdbool.h>
#include <stdint.h>
#include "richc/str.h"
#include "richc/math/vec2i.h"
#include "richc_app/events.h"

/* Opaque application handle. */
typedef struct rc_app_ rc_app;

/* Application descriptor — zero-initialise and fill only what you need. */
typedef struct {
    rc_str   title;
    int32_t  width;
    int32_t  height;
    bool     resizable;

    /* Graphics hints. */
    bool     srgb;
    int32_t  depth_bits;    /* 0 = no depth buffer */
    int32_t  msaa_samples;  /* 0 or 1 = no MSAA    */

    /* Callbacks — all optional (NULL = ignored).
     * ctx is forwarded as the first argument of every callback. */
    void (*on_key)   (void *ctx, rc_key_event);
    void (*on_mouse) (void *ctx, rc_mouse_event);
    void (*on_scroll)(void *ctx, rc_scroll_event);
    void (*on_cursor)(void *ctx, rc_cursor_event);
    void (*on_resize)(void *ctx, rc_resize_event);
    void (*on_char)  (void *ctx, rc_char_event);
    void (*on_close) (void *ctx);

    void *ctx;
} rc_app_desc;

rc_app  *rc_app_make       (const rc_app_desc *desc);
void     rc_app_destroy    (rc_app *app);
void     rc_app_poll       (rc_app *app);
void     rc_app_swap       (rc_app *app);
bool     rc_app_is_running (const rc_app *app);
rc_vec2i rc_app_size       (const rc_app *app);

#endif /* RC_APP_H_ */
