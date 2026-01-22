#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "brain_block.h"
#include "device_registry.h"

static const char *TAG = "BRAIN";
QueueHandle_t demo_cmd_queue = NULL;

// ============================================================================
// REGISTRY SCAN TASK - Scans every 1 second and prints results
// ============================================================================
static void registry_scan_task(void *arg) {
    while (1) {
        device_registry_scan();
        device_registry_print();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// ============================================================================
// MAIN - Only initialization and task creation
// ============================================================================
void app_main(void) {
    ESP_LOGI(TAG, "=== BRAIN BLOCK ===");
    
    // Initialize I²C Master
    ESP_ERROR_CHECK(i2c_master_init());
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // Initialize device registry
    device_registry_init();
    
    // Initial scan
    i2c_safe_scan();

    // Create command queue for demo task
    demo_cmd_queue = xQueueCreate(4, sizeof(demo_cmd_t));

    // Create demo task
    xTaskCreate(demo_task, "demo", 4096, NULL, 5, NULL);
    
    // Create registry scan task (scans every 1 second)
    xTaskCreate(registry_scan_task, "reg_scan", 4096, NULL, 4, NULL);

    // Create network client task
    start_network_client();
    
    ESP_LOGI(TAG, "Brain Block initialized!");
}