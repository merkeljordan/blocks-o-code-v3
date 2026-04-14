/*
 * device_registry.c
 *
 * Sticky Brain-side device registry.
 * Scans the full 0x08-0x77 I2C range every cycle, debounces devices with a
 * confidence counter, and keeps a stable active-chain ordering that only
 * changes when a device fully disappears.
 */

#include <string.h>
#include "device_registry.h"
#include "brain_block.h"
#include "esp_log.h"
#include "freertos/task.h"

static const char *TAG = "DEV_REGISTRY";

// Transient I²C errors often clear on a short backoff; identity reads are small and infrequent.
#define DEV_REGISTRY_IDENTITY_READ_ATTEMPTS 4
#define DEV_REGISTRY_IDENTITY_READ_GAP_MS 2

typedef struct {
    uint8_t address;
    block_type_t type;
    uint32_t uid;
    uint8_t physical_position;
    bool locked;
} address_state_t;

static device_registry_t s_registry;
static address_state_t s_address_state[128];
static uint8_t confidence[128];

static uint32_t read_device_uid(uint8_t addr)
{
    uint8_t uid_bytes[4] = {0};

    for (int attempt = 0; attempt < DEV_REGISTRY_IDENTITY_READ_ATTEMPTS; attempt++) {
        if (attempt > 0) {
            vTaskDelay(pdMS_TO_TICKS(DEV_REGISTRY_IDENTITY_READ_GAP_MS));
        }
        /* Read each UID byte individually: the slave loads one byte per
         * register index into the TX ring buffer, so a single 4-byte read
         * would only get the first byte correct and stall / timeout. */
        bool ok = true;
        for (int b = 0; b < 4; b++) {
            if (i2c_read_reg(addr, (uint8_t)(REG_UID0 + b), &uid_bytes[b], 1) != ESP_OK) {
                ok = false;
                break;
            }
        }
        if (!ok) {
            continue;
        }

        return ((uint32_t)uid_bytes[0]) |
               ((uint32_t)uid_bytes[1] << 8) |
               ((uint32_t)uid_bytes[2] << 16) |
               ((uint32_t)uid_bytes[3] << 24);
    }

    return 0u;
}

static void refresh_identity(uint8_t addr)
{
    address_state_t *state = &s_address_state[addr];

    block_type_t type = block_infer_type_from_child_i2c_address(addr);
    uint32_t uid = read_device_uid(addr);

    if (type != BLOCK_TYPE_UNKNOWN) {
        state->type = type;
    }
    if (uid != 0u) {
        state->uid = uid;
    }

    ESP_LOGI(TAG,
             "Identity addr=0x%02X type=%s uid=0x%08lX pos=%u conf=%u",
             addr,
             block_type_to_string(state->type),
             (unsigned long)state->uid,
             (unsigned)state->physical_position,
             (unsigned)confidence[addr]);
}

static int find_free_physical_position(void)
{
    bool used[DEVICE_REGISTRY_MAX_DEVICES] = {0};

    for (int addr = DEVICE_REGISTRY_ADDR_MIN; addr <= DEVICE_REGISTRY_ADDR_MAX; addr++) {
        const address_state_t *state = &s_address_state[addr];
        if (!state->locked || state->physical_position >= DEVICE_REGISTRY_MAX_DEVICES) {
            continue;
        }
        used[state->physical_position] = true;
    }

    for (int pos = 0; pos < DEVICE_REGISTRY_MAX_DEVICES; pos++) {
        if (!used[pos]) {
            return pos;
        }
    }

    return -1;
}

static void assign_newly_locked_addresses(const uint8_t *addresses, uint8_t count)
{
    for (uint8_t i = 0; i < count; i++) {
        uint8_t addr = addresses[i];
        address_state_t *state = &s_address_state[addr];

        if (state->locked) {
            continue;
        }

        int physical_position = find_free_physical_position();
        if (physical_position < 0) {
            ESP_LOGW(TAG, "Active chain full; dropping new address 0x%02X", addr);
            break;
        }

        state->locked = true;
        state->physical_position = (uint8_t)physical_position;
        refresh_identity(addr);
        ESP_LOGI(TAG,
                 "Locked addr=0x%02X into physical_position=%u",
                 addr,
                 (unsigned)state->physical_position);
    }
}

static void release_address(uint8_t addr)
{
    address_state_t *state = &s_address_state[addr];
    if (!state->locked) {
        state->type = BLOCK_TYPE_UNKNOWN;
        state->uid = 0u;
        return;
    }

    ESP_LOGW(TAG,
             "Releasing addr=0x%02X from physical_position=%u",
             addr,
             (unsigned)state->physical_position);

    state->locked = false;
    state->type = BLOCK_TYPE_UNKNOWN;
    state->uid = 0u;
    state->physical_position = UINT8_MAX;
}

static void rebuild_registry_snapshot(void)
{
    memset(&s_registry, 0, sizeof(s_registry));

    for (uint8_t pos = 0; pos < DEVICE_REGISTRY_MAX_DEVICES; pos++) {
        for (int addr = DEVICE_REGISTRY_ADDR_MIN; addr <= DEVICE_REGISTRY_ADDR_MAX; addr++) {
            const address_state_t *state = &s_address_state[addr];
            if (!state->locked || state->physical_position != pos) {
                continue;
            }

            uint8_t index = s_registry.count;
            device_entry_t *entry = &s_registry.devices[index];
            active_chain_entry_t *chain_entry = &s_registry.active_chain[index];

            entry->address = state->address;
            entry->type = state->type;
            entry->uid = state->uid;
            entry->present = true;
            entry->confidence = confidence[addr];
            entry->physical_position = state->physical_position;

            chain_entry->address = state->address;
            chain_entry->physical_position = state->physical_position;
            chain_entry->occupied = true;

            s_registry.count++;
            break;
        }
    }
}

void device_registry_init(void)
{
    memset(&s_registry, 0, sizeof(s_registry));
    memset(s_address_state, 0, sizeof(s_address_state));
    memset(confidence, 0, sizeof(confidence));

    for (int addr = DEVICE_REGISTRY_ADDR_MIN; addr <= DEVICE_REGISTRY_ADDR_MAX; addr++) {
        s_address_state[addr].address = (uint8_t)addr;
        s_address_state[addr].type = BLOCK_TYPE_UNKNOWN;
        s_address_state[addr].uid = 0u;
        s_address_state[addr].physical_position = UINT8_MAX;
        s_address_state[addr].locked = false;
    }

    ESP_LOGI(TAG,
             "Device registry initialized (scan range 0x%02X-0x%02X, max_active=%u)",
             DEVICE_REGISTRY_ADDR_MIN,
             DEVICE_REGISTRY_ADDR_MAX,
             (unsigned)DEVICE_REGISTRY_MAX_DEVICES);
}

esp_err_t device_registry_scan(void)
{
    uint8_t newly_locked[DEVICE_REGISTRY_MAX_DEVICES] = {0};
    uint8_t newly_locked_count = 0;

    for (int addr = DEVICE_REGISTRY_ADDR_MIN; addr <= DEVICE_REGISTRY_ADDR_MAX; addr++) {
        esp_err_t ret = i2c_ping((uint8_t)addr);
        if (ret == ESP_OK) {
            if (confidence[addr] < DEVICE_REGISTRY_CONFIDENCE_MAX) {
                confidence[addr]++;
            }

            if (confidence[addr] == DEVICE_REGISTRY_CONFIDENCE_MAX) {
                if (!s_address_state[addr].locked &&
                    newly_locked_count < DEVICE_REGISTRY_MAX_DEVICES) {
                    newly_locked[newly_locked_count++] = (uint8_t)addr;
                } else if (s_address_state[addr].locked &&
                           (s_address_state[addr].type == BLOCK_TYPE_UNKNOWN ||
                            s_address_state[addr].uid == 0u)) {
                    refresh_identity((uint8_t)addr);
                }
            }
        } else {
            if (confidence[addr] > 0u) {
                confidence[addr]--;
            }

            if (confidence[addr] == 0u) {
                release_address((uint8_t)addr);
            }
        }
    }

    // The scan loop is ascending, so simultaneous arrivals naturally fall back
    // to numerical-address ordering inside the same scan window.
    assign_newly_locked_addresses(newly_locked, newly_locked_count);
    rebuild_registry_snapshot();
    return ESP_OK;
}

const device_registry_t* device_registry_get(void)
{
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

void device_registry_print(void)
{
    ESP_LOGI(TAG, "=== DEVICE REGISTRY (%u active) ===", (unsigned)s_registry.count);

    if (s_registry.count == 0u) {
        ESP_LOGI(TAG, "  (no devices detected)");
        return;
    }

    for (uint8_t i = 0; i < s_registry.count; i++) {
        const device_entry_t *entry = &s_registry.devices[i];
        ESP_LOGI(TAG,
                 "  pos=%u addr=0x%02X conf=%u type=%s uid=0x%08lX",
                 (unsigned)entry->physical_position,
                 entry->address,
                 (unsigned)entry->confidence,
                 block_type_to_string(entry->type),
                 (unsigned long)entry->uid);
    }
}

const device_entry_t* device_registry_find(uint8_t address)
{
    for (uint8_t i = 0; i < s_registry.count; i++) {
        if (s_registry.devices[i].address == address) {
            return &s_registry.devices[i];
        }
    }

    return NULL;
}

uint8_t device_registry_count(void)
{
    return s_registry.count;
}
