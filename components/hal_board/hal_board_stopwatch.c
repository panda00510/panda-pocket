/*
 * M5Stack StopWatch (C152): ESP32-S3R8, 1.75" round 466x466 CO5300 AMOLED on
 * QSPI, CST820 touch, M5PM1 power chip (0x6E) and M5IOE1 expander (0x4F) on
 * one I2C bus (GPIO47/48).
 *
 * Derived from Muse Gadget SDK board_m5stack_stopwatch.c (Apache-2.0,
 * Copyright (c) Meta Platforms, Inc. and affiliates), trimmed to display,
 * touch, brightness and battery. Pins, registers and init sequence were
 * cross-checked against M5's factory firmware M5StopWatch-UserDemo (MIT,
 * Copyright (c) 2026 M5Stack Technology CO LTD). Audio, buttons, IMU, motor,
 * power-key interrupt and rail power-down are left out (see PROGRESS.md).
 * NOT yet run on hardware.
 */
#include <string.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_co5300.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_lv_adapter.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "hal_board.h"
#include "pp_lcd_bands.h"
#include "pp_mem.h"

static const char *TAG = "hal";

#define LCD_GAP_X 6   /* the 466 columns start at 6 in the controller's 480 */
#define LCD_HOST SPI2_HOST
#define LCD_CS GPIO_NUM_39
#define LCD_SCLK GPIO_NUM_40
#define LCD_D0 GPIO_NUM_41
#define LCD_D1 GPIO_NUM_42
#define LCD_D2 GPIO_NUM_46
#define LCD_D3 GPIO_NUM_45
#define DRAW_BUF_LINES 118   /* four bands to the screen */
#define LCD_CHUNK_BYTES (HAL_LCD_RES * 8 * 2)
#define LCD_LEVEL_BOOT 0xA0

#define I2C_SDA GPIO_NUM_47
#define I2C_SCL GPIO_NUM_48

#define TP_ADDR 0x15
#define TP_REG_POINTS 0x02         /* finger count, then X and Y, 12 bits each */
#define TP_REG_NO_AUTO_SLEEP 0xFE  /* else it stops answering I2C when idle */

#define PMIC_ADDR 0x6E
#define PMIC_PWR_CFG 0x06
#define PMIC_PWR_5V_OUT 0x08       /* 5 V boost to the Grove port */
#define PMIC_I2C_CFG 0x09          /* 0: no I2C idle sleep */
#define PMIC_WDT 0x0A              /* 0: watchdog off */
#define PMIC_VBAT_MV 0x22          /* 16-bit little-endian millivolts */
#define PMIC_BTN_CFG1 0x49
#define PMIC_SINGLE_RESET_DISABLE BIT(0)

#define IOE_ADDR 0x4F
#define IOE_MODE 0x03              /* 16 bits, IO1 in bit 0; 1 = output */
#define IOE_OUT 0x05
#define IOE_OD 0x13                /* 1 = open drain */
#define IOE_PWM1_DUTY 0x1B
#define IOE_I2C_CFG 0x23
#define IOE_PIN(n) ((uint16_t)BIT((n) - 1))
#define IOE_AUDIO IOE_PIN(3)
#define IOE_TP_RST IOE_PIN(4)
#define IOE_LCD_RST IOE_PIN(5)
#define IOE_L3B IOE_PIN(8)         /* the OLED's supply rail */
#define IOE_MOTOR IOE_PIN(9)
#define IOE_SPK IOE_PIN(10)
#define IOE_OUTPUTS (IOE_AUDIO | IOE_TP_RST | IOE_LCD_RST | IOE_L3B | IOE_MOTOR | IOE_SPK)

static i2c_master_bus_handle_t s_i2c;
static i2c_master_dev_handle_t s_pmic, s_ioe, s_tp;
static esp_lcd_panel_io_handle_t s_io;
static esp_lcd_panel_handle_t s_panel;
static uint8_t s_level = LCD_LEVEL_BOOT;   /* last CO5300 brightness (0x51) */

/* The power chip and expander are microcontrollers and now and then miss a transfer; retry. */
static esp_err_t reg_read(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t *buf, size_t n)
{
    esp_err_t err = ESP_FAIL;
    for (int i = 0; i < 3 && err != ESP_OK; i++) {
        err = i2c_master_transmit_receive(dev, &reg, 1, buf, n, 50);
    }
    return err;
}

static esp_err_t reg_write(i2c_master_dev_handle_t dev, uint8_t reg, const uint8_t *val, size_t n)
{
    uint8_t buf[3] = { reg };
    assert(n < sizeof(buf));
    memcpy(buf + 1, val, n);
    esp_err_t err = ESP_FAIL;
    for (int i = 0; i < 3 && err != ESP_OK; i++) {
        err = i2c_master_transmit(dev, buf, n + 1, 50);
    }
    return err;
}

static esp_err_t pmic_update(uint8_t reg, uint8_t mask, uint8_t bits)
{
    uint8_t v;
    ESP_RETURN_ON_ERROR(reg_read(s_pmic, reg, &v, 1), TAG, "pmic read %02x", reg);
    uint8_t want = (v & ~mask) | (bits & mask);
    return want == v ? ESP_OK : reg_write(s_pmic, reg, &want, 1);
}

/* Expander registers come in pairs (IO1-8, IO9-14). Only init and tp_reset touch them. */
static esp_err_t ioe_update(uint8_t reg, uint16_t mask, uint16_t bits)
{
    uint8_t b[2];
    ESP_RETURN_ON_ERROR(reg_read(s_ioe, reg, b, sizeof b), TAG, "ioe read");
    uint16_t v = b[0] | b[1] << 8, want = (v & ~mask) | (bits & mask);
    if (want == v) return ESP_OK;
    b[0] = want;
    b[1] = want >> 8;
    return reg_write(s_ioe, reg, b, sizeof b);
}

static esp_err_t ioe_set(uint16_t pins, bool high) { return ioe_update(IOE_OUT, pins, high ? pins : 0); }

static esp_err_t add_device(uint8_t addr, uint32_t hz, i2c_master_dev_handle_t *dev)
{
    const i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = addr, .scl_speed_hz = hz,
    };
    return i2c_master_bus_add_device(s_i2c, &cfg, dev);
}

esp_err_t board_init(void)
{
    const i2c_master_bus_config_t i2c_cfg = {
        .i2c_port = I2C_NUM_0, .sda_io_num = I2C_SDA, .scl_io_num = I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7, .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&i2c_cfg, &s_i2c), TAG, "i2c");
    ESP_RETURN_ON_ERROR(add_device(PMIC_ADDR, 100000, &s_pmic), TAG, "pmic");
    ESP_RETURN_ON_ERROR(add_device(IOE_ADDR, 100000, &s_ioe), TAG, "ioe");
    ESP_RETURN_ON_ERROR(add_device(TP_ADDR, 400000, &s_tp), TAG, "touch");

    /* The power chip keeps its settings across an ESP32 reset: clear I2C sleep and watchdog,
     * 5 V boost off, and disable the power key's single-click reset (as M5's factory firmware does). */
    ESP_RETURN_ON_ERROR(reg_write(s_pmic, PMIC_I2C_CFG, (uint8_t[]){ 0 }, 1), TAG, "pmic i2c sleep off");
    ESP_RETURN_ON_ERROR(reg_write(s_pmic, PMIC_WDT, (uint8_t[]){ 0 }, 1), TAG, "pmic watchdog off");
    ESP_RETURN_ON_ERROR(pmic_update(PMIC_PWR_CFG, PMIC_PWR_5V_OUT, 0), TAG, "5 V out off");
    ESP_RETURN_ON_ERROR(pmic_update(PMIC_BTN_CFG1, PMIC_SINGLE_RESET_DISABLE, PMIC_SINGLE_RESET_DISABLE), TAG, "power key");

    /* Motor and amp off; audio, OLED rail and both resets high; then reset panel + touch together. */
    ESP_RETURN_ON_ERROR(reg_write(s_ioe, IOE_I2C_CFG, (uint8_t[]){ 0 }, 1), TAG, "ioe i2c sleep off");
    ESP_RETURN_ON_ERROR(reg_write(s_ioe, IOE_PWM1_DUTY, (uint8_t[]){ 0, 0 }, 2), TAG, "motor pwm off");
    ESP_RETURN_ON_ERROR(ioe_update(IOE_OD, IOE_OUTPUTS, 0), TAG, "ioe push-pull");
    ESP_RETURN_ON_ERROR(ioe_update(IOE_OUT, IOE_OUTPUTS, IOE_AUDIO | IOE_TP_RST | IOE_LCD_RST | IOE_L3B), TAG, "ioe levels");
    ESP_RETURN_ON_ERROR(ioe_update(IOE_MODE, IOE_OUTPUTS, IOE_OUTPUTS), TAG, "ioe outputs");
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_RETURN_ON_ERROR(ioe_set(IOE_TP_RST | IOE_LCD_RST, false), TAG, "reset low");
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_RETURN_ON_ERROR(ioe_set(IOE_TP_RST | IOE_LCD_RST, true), TAG, "reset high");
    vTaskDelay(pdMS_TO_TICKS(120));   /* the panel's wake from reset */
    return ESP_OK;
}

/* M5GFX's Panel_StopWatch init, less the tearing line; brightness and display-on follow in panel_setup(). */
static const co5300_lcd_init_cmd_t s_lcd_init[] = {
    { 0x11, NULL, 0, 150 },                 /* sleep out */
    { 0xC4, (uint8_t[]){ 0x80 }, 1, 0 },
    { 0x53, (uint8_t[]){ 0x20 }, 1, 0 },    /* brightness control on */
};

/* A CO5300 command over QSPI. */
static void lcd_cmd(uint8_t cmd, const void *param, size_t n)
{
    esp_lcd_panel_io_tx_param(s_io, (0x02 << 24) | (cmd << 8), param, n);
}

/* Reset + init, then black over the panel's random memory before the display goes on. */
static void panel_setup(void)
{
    esp_lcd_panel_reset(s_panel);
    esp_lcd_panel_init(s_panel);
    uint8_t *black = heap_caps_calloc(1, LCD_CHUNK_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (black) {
        for (int y = 0; y < HAL_LCD_RES; y += 8) {
            esp_lcd_panel_draw_bitmap(s_panel, 0, y, HAL_LCD_RES, y + 8 < HAL_LCD_RES ? y + 8 : HAL_LCD_RES, black);
        }
    }
    lcd_cmd(0x51, &s_level, 1);
    esp_lcd_panel_disp_on_off(s_panel, true);
    free(black);
}

/* The CO5300 needs even-aligned update windows. */
static void round_area(lv_event_t *e)
{
    lv_area_t *a = lv_event_get_param(e);
    a->x1 &= ~1;
    a->y1 &= ~1;
    a->x2 |= 1;
    a->y2 |= 1;
}

/* LVGL polls this from its own task. The CST820 reports one point. */
static void tp_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    uint8_t b[5];
    if (reg_read(s_tp, TP_REG_POINTS, b, sizeof b) != ESP_OK || (b[0] & 0x0F) == 0) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }
    int x = (b[1] & 0x0F) << 8 | b[2], y = (b[3] & 0x0F) << 8 | b[4];
    data->point.x = x < HAL_LCD_RES ? x : HAL_LCD_RES - 1;   /* M5's firmware passes coordinates unscaled */
    data->point.y = y < HAL_LCD_RES ? y : HAL_LCD_RES - 1;
    data->state = LV_INDEV_STATE_PRESSED;
}

lv_display_t *hal_display_start(void)
{
    const spi_bus_config_t bus = CO5300_PANEL_BUS_QSPI_CONFIG(LCD_SCLK, LCD_D0, LCD_D1, LCD_D2, LCD_D3, LCD_CHUNK_BYTES);
    if (spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) return NULL;
    const esp_lcd_panel_io_spi_config_t io_cfg = CO5300_PANEL_IO_QSPI_CONFIG(LCD_CS, NULL, NULL);
    if (esp_lcd_new_panel_io_spi(LCD_HOST, &io_cfg, &s_io) != ESP_OK) return NULL;
    co5300_vendor_config_t vendor_cfg = {
        .init_cmds = s_lcd_init,
        .init_cmds_size = sizeof(s_lcd_init) / sizeof(s_lcd_init[0]),
        .flags.use_qspi_interface = 1,
    };
    const esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = GPIO_NUM_NC,   /* the expander's IO5, pulsed in board_init() */
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config = &vendor_cfg,
    };
    if (esp_lcd_new_panel_co5300(s_io, &panel_cfg, &s_panel) != ESP_OK) return NULL;
    esp_lcd_panel_set_gap(s_panel, LCD_GAP_X, 0);
    panel_setup();

    esp_lv_adapter_config_t adapter_cfg = ESP_LV_ADAPTER_DEFAULT_CONFIG();
    adapter_cfg.task_core_id = PP_UI_CORE;
    adapter_cfg.task_priority = PP_UI_PRIORITY;
    if (esp_lv_adapter_init(&adapter_cfg) != ESP_OK) return NULL;
    const esp_lv_adapter_display_config_t disp_cfg = {
        .panel = s_panel,
        .panel_io = s_io,
        .profile = {
            .interface = ESP_LV_ADAPTER_PANEL_IF_OTHER,
            .rotation = ESP_LV_ADAPTER_ROTATE_0,
            .hor_res = HAL_LCD_RES,
            .ver_res = HAL_LCD_RES,
        },
        .tear_avoid_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_NONE,
    };
    lv_display_t *disp = pp_lcd_bands_register(disp_cfg, DRAW_BUF_LINES, LCD_CHUNK_BYTES);
    if (!disp) return NULL;
    lv_display_add_event_cb(disp, round_area, LV_EVENT_INVALIDATE_AREA, NULL);

    /* Touch reset pulse, then keep the CST820 answering while idle. */
    ioe_set(IOE_TP_RST, false);
    vTaskDelay(pdMS_TO_TICKS(10));
    ioe_set(IOE_TP_RST, true);
    vTaskDelay(pdMS_TO_TICKS(50));
    if (reg_write(s_tp, TP_REG_NO_AUTO_SLEEP, (uint8_t[]){ 0x01 }, 1) != ESP_OK) ESP_LOGW(TAG, "touch not answering");
    lv_indev_t *touch = lv_indev_create();
    if (!touch) return NULL;
    lv_indev_set_type(touch, LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(touch, disp);
    lv_indev_set_read_cb(touch, tp_read);
    if (esp_lv_adapter_start() != ESP_OK) return NULL;
    return disp;
}

bool hal_lock(int timeout_ms) { return esp_lv_adapter_lock(timeout_ms) == ESP_OK; }
void hal_unlock(void) { esp_lv_adapter_unlock(); }

/* Panel commands go out on the band-send task so they never interleave with a band. */
static void send_brightness(void *level)
{
    s_level = *(uint8_t *)level;
    lcd_cmd(0x51, &s_level, 1);
}

void hal_set_brightness(int pct)
{
    uint8_t level = (uint8_t)(pct * 255 / 100);
    pp_lcd_bands_run(send_brightness, &level);
}

static void send_sleep(void *sleep) { lcd_cmd(*(bool *)sleep ? 0x10 : 0x11, NULL, 0); }

void hal_panel_sleep(bool sleep)
{
    pp_lcd_bands_run(send_sleep, &sleep);
    vTaskDelay(pdMS_TO_TICKS(120));
}

esp_err_t hal_read_battery(int *mv, int *pct)
{
    uint8_t b[2];
    ESP_RETURN_ON_ERROR(reg_read(s_pmic, PMIC_VBAT_MV, b, sizeof b), TAG, "pmic vbat");
    *mv = b[0] | b[1] << 8;
    /* LiPo curve from Muse (the Watcher's); M5's factory firmware uses linear 3.3-4.2 V. Tune on hardware. */
    int p = (-*mv * *mv + 9016 * *mv - 19189000) / 10000;
    *pct = *mv < 2500 ? -1 : p < 0 ? 0 : p > 100 ? 100 : p;
    return ESP_OK;
}
