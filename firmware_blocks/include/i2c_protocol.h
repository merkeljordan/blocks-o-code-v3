#pragma once
#ifndef I2C_PROTOCOL_H
#define I2C_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>
#include "driver/i2c.h"  // Add this for I2C_NUM_0

// ============================================================================
// I2C HARDWARE CONFIGURATION
// ============================================================================
#define I2C_PORT_NUM    I2C_NUM_0
#define I2C_SDA_PIN     21
#define I2C_SCL_PIN     22
#define I2C_FREQ_HZ     100000 // 100 kHz

// ============================================================================
// REGISTER MAP (Brain -> Child reads)
// ============================================================================

#define REG_WHOAMI      0x00    // 1 byte: block_type_t
#define REG_STATUS      0x01    // 1 byte: STATUS_* flags
#define REG_FW_MAJOR    0x02    // 1 byte (optional, for later)
#define REG_FW_MINOR    0x03    // 1 byte (optional, for later)
#define REG_CAPS        0x04    // 1 byte (optional, for later)
// Optional: length of CMD_GET_DATA event frame when STATUS_DATA_READY is set.
// 0 when no event is pending.
// NOTE: For NOTE block sequences we support up to 15 notes:
//   payload = [event_id, count, note0..note14] => 17 bytes total.
#define REG_DATA_LEN    0x05    // 1 byte: 0..32

// Optional: keep for legacy/one-off features (if you already use it)
#define CMD_OLED_TEXT   0xF1

// ============================================================================
// COMMAND DEFINITIONS (Brain -> Child)
// NOTE: Commands are "do something". Registers are "tell me who you are".
// IMPORTANT: Keep command opcodes out of the register range (< 0x10) so child
// parsers can deterministically distinguish register reads from command packets.
// ============================================================================

typedef enum {
    CMD_PING            = 0x80,  // Check if device is alive
    CMD_GET_TYPE        = 0x81,  // (legacy) Request block type
    CMD_SET_LED         = 0x82,  // Set LED color (RGB)
    CMD_GET_STATUS      = 0x83,  // Request status
    CMD_GET_DATA        = 0x84,  // Request sensor/input data
    CMD_PLAY_NOTE       = 0x85,  // Play musical note
    CMD_EXECUTE         = 0x86,  // Execute block action
    CMD_RESET           = 0x87,  // Reset block state
    CMD_SET_DELAY       = 0x88,  // Set delay time
    CMD_SET_LOOP        = 0x89,  // Set loop count

    // LED MATRIX COMMANDS
    CMD_MATRIX_FILL         = 0x90,
    CMD_MATRIX_SET_PIXEL    = 0x91,
    CMD_MATRIX_CLEAR        = 0x92,
    CMD_MATRIX_SET_ROW      = 0x93,
    CMD_MATRIX_SET_COLUMN   = 0x94,
    CMD_MATRIX_DRAW_PATTERN = 0x95,
    CMD_MATRIX_BRIGHTNESS   = 0x96,
    CMD_MATRIX_SHOW         = 0x97,
} i2c_command_t;

// ============================================================================
// BLOCK TYPES (15 total: 1 Brain + 14 Child)
// ============================================================================

typedef enum {
    // System
    BLOCK_TYPE_BRAIN       = 0x00,

    // Control Flow Blocks
    BLOCK_TYPE_IF          = 0x10,
    BLOCK_TYPE_THEN        = 0x11,
    BLOCK_TYPE_END_IF      = 0x12,
    BLOCK_TYPE_LOOP        = 0x13,
    BLOCK_TYPE_END_LOOP    = 0x14,
    BLOCK_TYPE_DELAY       = 0x15,

    // Input Blocks
    BLOCK_TYPE_BUTTON      = 0x20,

    // Output Blocks
    BLOCK_TYPE_NOTE        = 0x30,
    BLOCK_TYPE_MUSIC_SEQ   = 0x31,
    BLOCK_TYPE_LED_FLASH   = 0x32,
    BLOCK_TYPE_DISCO       = 0x33,

    BLOCK_TYPE_UNKNOWN     = 0xFF
} block_type_t;

// ============================================================================
// LED MATRIX PATTERNS
// ============================================================================

typedef enum {
    PATTERN_HEART       = 0x00,
    PATTERN_SMILE       = 0x01,
    PATTERN_ARROW_UP    = 0x02,
    PATTERN_ARROW_DOWN  = 0x03,
    PATTERN_ARROW_LEFT  = 0x04,
    PATTERN_ARROW_RIGHT = 0x05,
    PATTERN_CHECKMARK   = 0x06,
    PATTERN_X           = 0x07,
    PATTERN_MUSIC_NOTE  = 0x08,
    PATTERN_STAR        = 0x09,
} led_pattern_t;

// ============================================================================
// STATUS FLAGS
// ============================================================================

#define STATUS_READY        0x01
#define STATUS_BUSY         0x02
#define STATUS_ERROR        0x04
#define STATUS_DATA_READY   0x08

// ============================================================================
// BLOCK -> BRAIN EVENT IDs (returned via CMD_GET_DATA when STATUS_DATA_READY set)
// ============================================================================
// Payload wire format returned by child on CMD_GET_DATA:
//   byte0 = event_id
//   byte1.. = event payload (event-specific)
#define BRAIN_BLOCK_EVENT_SELECTION_SUBMIT  0x01
#define BRAIN_BLOCK_EVENT_LOOP_COUNT_SUBMIT 0x02
#define BRAIN_BLOCK_EVENT_DELAY_MS_SUBMIT   0x03
#define BRAIN_BLOCK_EVENT_BUTTON_PRESS      0x04

#define BRAIN_BLOCK_EVENT_SELECTION_SUBMIT_PAYLOAD_LEN  1
#define BRAIN_BLOCK_EVENT_LOOP_COUNT_SUBMIT_PAYLOAD_LEN 1
#define BRAIN_BLOCK_EVENT_DELAY_MS_SUBMIT_PAYLOAD_LEN   4
#define BRAIN_BLOCK_EVENT_BUTTON_PRESS_PAYLOAD_LEN      1

// ============================================================================
// OPTIONAL UTILITY FUNCTIONS (safe to keep in header)
// ============================================================================

static inline const char* block_type_to_string(block_type_t type) {
    switch (type) {
        case BLOCK_TYPE_BRAIN:      return "BRAIN";
        case BLOCK_TYPE_IF:         return "IF";
        case BLOCK_TYPE_THEN:       return "THEN";
        case BLOCK_TYPE_END_IF:     return "END_IF";
        case BLOCK_TYPE_LOOP:       return "LOOP";
        case BLOCK_TYPE_END_LOOP:   return "END_LOOP";
        case BLOCK_TYPE_DELAY:      return "DELAY";
        case BLOCK_TYPE_BUTTON:     return "BUTTON";
        case BLOCK_TYPE_NOTE:       return "NOTE";
        case BLOCK_TYPE_MUSIC_SEQ:  return "MUSIC_SEQ";
        case BLOCK_TYPE_LED_FLASH:  return "LED_FLASH";
        case BLOCK_TYPE_DISCO:      return "DISCO";
        default:                    return "UNKNOWN";
    }
}

static inline const char* command_to_string(i2c_command_t cmd) {
    switch (cmd) {
        case CMD_PING:                  return "PING";
        case CMD_GET_TYPE:              return "GET_TYPE";
        case CMD_SET_LED:               return "SET_LED";
        case CMD_GET_STATUS:            return "GET_STATUS";
        case CMD_GET_DATA:              return "GET_DATA";
        case CMD_PLAY_NOTE:             return "PLAY_NOTE";
        case CMD_EXECUTE:               return "EXECUTE";
        case CMD_RESET:                 return "RESET";
        case CMD_SET_DELAY:             return "SET_DELAY";
        case CMD_SET_LOOP:              return "SET_LOOP";
        case CMD_MATRIX_FILL:           return "MATRIX_FILL";
        case CMD_MATRIX_SET_PIXEL:      return "MATRIX_SET_PIXEL";
        case CMD_MATRIX_CLEAR:          return "MATRIX_CLEAR";
        case CMD_MATRIX_SET_ROW:        return "MATRIX_SET_ROW";
        case CMD_MATRIX_SET_COLUMN:     return "MATRIX_SET_COLUMN";
        case CMD_MATRIX_DRAW_PATTERN:   return "MATRIX_DRAW_PATTERN";
        case CMD_MATRIX_BRIGHTNESS:     return "MATRIX_BRIGHTNESS";
        case CMD_MATRIX_SHOW:           return "MATRIX_SHOW";
        default:                        return "UNKNOWN_CMD";
    }
}

#endif // I2C_PROTOCOL_H
