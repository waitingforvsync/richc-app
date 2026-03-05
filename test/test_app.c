/*
 * test_app.c - basic richc-app smoke test.
 *
 * Opens a resizable 1280x720 window, clears it to mid-grey each frame,
 * and exits cleanly when the user closes the window.
 */

#include "richc_app/app.h"
#include "richc_app/gfx.h"

static void on_render(void *ctx)
{
    (void)ctx;
    rc_gfx_clear(rc_color_make_rgb(0.5f, 0.5f, 0.5f));
}

int main(void)
{
    rc_app_init(&(rc_app_desc){
        .title     = RC_STR("richc-app test"),
        .width     = 1280,
        .height    = 720,
        .resizable = true,
        .callbacks = { .on_render = on_render },
    });

    while (rc_app_is_running()) {
        rc_app_poll();
        rc_app_request_render();
    }

    rc_app_destroy();
    return 0;
}
