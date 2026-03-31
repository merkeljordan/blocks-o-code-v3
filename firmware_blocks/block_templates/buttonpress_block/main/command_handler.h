#pragma once

#include <stddef.h>
#include <stdint.h>
#include "i2c_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

void handle_command(uint8_t *buffer, int len);
uint8_t command_handler_get_status_flags(void);
void led_status_task(void *arg);

// New control-flow-style command handler used by i2c_comm.c (preferred).
// Note: Keeping handle_command() for compatibility with older templates.
void command_handle(i2c_command_t cmd,
                    const uint8_t *rx,
                    size_t rx_len,
                    uint8_t *tx,
                    size_t *tx_len);
uint8_t button_block_get_status_flags(void);
uint8_t button_block_get_pending_data_len(void);

#ifdef __cplusplus
}
#endif
