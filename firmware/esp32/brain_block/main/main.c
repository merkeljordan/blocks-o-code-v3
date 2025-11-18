#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "brain_block.h"

static const char *TAG = "BRAIN";
QueueHandle_t demo_cmd_queue = NULL;

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

    // Create command queue for demo task
    demo_cmd_queue = xQueueCreate(4, sizeof(demo_cmd_t));

    // Create demo task
    xTaskCreate(demo_task, "demo", 4096, NULL, 5, NULL);

    // Create network client task
    start_network_client();
    
    ESP_LOGI(TAG, "Brain Block initialized!");
}
