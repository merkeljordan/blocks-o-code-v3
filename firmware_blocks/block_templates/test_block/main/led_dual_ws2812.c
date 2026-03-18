#include <stdint.h>

#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"

#include "led_dual_ws2812.h"
#include "led_strip.h"

static const char *TAG = "DUAL_WS2812";

/* "Matrix" LEDs (GPIO 18) */
#define MATRIX_LED_GPIO      15
#define MATRIX_LED_COUNT     16

/* "LED strip" LEDs (GPIO 13) */
#define STRIP_LED_GPIO       13
#define STRIP_LED_COUNT      30

/* Software brightness scaling, 0..255 */
#define MATRIX_TEST_BRIGHTNESS   200
#define STRIP_TEST_BRIGHTNESS    200

typedef struct {
    uint16_t count;
    uint8_t brightness;
    led_strip_handle_t handle;
} ws2812_device_t;

static ws2812_device_t s_matrix = {
    .count = MATRIX_LED_COUNT,
    .brightness = MATRIX_TEST_BRIGHTNESS,
    .handle = NULL,
};

static ws2812_device_t s_strip = {
    .count = STRIP_LED_COUNT,
    .brightness = STRIP_TEST_BRIGHTNESS,
    .handle = NULL,
};

static inline uint8_t scale8(uint8_t value, uint8_t brightness)
{
    return (uint8_t)(((uint16_t)value * brightness) / 255U);
}

static esp_err_t ws2812_init_device(ws2812_device_t *dev, gpio_num_t gpio)
{
    if (!dev) return ESP_ERR_INVALID_ARG;
    if (dev->handle) return ESP_OK;

    led_strip_config_t strip_config = {
        .strip_gpio_num = gpio,
        .max_leds = dev->count,
        .led_pixel_format = LED_PIXEL_FORMAT_GRB,
        .led_model = LED_MODEL_WS2812,
        .flags.invert_out = 0,
    };

    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .flags.with_dma = 0,
    };

    ESP_LOGI(TAG, "Init WS2812 device gpio=%d leds=%u", (int)gpio, (unsigned)dev->count);
    esp_err_t err = led_strip_new_rmt_device(&strip_config, &rmt_config, &dev->handle);
    if (err != ESP_OK) return err;

    led_strip_clear(dev->handle);
    led_strip_refresh(dev->handle);
    return ESP_OK;
}

static void ws2812_fill(ws2812_device_t *dev, uint8_t r, uint8_t g, uint8_t b)
{
    if (!dev || !dev->handle) return;

    for (uint16_t i = 0; i < dev->count; i++) {
        led_strip_set_pixel(dev->handle, i,
                            scale8(r, dev->brightness),
                            scale8(g, dev->brightness),
                            scale8(b, dev->brightness));
    }
}

static void ws2812_show(ws2812_device_t *dev)
{
    if (!dev || !dev->handle) return;
    led_strip_refresh(dev->handle);
}

static void ws2812_clear(ws2812_device_t *dev)
{
    if (!dev || !dev->handle) return;
    led_strip_clear(dev->handle);
    led_strip_refresh(dev->handle);
}

esp_err_t led_dual_ws2812_init(void)
{
    esp_err_t err = ESP_OK;
    err = ws2812_init_device(&s_matrix, MATRIX_LED_GPIO);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Matrix init failed: %s", esp_err_to_name(err));
        return err;
    }

    err = ws2812_init_device(&s_strip, STRIP_LED_GPIO);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Strip init failed: %s", esp_err_to_name(err));
        ws2812_clear(&s_matrix);
        return err;
    }

    return ESP_OK;
}

void led_dual_ws2812_clear(void)
{
    ws2812_clear(&s_matrix);
    ws2812_clear(&s_strip);
}

void led_dual_ws2812_flash_matrix_red(uint32_t duration_ms)
{
    ESP_LOGI(TAG, "Flash matrix RED");
    ws2812_fill(&s_matrix, 255, 0, 0);
    ws2812_show(&s_matrix);
    if (duration_ms) vTaskDelay(pdMS_TO_TICKS(duration_ms));
    ws2812_clear(&s_matrix);
}

void led_dual_ws2812_flash_ledstrip_blue(uint32_t duration_ms)
{
    ESP_LOGI(TAG, "Flash ledstrip BLUE");
    ws2812_fill(&s_strip, 0, 0, 255);
    ws2812_show(&s_strip);
    if (duration_ms) vTaskDelay(pdMS_TO_TICKS(duration_ms));
    ws2812_clear(&s_strip);
}

