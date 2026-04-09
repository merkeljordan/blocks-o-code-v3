#include "esp_err.h"
#include "esp_log.h"
#include <stdint.h>
#include <string.h>
#include "block_config_manager.h"
#include "device_registry.h"
#include "status_strip.h"
#include "audio_speaker.h"

static const char* TAG = "MOCK_COMPONENTS";

// Global mock configuration state
static block_config_state_t s_mock_config;

// Test helper to inject simulated blocks
void mock_set_config(const block_config_state_t* cfg) {
    if (cfg) {
        s_mock_config = *cfg;
    }
}

// =================================================================================
// block_config_manager.h
// =================================================================================

const block_config_state_t* block_config_manager_get_state(void) {
    return &s_mock_config;
}

esp_err_t block_config_manager_get_state_snapshot(block_config_state_t *out_state) {
    if (out_state == NULL) return ESP_FAIL;
    *out_state = s_mock_config;
    return ESP_OK;
}

const char* block_type_to_json_string(block_type_t type) {
    return "mock_type"; // simplified
}

// =================================================================================
// device_registry.h
// =================================================================================

static device_entry_t s_mock_device;

const device_entry_t *device_registry_find(uint8_t addr) {
    // For simplicity, we just look inside our mock config and return a valid device if it's there.
    for (int i=0; i<s_mock_config.block_count; i++) {
        if (s_mock_config.blocks[i].i2c_address == addr) {
            s_mock_device.address = addr;
            s_mock_device.type = s_mock_config.blocks[i].block_type;
            s_mock_device.present = s_mock_config.blocks[i].present;
            return &s_mock_device;
        }
    }
    return NULL;
}

esp_err_t device_registry_scan(void) {
    return ESP_OK;
}

void device_registry_init(void) {
    // no-op
}

const device_registry_t *device_registry_get_state(void) {
    return NULL;
}

// =================================================================================
// status_strip.h
// =================================================================================

void status_strip_play_runtime_audio_event(brain_runtime_broadcast_state_t state, uint8_t pc, uint8_t step_type) {
    // no-op 
}

// =================================================================================
// audio_speaker.h
// =================================================================================

esp_err_t speaker_play_tone(uint32_t freq_hz, uint32_t duration_ms) {
    ESP_LOGI(TAG, "speaker_play_tone(%lu Hz, %lu ms)", freq_hz, duration_ms);
    return ESP_OK;
}
