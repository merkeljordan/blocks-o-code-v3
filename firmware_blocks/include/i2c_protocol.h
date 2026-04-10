#pragma once
#ifndef I2C_PROTOCOL_H
#define I2C_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>
#include "driver/i2c.h"  // Add this for I2C_NUM_0
#if __has_include("esp_mac.h")
#include "esp_mac.h"
#define BLOCK_HAS_ESP_MAC 1
#else
#define BLOCK_HAS_ESP_MAC 0
#endif

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

#define REG_WHOAMI      0x00    // 1 byte: block_type_t (children may mirror type; Brain uses fixed I2C address)
#define REG_STATUS      0x01    // 1 byte: STATUS_* flags
#define REG_FW_MAJOR    0x02    // 1 byte (optional, for later)
#define REG_FW_MINOR    0x03    // 1 byte (optional, for later)
#define REG_CAPS        0x04    // 1 byte (optional, for later)
// Optional: length of CMD_GET_DATA event frame when STATUS_DATA_READY is set.
// 0 when no event is pending.
// NOTE: For NOTE block sequences we support up to 15 notes:
//   payload = [event_id, count, note0..note14] => 17 bytes total.
#define REG_DATA_LEN    0x05    // 1 byte: 0..32
#define REG_UID0        0x06    // 4 bytes: stable per-device UID (LSB first)
#define REG_UID1        0x07
#define REG_UID2        0x08
#define REG_UID3        0x09
#define REG_ASSIGNED_ADDR 0x0A  // 1 byte: currently active child I2C address
// Loop block: configured iteration count for Brain executor (1..255; 0 treated as 1).
// Other block templates may leave this register at 0; Brain reads it only for BLOCK_TYPE_LOOP.
#define REG_LOOP_COUNT    0x0B
// Delay block: configured wait time in milliseconds (uint32_t little-endian, regs 0x0C..0x0F).
// Brain reads these only for BLOCK_TYPE_DELAY.
#define REG_DELAY_MS0     0x0C
#define REG_DELAY_MS1     0x0D
#define REG_DELAY_MS2     0x0E
#define REG_DELAY_MS3     0x0F

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
    CMD_SET_I2C_ADDRESS = 0x8A,  // Child: rebind slave address (Brain firmware does not send this)

    // LED MATRIX COMMANDS
    CMD_MATRIX_FILL         = 0x90,
    CMD_MATRIX_SET_PIXEL    = 0x91,
    CMD_MATRIX_CLEAR        = 0x92,
    CMD_MATRIX_SET_ROW      = 0x93,
    CMD_MATRIX_SET_COLUMN   = 0x94,
    CMD_MATRIX_DRAW_PATTERN = 0x95,
    CMD_MATRIX_BRIGHTNESS   = 0x96,
    CMD_MATRIX_SHOW         = 0x97,
    CMD_RUNTIME_BROADCAST   = 0x98,
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

    BLOCK_TYPE_UNKNOWN     = 0xFF
} block_type_t;

// Child-block dynamic address window (inclusive): 0x08-0x16
#define CHILD_I2C_ADDR_MIN 0x08u
#define CHILD_I2C_ADDR_MAX 0x16u

// Fixed boot 7-bit I2C addresses: one constant per firmware template under
// firmware_blocks/block_templates/*. Brain infers block_type_t from this address (no readdressing).
// Duplicate SKUs (0x0C..0x12) share one logical type per family. UID uses block_compute_device_uid().
//
// 0x08..0x0B  control-flow (unique type per image)
// 0x0C..0x12  duplicate-type product SKUs (NOTE / MUSIC_SEQ / LED_FLASH variants)
// 0x13..0x16  remaining unique types
#define BLOCK_BOOT_I2C_ADDR_IF_BLOCK                 0x08u
#define BLOCK_BOOT_I2C_ADDR_THEN_BLOCK               0x09u
#define BLOCK_BOOT_I2C_ADDR_END_IF_BLOCK             0x0Au
#define BLOCK_BOOT_I2C_ADDR_LOOP_BLOCK               0x0Bu
#define BLOCK_BOOT_I2C_ADDR_NOTE_BLOCK               0x0Cu
#define BLOCK_BOOT_I2C_ADDR_NOTE_BLOCK_2             0x0Du
#define BLOCK_BOOT_I2C_ADDR_NOTE_BLOCK_3             0x0Eu
#define BLOCK_BOOT_I2C_ADDR_MUSIC_SEQUENCE_BLOCK     0x0Fu
#define BLOCK_BOOT_I2C_ADDR_MUSIC_SEQUENCE_BLOCK_2   0x10u
#define BLOCK_BOOT_I2C_ADDR_LED_COLOR_FLASH_BLOCK    0x11u
#define BLOCK_BOOT_I2C_ADDR_LED_COLOR_FLASH_BLOCK_2  0x12u
#define BLOCK_BOOT_I2C_ADDR_END_LOOP_BLOCK           0x13u
#define BLOCK_BOOT_I2C_ADDR_DELAY_BLOCK              0x14u
#define BLOCK_BOOT_I2C_ADDR_BUTTONPRESS_BLOCK        0x15u

/** Infer canonical child block_type_t from fixed boot I2C address (see BLOCK_BOOT_I2C_ADDR_*). */
static inline block_type_t block_infer_type_from_child_i2c_address(uint8_t address) {
    switch (address) {
        case BLOCK_BOOT_I2C_ADDR_IF_BLOCK:
            return BLOCK_TYPE_IF;
        case BLOCK_BOOT_I2C_ADDR_THEN_BLOCK:
            return BLOCK_TYPE_THEN;
        case BLOCK_BOOT_I2C_ADDR_END_IF_BLOCK:
            return BLOCK_TYPE_END_IF;
        case BLOCK_BOOT_I2C_ADDR_LOOP_BLOCK:
            return BLOCK_TYPE_LOOP;
        case BLOCK_BOOT_I2C_ADDR_NOTE_BLOCK:
        case BLOCK_BOOT_I2C_ADDR_NOTE_BLOCK_2:
        case BLOCK_BOOT_I2C_ADDR_NOTE_BLOCK_3:
            return BLOCK_TYPE_NOTE;
        case BLOCK_BOOT_I2C_ADDR_MUSIC_SEQUENCE_BLOCK:
        case BLOCK_BOOT_I2C_ADDR_MUSIC_SEQUENCE_BLOCK_2:
            return BLOCK_TYPE_MUSIC_SEQ;
        case BLOCK_BOOT_I2C_ADDR_LED_COLOR_FLASH_BLOCK:
        case BLOCK_BOOT_I2C_ADDR_LED_COLOR_FLASH_BLOCK_2:
            return BLOCK_TYPE_LED_FLASH;
        case BLOCK_BOOT_I2C_ADDR_END_LOOP_BLOCK:
            return BLOCK_TYPE_END_LOOP;
        case BLOCK_BOOT_I2C_ADDR_DELAY_BLOCK:
            return BLOCK_TYPE_DELAY;
        case BLOCK_BOOT_I2C_ADDR_BUTTONPRESS_BLOCK:
            return BLOCK_TYPE_BUTTON;
        default:
            return BLOCK_TYPE_UNKNOWN;
    }
}

// ============================================================================
// BRAIN EXECUTOR BROADCAST POLICY (v3)
// ============================================================================
// Policy is intentionally deterministic and matches Brain runtime behavior:
// - Output trigger steps fan out CMD_EXECUTE to all present blocks.
// - Fan-out order is deterministic (configuration snapshot order).
// - DELAY timing uses Brain-side shared monotonic tick scheduling.
// - IF/LOOP are evaluated at each program-counter context (not pre-frozen globally).
#define BRAIN_BROADCAST_ALL_BLOCKS 1u
#define BRAIN_BROADCAST_ALL_OUTPUTS BRAIN_BROADCAST_ALL_BLOCKS
#define BRAIN_BROADCAST_DETERMINISTIC_ORDER 1u
#define BRAIN_DELAY_SHARED_TICK 1u
#define BRAIN_IF_LOOP_PER_PC_EVAL 1u

// ============================================================================
// SHARED RUNTIME BROADCAST CONTRACT (Brain -> Child)
// ============================================================================
// Payload wire format for CMD_RUNTIME_BROADCAST:
//   byte0 = brain_runtime_broadcast_state_t
//   byte1 = highlight pc / program index
//   byte2 = current block_type_t step type (or BLOCK_TYPE_UNKNOWN if n/a)
typedef enum {
    BRAIN_RUNTIME_IDLE    = 0x00,
    BRAIN_RUNTIME_RUNNING = 0x01,
    BRAIN_RUNTIME_STEP    = 0x02,
    BRAIN_RUNTIME_DONE    = 0x03,
    BRAIN_RUNTIME_ERROR   = 0x04,
    BRAIN_RUNTIME_STOP    = 0x05,
} brain_runtime_broadcast_state_t;

#define BRAIN_RUNTIME_BROADCAST_PAYLOAD_LEN 3u
#define BRAIN_RUNTIME_PC_NONE               0xFFu

typedef struct __attribute__((packed)) {
    uint8_t state;
    uint8_t pc;
    uint8_t step_type;
} brain_runtime_broadcast_payload_t;


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
        default:                    return "UNKNOWN";
    }
}

static inline uint8_t block_compute_i2c_address(block_type_t type) {
    // Lightweight deterministic hash from chip MAC + block type.
    // Keeps address inside the child scan window used by Brain.
    uint32_t hash = 2166136261u;

#if BLOCK_HAS_ESP_MAC
    uint8_t mac[6] = {0};
    (void)esp_read_mac(mac, ESP_MAC_WIFI_STA);
    for (int i = 0; i < 6; i++) {
        hash ^= mac[i];
        hash *= 16777619u;
    }
#else
    hash ^= (uint32_t)((uint8_t)type);
    hash *= 16777619u;
    hash ^= 0xA5u;
    hash *= 16777619u;
#endif

    hash ^= (uint32_t)((uint8_t)type);
    hash *= 16777619u;

    const uint8_t span = (uint8_t)(CHILD_I2C_ADDR_MAX - CHILD_I2C_ADDR_MIN + 1u);
    return (uint8_t)(CHILD_I2C_ADDR_MIN + (hash % span));
}

static inline uint32_t block_compute_device_uid(block_type_t type) {
    uint32_t hash = 2166136261u;

#if BLOCK_HAS_ESP_MAC
    uint8_t mac[6] = {0};
    (void)esp_read_mac(mac, ESP_MAC_WIFI_STA);
    for (int i = 0; i < 6; i++) {
        hash ^= mac[i];
        hash *= 16777619u;
    }
#else
    hash ^= (uint32_t)((uint8_t)type);
    hash *= 16777619u;
    hash ^= 0x5Au;
    hash *= 16777619u;
#endif

    hash ^= (uint32_t)((uint8_t)type);
    hash *= 16777619u;
    if (hash == 0u) {
        hash = 0xA5A5A5A5u;
    }
    return hash;
}

static inline bool block_is_valid_child_address(uint8_t address) {
    return (address >= CHILD_I2C_ADDR_MIN) && (address <= CHILD_I2C_ADDR_MAX);
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
        case CMD_SET_I2C_ADDRESS:       return "SET_I2C_ADDRESS";
        case CMD_MATRIX_FILL:           return "MATRIX_FILL";
        case CMD_MATRIX_SET_PIXEL:      return "MATRIX_SET_PIXEL";
        case CMD_MATRIX_CLEAR:          return "MATRIX_CLEAR";
        case CMD_MATRIX_SET_ROW:        return "MATRIX_SET_ROW";
        case CMD_MATRIX_SET_COLUMN:     return "MATRIX_SET_COLUMN";
        case CMD_MATRIX_DRAW_PATTERN:   return "MATRIX_DRAW_PATTERN";
        case CMD_MATRIX_BRIGHTNESS:     return "MATRIX_BRIGHTNESS";
        case CMD_MATRIX_SHOW:           return "MATRIX_SHOW";
        case CMD_RUNTIME_BROADCAST:     return "RUNTIME_BROADCAST";
        default:                        return "UNKNOWN_CMD";
    }
}

#endif // I2C_PROTOCOL_H
