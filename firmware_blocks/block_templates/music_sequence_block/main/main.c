// Skeleton firmware for MUSIC_SEQUENCE block template.
// Intentionally minimal: fill in modules as you implement the block.

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "sdkconfig.h"

#include "i2c_protocol.h"
#include "speaker.h"
#include "tft_ui.h"

#define BLOCK_NAME            "MUSIC_SEQ"
#define BLOCK_I2C_ADDRESS     0x08  // TODO: set per board
#define BLOCK_TYPE            BLOCK_TYPE_MUSIC_SEQ

#if CONFIG_FREERTOS_UNICORE
#define EXEC_CORE_ID          0
#else
#define EXEC_CORE_ID          0   // core 1 is reserved for TFT/LVGL in tft_ui.c
#endif

static const char *TAG = "TPL_MUSIC_SEQ";

// ============================================================================
// CONFIG (payload: sequence_id)
// ============================================================================
typedef struct {
    uint8_t sequence_id; // TODO: pre-made sequence index
} block_config_t;

static block_config_t g_config;
static bool g_config_valid = false;
static bool g_speaker_ready = false;
static uint8_t g_status_flags = STATUS_READY;

static void config_reset(void) {
    g_config.sequence_id = MUSIC_PRESET_TWINKLE;
    g_config_valid = false;
}
static bool config_is_valid(void) { return g_config_valid; }
static size_t config_get_payload(uint8_t *out, size_t max_len) {
    if (out == NULL || max_len < sizeof(music_seq_payload_v1_t)) {
        return 0;
    }

    music_seq_payload_v1_t payload = {
        .sequence_id = g_config.sequence_id,
    };
    memcpy(out, &payload, sizeof(payload));
    return sizeof(payload);
}

// ============================================================================
// PERIPHERALS (STUBS)
// ============================================================================
static void peripherals_init(void) {
    esp_err_t err = speaker_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "speaker_init failed: %s", esp_err_to_name(err));
        g_speaker_ready = false;
        g_status_flags |= STATUS_ERROR;
        return;
    }

    speaker_set_volume(28);
    g_speaker_ready = true;
}

static void peripherals_boot_feedback(void) {
    if (g_speaker_ready) {
        speaker_beep_ok();
    }
}

static void peripherals_error_feedback(void) {
    if (g_speaker_ready) {
        speaker_beep_error();
    }
}

static void peripherals_ok_feedback(void) {
    if (g_speaker_ready) {
        (void)speaker_play_tone(1000, 60);
    }
}

static void peripherals_show_running(void) {
    if (g_speaker_ready) {
        (void)speaker_play_tone(880, 40);
    }
}

// ============================================================================
// COMMAND HANDLER (STUB)
// ============================================================================
static void command_handle(i2c_command_t cmd,
                           const uint8_t *rx,
                           size_t rx_len,
                           uint8_t *tx,
                           size_t *tx_len) {
    if (tx_len) {
        *tx_len = 0;
    }

    switch (cmd) {
        case CMD_PING:
            break;

        case CMD_GET_STATUS:
            if (tx && tx_len) {
                tx[0] = g_status_flags;
                *tx_len = 1;
            }
            break;

        case CMD_GET_DATA:
            if (tx && tx_len) {
                *tx_len = config_get_payload(tx, 16);
            }
            break;

        case CMD_EXECUTE:
            // TODO: trigger actual playback state machine / sequence execution.
            peripherals_show_running();
            break;

        case CMD_RESET:
            config_reset();
            g_status_flags = STATUS_READY;
            break;

        default:
            (void)rx;
            (void)rx_len;
            break;
    }
}

// ============================================================================
// I2C COMM (STUB)
// ============================================================================
static esp_err_t i2c_slave_init(void) {
    ESP_LOGI(TAG, "I2C stub init @0x%02X (type=%s)", BLOCK_I2C_ADDRESS, block_type_to_string(BLOCK_TYPE));
    return ESP_OK;
}

static void i2c_task(void *arg) {
    (void)arg;
    ESP_LOGI(TAG, "i2c_task running on core %d (stub)", xPortGetCoreID());

    while (1) {
        // TODO: replace with real I2C slave RX/TX loop (see led_color_flash_block/i2c_comm.c)
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// ============================================================================
// EXECUTION TASK (core0)
// ============================================================================
static void execution_task(void *arg) {
    (void)arg;
    ESP_LOGI(TAG, "execution_task running on core %d", xPortGetCoreID());

    music_ui_action_t ui_action;
    music_playback_state_t playback_state = {0};
    tft_ui_set_playback_state(&playback_state);

    while (1) {
        if (tft_ui_take_action(&ui_action, 250)) {
            g_config.sequence_id = ui_action.sequence_id;
            g_config_valid = (ui_action.config_valid != 0U);

            ESP_LOGI(TAG,
                     "UI action type=%u mode=%u seq=0x%02X tempo=%u valid=%u",
                     (unsigned)ui_action.type,
                     (unsigned)ui_action.mode,
                     (unsigned)ui_action.sequence_id,
                     (unsigned)ui_action.tempo_pct,
                     (unsigned)ui_action.config_valid);

            g_status_flags |= STATUS_DATA_READY;
            if (g_config_valid) {
                g_status_flags &= ~STATUS_ERROR;
                tft_ui_set_status_message("Config synced to core0. Waiting for Execute.");
            }
        }

        // TODO: when command handler receives CMD_EXECUTE, run speaker_play_preset() or custom sequence
        // and update playback_state with tft_ui_set_playback_state().
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// ============================================================================
// MAIN
// ============================================================================
void app_main(void) {
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "    %s block boot", BLOCK_NAME);
    ESP_LOGI(TAG, "    core0 = I2C/execution, core1 = TFT UI");
    ESP_LOGI(TAG, "========================================");

    config_reset();
    peripherals_init();
    peripherals_boot_feedback();

    esp_err_t err = i2c_slave_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize I2C stub: %s", esp_err_to_name(err));
        peripherals_error_feedback();
        return;
    }

    err = tft_ui_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start TFT UI: %s", esp_err_to_name(err));
        peripherals_error_feedback();
        return;
    }

    BaseType_t ok_i2c = xTaskCreatePinnedToCore(i2c_task, "i2c", 4096, NULL, 5, NULL, EXEC_CORE_ID);
    BaseType_t ok_exec = xTaskCreatePinnedToCore(execution_task, "exec", 4096, NULL, 4, NULL, EXEC_CORE_ID);

    if (ok_i2c != pdPASS || ok_exec != pdPASS) {
        ESP_LOGE(TAG, "Failed to create one or more core%d tasks", EXEC_CORE_ID);
        peripherals_error_feedback();
        return;
    }

    peripherals_ok_feedback();
    ESP_LOGI(TAG, "Music sequence block ready");
}
