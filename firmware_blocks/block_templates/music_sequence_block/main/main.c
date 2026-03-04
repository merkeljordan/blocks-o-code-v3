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

extern void initArduino(void);

#define BLOCK_NAME            "MUSIC_SEQ"
#define BLOCK_I2C_ADDRESS     0x08
#define BLOCK_TYPE            BLOCK_TYPE_MUSIC_SEQ

#if CONFIG_FREERTOS_UNICORE
#define EXEC_CORE_ID          0
#else
#define EXEC_CORE_ID          0
#endif

static const char *TAG = "TPL_MUSIC_SEQ";

// ============================================================================
// State
// ============================================================================
static uint8_t g_selected_song = 0;
static bool    g_config_valid  = false;
static bool    g_speaker_ready = false;
static uint8_t g_status_flags  = STATUS_READY;

// ============================================================================
// Peripherals
// ============================================================================
static void peripherals_init(void)
{
    esp_err_t err = speaker_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "speaker_init failed: %s", esp_err_to_name(err));
        g_speaker_ready = false;
        g_status_flags |= STATUS_ERROR;
        return;
    }
    speaker_set_volume(100);
    g_speaker_ready = true;
}

static void peripherals_boot_feedback(void)
{
    if (g_speaker_ready) {
        speaker_play_boot_sound();
    }
}

static void peripherals_error_feedback(void)
{
    if (g_speaker_ready) {
        speaker_beep_error();
    }
}

// ============================================================================
// Command handler (stub)
// ============================================================================
static void command_handle(i2c_command_t cmd,
                           const uint8_t *rx, size_t rx_len,
                           uint8_t *tx, size_t *tx_len)
{
    if (tx_len) *tx_len = 0;

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
                music_seq_payload_v1_t payload = { .song_id = g_selected_song };
                memcpy(tx, &payload, sizeof(payload));
                *tx_len = sizeof(payload);
            }
            break;

        case CMD_EXECUTE:
            if (g_speaker_ready && g_config_valid) {
                speaker_play_song(g_selected_song);
            }
            break;

        case CMD_RESET:
            g_selected_song = 0;
            g_config_valid = false;
            g_status_flags = STATUS_READY;
            break;

        default:
            (void)rx;
            (void)rx_len;
            break;
    }
}

// ============================================================================
// I2C task (stub)
// ============================================================================
static esp_err_t i2c_slave_init(void)
{
    ESP_LOGI(TAG, "I2C stub init @0x%02X (type=%s)",
             BLOCK_I2C_ADDRESS, block_type_to_string(BLOCK_TYPE));
    return ESP_OK;
}

static void i2c_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "i2c_task running on core %d (stub)", xPortGetCoreID());
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// ============================================================================
// Execution task — processes UI actions
// ============================================================================
static void execution_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "execution_task running on core %d", xPortGetCoreID());

    music_ui_action_t ui_action;

    while (1) {
        if (tft_ui_take_action(&ui_action, 250)) {
            switch (ui_action.type) {
                case MUSIC_UI_ACTION_SONG_CHANGED:
                    g_selected_song = ui_action.song_index;
                    g_config_valid = false;
                    ESP_LOGI(TAG, "Song changed to %u (%s)",
                             (unsigned)g_selected_song,
                             speaker_get_song_name(g_selected_song));
                    break;

                case MUSIC_UI_ACTION_SONG_SELECTED:
                    g_selected_song = ui_action.song_index;
                    g_config_valid = true;
                    g_status_flags |= STATUS_DATA_READY;
                    g_status_flags &= ~STATUS_ERROR;
                    ESP_LOGI(TAG, "Song selected: %u (%s)",
                             (unsigned)g_selected_song,
                             speaker_get_song_name(g_selected_song));
                    break;

                default:
                    break;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// ============================================================================
// Main
// ============================================================================
void app_main(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "    %s block boot", BLOCK_NAME);
    ESP_LOGI(TAG, "========================================");

    initArduino();

    peripherals_init();
    peripherals_boot_feedback();

    esp_err_t err = i2c_slave_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C init failed: %s", esp_err_to_name(err));
        peripherals_error_feedback();
        return;
    }

    err = tft_ui_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "TFT UI start failed: %s", esp_err_to_name(err));
        peripherals_error_feedback();
        return;
    }

    xTaskCreatePinnedToCore(i2c_task, "i2c", 4096, NULL, 5, NULL, EXEC_CORE_ID);
    xTaskCreatePinnedToCore(execution_task, "exec", 4096, NULL, 4, NULL, EXEC_CORE_ID);

    ESP_LOGI(TAG, "Music sequence block ready");
}
