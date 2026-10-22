#pragma once
#include <stdbool.h>
#include <stdint.h>

#define WIFI_SSID_MAX 33   /* 32 + NUL */

typedef struct {
    char ssid[WIFI_SSID_MAX];
    int8_t rssi;
    bool secure;
} wifi_ap_t;

/* Drops empty SSIDs, merges duplicates (keeps the strongest), sorts by signal, strongest first.
 * In place; returns the new count. Mesh/multi-band routers show up many times in a raw scan. */
int ap_list_clean(wifi_ap_t *a, int n);
