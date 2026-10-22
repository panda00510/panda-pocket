#pragma once
#include "esp_heap_caps.h"

#define PP_BIG_CAPS (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
/* LVGL + panel SPI run on core 1, leaving core 0 to Wi-Fi/network. */
#define PP_UI_CORE 1
#define PP_UI_PRIORITY 5
