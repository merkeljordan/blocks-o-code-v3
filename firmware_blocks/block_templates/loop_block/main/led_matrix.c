#include <stdio.h>
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"

static const char *TAG = "LED_MATRIX";

// LED Matrix Configuration (update per block)
#define LED_GPIO            15
#define LED_MATRIX_SIZE     16

// Module-private state
static led_strip_handle_t led_strip = NULL;
static uint8_t matrix_brightness = 50;  // 0-255 (~20% starting)

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
}

// ============================================================================
// MATRIX FILL
// ============================================================================
void matrix_fill(uint8_t r, uint8_t g, uint8_t b) {
    ESP_LOGI(TAG, "Filling matrix RGB(%d, %d, %d) @ brightness %d",
             r, g, b, matrix_brightness);

    // Apply brightness scaling
    r = (r * matrix_brightness) / 255;
    g = (g * matrix_brightness) / 255;
    b = (b * matrix_brightness) / 255;

    for (int i = 0; i < LED_MATRIX_SIZE; i++) {
        led_strip_set_pixel(led_strip, i, r, g, b);
    }
}

// ============================================================================
// MATRIX CLEAR
// ============================================================================
void matrix_clear(void) {
    ESP_LOGI(TAG, "Clearing matrix");
    led_strip_clear(led_strip);
}

// ============================================================================
// MATRIX SHOW (Refresh Display)
// ============================================================================
void matrix_show(void) {
    led_strip_refresh(led_strip);
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
