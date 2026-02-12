#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "i2c_protocol.h"
#include "brain_block.h"
#include "device_registry.h"

// Forward declarations from i2c_comm.c
extern esp_err_t i2c_ping(uint8_t addr);
extern void i2c_safe_scan(void);
extern esp_err_t i2c_matrix_fill(uint8_t address, uint8_t r, uint8_t g, uint8_t b);
extern esp_err_t i2c_matrix_clear(uint8_t address);
extern esp_err_t i2c_matrix_set_brightness(uint8_t address, uint8_t brightness);
extern esp_err_t i2c_set_led(uint8_t address, uint8_t r, uint8_t g, uint8_t b);
extern esp_err_t i2c_execute(uint8_t address);

static const char *TAG = "DEMO";



// ============================================================================
// DEMO TASK - Cycles through colors on connected blocks
// ============================================================================
void demo_task(void *arg) {
    vTaskDelay(pdMS_TO_TICKS(2000)); // Wait for children
    demo_cmd_t cmd;
    while (1) {
        ESP_LOGI(TAG, "\n--- NEW CYCLE ---");                      
        // Scan to see what's connected
        i2c_safe_scan();
        // Wait indefinitely for a command
        if (xQueueReceive(demo_cmd_queue, &cmd, pdMS_TO_TICKS(10))) {

            if (cmd == CMD_START) {
                ESP_LOGI(TAG, "Demo START received");
                
                // Get the device registry to find all connected devices
                const device_registry_t *registry = device_registry_get();
                
                if (registry->count == 0) {
                    ESP_LOGW(TAG, "No devices detected, skipping demo");
                } else {
                    ESP_LOGI(TAG, "Found %d device(s), starting demo", registry->count);
                    
                    // Iterate through all discovered devices
                    for (int i = 0; i < DEVICE_REGISTRY_MAX_DEVICES; i++) {
                        const device_entry_t *entry = &registry->devices[i];
                        
                        if (!entry->present) {
                            continue;
                        }
                        
                        uint8_t addr = entry->address;
                        ESP_LOGI(TAG, "Demo on device 0x%02X (type: %s)", 
                                addr, block_type_to_string(entry->type));
                        
                        if (entry->type == BLOCK_TYPE_LED_FLASH) {
                            ESP_LOGI(TAG, "Device 0x%02X: LED_FLASH demo", addr);
                            
                            ESP_LOGI(TAG, "Device 0x%02X: FLASH RED", addr);
                            i2c_set_led(addr, 255, 0, 0);
                            i2c_execute(addr);
                            vTaskDelay(pdMS_TO_TICKS(400));
                            
                            ESP_LOGI(TAG, "Device 0x%02X: FLASH GREEN", addr);
                            i2c_set_led(addr, 0, 255, 0);
                            i2c_execute(addr);
                            vTaskDelay(pdMS_TO_TICKS(400));
                            
                            ESP_LOGI(TAG, "Device 0x%02X: FLASH BLUE", addr);
                            i2c_set_led(addr, 0, 0, 255);
                            i2c_execute(addr);
                            vTaskDelay(pdMS_TO_TICKS(400));
                        } else {
                            // Set brightness
                            ESP_LOGI(TAG, "Device 0x%02X: Setting brightness to 30%%", addr);
                            i2c_matrix_set_brightness(addr, 76);
                            vTaskDelay(pdMS_TO_TICKS(500));
                            
                            // Cycle through colors
                            ESP_LOGI(TAG, "Device 0x%02X: RED", addr);
                            i2c_matrix_fill(addr, 255, 0, 0);
                            vTaskDelay(pdMS_TO_TICKS(1000));
                            
                            ESP_LOGI(TAG, "Device 0x%02X: GREEN", addr);
                            i2c_matrix_fill(addr, 0, 255, 0);
                            vTaskDelay(pdMS_TO_TICKS(1000));
                            
                            ESP_LOGI(TAG, "Device 0x%02X: BLUE", addr);
                            i2c_matrix_fill(addr, 0, 0, 255);
                            vTaskDelay(pdMS_TO_TICKS(1000));
                            
                            ESP_LOGI(TAG, "Device 0x%02X: CLEAR", addr);
                            i2c_matrix_clear(addr);
                            vTaskDelay(pdMS_TO_TICKS(500));
                        }
                    }
                }
            }
            else if (cmd == CMD_STOP) {
                ESP_LOGI(TAG, "Demo STOP received");
                
                // Clear all discovered devices
                const device_registry_t *registry = device_registry_get();
                
                for (int i = 0; i < DEVICE_REGISTRY_MAX_DEVICES; i++) {
                    const device_entry_t *entry = &registry->devices[i];
                    if (entry->present) {
                        ESP_LOGI(TAG, "Clearing device 0x%02X", entry->address);
                        i2c_matrix_clear(entry->address);
                    }
                }
            }
        }
        // Wait before next cycle
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}