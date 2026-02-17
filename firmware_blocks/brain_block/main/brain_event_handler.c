// Brain block event handler skeleton.
// Implement message parsing + routing here.

#include "brain_event_handler.h"

#include <string.h>
#include "esp_log.h"

static const char *TAG = "brain_evt";
static brain_validation_state_t s_validation_state;
static block_event_map_t s_event_map;

static void set_default_validation_state(void) {
    memset(&s_validation_state, 0, sizeof(s_validation_state));
    s_validation_state.app_config_valid = false;
    s_validation_state.has_received_validation = false;
}

void brain_event_handler_init(void) {
    set_default_validation_state();
    memset(&s_event_map, 0, sizeof(s_event_map));
    ESP_LOGI(TAG, "brain_event_handler_init");
}

void brain_event_handler_reset_validation(void) {
    set_default_validation_state();
    ESP_LOGW(TAG, "Validation state reset to invalid");
}

void brain_event_handler_set_config_validation(bool is_valid, uint32_t error_count, uint64_t timestamp_ms) {
    s_validation_state.app_config_valid = is_valid;
    s_validation_state.last_error_count = error_count;
    s_validation_state.last_validation_ts_ms = timestamp_ms;
    s_validation_state.has_received_validation = true;
    ESP_LOGI(TAG, "Validation updated: valid=%s errors=%lu ts=%llu",
             is_valid ? "true" : "false",
             (unsigned long)error_count,
             (unsigned long long)timestamp_ms);
}

const brain_validation_state_t *brain_event_handler_get_validation_state(void) {
    return &s_validation_state;
}

bool brain_event_handler_can_start_execution(void) {
    return s_validation_state.has_received_validation && s_validation_state.app_config_valid;
}

void brain_event_handler_refresh_config_event_map(const block_event_map_t *event_map) {
    if (event_map == NULL) {
        memset(&s_event_map, 0, sizeof(s_event_map));
        return;
    }

    memcpy(&s_event_map, event_map, sizeof(s_event_map));
    ESP_LOGI(TAG, "Config event map refreshed: seq=%u", s_event_map.sequence_count);
}

const block_event_map_t *brain_event_handler_get_config_event_map(void) {
    return &s_event_map;
}

void brain_event_handle_message(const char *message) {
    (void)message;
    // TODO: parse app/host messages and route to handlers
}

void brain_event_handle_block_event(uint8_t block_addr,
                                    uint8_t event_id,
                                    const uint8_t *payload,
                                    size_t payload_len) {
    (void)block_addr;
    (void)event_id;
    (void)payload;
    (void)payload_len;
    // TODO: react to block-side events
}
