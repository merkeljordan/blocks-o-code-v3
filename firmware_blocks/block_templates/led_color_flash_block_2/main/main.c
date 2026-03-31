#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "driver/gpio.h"
#include "i2c_protocol.h"
#include "audio_speaker.h"
#include "battery_monitor.h"
#include "tft_ui.h"
#include "command_handler.h"

extern void initArduino(void);

// Forward declarations from other modules
extern esp_err_t led_matrix_init(void);
extern esp_err_t i2c_slave_init(void);
extern void i2c_task(void *arg);
extern void led_status_task(void *arg);

static const char *TAG = "LED_FLASH";
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

// ============================================================================
// MAIN - Only initialization and task creation
// ============================================================================
/*
 * Boot sequence overview:
 * 1) Bring up local peripherals (speaker + matrix).
 * 2) Bring up I2C slave interface so Brain can discover/control this block.
 * 3) Bring up command handler queue/task for non-blocking action execution.
 * 4) Start TFT UI task.
 * 5) Start background tasks pinned to Core 0.
 *
 * Core split used in this project:
 * - Core 1: LVGL UI task (created inside tft_ui_start()).
 * - Core 0: I2C task + command/action worker + status task.
 */
void app_main(void) {
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "    LED COLOR FLASH BLOCK BOOT");
    ESP_LOGI(TAG, "========================================");

    startup_power_guard();

    initArduino();

    esp_err_t ret = speaker_init();
    if (ret == ESP_OK) {
        speaker_play_boot_sound();
    }

    // Initialize LED Matrix
    ret = led_matrix_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize LED matrix!");
        speaker_beep_error();
        return;
    }

    // I2C slave is the Brain-facing interface for this child block.
    ret = i2c_slave_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize I²C slave!");
        speaker_beep_error();
        return;
    }

    vTaskDelay(pdMS_TO_TICKS(500));
    ESP_LOGI(TAG, "Block ready and waiting for commands!\n");

    // Initializes action queue + worker task that executes LED actions off the UI thread.
    ret = command_handler_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize command handler!");
        speaker_beep_error();
        return;
    }

    // Start TFT UI (intro screen -> Start button -> numpad sequence control).
    // This launches an internal GUI task and returns immediately.
    ret = battery_monitor_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start battery monitor!");
        speaker_beep_error();
        return;
    }

    tft_ui_start();

    // Keep non-UI tasks on Core 0 so TFT interactions on Core 1 remain smooth.
    xTaskCreatePinnedToCore(i2c_task, "i2c", 4096, NULL, 5, NULL, 0);
    xTaskCreatePinnedToCore(led_status_task, "led_status", 2048, NULL, 3, NULL, 0);

    ESP_LOGI(TAG, "All tasks created successfully!");
}
