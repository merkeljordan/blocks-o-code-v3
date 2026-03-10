#pragma once

#include "esp_err.h"
#include <stdint.h>

// Initialize the LED matrix (call once at startup)
esp_err_t led_matrix_init(void);

// Display a startup animation on the LED matrix
void led_matrix_startup_animation(void);

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
