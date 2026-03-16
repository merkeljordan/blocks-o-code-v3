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
#include "freertos/queue.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_log.h"
#include "sdkconfig.h"

#include "i2c_protocol.h"
#include "speaker.h"
#include "status_strip.h"
#include "tft_ui.h"

extern void initArduino(void);

#define BLOCK_NAME            "MUSIC_SEQ"

#if CONFIG_FREERTOS_UNICORE
#define EXEC_CORE_ID          0
#else
#define EXEC_CORE_ID          0
#endif

static const char *TAG = "TPL_MUSIC_SEQ";
#define STATUS_STRIP_GPIO      GPIO_NUM_13
#define STATUS_STRIP_LED_COUNT 16

static const status_strip_config_t kStatusStripConfig = {
    .gpio_num = STATUS_STRIP_GPIO,
    .led_count = STATUS_STRIP_LED_COUNT,
};

// Implemented in main/i2c_comm.c (kept separate so address/type live with the I2C slave transport).
esp_err_t i2c_slave_init(void);
void i2c_task(void *arg);

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

/* Called by i2c_comm.c to sync REG_STATUS with runtime state. */
uint8_t music_block_get_status_flags(void)
{
    /* Only expose defined status bits and ensure READY is set whenever
     * the block is not BUSY or in ERROR, so the Brain/app don't see
     * spurious "stuck" states from high bits or transient flags. */
    uint8_t flags = g_status_flags & (STATUS_READY |
                                      STATUS_BUSY |
                                      STATUS_ERROR |
                                      STATUS_DATA_READY);

    if ((flags & (STATUS_BUSY | STATUS_ERROR)) == 0) {
        flags |= STATUS_READY;
    }

    return flags;
}

/* Queue for execute requests from I2C (used by CMD_RESET to clear pending). */
typedef struct { uint8_t placeholder; } execute_request_t;
static QueueHandle_t g_execute_queue = NULL;

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
 * Called by i2c_comm.c i2c_task() when Brain sends a command.
 */
void command_handle(i2c_command_t cmd,
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

    if (status_strip_handle_matrix_command(TAG,
                                           &kStatusStripConfig,
                                           cmd,
                                           rx,
                                           rx_len)) {
        return;
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
            if (g_speaker_ready && g_config_valid) {
                ESP_LOGI(TAG, "CMD_EXECUTE: about to play song %u (status=0x%02X)",
                         (unsigned)g_selected_song, (unsigned)g_status_flags);
        
                g_status_flags |= STATUS_BUSY;
                g_status_flags &= (uint8_t)~STATUS_ERROR;
        
                esp_err_t err = speaker_play_song(g_selected_song);
        
                g_status_flags &= (uint8_t)~STATUS_BUSY;
                if (err != ESP_OK) {
                    g_status_flags |= STATUS_ERROR;
                    ESP_LOGE(TAG, "CMD_EXECUTE: speaker_play_song failed err=%d", (int)err);
                } else {
                    g_status_flags |= STATUS_DATA_READY;
                    ESP_LOGI(TAG, "CMD_EXECUTE: playback complete");
                }
            } else {
                ESP_LOGW(TAG, "CMD_EXECUTE: skipped (speaker_ready=%d config_valid=%d)",
                         (int)g_speaker_ready, (int)g_config_valid);
            }
            break;

        case CMD_RESET:
            // Reset this block's local selection state back to startup defaults.
            g_selected_song = 0;
            g_config_valid = false;
            g_status_flags = STATUS_READY;
            (void)status_strip_reset(&kStatusStripConfig);
            if (g_execute_queue != NULL) {
                (void)xQueueReset(g_execute_queue);
            }
            break;

        default:
            // Unknown command -> ignore safely.
            (void)rx;
            (void)rx_len;
            break;
    }
}

/* I2C transport is implemented in i2c_comm.c (i2c_slave_init, i2c_task). */

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

    g_execute_queue = xQueueCreate(1, sizeof(execute_request_t));
    if (g_execute_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create execute request queue");
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
}
