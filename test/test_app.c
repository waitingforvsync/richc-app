/*
 * test_app.c - basic richc-app smoke test.
 *
 * Opens a resizable 1280x720 window, clears it to mid-grey each frame,
 * and exits cleanly when the user closes the window.
 */

#include "richc_app/app.h"
#include "richc_app/gfx.h"

int main(void)
{
    rc_app_desc desc = {0};
    desc.title     = RC_STR("richc-app test");
    desc.width     = 1280;
    desc.height    = 720;
    desc.resizable = true;

    rc_app *app = rc_app_make(&desc);

    while (rc_app_is_running(app)) {
        rc_app_poll(app);

        rc_gfx_viewport(rc_app_size(app));
        rc_gfx_clear(rc_color_make_rgb(0.5f, 0.5f, 0.5f));

        rc_app_swap(app);
    }

    rc_app_destroy(app);
    return 0;
}
