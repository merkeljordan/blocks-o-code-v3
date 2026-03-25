#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "driver/gpio.h"
#include "i2c_protocol.h"
#include "audio_speaker.h"
#include "led_matrix.h"
#include "command_handler.h"

extern void initArduino(void);

#define BLOCK_NAME            "DELAY"
#define BLOCK_I2C_ADDRESS     0x0D  // I2C address for Delay block
#define BLOCK_TYPE            BLOCK_TYPE_DELAY

static const char *TAG = "DELAY_BLOCK";
#define STARTUP_GUARD_SETTLE_MS 120
static void startup_power_guard(void)
{
    static const gpio_num_t k_quiet_pins[] = { GPIO_NUM_13, GPIO_NUM_15, GPIO_NUM_18 };
    gpio_config_t io_cfg = {
        .pin_bit_mask = 0,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    io_cfg.pin_bit_mask = (1ULL << GPIO_NUM_5);
    (void)gpio_config(&io_cfg);
    (void)gpio_set_level(GPIO_NUM_5, 1);

    for (size_t i = 0; i < (sizeof(k_quiet_pins) / sizeof(k_quiet_pins[0])); ++i) {
        io_cfg.pin_bit_mask = (1ULL << k_quiet_pins[i]);
        (void)gpio_config(&io_cfg);
        (void)gpio_set_level(k_quiet_pins[i], 0);
    }

    vTaskDelay(pdMS_TO_TICKS(STARTUP_GUARD_SETTLE_MS));
}
#define STATUS_STRIP_GPIO      GPIO_NUM_13
#define STATUS_STRIP_LED_COUNT 16

static const status_strip_config_t kStatusStripConfig = {
    .gpio_num = STATUS_STRIP_GPIO,
    .led_count = STATUS_STRIP_LED_COUNT,
};

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
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "    DELAY BLOCK BOOT");
    ESP_LOGI(TAG, "========================================");

    startup_power_guard();

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
    led_matrix_startup_animation();

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
