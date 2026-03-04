// Skeleton firmware for DELAY block template.
// Intentionally minimal: fill in modules as you implement the block.

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"

#include "i2c_protocol.h"
#include "audio_speaker.h"

extern void initArduino(void);

#define BLOCK_NAME            "DELAY"
#define BLOCK_I2C_ADDRESS     0x08  // TODO: set per board
#define BLOCK_TYPE            BLOCK_TYPE_DELAY

static const char *TAG = "TPL_DELAY";

// ============================================================================
// CONFIG (payload: delay_ms)
// ============================================================================
typedef struct {
    uint32_t delay_ms; // TODO: choose ms or seconds input
} block_config_t;

static block_config_t g_config;
static bool g_config_valid = false;

static void config_reset(void) { /* TODO */ }
static bool config_is_valid(void) { return g_config_valid; }
static size_t config_get_payload(uint8_t *out, size_t max_len) {
    (void)out;
    (void)max_len;
    // TODO: write delay_ms to payload
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
    ESP_LOGI(TAG, "==== %s block (skeleton) ====", BLOCK_NAME);

    config_reset();
    peripherals_init();
    peripherals_boot_feedback();

    (void)i2c_slave_init();
    xTaskCreate(i2c_task, "i2c", 4096, NULL, 5, NULL);

    peripherals_ok_feedback();
}
