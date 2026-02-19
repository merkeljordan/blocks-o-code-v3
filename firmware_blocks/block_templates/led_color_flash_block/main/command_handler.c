#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "i2c_protocol.h"
#include "command_handler.h"

// Forward declarations from led_matrix.c
extern void matrix_fill(uint8_t r, uint8_t g, uint8_t b);
extern void matrix_clear(void);
extern void matrix_show(void);
extern void matrix_set_pixel(uint8_t idx, uint8_t r, uint8_t g, uint8_t b);
extern uint8_t matrix_get_size(void);
extern void matrix_set_brightness(uint8_t brightness);
extern uint8_t matrix_get_brightness(void);

static const char *TAG = "CMD_HANDLER";

// Module-private runtime state mirrored to Brain via REG_STATUS/CMD_GET_DATA.
static uint8_t led_r = 0, led_g = 0, led_b = 0;
static uint8_t color_id = 0;
static uint8_t current_status = STATUS_READY;
static QueueHandle_t s_action_queue = NULL;
static bool s_pending_event_valid = false;
static uint8_t s_pending_event_id = 0;
static uint8_t s_pending_event_value = 0;

#define LED_FLASH_EVENT_SELECTION_SUBMIT 0x01

// Action queue types decouple request source (UI/I2C) from timing-heavy execution.
typedef enum {
    ACTION_PREVIEW_DIGIT = 0,
    ACTION_EXECUTE_DIGIT = 1,
    ACTION_EXECUTE_COLOR_ID = 2,
} led_action_type_t;

typedef struct {
    led_action_type_t type;
    uint8_t value;
} led_action_t;

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

// Atomic flash helper used by the worker task.
static void flash_color(uint8_t r, uint8_t g, uint8_t b, uint32_t ms) {
    matrix_fill(r, g, b);
    matrix_show();
    vTaskDelay(pdMS_TO_TICKS(ms));
    matrix_clear();
    matrix_show();
}

static void set_all_pixels(uint8_t r, uint8_t g, uint8_t b) {
    uint8_t n = matrix_get_size();
    for (uint8_t i = 0; i < n; i++) {
        matrix_set_pixel(i, r, g, b);
    }
}

static void color_wheel(uint8_t pos, uint8_t *r, uint8_t *g, uint8_t *b) {
    // Standard 0..255 rainbow wheel.
    if (pos < 85) {
        *r = (uint8_t)(255 - pos * 3);
        *g = (uint8_t)(pos * 3);
        *b = 0;
    } else if (pos < 170) {
        pos = (uint8_t)(pos - 85);
        *r = 0;
        *g = (uint8_t)(255 - pos * 3);
        *b = (uint8_t)(pos * 3);
    } else {
        pos = (uint8_t)(pos - 170);
        *r = (uint8_t)(pos * 3);
        *g = 0;
        *b = (uint8_t)(255 - pos * 3);
    }
}

// WS2812FX-inspired: Chase Flash Random (mode 35 feel).
static void effect_chase_flash_random(void) {
    uint8_t n = matrix_get_size();
    if (n == 0) {
        return;
    }

    for (uint8_t round = 0; round < 5; round++) {
        uint8_t base_r = (uint8_t)(80 + (esp_random() % 140));
        uint8_t base_g = (uint8_t)(80 + (esp_random() % 140));
        uint8_t base_b = (uint8_t)(80 + (esp_random() % 140));

        for (uint8_t head = 0; head < n; head++) {
            set_all_pixels(base_r, base_g, base_b);
            matrix_set_pixel(head, 255, 255, 255);
            matrix_show();
            vTaskDelay(pdMS_TO_TICKS(70));
        }

        // Quick white sparkle burst between random color chases.
        for (uint8_t i = 0; i < 2; i++) {
            set_all_pixels(255, 255, 255);
            matrix_show();
            vTaskDelay(pdMS_TO_TICKS(35));
            set_all_pixels(base_r, base_g, base_b);
            matrix_show();
            vTaskDelay(pdMS_TO_TICKS(35));
        }
    }

    matrix_clear();
    matrix_show();
}

// WS2812FX-inspired: Chase Rainbow White (mode 36 feel).
static void effect_chase_rainbow_white(void) {
    uint8_t n = matrix_get_size();
    if (n == 0) {
        return;
    }

    for (uint16_t frame = 0; frame < (uint16_t)(n * 10); frame++) {
        uint8_t white_pos = (uint8_t)(frame % n);
        for (uint8_t i = 0; i < n; i++) {
            uint8_t r = 0, g = 0, b = 0;
            uint8_t wheel_pos = (uint8_t)((i * 256 / n + frame * 6) & 0xFF);
            color_wheel(wheel_pos, &r, &g, &b);
            matrix_set_pixel(i, r, g, b);
        }

        matrix_set_pixel(white_pos, 255, 255, 255);
        matrix_show();
        vTaskDelay(pdMS_TO_TICKS(70));
    }

    matrix_clear();
    matrix_show();
}

// WS2812FX-inspired: Comet (mode 44 feel).
static void effect_comet(void) {
    uint8_t n = matrix_get_size();
    const uint8_t tail = 5;
    if (n == 0) {
        return;
    }

    for (int16_t head = 0; head < (int16_t)(n + tail); head++) {
        set_all_pixels(0, 0, 0);
        for (uint8_t t = 0; t < tail; t++) {
            int16_t idx = (int16_t)(head - t);
            if (idx >= 0 && idx < n) {
                uint8_t falloff = (uint8_t)(255 - (t * (220 / tail)));
                uint8_t r = (uint8_t)((50 * falloff) / 255);
                uint8_t g = (uint8_t)((150 * falloff) / 255);
                uint8_t b = falloff;
                matrix_set_pixel((uint8_t)idx, r, g, b);
            }
        }
        matrix_show();
        vTaskDelay(pdMS_TO_TICKS(55));
    }

    for (int16_t head = (int16_t)(n - 1); head >= -((int16_t)tail); head--) {
        set_all_pixels(0, 0, 0);
        for (uint8_t t = 0; t < tail; t++) {
            int16_t idx = (int16_t)(head + t);
            if (idx >= 0 && idx < n) {
                uint8_t falloff = (uint8_t)(255 - (t * (220 / tail)));
                uint8_t r = (uint8_t)((50 * falloff) / 255);
                uint8_t g = (uint8_t)((150 * falloff) / 255);
                uint8_t b = falloff;
                matrix_set_pixel((uint8_t)idx, r, g, b);
            }
        }
        matrix_show();
        vTaskDelay(pdMS_TO_TICKS(55));
    }

    matrix_clear();
    matrix_show();
}

// Full "execute" sequence for a selected digit.
static void run_digit_sequence(uint8_t digit) {
    // Map selected digits to richer effects.
    // - 7: Chase Flash Random
    // - 8: Chase Rainbow White
    // - 9: Comet
    if (digit == 7) {
        effect_chase_flash_random();
        return;
    }
    if (digit == 8) {
        effect_chase_rainbow_white();
        return;
    }
    if (digit == 9) {
        effect_comet();
        return;
    }

    uint8_t r = 0, g = 0, b = 0;
    color_id_to_rgb(digit, &r, &g, &b);

    if (digit == 0) {
        matrix_clear();
        matrix_show();
        return;
    }

    uint8_t flashes = ((digit - 1) % 3) + 1;
    for (uint8_t i = 0; i < flashes; i++) {
        flash_color(r, g, b, 180);
        vTaskDelay(pdMS_TO_TICKS(120));
    }
}

/*
 * Worker task model:
 * - Runs on Core 0.
 * - Owns all blocking LED timing (vTaskDelay).
 * - Prevents UI callback thread from stalling.
 */
static void command_action_task(void *arg) {
    (void)arg;
    led_action_t action;
    ESP_LOGI(TAG, "Command action task started");
    while (1) {
        if (xQueueReceive(s_action_queue, &action, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        // Brain can poll this via REG_STATUS while actions are in-flight.
        current_status = STATUS_BUSY;
        switch (action.type) {
            case ACTION_PREVIEW_DIGIT: {
                uint8_t r = 0, g = 0, b = 0;
                color_id_to_rgb(action.value, &r, &g, &b);
                if (action.value == 0) {
                    matrix_clear();
                    matrix_show();
                } else {
                    flash_color(r, g, b, 80);
                }
                break;
            }
            case ACTION_EXECUTE_DIGIT:
                run_digit_sequence(action.value);
                break;
            case ACTION_EXECUTE_COLOR_ID: {
                uint8_t r = 0, g = 0, b = 0;
                color_id_to_rgb(action.value, &r, &g, &b);
                flash_color(r, g, b, 300);
                break;
            }
            default:
                break;
        }
        // Preserve DATA_READY if submit event is pending for Brain to consume.
        current_status = s_pending_event_valid ? STATUS_DATA_READY : STATUS_READY;
    }
}

// Create queue + worker once at boot.
esp_err_t command_handler_init(void) {
    if (s_action_queue) {
        return ESP_OK;
    }

    s_action_queue = xQueueCreate(12, sizeof(led_action_t));
    if (!s_action_queue) {
        ESP_LOGE(TAG, "Failed to create command action queue");
        return ESP_FAIL;
    }

    BaseType_t ok = xTaskCreatePinnedToCore(
        command_action_task, "cmd_actions", 3072, NULL, 4, NULL, 0
    );
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create command action task");
        return ESP_FAIL;
    }

    return ESP_OK;
}

// Exposed for REG_STATUS register reads.
uint8_t command_handler_get_status(void) {
    return current_status;
}

// Enqueue short visual confirmation for local UI number taps.
bool command_handler_enqueue_preview(uint8_t digit) {
    if (!s_action_queue) {
        return false;
    }
    led_action_t action = {.type = ACTION_PREVIEW_DIGIT, .value = digit};
    return xQueueSend(s_action_queue, &action, 0) == pdTRUE;
}

// Enqueue full local execution (e.g., after SUBMIT press).
bool command_handler_enqueue_execute_digit(uint8_t digit) {
    if (!s_action_queue) {
        return false;
    }
    led_action_t action = {.type = ACTION_EXECUTE_DIGIT, .value = digit};
    return xQueueSend(s_action_queue, &action, 0) == pdTRUE;
}

bool command_handler_submit_selection(uint8_t digit) {
    if (digit > 9) {
        return false;
    }

    // If animation is actively running, reject and let UI prompt retry.
    if (current_status & STATUS_BUSY) {
        return false;
    }

    color_id = digit;
    s_pending_event_id = LED_FLASH_EVENT_SELECTION_SUBMIT;
    s_pending_event_value = digit;
    s_pending_event_valid = true;
    current_status = STATUS_DATA_READY;
    return true;
}

// CMD_GET_DATA payload currently reports selected color_id.
size_t get_data_payload(uint8_t *out, size_t max_len) {
    if (!s_pending_event_valid || max_len < 2) {
        return 0;
    }

    out[0] = s_pending_event_id;
    out[1] = s_pending_event_value;
    s_pending_event_valid = false;
    current_status = STATUS_READY;
    return 2;
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

        case CMD_GET_DATA:
            // Data is returned by i2c_comm.c via get_data_payload().
            ESP_LOGI(TAG, "  → GET_DATA");
            break;

        case CMD_SET_LED:
            // Two forms supported:
            // 1) len==2 : palette color_id
            // 2) len>=4 : raw RGB write
            if (len == 2) {
                color_id = buffer[1];
                color_id_to_rgb(color_id, &led_r, &led_g, &led_b);
                ESP_LOGI(TAG, "  → SET_COLOR ID=%d RGB(%d, %d, %d)",
                         color_id, led_r, led_g, led_b);
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
            // Brain-triggered execute path: queue action and return quickly.
            ESP_LOGI(TAG, "  → EXECUTE color_id=%d", color_id);
            if (s_action_queue) {
                led_action_t action = {.type = ACTION_EXECUTE_COLOR_ID, .value = color_id};
                if (xQueueSend(s_action_queue, &action, 0) != pdTRUE) {
                    ESP_LOGW(TAG, "Action queue full, dropping EXECUTE");
                }
            }
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
        // Human-readable periodic log to aid bring-up/demo debugging.
        ESP_LOGI(TAG, "Status: %s | LED: RGB(%d,%d,%d) | Brightness: %d%%",
                 (current_status & STATUS_READY) ? "READY" : "BUSY",
                 led_r, led_g, led_b,
                 (matrix_get_brightness() * 100) / 255);

        vTaskDelay(pdMS_TO_TICKS(10000));  // Every 10 seconds
    }
}
