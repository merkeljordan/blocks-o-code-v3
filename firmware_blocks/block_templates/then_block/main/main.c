#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "i2c_protocol.h"
#include "audio_speaker.h"
#include "led_matrix.h"
#include "command_handler.h"

extern void initArduino(void);

// I2C slave transport implemented in i2c_comm.c
esp_err_t i2c_slave_init(void);
void i2c_task(void *arg);

#define BLOCK_NAME            "THEN"
#define BLOCK_I2C_ADDRESS     0x08  // TODO: set per board
#define BLOCK_TYPE            BLOCK_TYPE_THEN

static const char *TAG = "THEN_BLOCK";

// ============================================================================
// CONFIG (no payload for THEN)
// ============================================================================
typedef struct {
    uint8_t unused;
} block_config_t;

static block_config_t g_config;
static bool g_config_valid = true;

static void config_reset(void)
{
    // THEN block has no external payload; treat it as always-configured.
    g_config.unused = 0;
    g_config_valid = true;
}
static bool config_is_valid(void) { return g_config_valid; }
static size_t config_get_payload(uint8_t *out, size_t max_len) {
    (void)out;
    (void)max_len;
    return 0;
}

// ============================================================================
// PERIPHERALS
// ============================================================================
static void peripherals_init(void)
{
    initArduino();
    speaker_init();
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
    // Simple "running" indication: brief green flash on the matrix.
    matrix_fill(0, 64, 0);
    matrix_show();
    vTaskDelay(pdMS_TO_TICKS(120));
    matrix_clear();
    matrix_show();
}

// ============================================================================
// COMMAND HANDLER
// ============================================================================
static uint8_t g_status_flags = STATUS_READY;

uint8_t then_block_get_status_flags(void)
{
    return g_status_flags;
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
            // Keep it simple: acknowledge by staying READY and play a short beep.
            g_status_flags = STATUS_READY;
            peripherals_ok_feedback();
            break;

        case CMD_GET_STATUS:
            if (tx && tx_len) {
                tx[0] = g_status_flags;
                *tx_len = 1;
            }
            break;

        case CMD_GET_DATA:
            // THEN block has no payload; return zero-length response.
            break;

        case CMD_EXECUTE:
            if (!config_is_valid()) {
                g_status_flags = STATUS_ERROR;
                peripherals_error_feedback();
                break;
            }
            peripherals_show_running();
            g_status_flags = STATUS_READY;
            break;

        case CMD_RESET:
            config_reset();
            matrix_clear();
            matrix_show();
            g_status_flags = STATUS_READY;
            break;

        default:
            // Unknown commands are ignored safely.
            break;
    }
}

// ============================================================================
// MAIN
// ============================================================================
void app_main(void) {
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "    THEN BLOCK BOOT");
    ESP_LOGI(TAG, "========================================");

    // Initialize speaker early for boot/error beeps
    esp_err_t ret = speaker_init();
    if (ret == ESP_OK) {
        speaker_beep_ok();
    }

    // Initialize LED Matrix
    ret = led_matrix_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize LED matrix!");
        speaker_beep_error();
        return;
    }

    // Show startup animation

    // Initialize I²C slave
    ret = i2c_slave_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize I²C slave!");
        speaker_beep_error();
        return;
    }

    vTaskDelay(pdMS_TO_TICKS(500));
    ESP_LOGI(TAG, "Block ready and waiting for commands!\n");

    // Create tasks
    xTaskCreate(i2c_task, "i2c", 4096, NULL, 5, NULL);
    xTaskCreate(led_status_task, "led_status", 2048, NULL, 3, NULL);

    ESP_LOGI(TAG, "All tasks created successfully!");
}
