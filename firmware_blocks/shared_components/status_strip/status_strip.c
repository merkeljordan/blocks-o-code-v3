#include "status_strip.h"

#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "led_strip.h"

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} status_strip_pixel_t;

static const char *TAG = "STATUS_STRIP";
static led_strip_handle_t s_strip = NULL;
static status_strip_pixel_t *s_pixels = NULL;
static gpio_num_t s_gpio_num = GPIO_NUM_NC;
static uint16_t s_led_count = 0;
static uint8_t s_brightness = 96U;

static uint8_t scale_channel(uint8_t channel)
{
    return (uint8_t)(((uint16_t)channel * s_brightness) / 255U);
}

static bool status_strip_is_ready(void)
{
    return (s_strip != NULL && s_pixels != NULL && s_led_count > 0U);
}

esp_err_t status_strip_ensure_ready(const status_strip_config_t *cfg)
{
    if (cfg == NULL || cfg->gpio_num == GPIO_NUM_NC || cfg->led_count == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    if (status_strip_is_ready()) {
        if (s_gpio_num == cfg->gpio_num && s_led_count == cfg->led_count) {
            return ESP_OK;
        }
        ESP_LOGE(TAG,
                 "status strip already initialized on GPIO %d (%u LEDs), requested GPIO %d (%u LEDs)",
                 (int)s_gpio_num,
                 (unsigned)s_led_count,
                 (int)cfg->gpio_num,
                 (unsigned)cfg->led_count);
        return ESP_ERR_INVALID_STATE;
    }

    s_pixels = calloc(cfg->led_count, sizeof(*s_pixels));
    if (s_pixels == NULL) {
        ESP_LOGE(TAG, "Failed to allocate pixel buffer for %u LEDs", (unsigned)cfg->led_count);
        return ESP_ERR_NO_MEM;
    }

    led_strip_config_t strip_config = {
        .strip_gpio_num = cfg->gpio_num,
        .max_leds = cfg->led_count,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .led_model = LED_MODEL_WS2812,
        .flags.invert_out = false,
    };

    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .flags.with_dma = false,
    };

    esp_err_t err = led_strip_new_rmt_device(&strip_config, &rmt_config, &s_strip);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create status strip: %s", esp_err_to_name(err));
        free(s_pixels);
        s_pixels = NULL;
        return err;
    }

    s_gpio_num = cfg->gpio_num;
    s_led_count = cfg->led_count;
    s_brightness = 96U;

    err = led_strip_clear(s_strip);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Initial clear failed: %s", esp_err_to_name(err));
    }

    ESP_LOGI(TAG, "Initialized status strip on GPIO %d with %u LEDs",
             (int)s_gpio_num, (unsigned)s_led_count);
    return ESP_OK;
}

void status_strip_fill(uint8_t r, uint8_t g, uint8_t b)
{
    if (!status_strip_is_ready()) {
        return;
    }

    for (uint16_t i = 0; i < s_led_count; i++) {
        s_pixels[i].r = r;
        s_pixels[i].g = g;
        s_pixels[i].b = b;
    }
}

void status_strip_clear(void)
{
    if (!status_strip_is_ready()) {
        return;
    }

    memset(s_pixels, 0, s_led_count * sizeof(*s_pixels));
}

void status_strip_set_brightness(uint8_t brightness)
{
    s_brightness = brightness;
}

uint8_t status_strip_get_brightness(void)
{
    return s_brightness;
}

esp_err_t status_strip_show(void)
{
    if (!status_strip_is_ready()) {
        return ESP_ERR_INVALID_STATE;
    }

    for (uint16_t i = 0; i < s_led_count; i++) {
        esp_err_t err = led_strip_set_pixel(s_strip,
                                            i,
                                            scale_channel(s_pixels[i].r),
                                            scale_channel(s_pixels[i].g),
                                            scale_channel(s_pixels[i].b));
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set pixel %u: %s", (unsigned)i, esp_err_to_name(err));
            return err;
        }
    }

    return led_strip_refresh(s_strip);
}

esp_err_t status_strip_reset(const status_strip_config_t *cfg)
{
    esp_err_t err = status_strip_ensure_ready(cfg);
    if (err != ESP_OK) {
        return err;
    }

    status_strip_clear();
    return status_strip_show();
}

bool status_strip_handle_matrix_command(const char *tag,
                                        const status_strip_config_t *cfg,
                                        i2c_command_t cmd,
                                        const uint8_t *payload,
                                        size_t payload_len)
{
    if (cmd != CMD_MATRIX_FILL &&
        cmd != CMD_MATRIX_CLEAR &&
        cmd != CMD_MATRIX_BRIGHTNESS &&
        cmd != CMD_MATRIX_SHOW) {
        return false;
    }

    esp_err_t err = status_strip_ensure_ready(cfg);
    if (err != ESP_OK) {
        ESP_LOGE(tag ? tag : TAG, "Status strip init failed: %s", esp_err_to_name(err));
        return true;
    }

    switch (cmd) {
        case CMD_MATRIX_FILL:
            if (payload_len >= 3U) {
                status_strip_fill(payload[0], payload[1], payload[2]);
                ESP_LOGI(tag ? tag : TAG, "Status strip fill RGB(%u, %u, %u)",
                         (unsigned)payload[0], (unsigned)payload[1], (unsigned)payload[2]);
            }
            return true;

        case CMD_MATRIX_CLEAR:
            status_strip_clear();
            ESP_LOGI(tag ? tag : TAG, "Status strip clear");
            return true;

        case CMD_MATRIX_BRIGHTNESS:
            if (payload_len >= 1U) {
                status_strip_set_brightness(payload[0]);
                ESP_LOGI(tag ? tag : TAG, "Status strip brightness %u", (unsigned)payload[0]);
            }
            return true;

        case CMD_MATRIX_SHOW:
            err = status_strip_show();
            if (err != ESP_OK) {
                ESP_LOGE(tag ? tag : TAG, "Status strip show failed: %s", esp_err_to_name(err));
            }
            return true;

        default:
            return false;
    }
}
