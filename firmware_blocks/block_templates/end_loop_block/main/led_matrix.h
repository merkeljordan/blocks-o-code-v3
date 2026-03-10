#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t led_matrix_init(void);
void led_matrix_startup_animation(void);

void matrix_fill(uint8_t r, uint8_t g, uint8_t b);
void matrix_clear(void);
void matrix_show(void);
void matrix_set_brightness(uint8_t brightness);
uint8_t matrix_get_brightness(void);

#ifdef __cplusplus
}
#endif
