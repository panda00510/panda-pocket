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
/* Screen off. Returns at once; may finish asynchronously. With PP_LOW_POWER the UI is paused, the touch chip sleeps
 * and the chip light-sleeps until a side key or the power key wakes everything again (touch cannot wake it: the
 * CST820's interrupt line is not known). Without it only the panel sleeps and touch wakes via hal_screen_on(). */
void hal_screen_off(void);
/* Panel back on. Idempotent; no-op while a low-power wake is already doing it. Brightness is the caller's job. */
void hal_screen_on(void);
/* USB power present / battery charging. Leaves the charge current at the board default (M5 docs: 185 mA;
 * M5's factory firmware raises it to 425 mA, about 0.94C on the 450 mAh cell, which is not done here). */
esp_err_t hal_read_charge(bool *usb, bool *charging);
esp_err_t hal_read_battery(int *mv, int *pct);
