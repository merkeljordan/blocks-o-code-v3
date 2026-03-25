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

// I2C slave transport implemented in i2c_comm.c
extern esp_err_t i2c_slave_init(void);
extern void i2c_task(void *arg);

static const char *TAG = "BLOCK_TEMPLATE";
// Keep high-current peripherals quiescent while rails settle.
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

    // Amp enable is active-low on this hardware; drive high to keep it muted.
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
void app_main(void) {
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "    BLOCK TEMPLATE BOOT");
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
