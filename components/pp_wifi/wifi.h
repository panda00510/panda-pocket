#pragma once
#include <stdbool.h>

#include "ap_list.h"

typedef enum { WIFI_NONE, WIFI_CONNECTING, WIFI_UP } wifi_state_t;

/* Starts the driver (always, so scanning works) and connects if ssid is non-empty. */
void wifi_start(const char *ssid, const char *pass);
/* New credentials: connects now, auto-reconnects from then on. Caller persists them. */
void wifi_reconfigure(const char *ssid, const char *pass);
wifi_state_t wifi_state(void);
const char *wifi_ssid(void);   /* configured network, "" if none */
bool wifi_time_valid(void);    /* SNTP has set the clock */

/* Blocking scan (a few seconds): fills out[] with up to max cleaned entries (ap_list_clean).
 * Returns the count, or -1 if the driver isn't ready. Call from a worker task, not the UI task. */
int wifi_scan(wifi_ap_t *out, int max);
