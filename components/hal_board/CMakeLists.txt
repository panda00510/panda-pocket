# Managed deps (LVGL, esp_lvgl_adapter, esp_lcd_co5300) come from idf_component.yml.
idf_component_register(
    SRCS "hal_board_stopwatch.c" "pp_lcd_bands.c" "pp_lv_mem.c"
    INCLUDE_DIRS "."
    WHOLE_ARCHIVE   # pp_lv_mem.c provides symbols only LVGL references
    REQUIRES esp_driver_gpio esp_driver_i2c lvgl
    PRIV_REQUIRES esp_timer esp_lcd esp_driver_spi)
