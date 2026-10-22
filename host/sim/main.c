/*
 * Desktop preview of the real pp_ui (components/pp_ui/ui.c) in a 466x466 SDL window,
 * with mock data. Usage: sim [--shot out.bmp --screen home|menu|bus|nav|settings]
 * Shot mode renders offscreen (use SDL_VIDEODRIVER=dummy) and exits.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "hal_board.h"
#include "lvgl.h"
#include "storage.h"
#include "ui.h"

void sim_lvgl_lock(void);
void sim_lvgl_unlock(void);

static void save_bmp(const char *path)
{
    lv_draw_buf_t *snap = lv_snapshot_take(lv_screen_active(), LV_COLOR_FORMAT_ARGB8888);
    if (!snap) return;
    int w = snap->header.w, h = snap->header.h;
    FILE *f = fopen(path, "wb");
    unsigned size = 54 + w * h * 4;
    unsigned char hd[54] = { 'B', 'M' };
    memcpy(hd + 2, &size, 4);
    unsigned off = 54, dib = 40, planes_bpp = 1 | (32 << 16);
    int nh = -h;   /* top-down */
    memcpy(hd + 10, &off, 4); memcpy(hd + 14, &dib, 4); memcpy(hd + 18, &w, 4); memcpy(hd + 22, &nh, 4);
    memcpy(hd + 26, &planes_bpp, 4);
    fwrite(hd, 1, 54, f);
    for (int y = 0; y < h; y++) fwrite((char *)snap->data + (size_t)y * snap->header.stride, 4, w, f);
    fclose(f);
    lv_draw_buf_destroy(snap);
}

/* --taps "x,y x,y ...": scripted touches in shot mode (press 150 ms, then 600 ms for animations/jobs). */
static int tap_x[64], tap_y[64], tap_hold[64], n_taps, tap_i;   /* hold: ms the finger stays down */
static uint32_t tap_t0;

static void tap_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    static int last_x, last_y;   /* a release must report the press position, or LVGL sees a drag */
    data->point.x = last_x;
    data->point.y = last_y;
    data->state = LV_INDEV_STATE_RELEASED;
    if (tap_i >= n_taps) return;
    uint32_t dt = lv_tick_get() - tap_t0;
    if (dt < 600) return;                      /* settle before the next touch */
    last_x = data->point.x = tap_x[tap_i];
    last_y = data->point.y = tap_y[tap_i];
    if (dt < 600 + (uint32_t)tap_hold[tap_i] + 150) { data->state = LV_INDEV_STATE_PRESSED; return; }
    printf("[sim] tap %d done t=%u active=%p\n", tap_i, (unsigned)lv_tick_get(), (void *)lv_screen_active());
    tap_i++;
    tap_t0 = lv_tick_get();
}

static void dbg_ev(lv_event_t *e)
{
    lv_event_code_t c = lv_event_get_code(e);
    if (c == LV_EVENT_PRESSED || c == LV_EVENT_RELEASED || c == LV_EVENT_CLICKED || c == LV_EVENT_PRESSING || c == LV_EVENT_GESTURE || c == LV_EVENT_SCROLL_BEGIN)
        printf("[ev] %d\n", (int)c);
}

static void settle(int ms)
{
    for (int t = 0; t < ms; t += 10) {
        sim_lvgl_lock();
        lv_timer_handler();
        sim_lvgl_unlock();
        usleep(10000);   /* long enough for a waiting worker thread to take the lock */
        lv_tick_inc(10);
    }
}

int main(int argc, char **argv)
{
    const char *shot = NULL, *screen = NULL, *taps = NULL;
    int lang = 0;
    for (int i = 1; i + 1 < argc; i++) {
        if (!strcmp(argv[i], "--shot")) shot = argv[i + 1];
        if (!strcmp(argv[i], "--screen")) screen = argv[i + 1];
        if (!strcmp(argv[i], "--taps")) taps = argv[i + 1];
        if (!strcmp(argv[i], "--lang")) lang = !strcmp(argv[i + 1], "zh");
    }
    setenv("TZ", "SGT-8", 1);
    tzset();
    lv_init();
    lv_display_t *d = lv_sdl_window_create(HAL_LCD_RES, HAL_LCD_RES);
    lv_sdl_window_set_title(d, "Panda Pocket (sim)");
    if (taps) {
        const char *p = taps;
        int x, y, n;
        while (n_taps < 64 && sscanf(p, " %d,%d%n", &x, &y, &n) == 2) {
            int hold = 0, m = 0;
            p += n;
            if (*p == ',' && sscanf(p, ",%d%n", &hold, &m) == 1) p += m;   /* x,y,hold_ms: a long press */
            tap_x[n_taps] = x; tap_y[n_taps] = y; tap_hold[n_taps++] = hold;
        }
        lv_indev_t *in = lv_indev_create();
        lv_indev_set_type(in, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(in, tap_read);
        tap_t0 = lv_tick_get();
    } else {
        lv_sdl_mouse_create();
    }

    static pp_settings_t set = { .brightness = 80 };
    set.lang = lang;
    ui_start(&set);
    if (getenv("PP_SIM_DEBUG")) lv_obj_add_event_cb(lv_screen_active(), dbg_ev, LV_EVENT_ALL, NULL);
    if (screen) ui_sim_open(screen);

    if (shot) {
        settle(getenv("PP_SIM_SETTLE_MS") ? atoi(getenv("PP_SIM_SETTLE_MS")) : (taps ? 1500 + n_taps * 1400 : 1500));
        if (getenv("PP_SIM_SCROLL")) { ui_sim_scroll_steps(atoi(getenv("PP_SIM_SCROLL"))); settle(300); }
        save_bmp(shot);
        return 0;
    }
    for (;;) settle(10);
}
