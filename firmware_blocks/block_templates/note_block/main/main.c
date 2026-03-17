#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"

#include "i2c_protocol.h"
#include "led_matrix.h"
#include "speaker.h"
#include "tft_ui.h"

extern void initArduino(void);

// I2C glue is implemented in i2c_comm.c following the "new style" template.
extern esp_err_t i2c_slave_init(void);
extern void i2c_task(void *arg);

#define BLOCK_NAME         "NOTE"
#define BLOCK_I2C_ADDRESS  0x0F
#define BLOCK_TYPE_NOTE_STR "NOTE"

static const char *TAG = "NOTE_BLOCK";

// ============================================================================
// CONFIG (payload: note_id)
// ============================================================================
typedef struct {
    uint8_t note_id; // 0–6 demo notes mapping
} block_config_t;

static block_config_t s_config;
static bool s_config_valid = false;

// Block-originated event payload (Brain reads via CMD_GET_DATA when STATUS_DATA_READY is set).
#define NOTE_EVENT_SELECTION_SUBMIT 0x01
static bool s_pending_event_valid = false;
static uint8_t s_pending_event_id = 0;
static uint8_t s_pending_event_value = 0;

static void config_reset(void)
{
    memset(&s_config, 0, sizeof(s_config));
    s_config_valid = false;
    s_pending_event_valid = false;
}

static bool config_is_valid(void)
{
    return s_config_valid;
}

static size_t config_get_payload(uint8_t *out, size_t max_len)
{
    if (!out || max_len < 1) {
        return 0;
    }
    out[0] = s_config.note_id;
    return 1;
}

// ============================================================================
// STATUS FLAGS (exposed to i2c_comm register map)
// ============================================================================
static uint8_t s_status_flags = STATUS_READY;

uint8_t note_block_get_status_flags(void)
{
    return s_status_flags;
}

// ============================================================================
// PERIPHERALS
// ============================================================================
static void peripherals_init(void)
{
    initArduino();
    (void)speaker_init();
}

static void peripherals_boot_feedback(void)
{
    speaker_beep_ok();
}

static void peripherals_error_feedback(void)
{
    speaker_beep_error();
}

static void peripherals_ok_feedback(void)
{
    speaker_beep_ok();
}

static void peripherals_show_running(void)
{
    // Simple visual: briefly fill the matrix to indicate activity.
    matrix_fill(0, 0, 255);
    matrix_show();
}

// ============================================================================
// NOTE EXECUTION
// ============================================================================
static void play_note(uint8_t note_id)
{
    // note_id 0–6 maps to A–G per project convention.
    static const uint32_t k_note_freqs_hz[] = {
        220U, /* A */
        247U, /* B (246.94) */
        262U, /* C (261.63) */
        294U, /* D (293.66) */
        330U, /* E (329.63) */
        349U, /* F (349.32) */
        392U, /* G */
    };
    const uint32_t count = (uint32_t)(sizeof(k_note_freqs_hz) / sizeof(k_note_freqs_hz[0]));
    uint32_t freq = k_note_freqs_hz[note_id < count ? note_id : 0U];
    (void)speaker_play_tone(freq, 400U);
}

void note_block_preview_note(uint8_t note_id)
{
    play_note(note_id);
}

bool note_block_submit_selection(uint8_t note_id)
{
    // If something is actively running, reject selection submit.
    if ((s_status_flags & STATUS_BUSY) != 0U) {
        return false;
    }

    s_config.note_id = note_id;
    s_config_valid = true;

    // Publish selection-submit event for Brain orchestration.
    s_pending_event_id = NOTE_EVENT_SELECTION_SUBMIT;
    s_pending_event_value = note_id;
    s_pending_event_valid = true;
    s_status_flags = STATUS_DATA_READY;
    return true;
}

// ============================================================================
// COMMAND HANDLER (called from i2c_comm.c)
// ============================================================================
void command_handle(i2c_command_t cmd,
                    const uint8_t *rx,
                    size_t rx_len,
                    uint8_t *tx,
                    size_t *tx_len)
{
    if (tx_len) {
        *tx_len = 0;
    }

    switch (cmd) {
    case CMD_PING:
        s_status_flags = STATUS_READY;
        break;

    case CMD_GET_TYPE:
        if (tx && tx_len && rx_len == 0) {
            tx[0] = (uint8_t)BLOCK_TYPE_NOTE;
            *tx_len = 1;
        }
        break;

    case CMD_GET_STATUS:
        if (tx && tx_len && rx_len == 0) {
            tx[0] = s_status_flags;
            *tx_len = 1;
        }
        break;

    case CMD_PLAY_NOTE:
        if (rx && rx_len >= 1) {
            s_status_flags = STATUS_BUSY;
            peripherals_show_running();
            play_note(rx[0]);
            s_status_flags = s_pending_event_valid ? STATUS_DATA_READY : STATUS_READY;
        } else {
            s_status_flags = STATUS_ERROR;
        }
        break;

    case CMD_GET_DATA:
        // Only return payload when an event is pending.
        if (!s_pending_event_valid) {
            break;
        }
        if (tx && tx_len && rx_len == 0) {
            tx[0] = s_pending_event_id;
            tx[1] = s_pending_event_value;
            *tx_len = 2;
            s_pending_event_valid = false;
            s_status_flags = STATUS_READY;
        }
        break;

    case CMD_EXECUTE:
        if (!config_is_valid()) {
            peripherals_error_feedback();
            break;
        }
        s_status_flags = STATUS_BUSY;
        peripherals_show_running();
        play_note(s_config.note_id);
        s_status_flags = s_pending_event_valid ? STATUS_DATA_READY : STATUS_READY;
        break;

    case CMD_RESET:
        config_reset();
        s_status_flags = STATUS_READY;
        matrix_clear();
        matrix_show();
        break;

    default:
        // Other commands are ignored for now.
        break;
    }
}

// ============================================================================
// MAIN
// ============================================================================
void app_main(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "    NOTE BLOCK BOOT (%s @ 0x%02X)", BLOCK_TYPE_NOTE_STR, BLOCK_I2C_ADDRESS);
    ESP_LOGI(TAG, "========================================");

    config_reset();

    peripherals_init();
    peripherals_boot_feedback();

    esp_err_t ret = led_matrix_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize LED matrix!");
        peripherals_error_feedback();
        return;
    }

    led_matrix_startup_animation();

    ret = i2c_slave_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize I2C slave!");
        peripherals_error_feedback();
        return;
    }

    vTaskDelay(pdMS_TO_TICKS(200));
    ESP_LOGI(TAG, "Block ready and waiting for commands");

    // Start TFT UI (runs on Core 1) and keep I2C on Core 0.
    tft_ui_start();

    xTaskCreatePinnedToCore(i2c_task, "i2c", 4096, NULL, 5, NULL, 0);
    ESP_LOGI(TAG, "Tasks started");
}
