/*
 * test_app.c - basic richc-app smoke test.
 *
 * Opens a resizable 1280x720 window, clears it to mid-grey each frame,
 * and exits cleanly when the user closes the window.
 */

#include "richc_app/gfx.h"

typedef struct { int unused; } TestCtx;

#define APP_CTX_T TestCtx
#include "richc_app/app.h"

int main(void)
{
    TestCtx ctx = {0};

    rc_app_TestCtx_desc desc = {0};
    desc.title     = RC_STR("richc-app test");
    desc.width     = 1280;
    desc.height    = 720;
    desc.resizable = true;
    desc.ctx       = &ctx;

    rc_app_TestCtx app = rc_app_TestCtx_make(&desc);

    while (rc_app_TestCtx_is_running(&app)) {
        rc_app_TestCtx_poll(&app);

        rc_vec2i size = rc_app_TestCtx_size(&app);
        rc_gfx_viewport(size);
        rc_gfx_clear(rc_color_make_rgb(0.5f, 0.5f, 0.5f));

        rc_app_TestCtx_swap(&app);
    }

    rc_app_TestCtx_destroy(&app);
    return 0;
}
