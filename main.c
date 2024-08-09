#include <unistd.h>
#include <pthread.h>
#include "lvgl.h"
#include "lv_conf.h"
#include "lv_drv_conf.h"
#include "demos/lv_demos.h"
#include "display/drm.h"
#include "display/fbdev.h"
#include "sdl/sdl.h"
#include "indev/evdev.h"
#include "panel.h"

#define DISP_BUF_SIZE (2 * 1024 * 1024)

static void display_init(void)
{
    drm_init();
    static lv_color_t buf1[DISP_BUF_SIZE];
    static lv_color_t buf2[DISP_BUF_SIZE];

    static lv_disp_draw_buf_t disp_buf;
    lv_disp_draw_buf_init(&disp_buf, buf1, buf2, DISP_BUF_SIZE);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.draw_buf = &disp_buf;

    uint32_t dpi;
    disp_drv.flush_cb = drm_flush;
    disp_drv.wait_cb = drm_wait_vsync;
    drm_get_sizes(&disp_drv.hor_res, &disp_drv.ver_res, &dpi);

    lv_disp_drv_register(&disp_drv);
}

static void input_init(void)
{
    evdev_init();

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);

    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = evdev_read;
    lv_indev_drv_register(&indev_drv);
}

int main(void)
{
    lv_init();

    display_init();

    input_init();

    panel_create();

    while (1)
    {
        usleep(lv_timer_handler() * 1000);
    }

    return 0;
}
