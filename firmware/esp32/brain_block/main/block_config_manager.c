/*
 * block_config_manager.c
 *
 * Implementation of Block Configuration Manager
 * Scans I2C bus, reads WHOAMI data, detects changes, and generates JSON.
 */

#include <string.h>
#include <stdio.h>
#include "block_config_manager.h"
#include "device_registry.h"
#include "brain_block.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "cJSON.h"

static const char *TAG = "BLOCK_CONFIG";

// Global configuration state
static block_config_state_t s_config_state;
static block_config_state_t s_previous_state;

// Initialize previous state to empty
static bool s_previous_state_valid = false;

void block_config_manager_init(void) {
    memset(&s_config_state, 0, sizeof(s_config_state));
    memset(&s_previous_state, 0, sizeof(s_previous_state));
    s_previous_state_valid = false;
    s_config_state.has_changed = true; // Force initial send
    ESP_LOGI(TAG, "Block configuration manager initialized");
}

const char* block_type_to_json_string(block_type_t type) {
    switch (type) {
        case BLOCK_TYPE_BRAIN:      return "brain_block";
        case BLOCK_TYPE_IF:         return "if_block";
        case BLOCK_TYPE_THEN:       return "then_block";
        case BLOCK_TYPE_END_IF:     return "end_if_block";
        case BLOCK_TYPE_LOOP:       return "loop_block";
        case BLOCK_TYPE_END_LOOP:   return "end_loop_block";
        case BLOCK_TYPE_BUTTON:     return "button_press";
        case BLOCK_TYPE_NOTE:       return "note_block";
        case BLOCK_TYPE_MUSIC_SEQ:  return "music_sequence_block";
        case BLOCK_TYPE_LED_FLASH:  return "led_color_flash_block";
        case BLOCK_TYPE_DISCO:      return "disco_mode_block";
        default:                    return "unknown";
    }
}

static void read_whoami_data(uint8_t address, block_config_entry_t *entry) {
    uint8_t whoami = BLOCK_TYPE_UNKNOWN;
    esp_err_t ret;

    // Read REG_WHOAMI (required)
    ret = i2c_read_reg(address, REG_WHOAMI, &whoami, 1);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to read WHOAMI from 0x%02X: %s", address, esp_err_to_name(ret));
        entry->block_type = BLOCK_TYPE_UNKNOWN;
        entry->present = false;
        return;
    }

    entry->block_type = (block_type_t)whoami;
    entry->present = true;

    // Try to read firmware version (optional)
    uint8_t fw_major = 0;
    uint8_t fw_minor = 0;
    ret = i2c_read_reg(address, REG_FW_MAJOR, &fw_major, 1);
    if (ret == ESP_OK) {
        entry->fw_major = fw_major;
        ret = i2c_read_reg(address, REG_FW_MINOR, &fw_minor, 1);
        if (ret == ESP_OK) {
            entry->fw_minor = fw_minor;
        }
    }

    // Try to read capabilities (optional)
    uint8_t caps = 0;
    ret = i2c_read_reg(address, REG_CAPS, &caps, 1);
    if (ret == ESP_OK) {
        entry->caps = caps;
    }
}

static bool compare_configurations(const block_config_state_t *prev, const block_config_state_t *curr) {
    // Check if block count changed
    if (prev->block_count != curr->block_count) {
        return true;
    }

    // Check if any block addresses or types changed
    for (int i = 0; i < curr->block_count; i++) {
        bool found = false;
        for (int j = 0; j < prev->block_count; j++) {
            if (prev->blocks[j].i2c_address == curr->blocks[i].i2c_address) {
                found = true;
                // Check if type changed (error condition)
                if (prev->blocks[j].block_type != curr->blocks[i].block_type) {
                    return true;
                }
                break;
            }
        }
        // New block detected
        if (!found) {
            return true;
        }
    }

    // Check if any previous blocks are missing
    for (int i = 0; i < prev->block_count; i++) {
        bool found = false;
        for (int j = 0; j < curr->block_count; j++) {
            if (prev->blocks[j].i2c_address == curr->blocks[i].i2c_address) {
                found = true;
                break;
            }
        }
        if (!found) {
            return true;
        }
    }

    return false;
}

esp_err_t block_config_manager_scan(void) {
    ESP_LOGI(TAG, "=== BLOCK CONFIGURATION SCAN ===");

    // Save previous state
    memcpy(&s_previous_state, &s_config_state, sizeof(s_config_state));
    s_previous_state_valid = true;

    // Clear current state
    memset(&s_config_state.blocks, 0, sizeof(s_config_state.blocks));
    s_config_state.block_count = 0;
    s_config_state.error_count = 0;

    // Use device registry to scan I2C bus
    device_registry_scan();
    const device_registry_t *registry = device_registry_get();

    // Process detected devices
    int connection_order = 0;
    for (int i = 0; i < DEVICE_REGISTRY_MAX_DEVICES; i++) {
        const device_entry_t *entry = &registry->devices[i];
        if (!entry->present) {
            continue;
        }

        if (s_config_state.block_count >= BLOCK_CONFIG_MAX_BLOCKS) {
            ESP_LOGW(TAG, "Maximum block count reached, skipping additional blocks");
            break;
        }

        block_config_entry_t *config_entry = &s_config_state.blocks[s_config_state.block_count];
        config_entry->i2c_address = entry->address;
        config_entry->connection_order = connection_order++;
        config_entry->fw_major = 0;
        config_entry->fw_minor = 0;
        config_entry->caps = 0;

        // Read full WHOAMI data
        read_whoami_data(entry->address, config_entry);

        if (!config_entry->present) {
            s_config_state.error_count++;
            ESP_LOGW(TAG, "Block at 0x%02X failed WHOAMI read", entry->address);
        } else if (config_entry->block_type == BLOCK_TYPE_UNKNOWN) {
            s_config_state.error_count++;
            ESP_LOGW(TAG, "Block at 0x%02X returned unknown type", entry->address);
        }

        s_config_state.block_count++;
    }

    // Check for missing blocks (blocks that were present before but not now)
    if (s_previous_state_valid) {
        for (int i = 0; i < s_previous_state.block_count; i++) {
            bool found = false;
            for (int j = 0; j < s_config_state.block_count; j++) {
                if (s_previous_state.blocks[i].i2c_address == s_config_state.blocks[j].i2c_address) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                s_config_state.error_count++;
                ESP_LOGW(TAG, "Block at 0x%02X is missing", s_previous_state.blocks[i].i2c_address);
            }
        }
    }

    // Detect changes
    if (s_previous_state_valid) {
        s_config_state.has_changed = compare_configurations(&s_previous_state, &s_config_state);
    } else {
        s_config_state.has_changed = true; // First scan always counts as change
    }

    // Update timestamp
    s_config_state.last_scan_timestamp = esp_timer_get_time() / 1000; // Convert to milliseconds

    ESP_LOGI(TAG, "Scan complete: %d block(s), %d error(s), changed: %s",
             s_config_state.block_count, s_config_state.error_count,
             s_config_state.has_changed ? "yes" : "no");

    return ESP_OK;
}

bool block_config_manager_has_changed(void) {
    return s_config_state.has_changed;
}

uint8_t block_config_manager_get_error_count(void) {
    return s_config_state.error_count;
}

const block_config_state_t* block_config_manager_get_state(void) {
    return &s_config_state;
}

static void add_capabilities_array(cJSON *whoami_obj, uint8_t caps) {
    // For now, capabilities are a single byte
    // In the future, this could be decoded into an array of strings
    // For now, return empty array as capabilities decoding is not defined
    cJSON *caps_array = cJSON_CreateArray();
    cJSON_AddItemToObject(whoami_obj, "capabilities", caps_array);
}

static void add_error_to_json(cJSON *errors_array, const char *type, const char *message, int block_index, int i2c_address) {
    cJSON *error_obj = cJSON_CreateObject();
    cJSON_AddStringToObject(error_obj, "type", type);
    cJSON_AddStringToObject(error_obj, "message", message);
    if (block_index >= 0) {
        cJSON_AddNumberToObject(error_obj, "block_index", block_index);
    }
    if (i2c_address >= 0) {
        cJSON_AddNumberToObject(error_obj, "i2c_address", i2c_address);
    }
    cJSON_AddItemToArray(errors_array, error_obj);
}

esp_err_t block_config_manager_get_json(char *json_buffer, size_t buffer_size) {
    if (json_buffer == NULL || buffer_size < 256) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return ESP_ERR_NO_MEM;
    }

    // Add type field
    cJSON_AddStringToObject(root, "type", "block_config");

    // Add timestamp
    uint64_t timestamp_ms = s_config_state.last_scan_timestamp;
    if (timestamp_ms == 0) {
        timestamp_ms = esp_timer_get_time() / 1000;
    }
    cJSON_AddNumberToObject(root, "timestamp", (double)timestamp_ms);

    // Create config object
    cJSON *config = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "config", config);

    // Add total_blocks
    cJSON_AddNumberToObject(config, "total_blocks", s_config_state.block_count);

    // Create blocks array
    cJSON *blocks_array = cJSON_CreateArray();
    cJSON_AddItemToObject(config, "blocks", blocks_array);

    // Add each block
    for (int i = 0; i < s_config_state.block_count; i++) {
        const block_config_entry_t *entry = &s_config_state.blocks[i];
        
        cJSON *block_obj = cJSON_CreateObject();
        cJSON_AddNumberToObject(block_obj, "index", i);
        cJSON_AddNumberToObject(block_obj, "i2c_address", entry->i2c_address);
        cJSON_AddNumberToObject(block_obj, "connection_order", entry->connection_order);

        // Create whoami object
        cJSON *whoami_obj = cJSON_CreateObject();
        cJSON_AddItemToObject(block_obj, "whoami", whoami_obj);

        // Add block_type
        const char *block_type_str = block_type_to_json_string(entry->block_type);
        cJSON_AddStringToObject(whoami_obj, "block_type", block_type_str);

        // Add block_id (format: "BLOCK_0x08")
        char block_id[16];
        snprintf(block_id, sizeof(block_id), "BLOCK_0x%02X", entry->i2c_address);
        cJSON_AddStringToObject(whoami_obj, "block_id", block_id);

        // Add firmware_version
        char fw_version[16];
        if (entry->fw_major > 0 || entry->fw_minor > 0) {
            snprintf(fw_version, sizeof(fw_version), "%d.%d", entry->fw_major, entry->fw_minor);
        } else {
            snprintf(fw_version, sizeof(fw_version), "1.0.0");
        }
        cJSON_AddStringToObject(whoami_obj, "firmware_version", fw_version);

        // Add capabilities (empty array for now)
        add_capabilities_array(whoami_obj, entry->caps);

        cJSON_AddItemToArray(blocks_array, block_obj);
    }

    // Create errors array
    cJSON *errors_array = cJSON_CreateArray();
    cJSON_AddItemToObject(config, "errors", errors_array);

    // Add errors
    for (int i = 0; i < s_config_state.block_count; i++) {
        const block_config_entry_t *entry = &s_config_state.blocks[i];
        
        if (!entry->present) {
            char error_msg[64];
            snprintf(error_msg, sizeof(error_msg), "Block at 0x%02X failed communication", entry->i2c_address);
            add_error_to_json(errors_array, "communication", error_msg, i, entry->i2c_address);
        } else if (entry->block_type == BLOCK_TYPE_UNKNOWN) {
            char error_msg[64];
            snprintf(error_msg, sizeof(error_msg), "Block at 0x%02X returned invalid WHOAMI", entry->i2c_address);
            add_error_to_json(errors_array, "invalid_whoami", error_msg, i, entry->i2c_address);
        }
    }

    // Check for missing blocks
    if (s_previous_state_valid) {
        for (int i = 0; i < s_previous_state.block_count; i++) {
            bool found = false;
            for (int j = 0; j < s_config_state.block_count; j++) {
                if (s_previous_state.blocks[i].i2c_address == s_config_state.blocks[j].i2c_address) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                char error_msg[64];
                snprintf(error_msg, sizeof(error_msg), "Block at 0x%02X is missing", s_previous_state.blocks[i].i2c_address);
                add_error_to_json(errors_array, "missing_block", error_msg, -1, s_previous_state.blocks[i].i2c_address);
            }
        }
    }

    // Convert to JSON string
    char *json_string = cJSON_Print(root);
    if (json_string == NULL) {
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }

    // Copy to buffer (truncate if necessary)
    size_t json_len = strlen(json_string);
    if (json_len >= buffer_size) {
        ESP_LOGW(TAG, "JSON buffer too small (%d < %d), truncating", buffer_size, json_len);
        json_len = buffer_size - 1;
    }
    memcpy(json_buffer, json_string, json_len);
    json_buffer[json_len] = '\0';

    // Cleanup
    free(json_string);
    cJSON_Delete(root);

    ESP_LOGI(TAG, "Generated JSON (%d bytes): %s", json_len, json_buffer);

    return ESP_OK;
}
