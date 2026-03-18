#include <stdio.h>
#include <stdint.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "i2c_protocol.h"
#include "led_matrix.h"
#include "status_strip.h"

static const char *TAG = "CMD_HANDLER";
#define STATUS_STRIP_GPIO      GPIO_NUM_13
#define STATUS_STRIP_LED_COUNT 16

static const status_strip_config_t kStatusStripConfig = {
    .gpio_num = STATUS_STRIP_GPIO,
    .led_count = STATUS_STRIP_LED_COUNT,
};

static uint8_t led_r = 0, led_g = 0, led_b = 0;
static uint8_t current_status = STATUS_READY;

static void run_execute_feedback(void)
{
    current_status = STATUS_BUSY;
    matrix_fill(255, 0, 255);
    matrix_show();
    vTaskDelay(pdMS_TO_TICKS(120));
    matrix_clear();
    matrix_show();
    current_status = STATUS_READY;
}

void handle_command(uint8_t *buffer, int len) {
    if (len < 1) {
        return;
    }

    uint8_t cmd = buffer[0];

    ESP_LOGI(TAG, "Command: %s (0x%02X), Length: %d bytes",
             command_to_string(cmd), cmd, len);

    if (status_strip_handle_matrix_command(TAG,
                                           &kStatusStripConfig,
                                           (i2c_command_t)cmd,
                                           (len > 1) ? &buffer[1] : NULL,
                                           (len > 1) ? (size_t)(len - 1) : 0U)) {
        return;
    }

    switch (cmd) {
        case CMD_PING:
            ESP_LOGI(TAG, "  -> PING");
            current_status = STATUS_READY;
            break;

        case CMD_GET_TYPE:
            ESP_LOGI(TAG, "  -> GET_TYPE");
            break;

        case CMD_SET_LED:
            if (len >= 4) {
                led_r = buffer[1];
                led_g = buffer[2];
                led_b = buffer[3];
                ESP_LOGI(TAG, "  -> SET_LED RGB(%d, %d, %d)", led_r, led_g, led_b);
            }
            break;

        case CMD_EXECUTE:
            ESP_LOGI(TAG, "  -> EXECUTE");
            run_execute_feedback();
            break;

        case CMD_RESET:
            ESP_LOGI(TAG, "  -> RESET");
            led_r = 0;
            led_g = 0;
            led_b = 0;
            (void)status_strip_reset(&kStatusStripConfig);
            matrix_clear();
            matrix_show();
            current_status = STATUS_READY;
            break;

        default:
            ESP_LOGW(TAG, "  -> Unknown command: 0x%02X", cmd);
            break;
    }
}

uint8_t command_handler_get_status_flags(void)
{
    return current_status;
}

void led_status_task(void *arg) {
    ESP_LOGI(TAG, "LED status task started");

    while (1) {
        ESP_LOGI(TAG, "Status: %s | LED: RGB(%d,%d,%d) | Brightness: %d%%",
                 (current_status & STATUS_BUSY) ? "BUSY" : "READY",
                 led_r, led_g, led_b,
                 (matrix_get_brightness() * 100) / 255);

        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
