// Skeleton firmware for LED_COLOR_FLASH block template.
// Intentionally minimal: fill in modules as you implement the block.

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"

#include "i2c_protocol.h"

#define BLOCK_NAME            "LED_FLASH"
#define BLOCK_I2C_ADDRESS     0x08  // TODO: set per board
#define BLOCK_TYPE            BLOCK_TYPE_LED_FLASH

static const char *TAG = "TPL_LED_FLASH";

// ============================================================================
// CONFIG (payload: color_id)
// ============================================================================
typedef struct {
    uint8_t color_id; // TODO: map numpad to color
} block_config_t;

static block_config_t g_config;
static bool g_config_valid = false;

static void config_reset(void) { /* TODO */ }
static bool config_is_valid(void) { return g_config_valid; }
static size_t config_get_payload(uint8_t *out, size_t max_len) {
    (void)out;
    (void)max_len;
    // TODO: write color_id to payload
    return 0;
}

// ============================================================================
// PERIPHERALS (STUBS)
// ============================================================================
static void peripherals_init(void) { /* TODO */ }
static void peripherals_boot_feedback(void) { /* TODO */ }
static void peripherals_error_feedback(void) { /* TODO */ }
static void peripherals_ok_feedback(void) { /* TODO */ }
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
