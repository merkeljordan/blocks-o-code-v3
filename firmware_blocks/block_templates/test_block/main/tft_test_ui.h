#pragma once

#include <stdint.h>
#include "esp_err.h"

typedef enum {
    TFT_TEST_LINE_MATRIX = 0,
    TFT_TEST_LINE_LEDSTRIP,
    TFT_TEST_LINE_SPEAKER,
    TFT_TEST_LINE_TOUCH,
    TFT_TEST_LINE_COUNT
} tft_test_line_t;

/**
 * Start LVGL TFT + touch UI task.
 *
 * - Uses ILI9341 @ 240x320 SPI
 * - Uses XPT2046 touch @ SPI (interrupt/IRQ line)
 */
esp_err_t tft_test_ui_start(void);

/**
 * Update one text line shown on the TFT.
 * Safe to call from non-LVGL tasks.
 */
void tft_test_ui_set_line(tft_test_line_t line, const char *text);

