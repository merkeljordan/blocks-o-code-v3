#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"

#include "i2c_protocol.h"
#include "speaker.h"
#include "led_matrix.h"

extern void initArduino(void);

// I2C glue is implemented in i2c_comm.c following the "new style" template.
extern esp_err_t i2c_slave_init(void);
extern void i2c_task(void *arg);

#define BLOCK_NAME        "NOTE"
#define BLOCK_I2C_ADDRESS 0x0F
#define BLOCK_TYPE_NOTE_STR "NOTE"

static const char *TAG = "NOTE_BLOCK";

// ============================================================================
// CONFIG (payload: note_id)
// ============================================================================
typedef struct {
    uint8_t note_id; // 0–6 for demo notes (A–G or similar mapping)
} block_config_t;

static block_config_t s_config;
static bool s_config_valid = false;

static void config_reset(void)
{
    memset(&s_config, 0, sizeof(s_config));
    s_config_valid = false;
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
// STATUS FLAGS
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
    // Minimal mapping from note_id 0–6 to frequencies (approx. C4–B4).
    static const uint32_t k_note_freqs_hz[] = {
        262U, 294U, 330U, 349U, 392U, 440U, 494U
    };
    const uint32_t count = (uint32_t)(sizeof(k_note_freqs_hz) / sizeof(k_note_freqs_hz[0]));
    uint32_t freq = k_note_freqs_hz[note_id < count ? note_id : 0U];
    (void)speaker_play_tone(freq, 400U);
}

// ============================================================================
// COMMAND HANDLER
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
    case CMD_GET_STATUS:
        if (tx && tx_len && rx_len == 0) {
            tx[0] = s_status_flags;
            *tx_len = 1;
        }
        break;

    case CMD_PLAY_NOTE:
        if (rx && rx_len >= 1) {
            play_note(rx[0]);
        }
        break;

    case CMD_GET_DATA:
        if (tx && tx_len && rx_len == 0) {
            uint8_t payload[4];
            size_t written = config_get_payload(payload, sizeof(payload));
            if (written > 0) {
                memcpy(tx, payload, written);
                *tx_len = written;
            }
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
        s_status_flags = STATUS_READY | STATUS_DATA_READY;
        peripherals_ok_feedback();
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

    xTaskCreate(i2c_task, "i2c", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "Tasks started");
}
