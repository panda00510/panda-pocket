#include "esp_log.h"
#include "esp_pm.h"
#include "sdkconfig.h"
#include "hal_board.h"
#include "storage.h"
#include "ui.h"
#include "wifi.h"

static const char *TAG = "panda";
static pp_settings_t s_settings;

void app_main(void)
{
#ifdef CONFIG_PP_LOW_POWER
    /* DFS between the crystal clock and 240 MHz, and automatic light sleep when every task is blocked. */
    const esp_pm_config_t pm = { .max_freq_mhz = 240, .min_freq_mhz = 40, .light_sleep_enable = true };
    ESP_ERROR_CHECK(esp_pm_configure(&pm));
#endif
    ESP_ERROR_CHECK(storage_init());
    storage_settings_load(&s_settings);
    ESP_ERROR_CHECK(board_init());
    /* Display must start from this task: the panel SPI interrupt lands on its core (see pp_lcd_bands.h). */
    if (!hal_display_start()) {
        ESP_LOGE(TAG, "display failed to start");
        return;
    }
    ui_start(&s_settings);
    wifi_start(s_settings.ssid, s_settings.pass);   /* no-op without an SSID: Mock Mode needs no network */
}
