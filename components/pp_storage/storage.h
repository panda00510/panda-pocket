#pragma once
#include "esp_err.h"
#include "favs.h"

typedef struct {
    uint8_t brightness;   /* 5..100 */
    uint8_t lang;         /* 0 en, 1 zh */
    char ssid[33];
    char pass[65];
} pp_settings_t;

esp_err_t storage_init(void);
void storage_settings_load(pp_settings_t *s);   /* defaults if nothing saved */
esp_err_t storage_settings_save(const pp_settings_t *s);
void storage_favs_load(favs_t *f);
esp_err_t storage_favs_save(const favs_t *f);
