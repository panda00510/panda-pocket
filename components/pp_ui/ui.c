/*
 * Panda UI: Home (animated eyes + status), menu, and thin Bus / Navigate /
 * Settings screens wired to the data modules. Screen stack: tap pushes,
 * swipe right pops. Network calls run off the LVGL task.
 * NOT compiled or run yet: written against LVGL 9.5 from memory.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "bus.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal_board.h"
#include "i18n.h"
#include "nav.h"
#include "pp_config.h"
#include "sdkconfig.h"
#include "ui.h"
#include "wifi.h"

#define STACK_MAX 6
#define W HAL_LCD_RES
#define SC(v) ((v) * W / 360)   /* layout is drawn for 360 px, scaled to the panel */
#define C_BG lv_color_hex(0x101010)
#define C_FG lv_color_hex(0xF2F2F2)
#define C_DIM lv_color_hex(0x8A8A8A)

extern const lv_font_t pp_font_cjk_14, pp_font_cjk_16, pp_font_cjk_20;   /* fonts/, from tools/gen_cjk_font.sh */
static lv_font_t s_f14, s_f16, s_f20;
#define F14 (&s_f14)
#define F16 (&s_f16)
#define F20 (&s_f20)

static void fonts_init(void)
{
    s_f14 = lv_font_montserrat_14;
    s_f14.fallback = &pp_font_cjk_14;
    s_f16 = lv_font_montserrat_16;
    s_f16.fallback = &pp_font_cjk_16;
    s_f20 = lv_font_montserrat_20;
    s_f20.fallback = &pp_font_cjk_20;
}

static pp_settings_t *s_set;
static lv_obj_t *s_stack[STACK_MAX];
static int s_depth;
static bool s_asleep;

/* ---------- screen stack ---------- */

static void on_gesture(lv_event_t *e);

static lv_obj_t *new_screen(const char *title)
{
    lv_obj_t *s = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s, C_BG, 0);
    lv_obj_set_style_text_color(s, C_FG, 0);
    lv_obj_set_style_text_font(s, F20, 0);   /* children inherit: Latin + CJK fallback */
    lv_obj_remove_flag(s, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s, on_gesture, LV_EVENT_GESTURE, NULL);
    if (title) {
        lv_obj_t *t = lv_label_create(s);
        lv_label_set_text(t, title);
        lv_obj_set_style_text_font(t, F20, 0);
        lv_obj_align(t, LV_ALIGN_TOP_MID, 0, SC(28));   /* inside the circle */
    }
    return s;
}

static void push(lv_obj_t *s)
{
    if (s_depth >= STACK_MAX) { lv_obj_delete(s); return; }
#ifdef PP_SIM
    printf("[sim] push -> depth %d\n", s_depth + 1);
#endif
    s_stack[s_depth++] = s;
    lv_screen_load_anim(s, LV_SCREEN_LOAD_ANIM_MOVE_LEFT, 200, 0, false);
}

/* Back n screens at once (the ones in between are deleted, the one shown is deleted by the load). */
static void pop_n(int n)
{
    if (n > s_depth - 1) n = s_depth - 1;
    if (n < 1) return;
#ifdef PP_SIM
    printf("[sim] pop %d -> depth %d\n", n, s_depth - n);
#endif
    for (int i = 1; i < n; i++) lv_obj_delete(s_stack[s_depth - 1 - i]);
    s_depth -= n;
    lv_screen_load_anim(s_stack[s_depth - 1], LV_SCREEN_LOAD_ANIM_MOVE_RIGHT, 200, 0, true);
}

static void pop(void) { pop_n(1); }

static void on_gesture(lv_event_t *e)
{
    if (lv_indev_get_gesture_dir(lv_indev_active()) == LV_DIR_RIGHT) pop();
}

/* Runs fn on a worker task (never block the LVGL task on HTTP). */
typedef struct { void (*fn)(void); } job_t;
static void job_task(void *a)
{
    ((job_t *)a)->fn();
    free(a);
    vTaskDelete(NULL);
}
static void run_async(void (*fn)(void))
{
    job_t *j = malloc(sizeof *j);
    if (!j) return;
    j->fn = fn;
    if (xTaskCreate(job_task, "ui_job", 8192, j, 3, NULL) != pdPASS) free(j);
}

/* Set label text from any task. */
static void label_set(lv_obj_t *l, const char *txt)
{
    if (hal_lock(1000)) {
        if (lv_obj_is_valid(l)) lv_label_set_text(l, txt);
        hal_unlock();
    }
}

/* ---------- Bus ---------- */

static favs_t s_favs;
static char s_stop[BUS_STOP_LEN];   /* stop shown on the arrivals screen */
static lv_obj_t *s_bus_head, *s_bus_table, *s_save_label;
static uint8_t s_cell_load[5][3];   /* bus_load_t per table cell (columns 1 and 2), for the text colour */

/* Colour the load text per cell: green seats, amber standing, red limited. The words stay, for colour-blind users. */
static void on_bus_draw(lv_event_t *e)
{
    lv_draw_task_t *t = lv_event_get_draw_task(e);
    lv_draw_dsc_base_t *base = lv_draw_task_get_draw_dsc(t);
    lv_draw_label_dsc_t *ld = lv_draw_task_get_label_dsc(t);
    if (!ld || base->part != LV_PART_ITEMS || base->id1 >= 5 || base->id2 < 1 || base->id2 > 2) return;
    switch (s_cell_load[base->id1][base->id2]) {
    case BUS_LOAD_SEATS: ld->color = lv_color_hex(0x4CC38A); break;
    case BUS_LOAD_STANDING: ld->color = lv_color_hex(0xFFB020); break;
    case BUS_LOAD_LIMITED: ld->color = lv_color_hex(0xE5484D); break;
    default: break;
    }
}
/* " seat" / " stand" / " full", translated */
static const char *load_word(int load)
{
    static char b[2][16];   /* two cells are formatted per call site; reused immediately */
    static int k;
    if (load < BUS_LOAD_SEATS || load > BUS_LOAD_LIMITED) return "";
    k ^= 1;
    snprintf(b[k], sizeof b[k], " %s", tr(load == BUS_LOAD_SEATS ? T_SEAT : load == BUS_LOAD_STANDING ? T_STAND : T_CROWDED));
    return b[k];
}

static void bus_job(void)
{
    static bus_arrivals_t a;   /* too big for the job's stack */
    bool mock = false;
    char stop[BUS_STOP_LEN];
    snprintf(stop, sizeof stop, "%s", s_stop);
    bus_err_t e = bus_fetch(stop, &a, &mock);
    if (!hal_lock(1000)) return;
    if (lv_obj_is_valid(s_bus_head) && lv_obj_is_valid(s_bus_table)) {
        if (e != BUS_OK) {
            lv_label_set_text(s_bus_head, tr(e == BUS_ERR_NET ? T_ERR_NET : e == BUS_ERR_AUTH ? T_ERR_KEY : T_ERR_API));
            lv_table_set_row_count(s_bus_table, 0);
        } else if (a.n == 0) {   /* LTA answers 200 with an empty list for a stop that does not exist */
            lv_label_set_text(s_bus_head, tr(T_NO_BUSES));
            lv_table_set_row_count(s_bus_table, 0);
        } else {
            lv_label_set_text(s_bus_head, mock ? tr(T_MOCK) : tr(T_LIVE));
            int rows = a.n < 5 ? a.n : 5;
            lv_table_set_row_count(s_bus_table, rows);
            for (int i = 0; i < rows; i++) {
                lv_table_set_cell_value(s_bus_table, i, 0, a.svc[i].service);
                for (int j = 0; j < 2; j++) {
                    s_cell_load[i][1 + j] = a.svc[i].eta_min[j] < 0 ? BUS_LOAD_UNKNOWN : a.svc[i].load[j];
                    int v = a.svc[i].eta_min[j];
                    if (v < 0) lv_table_set_cell_value(s_bus_table, i, 1 + j, "-");
                    else if (v == 0) lv_table_set_cell_value_fmt(s_bus_table, i, 1 + j, "%s%s", tr(T_ARR), load_word(a.svc[i].load[j]));
                    else lv_table_set_cell_value_fmt(s_bus_table, i, 1 + j, "%d %s%s", v, tr(T_MIN), load_word(a.svc[i].load[j]));
                }
            }
        }
    }
    hal_unlock();
}

static void bus_tick(lv_timer_t *t)
{
    if (!lv_obj_is_valid(s_bus_table)) { lv_timer_delete(t); return; }
    run_async(bus_job);
}

static bool is_fav(void)
{
    for (int i = 0; i < s_favs.n; i++) if (!strcmp(s_favs.code[i], s_stop)) return true;
    return false;
}

static void on_save(lv_event_t *e)
{
    if (is_fav()) {
        favs_remove(&s_favs, s_stop);
    } else if (favs_add(&s_favs, s_stop) != 0) {
        lv_label_set_text(s_save_label, tr(T_FULL));
        return;
    }
    storage_favs_save(&s_favs);
    lv_label_set_text(s_save_label, is_fav() ? tr(T_SAVED) : tr(T_SAVE));
}

/* Arrivals for `stop`; replace_top swaps it for the numpad on the stack instead of stacking on it. */
static void show_arrivals(const char *stop, bool replace_top)
{
    snprintf(s_stop, sizeof s_stop, "%s", stop);
    lv_obj_t *s = new_screen(s_stop);
    s_bus_head = lv_label_create(s);
    lv_label_set_text(s_bus_head, tr(T_LOADING));
    lv_obj_set_style_text_color(s_bus_head, C_DIM, 0);
    lv_obj_align(s_bus_head, LV_ALIGN_TOP_MID, 0, SC(62));
    s_bus_table = lv_table_create(s);
    lv_table_set_column_count(s_bus_table, 3);
    lv_table_set_column_width(s_bus_table, 0, SC(80));
    lv_table_set_column_width(s_bus_table, 1, SC(110));
    lv_table_set_column_width(s_bus_table, 2, SC(110));
    lv_obj_set_style_bg_opa(s_bus_table, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_bus_table, 0, 0);
    lv_obj_set_style_bg_opa(s_bus_table, LV_OPA_TRANSP, LV_PART_ITEMS);
    lv_obj_set_style_border_width(s_bus_table, 0, LV_PART_ITEMS);
    lv_obj_set_style_pad_ver(s_bus_table, 6, LV_PART_ITEMS);
    lv_obj_set_style_text_color(s_bus_table, C_FG, LV_PART_ITEMS);
    lv_obj_set_style_text_font(s_bus_table, F16, LV_PART_ITEMS);
    lv_obj_align(s_bus_table, LV_ALIGN_TOP_MID, 0, SC(88));
    memset(s_cell_load, 0, sizeof s_cell_load);
    lv_obj_add_flag(s_bus_table, LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS);
    lv_obj_add_event_cb(s_bus_table, on_bus_draw, LV_EVENT_DRAW_TASK_ADDED, NULL);

    lv_obj_t *b = lv_button_create(s);
    lv_obj_set_size(b, SC(130), SC(38));
    lv_obj_align(b, LV_ALIGN_BOTTOM_MID, 0, -SC(30));
    s_save_label = lv_label_create(b);
    lv_label_set_text(s_save_label, is_fav() ? tr(T_SAVED) : tr(T_SAVE));
    lv_obj_center(s_save_label);
    lv_obj_add_event_cb(b, on_save, LV_EVENT_CLICKED, NULL);

    lv_timer_create(bus_tick, 30000, NULL);   /* above BUS_MIN_REFRESH_S */
    run_async(bus_job);
    if (replace_top && s_depth > 0) {
        s_stack[s_depth - 1] = s;
        lv_screen_load_anim(s, LV_SCREEN_LOAD_ANIM_MOVE_LEFT, 200, 0, true);   /* deletes the numpad */
    } else {
        push(s);
    }
}

/* ---- numpad: five digits, OK enabled only when complete ---- */

#define NUM_BTN_BACKSPACE 9
#define NUM_BTN_OK 11
static const char *const NUM_MAP[] = { "1", "2", "3", "\n", "4", "5", "6", "\n", "7", "8", "9", "\n",
                                       LV_SYMBOL_BACKSPACE, "0", LV_SYMBOL_OK, "" };
static char s_digits[BUS_STOP_LEN];
static int s_nd;
static lv_obj_t *s_digits_label, *s_numpad;

static void digits_refresh(void)
{
    char b[24];
    int o = 0;
    for (int i = 0; i < 5; i++) o += snprintf(b + o, sizeof b - o, "%c ", i < s_nd ? s_digits[i] : '_');
    lv_label_set_text(s_digits_label, b);
    if (s_nd == 5) lv_buttonmatrix_clear_button_ctrl(s_numpad, NUM_BTN_OK, LV_BUTTONMATRIX_CTRL_DISABLED);
    else lv_buttonmatrix_set_button_ctrl(s_numpad, NUM_BTN_OK, LV_BUTTONMATRIX_CTRL_DISABLED);
}

static void on_num(lv_event_t *e)
{
    uint32_t id = lv_buttonmatrix_get_selected_button(s_numpad);
    const char *t = lv_buttonmatrix_get_button_text(s_numpad, id);
    if (!t) return;
    if (t[0] >= '0' && t[0] <= '9' && !t[1]) {
        if (s_nd < 5) { s_digits[s_nd++] = t[0]; s_digits[s_nd] = 0; }
    } else if (id == NUM_BTN_BACKSPACE) {
        if (s_nd > 0) s_digits[--s_nd] = 0;
    } else if (id == NUM_BTN_OK && bus_stop_valid(s_digits)) {
        show_arrivals(s_digits, true);
        return;
    }
    digits_refresh();
}

static void open_numpad(lv_event_t *e)
{
    s_nd = 0;
    s_digits[0] = 0;
    lv_obj_t *s = new_screen(tr(T_NEW_STOP));
    s_digits_label = lv_label_create(s);
    lv_obj_set_style_text_font(s_digits_label, &lv_font_montserrat_28, 0);
    lv_obj_align(s_digits_label, LV_ALIGN_TOP_MID, 0, SC(66));
    s_numpad = lv_buttonmatrix_create(s);
    lv_buttonmatrix_set_map(s_numpad, NUM_MAP);
    lv_obj_set_size(s_numpad, SC(220), SC(180));
    lv_obj_align(s_numpad, LV_ALIGN_TOP_MID, 0, SC(110));
    lv_obj_set_style_bg_opa(s_numpad, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_numpad, 0, 0);
    lv_obj_set_style_pad_all(s_numpad, 0, 0);
    lv_obj_add_event_cb(s_numpad, on_num, LV_EVENT_VALUE_CHANGED, NULL);
    digits_refresh();
    push(s);
}

/* ---- favorites list ---- */

static void fill_favs(lv_obj_t *list);

static void refill_async(void *list) { if (lv_obj_is_valid(list)) fill_favs(list); }

/* Long press removes a favorite (the list is rebuilt after the event, not from inside it). */
static void on_fav_long(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target_obj(e);
    lv_obj_t *list = lv_obj_get_parent(btn);
    favs_remove(&s_favs, lv_list_get_button_text(list, btn));
    storage_favs_save(&s_favs);
    lv_async_call(refill_async, list);
}

static void on_fav(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target_obj(e);
    show_arrivals(lv_list_get_button_text(lv_obj_get_parent(btn), btn), false);   /* by text: the list may be stale */
}

/* Rebuilt every time the screen is shown: a stop may have been un-saved on the arrivals screen. */
static void fill_favs(lv_obj_t *list)
{
    lv_obj_clean(list);
    for (int i = 0; i < s_favs.n; i++) {
        lv_obj_t *b = lv_list_add_button(list, NULL, s_favs.code[i]);
        lv_obj_add_event_cb(b, on_fav, LV_EVENT_SHORT_CLICKED, NULL);   /* not after a long press */
        lv_obj_add_event_cb(b, on_fav_long, LV_EVENT_LONG_PRESSED, NULL);
    }
    char t[32];
    snprintf(t, sizeof t, "%s %s", LV_SYMBOL_PLUS, tr(T_NEW_STOP));
    lv_obj_add_event_cb(lv_list_add_button(list, NULL, t), open_numpad, LV_EVENT_CLICKED, NULL);
}

static void on_favs_show(lv_event_t *e) { fill_favs(lv_event_get_user_data(e)); }

static void open_bus(lv_event_t *e)
{
    lv_obj_t *s = new_screen(tr(T_BUS));
    lv_obj_t *list = lv_list_create(s);
    lv_obj_set_size(list, SC(220), SC(200));
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, SC(78));
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    fill_favs(list);
    lv_obj_t *hint = lv_label_create(s);
    lv_label_set_text(hint, tr(T_HOLD_DEL));
    lv_obj_set_style_text_color(hint, C_DIM, 0);
    lv_obj_set_style_text_font(hint, F14, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -SC(46));
    lv_obj_add_event_cb(s, on_favs_show, LV_EVENT_SCREEN_LOAD_START, list);
    push(s);
}

/* ---------- Navigate: pick from / to, then a route preview ---------- */

static int s_from = 0, s_to = 1;   /* indexes into NAV_PLACES */

static const char *place_name(int i) { return tr_lang() ? NAV_PLACES[i].name_zh : NAV_PLACES[i].name_en; }

static lv_obj_t *s_nav_label, *s_nav_steps;   /* summary text; scrollable list of every step */
static lv_obj_t *s_nav_line;
static lv_point_precise_t s_nav_pts[NAV_MAX_PTS];
static lv_obj_t *s_nav_a, *s_nav_b;   /* start / end markers, on the screen above the line */
#define MARK_D SC(20)

static lv_obj_t *route_marker(lv_obj_t *screen, const char *letter, uint32_t color)
{
    lv_obj_t *m = lv_obj_create(screen);
    lv_obj_remove_style_all(m);
    lv_obj_remove_flag(m, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(m, MARK_D, MARK_D);
    lv_obj_set_style_radius(m, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(m, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(m, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(m, 2, 0);
    lv_obj_set_style_border_color(m, lv_color_hex(0xF2F2F2), 0);
    lv_obj_add_flag(m, LV_OBJ_FLAG_HIDDEN);   /* until the route is known */
    lv_obj_t *l = lv_label_create(m);
    lv_label_set_text(l, letter);
    lv_obj_set_style_text_font(l, F14, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(0x101010), 0);
    lv_obj_center(l);
    return m;
}

static void nav_job(void)
{
    static nav_route_t r;
    bool mock = false;
    char out[160];
    int from = s_from, to = s_to;
    nav_err_t e = nav_route(from, to, &r, &mock);
    if (e != NAV_OK) {
        label_set(s_nav_label, tr(e == NAV_ERR_NET ? T_ERR_NET : e == NAV_ERR_AUTH ? T_ERR_TOKEN : T_ERR_API));
        return;
    }
    snprintf(out, sizeof out, "%s > %s\n%d m  %d %s  %s", place_name(from), place_name(to), r.dist_m,
             (r.time_s + 30) / 60, tr(T_MIN), mock ? tr(T_MOCK) : tr(T_LIVE));
    static char steps[NAV_MAX_STEPS * 80];   /* every step; the list scrolls */
    int o = 0;
    for (int i = 0; i < r.n_steps && o < (int)sizeof steps; i++) {
        o += snprintf(steps + o, sizeof steps - o, "%d. %s", i + 1, r.steps[i].text);
        if (r.steps[i].dist_m > 0 && o < (int)sizeof steps) o += snprintf(steps + o, sizeof steps - o, "  %d m", r.steps[i].dist_m);   /* OneMap walk steps carry no road names */
        if (o < (int)sizeof steps) o += snprintf(steps + o, sizeof steps - o, "\n");
    }
    if (o > 0 && o <= (int)sizeof steps && steps[o - 1] == '\n') steps[o - 1] = 0;   /* no blank line to scroll to */

    /* Schematic: fit points into a 200 px box, lat flipped to screen y. */
    float la0 = r.pts[0].lat, la1 = la0, lo0 = r.pts[0].lng, lo1 = lo0;
    for (int i = 1; i < r.n_pts; i++) {
        if (r.pts[i].lat < la0) la0 = r.pts[i].lat;
        if (r.pts[i].lat > la1) la1 = r.pts[i].lat;
        if (r.pts[i].lng < lo0) lo0 = r.pts[i].lng;
        if (r.pts[i].lng > lo1) lo1 = r.pts[i].lng;
    }
    float span = (la1 - la0 > lo1 - lo0 ? la1 - la0 : lo1 - lo0);
    if (span < 1e-5f) span = 1e-5f;   /* equirectangular without cos(lat): fine at 1.3 N for a sketch */
    if (hal_lock(1000)) {
        if (lv_obj_is_valid(s_nav_line)) {
            for (int i = 0; i < r.n_pts; i++) {
                s_nav_pts[i].x = (lv_value_precise_t)((r.pts[i].lng - lo0) / span * SC(230));
                s_nav_pts[i].y = (lv_value_precise_t)((la1 - r.pts[i].lat) / span * SC(115));
            }
            lv_line_set_points(s_nav_line, s_nav_pts, r.n_pts);
            lv_obj_update_layout(s_nav_line);   /* it was just resized and re-centred: read where it really is */
            int ox = lv_obj_get_x(s_nav_line) - MARK_D / 2, oy = lv_obj_get_y(s_nav_line) - MARK_D / 2;
            lv_obj_set_pos(s_nav_a, ox + (int)s_nav_pts[0].x, oy + (int)s_nav_pts[0].y);
            lv_obj_set_pos(s_nav_b, ox + (int)s_nav_pts[r.n_pts - 1].x, oy + (int)s_nav_pts[r.n_pts - 1].y);
            lv_obj_remove_flag(s_nav_a, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(s_nav_b, LV_OBJ_FLAG_HIDDEN);
        }
        hal_unlock();
    }
    label_set(s_nav_label, out);
    label_set(lv_obj_get_child(s_nav_steps, 0), steps);
}

static void open_route(lv_event_t *e)
{
    lv_obj_t *s = new_screen(tr(T_NAV));
    lv_obj_t *banner = lv_label_create(s);
    lv_label_set_text(banner, tr(T_PREVIEW));
    lv_obj_set_style_text_color(banner, lv_color_hex(0xFFB020), 0);   /* never pass as live positioning */
    lv_obj_set_style_text_font(banner, F14, 0);
    lv_obj_align(banner, LV_ALIGN_TOP_MID, 0, SC(56));
    s_nav_line = lv_line_create(s);
    lv_obj_set_style_line_width(s_nav_line, 4, 0);
    lv_obj_set_style_line_color(s_nav_line, lv_color_hex(0x4CC38A), 0);
    lv_obj_align(s_nav_line, LV_ALIGN_CENTER, 0, -SC(20));   /* the line is as big as the route */
    s_nav_a = route_marker(s, "A", 0x4CC38A);
    s_nav_b = route_marker(s, "B", 0xE5484D);
    s_nav_label = lv_label_create(s);
    lv_label_set_text(s_nav_label, tr(T_LOADING));
    lv_obj_set_style_text_font(s_nav_label, F14, 0);
    lv_obj_set_style_text_align(s_nav_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_nav_label, LV_ALIGN_BOTTOM_MID, 0, -SC(106));
    /* The steps: three lines visible, drag up/down for the rest (a sideways swipe still goes back). */
    s_nav_steps = lv_obj_create(s);
    lv_obj_remove_style_all(s_nav_steps);
    lv_obj_set_size(s_nav_steps, SC(240), 4 * lv_font_get_line_height(F14));   /* exactly four lines: no half line */
    lv_obj_align(s_nav_steps, LV_ALIGN_BOTTOM_MID, 0, -SC(52));
    lv_obj_set_scroll_dir(s_nav_steps, LV_DIR_VER);
    lv_obj_t *sl = lv_label_create(s_nav_steps);
    lv_label_set_text(sl, "");
    lv_obj_set_width(sl, SC(240));
    lv_obj_set_style_text_font(sl, F14, 0);
    lv_obj_set_style_text_align(sl, LV_TEXT_ALIGN_CENTER, 0);
    run_async(nav_job);
    push(s);
}

static bool s_pick_to;
static lv_obj_t *s_from_lbl, *s_to_lbl, *s_go_btn;

static void nav_refresh(void)
{
    lv_label_set_text_fmt(s_from_lbl, "%s: %s", tr(T_FROM), place_name(s_from));
    lv_label_set_text_fmt(s_to_lbl, "%s: %s", tr(T_TO), place_name(s_to));
    if (s_from == s_to) lv_obj_add_state(s_go_btn, LV_STATE_DISABLED);   /* nothing to walk */
    else lv_obj_remove_state(s_go_btn, LV_STATE_DISABLED);
}

static void on_picker_show(lv_event_t *e) { nav_refresh(); }

static void on_place(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (s_pick_to) s_to = i;
    else s_from = i;
    pop();   /* the picker refreshes itself when it is shown again */
}

static void open_places(bool to)
{
    s_pick_to = to;
    lv_obj_t *s = new_screen(tr(to ? T_TO : T_FROM));
    lv_obj_t *list = lv_list_create(s);
    lv_obj_set_size(list, SC(250), SC(220));
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, SC(78));
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    for (int i = 0; i < NAV_N_PLACES; i++) {
        lv_obj_t *b = lv_list_add_button(list, NULL, place_name(i));
        lv_obj_add_event_cb(b, on_place, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
    push(s);
}

static void on_pick_from(lv_event_t *e) { open_places(false); }
static void on_pick_to(lv_event_t *e) { open_places(true); }

static lv_obj_t *picker_row(lv_obj_t *s, int y, lv_event_cb_t cb)
{
    lv_obj_t *b = lv_button_create(s);
    lv_obj_set_size(b, SC(270), SC(46));
    lv_obj_align(b, LV_ALIGN_TOP_MID, 0, SC(y));
    lv_obj_t *l = lv_label_create(b);
    lv_obj_set_style_text_font(l, F16, 0);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    return l;
}

static void open_nav(lv_event_t *e)
{
    lv_obj_t *s = new_screen(tr(T_NAV));
    s_from_lbl = picker_row(s, 82, on_pick_from);
    s_to_lbl = picker_row(s, 142, on_pick_to);
    s_go_btn = lv_button_create(s);
    lv_obj_set_size(s_go_btn, SC(140), SC(46));
    lv_obj_align(s_go_btn, LV_ALIGN_TOP_MID, 0, SC(214));
    lv_obj_t *gl = lv_label_create(s_go_btn);
    lv_label_set_text(gl, tr(T_GO));
    lv_obj_center(gl);
    lv_obj_add_event_cb(s_go_btn, open_route, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s, on_picker_show, LV_EVENT_SCREEN_LOAD_START, NULL);
    nav_refresh();
    push(s);
}

/* ---------- Wi-Fi: scan list -> password -> connect ---------- */

static lv_obj_t *s_wifi_status, *s_wifi_list, *s_pw_ta, *s_pw_show_lbl;
static char s_pick_ssid[WIFI_SSID_MAX];
static bool s_pick_secure;
static int s_wifi_base;   /* stack depth of Settings: connecting pops back to it, however many screens deep */

/* Why a connection attempt is failing, or "" */
static const char *wifi_problem(void)
{
    if (wifi_state() == WIFI_UP) return "";
    return wifi_last_error() == WIFI_ERR_AUTH ? tr(T_WRONG_PW) : wifi_last_error() == WIFI_ERR_NO_AP ? tr(T_NO_AP) : "";
}

static void wifi_status_text(char *b, size_t n)
{
    if (!wifi_ssid()[0]) snprintf(b, n, "%s", tr(T_NOT_SET));
    else snprintf(b, n, "%s: %s\n%s", wifi_state() == WIFI_UP ? tr(T_CONNECTED) : tr(T_CONNECTING), wifi_ssid(), wifi_problem());
}

static void connect_to(const char *ssid, const char *pass)
{
    snprintf(s_set->ssid, sizeof s_set->ssid, "%s", ssid);
    snprintf(s_set->pass, sizeof s_set->pass, "%s", pass);
    storage_settings_save(s_set);   /* TODO: NVS holds the password in plain text */
    wifi_reconfigure(ssid, pass);
}

static void on_pw_typed(lv_event_t *e) { lv_obj_set_style_border_color(s_pw_ta, lv_color_hex(0x3A3A3A), 0); }

static void on_pw_show(lv_event_t *e)
{
    bool hide = !lv_textarea_get_password_mode(s_pw_ta);
    lv_textarea_set_password_mode(s_pw_ta, hide);
    lv_label_set_text(s_pw_show_lbl, tr(hide ? T_SHOW : T_HIDE));
}

static void on_pw_key(lv_event_t *e)
{
    lv_event_code_t c = lv_event_get_code(e);
    if (c == LV_EVENT_CANCEL) {
        pop();
    } else if (c == LV_EVENT_READY) {
        const char *pw = lv_textarea_get_text(s_pw_ta);
        if (strlen(pw) < 8) {   /* WPA passphrases are 8..63 characters */
            lv_obj_set_style_border_color(s_pw_ta, lv_color_hex(0xE5484D), 0);
            return;
        }
        connect_to(s_pick_ssid, pw);
        pop_n(s_depth - s_wifi_base);   /* password, name entry and scan list: back to Settings */
    }
}

static void open_password(void)
{
    lv_obj_t *s = new_screen(s_pick_ssid);
    s_pw_ta = lv_textarea_create(s);
    lv_textarea_set_one_line(s_pw_ta, true);
    lv_textarea_set_password_mode(s_pw_ta, true);
    lv_textarea_set_max_length(s_pw_ta, 63);
    lv_textarea_set_placeholder_text(s_pw_ta, tr(T_PASSWORD));
    lv_obj_add_event_cb(s_pw_ta, on_pw_typed, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_set_size(s_pw_ta, SC(190), SC(40));
    lv_obj_align(s_pw_ta, LV_ALIGN_TOP_MID, -SC(35), SC(58));
    lv_obj_t *sb = lv_button_create(s);
    lv_obj_set_size(sb, SC(60), SC(40));
    lv_obj_align(sb, LV_ALIGN_TOP_MID, SC(95), SC(58));
    s_pw_show_lbl = lv_label_create(sb);
    lv_obj_set_style_text_font(s_pw_show_lbl, F14, 0);
    lv_label_set_text(s_pw_show_lbl, tr(T_SHOW));
    lv_obj_center(s_pw_show_lbl);
    lv_obj_add_event_cb(sb, on_pw_show, LV_EVENT_CLICKED, NULL);
    lv_obj_t *kb = lv_keyboard_create(s);
    lv_obj_set_size(kb, SC(290), SC(172));
    lv_obj_align(kb, LV_ALIGN_TOP_MID, 0, SC(110));
    lv_keyboard_set_textarea(kb, s_pw_ta);
    lv_obj_add_event_cb(kb, on_pw_key, LV_EVENT_ALL, NULL);
    push(s);
}

/* ---- a network that is not in the scan list (hidden): type its name, then the password ---- */

static lv_obj_t *s_ssid_ta;

static void on_ssid_key(lv_event_t *e)
{
    lv_event_code_t c = lv_event_get_code(e);
    if (c == LV_EVENT_CANCEL) {
        pop();
    } else if (c == LV_EVENT_READY) {
        const char *t = lv_textarea_get_text(s_ssid_ta);
        if (!*t) {
            lv_obj_set_style_border_color(s_ssid_ta, lv_color_hex(0xE5484D), 0);
            return;
        }
        snprintf(s_pick_ssid, sizeof s_pick_ssid, "%s", t);
        s_pick_secure = true;   /* hidden open networks are not supported */
        open_password();
    }
}

static void open_ssid_entry(lv_event_t *e)
{
    lv_obj_t *s = new_screen(tr(T_OTHER_NET));
    s_ssid_ta = lv_textarea_create(s);
    lv_textarea_set_one_line(s_ssid_ta, true);
    lv_textarea_set_max_length(s_ssid_ta, WIFI_SSID_MAX - 1);
    lv_textarea_set_placeholder_text(s_ssid_ta, tr(T_SSID));
    lv_obj_set_size(s_ssid_ta, SC(250), SC(40));
    lv_obj_align(s_ssid_ta, LV_ALIGN_TOP_MID, 0, SC(58));
    lv_obj_t *kb = lv_keyboard_create(s);
    lv_obj_set_size(kb, SC(290), SC(172));
    lv_obj_align(kb, LV_ALIGN_TOP_MID, 0, SC(110));
    lv_keyboard_set_textarea(kb, s_ssid_ta);
    lv_obj_add_event_cb(kb, on_ssid_key, LV_EVENT_ALL, NULL);
    push(s);
}

static void on_ap(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target_obj(e);
    snprintf(s_pick_ssid, sizeof s_pick_ssid, "%s", lv_list_get_button_text(lv_obj_get_parent(btn), btn));
    s_pick_secure = (bool)(intptr_t)lv_event_get_user_data(e);
    char *open = strrchr(s_pick_ssid, '(');   /* the label's " (open)" suffix, not part of the name */
    if (open && open > s_pick_ssid && open[-1] == ' ') open--;
    else open = NULL;
    if (!s_pick_secure && open) *open = 0;
    if (s_pick_secure) {
        open_password();
    } else {
        connect_to(s_pick_ssid, "");
        pop();
    }
}

static void scan_job(void)
{
    static wifi_ap_t aps[12];
    int n = wifi_scan(aps, 12);
    if (!hal_lock(1000)) return;
    if (lv_obj_is_valid(s_wifi_list)) {
        lv_obj_clean(s_wifi_list);
        if (n <= 0) {
            lv_list_add_text(s_wifi_list, tr(n < 0 ? T_ERR_NET : T_NO_NETWORKS));
        } else {
            for (int i = 0; i < n; i++) {
                char label[48];
                snprintf(label, sizeof label, "%.32s%s%s%s", aps[i].ssid, aps[i].secure ? "" : " (", aps[i].secure ? "" : tr(T_OPEN), aps[i].secure ? "" : ")");
                lv_obj_t *b = lv_list_add_button(s_wifi_list, LV_SYMBOL_WIFI, label);
                lv_obj_add_event_cb(b, on_ap, LV_EVENT_CLICKED, (void *)(intptr_t)aps[i].secure);
            }
        }
        char other[40];   /* always offered: a hidden network is not in the scan */
        snprintf(other, sizeof other, "%s %s", LV_SYMBOL_PLUS, tr(T_OTHER_NET));
        lv_obj_add_event_cb(lv_list_add_button(s_wifi_list, NULL, other), open_ssid_entry, LV_EVENT_CLICKED, NULL);
    }
    hal_unlock();
}

static void open_wifi(lv_event_t *e)
{
    lv_obj_t *s = new_screen(tr(T_WIFI));
    s_wifi_status = lv_label_create(s);
    lv_obj_set_style_text_color(s_wifi_status, C_DIM, 0);
    lv_obj_set_style_text_font(s_wifi_status, F14, 0);
    char t[128];
    wifi_status_text(t, sizeof t);
    lv_label_set_text(s_wifi_status, t);
    lv_obj_align(s_wifi_status, LV_ALIGN_TOP_MID, 0, SC(62));
    s_wifi_list = lv_list_create(s);
    lv_obj_set_size(s_wifi_list, SC(250), SC(200));
    lv_obj_align(s_wifi_list, LV_ALIGN_TOP_MID, 0, SC(88));
    lv_obj_set_style_bg_opa(s_wifi_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_wifi_list, 0, 0);
    lv_list_add_text(s_wifi_list, tr(T_SCANNING));
    run_async(scan_job);
    s_wifi_base = s_depth;   /* Settings */
    push(s);
}

/* ---------- Settings ---------- */

static lv_obj_t *s_status_label;

static void status_text(char *b, size_t n)
{
    int mv = 0, pct = -1;
    bool usb = false, charging = false;
    hal_read_battery(&mv, &pct);
    hal_read_charge(&usb, &charging);
    const char *wifi = wifi_state() == WIFI_UP ? tr(T_CONNECTED) : wifi_state() == WIFI_CONNECTING ? tr(T_CONNECTING) : tr(T_NOT_SET);
    bool mock = PP_BUS_USES_MOCK || PP_NAV_USES_MOCK;   /* any feature still on fixtures */
    snprintf(b, n, "%s %d%% (%d mV) %s\n%s %s %s\n%s %s", tr(T_BATTERY), pct, mv, charging ? tr(T_CHARGING) : "", tr(T_WIFI), wifi, wifi_problem(), tr(T_API), mock ? tr(T_MOCK) : tr(T_LIVE));
}

static void on_brightness(lv_event_t *e)
{
    s_set->brightness = (uint8_t)lv_slider_get_value(lv_event_get_target(e));
    hal_set_brightness(s_set->brightness);
    if (lv_event_get_code(e) == LV_EVENT_RELEASED) storage_settings_save(s_set);
}

static lv_obj_t *build_settings(void);

static void on_lang(lv_event_t *e)
{
    s_set->lang = !s_set->lang;
    tr_set_lang(s_set->lang);
    storage_settings_save(s_set);
    lv_obj_t *n = build_settings();   /* rebuild in place so the new language shows at once */
    s_stack[s_depth - 1] = n;
    lv_screen_load_anim(n, LV_SCREEN_LOAD_ANIM_FADE_IN, 150, 0, true);
}

static lv_obj_t *build_settings(void)
{
    lv_obj_t *s = new_screen(tr(T_SETTINGS));
    lv_obj_t *bt = lv_label_create(s);
    lv_label_set_text(bt, tr(T_BRIGHTNESS));
    lv_obj_set_style_text_font(bt, F14, 0);
    lv_obj_set_style_text_color(bt, C_DIM, 0);
    lv_obj_align(bt, LV_ALIGN_TOP_MID, 0, SC(80));
    lv_obj_t *sl = lv_slider_create(s);
    lv_slider_set_range(sl, 5, 100);
    lv_slider_set_value(sl, s_set->brightness, LV_ANIM_OFF);
    lv_obj_set_width(sl, SC(200));
    lv_obj_align(sl, LV_ALIGN_TOP_MID, 0, SC(110));
    lv_obj_add_event_cb(sl, on_brightness, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(sl, on_brightness, LV_EVENT_RELEASED, NULL);
    lv_obj_t *b = lv_button_create(s);
    lv_obj_set_size(b, SC(200), SC(40));
    lv_obj_t *bl = lv_label_create(b);
    lv_label_set_text_fmt(bl, "%s: %s", tr(T_LANGUAGE), tr(T_LANG_NAME));
    lv_obj_center(bl);
    lv_obj_align(b, LV_ALIGN_TOP_MID, 0, SC(150));
    lv_obj_add_event_cb(b, on_lang, LV_EVENT_CLICKED, NULL);
    lv_obj_t *wb = lv_button_create(s);
    lv_obj_set_size(wb, SC(200), SC(40));
    lv_obj_t *wl = lv_label_create(wb);
    lv_label_set_text(wl, tr(T_WIFI));
    lv_obj_center(wl);
    lv_obj_align(wb, LV_ALIGN_TOP_MID, 0, SC(200));
    lv_obj_add_event_cb(wb, open_wifi, LV_EVENT_CLICKED, NULL);
    s_status_label = lv_label_create(s);
    lv_obj_set_style_text_font(s_status_label, F14, 0);
    lv_obj_align(s_status_label, LV_ALIGN_BOTTOM_MID, 0, -SC(50));
    char t[160];
    status_text(t, sizeof t);
    lv_label_set_text(s_status_label, t);
    return s;
}

static void open_settings(lv_event_t *e) { push(build_settings()); }

/* ---------- menu ---------- */

static lv_obj_t *s_menu_lbl[3];
static const tr_id_t MENU_IDS[3] = { T_BUS, T_NAV, T_SETTINGS };

static void add_menu_btn(lv_obj_t *s, int idx, int y, lv_event_cb_t cb)
{
    lv_obj_t *b = lv_button_create(s);
    lv_obj_set_size(b, SC(200), SC(52));
    lv_obj_align(b, LV_ALIGN_TOP_MID, 0, SC(y));
    s_menu_lbl[idx] = lv_label_create(b);
    lv_label_set_text(s_menu_lbl[idx], tr(MENU_IDS[idx]));
    lv_obj_center(s_menu_lbl[idx]);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
}

static void on_menu_show(lv_event_t *e)
{
    for (int i = 0; i < 3; i++) lv_label_set_text(s_menu_lbl[i], tr(MENU_IDS[i]));
}

static void open_menu(lv_event_t *e)
{
    lv_obj_t *s = new_screen(NULL);
    add_menu_btn(s, 0, 90, open_bus);
    add_menu_btn(s, 1, 158, open_nav);
    add_menu_btn(s, 2, 226, open_settings);
    lv_obj_add_event_cb(s, on_menu_show, LV_EVENT_SCREEN_LOAD_START, NULL);
    push(s);
}

/* ---------- Home ---------- */

static lv_obj_t *s_time, *s_batt, *s_wifi, *s_pupil[2];

static lv_obj_t *circle(lv_obj_t *p, int d, lv_color_t c, int x, int y)
{
    lv_obj_t *o = lv_obj_create(p);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);   /* decoration: taps go to the screen */
    lv_obj_set_size(o, SC(d), SC(d));
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(o, c, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_align(o, LV_ALIGN_CENTER, SC(x), SC(y));
    return o;
}

static void anim_h(void *o, int32_t v) { lv_obj_set_height(o, v); }

static void blink_tick(lv_timer_t *t)
{
    static int n;
    n++;
    for (int i = 0; i < 2; i++) {
        /* eye patch is the pupil's parent: squash it briefly */
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, lv_obj_get_parent(s_pupil[i]));
        lv_anim_set_exec_cb(&a, anim_h);
        lv_anim_set_values(&a, SC(56), SC(6));
        lv_anim_set_duration(&a, 90);
        lv_anim_set_reverse_duration(&a, 90);
        lv_anim_start(&a);
        /* look around: 4 positions */
        lv_obj_align(s_pupil[i], LV_ALIGN_CENTER, SC(((n + i) % 3 - 1) * 4), 0);
    }
    lv_timer_set_period(t, 2500 + (n * 997) % 2500);   /* irregular, no RNG needed */
}

static void home_tick(lv_timer_t *t)
{
    char b[24];
    int mv, pct;
    time_t now = time(NULL);
    if (wifi_time_valid()) {
        struct tm tm;
        localtime_r(&now, &tm);   /* TZ=SGT-8 set by wifi.c once SNTP is up */
        snprintf(b, sizeof b, "%02d:%02d", tm.tm_hour, tm.tm_min);
    } else {
        snprintf(b, sizeof b, "--:--");
    }
    lv_label_set_text(s_time, b);
    if (hal_read_battery(&mv, &pct) == ESP_OK) {
        bool usb = false, charging = false;
        hal_read_charge(&usb, &charging);
        if (pct < 0) snprintf(b, sizeof b, "USB");   /* no cell fitted */
        else snprintf(b, sizeof b, "%s%d%%", charging ? LV_SYMBOL_CHARGE " " : "", pct);
        lv_label_set_text(s_batt, b);
    }
    lv_label_set_text(s_wifi, wifi_state() == WIFI_UP ? LV_SYMBOL_WIFI : wifi_state() == WIFI_CONNECTING ? LV_SYMBOL_REFRESH : "-");
}

#define DIM_PCT 15   /* before the screen goes off: a cheap warning, and a saving on AMOLED */
static bool s_dim;

static int dim_level(void) { return s_set->brightness < DIM_PCT ? s_set->brightness : DIM_PCT; }

/* Idle: dim at half the timeout, then off. Any activity restores. With PP_LOW_POWER this timer stops while the
 * screen is off (LVGL is paused) and the board wakes on a key; the first tick after that sees fresh activity. */
static void sleep_tick(lv_timer_t *t)
{
    uint32_t idle = lv_display_get_inactive_time(NULL);
    uint32_t off_ms = CONFIG_PP_SCREEN_OFF_S * 1000u;
    if (idle < 500) {
        if (s_asleep) { hal_screen_on(); s_asleep = false; s_dim = false; hal_set_brightness(s_set->brightness); }
        else if (s_dim) { s_dim = false; hal_set_brightness(s_set->brightness); }
    } else if (!s_asleep && idle > off_ms) {
        s_asleep = true;
        hal_screen_off();
    } else if (!s_asleep && !s_dim && idle > off_ms / 2) {
        s_dim = true;
        hal_set_brightness(dim_level());
    }
}

static lv_obj_t *build_home(void)
{
    lv_obj_t *s = new_screen(NULL);
    lv_obj_add_flag(s, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s, open_menu, LV_EVENT_CLICKED, NULL);

    circle(s, 90, lv_color_hex(0x383838), -88, -112);   /* ears */
    circle(s, 90, lv_color_hex(0x383838), 88, -112);
    lv_obj_t *face = circle(s, 230, lv_color_hex(0xF8F8F8), 0, -42);
    for (int i = 0; i < 2; i++) {
        lv_obj_t *patch = circle(face, 56, lv_color_hex(0x1A1A1A), i ? 50 : -50, -8);
        lv_obj_set_height(patch, SC(56));
        lv_obj_set_style_radius(patch, SC(28), 0);
        s_pupil[i] = circle(patch, 22, C_FG, 0, 0);
    }
    circle(face, 26, lv_color_hex(0x1A1A1A), 0, 42);   /* nose */

    s_time = lv_label_create(s);
    lv_obj_set_style_text_font(s_time, &lv_font_montserrat_28, 0);
    lv_obj_align(s_time, LV_ALIGN_BOTTOM_MID, 0, -SC(50));
    s_batt = lv_label_create(s);
    lv_obj_set_style_text_color(s_batt, C_DIM, 0);
    lv_obj_align(s_batt, LV_ALIGN_BOTTOM_MID, SC(40), -SC(22));
    s_wifi = lv_label_create(s);
    lv_obj_set_style_text_color(s_wifi, C_DIM, 0);
    lv_obj_align(s_wifi, LV_ALIGN_BOTTOM_MID, -SC(40), -SC(22));

    lv_timer_create(blink_tick, 3000, NULL);
    lv_timer_create(home_tick, 1000, NULL);
    lv_timer_create(sleep_tick, 500, NULL);
    home_tick(NULL);
    return s;
}

void ui_start(pp_settings_t *s)
{
    fonts_init();
    s_set = s;
    storage_favs_load(&s_favs);
    tr_set_lang(s->lang);
    if (!hal_lock(-1)) return;
    s_stack[s_depth++] = build_home();
    lv_screen_load(s_stack[0]);
    hal_unlock();
    hal_set_brightness(s->brightness);
}

#ifdef PP_SIM
/* Scroll the route step list by dy pixels (what a finger drag does) and report how far it can go. */
void ui_sim_scroll_steps(int dy)
{
    if (!s_nav_steps || !lv_obj_is_valid(s_nav_steps)) return;
    lv_obj_scroll_by(s_nav_steps, 0, -dy, LV_ANIM_OFF);
    printf("[sim] steps scrolled: top=%d, still below=%d px\n", (int)lv_obj_get_scroll_top(s_nav_steps), (int)lv_obj_get_scroll_bottom(s_nav_steps));
}

/* "menu" | "bus" | "numpad[:digits]" | "arrivals[:stop]" | "nav" | "route" | "places" | "settings" */
void ui_sim_open(const char *name)
{
    if (!strcmp(name, "menu")) open_menu(NULL);
    else if (!strcmp(name, "bus")) open_bus(NULL);
    else if (!strncmp(name, "numpad", 6)) {
        open_numpad(NULL);
        for (const char *d = name[6] == ':' ? name + 7 : ""; *d && s_nd < 5; d++) s_digits[s_nd++] = *d;
        s_digits[s_nd] = 0;
        digits_refresh();
    } else if (!strncmp(name, "arrivals", 8)) show_arrivals(name[8] == ':' ? name + 9 : "83139", false);
    else if (!strcmp(name, "nav")) open_nav(NULL);
    else if (!strcmp(name, "route")) open_route(NULL);
    else if (!strcmp(name, "places")) open_places(false);
    else if (!strcmp(name, "settings")) open_settings(NULL);
    else if (!strcmp(name, "wifi")) open_wifi(NULL);
    else if (!strncmp(name, "password", 8)) { snprintf(s_pick_ssid, sizeof s_pick_ssid, "%s", name[8] == ':' ? name + 9 : "HomeNet"); open_password(); }
}
#endif
