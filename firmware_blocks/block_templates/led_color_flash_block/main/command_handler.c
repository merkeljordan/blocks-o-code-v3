#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i2c_protocol.h"

// Forward declarations from led_matrix.c
extern void matrix_fill(uint8_t r, uint8_t g, uint8_t b);
extern void matrix_clear(void);
extern void matrix_show(void);
extern void matrix_set_brightness(uint8_t brightness);
extern uint8_t matrix_get_brightness(void);

static const char *TAG = "CMD_HANDLER";

// Module-private state
static uint8_t led_r = 0, led_g = 0, led_b = 0;
static uint8_t color_id = 0;
static uint8_t current_status = STATUS_READY;

static void color_id_to_rgb(uint8_t id, uint8_t *r, uint8_t *g, uint8_t *b) {
    // Simple 0-9 palette (update as needed)
    switch (id % 10) {
        case 0: *r = 20; *g = 0;  *b = 0;  break; // red
        case 1: *r = 0;  *g = 20; *b = 0;  break; // green
        case 2: *r = 0;  *g = 0;  *b = 20; break; // blue
        case 3: *r = 20; *g = 20; *b = 0;  break; // yellow
        case 4: *r = 20; *g = 0;  *b = 20; break; // magenta
        case 5: *r = 0;  *g = 20; *b = 20; break; // cyan
        case 6: *r = 20; *g = 10; *b = 0;  break; // orange
        case 7: *r = 10; *g = 0;  *b = 20; break; // purple
        case 8: *r = 20; *g = 20; *b = 20; break; // white (dim)
        case 9: *r = 0;  *g = 0;  *b = 0;  break; // off
        default: *r = 0; *g = 0; *b = 0; break;
    }
}

static void flash_color(uint8_t r, uint8_t g, uint8_t b, uint32_t ms) {
    matrix_fill(r, g, b);
    matrix_show();
    vTaskDelay(pdMS_TO_TICKS(ms));
    matrix_clear();
    matrix_show();
}

size_t get_data_payload(uint8_t *out, size_t max_len) {
    if (max_len < 1) {
        return 0;
    }
    out[0] = color_id;
    return 1;
}

// ============================================================================
// COMMAND HANDLER
// ============================================================================
void handle_command(uint8_t *buffer, int len) {
    if (len < 1) {
        return;
    }

    uint8_t cmd = buffer[0];

    ESP_LOGI(TAG, "Command: %s (0x%02X), Length: %d bytes",
             command_to_string(cmd), cmd, len);

    switch (cmd) {
        case CMD_PING:
            ESP_LOGI(TAG, "  → PING");
            current_status = STATUS_READY;
            break;

        case CMD_GET_TYPE:
            ESP_LOGI(TAG, "  → GET_TYPE");
            break;

        case CMD_SET_LED:
            if (len == 2) {
                color_id = buffer[1];
                color_id_to_rgb(color_id, &led_r, &led_g, &led_b);
                ESP_LOGI(TAG, "  → SET_COLOR ID=%d RGB(%d, %d, %d)",
                         color_id, led_r, led_g, led_b);
                flash_color(led_r, led_g, led_b, 120);
                current_status = STATUS_DATA_READY;
            } else if (len >= 4) {
                led_r = buffer[1];
                led_g = buffer[2];
                led_b = buffer[3];
                ESP_LOGI(TAG, "  → SET_LED RGB(%d, %d, %d)", led_r, led_g, led_b);
                current_status = STATUS_DATA_READY;
            }
            break;

        case CMD_MATRIX_FILL:
            if (len >= 4) {
                uint8_t r = buffer[1];
                uint8_t g = buffer[2];
                uint8_t b = buffer[3];
                ESP_LOGI(TAG, "  → FILL RGB(%d, %d, %d)", r, g, b);
                matrix_fill(r, g, b);
                matrix_show();
            }
            break;

        case CMD_MATRIX_CLEAR:
            ESP_LOGI(TAG, "  → CLEAR");
            matrix_clear();
            matrix_show();
            break;

        case CMD_MATRIX_BRIGHTNESS:
            if (len >= 2) {
                uint8_t brightness = buffer[1];
                ESP_LOGI(TAG, "  → BRIGHTNESS %d", brightness);
                matrix_set_brightness(brightness);
            }
            break;

        case CMD_MATRIX_SHOW:
            ESP_LOGI(TAG, "  → SHOW");
            matrix_show();
            break;

        case CMD_RESET:
            ESP_LOGI(TAG, "  → RESET");
            led_r = 0;
            led_g = 0;
            led_b = 0;
            color_id = 0;
            matrix_clear();
            matrix_show();
            current_status = STATUS_READY;
            break;

        case CMD_EXECUTE:
            ESP_LOGI(TAG, "  → EXECUTE color_id=%d", color_id);
            color_id_to_rgb(color_id, &led_r, &led_g, &led_b);
            flash_color(led_r, led_g, led_b, 300);
            current_status = STATUS_READY;
            break;

        default:
            ESP_LOGW(TAG, "  → Unknown command: 0x%02X", cmd);
            break;
    }
}

// ============================================================================
// LED STATUS TASK
// ============================================================================
void led_status_task(void *arg) {
    ESP_LOGI(TAG, "LED status task started");

    while (1) {
        ESP_LOGI(TAG, "Status: %s | LED: RGB(%d,%d,%d) | Brightness: %d%%",
                 (current_status & STATUS_READY) ? "READY" : "BUSY",
                 led_r, led_g, led_b,
                 (matrix_get_brightness() * 100) / 255);

        vTaskDelay(pdMS_TO_TICKS(10000));  // Every 10 seconds
    }
}
