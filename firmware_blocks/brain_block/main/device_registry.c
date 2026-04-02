/*
 * device_registry.c
 *
 * Implementation of the Brain-side device registry.
 * Scans I2C addresses 0x08-0x16 with debounced presence, reads identity registers, and stores results.
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

// Debounce / skeptical discovery:
// - Confirm presence only after 10 consecutive ACKs
// - Confirm removal only after 5 consecutive NACKs (while previously confirmed present)
#define DEVICE_REGISTRY_CONFIRM_ACKS   10
#define DEVICE_REGISTRY_REMOVE_NACKS   5
static uint8_t s_confidence[128] = {0};   // ACK streak per address
static uint8_t s_nack_streak[128] = {0};  // NACK streak per address (only meaningful once confirmed present)

#define IDENTITY_READ_ATTEMPTS 3
#define IDENTITY_READ_SETTLE_MS 6
#define SCAN_POST_PING_SETTLE_MS 2
#define SCAN_INTER_DEVICE_SETTLE_MS 2

/* REG_WHOAMI (0x00) through REG_ASSIGNED_ADDR (0x0A) inclusive = 11 bytes per i2c_protocol.h */
#define IDENTITY_BURST_LEN 11

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

static esp_err_t read_device_whoami(uint8_t addr, block_type_t *out_type)
{
    if (out_type == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t whoami = BLOCK_TYPE_UNKNOWN;
    esp_err_t ret = i2c_read_reg(addr, REG_WHOAMI, &whoami, 1);
    if (ret != ESP_OK) {
        return ret;
    }

    *out_type = is_valid_block_type_byte(whoami) ? (block_type_t)whoami : BLOCK_TYPE_UNKNOWN;
    return ESP_OK;
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

    uint8_t raw[IDENTITY_BURST_LEN] = {0};
    esp_err_t ret = i2c_read_reg(addr, REG_WHOAMI, raw, sizeof(raw));
    if (ret != ESP_OK) {
        return false;
    }

    uint8_t whoami = raw[REG_WHOAMI];
    uint32_t uid = ((uint32_t)raw[REG_UID0]) |
                   ((uint32_t)raw[REG_UID1] << 8) |
                   ((uint32_t)raw[REG_UID2] << 16) |
                   ((uint32_t)raw[REG_UID3] << 24);
    uint8_t assigned = raw[REG_ASSIGNED_ADDR];

    out->valid = is_valid_block_type_byte(whoami) &&
                 uid != 0u &&
                 assigned == addr;
    out->whoami = whoami;
    out->uid = uid;
    out->assigned_addr = assigned;
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

    for (int i = 0; i < IDENTITY_READ_ATTEMPTS; i++) {
        if (snapshots[i].valid) {
            *out = snapshots[i];
            ESP_LOGW(TAG, "Identity at 0x%02X was not fully stable; accepting single valid snapshot", addr);
            return true;
        }
    }

    return false;
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

static void refresh_entry_identity(uint8_t addr, device_entry_t *entry, bool just_confirmed)
{
    if (entry == NULL || !entry->present) {
        return;
    }

    if (!just_confirmed && entry->type != BLOCK_TYPE_UNKNOWN && entry->uid != 0u) {
        return;
    }

    vTaskDelay(pdMS_TO_TICKS(SCAN_POST_PING_SETTLE_MS));

    identity_snapshot_t identity = {0};
    if (read_stable_identity(addr, &identity)) {
        entry->type = (block_type_t)identity.whoami;
        entry->uid = identity.uid;
    } else {
        block_type_t type = BLOCK_TYPE_UNKNOWN;
        esp_err_t whoami_ret = read_device_whoami(addr, &type);
        if (whoami_ret == ESP_OK) {
            entry->type = type;
        }
        uint32_t uid = read_device_uid(addr);
        if (uid != 0u) {
            entry->uid = uid;
        }
    }

    if (entry->type == BLOCK_TYPE_UNKNOWN) {
        ESP_LOGW(TAG, "Device at 0x%02X confirmed but identity was unknown/unstable", addr);
    } else {
        ESP_LOGI(TAG, "Device at 0x%02X: %s uid=0x%08lX",
                 addr,
                 block_type_to_string(entry->type),
                 (unsigned long)entry->uid);
    }
}

static esp_err_t device_registry_scan_once(bool allow_allocator)
{
    uint8_t confirmed_found = 0;
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

    for (uint8_t i = 0; i < DEVICE_REGISTRY_MAX_DEVICES; i++) {
        uint8_t addr = DEVICE_REGISTRY_ADDR_MIN + i;
        device_entry_t *entry = &s_registry.devices[i];
        entry->address = addr;

        esp_err_t ret = i2c_ping(addr);
        if (ret == ESP_OK) {
            s_nack_streak[addr] = 0;
            if (s_confidence[addr] < DEVICE_REGISTRY_CONFIRM_ACKS) {
                s_confidence[addr]++;
            }

            bool just_confirmed = (!entry->present && s_confidence[addr] >= DEVICE_REGISTRY_CONFIRM_ACKS);
            if (just_confirmed) {
                entry->present = true;
                ESP_LOGI(TAG, "Confirmed present at 0x%02X (confidence=%u)", addr, (unsigned)s_confidence[addr]);
            }

            if (entry->present &&
                (just_confirmed || entry->type == BLOCK_TYPE_UNKNOWN || entry->uid == 0u)) {
                refresh_entry_identity(addr, entry, just_confirmed);
            }
        } else {
            s_confidence[addr] = 0;
            if (entry->present) {
                if (s_nack_streak[addr] < DEVICE_REGISTRY_REMOVE_NACKS) {
                    s_nack_streak[addr]++;
                }

                if (s_nack_streak[addr] >= DEVICE_REGISTRY_REMOVE_NACKS) {
                    entry->present = false;
                    ESP_LOGW(TAG, "Confirmed removed at 0x%02X (nack_streak=%u)", addr, (unsigned)s_nack_streak[addr]);
                }
            } else {
                s_nack_streak[addr] = 0;
            }
        }

        if (entry->present) {
            confirmed_found++;
            if (detected_count < DEVICE_REGISTRY_MAX_DEVICES) {
                detected_entries[detected_count++] = *entry;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(SCAN_INTER_DEVICE_SETTLE_MS));
    }

    if (allow_allocator && detected_count > 0 &&
        apply_allocator(previous_entries, previous_count, detected_entries, detected_count)) {
        ESP_LOGI(TAG, "Allocator requested address move(s); will be reflected on next scan tick");
    }

    s_registry.count = confirmed_found;
    return ESP_OK;
}

void device_registry_init(void) {
    memset(&s_registry, 0, sizeof(s_registry));
    for (int i = 0; i < DEVICE_REGISTRY_MAX_DEVICES; i++) {
        s_registry.devices[i].address = DEVICE_REGISTRY_ADDR_MIN + i;
        s_registry.devices[i].type = BLOCK_TYPE_UNKNOWN;
        s_registry.devices[i].uid = 0u;
        s_registry.devices[i].present = false;
        s_confidence[DEVICE_REGISTRY_ADDR_MIN + i] = 0;
        s_nack_streak[DEVICE_REGISTRY_ADDR_MIN + i] = 0;
    }
    s_registry.count = 0;
    ESP_LOGI(TAG, "Device registry initialized (addr range 0x%02X-0x%02X)",
             DEVICE_REGISTRY_ADDR_MIN, DEVICE_REGISTRY_ADDR_MAX);
}

esp_err_t device_registry_scan(void) {
    return device_registry_scan_once(true);
}

const device_registry_t* device_registry_get(void) {
    return &s_registry;
}

esp_err_t device_registry_get_snapshot(device_registry_t *out_registry)
{
    if (out_registry == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memcpy(out_registry, &s_registry, sizeof(*out_registry));
    return ESP_OK;
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
