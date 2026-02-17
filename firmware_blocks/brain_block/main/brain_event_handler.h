// Brain block event handler and execution-gate state.

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "block_config_manager.h"

typedef struct {
    bool app_config_valid;
    uint64_t last_validation_ts_ms;
    uint32_t last_error_count;
    bool has_received_validation;
} brain_validation_state_t;

void brain_event_handler_init(void);
void brain_event_handler_reset_validation(void);
void brain_event_handler_set_config_validation(bool is_valid, uint32_t error_count, uint64_t timestamp_ms);
const brain_validation_state_t *brain_event_handler_get_validation_state(void);
bool brain_event_handler_can_start_execution(void);

void brain_event_handler_refresh_config_event_map(const block_event_map_t *event_map);
const block_event_map_t *brain_event_handler_get_config_event_map(void);

void brain_event_handle_message(const char *message);
void brain_event_handle_block_event(uint8_t block_addr,
                                    uint8_t event_id,
                                    const uint8_t *payload,
                                    size_t payload_len);
