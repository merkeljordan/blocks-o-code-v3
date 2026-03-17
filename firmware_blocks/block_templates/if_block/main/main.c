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

#define BLOCK_NAME            "IF"
#define BLOCK_I2C_ADDRESS     0x08  // TODO: set per board
#define BLOCK_TYPE            BLOCK_TYPE_IF

static const char *TAG = "IF_BLOCK";

// ============================================================================
// CONFIG (no payload for IF)
// ============================================================================
typedef struct {
    uint8_t unused;
} block_config_t;

static block_config_t g_config;
static bool g_config_valid = true;

static void config_reset(void) { /* TODO */ }
static bool config_is_valid(void) { return g_config_valid; }
static size_t config_get_payload(uint8_t *out, size_t max_len) {
    (void)out;
    (void)max_len;
    return 0;
}

// ============================================================================
// PERIPHERALS (STUBS)
// ============================================================================
static void peripherals_init(void) {
    initArduino();
    speaker_init();
}
static void peripherals_boot_feedback(void) { speaker_play_boot_sound(); }
static void peripherals_error_feedback(void) { speaker_beep_error(); }
static void peripherals_ok_feedback(void) { speaker_beep_ok(); }
static void peripherals_show_running(void) { /* TODO */ }

// ============================================================================
// COMMAND HANDLER (STUB)
// ============================================================================
static uint8_t g_status_flags = STATUS_READY;

static void command_handle(i2c_command_t cmd,
                           const uint8_t *rx,
                           size_t rx_len,
                           uint8_t *tx,
                           size_t *tx_len) {
    (void)cmd;
    (void)rx;
    (void)rx_len;
    (void)tx;
    (void)tx_len;
    // TODO: implement CMD_* handling per FRAMEWORK.md
}

// ============================================================================
// I2C COMM (STUB)
// ============================================================================
static esp_err_t i2c_slave_init(void) { return ESP_OK; }
static void i2c_task(void *arg) { (void)arg; vTaskDelay(pdMS_TO_TICKS(1000)); }

// ============================================================================
// MAIN
// ============================================================================
void app_main(void) {
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "    IF BLOCK BOOT");
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
