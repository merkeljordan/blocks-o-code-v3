#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "brain_block.h"
#include "device_registry.h"
#include "tft_ui.h"
#include "brain_event_handler.h"


static const char *TAG = "BRAIN";
QueueHandle_t demo_cmd_queue = NULL;

// Keep this off in normal runtime to avoid duplicate bus scans.
#define ENABLE_DEBUG_REGISTRY_SCAN_TASK 0

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

// Poll child blocks for DATA_READY and forward block-originated events.
static void block_event_poll_task(void *arg) {
    (void)arg;
    uint8_t status = 0;
    uint8_t payload[2] = {0};

    while (1) {
        const device_registry_t *registry = device_registry_get();
        for (int i = 0; i < DEVICE_REGISTRY_MAX_DEVICES; i++) {
            const device_entry_t *entry = &registry->devices[i];
            if (!entry->present || entry->type != BLOCK_TYPE_LED_FLASH) {
                continue;
            }

            if (i2c_read_reg(entry->address, REG_STATUS, &status, 1) != ESP_OK) {
                continue;
            }

            if ((status & STATUS_DATA_READY) == 0) {
                continue;
            }

            if (i2c_get_data(entry->address, payload, sizeof(payload)) == ESP_OK) {
                // payload[0] = event_id, payload[1] = event value (selection digit)
                brain_event_handle_block_event(entry->address, payload[0], &payload[1], 1);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(120));
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
    
    // Optional debug-only registry logger task.
#if ENABLE_DEBUG_REGISTRY_SCAN_TASK
    xTaskCreatePinnedToCore(registry_scan_task, "reg_scan", 4096, NULL, 4, NULL, 0);
#endif


    // Create network client task
    start_network_client();
    
    ESP_LOGI(TAG, "Brain Block initialized!");
}