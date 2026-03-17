/*
 * led_matrix.h  --  LED Color Flash Block: WS2812 Strip Driver & Effect Engine
 *
 * Hardware : 30x WS2812B LEDs on GPIO 18 via ESP-IDF RMT driver.
 * Brightness: Software-scaled (0-255) applied to every set_pixel/fill call.
 *
 * The effect engine exposes 10 pattern IDs (0-9), each mapped to a
 * WS2812FX-inspired animation.  Two entry points drive them:
 *
 *   led_flash_play_preview(id)  -- Quick color pulse so the kid sees
 *                                  which color this pattern uses.
 *   led_flash_play_execute(id)  -- Runs the full animation (~0.5-2 s).
 *
 * Pattern ID table:
 *   0 = Lights Off       5 = Rainbow Cycle
 *   1 = Color Wipe       6 = Sparkle
 *   2 = Theater Chase    7 = Running Lights
 *   3 = Larson Scanner   8 = Fire Flicker
 *   4 = Breathe          9 = Comet
 *
 * Both the local TFT UI (via command_handler) and the brain block
 * (over I2C CMD_SET_LED + CMD_EXECUTE) use these same IDs.
 */

 #pragma once

 #include <stdint.h>
 #include "esp_err.h"
 
 #ifdef __cplusplus
 extern "C" {
 #endif
 
/* Initialise the RMT-backed WS2812 strip.  Must be called once before
 * any other matrix_* or led_flash_* function. */
esp_err_t led_matrix_init(void);

/* Short boot-time animation shown once after matrix init succeeds. */


/* ── Low-level primitives (also used by command_handler for raw I2C
 *    commands like CMD_MATRIX_FILL / CMD_MATRIX_CLEAR). ────────────── */
 
 void     matrix_fill(uint8_t r, uint8_t g, uint8_t b);
 void     matrix_set_pixel(uint16_t idx, uint8_t r, uint8_t g, uint8_t b);
 void     matrix_clear(void);
 void     matrix_show(void);
 void     matrix_set_brightness(uint8_t brightness);
 uint8_t  matrix_get_brightness(void);
 uint16_t matrix_get_size(void);
 
 /* Return the human-readable name for a pattern ID (e.g. "Color Wipe").
  * Used by the TFT UI status label and ESP_LOG messages. */
 const char *led_pattern_name(uint8_t pattern_id);
 
 /* Quick colour pulse (PREVIEW_PULSE_MS) so the user can preview the
  * pattern's colour before committing. */
 void led_flash_play_preview(uint8_t color_id);
 
 /* Run the full WS2812FX-style animation for the given pattern ID.
  * Blocking -- returns after the effect finishes (~0.5-2 s). */
 void led_flash_play_execute(uint8_t color_id);
 
 #ifdef __cplusplus
 }
 #endif
 
