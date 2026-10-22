#include "storage.h"

#include <string.h>

#include "nvs.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#define NS "pp"

esp_err_t storage_init(void)
{
    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        e = nvs_flash_init();
    }
    return e;
}

static esp_err_t get_blob(const char *key, void *p, size_t n)
{
    nvs_handle_t h;
    esp_err_t e = nvs_open(NS, NVS_READONLY, &h);
    if (e != ESP_OK) return e;
    size_t len = n;
    e = nvs_get_blob(h, key, p, &len);
    nvs_close(h);
    return e == ESP_OK && len != n ? ESP_ERR_INVALID_SIZE : e;
}

static esp_err_t set_blob(const char *key, const void *p, size_t n)
{
    nvs_handle_t h;
    esp_err_t e = nvs_open(NS, NVS_READWRITE, &h);
    if (e != ESP_OK) return e;
    e = nvs_set_blob(h, key, p, n);
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    return e;
}

void storage_settings_load(pp_settings_t *s)
{
    if (get_blob("settings", s, sizeof *s) == ESP_OK && s->brightness >= 5 && s->brightness <= 100) return;
    memset(s, 0, sizeof *s);
    s->brightness = 80;
    strlcpy(s->ssid, CONFIG_PP_WIFI_SSID, sizeof s->ssid);
    strlcpy(s->pass, CONFIG_PP_WIFI_PASSWORD, sizeof s->pass);
}

esp_err_t storage_settings_save(const pp_settings_t *s) { return set_blob("settings", s, sizeof *s); }

void storage_favs_load(favs_t *f)
{
    if (get_blob("favs", f, sizeof *f) != ESP_OK || f->n < 0 || f->n > FAVS_MAX) memset(f, 0, sizeof *f);
}

esp_err_t storage_favs_save(const favs_t *f) { return set_blob("favs", f, sizeof *f); }
