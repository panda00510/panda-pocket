#include "wifi.h"

#include <string.h>
#include <time.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

static const char *TAG = "wifi";
static volatile wifi_state_t s_state = WIFI_NONE;
static volatile wifi_error_t s_err = WIFI_ERR_NONE;
static TimerHandle_t s_retry;
static int s_backoff_s = 1;
static bool s_inited, s_sntp, s_configured;
static volatile bool s_scanning;   /* a scan owns the radio: don't reconnect under it */
static char s_ssid[WIFI_SSID_MAX];

static void retry_cb(TimerHandle_t t) { if (!s_scanning) esp_wifi_connect(); }

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        if (s_configured) esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (!s_configured || s_scanning) return;
        switch (((wifi_event_sta_disconnected_t *)data)->reason) {   /* ponytail: mapping from IDF docs, unverified on hardware */
        case WIFI_REASON_AUTH_FAIL:
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_MIC_FAILURE:
            s_err = WIFI_ERR_AUTH;
            break;
        case WIFI_REASON_NO_AP_FOUND:
            s_err = WIFI_ERR_NO_AP;
            break;
        default:
            s_err = WIFI_ERR_OTHER;
        }
        s_state = WIFI_CONNECTING;
        xTimerChangePeriod(s_retry, pdMS_TO_TICKS(s_backoff_s * 1000), 0);   /* 1,2,4..30 s */
        s_backoff_s = s_backoff_s < 16 ? s_backoff_s * 2 : 30;
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        s_state = WIFI_UP;
        s_err = WIFI_ERR_NONE;
        s_backoff_s = 1;
        if (!s_sntp) {
            setenv("TZ", "SGT-8", 1);   /* Singapore, no DST */
            tzset();
            esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
            s_sntp = esp_netif_sntp_init(&cfg) == ESP_OK;
        }
    }
}

static void apply(const char *ssid, const char *pass)
{
    wifi_config_t c = { 0 };
    strlcpy((char *)c.sta.ssid, ssid, sizeof c.sta.ssid);
    strlcpy((char *)c.sta.password, pass, sizeof c.sta.password);
    esp_wifi_set_config(WIFI_IF_STA, &c);
    strlcpy(s_ssid, ssid, sizeof s_ssid);
    s_configured = ssid[0] != 0;
}

void wifi_start(const char *ssid, const char *pass)
{
    if (s_inited) return;
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();
    const wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&init);
    s_retry = xTimerCreate("wifi_retry", pdMS_TO_TICKS(1000), pdFALSE, NULL, retry_cb);
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL);
    esp_wifi_set_mode(WIFI_MODE_STA);
    apply(ssid ? ssid : "", pass ? pass : "");
    s_inited = true;
    s_state = s_configured ? WIFI_CONNECTING : WIFI_NONE;
    ESP_LOGI(TAG, "start, ssid '%s'", s_ssid);
    esp_wifi_start();
}

void wifi_reconfigure(const char *ssid, const char *pass)
{
    if (!s_inited) { wifi_start(ssid, pass); return; }
    apply(ssid, pass);
    s_backoff_s = 1;
    s_err = WIFI_ERR_NONE;
    s_state = s_configured ? WIFI_CONNECTING : WIFI_NONE;
    xTimerStop(s_retry, 0);
    if (!s_configured) { esp_wifi_disconnect(); return; }
    /* Connected: disconnect, the handler reconnects with the new config. Otherwise connect straight away. */
    if (esp_wifi_disconnect() != ESP_OK || s_state != WIFI_UP) esp_wifi_connect();
}

wifi_state_t wifi_state(void) { return s_state; }
wifi_error_t wifi_last_error(void) { return s_err; }
const char *wifi_ssid(void) { return s_ssid; }
bool wifi_time_valid(void) { return time(NULL) > 1735689600; }   /* after 2025-01-01 */

int wifi_scan(wifi_ap_t *out, int max)
{
    if (!s_inited) return -1;
    s_scanning = true;
    xTimerStop(s_retry, 0);
    if (s_state == WIFI_CONNECTING) esp_wifi_disconnect();   /* a scan can't run mid-connect */
    int n = -1;
    if (esp_wifi_scan_start(NULL, true) == ESP_OK) {
        enum { RAW = 24 };
        wifi_ap_record_t rec[RAW];
        uint16_t cnt = RAW;
        if (esp_wifi_scan_get_ap_records(&cnt, rec) == ESP_OK) {
            wifi_ap_t tmp[RAW];
            for (int i = 0; i < cnt; i++) {
                strlcpy(tmp[i].ssid, (const char *)rec[i].ssid, sizeof tmp[i].ssid);
                tmp[i].rssi = rec[i].rssi;
                tmp[i].secure = rec[i].authmode != WIFI_AUTH_OPEN;
            }
            n = ap_list_clean(tmp, cnt);
            if (n > max) n = max;
            memcpy(out, tmp, n * sizeof *out);
        }
    }
    s_scanning = false;
    if (s_configured && s_state != WIFI_UP) esp_wifi_connect();   /* resume what the scan interrupted */
    return n;
}
