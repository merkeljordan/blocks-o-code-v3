// Brain block event handler skeleton.
// Add event types and routing as the system grows.

#pragma once

#include <stddef.h>
#include <stdint.h>

void brain_event_handler_init(void);
void brain_event_handle_message(const char *message);
void brain_event_handle_block_event(uint8_t block_addr,
                                    uint8_t event_id,
                                    const uint8_t *payload,
                                    size_t payload_len);
