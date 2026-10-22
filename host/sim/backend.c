/* Host implementations of the hardware/network seams ui.c talks to (mock data only). */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "bus.h"
#include "hal_board.h"
#include "nav.h"
#include "wifi.h"

static pthread_mutex_t s_lvgl = PTHREAD_MUTEX_INITIALIZER;
static int s_batt_pct = 72;

/* The LVGL loop (main.c) holds this around lv_timer_handler; worker threads take it via hal_lock. */
void sim_lvgl_lock(void) { pthread_mutex_lock(&s_lvgl); }
void sim_lvgl_unlock(void) { pthread_mutex_unlock(&s_lvgl); }

bool hal_lock(int timeout_ms)
{
    pthread_mutex_lock(&s_lvgl);   /* blocking: polling with trylock starved under load (macOS has no timedlock) */
    return true;
}
void hal_unlock(void) { pthread_mutex_unlock(&s_lvgl); }
void hal_set_brightness(int pct) { printf("[sim] brightness %d%%\n", pct); }
void hal_panel_sleep(bool sleep) { printf("[sim] panel %s\n", sleep ? "sleep" : "wake"); }
esp_err_t hal_read_battery(int *mv, int *pct) { *pct = s_batt_pct; *mv = 3300 + s_batt_pct * 9; return ESP_OK; }

wifi_state_t wifi_state(void) { return WIFI_UP; }
bool wifi_time_valid(void) { return true; }
void wifi_start(const char *s, const char *p) {}
static char s_sim_ssid[WIFI_SSID_MAX] = "";
void wifi_reconfigure(const char *s, const char *p) { snprintf(s_sim_ssid, sizeof s_sim_ssid, "%s", s); printf("[sim] wifi connect '%s' (password %zu chars)\n", s, strlen(p)); }
const char *wifi_ssid(void) { return s_sim_ssid; }
int wifi_scan(wifi_ap_t *out, int max)
{
    static const wifi_ap_t fake[] = { { "HomeNet", -48, true }, { "SG-Free-WiFi", -62, false }, { "Neighbour 2.4G", -77, true } };
    usleep(600000);
    int n = sizeof fake / sizeof *fake < (size_t)max ? (int)(sizeof fake / sizeof *fake) : max;
    memcpy(out, fake, n * sizeof *out);
    return n;
}

bus_err_t bus_fetch(const char *stop, bus_arrivals_t *out, bool *from_mock)
{
    static char js[2048];
    if (!bus_stop_valid(stop)) return BUS_ERR_STOP;
    usleep(300000);   /* feel like a network call */
    time_t now = time(NULL);
    bus_mock_json(js, sizeof js, stop, now);
    *from_mock = true;
    return bus_parse(js, now, out) ? BUS_ERR_PARSE : BUS_OK;
}

nav_err_t nav_route(int from, int to, nav_route_t *out, bool *from_mock)
{
    usleep(300000);
    *from_mock = true;
    return nav_mock_route(from, to, out) ? NAV_ERR_PARSE : NAV_OK;
}

#include "storage.h"
esp_err_t storage_settings_save(const pp_settings_t *s) { printf("[sim] settings saved: brightness %d lang %d\n", s->brightness, s->lang); return ESP_OK; }

/* In-memory favorites; PP_SIM_FAVS="83139,12345" seeds them for screenshots. */
static favs_t s_sim_favs;
void storage_favs_load(favs_t *f)
{
    memset(f, 0, sizeof *f);
    char *env = getenv("PP_SIM_FAVS");
    if (env) {
        char *copy = strdup(env);
        for (char *t = strtok(copy, ","); t; t = strtok(NULL, ",")) favs_add(f, t);
        free(copy);
    }
    s_sim_favs = *f;
}
esp_err_t storage_favs_save(const favs_t *f) { s_sim_favs = *f; printf("[sim] favorites saved: %d\n", f->n); return ESP_OK; }
