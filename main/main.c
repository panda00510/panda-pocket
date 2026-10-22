#include "esp_log.h"
#include "hal_board.h"
#include "storage.h"
#include "ui.h"
#include "wifi.h"

static const char *TAG = "panda";
static pp_settings_t s_settings;

void app_main(void)
{
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
