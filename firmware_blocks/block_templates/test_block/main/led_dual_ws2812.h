#pragma once

#include <stdint.h>
#include "esp_err.h"

/**
 * Dual WS2812 bring-up helper.
 *
 * Assumption (based on existing templates in this repo):
 * - "Matrix" LEDs: GPIO 18, 16 LEDs
 * - "LED strip" LEDs: GPIO 15, 16 LEDs
 *
 * If your new boards use different pins/counts, adjust the constants in
 * led_dual_ws2812.c (or in a future refactor, parameterize via sdkconfig).
 */
esp_err_t led_dual_ws2812_init(void);

void led_dual_ws2812_clear(void);

void led_dual_ws2812_flash_matrix_red(uint32_t duration_ms);
void led_dual_ws2812_flash_ledstrip_blue(uint32_t duration_ms);

