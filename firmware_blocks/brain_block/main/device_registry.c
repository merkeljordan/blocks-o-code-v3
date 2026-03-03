/*
 * device_registry.c
 *
 * Implementation of the Brain-side device registry.
 * Scans I2C addresses 0x08–0x15, reads REG_WHOAMI, and stores results.
 */

#include <string.h>
#include "device_registry.h"
#include "brain_block.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "DEV_REGISTRY";

// Global registry instance
static device_registry_t s_registry;

void device_registry_init(void) {
    memset(&s_registry, 0, sizeof(s_registry));
    for (int i = 0; i < DEVICE_REGISTRY_MAX_DEVICES; i++) {
        s_registry.devices[i].address = DEVICE_REGISTRY_ADDR_MIN + i;
        s_registry.devices[i].type = BLOCK_TYPE_UNKNOWN;
        s_registry.devices[i].present = false;
    }
    s_registry.count = 0;
    ESP_LOGI(TAG, "Device registry initialized (addr range 0x%02X-0x%02X)",
             DEVICE_REGISTRY_ADDR_MIN, DEVICE_REGISTRY_ADDR_MAX);
}

esp_err_t device_registry_scan(void) {
    ESP_LOGI(TAG, "=== DEVICE REGISTRY SCAN ===");
    
    uint8_t found = 0;

    for (uint8_t i = 0; i < DEVICE_REGISTRY_MAX_DEVICES; i++) {
        uint8_t addr = DEVICE_REGISTRY_ADDR_MIN + i;
        device_entry_t *entry = &s_registry.devices[i];
        
        // Reset entry
        entry->address = addr;
        entry->present = false;
        entry->type = BLOCK_TYPE_UNKNOWN;

        // First, ping to check if device exists
        esp_err_t ret = i2c_ping(addr);
        if (ret != ESP_OK) {
            continue;
        }

        // Device responded, try to read REG_WHOAMI with retries to smooth transients
        uint8_t whoami = BLOCK_TYPE_UNKNOWN;
        ret = ESP_FAIL;
        for (int attempt = 0; attempt < 5 && ret != ESP_OK; attempt++) {
            ret = i2c_read_reg(addr, REG_WHOAMI, &whoami, 1);
            if (ret != ESP_OK) {
                vTaskDelay(pdMS_TO_TICKS(10));
            }
        }
        
        if (ret == ESP_OK) {
            entry->present = true;
            entry->type = (block_type_t)whoami;
            found++;
            ESP_LOGI(TAG, "Device at 0x%02X: type=0x%02X (%s)",
                     addr, whoami, block_type_to_string(entry->type));
        } else {
            // Device ACK'd but failed REG_WHOAMI read - still mark present
            entry->present = true;
            entry->type = BLOCK_TYPE_UNKNOWN;
            found++;
            ESP_LOGW(TAG, "Device at 0x%02X: present but WHOAMI failed (err=%d)",
                     addr, ret);
        }
    }

    s_registry.count = found;
    ESP_LOGI(TAG, "Scan complete: %d device(s) found", found);
    
    return ESP_OK;
}

const device_registry_t* device_registry_get(void) {
    return &s_registry;
}

void device_registry_print(void) {
    ESP_LOGI(TAG, "=== DEVICE REGISTRY (%d devices) ===", s_registry.count);
    
    if (s_registry.count == 0) {
        ESP_LOGI(TAG, "  (no devices detected)");
        return;
    }

    for (int i = 0; i < DEVICE_REGISTRY_MAX_DEVICES; i++) {
        const device_entry_t *entry = &s_registry.devices[i];
        if (entry->present) {
            ESP_LOGI(TAG, "  [0x%02X] %s (type=0x%02X)",
                     entry->address,
                     block_type_to_string(entry->type),
                     entry->type);
        }
    }
}

const device_entry_t* device_registry_find(uint8_t address) {
    if (address < DEVICE_REGISTRY_ADDR_MIN || address > DEVICE_REGISTRY_ADDR_MAX) {
        return NULL;
    }
    
    uint8_t idx = address - DEVICE_REGISTRY_ADDR_MIN;
    const device_entry_t *entry = &s_registry.devices[idx];
    
    return entry->present ? entry : NULL;
}

uint8_t device_registry_count(void) {
    return s_registry.count;
}