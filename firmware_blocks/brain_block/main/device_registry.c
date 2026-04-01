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

static int find_entry_index_by_uid(const device_entry_t *entries, uint8_t count, uint32_t uid)
{
    if (uid == 0u) {
        return -1;
    }

    for (uint8_t i = 0; i < count; i++) {
        if (entries[i].present && entries[i].uid == uid) {
            return i;
        }
    }
    return -1;
}

static uint8_t choose_lowest_free_slot(const bool used_slots[DEVICE_REGISTRY_MAX_DEVICES])
{
    for (uint8_t i = 0; i < DEVICE_REGISTRY_MAX_DEVICES; i++) {
        if (!used_slots[i]) {
            return (uint8_t)(DEVICE_REGISTRY_ADDR_MIN + i);
        }
    }
    return DEVICE_REGISTRY_ADDR_MAX;
}

static uint32_t read_device_uid(uint8_t addr)
{
    uint8_t uid_bytes[4] = {0};
    esp_err_t ret = i2c_read_reg(addr, REG_UID0, uid_bytes, sizeof(uid_bytes));
    if (ret != ESP_OK) {
        return 0u;
    }

    uint32_t uid = ((uint32_t)uid_bytes[0]) |
                   ((uint32_t)uid_bytes[1] << 8) |
                   ((uint32_t)uid_bytes[2] << 16) |
                   ((uint32_t)uid_bytes[3] << 24);
    return uid;
}

static bool apply_allocator(const device_entry_t *previous_entries,
                            uint8_t previous_count,
                            device_entry_t *detected_entries,
                            uint8_t detected_count)
{
    if (detected_entries == NULL || detected_count == 0U) {
        return false;
    }

    bool used_slots[DEVICE_REGISTRY_MAX_DEVICES] = {0};
    for (uint8_t i = 0; i < detected_count; i++) {
        if (block_is_valid_child_address(detected_entries[i].address)) {
            used_slots[detected_entries[i].address - DEVICE_REGISTRY_ADDR_MIN] = true;
        }
    }

    bool requested_move = false;

    for (uint8_t i = 0; i < detected_count; i++) {
        device_entry_t *entry = &detected_entries[i];
        if (!entry->present || entry->uid == 0u) {
            continue;
        }

        uint8_t desired_addr = entry->address;
        int previous_idx = find_entry_index_by_uid(previous_entries, previous_count, entry->uid);
        if (previous_idx >= 0 && block_is_valid_child_address(previous_entries[previous_idx].address)) {
            /*
             * Preserve transport slots only for devices that were still present
             * in the immediately previous live scan.
             *
             * If a device disappears and later reappears, it is treated as a
             * newly inserted device for allocator purposes and receives the
             * next currently free slot instead of reclaiming a historical slot.
             */
            desired_addr = previous_entries[previous_idx].address;
        } else {
            desired_addr = choose_lowest_free_slot(used_slots);
        }

        if (!block_is_valid_child_address(desired_addr) || desired_addr == entry->address) {
            continue;
        }

        uint8_t desired_slot_idx = (uint8_t)(desired_addr - DEVICE_REGISTRY_ADDR_MIN);
        if (used_slots[desired_slot_idx]) {
            continue;
        }

        esp_err_t ret = i2c_set_child_address(entry->address, desired_addr);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to move UID 0x%08lX from 0x%02X to 0x%02X: %s",
                     (unsigned long)entry->uid,
                     entry->address,
                     desired_addr,
                     esp_err_to_name(ret));
            continue;
        }

        used_slots[desired_slot_idx] = true;
        requested_move = true;
        ESP_LOGI(TAG, "Moved UID 0x%08lX from 0x%02X to 0x%02X",
                 (unsigned long)entry->uid,
                 entry->address,
                 desired_addr);
    }

    return requested_move;
}

static esp_err_t device_registry_scan_once(bool allow_allocator)
{
    uint8_t found = 0;
    device_entry_t detected_entries[DEVICE_REGISTRY_MAX_DEVICES];
    uint8_t detected_count = 0;
    device_entry_t previous_entries[DEVICE_REGISTRY_MAX_DEVICES];
    uint8_t previous_count = 0;

    memset(detected_entries, 0, sizeof(detected_entries));
    memcpy(previous_entries, s_registry.devices, sizeof(previous_entries));
    for (uint8_t i = 0; i < DEVICE_REGISTRY_MAX_DEVICES; i++) {
        if (previous_entries[i].present) {
            previous_count++;
        }
    }
    memset(&s_registry, 0, sizeof(s_registry));

    for (uint8_t i = 0; i < DEVICE_REGISTRY_MAX_DEVICES; i++) {
        uint8_t addr = DEVICE_REGISTRY_ADDR_MIN + i;
        device_entry_t *entry = &s_registry.devices[i];
        entry->address = addr;
        entry->type = BLOCK_TYPE_UNKNOWN;
        entry->uid = 0u;
        entry->present = false;

        esp_err_t ret = i2c_ping(addr);
        if (ret != ESP_OK) {
            continue;
        }

        uint8_t whoami = BLOCK_TYPE_UNKNOWN;
        ret = ESP_FAIL;
        for (int attempt = 0; attempt < 5 && ret != ESP_OK; attempt++) {
            ret = i2c_read_reg(addr, REG_WHOAMI, &whoami, 1);
            if (ret != ESP_OK) {
                vTaskDelay(pdMS_TO_TICKS(10));
            }
        }

        entry->present = true;
        entry->type = is_valid_block_type_byte(whoami) ? (block_type_t)whoami : BLOCK_TYPE_UNKNOWN;
        entry->uid = read_device_uid(addr);
        s_miss_counts[i] = 0;
        found++;

        if (detected_count < DEVICE_REGISTRY_MAX_DEVICES) {
            detected_entries[detected_count++] = *entry;
        }

        if (entry->type == BLOCK_TYPE_UNKNOWN) {
            ESP_LOGW(TAG, "Device at 0x%02X pinged but WHOAMI was invalid/unknown (0x%02X)", addr, whoami);
        } else {
            ESP_LOGI(TAG, "Device at 0x%02X: type=0x%02X (%s), uid=0x%08lX",
                     addr,
                     whoami,
                     block_type_to_string(entry->type),
                     (unsigned long)entry->uid);
        }
    }

    if (allow_allocator && apply_allocator(previous_entries, previous_count, detected_entries, detected_count)) {
        vTaskDelay(pdMS_TO_TICKS(60));
        return device_registry_scan_once(false);
    }

    s_registry.count = found;
    ESP_LOGI(TAG, "Scan complete: %d device(s) found", found);
    return ESP_OK;
}

void device_registry_init(void) {
    memset(&s_registry, 0, sizeof(s_registry));
    for (int i = 0; i < DEVICE_REGISTRY_MAX_DEVICES; i++) {
        s_registry.devices[i].address = DEVICE_REGISTRY_ADDR_MIN + i;
        s_registry.devices[i].type = BLOCK_TYPE_UNKNOWN;
        s_registry.devices[i].uid = 0u;
        s_registry.devices[i].present = false;
        s_miss_counts[i] = 0;
    }
    s_registry.count = 0;
    ESP_LOGI(TAG, "Device registry initialized (addr range 0x%02X-0x%02X)",
             DEVICE_REGISTRY_ADDR_MIN, DEVICE_REGISTRY_ADDR_MAX);
}

esp_err_t device_registry_scan(void) {
    ESP_LOGI(TAG, "=== DEVICE REGISTRY SCAN ===");
    return device_registry_scan_once(true);
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
