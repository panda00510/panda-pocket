#pragma once

typedef enum {
    T_BUS, T_NAV, T_SETTINGS, T_BRIGHTNESS, T_LANGUAGE, T_BATTERY, T_API, T_WIFI,
    T_PREVIEW, T_MOCK, T_LIVE, T_LOADING, T_ERR_NET, T_ERR_API, T_MIN, T_ARR, T_SAVE, T_SAVED, T_FULL, T_NEW_STOP, T_FROM, T_TO, T_GO, T_SCANNING, T_NO_NETWORKS, T_PASSWORD, T_SHOW, T_HIDE, T_NOT_SET, T_CONNECTING, T_CONNECTED, T_SEAT, T_STAND, T_CROWDED, T_LANG_NAME, T_OPEN, T_COUNT
} tr_id_t;

void tr_set_lang(int lang);   /* 0 en, 1 zh */
int tr_lang(void);
const char *tr(tr_id_t id);
