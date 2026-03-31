/*
 * device_registry.c
 *
 * Implementation of the Brain-side device registry.
 * Scans I2C addresses 0x08-0x16, reads REG_WHOAMI, and stores results.
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
/* One miss tolerated; second miss = device gone (fast removal detection) */
#define DEVICE_REGISTRY_MAX_MISSES 1
static uint8_t s_miss_counts[DEVICE_REGISTRY_MAX_DEVICES] = {0};

typedef enum {
    DEVICE_SCAN_NO_DEVICE = 0,
    DEVICE_SCAN_OK,
    DEVICE_SCAN_INVALID_WHOAMI,
    DEVICE_SCAN_INVALID_BRAIN_TYPE_AT_CHILD_ADDR,
    DEVICE_SCAN_WHOAMI_READ_FAIL,
} device_scan_result_t;

static bool is_valid_block_type_byte(uint8_t raw)
{
    switch ((block_type_t)raw) {
        case BLOCK_TYPE_BRAIN:
        case BLOCK_TYPE_IF:
        case BLOCK_TYPE_THEN:
        case BLOCK_TYPE_END_IF:
        case BLOCK_TYPE_LOOP:
        case BLOCK_TYPE_END_LOOP:
        case BLOCK_TYPE_DELAY:
        case BLOCK_TYPE_BUTTON:
        case BLOCK_TYPE_NOTE:
        case BLOCK_TYPE_MUSIC_SEQ:
        case BLOCK_TYPE_LED_FLASH:
        case BLOCK_TYPE_DISCO:
        case BLOCK_TYPE_UNKNOWN:
            return true;
        default:
            return false;
    }
}

static device_scan_result_t scan_one_address(uint8_t addr,
                                             device_entry_t *entry,
                                             uint8_t *out_whoami,
                                             esp_err_t *out_err)
{
    if (entry == NULL) {
        if (out_err != NULL) {
            *out_err = ESP_ERR_INVALID_ARG;
        }
        if (out_whoami != NULL) {
            *out_whoami = BLOCK_TYPE_UNKNOWN;
        }
        return DEVICE_SCAN_NO_DEVICE;
    }

    entry->address = addr;
    entry->present = false;
    entry->type = BLOCK_TYPE_UNKNOWN;

    if (out_whoami != NULL) {
        *out_whoami = BLOCK_TYPE_UNKNOWN;
    }
    if (out_err != NULL) {
        *out_err = ESP_OK;
    }

    esp_err_t ret = i2c_ping(addr);
    if (ret != ESP_OK) {
        if (out_err != NULL) {
            *out_err = ret;
        }
        return DEVICE_SCAN_NO_DEVICE;
    }

    entry->present = true;

    uint8_t whoami = BLOCK_TYPE_UNKNOWN;
    ret = i2c_read_reg(addr, REG_WHOAMI, &whoami, 1);
    if (out_whoami != NULL) {
        *out_whoami = whoami;
    }
    if (out_err != NULL) {
        *out_err = ret;
    }

    if (ret != ESP_OK) {
        return DEVICE_SCAN_WHOAMI_READ_FAIL;
    }

    if (!is_valid_block_type_byte(whoami)) {
        return DEVICE_SCAN_INVALID_WHOAMI;
    }

    if (whoami == (uint8_t)BLOCK_TYPE_BRAIN && addr != 0x00U) {
        return DEVICE_SCAN_INVALID_BRAIN_TYPE_AT_CHILD_ADDR;
    }

    entry->type = (block_type_t)whoami;
    return DEVICE_SCAN_OK;
}

void device_registry_init(void) {
    memset(&s_registry, 0, sizeof(s_registry));
    for (int i = 0; i < DEVICE_REGISTRY_MAX_DEVICES; i++) {
        s_registry.devices[i].address = DEVICE_REGISTRY_ADDR_MIN + i;
        s_registry.devices[i].type = BLOCK_TYPE_UNKNOWN;
        s_registry.devices[i].present = false;
        s_miss_counts[i] = 0;
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

        // Preserve previous state for simple hysteresis across scans.
        bool was_present = entry->present && (entry->type != BLOCK_TYPE_UNKNOWN);
        block_type_t prev_type = entry->type;

        // Reset entry
        entry->address = addr;
        entry->present = false;
        entry->type = BLOCK_TYPE_UNKNOWN;

        // First, ping to check if device exists
        esp_err_t ret = i2c_ping(addr);
        if (ret != ESP_OK) {
            // If this device was previously known, tolerate a few transient misses
            if (was_present && s_miss_counts[i] < DEVICE_REGISTRY_MAX_MISSES) {
                s_miss_counts[i]++;
                entry->present = true;
                entry->type = prev_type;
                found++;
                ESP_LOGW(TAG, "Transient miss %u/%u at 0x%02X; keeping previous device (%s)",
                         (unsigned)s_miss_counts[i], (unsigned)DEVICE_REGISTRY_MAX_MISSES,
                         addr, block_type_to_string(prev_type));
            }
            continue;
        }

        // Successful ping; reset miss counter for this slot.
        s_miss_counts[i] = 0;

        // For previously known-good devices, we trust the cached type and skip WHOAMI.
        if (was_present && prev_type != BLOCK_TYPE_UNKNOWN) {
            entry->present = true;
            entry->type = prev_type;
            found++;
            ESP_LOGD(TAG, "Device at 0x%02X present (cached type %s)",
                     addr, block_type_to_string(prev_type));
            continue;
        }

        // New or previously unknown device: read REG_WHOAMI once to learn its type.
        uint8_t whoami = BLOCK_TYPE_UNKNOWN;
        ret = ESP_FAIL;
        for (int attempt = 0; attempt < 5 && ret != ESP_OK; attempt++) {
            ret = i2c_read_reg(addr, REG_WHOAMI, &whoami, 1);
            if (ret != ESP_OK) {
                vTaskDelay(pdMS_TO_TICKS(10));
            }
        }

        if (ret == ESP_OK && whoami != BLOCK_TYPE_UNKNOWN) {
            entry->present = true;
            entry->type = (block_type_t)whoami;
            found++;
            ESP_LOGI(TAG, "Device at 0x%02X: type=0x%02X (%s) (new/updated)",
                     addr, whoami, block_type_to_string(entry->type));
        } else {
            ESP_LOGW(TAG, "Device at 0x%02X pinged but WHOAMI failed/unknown (err=%d, whoami=0x%02X); not adding",
                     addr, ret, whoami);
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
