#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "i2c_protocol.h"
#include "audio_speaker.h"
#include "led_matrix.h"
#include "command_handler.h"

#if defined(CONTROL_FLOW_TFT_UI_ENABLED)
#include "tft_ui.h"
#else
static inline void tft_ui_start(void) {}
static inline void tft_ui_trigger_execute(void) {}
static inline void tft_ui_set_idle(void) {}
#endif

extern void initArduino(void);

// I2C slave transport implemented in i2c_comm.c
extern esp_err_t i2c_slave_init(void);
extern void i2c_task(void *arg);

// Legacy LED status task (still useful for debug output)
extern void led_status_task(void *arg);

static const char *TAG = "BUTTON_BLOCK";

static uint8_t g_status_flags = STATUS_READY;

static struct {
    bool has_event;
    uint8_t event_id;
    uint8_t payload[8];
    size_t payload_len;
} g_pending_event;

static void publish_button_press_event(uint8_t pressed)
{
    g_pending_event.has_event = true;
    g_pending_event.event_id = BRAIN_BLOCK_EVENT_BUTTON_PRESS;
    g_pending_event.payload[0] = pressed ? 1 : 0;
    g_pending_event.payload_len = 1;
    g_status_flags |= STATUS_DATA_READY;
}

uint8_t button_block_get_status_flags(void)
{
    return g_status_flags;
}

// TFT/UI integration points:
// - call button_block_execute_from_ui() when the user taps "Execute" (true).
// - call button_block_pass_from_ui() when the user taps "Pass" (false).
void button_block_execute_from_ui(void)
{
    publish_button_press_event(1);
    speaker_beep_ok();
}

void button_block_pass_from_ui(void)
{
    publish_button_press_event(0);
    speaker_beep_ok();
}

void command_handle(i2c_command_t cmd,
                    const uint8_t *rx,
                    size_t rx_len,
                    uint8_t *tx,
                    size_t *tx_len)
{
    (void)rx;
    (void)rx_len;
    if (tx_len) {
        *tx_len = 0;
    }

    switch (cmd) {
        case CMD_PING:
            g_status_flags = STATUS_READY | (g_pending_event.has_event ? STATUS_DATA_READY : 0);
            break;

        case CMD_GET_STATUS:
            if (tx && tx_len) {
                tx[0] = g_status_flags;
                *tx_len = 1;
            }
            break;

        case CMD_GET_DATA:
            if (tx && tx_len && g_pending_event.has_event) {
                tx[0] = g_pending_event.event_id;
                if (g_pending_event.payload_len > 0) {
                    memcpy(&tx[1], g_pending_event.payload, g_pending_event.payload_len);
                }
                *tx_len = 1 + g_pending_event.payload_len;

                // Clear DATA_READY after Brain consumes the event.
                g_pending_event.has_event = false;
                g_pending_event.payload_len = 0;
                g_status_flags &= (uint8_t)~STATUS_DATA_READY;
            }
            break;

        case CMD_RESET:
            g_pending_event.has_event = false;
            g_pending_event.payload_len = 0;
            g_status_flags = STATUS_READY;
            tft_ui_set_idle();
            break;

        case CMD_EXECUTE:
            tft_ui_trigger_execute();
            break;
        default:
            break;
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "    BUTTON BLOCK BOOT");
    ESP_LOGI(TAG, "========================================");

    initArduino();

    esp_err_t ret = speaker_init();
    if (ret == ESP_OK) {
        speaker_beep_ok();
    }

    ret = led_matrix_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize LED matrix!");
        speaker_beep_error();
        return;
    }

    led_matrix_startup_animation();
    tft_ui_start();
    tft_ui_set_idle();

    ret = i2c_slave_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize I²C slave!");
        speaker_beep_error();
        return;
    }

    vTaskDelay(pdMS_TO_TICKS(500));
    ESP_LOGI(TAG, "Block ready and waiting for commands!\n");

    xTaskCreate(i2c_task, "i2c", 4096, NULL, 5, NULL);
    xTaskCreate(led_status_task, "led_status", 2048, NULL, 3, NULL);

    ESP_LOGI(TAG, "All tasks created successfully!");
}
