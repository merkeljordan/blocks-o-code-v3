// Brain block event handler and execution-gate state.

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "block_config_manager.h"

typedef struct {
    bool app_config_valid;
    uint64_t last_validation_ts_ms;
    uint32_t last_error_count;
    bool has_received_validation;
} brain_validation_state_t;

typedef enum {
    EXECUTOR_IDLE = 0,
    EXECUTOR_RUNNING,
    EXECUTOR_WAIT_INPUT,
    EXECUTOR_WAIT_DELAY,
    EXECUTOR_STOPPED,
    EXECUTOR_DONE
} brain_executor_state_t;

typedef struct {
    uint8_t color_id;
    uint8_t note_id;
    uint8_t music_sequence_id;
    uint16_t loop_count;
    uint32_t delay_ms;
} brain_executor_params_t;

#define BRAIN_EXECUTOR_MAX_PROGRAM_BLOCKS BLOCK_CONFIG_MAX_BLOCKS
#define BRAIN_EXECUTOR_MAX_LOOP_DEPTH 8

typedef struct {
    uint8_t loop_start_pc;
    uint8_t loop_end_pc;
    uint16_t remaining_iterations;
} brain_loop_frame_t;

typedef struct {
    brain_executor_state_t state;
    block_type_t program[BRAIN_EXECUTOR_MAX_PROGRAM_BLOCKS];
    uint8_t program_len;
    uint8_t pc;
    brain_loop_frame_t loop_stack[BRAIN_EXECUTOR_MAX_LOOP_DEPTH];
    uint8_t loop_depth;
    uint64_t wait_until_ms;
    bool stop_requested;
    bool button_pressed;
} brain_executor_context_t;

void brain_event_handler_init(void);
void brain_event_handler_reset_validation(void);
void brain_event_handler_set_config_validation(bool is_valid, uint32_t error_count, uint64_t timestamp_ms);
const brain_validation_state_t *brain_event_handler_get_validation_state(void);
bool brain_event_handler_can_start_execution(void);

void brain_event_handler_refresh_config_event_map(const block_event_map_t *event_map);
const block_event_map_t *brain_event_handler_get_config_event_map(void);

void brain_executor_set_params(const brain_executor_params_t *params);
const brain_executor_context_t *brain_executor_get_context(void);
void brain_executor_set_button_state(bool is_pressed);
esp_err_t brain_executor_start(void);
void brain_executor_stop(void);
void brain_executor_tick(void);

void brain_event_handle_message(const char *message);
void brain_event_handle_block_event(uint8_t block_addr,
                                    uint8_t event_id,
                                    const uint8_t *payload,
                                    size_t payload_len);
