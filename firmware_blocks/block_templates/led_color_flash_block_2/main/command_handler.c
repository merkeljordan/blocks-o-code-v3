#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "i2c_protocol.h"
#include "command_handler.h"
#include "led_matrix.h"
#include "audio_speaker.h"
#include "status_strip.h"
#include "led_contract.h"

static const char *TAG = "CMD_HANDLER";
#define STATUS_STRIP_GPIO      GPIO_NUM_13
#define STATUS_STRIP_LED_COUNT 30

static const status_strip_config_t kStatusStripConfig = {
    .gpio_num = STATUS_STRIP_GPIO,
    .led_count = STATUS_STRIP_LED_COUNT,
};

static uint8_t led_r = 0, led_g = 0, led_b = 0;
static uint8_t color_id = 0;
static uint8_t current_status = STATUS_READY;
static QueueHandle_t s_action_queue = NULL;
static int64_t s_busy_since_us = 0;
static volatile bool s_effect_active = false;
static volatile bool s_execute_inflight = false;

#define LED_FLASH_MIN_BUSY_HOLD_MS 35

typedef enum {
    ACTION_PREVIEW = 0,
    ACTION_EXECUTE = 1,
    ACTION_PLAY_NOTE = 2,
} led_action_type_t;

typedef struct {
    led_action_type_t type;
    uint8_t value;
} led_action_t;

static void refresh_status_strip(uint8_t status)
{
    led_contract_rgb_t identity = led_contract_identity_color(BLOCK_TYPE_LED_FLASH);
    led_contract_rgb_t color = led_contract_status_color(status, identity);
    if (status_strip_ensure_ready(&kStatusStripConfig) != ESP_OK) {
        return;
    }
    status_strip_fill(color.r, color.g, color.b);
    status_strip_set_brightness(led_contract_status_brightness(status));
    (void)status_strip_show();
}

static void show_status_matrix(uint8_t status)
{
    led_contract_rgb_t identity = led_contract_identity_color(BLOCK_TYPE_LED_FLASH);
    led_contract_rgb_t color = led_contract_status_color(status, identity);
    matrix_clear();
    matrix_show();
    matrix_fill(color.r, color.g, color.b);
    matrix_show();
}

static bool is_status_strip_command(i2c_command_t cmd)
{
    return (cmd == CMD_MATRIX_FILL ||
            cmd == CMD_MATRIX_CLEAR ||
            cmd == CMD_MATRIX_BRIGHTNESS ||
            cmd == CMD_MATRIX_SHOW);
}

static uint8_t normalize_status(uint8_t status)
{
    uint8_t flags = status & (STATUS_READY | STATUS_BUSY | STATUS_ERROR |
                              STATUS_DATA_READY | STATUS_IDLE);

    if ((flags & (STATUS_BUSY | STATUS_ERROR)) == 0U) {
        flags |= STATUS_READY;
    } else {
        flags &= (uint8_t)~STATUS_READY;
    }

    return flags;
}

static uint8_t compose_nonbusy_status(bool preserve_idle)
{
    uint8_t flags = 0U;

    if (preserve_idle) {
        flags |= STATUS_IDLE;
    }
    return normalize_status(flags);
}

static void set_current_status(uint8_t status)
{
    uint8_t old_status = current_status;
    uint8_t new_status = normalize_status(status);

    if ((old_status & STATUS_BUSY) == 0U && (new_status & STATUS_BUSY) != 0U) {
        s_busy_since_us = esp_timer_get_time();
        ESP_LOGI(TAG, "Status set to BUSY");
    } else if ((old_status & STATUS_BUSY) != 0U && (new_status & STATUS_BUSY) == 0U) {
        uint32_t elapsed_ms = 0U;
        uint32_t extra_hold_ms = 0U;
        if (s_busy_since_us > 0) {
            int64_t elapsed_us = esp_timer_get_time() - s_busy_since_us;
            if (elapsed_us < 0) {
                elapsed_us = 0;
            }
            elapsed_ms = (uint32_t)(elapsed_us / 1000LL);
            const int64_t min_hold_us = (int64_t)LED_FLASH_MIN_BUSY_HOLD_MS * 1000LL;
            if (elapsed_us < min_hold_us) {
                const int64_t remain_us = min_hold_us - elapsed_us;
                extra_hold_ms = (uint32_t)((remain_us + 999LL) / 1000LL);
                TickType_t delay_ticks = pdMS_TO_TICKS(extra_hold_ms);
                if (delay_ticks > 0) {
                    vTaskDelay(delay_ticks);
                }
                elapsed_ms += extra_hold_ms;
            }
        }
        s_busy_since_us = 0;
        ESP_LOGI(TAG, "Playback finished. elapsed=%lu ms extra_hold=%lu ms final=0x%02X",
                 (unsigned long)elapsed_ms, (unsigned long)extra_hold_ms, (unsigned)new_status);
    }
    if ((old_status & STATUS_IDLE) == 0U && (new_status & STATUS_IDLE) != 0U) {
        ESP_LOGI(TAG, "Status set to IDLE");
    }

    current_status = new_status;

    // BUSY means the local worker is actively rendering a preview/execute effect.
    // In that mode, the dedicated status strip should mirror matrix frames instead
    // of showing a solid idle/status color.
    if ((new_status & STATUS_BUSY) != 0U) {
        led_matrix_set_status_mirror(true);
        return;
    }

    // Any non-busy state gives the strip back to the simple status-color renderer.
    led_matrix_set_status_mirror(false);
    show_status_matrix(new_status);
    refresh_status_strip(new_status);
}

static void command_action_task(void *arg) {
    (void)arg;
    led_action_t action;
    ESP_LOGI(TAG, "Command action task started");
    while (1) {
        if (xQueueReceive(s_action_queue, &action, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        s_effect_active = true;
        led_matrix_set_status_mirror(true);
        led_matrix_set_lock(false);
        if (action.type == ACTION_PREVIEW && s_execute_inflight) {
            led_matrix_set_status_mirror(false);
            s_effect_active = false;
            continue;
        }
        bool track_busy = (action.type != ACTION_PREVIEW);
        if (track_busy) {
            set_current_status(STATUS_BUSY);
        }
        switch (action.type) {
            case ACTION_PREVIEW:
                led_flash_play_preview(action.value);
                break;
            case ACTION_EXECUTE:
                int64_t exec_start_us = esp_timer_get_time();
                led_flash_play_execute(action.value);
                ESP_LOGI(TAG, "Worker execute returned color=%u elapsed=%lu ms",
                         (unsigned)action.value,
                         (unsigned long)((esp_timer_get_time() - exec_start_us) / 1000LL));
                break;
            case ACTION_PLAY_NOTE: {
                static const uint32_t k_note_freq_hz[7] = {
                    220U, /* A */
                    247U, /* B (246.94) */
                    262U, /* C (261.63) */
                    294U, /* D (293.66) */
                    330U, /* E (329.63) */
                    349U, /* F (349.32) */
                    392U, /* G */
                };
                uint8_t note_id = action.value;
                if (note_id >= 7) {
                    note_id = 0;
                }
                (void)speaker_play_tone(k_note_freq_hz[note_id], 400U);
                break;
            }
            default:
                break;
        }
        if (track_busy) {
            set_current_status(compose_nonbusy_status(true));
            if (action.type == ACTION_EXECUTE) {
                s_execute_inflight = false;
            }
        } else {
            if (!s_execute_inflight) {
                set_current_status(compose_nonbusy_status(true));
            }
        }
        led_matrix_set_status_mirror(false);
        s_effect_active = false;
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

    show_status_matrix(STATUS_READY);
    refresh_status_strip(current_status);
    return ESP_OK;
}

// Exposed for REG_STATUS register reads.
uint8_t command_handler_get_status(void) {
    return normalize_status(current_status);
}

bool command_handler_enqueue_preview(uint8_t digit) {
    if (!s_action_queue) {
        return false;
    }
    led_action_t action = {.type = ACTION_PREVIEW, .value = digit};
    return xQueueSend(s_action_queue, &action, 0) == pdTRUE;
}

bool command_handler_enqueue_execute_digit(uint8_t digit) {
    if (!s_action_queue) {
        return false;
    }
    led_action_t action = {.type = ACTION_EXECUTE, .value = digit};
    led_action_t dropped = {0};
    s_execute_inflight = true;
    if (xQueueSend(s_action_queue, &action, pdMS_TO_TICKS(20)) == pdTRUE) {
        return true;
    }

    // If preview taps filled the queue, drop stale previews and prioritize execute.
    while (uxQueueMessagesWaiting(s_action_queue) > 0U) {
        if (xQueuePeek(s_action_queue, &dropped, 0) != pdTRUE || dropped.type != ACTION_PREVIEW) {
            break;
        }
        (void)xQueueReceive(s_action_queue, &dropped, 0);
    }
    if (xQueueSend(s_action_queue, &action, pdMS_TO_TICKS(20)) == pdTRUE) {
        return true;
    }

    s_execute_inflight = false;
    return false;
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
    set_current_status(compose_nonbusy_status((current_status & STATUS_IDLE) != 0U));
    return true;
}

// LED flash uses execute/status contract; no Brain-facing payload event.
size_t get_data_payload(uint8_t *out, size_t max_len) {
    (void)out;
    (void)max_len;
    return 0;
}

// ============================================================================
// COMMAND HANDLER
// ============================================================================
void handle_command(uint8_t *buffer, int len) {
    if (len < 1) {
        return;
    }

    uint8_t cmd = buffer[0];

    if ((i2c_command_t)cmd == CMD_RUNTIME_BROADCAST) {
        ESP_LOGD(TAG, "Command: %s (0x%02X), Length: %d bytes",
                 command_to_string(cmd), cmd, len);
    } else {
        ESP_LOGI(TAG, "Command: %s (0x%02X), Length: %d bytes",
                 command_to_string(cmd), cmd, len);
    }

    // While the local matrix effect owns the strip mirror, ignore Brain-driven
    // strip paint commands so the mirrored animation is not overwritten mid-run.
    if (((current_status & STATUS_BUSY) != 0U || s_effect_active) &&
        is_status_strip_command((i2c_command_t)cmd)) {
        ESP_LOGD(TAG, "Ignoring external status strip command 0x%02X while local effect is active", cmd);
        return;
    }

    // Supported MATRIX_* commands are first offered to the shared status-strip
    // renderer. This is how the Brain paints idle colors on the child strip.
    if (status_strip_handle_matrix_command(TAG,
                                           &kStatusStripConfig,
                                           (i2c_command_t)cmd,
                                           (len > 1) ? &buffer[1] : NULL,
                                           (len > 1) ? (size_t)(len - 1) : 0U)) {
        return;
    }
    if ((i2c_command_t)cmd == CMD_RUNTIME_BROADCAST &&
        ((current_status & STATUS_BUSY) != 0U || s_effect_active)) {
        ESP_LOGD(TAG, "Ignoring runtime broadcast while BUSY");
        return;
    }
    if (status_strip_handle_runtime_broadcast(TAG,
                                              &kStatusStripConfig,
                                              BLOCK_TYPE_LED_FLASH,
                                              (i2c_command_t)cmd,
                                              (len > 1) ? &buffer[1] : NULL,
                                              (len > 1) ? (size_t)(len - 1) : 0U)) {
        return;
    }

    switch (cmd) {
        case CMD_PING:
            ESP_LOGI(TAG, "  → PING");
            if ((current_status & STATUS_BUSY) == 0U) {
                set_current_status(compose_nonbusy_status((current_status & STATUS_IDLE) != 0U));
            }
            break;

        case CMD_GET_TYPE:
            ESP_LOGI(TAG, "  → GET_TYPE");
            break;

        case CMD_GET_DATA:
            // Data is returned by i2c_comm.c via get_data_payload().
            ESP_LOGI(TAG, "  → GET_DATA");
            break;

        case CMD_SET_LED:
            if (len == 2) {
                color_id = buffer[1] % 10;
                ESP_LOGI(TAG, "  → SET_COLOR ID=%d (%s)", color_id, led_pattern_name(color_id));
                // SET_LED is a configuration update, not a block-originated event.
                // Only assert DATA_READY when a submit event is actually pending.
                if ((current_status & STATUS_BUSY) == 0U) {
                    set_current_status(compose_nonbusy_status((current_status & STATUS_IDLE) != 0U));
                }
            } else if (len >= 4) {
                led_r = buffer[1];
                led_g = buffer[2];
                led_b = buffer[3];
                ESP_LOGI(TAG, "  → SET_LED RGB(%d, %d, %d)", led_r, led_g, led_b);
                // SET_LED is a configuration update, not a block-originated event.
                if ((current_status & STATUS_BUSY) == 0U) {
                    set_current_status(compose_nonbusy_status((current_status & STATUS_IDLE) != 0U));
                }
            }
            break;

        case CMD_PLAY_NOTE:
            if (len >= 2 && s_action_queue) {
                led_action_t action = {.type = ACTION_PLAY_NOTE, .value = buffer[1]};
                if (xQueueSend(s_action_queue, &action, 0) == pdTRUE) {
                    ESP_LOGI(TAG, "Queued PLAY_NOTE note=%u", (unsigned)buffer[1]);
                    set_current_status(STATUS_BUSY);
                } else {
                    ESP_LOGW(TAG, "Action queue full, dropping PLAY_NOTE");
                    set_current_status(STATUS_ERROR);
                }
            } else if (len >= 2) {
                set_current_status(STATUS_ERROR);
            }
            break;

        case CMD_MATRIX_FILL:
            if (len >= 4) {
                uint8_t r = buffer[1];
                uint8_t g = buffer[2];
                uint8_t b = buffer[3];
                ESP_LOGD(TAG, "  → FILL RGB(%d, %d, %d)", r, g, b);
                matrix_fill(r, g, b);
                matrix_show();
            }
            break;

        case CMD_MATRIX_CLEAR:
            ESP_LOGD(TAG, "  → CLEAR");
            matrix_clear();
            matrix_show();
            break;

        case CMD_MATRIX_BRIGHTNESS:
            if (len >= 2) {
                uint8_t brightness = buffer[1];
                ESP_LOGD(TAG, "  → BRIGHTNESS %d", brightness);
                matrix_set_brightness(brightness);
            }
            break;

        case CMD_MATRIX_SHOW:
            ESP_LOGD(TAG, "  → SHOW");
            matrix_show();
            break;

        case CMD_RESET:
            ESP_LOGI(TAG, "  → RESET");
            led_r = 0;
            led_g = 0;
            led_b = 0;
            color_id = 0;
            s_execute_inflight = false;
            s_effect_active = false;
            (void)status_strip_reset(&kStatusStripConfig);
            matrix_clear();
            matrix_show();
            set_current_status(STATUS_READY);
            break;

        case CMD_EXECUTE:
            ESP_LOGI(TAG, "  → EXECUTE color_id=%d (%s)", color_id, led_pattern_name(color_id));
            if (command_handler_enqueue_execute_digit(color_id)) {
                ESP_LOGI(TAG, "Queued EXECUTE color=%u", (unsigned)color_id);
            } else {
                ESP_LOGW(TAG, "Action queue full/unavailable, dropping EXECUTE");
                set_current_status(STATUS_ERROR);
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
        vTaskDelay(pdMS_TO_TICKS(10000));  // Every 10 seconds
    }
}


