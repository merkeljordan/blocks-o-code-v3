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
/* Tolerate several misses before dropping a marginal block from the live set. */
#define DEVICE_REGISTRY_MAX_MISSES 3
static uint8_t s_miss_counts[DEVICE_REGISTRY_MAX_DEVICES] = {0};

#define IDENTITY_READ_ATTEMPTS 3
#define IDENTITY_READ_SETTLE_MS 6
#define SCAN_POST_PING_SETTLE_MS 2
#define SCAN_INTER_DEVICE_SETTLE_MS 2

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

typedef struct {
    bool valid;
    uint8_t whoami;
    uint32_t uid;
    uint8_t assigned_addr;
} identity_snapshot_t;

static bool read_identity_snapshot(uint8_t addr, identity_snapshot_t *out)
{
    if (out == NULL) {
        return false;
    }

    uint8_t raw[6] = {0};
    esp_err_t ret = i2c_read_reg(addr, REG_WHOAMI, raw, sizeof(raw));
    if (ret != ESP_OK) {
        return false;
    }

    uint32_t uid = ((uint32_t)raw[1]) |
                   ((uint32_t)raw[2] << 8) |
                   ((uint32_t)raw[3] << 16) |
                   ((uint32_t)raw[4] << 24);

    out->valid = is_valid_block_type_byte(raw[0]) &&
                 uid != 0u &&
                 raw[5] == addr;
    out->whoami = raw[0];
    out->uid = uid;
    out->assigned_addr = raw[5];
    return out->valid;
}

static bool read_stable_identity(uint8_t addr, identity_snapshot_t *out)
{
    if (out == NULL) {
        return false;
    }

    identity_snapshot_t snapshots[IDENTITY_READ_ATTEMPTS];
    memset(snapshots, 0, sizeof(snapshots));

    int valid_count = 0;
    for (int attempt = 0; attempt < IDENTITY_READ_ATTEMPTS; attempt++) {
        if (read_identity_snapshot(addr, &snapshots[attempt])) {
            valid_count++;
        }
        if (attempt + 1 < IDENTITY_READ_ATTEMPTS) {
            vTaskDelay(pdMS_TO_TICKS(IDENTITY_READ_SETTLE_MS));
        }
    }

    if (valid_count == 0) {
        return false;
    }

    for (int i = 0; i < IDENTITY_READ_ATTEMPTS; i++) {
        if (!snapshots[i].valid) {
            continue;
        }
        int matches = 1;
        for (int j = i + 1; j < IDENTITY_READ_ATTEMPTS; j++) {
            if (!snapshots[j].valid) {
                continue;
            }
            if (snapshots[i].whoami == snapshots[j].whoami &&
                snapshots[i].uid == snapshots[j].uid &&
                snapshots[i].assigned_addr == snapshots[j].assigned_addr) {
                matches++;
            }
        }

        if (matches >= 2) {
            *out = snapshots[i];
            return true;
        }
    }

    // On marginal chains, allow a single valid identity snapshot through as a
    // best-effort detection instead of dropping the device entirely.
    for (int i = 0; i < IDENTITY_READ_ATTEMPTS; i++) {
        if (snapshots[i].valid) {
            *out = snapshots[i];
            ESP_LOGW(TAG, "Identity at 0x%02X was not fully stable; accepting single valid snapshot", addr);
            return true;
        }
    }

    return false;
}

static esp_err_t device_registry_scan_once(void)
{
    uint8_t found = 0;
    device_entry_t detected_entries[DEVICE_REGISTRY_MAX_DEVICES];
    uint8_t detected_count = 0;
    device_entry_t previous_entries[DEVICE_REGISTRY_MAX_DEVICES];

    memset(detected_entries, 0, sizeof(detected_entries));
    memcpy(previous_entries, s_registry.devices, sizeof(previous_entries));
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
            if (previous_entries[i].present && s_miss_counts[i] < DEVICE_REGISTRY_MAX_MISSES) {
                s_miss_counts[i]++;
                *entry = previous_entries[i];
                entry->address = addr;
                found++;
                if (detected_count < DEVICE_REGISTRY_MAX_DEVICES) {
                    detected_entries[detected_count++] = *entry;
                }
                ESP_LOGW(TAG,
                         "Transient miss at 0x%02X; keeping previous device for miss %u/%u",
                         addr,
                         (unsigned)s_miss_counts[i],
                         (unsigned)DEVICE_REGISTRY_MAX_MISSES);
            } else {
                s_miss_counts[i] = 0;
            }
            continue;
        }

        vTaskDelay(pdMS_TO_TICKS(SCAN_POST_PING_SETTLE_MS));
        entry->present = true;
        identity_snapshot_t identity = {0};
        if (read_stable_identity(addr, &identity)) {
            entry->type = (block_type_t)identity.whoami;
            entry->uid = identity.uid;
        } else {
            uint8_t whoami = BLOCK_TYPE_UNKNOWN;
            ret = ESP_FAIL;
            for (int attempt = 0; attempt < 5 && ret != ESP_OK; attempt++) {
                ret = i2c_read_reg(addr, REG_WHOAMI, &whoami, 1);
                if (ret != ESP_OK) {
                    vTaskDelay(pdMS_TO_TICKS(10));
                }
            }
            entry->type = is_valid_block_type_byte(whoami) ? (block_type_t)whoami : BLOCK_TYPE_UNKNOWN;
            entry->uid = read_device_uid(addr);
        }
        s_miss_counts[i] = 0;
        found++;

        if (detected_count < DEVICE_REGISTRY_MAX_DEVICES) {
            detected_entries[detected_count++] = *entry;
        }

        if (entry->type == BLOCK_TYPE_UNKNOWN) {
            ESP_LOGW(TAG, "Device at 0x%02X pinged but identity was unstable/unknown", addr);
        } else {
            ESP_LOGI(TAG, "Device at 0x%02X: type=0x%02X (%s), uid=0x%08lX",
                     addr,
                     entry->type,
                     block_type_to_string(entry->type),
                     (unsigned long)entry->uid);
        }

        vTaskDelay(pdMS_TO_TICKS(SCAN_INTER_DEVICE_SETTLE_MS));
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
    return device_registry_scan_once();
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
