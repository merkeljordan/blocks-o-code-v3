#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_err.h"
#include "esp_log.h"

#include "i2c_protocol.h"
#include "led_matrix.h"
#include "speaker.h"
#include "tft_ui.h"

extern void initArduino(void);
extern void speaker_play_boot_sound(void);

// I2C glue is implemented in i2c_comm.c following the "new style" template.
extern esp_err_t i2c_slave_init(void);
extern void i2c_task(void *arg);

#define BLOCK_NAME         "NOTE"
#define BLOCK_I2C_ADDRESS  0x0F
#define BLOCK_TYPE_NOTE_STR "NOTE"
#define NOTE_BLOCK_MAX_SEQUENCE_LEN  15

static const char *TAG = "NOTE_BLOCK";

// ============================================================================
// CONFIG (payload: single note or custom sequence)
// ============================================================================
typedef struct {
    bool    is_custom_sequence;
    uint8_t note_id; // single note, 0–6
    uint8_t seq_len; // 0..15 (max sequence length)
    uint8_t seq[NOTE_BLOCK_MAX_SEQUENCE_LEN];  // ordered notes 0..6 (max 15 notes)
} block_config_t;

static block_config_t s_config;
static bool s_config_valid = false;

// Spinlock protecting pending-event shared state accessed from multiple tasks/cores.
static portMUX_TYPE s_pending_event_spinlock = portMUX_INITIALIZER_UNLOCKED;

// ============================================================================
// ASYNCHRONOUS NOTE PLAYBACK
// ============================================================================
typedef struct {
    uint8_t note_id;
} playback_cmd_t;

static QueueHandle_t s_playback_queue = NULL;

// Forward declarations for helpers and status flags used by note_playback_task().
static void peripherals_show_running(void);
static void play_note(uint8_t note_id);
static uint8_t s_status_flags = STATUS_READY;

// Block-originated event payload (Brain reads via CMD_GET_DATA when STATUS_DATA_READY is set).
#define NOTE_EVENT_SELECTION_SUBMIT 0x01
static bool s_pending_event_valid = false;
// Payload frame format:
//   [event_id, count, note0..noteN] => (2 + N) bytes total.
// For max sequence length (N=15) => 17 bytes.
static uint8_t s_pending_event_buf[20] = {0};
static uint8_t s_pending_event_len = 0;

static void note_playback_task(void *arg)
{
    playback_cmd_t cmd;

    for (;;) {
        if (xQueueReceive(s_playback_queue, &cmd, portMAX_DELAY) == pdTRUE) {
            peripherals_show_running();
            play_note(cmd.note_id);
            portENTER_CRITICAL(&s_pending_event_spinlock);
            s_status_flags = s_pending_event_valid ? STATUS_DATA_READY : STATUS_READY;
            portEXIT_CRITICAL(&s_pending_event_spinlock);
        }
    }
}

uint8_t note_block_get_pending_event_len(void)
{
    uint8_t len = 0U;

    portENTER_CRITICAL(&s_pending_event_spinlock);
    if (s_pending_event_valid) {
        len = s_pending_event_len;
    }
    portEXIT_CRITICAL(&s_pending_event_spinlock);

    return len;
}

static void config_reset(void)
{
    portENTER_CRITICAL(&s_pending_event_spinlock);
    memset(&s_config, 0, sizeof(s_config));
    s_config_valid = false;
    s_pending_event_valid = false;
    s_pending_event_len = 0;
    portEXIT_CRITICAL(&s_pending_event_spinlock);
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
    speaker_play_boot_sound();
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
    // No matrix activity indicator.
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
    // Allow selection updates while BUSY.
    // If we receive a new selection during playback, we "queue" it via the
    // pending-event mechanism. The Brain will pick it up as soon as this
    // block finishes the current CMD_PLAY_NOTE / CMD_EXECUTE action.
    const bool was_busy = ((s_status_flags & STATUS_BUSY) != 0U);

    s_config.is_custom_sequence = false;
    s_config.note_id = note_id;
    s_config.seq_len = 0;
    s_config_valid = true;

    // Publish selection-submit event for Brain orchestration.
    // Use the standard frame: [event_id, count, note0..noteN].
    portENTER_CRITICAL(&s_pending_event_spinlock);
    s_pending_event_buf[0] = NOTE_EVENT_SELECTION_SUBMIT;
    s_pending_event_buf[1] = 1;       // single selected note
    s_pending_event_buf[2] = note_id; // note0
    s_pending_event_len = 3;
    s_pending_event_valid = true;
    portEXIT_CRITICAL(&s_pending_event_spinlock);
    if (!was_busy) {
        // Clear BUSY/ERROR and mark data as ready without clobbering other flags.
        s_status_flags &= ~(STATUS_BUSY | STATUS_ERROR);
        s_status_flags |= STATUS_DATA_READY;
    }
    return true;
}

bool note_block_submit_sequence(const uint8_t *notes, uint8_t count)
{
    // Allow sequence updates while BUSY (see note_block_submit_selection()).
    const bool was_busy = ((s_status_flags & STATUS_BUSY) != 0U);
    if (notes == NULL || count == 0) {
        return false;
    }

    // Cap to fit the Brain note event frame: [event_id, count, notes...]
    uint8_t capped = count;
    if (capped > (uint8_t)NOTE_BLOCK_MAX_SEQUENCE_LEN) {
        capped = NOTE_BLOCK_MAX_SEQUENCE_LEN;
    }

    s_config.is_custom_sequence = true;
    s_config.seq_len = capped;
    for (uint8_t i = 0; i < capped; i++) {
        s_config.seq[i] = (uint8_t)(notes[i] % 7);
    }
    s_config_valid = true;

    portENTER_CRITICAL(&s_pending_event_spinlock);
    s_pending_event_buf[0] = NOTE_EVENT_SELECTION_SUBMIT;
    s_pending_event_buf[1] = capped;
    for (uint8_t i = 0; i < capped; i++) {
        s_pending_event_buf[2 + i] = s_config.seq[i];
    }
    s_pending_event_len = (uint8_t)(2 + capped);
    s_pending_event_valid = true;
    portEXIT_CRITICAL(&s_pending_event_spinlock);
    if (!was_busy) {
        // Clear BUSY/ERROR and mark data as ready without clobbering other flags.
        s_status_flags &= ~(STATUS_BUSY | STATUS_ERROR);
        s_status_flags |= STATUS_DATA_READY;
    }
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
        // Do not clear STATUS_DATA_READY while an event is pending.
        if (!(s_pending_event_valid && (s_status_flags == STATUS_DATA_READY))) {
            s_status_flags = STATUS_READY;
        }
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
            if (s_playback_queue != NULL) {
                playback_cmd_t cmd_play = { .note_id = rx[0] };
                if (xQueueSendToBack(s_playback_queue, &cmd_play, 0) == pdPASS) {
                    s_status_flags = STATUS_BUSY;
                } else {
                    s_status_flags = STATUS_ERROR;
                }
            } else {
                s_status_flags = STATUS_ERROR;
            }
        } else {
            s_status_flags = STATUS_ERROR;
        }
        break;

    case CMD_GET_DATA:
        // Only return payload when an event is pending.
        if (tx && tx_len && rx_len == 0) {
            portENTER_CRITICAL(&s_pending_event_spinlock);
            if (s_pending_event_valid) {
                memcpy(tx, s_pending_event_buf, s_pending_event_len);
                *tx_len = s_pending_event_len;
                s_pending_event_valid = false;
                s_pending_event_len = 0;
                s_status_flags = STATUS_READY;
            }
            portEXIT_CRITICAL(&s_pending_event_spinlock);
        }
        break;

    case CMD_EXECUTE:
        if (!config_is_valid()) {
            peripherals_error_feedback();
            s_status_flags = STATUS_ERROR;
            break;
        }
        s_status_flags = STATUS_BUSY;
        peripherals_show_running();
        if (s_config.is_custom_sequence && s_config.seq_len > 0) {
            for (uint8_t i = 0; i < s_config.seq_len; i++) {
                play_note(s_config.seq[i]);
            }
        } else {
            play_note(s_config.note_id);
        }
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

    s_playback_queue = xQueueCreate(4, sizeof(playback_cmd_t));
    if (s_playback_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create playback queue!");
        peripherals_error_feedback();
        return;
    }

    vTaskDelay(pdMS_TO_TICKS(200));
    ESP_LOGI(TAG, "Block ready and waiting for commands");

    // Start TFT UI (runs on Core 1) and keep I2C on Core 0.
    tft_ui_start();

    xTaskCreatePinnedToCore(note_playback_task, "note_playback", 4096, NULL, 4, NULL, 1);
    xTaskCreatePinnedToCore(i2c_task, "i2c", 4096, NULL, 5, NULL, 0);
    ESP_LOGI(TAG, "Tasks started");
}
