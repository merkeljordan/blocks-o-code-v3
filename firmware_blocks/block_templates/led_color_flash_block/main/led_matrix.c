/*
 * led_matrix.c  --  LED Color Flash Block: WS2812 Strip Driver & Effect Engine
 *
 * Drives a 30-LED WS2812B strip on GPIO 18 through the ESP-IDF RMT
 * peripheral (espressif/led_strip component).
 *
 * ARCHITECTURE
 * ~~~~~~~~~~~~
 *   PATTERN_COLORS[10]   Full-brightness RGB for each pattern ID.
 *   PATTERN_NAMES[10]    Human-readable name for logging / TFT UI.
 *   scale8()             Applies matrix_brightness (0-255) to any colour value.
 *   fx_*()               One static function per WS2812FX-inspired effect.
 *   led_flash_play_*()   Public API: preview (quick pulse) / execute (full anim).
 *
 * TIMING
 * ~~~~~~
 *   Every fx_*() is bounded to ~0.5-2 seconds so it stays responsive.
 *   All delays use vTaskDelay() -- the calling task blocks while the
 *   animation plays, which is fine because the GUI and I2C run on
 *   separate FreeRTOS tasks / cores.
 *
 * ADDING A NEW EFFECT
 * ~~~~~~~~~~~~~~~~~~~
 *   1. Add an rgb_t entry to PATTERN_COLORS[] and a name to PATTERN_NAMES[].
 *   2. Write a static void fx_new_effect(r, g, b) function.
 *   3. Add a case in led_flash_play_execute().
 *   4. Update the pattern table comment in led_matrix.h.
 *
 * Reference: WS2812FX library by Harm Aldick (MIT) --
 *   https://github.com/kitesurfer1404/WS2812FX
 */

 #include <stdio.h>
 #include <stdint.h>
 #include <string.h>
 
 #include "esp_log.h"
 #include "esp_random.h"
 #include "freertos/FreeRTOS.h"
 #include "freertos/task.h"
 #include "led_strip.h"
 
 #include "led_matrix.h"
 
 static const char *TAG = "LED_MATRIX";
 
 /* ── Hardware constants ────────────────────────────────────────────────── */
#define LED_GPIO             15       /* WS2812 data pin                  */
#define LED_MATRIX_SIZE      16       /* Number of LEDs on the matrix      */
 
 /* ── Module state ──────────────────────────────────────────────────────── */
 static led_strip_handle_t led_strip = NULL;
 static uint8_t matrix_brightness = 50;  /* 0-255; ~20 % at boot */
 
 /* ── Per-pattern colour palette ────────────────────────────────────────
  *
  *  Colours are stored at full brightness; the scale8() helper applies
  *  matrix_brightness before writing to the hardware.  This keeps the
  *  palette readable and allows runtime brightness changes.            */
 
 typedef struct {
     uint8_t r, g, b;
 } rgb_t;
 
 static const rgb_t PATTERN_COLORS[10] = {
     [0] = {  0,   0,   0},   /* off   */
     [1] = {255,   0,   0},   /* red   */
     [2] = {  0, 255,   0},   /* green */
     [3] = {  0,   0, 255},   /* blue  */
     [4] = {255, 140,   0},   /* orange */
     [5] = {160,   0, 255},   /* purple (rainbow uses wheel, not this) */
     [6] = {  0, 255, 255},   /* cyan   */
     [7] = {255, 255,   0},   /* yellow */
     [8] = { 80, 120, 255},   /* sky blue */
     [9] = {255, 255, 255},   /* white  */
 };
 
 static const char *PATTERN_NAMES[10] = {
     "Lights Off",
     "Color Wipe",
     "Theater Chase",
     "Larson Scanner",
     "Breathe",
     "Rainbow Cycle",
     "Sparkle",
     "Running Lights",
     "Fire Flicker",
     "Comet",
 };
 
 /* ══════════════════════════════════════════════════════════════════════════
  *  INTERNAL HELPERS
  * ══════════════════════════════════════════════════════════════════════════ */
 
 /* Scale an 8-bit colour channel by an 8-bit brightness (0-255). */
 static inline uint8_t scale8(uint8_t value, uint8_t brightness) {
     return (uint8_t)(((uint16_t)value * brightness) / 255U);
 }
 
 /* Write one pixel with software brightness applied. */
 static void set_pixel_scaled(uint16_t idx, uint8_t r, uint8_t g, uint8_t b) {
     if (!led_strip || idx >= LED_MATRIX_SIZE) return;
     led_strip_set_pixel(led_strip, idx,
                         scale8(r, matrix_brightness),
                         scale8(g, matrix_brightness),
                         scale8(b, matrix_brightness));
 }
 
 /* Fill every pixel with the same colour (brightness-scaled). */
 static void fill_scaled(uint8_t r, uint8_t g, uint8_t b) {
     for (uint16_t i = 0; i < LED_MATRIX_SIZE; i++) {
         set_pixel_scaled(i, r, g, b);
     }
 }
 
 /* Push pixel buffer to the physical strip. */
 static void show(void) {
     if (led_strip) led_strip_refresh(led_strip);
 }
 
 /* Blank all pixels and push to strip. */
 static void clear_and_show(void) {
     if (led_strip) led_strip_clear(led_strip);
     show();
 }
 
 /* Map a 0-255 position on a colour wheel to an RGB triple.
  * The wheel cycles: red -> green -> blue -> red.
  * Directly ported from the WS2812FX color_wheel() function. */
 static void wheel_color(uint8_t pos, uint8_t *r, uint8_t *g, uint8_t *b) {
     pos = 255 - pos;
     if (pos < 85) {
         *r = 255 - pos * 3;  *g = 0;              *b = pos * 3;
     } else if (pos < 170) {
         pos -= 85;
         *r = 0;              *g = pos * 3;         *b = 255 - pos * 3;
     } else {
         pos -= 170;
         *r = pos * 3;        *g = 255 - pos * 3;  *b = 0;
     }
 }
 
 /* Hardware RNG → single random byte. */
 static uint8_t fast_random8(void) {
     return (uint8_t)(esp_random() & 0xFF);
 }
 
 /* ══════════════════════════════════════════════════════════════════════════
  *  WS2812FX-INSPIRED EFFECTS (fx_*)
  *
  *  Each function is self-contained and blocking.  It renders the full
  *  animation, then clears the strip before returning.
  *
  *  Timing budgets are tuned for a 30-LED strip so kids see a crisp,
  *  punchy animation that completes within ~0.5-2 seconds.
  * ══════════════════════════════════════════════════════════════════════════ */
 
 /* Pattern 1 -- Color Wipe
  * Fills pixels one-by-one from start to end, holds briefly, then wipes
  * them off in reverse.  Inspired by WS2812FX FX_MODE_COLOR_WIPE. */
static void fx_color_wipe(uint8_t r, uint8_t g, uint8_t b, uint8_t passes) {
    clear_and_show();
    uint8_t step = (passes < 2) ? 2 : 1;
    uint8_t hold = (passes < 2) ? 100 : 250;
    for (uint16_t i = 0; i < LED_MATRIX_SIZE; i += step) {
        for (uint16_t j = i; j < i + step && j < LED_MATRIX_SIZE; j++)
            set_pixel_scaled(j, r, g, b);
        show();
        vTaskDelay(pdMS_TO_TICKS(18));
    }
    vTaskDelay(pdMS_TO_TICKS(hold));
    for (int i = LED_MATRIX_SIZE - 1; i >= 0; i -= step) {
        for (int j = i; j > i - step && j >= 0; j--)
            set_pixel_scaled((uint16_t)j, 0, 0, 0);
        show();
        vTaskDelay(pdMS_TO_TICKS(12));
    }
    clear_and_show();
}
 
 /* Pattern 2 -- Theater Chase
  * Every 3rd pixel lights up, then shifts by one -- the classic
  * "marquee" effect.  Inspired by WS2812FX FX_MODE_THEATER_CHASE. */
static void fx_theater_chase(uint8_t r, uint8_t g, uint8_t b, uint8_t cycles) {
    for (uint8_t cycle = 0; cycle < cycles; cycle++) {
         for (uint8_t phase = 0; phase < 3; phase++) {
             if (led_strip) led_strip_clear(led_strip);
             for (uint16_t i = phase; i < LED_MATRIX_SIZE; i += 3) {
                 set_pixel_scaled(i, r, g, b);
             }
             show();
             vTaskDelay(pdMS_TO_TICKS(55));
         }
     }
     clear_and_show();
 }
 
 /* Pattern 3 -- Larson Scanner (Cylon / KITT)
  * A bright dot bounces back and forth with an exponentially fading tail.
  * Inspired by WS2812FX FX_MODE_LARSON_SCANNER. */
static void fx_larson_scanner(uint8_t r, uint8_t g, uint8_t b, uint8_t bounces) {
    for (uint8_t bounce = 0; bounce < bounces; bounce++) {
         /* Sweep left → right */
         for (int pos = 0; pos < LED_MATRIX_SIZE; pos++) {
             if (led_strip) led_strip_clear(led_strip);
             set_pixel_scaled((uint16_t)pos, r, g, b);
             if (pos > 0)
                 set_pixel_scaled((uint16_t)(pos - 1), r / 4, g / 4, b / 4);
             if (pos < LED_MATRIX_SIZE - 1)
                 set_pixel_scaled((uint16_t)(pos + 1), r / 4, g / 4, b / 4);
             if (pos > 1)
                 set_pixel_scaled((uint16_t)(pos - 2), r / 16, g / 16, b / 16);
             if (pos < LED_MATRIX_SIZE - 2)
                 set_pixel_scaled((uint16_t)(pos + 2), r / 16, g / 16, b / 16);
             show();
             vTaskDelay(pdMS_TO_TICKS(16));
         }
         /* Sweep right → left */
         for (int pos = LED_MATRIX_SIZE - 2; pos > 0; pos--) {
             if (led_strip) led_strip_clear(led_strip);
             set_pixel_scaled((uint16_t)pos, r, g, b);
             if (pos > 0)
                 set_pixel_scaled((uint16_t)(pos - 1), r / 4, g / 4, b / 4);
             if (pos < LED_MATRIX_SIZE - 1)
                 set_pixel_scaled((uint16_t)(pos + 1), r / 4, g / 4, b / 4);
             if (pos > 1)
                 set_pixel_scaled((uint16_t)(pos - 2), r / 16, g / 16, b / 16);
             if (pos < LED_MATRIX_SIZE - 2)
                 set_pixel_scaled((uint16_t)(pos + 2), r / 16, g / 16, b / 16);
             show();
             vTaskDelay(pdMS_TO_TICKS(16));
         }
     }
     clear_and_show();
 }
 
 /* Pattern 4 -- Breathe
  * The whole strip smoothly fades up then down using a gamma-corrected
  * lookup table for a natural-looking pulse.
  * Inspired by WS2812FX FX_MODE_BREATH. */
static void fx_breathe(uint8_t r, uint8_t g, uint8_t b, uint8_t passes) {
    static const uint8_t GAMMA_LUT[32] = {
        0, 1, 2, 4, 7, 11, 17, 24, 32, 42, 53, 66, 80, 96, 113, 131,
        150, 162, 176, 190, 205, 218, 228, 237, 244, 248, 252, 254, 255, 255, 254, 252
    };
    for (uint8_t pass = 0; pass < passes; pass++) {
         for (uint8_t i = 0; i < 32; i++) {
             uint8_t s = GAMMA_LUT[i];
             fill_scaled(scale8(r, s), scale8(g, s), scale8(b, s));
             show();
             vTaskDelay(pdMS_TO_TICKS(18));
         }
         for (int i = 31; i >= 0; i--) {
             uint8_t s = GAMMA_LUT[i];
             fill_scaled(scale8(r, s), scale8(g, s), scale8(b, s));
             show();
             vTaskDelay(pdMS_TO_TICKS(18));
         }
     }
     clear_and_show();
 }
 
 /* Pattern 5 -- Rainbow Cycle
  * Distributes the full colour wheel evenly across all LEDs, then
  * rotates the wheel to create a flowing rainbow.  Ignores the
  * per-pattern colour -- it uses the full spectrum.
  * Inspired by WS2812FX FX_MODE_RAINBOW_CYCLE. */
static void fx_rainbow_cycle(uint16_t max_frames) {
    for (uint16_t frame = 0; frame < max_frames; frame += 3) {
         for (uint16_t i = 0; i < LED_MATRIX_SIZE; i++) {
             uint8_t hue = (uint8_t)(((i * 256) / LED_MATRIX_SIZE + frame) & 0xFF);
             uint8_t r, g, b;
             wheel_color(hue, &r, &g, &b);
             set_pixel_scaled(i, r, g, b);
         }
         show();
         vTaskDelay(pdMS_TO_TICKS(15));
     }
     clear_and_show();
 }
 
 /* Pattern 6 -- Sparkle
  * Dim background with random full-brightness pixels popping on/off.
  * Inspired by WS2812FX FX_MODE_SPARKLE. */
static void fx_sparkle(uint8_t r, uint8_t g, uint8_t b, uint8_t frames) {
    uint8_t dim_r = r / 6, dim_g = g / 6, dim_b = b / 6;

    for (uint8_t frame = 0; frame < frames; frame++) {
         fill_scaled(dim_r, dim_g, dim_b);
         uint16_t spark = fast_random8() % LED_MATRIX_SIZE;
         set_pixel_scaled(spark, r, g, b);
         if (frame & 1) {
             uint16_t spark2 = fast_random8() % LED_MATRIX_SIZE;
             set_pixel_scaled(spark2, r, g, b);
         }
         show();
         vTaskDelay(pdMS_TO_TICKS(35));
     }
     clear_and_show();
 }
 
 /* Pattern 7 -- Running Lights
  * A sinusoidal intensity wave scrolls along the strip.
  * Inspired by WS2812FX FX_MODE_RUNNING_LIGHTS. */
static void fx_running_lights(uint8_t r, uint8_t g, uint8_t b, uint16_t steps) {
    static const uint8_t SIN_LUT[16] = {
        0, 24, 49, 74, 98, 120, 142, 162, 180, 196, 210, 222, 231, 238, 243, 246
    };
    for (uint16_t step = 0; step < steps; step++) {
         for (uint16_t i = 0; i < LED_MATRIX_SIZE; i++) {
             uint8_t idx = (uint8_t)((i + step) % 16);
             uint8_t s = (idx < 16) ? SIN_LUT[idx] : SIN_LUT[15 - (idx - 16)];
             set_pixel_scaled(i, scale8(r, s), scale8(g, s), scale8(b, s));
         }
         show();
         vTaskDelay(pdMS_TO_TICKS(22));
     }
     clear_and_show();
 }
 
 /* Pattern 8 -- Fire Flicker
  * Each pixel gets a random intensity offset every frame, creating a
  * warm flickering campfire look.
  * Inspired by WS2812FX FX_MODE_FIRE_FLICKER. */
static void fx_fire_flicker(uint8_t r, uint8_t g, uint8_t b, uint8_t frames) {
    for (uint8_t frame = 0; frame < frames; frame++) {
         for (uint16_t i = 0; i < LED_MATRIX_SIZE; i++) {
             uint8_t flicker = fast_random8() % 100;
             uint8_t fr = (uint8_t)((uint16_t)r * (155 + flicker) / 255);
             uint8_t fg = (uint8_t)((uint16_t)g * (155 + flicker) / 255);
             uint8_t fb = (uint8_t)((uint16_t)b * (155 + flicker) / 255);
             set_pixel_scaled(i, fr, fg, fb);
         }
         show();
         vTaskDelay(pdMS_TO_TICKS(25));
     }
     clear_and_show();
 }
 
 /* Pattern 9 -- Comet
  * A bright head pixel streaks across the strip with an exponentially
  * fading tail behind it.  Inspired by WS2812FX FX_MODE_COMET. */
static void fx_comet(uint8_t r, uint8_t g, uint8_t b, uint8_t passes) {
    const uint8_t TAIL_LEN = 8;
    static const uint8_t TAIL_FADE[8] = {255, 180, 120, 70, 35, 15, 5, 1};

    for (uint8_t pass = 0; pass < passes; pass++) {
         for (int head = -TAIL_LEN; head < LED_MATRIX_SIZE + TAIL_LEN; head++) {
             if (led_strip) led_strip_clear(led_strip);
             for (uint8_t t = 0; t < TAIL_LEN; t++) {
                 int pos = head - t;
                 if (pos >= 0 && pos < LED_MATRIX_SIZE) {
                     set_pixel_scaled((uint16_t)pos,
                                      scale8(r, TAIL_FADE[t]),
                                      scale8(g, TAIL_FADE[t]),
                                      scale8(b, TAIL_FADE[t]));
                 }
             }
             show();
             vTaskDelay(pdMS_TO_TICKS(14));
         }
     }
     clear_and_show();
 }
 
 /* ══════════════════════════════════════════════════════════════════════════
  *  PUBLIC API
  * ══════════════════════════════════════════════════════════════════════════ */
 
 /**
  * Initialise the WS2812 strip via the RMT peripheral.
  * Call once from app_main() before any other matrix/led_flash function.
  */
 esp_err_t led_matrix_init(void) {
     ESP_LOGI(TAG, "Initializing LED Matrix (%d LEDs) on GPIO%d",
              LED_MATRIX_SIZE, LED_GPIO);
 
     led_strip_config_t strip_config = {
         .strip_gpio_num = LED_GPIO,
         .max_leds = LED_MATRIX_SIZE,
         .led_pixel_format = LED_PIXEL_FORMAT_GRB,
         .led_model = LED_MODEL_WS2812,
         .flags.invert_out = false,
     };
 
     led_strip_rmt_config_t rmt_config = {
         .clk_src = RMT_CLK_SRC_DEFAULT,
         .resolution_hz = 10 * 1000 * 1000,  /* 10 MHz tick → 100 ns resolution */
         .flags.with_dma = false,
     };
 
     esp_err_t err = led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip);
     if (err != ESP_OK) {
         ESP_LOGE(TAG, "Failed to create LED strip: %s", esp_err_to_name(err));
         return err;
     }
 
     led_strip_clear(led_strip);
     ESP_LOGI(TAG, "LED Matrix initialized successfully");
     return ESP_OK;
 }
 
 
 /* ── Low-level primitives ──────────────────────────────────────────────
  *  These are thin wrappers so command_handler.c can drive the strip
  *  directly for raw I2C commands (CMD_MATRIX_FILL, CMD_MATRIX_CLEAR,
  *  CMD_MATRIX_BRIGHTNESS, CMD_MATRIX_SHOW).                           */
 
 void matrix_fill(uint8_t r, uint8_t g, uint8_t b) {
     fill_scaled(r, g, b);
 }
 
 void matrix_set_pixel(uint16_t idx, uint8_t r, uint8_t g, uint8_t b) {
     set_pixel_scaled(idx, r, g, b);
 }
 
 void matrix_clear(void) {
     if (led_strip) led_strip_clear(led_strip);
 }
 
 void matrix_show(void) {
     show();
 }
 
 void matrix_set_brightness(uint8_t brightness) {
     matrix_brightness = brightness;
     ESP_LOGI(TAG, "Brightness set to %d", matrix_brightness);
 }
 
 uint8_t matrix_get_brightness(void) {
     return matrix_brightness;
 }
 
 uint16_t matrix_get_size(void) {
     return LED_MATRIX_SIZE;
 }
 
 /** Return human-readable pattern name for TFT / log display. */
 const char *led_pattern_name(uint8_t pattern_id) {
     if (pattern_id > 9) return "Unknown";
     return PATTERN_NAMES[pattern_id];
 }
 
 /* ── Preview & Execute ─────────────────────────────────────────────────
  *
 *  preview  = half-length animation so the user sees the pattern they
 *             picked; triggered by numpad tap or CMD_SET_LED from
 *             the brain.
 *
 *  execute  = full WS2812FX-style animation; triggered by SUBMIT on
 *             the TFT or CMD_EXECUTE from the brain.                  */
 
void led_flash_play_preview(uint8_t color_id) {
    uint8_t id = color_id % 10U;
    rgb_t c = PATTERN_COLORS[id];

    ESP_LOGI(TAG, "Preview pattern %u: %s", id, PATTERN_NAMES[id]);

    switch (id) {
        case 0: clear_and_show();                          break;
        case 1: fx_color_wipe(c.r, c.g, c.b, 1);         break;
        case 2: fx_theater_chase(c.r, c.g, c.b, 5);       break;
        case 3: fx_larson_scanner(c.r, c.g, c.b, 2);      break;
        case 4: fx_breathe(c.r, c.g, c.b, 1);             break;
        case 5: fx_rainbow_cycle(128);                     break;
        case 6: fx_sparkle(c.r, c.g, c.b, 20);            break;
        case 7: fx_running_lights(c.r, c.g, c.b, 40);     break;
        case 8: fx_fire_flicker(c.r, c.g, c.b, 30);       break;
        case 9: fx_comet(c.r, c.g, c.b, 1);               break;
        default: clear_and_show();                         break;
    }
}
 
 void led_flash_play_execute(uint8_t color_id) {
     uint8_t id = color_id % 10U;
     rgb_t c = PATTERN_COLORS[id];
 
     ESP_LOGI(TAG, "Execute pattern %u: %s", id, PATTERN_NAMES[id]);
 
    switch (id) {
        case 0: clear_and_show();                          break;
        case 1: fx_color_wipe(c.r, c.g, c.b, 2);         break;
        case 2: fx_theater_chase(c.r, c.g, c.b, 10);      break;
        case 3: fx_larson_scanner(c.r, c.g, c.b, 4);      break;
        case 4: fx_breathe(c.r, c.g, c.b, 3);             break;
        case 5: fx_rainbow_cycle(256);                     break;
        case 6: fx_sparkle(c.r, c.g, c.b, 40);            break;
        case 7: fx_running_lights(c.r, c.g, c.b, 80);     break;
        case 8: fx_fire_flicker(c.r, c.g, c.b, 60);       break;
        case 9: fx_comet(c.r, c.g, c.b, 3);               break;
        default: clear_and_show();                         break;
    }
 }
 