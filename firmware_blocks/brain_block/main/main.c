#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "brain_block.h"
#include "device_registry.h"
#include "tft_ui.h"


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
    esp_log_level_set("XPT2046", ESP_LOG_WARN);

    
    // Initialize I²C Master
    ESP_ERROR_CHECK(i2c_master_init());
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // Initialize device registry
    device_registry_init();
    
    // Initial scan
    i2c_safe_scan();

    tft_ui_start();   // starts LVGL + GUI task (returns after creating tasks)
    ESP_LOGI(TAG, "tft_ui_start() returned");


    // Demo task is intentionally disabled while the event executor is active.
    // Keep queue allocation for compatibility with older modules that reference it.
    demo_cmd_queue = xQueueCreate(4, sizeof(demo_cmd_t));
    
    // Create registry scan task (scans every 1 second)
    xTaskCreatePinnedToCore(registry_scan_task, "reg_scan", 4096, NULL, 4, NULL, 0);


    // Create network client task
    start_network_client();
    
    ESP_LOGI(TAG, "Brain Block initialized!");
}