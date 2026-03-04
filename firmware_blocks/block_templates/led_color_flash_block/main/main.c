#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "i2c_protocol.h"
#include "audio_speaker.h"
#include "tft_ui.h"

extern void initArduino(void);

// Forward declarations from other modules
extern esp_err_t led_matrix_init(void);
extern void led_matrix_startup_animation(void);
extern esp_err_t i2c_slave_init(void);
extern void i2c_task(void *arg);
extern void led_status_task(void *arg);

static const char *TAG = "LED_FLASH";

// ============================================================================
// MAIN - Only initialization and task creation
// ============================================================================
void app_main(void) {
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "    LED COLOR FLASH BLOCK BOOT");
    ESP_LOGI(TAG, "========================================");

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

    // Start TFT UI (intro screen -> Start button -> numpad sequence control).
    // This launches an internal GUI task and returns immediately.
    tft_ui_start();

   // Create tasks on core 0 (I2C/execution core)
BaseType_t ok_i2c = xTaskCreatePinnedToCore(i2c_task, "i2c", 4096, NULL, 5, NULL, 0);
BaseType_t ok_status = xTaskCreatePinnedToCore(led_status_task, "led_status", 2048, NULL, 3, NULL, 0);

if (ok_i2c != pdPASS || ok_status != pdPASS) {
    ESP_LOGE(TAG, "Failed to create one or more tasks");
    return;
}

    ESP_LOGI(TAG, "All tasks created successfully!");
}
