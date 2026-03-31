#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_err.h"
#include "esp_log.h"
#include "driver/gpio.h"

#include "i2c_protocol.h"
#include "led_matrix.h"
#include "speaker.h"
#include "audio_speaker.h"
#include "battery_monitor.h"
#include "status_strip.h"
#include "led_contract.h"
#include "tft_ui.h"

// I2C glue is implemented in i2c_comm.c following the "new style" template.
extern esp_err_t i2c_slave_init(void);
extern void i2c_task(void *arg);

#define BLOCK_NAME         "NOTE"
#define BLOCK_I2C_ADDRESS  0x12
#define BLOCK_TYPE_NOTE_STR "NOTE"
#define NOTE_BLOCK_MAX_SEQUENCE_LEN  15
#define STATUS_STRIP_GPIO      GPIO_NUM_13
#define STATUS_STRIP_LED_COUNT 30

static const char *TAG = "NOTE_BLOCK";
#define STARTUP_GUARD_SETTLE_MS 120
static void startup_power_guard(void)
{
    static const gpio_num_t k_quiet_pins[] = { GPIO_NUM_13, GPIO_NUM_15, GPIO_NUM_18 };
    gpio_config_t io_cfg = {
        .pin_bit_mask = 0,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    io_cfg.pin_bit_mask = (1ULL << GPIO_NUM_5);
    (void)gpio_config(&io_cfg);
    (void)gpio_set_level(GPIO_NUM_5, 1);

    for (size_t i = 0; i < (sizeof(k_quiet_pins) / sizeof(k_quiet_pins[0])); ++i) {
        io_cfg.pin_bit_mask = (1ULL << k_quiet_pins[i]);
        (void)gpio_config(&io_cfg);
        (void)gpio_set_level(k_quiet_pins[i], 0);
    }

    vTaskDelay(pdMS_TO_TICKS(STARTUP_GUARD_SETTLE_MS));
}

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} note_color_t;

static const note_color_t k_note_colors[7] = {
    {255, 32, 32},   /* A */
    {255, 128, 0},   /* B */
    {255, 220, 0},   /* C */
    {32, 200, 64},   /* D */
    {0, 170, 255},   /* E */
    {255, 0, 0},     /* F */
    {200, 64, 255},  /* G */
};

static const status_strip_config_t kStatusStripConfig = {
    .gpio_num = STATUS_STRIP_GPIO,
    .led_count = STATUS_STRIP_LED_COUNT,
};

#define STACK_WARN_LOW_WATERMARK_WORDS 128
#define STACK_MONITOR_PERIOD_MS        5000
#define STACK_MONITOR_VERBOSE          0

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

static void render_status_strip(uint8_t status_flags)
{
    led_contract_rgb_t identity = led_contract_identity_color(BLOCK_TYPE_NOTE);
    led_contract_rgb_t color = led_contract_status_color(status_flags, identity);
    if (status_strip_ensure_ready(&kStatusStripConfig) != ESP_OK) {
        return;
    }
    status_strip_fill(color.r, color.g, color.b);
    status_strip_set_brightness(led_contract_status_brightness(status_flags));
    (void)status_strip_show();
}

static void show_boot_ready_matrix(void)
{
    led_contract_rgb_t identity = led_contract_identity_color(BLOCK_TYPE_NOTE);
    led_contract_rgb_t color = led_contract_status_color(STATUS_READY, identity);
    matrix_clear();
    matrix_show();
    matrix_fill(color.r, color.g, color.b);
    matrix_show();
}

static void set_status_flags(uint8_t status_flags)
{
    s_status_flags = status_flags;
    bool busy = (status_flags & STATUS_BUSY) != 0U;
    led_matrix_set_status_mirror(busy);
    if (!busy) {
        render_status_strip(status_flags);
    }
}

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
            bool has_pending_event = false;
            peripherals_show_running();
            play_note(cmd.note_id);
            portENTER_CRITICAL(&s_pending_event_spinlock);
            has_pending_event = s_pending_event_valid;
            portEXIT_CRITICAL(&s_pending_event_spinlock);
            set_status_flags(has_pending_event ? STATUS_DATA_READY : STATUS_READY);
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
extern void initArduino(void);

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
    led_contract_rgb_t identity = led_contract_identity_color(BLOCK_TYPE_NOTE);
    led_contract_rgb_t color = led_contract_status_color(STATUS_BUSY, identity);
    matrix_fill(color.r, color.g, color.b);
    matrix_show();
}

static void show_note_color(uint8_t note_id)
{
    uint8_t idx = (note_id < 7U) ? note_id : 0U;

    matrix_fill(k_note_colors[idx].r,
                k_note_colors[idx].g,
                k_note_colors[idx].b);
    matrix_show();
}

static void restore_idle_color(void)
{
    led_contract_rgb_t identity = led_contract_identity_color(BLOCK_TYPE_NOTE);
    led_contract_rgb_t color = led_contract_status_color(STATUS_READY, identity);
    matrix_fill(color.r, color.g, color.b);
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
    show_note_color(note_id);
    (void)speaker_play_tone(freq, 400U);
    restore_idle_color();
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
        set_status_flags(STATUS_DATA_READY);
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
        set_status_flags(STATUS_DATA_READY);
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

    (void)status_strip_handle_matrix_command(TAG, &kStatusStripConfig, cmd, rx, rx_len);

    switch (cmd) {
    case CMD_PING:
        // Do not clear STATUS_DATA_READY while an event is pending.
        if (!(s_pending_event_valid && (s_status_flags == STATUS_DATA_READY))) {
            set_status_flags(STATUS_READY);
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
                    set_status_flags(STATUS_BUSY);
                } else {
                    set_status_flags(STATUS_ERROR);
                }
            } else {
                set_status_flags(STATUS_ERROR);
            }
        } else {
            set_status_flags(STATUS_ERROR);
        }
        break;

    case CMD_SET_LED:
        if (rx && rx_len >= 1) {
            s_config.is_custom_sequence = false;
            s_config.note_id = (uint8_t)(rx[0] % 7U);
            s_config.seq_len = 0U;
            s_config_valid = true;
        }
        break;

    case CMD_MATRIX_FILL:
        if (rx_len >= 3U) {
            matrix_fill(rx[0], rx[1], rx[2]);
        }
        break;

    case CMD_MATRIX_CLEAR:
        matrix_clear();
        break;

    case CMD_MATRIX_BRIGHTNESS:
        if (rx_len >= 1U) {
            matrix_set_brightness(rx[0]);
        }
        break;

    case CMD_MATRIX_SHOW:
        matrix_show();
        break;

    case CMD_GET_DATA:
        // Only return payload when an event is pending.
        if (tx && tx_len && rx_len == 0) {
            bool consumed_event = false;
            portENTER_CRITICAL(&s_pending_event_spinlock);
            if (s_pending_event_valid) {
                memcpy(tx, s_pending_event_buf, s_pending_event_len);
                *tx_len = s_pending_event_len;
                s_pending_event_valid = false;
                s_pending_event_len = 0;
                consumed_event = true;
            }
            portEXIT_CRITICAL(&s_pending_event_spinlock);
            if (consumed_event) {
                set_status_flags(STATUS_READY);
            }
        }
        break;

    case CMD_EXECUTE:
        if (!config_is_valid()) {
            peripherals_error_feedback();
            set_status_flags(STATUS_ERROR);
            break;
        }
        set_status_flags(STATUS_BUSY);
        peripherals_show_running();
        if (s_config.is_custom_sequence && s_config.seq_len > 0) {
            for (uint8_t i = 0; i < s_config.seq_len; i++) {
                play_note(s_config.seq[i]);
            }
        } else {
            play_note(s_config.note_id);
        }
        set_status_flags(s_pending_event_valid ? STATUS_DATA_READY : STATUS_READY);
        break;

    case CMD_RESET:
        config_reset();
        set_status_flags(STATUS_READY);
        (void)status_strip_reset(&kStatusStripConfig);
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
    startup_power_guard();
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

    show_boot_ready_matrix();
    render_status_strip(s_status_flags);

    battery_monitor_start();

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
