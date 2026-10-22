#include "ap_list.h"

#include <string.h>

int ap_list_clean(wifi_ap_t *a, int n)
{
    int m = 0;
    for (int i = 0; i < n; i++) {
        if (!a[i].ssid[0]) continue;
        int j;
        for (j = 0; j < m; j++) if (!strcmp(a[j].ssid, a[i].ssid)) break;
        if (j < m) {
            if (a[i].rssi > a[j].rssi) a[j] = a[i];
        } else {
            a[m++] = a[i];
        }
    }
    for (int i = 1; i < m; i++) {   /* insertion sort: m is tiny */
        wifi_ap_t t = a[i];
        int j = i - 1;
        for (; j >= 0 && a[j].rssi < t.rssi; j--) a[j + 1] = a[j];
        a[j + 1] = t;
    }
    return m;
}
