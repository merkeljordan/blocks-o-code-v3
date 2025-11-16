#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

// Forward declarations from other modules
extern esp_err_t i2c_master_init(void);
extern void i2c_safe_scan(void);
extern void demo_task(void *arg);

static const char *TAG = "BRAIN";

// ============================================================================
// MAIN - Only initialization and task creation
// ============================================================================
void app_main(void) {
    ESP_LOGI(TAG, "=== BRAIN BLOCK ===");
    
    // Initialize I²C Master
    ESP_ERROR_CHECK(i2c_master_init());
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // Initial scan
    i2c_safe_scan();
    
    // Create demo task
    xTaskCreate(demo_task, "demo", 4096, NULL, 5, NULL);
    
    ESP_LOGI(TAG, "Brain Block initialized!");
}