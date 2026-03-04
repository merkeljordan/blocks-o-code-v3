#pragma once
#ifndef I2C_PROTOCOL_H
#define I2C_PROTOCOL_H

/*
 * Shared wire protocol between Brain block (I2C master) and child blocks.
 *
 * Notes for Music Sequence integration:
 * - This header defines command IDs and status bits used by both sides.
 * - Music Sequence uses BLOCK_TYPE_MUSIC_SEQ and responds to CMD_EXECUTE.
 * - CMD_GET_DATA should return music_seq_payload_v1_t from music_sequence_types.h.
 */

#include <stdbool.h>
#include <stdint.h>
#include "driver/i2c.h"

/* ========================================================================== */
/* I2C bus configuration                                                       */
/* ========================================================================== */
#define I2C_PORT_NUM    I2C_NUM_0
#define I2C_SDA_PIN     21
#define I2C_SCL_PIN     22
#define I2C_FREQ_HZ     100000  /* 100 kHz */

/* ========================================================================== */
/* Register map (Brain reads these from child blocks)                          */
/* ========================================================================== */
#define REG_WHOAMI      0x00    /* 1 byte: block_type_t */
#define REG_STATUS      0x01    /* 1 byte: STATUS_* flags */
#define REG_FW_MAJOR    0x02    /* 1 byte: optional firmware major */
#define REG_FW_MINOR    0x03    /* 1 byte: optional firmware minor */
#define REG_CAPS        0x04    /* 1 byte: optional capabilities bitfield */

/* Legacy one-off command kept for compatibility with older experiments. */
#define CMD_OLED_TEXT   0xF1

/* ========================================================================== */
/* Commands (Brain -> Child)                                                   */
/* ========================================================================== */
typedef enum {
    CMD_PING            = 0x00,
    CMD_GET_TYPE        = 0x01,
    CMD_SET_LED         = 0x02,
    CMD_GET_STATUS      = 0x03,
    CMD_GET_DATA        = 0x04,
    CMD_PLAY_NOTE       = 0x05,
    CMD_EXECUTE         = 0x06,
    CMD_RESET           = 0x07,
    CMD_SET_DELAY       = 0x08,
    CMD_SET_LOOP        = 0x09,

    CMD_MATRIX_FILL         = 0x10,
    CMD_MATRIX_SET_PIXEL    = 0x11,
    CMD_MATRIX_CLEAR        = 0x12,
    CMD_MATRIX_SET_ROW      = 0x13,
    CMD_MATRIX_SET_COLUMN   = 0x14,
    CMD_MATRIX_DRAW_PATTERN = 0x15,
    CMD_MATRIX_BRIGHTNESS   = 0x16,
    CMD_MATRIX_SHOW         = 0x17,
} i2c_command_t;

/* ========================================================================== */
/* Block types                                                                 */
/* ========================================================================== */
typedef enum {
    BLOCK_TYPE_BRAIN       = 0x00,

    BLOCK_TYPE_IF          = 0x10,
    BLOCK_TYPE_THEN        = 0x11,
    BLOCK_TYPE_END_IF      = 0x12,
    BLOCK_TYPE_LOOP        = 0x13,
    BLOCK_TYPE_END_LOOP    = 0x14,
    BLOCK_TYPE_DELAY       = 0x15,

    BLOCK_TYPE_BUTTON      = 0x20,

    BLOCK_TYPE_NOTE        = 0x30,
    BLOCK_TYPE_MUSIC_SEQ   = 0x31,
    BLOCK_TYPE_LED_FLASH   = 0x32,
    BLOCK_TYPE_DISCO       = 0x33,

    BLOCK_TYPE_UNKNOWN     = 0xFF
} block_type_t;

/* ========================================================================== */
/* LED matrix patterns                                                         */
/* ========================================================================== */
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

/* ========================================================================== */
/* Common status bits                                                          */
/* ========================================================================== */
#define STATUS_READY        0x01
#define STATUS_BUSY         0x02
#define STATUS_ERROR        0x04
#define STATUS_DATA_READY   0x08

/* ========================================================================== */
/* Debug helpers                                                               */
/* ========================================================================== */
static inline const char *block_type_to_string(block_type_t type) {
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

static inline const char *command_to_string(i2c_command_t cmd) {
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

#endif /* I2C_PROTOCOL_H */
