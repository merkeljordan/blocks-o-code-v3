#include "led_matrix.h"

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"
#include "status_strip.h"

static const char *TAG = "LED_MATRIX";

#define LED_GPIO            15
#define LED_MATRIX_SIZE     16

typedef struct { uint8_t r, g, b; } matrix_rgb_t;

static led_strip_handle_t led_strip = NULL;
static uint8_t matrix_brightness = 50;
static matrix_rgb_t matrix_pixels[LED_MATRIX_SIZE];
static bool status_mirror_enabled = false;

static void render_status_strip_mirror(void)
{
    if (!status_mirror_enabled) {
        return;
    }

    uint16_t strip_led_count = status_strip_get_led_count();
    if (strip_led_count == 0U) {
        return;
    }

    status_strip_clear();
    status_strip_set_brightness(255U);

    for (uint16_t i = 0; i < strip_led_count; i++) {
        uint16_t src_idx = (uint16_t)(((uint32_t)i * LED_MATRIX_SIZE) / strip_led_count);
        if (src_idx >= LED_MATRIX_SIZE) {
            src_idx = LED_MATRIX_SIZE - 1;
        }
        status_strip_set_pixel(i,
                               matrix_pixels[src_idx].r,
                               matrix_pixels[src_idx].g,
                               matrix_pixels[src_idx].b);
    }

    (void)status_strip_show();
}

// ============================================================================
// LED MATRIX INITIALIZATION
// ============================================================================
esp_err_t led_matrix_init(void) {
    ESP_LOGI(TAG, "Initializing LED Matrix (%d LEDs) on GPIO%d",
             LED_MATRIX_SIZE, LED_GPIO);

    // LED strip configuration for WS2812B
    led_strip_config_t strip_config = {
        .strip_gpio_num = LED_GPIO,
        .max_leds = LED_MATRIX_SIZE,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .led_model = LED_MODEL_WS2812,
        .flags.invert_out = false,
    };

    // RMT configuration for WS2812B timing
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .flags.with_dma = false,
    };

    esp_err_t err = led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create LED strip: %s", esp_err_to_name(err));
        return err;
    }

    // Clear matrix on startup
    led_strip_clear(led_strip);

    ESP_LOGI(TAG, "LED Matrix initialized successfully!");
    return ESP_OK;
}

// ============================================================================
// STARTUP ANIMATION
// ============================================================================
void led_matrix_startup_animation(void) {
    // Intentionally no startup animation (avoid distracting flashes).
}

// ============================================================================
// MATRIX FILL
// ============================================================================
void matrix_fill(uint8_t r, uint8_t g, uint8_t b) {
    uint8_t sr = (r * matrix_brightness) / 255;
    uint8_t sg = (g * matrix_brightness) / 255;
    uint8_t sb = (b * matrix_brightness) / 255;

    for (int i = 0; i < LED_MATRIX_SIZE; i++) {
        matrix_pixels[i] = (matrix_rgb_t){sr, sg, sb};
        led_strip_set_pixel(led_strip, i, sr, sg, sb);
    }
}

void matrix_clear(void) {
    memset(matrix_pixels, 0, sizeof(matrix_pixels));
    led_strip_clear(led_strip);
}

void matrix_show(void) {
    led_strip_refresh(led_strip);
    render_status_strip_mirror();
}

// ============================================================================
// SET BRIGHTNESS
// ============================================================================
void matrix_set_brightness(uint8_t brightness) {
    matrix_brightness = brightness;
    ESP_LOGI(TAG, "Brightness set to %d", matrix_brightness);
}

// ============================================================================
// GET BRIGHTNESS
// ============================================================================
uint8_t matrix_get_brightness(void) {
    return matrix_brightness;
}

void led_matrix_set_status_mirror(bool enabled) {
    status_mirror_enabled = enabled;
    if (enabled) {
        render_status_strip_mirror();
    }
}
