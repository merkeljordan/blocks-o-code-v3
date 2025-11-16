#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_err.h"

// Forward declarations from other modules
extern esp_err_t oled_display_init(void);
extern void display_task(void *arg);
extern esp_err_t i2c_slave_init(void);
extern void i2c_task(void *arg);

#define MY_ADDRESS      0x09

static const char *TAG = "CHILD_2";

// ============================================================================
// MAIN - Only initialization and task creation
// ============================================================================
void app_main(void) {
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "    CHILD BLOCK 2 - OLED DISPLAY");
    ESP_LOGI(TAG, "    Address: 0x%02X", MY_ADDRESS);
    ESP_LOGI(TAG, "========================================");
    
    // Initialize OLED display
    esp_err_t ret = oled_display_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize display!");
        return;
    }
    
    // Initialize I²C slave
    ret = i2c_slave_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize I²C!");
        return;
    }
    
    vTaskDelay(pdMS_TO_TICKS(500));
    
    ESP_LOGI(TAG, "Ready and waiting for commands!\n");
    
    // Create tasks
    xTaskCreate(i2c_task, "i2c", 4096, NULL, 5, NULL);
    xTaskCreate(display_task, "display", 4096, NULL, 3, NULL);
    
    ESP_LOGI(TAG, "All tasks created successfully!");
}