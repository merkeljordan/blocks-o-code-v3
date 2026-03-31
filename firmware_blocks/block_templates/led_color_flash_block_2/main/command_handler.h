#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * command_handler module (child-side, not Brain-side):
 *
 * Purpose:
 * - Parse Brain-issued I2C commands (via handle_command).
 * - Maintain local execution status for REG_STATUS reads.
 * - Decouple UI/I2C from LED timing with an internal action queue.
 *
 * Architecture note:
 * - Brain remains the global orchestrator.
 * - This module is the local executor for LED Flash block actions.
 */

// Create queue/task used for asynchronous LED actions (preview/execute).
esp_err_t command_handler_init(void);

// Parse one incoming command packet from I2C.
void handle_command(uint8_t *buffer, int len);

// Payload returned when Brain sends CMD_GET_DATA.
size_t get_data_payload(uint8_t *out, size_t max_len);

// Live status read by i2c_comm when Brain reads REG_STATUS.
uint8_t command_handler_get_status(void);

// Queue non-blocking preview flash (used by local TFT number tap).
bool command_handler_enqueue_preview(uint8_t digit);

// Queue full local execute flow (used by legacy/local-only flows).
bool command_handler_enqueue_execute_digit(uint8_t digit);

// Publish submit intent for Brain orchestration (sets STATUS_DATA_READY + payload).
bool command_handler_submit_selection(uint8_t digit);

#ifdef __cplusplus
}
#endif
