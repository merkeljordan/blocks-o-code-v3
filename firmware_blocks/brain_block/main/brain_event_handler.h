// Brain block event handler and execution-gate state.
// Brain block event handler:
// Routes app text commands and child block events into Brain-side actions.

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
    /** Paused on a BUTTON program step until that block's I2C address reports BUTTON_PRESS. */
    EXECUTOR_WAIT_INPUT,
    /** DELAY: pc stays on the delay opcode until wait_until_ms. */
    EXECUTOR_WAIT_DELAY,
    EXECUTOR_STOPPED,
    EXECUTOR_DONE
} brain_executor_state_t;

typedef struct {
    uint8_t color_id;
    uint8_t note_id;
    uint8_t note_seq_len;      // 0 => use note_id, >0 => use note_seq[]
    uint8_t note_seq[15];      // ordered notes 0..6 (max sequence length)
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
    /** Written from any task without the executor mutex (STOP must preempt long NOTE waits). */
    volatile bool stop_requested;
    bool button_pressed;
} brain_executor_context_t;

typedef struct {
    brain_runtime_broadcast_state_t state;
    uint8_t pc;
    block_type_t step_type;
    uint64_t updated_at_ms;
} brain_runtime_snapshot_t;

void brain_event_handler_init(void);
void brain_event_handler_reset_validation(void);
void brain_event_handler_set_config_validation(bool is_valid, uint32_t error_count, uint64_t timestamp_ms);
const brain_validation_state_t *brain_event_handler_get_validation_state(void);
bool brain_event_handler_can_start_execution(void);
const brain_runtime_snapshot_t *brain_event_handler_get_runtime_snapshot(void);

void brain_executor_set_params(const brain_executor_params_t *params);
const brain_executor_context_t *brain_executor_get_context(void);
/** Test hook / legacy: prefer real BUTTON_PRESS events (IF uses bound-button latch). */
void brain_executor_set_button_state(bool is_pressed);
esp_err_t brain_executor_start(void);
void brain_executor_stop(void);
void brain_executor_tick(void);

/** True while the executor is actively running a program (including DELAY / BUTTON wait). Background I²C (config scan, event poll) should use longer intervals to reduce bus contention with dispatch. */
bool brain_executor_prefers_i2c_yield(void);

// App/host text command entry point (e.g., "START", "STOP", "SET_LED 0x08 7").
// START/STOP are handled immediately; return true only if the executor accepted them.
// Other commands return true if enqueued successfully.
bool brain_event_handle_message(const char *message);

// Child block event entry point.
// Returns true if queued/handled, false otherwise.
bool brain_event_handle_block_event(uint8_t block_addr,
                                    uint8_t event_id,
                                    const uint8_t *payload,
                                    size_t payload_len);

// Known block-originated event IDs.
#define BRAIN_BLOCK_EVENT_SELECTION_SUBMIT  0x01
#define BRAIN_BLOCK_EVENT_LOOP_COUNT_SUBMIT 0x02
#define BRAIN_BLOCK_EVENT_DELAY_MS_SUBMIT   0x03
#define BRAIN_BLOCK_EVENT_BUTTON_PRESS      0x04

// Block-originated payload layouts (payload passed to brain_event_handle_block_event()).
//
// - SELECTION_SUBMIT: payload[0] = selection digit (uint8_t)
// - LOOP_COUNT_SUBMIT: payload[0] = loop_count (uint8_t, 0 treated as 1 by executor)
// - DELAY_MS_SUBMIT: payload[0..3] = delay_ms (uint32_t little-endian)
// - BUTTON_PRESS: payload[0] = pressed (uint8_t; any nonzero treated as pressed)
#define BRAIN_BLOCK_EVENT_SELECTION_SUBMIT_PAYLOAD_LEN  1
#define BRAIN_BLOCK_EVENT_LOOP_COUNT_SUBMIT_PAYLOAD_LEN 1
#define BRAIN_BLOCK_EVENT_DELAY_MS_SUBMIT_PAYLOAD_LEN   4
#define BRAIN_BLOCK_EVENT_BUTTON_PRESS_PAYLOAD_LEN      1
