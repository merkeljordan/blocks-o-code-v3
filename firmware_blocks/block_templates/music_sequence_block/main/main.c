/*
 * main.c (Music Sequence Block)
 *
 * Runtime flow:
 * - app_main() initializes Arduino compatibility, speaker/audio, I2C, and UI
 * - tft_ui.c publishes browse/select actions
 * - execution_task() owns the selected-song/config-ready state
 * - Brain drives playback via CMD_EXECUTE
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
#include "driver/gpio.h"

#include "battery_monitor.h"
#include "i2c_protocol.h"
#include "music_leds.h"
#include "speaker.h"
#include "audio_speaker.h"
#include "../../../shared_components/led_matrix/led_matrix.h"
#include "status_strip.h"
#include "led_contract.h"
#include "tft_ui.h"
#include "startup_guard.h"

extern void initArduino(void);

esp_err_t i2c_slave_init(void);
void i2c_task(void *arg);

#define BLOCK_NAME "MUSIC_SEQ"

#if CONFIG_FREERTOS_UNICORE
#define EXEC_CORE_ID 0
#define I2C_CORE_ID 0
#else
#define EXEC_CORE_ID 1
#define I2C_CORE_ID 1
#endif

#define EXEC_TASK_STACK_SIZE 6144
#define I2C_TASK_STACK_SIZE 6144
#define STACK_MONITOR_TASK_STACK_SIZE 3072
#define STACK_MONITOR_PERIOD_MS 5000
#define STACK_WARN_LOW_WATERMARK_WORDS 256
#define STACK_MONITOR_VERBOSE 0

static const char *TAG = "TPL_MUSIC_SEQ";

#define STATUS_STRIP_GPIO      GPIO_NUM_13
#define STATUS_STRIP_LED_COUNT 30

static const status_strip_config_t kStatusStripConfig = {
    .gpio_num = STATUS_STRIP_GPIO,
    .led_count = STATUS_STRIP_LED_COUNT,
};

#define STACK_WARN_LOW_WATERMARK_WORDS 128
#define STACK_MONITOR_PERIOD_MS        5000
#define STACK_MONITOR_VERBOSE          0

static uint8_t g_selected_song = 0;
static bool g_config_valid = false;
static bool g_speaker_ready = false;
static bool g_leds_ready = false;
static uint8_t g_status_flags = STATUS_READY;
static TaskHandle_t g_exec_task_handle = NULL;
static TaskHandle_t g_i2c_task_handle = NULL;

static void render_status_strip(uint8_t status_flags)
{
    led_contract_rgb_t identity = led_contract_identity_color(BLOCK_TYPE_MUSIC_SEQ);
    led_contract_rgb_t color = led_contract_status_color(status_flags, identity);
    if (status_strip_ensure_ready(&kStatusStripConfig) != ESP_OK) {
        return;
    }
    status_strip_fill(color.r, color.g, color.b);
    status_strip_set_brightness(led_contract_status_brightness(status_flags));
    (void)status_strip_show();
}

static void set_status_flags(uint8_t status_flags)
{
    g_status_flags = status_flags;
    bool busy = (status_flags & STATUS_BUSY) != 0U;
    led_matrix_set_status_mirror(busy);
    if (!busy) {
        render_status_strip(g_status_flags);
    }
}

static void apply_startup_reset_state(void)
{
    g_selected_song = 0;
    g_config_valid = false;
    set_status_flags(STATUS_READY);

    if (g_speaker_ready) {
        (void)speaker_stop();
    }

    tft_ui_set_playback_state(&(music_playback_state_t) {
        .is_playing = false,
        .active_song_index = 0,
    });
    tft_ui_set_status_message("Pick a song and tap Play!");
    (void)status_strip_reset(&kStatusStripConfig);
    if (g_leds_ready) {
        music_leds_show_idle();
    } else {
        matrix_clear();
        matrix_show();
    }
}

static void stack_monitor_task(void *arg)
{
    (void)arg;
    while (1) {
        UBaseType_t exec_hwm = 0;
        UBaseType_t i2c_hwm = 0;

        if (g_exec_task_handle != NULL) {
            exec_hwm = uxTaskGetStackHighWaterMark(g_exec_task_handle);
        }
        if (g_i2c_task_handle != NULL) {
            i2c_hwm = uxTaskGetStackHighWaterMark(g_i2c_task_handle);
        }

        if ((exec_hwm > 0 && exec_hwm < STACK_WARN_LOW_WATERMARK_WORDS) ||
            (i2c_hwm > 0 && i2c_hwm < STACK_WARN_LOW_WATERMARK_WORDS)) {
            ESP_LOGW(TAG, "Low stack watermark: exec=%u words i2c=%u words",
                     (unsigned)exec_hwm, (unsigned)i2c_hwm);
#if STACK_MONITOR_VERBOSE
        } else {
            ESP_LOGI(TAG, "Stack watermark: exec=%u words i2c=%u words",
                     (unsigned)exec_hwm, (unsigned)i2c_hwm);
#endif
        }

        vTaskDelay(pdMS_TO_TICKS(STACK_MONITOR_PERIOD_MS));
    }
}

static void sync_selection_status_flag(void)
{
    if (g_config_valid) {
        g_status_flags |= STATUS_DATA_READY;
    } else {
        g_status_flags &= (uint8_t)~STATUS_DATA_READY;
    }

    if ((g_status_flags & (STATUS_BUSY | STATUS_ERROR)) == 0U) {
        g_status_flags |= STATUS_READY;
    } else {
        g_status_flags &= (uint8_t)~STATUS_READY;
    }
    render_status_strip(g_status_flags);
}

static void clear_busy_and_refresh_ready_state(void)
{
    g_status_flags &= (uint8_t)~STATUS_BUSY;
    led_matrix_set_status_mirror(false);
    sync_selection_status_flag();
}

uint8_t music_block_get_status_flags(void)
{
    uint8_t flags = g_status_flags & (STATUS_READY |
                                      STATUS_BUSY |
                                      STATUS_ERROR |
                                      STATUS_DATA_READY);

    if ((flags & (STATUS_BUSY | STATUS_ERROR)) == 0U) {
        flags |= STATUS_READY;
    } else {
        flags &= (uint8_t)~STATUS_READY;
    }

    return flags;
}

static void peripherals_init(void)
{
    esp_err_t err = speaker_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "speaker_init failed: %s", esp_err_to_name(err));
        g_speaker_ready = false;
        set_status_flags(STATUS_ERROR);
        return;
    }

    speaker_set_volume(30);
    g_speaker_ready = true;
    speaker_play_boot_sound();

    err = music_leds_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "music_leds_init failed: %s", esp_err_to_name(err));
        g_status_flags |= STATUS_ERROR;
        return;
    }

    music_leds_show_startup();
    music_leds_show_idle();
    g_leds_ready = true;
}

static void peripherals_error_feedback(void)
{
    if (g_speaker_ready) {
        speaker_beep_error();
    }
}

static void handle_play_note(const uint8_t *rx, size_t rx_len)
{
    static const uint32_t k_note_freq_hz[7] = {
        220U, /* A */
        247U, /* B */
        262U, /* C */
        294U, /* D */
        330U, /* E */
        349U, /* F */
        392U, /* G */
    };

    uint8_t note_id;
    esp_err_t err;

    if (!g_speaker_ready || rx == NULL || rx_len < 1U) {
        return;
    }

    note_id = rx[0];
    if (note_id >= 7U) {
        note_id = 0U;
    }

    set_status_flags(STATUS_BUSY);

    if (g_leds_ready) {
        music_leds_show_note_color(note_id, 0U);
    }
    err = speaker_play_tone(k_note_freq_hz[note_id], 400U);
    if (g_leds_ready) {
        music_leds_show_idle();
    }

    clear_busy_and_refresh_ready_state();
    if (err != ESP_OK) {
        set_status_flags(STATUS_ERROR);
    }
}

void command_handle(i2c_command_t cmd,
                    const uint8_t *rx,
                    size_t rx_len,
                    uint8_t *tx,
                    size_t *tx_len)
{
    if (tx_len != NULL) {
        *tx_len = 0;
    }

    if (status_strip_handle_matrix_command(TAG,
                                           &kStatusStripConfig,
                                           cmd,
                                           rx,
                                           rx_len)) {
        // Keep handling command below so matrix and strip stay mirrored.
    }
    if (status_strip_handle_runtime_broadcast(TAG, &kStatusStripConfig, BLOCK_TYPE_MUSIC_SEQ, cmd, rx, rx_len)) {
        return;
    }

    switch (cmd) {
        case CMD_PING:
            break;

        case CMD_PLAY_NOTE:
            handle_play_note(rx, rx_len);
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

        case CMD_GET_STATUS:
            if (tx != NULL && tx_len != NULL) {
                tx[0] = music_block_get_status_flags();
                *tx_len = 1;
            }
            break;

        case CMD_GET_DATA:
            if (tx != NULL && tx_len != NULL) {
                music_seq_payload_v1_t payload = {.song_id = g_selected_song};
                memcpy(tx, &payload, sizeof(payload));
                *tx_len = sizeof(payload);
            }
            break;

        case CMD_EXECUTE:
            if (!g_speaker_ready || !g_config_valid) {
                ESP_LOGW(TAG,
                         "CMD_EXECUTE ignored (speaker_ready=%d config_valid=%d)",
                         (int)g_speaker_ready, (int)g_config_valid);
                tft_ui_set_status_message("Select a song first.");
                break;
            }

            ESP_LOGI(TAG, "CMD_EXECUTE: playing song %u", (unsigned)g_selected_song);

            set_status_flags(STATUS_BUSY);

            tft_ui_set_playback_state(&(music_playback_state_t) {
                .is_playing = true,
                .active_song_index = g_selected_song,
            });
            tft_ui_set_status_message("Brain executing selected song...");

            {
                if (g_leds_ready) {
                    music_leds_start_song_pattern(g_selected_song);
                }
                esp_err_t err = speaker_play_song(g_selected_song);

                if (g_leds_ready) {
                    music_leds_stop_song_pattern();
                }
                tft_ui_set_playback_state(&(music_playback_state_t) {
                    .is_playing = false,
                    .active_song_index = g_selected_song,
                });

                clear_busy_and_refresh_ready_state();
                if (err != ESP_OK) {
                    set_status_flags(STATUS_ERROR);
                    tft_ui_set_status_message("Playback error.");
                    ESP_LOGE(TAG,
                             "CMD_EXECUTE: speaker_play_song failed err=%d",
                             (int)err);
                } else {
                    tft_ui_set_status_message("Playback complete. Ready.");
                }
            }
            break;

        case CMD_RESET:
            g_selected_song = 0;
            g_config_valid = false;
            g_status_flags = STATUS_READY;
            if (g_leds_ready) {
                music_leds_show_idle();
            }
            tft_ui_set_playback_state(&(music_playback_state_t) {
                .is_playing = false,
                .active_song_index = 0,
            });
            tft_ui_set_status_message("Pick a song and tap Play!");
            break;

        default:
            (void)rx;
            (void)rx_len;
            break;
    }
}

static void execution_task(void *arg)
{
    music_ui_action_t ui_action;

    (void)arg;
    ESP_LOGI(TAG, "execution_task running on core %d", xPortGetCoreID());

    while (1) {
        if (tft_ui_take_action(&ui_action, 250)) {
            switch (ui_action.type) {
                case MUSIC_UI_ACTION_SONG_CHANGED:
                    g_selected_song = ui_action.song_index;
                    g_config_valid = false;
                    set_status_flags(STATUS_READY);
                    ESP_LOGI(TAG, "Song changed to %u (%s)",
                             (unsigned)g_selected_song,
                             speaker_get_song_name(g_selected_song));
                    break;

                case MUSIC_UI_ACTION_SONG_SELECTED:
                    g_selected_song = ui_action.song_index;
                    g_config_valid = true;
                    set_status_flags(STATUS_DATA_READY);
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

void app_main(void)
{
    esp_err_t err;
    BaseType_t ok_exec;
    BaseType_t ok_i2c;

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "    %s BLOCK BOOT", BLOCK_NAME);
    ESP_LOGI(TAG, "========================================");

    startup_power_guard();
    initArduino();
    peripherals_init();
    if (!g_speaker_ready) {
        peripherals_error_feedback();
        return;
    }
    (void)speaker_play_boot_sound();

    err = i2c_slave_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_slave_init failed: %s", esp_err_to_name(err));
        set_status_flags(STATUS_ERROR);
        peripherals_error_feedback();
        return;
    }

    err = tft_ui_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "tft_ui_start failed: %s", esp_err_to_name(err));
        set_status_flags(STATUS_ERROR);
        peripherals_error_feedback();
        return;
    }

    apply_startup_reset_state();
    render_status_strip(g_status_flags);

    err = battery_monitor_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "battery_monitor_start failed: %s", esp_err_to_name(err));
        set_status_flags(STATUS_ERROR);
        peripherals_error_feedback();
        return;
    }

    ok_exec = xTaskCreatePinnedToCore(execution_task, "music_exec",
                                      EXEC_TASK_STACK_SIZE, NULL, 4,
                                      &g_exec_task_handle, EXEC_CORE_ID);
    ok_i2c = xTaskCreatePinnedToCore(i2c_task, "music_i2c",
                                     I2C_TASK_STACK_SIZE, NULL, 5,
                                     &g_i2c_task_handle, I2C_CORE_ID);

    if (ok_exec != pdPASS || ok_i2c != pdPASS) {
        ESP_LOGE(TAG, "Failed to create execution or I2C tasks");
        set_status_flags(STATUS_ERROR);
        peripherals_error_feedback();
        return;
    }

    (void)xTaskCreate(stack_monitor_task, "music_stack_mon",
                      STACK_MONITOR_TASK_STACK_SIZE, NULL, 1, NULL);

    ESP_LOGI(TAG, "%s block ready", BLOCK_NAME);
}
