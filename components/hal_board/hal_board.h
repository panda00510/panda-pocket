#pragma once
#include <stdbool.h>
#include "esp_err.h"
#include "lvgl.h"

#define HAL_LCD_RES 466   /* round 466x466 AMOLED */

/* I2C, power chip (M5PM1), expander (M5IOE1) rails and resets. Runs first. */
esp_err_t board_init(void);
/* Panel + LVGL + touch. Call from the task that will own the UI core (app_main is fine). */
lv_display_t *hal_display_start(void);
bool hal_lock(int timeout_ms);   /* -1 waits forever */
void hal_unlock(void);
void hal_set_brightness(int pct);   /* 0..100 */
void hal_panel_sleep(bool sleep);
/* mv/pct: pct is -1 when no cell is fitted. */
esp_err_t hal_read_battery(int *mv, int *pct);
