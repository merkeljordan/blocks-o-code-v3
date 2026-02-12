#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "i2c_protocol.h"

// Forward declarations from other modules
extern esp_err_t led_matrix_init(void);
extern void led_matrix_startup_animation(void);
extern esp_err_t i2c_slave_init(void);
extern void i2c_task(void *arg);
extern void led_status_task(void *arg);

static const char *TAG = "CHILD_1";

#define MY_ADDRESS      0x08
#define MY_BLOCK_TYPE   BLOCK_TYPE_LED

// ============================================================================
// MAIN - Only initialization and task creation
// ============================================================================
void app_main(void) {
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "    CHILD BLOCK 1 - LED MATRIX");
    ESP_LOGI(TAG, "    Address: 0x%02X", MY_ADDRESS);
    ESP_LOGI(TAG, "    Type: %s", block_type_to_string(MY_BLOCK_TYPE));
    ESP_LOGI(TAG, "========================================");
    
    // Initialize LED Matrix
    esp_err_t ret = led_matrix_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize LED matrix!");
        return;
    }
    
    // Show startup animation
    led_matrix_startup_animation();
    
    // Initialize I²C slave
    ret = i2c_slave_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize I²C slave!");
        return;
    }
    
    vTaskDelay(pdMS_TO_TICKS(500));
    
    ESP_LOGI(TAG, "Child Block 1 ready and waiting for commands!\n");
    
    // Create tasks
    xTaskCreate(i2c_task, "i2c", 4096, NULL, 5, NULL);
    xTaskCreate(led_status_task, "led_status", 2048, NULL, 3, NULL);
    
    ESP_LOGI(TAG, "All tasks created successfully!");
}