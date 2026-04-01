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
static const uint8_t TOPOLOGY_STABLE_SCAN_THRESHOLD = 2;
static const uint8_t APPEND_STABLE_SCAN_THRESHOLD = 3;
static const uint8_t REMOVAL_STABLE_SCAN_THRESHOLD = 4;

// Global configuration state
static block_config_state_t s_config_state;
static block_config_state_t s_previous_state;
static block_config_state_t s_pending_state;
static block_event_map_t s_event_map;

// Initialize previous state to empty
static bool s_previous_state_valid = false;
static bool s_pending_state_valid = false;
static uint32_t s_scan_counter = 0;
static uint8_t s_pending_stable_count = 0;

void block_config_manager_init(void) {
    memset(&s_config_state, 0, sizeof(s_config_state));
    memset(&s_previous_state, 0, sizeof(s_previous_state));
    memset(&s_pending_state, 0, sizeof(s_pending_state));
    memset(&s_event_map, 0, sizeof(s_event_map));
    s_previous_state_valid = false;
    s_pending_state_valid = false;
    s_scan_counter = 0;
    s_pending_stable_count = 0;
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
        case BLOCK_TYPE_DELAY:      return "delay_block";
        default:                    return "unknown";
    }
}

static void read_optional_block_metadata(uint8_t address, block_config_entry_t *entry) {
    if (entry == NULL || !entry->present || entry->block_type == BLOCK_TYPE_UNKNOWN) {
        return;
    }

    esp_err_t ret;

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

    // In the preserved-order model, visible stack order is meaningful.
    // Transport address is diagnostic metadata only and must not by itself
    // cause the app/program order to appear changed.
    for (int i = 0; i < curr->block_count; i++) {
        if (prev->blocks[i].device_uid != curr->blocks[i].device_uid ||
            prev->blocks[i].block_type != curr->blocks[i].block_type) {
            return true;
        }
    }
    return false;
}

static bool is_append_only_extension(const block_config_state_t *prev,
                                     const block_config_state_t *curr) {
    if (prev == NULL || curr == NULL) {
        return false;
    }
    if (curr->block_count < prev->block_count) {
        return false;
    }

    for (int i = 0; i < prev->block_count; i++) {
        if (prev->blocks[i].device_uid != curr->blocks[i].device_uid ||
            prev->blocks[i].block_type != curr->blocks[i].block_type ||
            prev->blocks[i].i2c_address != curr->blocks[i].i2c_address) {
            return false;
        }
    }
    return (curr->block_count > prev->block_count);
}

static bool is_removal_only_change(const block_config_state_t *prev,
                                   const block_config_state_t *curr) {
    if (prev == NULL || curr == NULL) {
        return false;
    }
    if (curr->block_count >= prev->block_count) {
        return false;
    }

    int curr_idx = 0;
    for (int prev_idx = 0; prev_idx < prev->block_count; prev_idx++) {
        if (curr_idx >= curr->block_count) {
            return true;
        }
        if (prev->blocks[prev_idx].device_uid == curr->blocks[curr_idx].device_uid &&
            prev->blocks[prev_idx].block_type == curr->blocks[curr_idx].block_type) {
            curr_idx++;
        }
    }

    return (curr_idx == curr->block_count);
}

static bool is_input_block_type(block_type_t type) {
    return (type == BLOCK_TYPE_BUTTON);
}

static bool is_output_or_delay_block_type(block_type_t type) {
    return (type == BLOCK_TYPE_LED_FLASH ||
            type == BLOCK_TYPE_NOTE ||
            type == BLOCK_TYPE_MUSIC_SEQ ||
            type == BLOCK_TYPE_DISCO ||
            type == BLOCK_TYPE_DELAY);
}

static bool is_known_child_type(block_type_t type) {
    return type != BLOCK_TYPE_UNKNOWN && type != BLOCK_TYPE_BRAIN;
}

static int find_scanned_entry_index_by_uid(const block_config_entry_t *entries,
                                           uint8_t count,
                                           uint32_t device_uid) {
    for (int i = 0; i < count; i++) {
        if (entries[i].device_uid == device_uid) {
            return i;
        }
    }
    return -1;
}

static const block_config_entry_t *find_committed_entry_by_address(const block_config_state_t *state,
                                                                   uint8_t address) {
    if (state == NULL) {
        return NULL;
    }

    for (int i = 0; i < state->block_count; i++) {
        if (state->blocks[i].i2c_address == address) {
            return &state->blocks[i];
        }
    }
    return NULL;
}

static const block_config_entry_t *find_committed_entry_by_uid(const block_config_state_t *state,
                                                               uint32_t device_uid) {
    if (state == NULL || device_uid == 0u) {
        return NULL;
    }

    for (int i = 0; i < state->block_count; i++) {
        if (state->blocks[i].device_uid == device_uid) {
            return &state->blocks[i];
        }
    }
    return NULL;
}

static void assign_stable_stack_order(const block_config_state_t *committed_state,
                                      block_config_state_t *out_state,
                                      block_config_entry_t *scanned_entries,
                                      uint8_t scanned_count) {
    if (out_state == NULL) {
        return;
    }
    if (scanned_entries == NULL) {
        out_state->block_count = 0;
        return;
    }

    block_config_entry_t ordered_entries[BLOCK_CONFIG_MAX_BLOCKS];
    bool used_scanned[BLOCK_CONFIG_MAX_BLOCKS] = {0};
    uint8_t ordered_count = 0;

    /*
     * Preserved-order model:
     * - keep the current committed known-block order whenever those blocks are
     *   still present
     * - append newly discovered known blocks after the committed known prefix
     * - append unknown/unstable entries last for visibility only
     *
     * This is the original "preserve first seen" behavior in spirit: order is
     * owned by the committed visible stack, not by hashed/bootstrap address.
     */
    if (committed_state != NULL) {
        for (int i = 0; i < committed_state->block_count && ordered_count < scanned_count; i++) {
            const block_config_entry_t *prev = &committed_state->blocks[i];
            if (!is_known_child_type(prev->block_type)) {
                continue;
            }

            int scanned_idx = find_scanned_entry_index_by_uid(scanned_entries,
                                                              scanned_count,
                                                              prev->device_uid);
            if (scanned_idx < 0 || used_scanned[scanned_idx]) {
                continue;
            }
            if (!is_known_child_type(scanned_entries[scanned_idx].block_type)) {
                continue;
            }

            ordered_entries[ordered_count] = scanned_entries[scanned_idx];
            ordered_entries[ordered_count].connection_order = ordered_count;
            used_scanned[scanned_idx] = true;
            ordered_count++;
        }
    }

    for (int i = 0; i < scanned_count && ordered_count < scanned_count; i++) {
        if (used_scanned[i]) {
            continue;
        }
        if (!is_known_child_type(scanned_entries[i].block_type)) {
            continue;
        }
        ordered_entries[ordered_count] = scanned_entries[i];
        ordered_entries[ordered_count].connection_order = ordered_count;
        used_scanned[i] = true;
        ordered_count++;
    }

    for (int i = 0; i < scanned_count && ordered_count < scanned_count; i++) {
        if (used_scanned[i]) {
            continue;
        }
        ordered_entries[ordered_count] = scanned_entries[i];
        ordered_entries[ordered_count].connection_order = ordered_count;
        ordered_count++;
    }

    memset(&out_state->blocks, 0, sizeof(out_state->blocks));
    memcpy(out_state->blocks, ordered_entries, sizeof(block_config_entry_t) * ordered_count);
    out_state->block_count = ordered_count;
}

static void recompute_event_map_from_config(void) {
    memset(&s_event_map, 0, sizeof(s_event_map));
    s_event_map.generated_at_ms = s_config_state.last_scan_timestamp;
    s_event_map.is_empty = (s_config_state.block_count == 0);

    int open_if_stack[BLOCK_CONFIG_MAX_BLOCKS];
    int open_loop_stack[BLOCK_CONFIG_MAX_BLOCKS];
    int if_top = -1;
    int loop_top = -1;

    for (int i = 0; i < s_config_state.block_count && i < BLOCK_CONFIG_MAX_BLOCKS; i++) {
        block_type_t type = s_config_state.blocks[i].block_type;

        if (type == BLOCK_TYPE_IF) {
            s_event_map.if_start_count++;
            if (s_event_map.sequence_count < BLOCK_CONFIG_MAX_BLOCKS) {
                int seq_idx = s_event_map.sequence_count++;
                block_sequence_metadata_t *seq = &s_event_map.sequences[seq_idx];
                memset(seq, 0, sizeof(*seq));
                seq->sequence_type = BLOCK_SEQUENCE_IF;
                seq->start_index = (uint8_t)i;
                seq->end_index = (uint8_t)i;
                if (if_top < (BLOCK_CONFIG_MAX_BLOCKS - 1)) {
                    open_if_stack[++if_top] = seq_idx;
                }
            }
            continue;
        }

        if (type == BLOCK_TYPE_LOOP) {
            s_event_map.loop_start_count++;
            if (s_event_map.sequence_count < BLOCK_CONFIG_MAX_BLOCKS) {
                int seq_idx = s_event_map.sequence_count++;
                block_sequence_metadata_t *seq = &s_event_map.sequences[seq_idx];
                memset(seq, 0, sizeof(*seq));
                seq->sequence_type = BLOCK_SEQUENCE_LOOP;
                seq->start_index = (uint8_t)i;
                seq->end_index = (uint8_t)i;
                if (loop_top < (BLOCK_CONFIG_MAX_BLOCKS - 1)) {
                    open_loop_stack[++loop_top] = seq_idx;
                }
            }
            continue;
        }

        if (type == BLOCK_TYPE_END_IF) {
            s_event_map.if_end_count++;
            if (if_top >= 0) {
                int seq_idx = open_if_stack[if_top--];
                block_sequence_metadata_t *seq = &s_event_map.sequences[seq_idx];
                seq->end_index = (uint8_t)i;
                seq->has_end_boundary = true;
            }
            continue;
        }

        if (type == BLOCK_TYPE_END_LOOP) {
            s_event_map.loop_end_count++;
            if (loop_top >= 0) {
                int seq_idx = open_loop_stack[loop_top--];
                block_sequence_metadata_t *seq = &s_event_map.sequences[seq_idx];
                seq->end_index = (uint8_t)i;
                seq->has_end_boundary = true;
            }
            continue;
        }

        for (int j = 0; j <= if_top; j++) {
            int seq_idx = open_if_stack[j];
            if (is_input_block_type(type)) {
                s_event_map.sequences[seq_idx].has_input = true;
                s_event_map.sequences[seq_idx].input_count++;
            }
            if (is_output_or_delay_block_type(type)) {
                s_event_map.sequences[seq_idx].has_output_or_delay = true;
                s_event_map.sequences[seq_idx].output_or_delay_count++;
            }
        }

        for (int j = 0; j <= loop_top; j++) {
            int seq_idx = open_loop_stack[j];
            if (is_input_block_type(type)) {
                s_event_map.sequences[seq_idx].has_input = true;
                s_event_map.sequences[seq_idx].input_count++;
            }
            if (is_output_or_delay_block_type(type)) {
                s_event_map.sequences[seq_idx].has_output_or_delay = true;
                s_event_map.sequences[seq_idx].output_or_delay_count++;
            }
        }
    }
}

esp_err_t block_config_manager_scan(void) {
    ESP_LOGI(TAG, "=== BLOCK CONFIGURATION SCAN ===");

    block_config_state_t committed_state;
    memcpy(&committed_state, &s_config_state, sizeof(committed_state));

    // Build candidate state from the latest scan without immediately
    // overwriting the committed visible order.
    block_config_state_t candidate_state;
    memset(&candidate_state, 0, sizeof(candidate_state));
    candidate_state.scan_id = ++s_scan_counter;

    // Use device registry to scan I2C bus
    device_registry_scan();
    const device_registry_t *registry = device_registry_get();

    // Process detected devices, then place them into a committed visible stack
    // so transport-address churn does not reshuffle the app order.
    block_config_entry_t scanned_entries[BLOCK_CONFIG_MAX_BLOCKS];
    uint8_t scanned_count = 0;
    for (int i = 0; i < DEVICE_REGISTRY_MAX_DEVICES; i++) {
        const device_entry_t *entry = &registry->devices[i];
        if (!entry->present) {
            continue;
        }

        if (scanned_count >= BLOCK_CONFIG_MAX_BLOCKS) {
            ESP_LOGW(TAG, "Maximum block count reached, skipping additional blocks");
            break;
        }

        if (entry->type == BLOCK_TYPE_BRAIN) {
            candidate_state.error_count++;
            ESP_LOGW(TAG, "Ignoring invalid child WHOAMI=BRAIN at 0x%02X", entry->address);
            continue;
        }

        block_config_entry_t *config_entry = &scanned_entries[scanned_count];
        config_entry->i2c_address = entry->address;
        config_entry->device_uid = entry->uid;
        config_entry->connection_order = scanned_count;
        config_entry->fw_major = 0;
        config_entry->fw_minor = 0;
        config_entry->caps = 0;

        // Use authoritative scan result for type/address from registry, but
        // pin last-known good identity harder when the same address is still
        // responding and current WHOAMI/UID data is unstable.
        block_type_t effective_type = entry->type;
        uint32_t effective_uid = entry->uid;
        if (entry->present && s_previous_state_valid) {
            const block_config_entry_t *prev_by_addr = find_committed_entry_by_address(&committed_state,
                                                                                        entry->address);
            if (prev_by_addr != NULL &&
                prev_by_addr->block_type != BLOCK_TYPE_UNKNOWN &&
                entry->type == BLOCK_TYPE_UNKNOWN) {
                ESP_LOGW(TAG,
                         "Identity unstable at 0x%02X, keeping previous block type 0x%02X",
                         entry->address, prev_by_addr->block_type);
                effective_type = prev_by_addr->block_type;
            }
            if (effective_uid == 0u &&
                prev_by_addr != NULL &&
                prev_by_addr->device_uid != 0u) {
                ESP_LOGW(TAG,
                         "UID unstable at 0x%02X, keeping previous uid 0x%08lX",
                         entry->address,
                         (unsigned long)prev_by_addr->device_uid);
                effective_uid = prev_by_addr->device_uid;
            }
        }

        config_entry->block_type = effective_type;
        config_entry->device_uid = effective_uid;
        config_entry->present = entry->present;

        const block_config_entry_t *prev_meta = find_committed_entry_by_uid(&committed_state,
                                                                            config_entry->device_uid);
        if (prev_meta != NULL) {
            config_entry->fw_major = prev_meta->fw_major;
            config_entry->fw_minor = prev_meta->fw_minor;
            config_entry->caps = prev_meta->caps;
        }

        if (entry->present && entry->type == BLOCK_TYPE_UNKNOWN) {
            candidate_state.error_count++;
            ESP_LOGW(TAG, "Block at 0x%02X has unknown type (from device registry WHOAMI)", entry->address);
        }
        scanned_count++;
    }

    assign_stable_stack_order(s_previous_state_valid ? &committed_state : NULL,
                              &candidate_state,
                              scanned_entries,
                              scanned_count);

    // Check for missing blocks (blocks that were present before but not now)
    if (s_previous_state_valid) {
        for (int i = 0; i < s_previous_state.block_count; i++) {
            bool found = false;
            for (int j = 0; j < candidate_state.block_count; j++) {
                if (s_previous_state.blocks[i].device_uid == candidate_state.blocks[j].device_uid) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                candidate_state.error_count++;
                ESP_LOGW(TAG, "Block at 0x%02X is missing", s_previous_state.blocks[i].i2c_address);
            }
        }
    }

    // Update timestamp on the candidate snapshot first.
    candidate_state.last_scan_timestamp = esp_timer_get_time() / 1000; // Convert to milliseconds

    bool candidate_changed = false;
    if (s_previous_state_valid) {
        candidate_changed = compare_configurations(&committed_state, &candidate_state);
    } else {
        candidate_changed = true; // First scan always counts as change
    }

    bool append_only_change = s_previous_state_valid &&
                              is_append_only_extension(&committed_state, &candidate_state);
    bool removal_only_change = s_previous_state_valid &&
                               is_removal_only_change(&committed_state, &candidate_state);

    bool commit_candidate = false;
    if (!s_previous_state_valid) {
        commit_candidate = true;
    } else if (!candidate_changed) {
        commit_candidate = true;
        s_pending_state_valid = false;
        s_pending_stable_count = 0;
    } else if (s_pending_state_valid &&
               !compare_configurations(&s_pending_state, &candidate_state)) {
        if (s_pending_stable_count < UINT8_MAX) {
            s_pending_stable_count++;
        }
        uint8_t required_stable_scans = TOPOLOGY_STABLE_SCAN_THRESHOLD;
        if (append_only_change) {
            required_stable_scans = APPEND_STABLE_SCAN_THRESHOLD;
        } else if (removal_only_change) {
            required_stable_scans = REMOVAL_STABLE_SCAN_THRESHOLD;
        }
        if (s_pending_stable_count >= required_stable_scans) {
            commit_candidate = true;
        }
    } else {
        memcpy(&s_pending_state, &candidate_state, sizeof(candidate_state));
        s_pending_state_valid = true;
        s_pending_stable_count = 1;
    }

    if (commit_candidate) {
        memcpy(&s_config_state, &candidate_state, sizeof(candidate_state));
        s_config_state.has_changed = candidate_changed;
        for (int i = 0; i < s_config_state.block_count; i++) {
            block_config_entry_t *entry = &s_config_state.blocks[i];
            if (!entry->present || entry->block_type == BLOCK_TYPE_UNKNOWN) {
                continue;
            }
            if (entry->fw_major != 0 || entry->fw_minor != 0 || entry->caps != 0) {
                continue;
            }
            read_optional_block_metadata(entry->i2c_address, entry);
        }
        memcpy(&s_previous_state, &committed_state, sizeof(committed_state));
        s_previous_state_valid = true;
        s_pending_state_valid = false;
        s_pending_stable_count = 0;
    } else {
        s_config_state.has_changed = false;
        s_config_state.scan_id = candidate_state.scan_id;
        s_config_state.last_scan_timestamp = candidate_state.last_scan_timestamp;
        s_config_state.error_count = candidate_state.error_count;
    }

    recompute_event_map_from_config();

    ESP_LOGI(TAG,
             "Raw detected=%u, committed=%u, pending=%u, append_only=%s, removal_only=%s",
             scanned_count,
             s_config_state.block_count,
             s_pending_stable_count,
             append_only_change ? "yes" : "no",
             removal_only_change ? "yes" : "no");
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

const block_event_map_t* block_config_manager_get_event_map(void) {
    return &s_event_map;
}

esp_err_t block_config_manager_get_state_snapshot(block_config_state_t *out_state) {
    if (out_state == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(out_state, &s_config_state, sizeof(*out_state));
    return ESP_OK;
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

    // Add total_blocks (include Brain as a synthetic entry)
    int total_blocks = s_config_state.block_count + 1;
    cJSON_AddNumberToObject(config, "total_blocks", total_blocks);

    // Create blocks array
    cJSON *blocks_array = cJSON_CreateArray();
    cJSON_AddItemToObject(config, "blocks", blocks_array);

    // Add Brain block as a synthetic entry (not on I2C bus)
    {
        cJSON *block_obj = cJSON_CreateObject();
        cJSON_AddNumberToObject(block_obj, "index", 0);
        cJSON_AddNumberToObject(block_obj, "i2c_address", 0);
        cJSON_AddNumberToObject(block_obj, "device_uid", 0);
        cJSON_AddNumberToObject(block_obj, "connection_order", -1);

        cJSON *whoami_obj = cJSON_CreateObject();
        cJSON_AddItemToObject(block_obj, "whoami", whoami_obj);
        cJSON_AddStringToObject(whoami_obj, "block_type", "brain_block");
        cJSON_AddStringToObject(whoami_obj, "block_id", "BRAIN");
        cJSON_AddStringToObject(whoami_obj, "firmware_version", "1.0.0");
        add_capabilities_array(whoami_obj, 0);

        cJSON_AddItemToArray(blocks_array, block_obj);
    }

    // Add each detected child block
    for (int i = 0; i < s_config_state.block_count; i++) {
        const block_config_entry_t *entry = &s_config_state.blocks[i];
        
        cJSON *block_obj = cJSON_CreateObject();
        cJSON_AddNumberToObject(block_obj, "index", i + 1);
        cJSON_AddNumberToObject(block_obj, "i2c_address", entry->i2c_address);
        cJSON_AddNumberToObject(block_obj, "device_uid", (double)entry->device_uid);
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
                if (s_previous_state.blocks[i].device_uid == s_config_state.blocks[j].device_uid) {
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
    char *json_string = cJSON_PrintUnformatted(root);
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

    ESP_LOGD(TAG, "Generated JSON (%d bytes): %s", json_len, json_buffer);

    return ESP_OK;
}
