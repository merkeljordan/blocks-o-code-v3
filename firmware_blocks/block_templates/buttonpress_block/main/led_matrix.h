#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Initialize the LED matrix (call once at startup)
esp_err_t led_matrix_init(void);

// Display a startup animation on the LED matrix
void led_matrix_startup_animation(void);

// Set a single pixel color (index 0-15)
void matrix_set_pixel(uint16_t index, uint8_t r, uint8_t g, uint8_t b);

// Fill all LEDs with the given RGB color (brightness-scaled)
void matrix_fill(uint8_t r, uint8_t g, uint8_t b);

// Clear all LEDs (turn off)
void matrix_clear(void);

// Refresh/push the current pixel buffer to the LED strip
void matrix_show(void);

// Set the global brightness (0-255)
void matrix_set_brightness(uint8_t brightness);

// Get the current global brightness
uint8_t matrix_get_brightness(void);

// Status mirroring and locking
void led_matrix_set_status_mirror(bool enabled);
void led_matrix_set_lock(bool locked);

#ifdef __cplusplus
}
#endif
