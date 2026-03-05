/*
 * main.c (Music Sequence Block)
 *
 * High-level runtime flow:
 * - app_main() starts audio + UI + worker tasks
 * - tft_ui.c sends UI actions through s_action_queue
 * - execution_task() consumes those actions and updates block state
 * - command_handle(CMD_EXECUTE) plays the selected song
 *
 * Who calls what (audio path):
 * - UI Play button -> preview_task() -> speaker_play_song()
 * - Brain CMD_EXECUTE -> command_handle() -> speaker_play_song()
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_log.h"
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

/* ========================================================================== */
/* Runtime state                                                               */
/* ========================================================================== */
/* Selected song index shown/confirmed in UI. */
static uint8_t g_selected_song = 0;
/* True only after kid taps Select in the UI. */
static bool    g_config_valid  = false;
/* Audio backend init state. */
static bool    g_speaker_ready = false;
/* STATUS_* bitfield exposed over protocol. */
static uint8_t g_status_flags  = STATUS_READY;

/* ========================================================================== */
/* Peripheral helpers                                                          */
/* ========================================================================== */
static void peripherals_init(void)
{
    // Called by: app_main()
    // Calls: speaker_init(), speaker_set_volume()
    esp_err_t err = speaker_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "speaker_init failed: %s", esp_err_to_name(err));
        g_speaker_ready = false;
        g_status_flags |= STATUS_ERROR;
        return;
    }

    /* Volume now feeds real gain scaling in WAV/tone playback backend. */
    speaker_set_volume(30);
    g_speaker_ready = true;
}

static void peripherals_boot_feedback(void)
{
    // Called by: app_main() after successful init.
    if (g_speaker_ready) {
        speaker_play_boot_sound();
    }
}

static void peripherals_error_feedback(void)
{
    // Called by: app_main() when init paths fail.
    if (g_speaker_ready) {
        speaker_beep_error();
    }
}

/* ========================================================================== */
/* Protocol command handler                                                    */
/* ========================================================================== */
/*
 * Note: this command handler is ready to be called by an I2C RX task.
 * In this template revision, i2c_task() below is still a transport stub.
 */
static void command_handle(i2c_command_t cmd,
                           const uint8_t *rx, size_t rx_len,
                           uint8_t *tx, size_t *tx_len)
{
    // Called by: future real I2C receive handler (currently not wired).
    // Calls: speaker_play_song() on CMD_EXECUTE when config is valid.
    //
    // How to read this function:
    // - `cmd` is the action Brain asked this block to perform.
    // - `rx/rx_len` is optional command payload from Brain.
    // - `tx/tx_len` is optional response payload back to Brain.
    //
    // Example:
    // Brain sends CMD_GET_DATA -> we return `music_seq_payload_v1_t` with song_id.
    // Brain sends CMD_EXECUTE  -> we play selected song if config is valid.
    if (tx_len) {
        *tx_len = 0;
    }

    switch (cmd) {
        case CMD_PING:
            break;

        case CMD_GET_STATUS:
            // Brain can poll this to know READY / ERROR / DATA_READY state.
            if (tx && tx_len) {
                tx[0] = g_status_flags;
                *tx_len = 1;
            }
            break;

        case CMD_GET_DATA:
            // Return selected song index so Brain/app knows current child config.
            if (tx && tx_len) {
                music_seq_payload_v1_t payload = {.song_id = g_selected_song};
                memcpy(tx, &payload, sizeof(payload));
                *tx_len = sizeof(payload);
            }
            break;

        case CMD_EXECUTE:
            /* Brain-side Play should eventually reach this branch via I2C. */
            if (g_speaker_ready && g_config_valid) {
                // Playback path triggered by Brain.
                speaker_play_song(g_selected_song);
            }
            break;

        case CMD_RESET:
            // Reset this block's local selection state back to startup defaults.
            g_selected_song = 0;
            g_config_valid = false;
            g_status_flags = STATUS_READY;
            break;

        default:
            // Unknown command -> ignore safely.
            (void)rx;
            (void)rx_len;
            break;
    }
}

/* ========================================================================== */
/* I2C transport (currently stubbed in this template)                          */
/* ========================================================================== */
static esp_err_t i2c_slave_init(void)
{
    // Called by: app_main() during startup.
    //
    // Current behavior:
    // - just logs and returns OK (stub).
    //
    // Future behavior (when enabling Brain control):
    // 1) Configure I2C in SLAVE mode using BLOCK_I2C_ADDRESS.
    // 2) Install i2c driver buffers.
    // 3) Return ESP_OK only when hardware init is successful.
    ESP_LOGI(TAG, "I2C stub init @0x%02X (type=%s)",
             BLOCK_I2C_ADDRESS, block_type_to_string(BLOCK_TYPE));
    return ESP_OK;
}

static void i2c_task(void *arg)
{
    // Called by: FreeRTOS task creation in app_main().
    (void)arg;
    ESP_LOGI(TAG, "i2c_task running on core %d (stub)", xPortGetCoreID());

    /*
     * TODO: Replace this stub with real slave transaction handling.
     *
     * Expected runtime flow once implemented:
     *   A) Read inbound bytes from Brain.
     *   B) If request is "register read" (WHOAMI/STATUS/etc), return register value.
     *   C) Else parse first byte as i2c_command_t.
     *   D) Call command_handle(cmd, payload, ...).
     *   E) If command_handle produced response bytes, write them back.
     *
     * Important:
     * - command_handle() already contains your execute logic.
     * - this task is just the transport bridge that delivers Brain commands to it.
     */
    while (1) {
        // Idle loop placeholder until real transport is implemented.
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

/* ========================================================================== */
/* Execution task: consumes UI actions                                         */
/* ========================================================================== */
static void execution_task(void *arg)
{
    // Called by: FreeRTOS task creation in app_main().
    // Calls: speaker_get_song_name() for logs.
    //
    // This task handles LOCAL UI state only (song browsing/selection),
    // not Brain I2C transport. Think of it as "child UI state manager".
    (void)arg;
    ESP_LOGI(TAG, "execution_task running on core %d", xPortGetCoreID());

    music_ui_action_t ui_action;

    while (1) {
        // Wait for next UI action from tft_ui.c.
        if (tft_ui_take_action(&ui_action, 250)) {
            switch (ui_action.type) {
                case MUSIC_UI_ACTION_SONG_CHANGED:
                    // Song browsing changed; keep config invalid until Select is tapped.
                    g_selected_song = ui_action.song_index;
                    g_config_valid = false;
                    ESP_LOGI(TAG, "Song changed to %u (%s)",
                             (unsigned)g_selected_song,
                             speaker_get_song_name(g_selected_song));
                    break;

                case MUSIC_UI_ACTION_SONG_SELECTED:
                    // Song confirmed; mark payload/status as ready for Brain execute.
                    // Brain can now poll STATUS_DATA_READY before sending CMD_EXECUTE.
                    g_selected_song = ui_action.song_index;
                    g_config_valid = true;
                    g_status_flags |= STATUS_DATA_READY;
                    g_status_flags &= (uint8_t)~STATUS_ERROR;
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

/* ========================================================================== */
/* app_main                                                                     */
/* ========================================================================== */
void app_main(void)
{
    // Entry point called by ESP-IDF runtime.
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

    /*
     * Local preview path is active now.
     * Brain-triggered execute path becomes active once I2C transport is implemented.
     */
    ESP_LOGI(TAG, "Music sequence block ready");

    /* Avoid unused warning if command_handle is not yet wired by transport code. */
    (void)command_handle;
}
