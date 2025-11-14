#ifndef I2C_PROTOCOL_H
#define I2C_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>

// ============================================================================
// I²C HARDWARE CONFIGURATION
// ============================================================================

// I²C Pin Definitions (ESP32 standard I²C pins)
#define I2C_SDA_PIN         21
#define I2C_SCL_PIN         22

// I²C Bus Configuration
#define I2C_FREQ_HZ         100000      // 100 kHz (Standard Mode)
#define I2C_PORT_NUM        I2C_NUM_0   // Use I²C port 0

// I²C Buffer Sizes (for slave mode)
#define I2C_SLAVE_RX_BUF    128
#define I2C_SLAVE_TX_BUF    128

// ============================================================================
// CHILD BLOCK ADDRESSES (7-bit addressing)
// ============================================================================

// Child Block I²C Addresses (0x08 - 0x15 range)
#define CHILD_1_ADDR        0x08
#define CHILD_2_ADDR        0x09
#define CHILD_3_ADDR        0x0A
#define CHILD_4_ADDR        0x0B

// Address Range
#define I2C_CHILD_MIN_ADDR  0x08
#define I2C_CHILD_MAX_ADDR  0x15
#define MAX_CHILD_BLOCKS    14

// ============================================================================
// COMMAND DEFINITIONS (Brain → Child)
// ============================================================================

typedef enum {
    CMD_PING            = 0x00,  // Check if device is alive
    CMD_GET_TYPE        = 0x01,  // Request block type
    CMD_SET_LED         = 0x02,  // Set LED color (RGB)
    CMD_GET_STATUS      = 0x03,  // Request status
    CMD_GET_DATA        = 0x04,  // Request sensor/input data
    CMD_PLAY_NOTE       = 0x05,  // Play musical note
    CMD_EXECUTE         = 0x06,  // Execute block action
    CMD_RESET           = 0x07,  // Reset block state
    CMD_SET_DELAY       = 0x08,  // Set delay time
    CMD_SET_LOOP        = 0x09,  // Set loop count
    
    // LED MATRIX COMMANDS
    CMD_MATRIX_FILL         = 0x10,  // Fill entire matrix with one color
    CMD_MATRIX_SET_PIXEL    = 0x11,  // Set single pixel
    CMD_MATRIX_CLEAR        = 0x12,  // Clear matrix (all black)
    CMD_MATRIX_SET_ROW      = 0x13,  // Set entire row
    CMD_MATRIX_SET_COLUMN   = 0x14,  // Set entire column
    CMD_MATRIX_DRAW_PATTERN = 0x15,  // Draw predefined pattern
    CMD_MATRIX_BRIGHTNESS   = 0x16,  // Set brightness (0-255)
    CMD_MATRIX_SHOW         = 0x17,  // Update display (commit changes)
} i2c_command_t;

// ============================================================================
// BLOCK TYPES
// ============================================================================

typedef enum {
    // System
    BLOCK_TYPE_BRAIN       = 0x00,
    
    // Input Blocks (0x01-0x0F)
    BLOCK_TYPE_NOTE        = 0x01,
    BLOCK_TYPE_VOICE       = 0x02,
    BLOCK_TYPE_INPUT       = 0x03,
    BLOCK_TYPE_BUTTON      = 0x04,
    BLOCK_TYPE_SENSOR      = 0x05,
    
    // Control Flow (0x10-0x1F)
    BLOCK_TYPE_IF          = 0x10,
    BLOCK_TYPE_THEN        = 0x11,
    BLOCK_TYPE_ENDIF       = 0x12,
    BLOCK_TYPE_LOOP        = 0x13,
    BLOCK_TYPE_ENDLOOP     = 0x14,
    BLOCK_TYPE_DELAY       = 0x15,
    
    // Output Blocks (0x20-0x2F)
    BLOCK_TYPE_LED         = 0x20,
    BLOCK_TYPE_MUSIC       = 0x21,
    BLOCK_TYPE_DISCO       = 0x22,
    BLOCK_TYPE_SPEAKER     = 0x23,
    BLOCK_TYPE_VIBRATION   = 0x24,
    
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

#define STATUS_READY        0x01  // Block is ready
#define STATUS_BUSY         0x02  // Block is processing
#define STATUS_ERROR        0x04  // Block has error
#define STATUS_DATA_READY   0x08  // Block has new data (sensor/button)

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

// Convert block type to string (for logging)
static inline const char* block_type_to_string(block_type_t type) {
    switch (type) {
        case BLOCK_TYPE_BRAIN:      return "BRAIN";
        case BLOCK_TYPE_NOTE:       return "NOTE";
        case BLOCK_TYPE_VOICE:      return "VOICE";
        case BLOCK_TYPE_INPUT:      return "INPUT";
        case BLOCK_TYPE_BUTTON:     return "BUTTON";
        case BLOCK_TYPE_SENSOR:     return "SENSOR";
        case BLOCK_TYPE_IF:         return "IF";
        case BLOCK_TYPE_THEN:       return "THEN";
        case BLOCK_TYPE_ENDIF:      return "ENDIF";
        case BLOCK_TYPE_LOOP:       return "LOOP";
        case BLOCK_TYPE_ENDLOOP:    return "ENDLOOP";
        case BLOCK_TYPE_DELAY:      return "DELAY";
        case BLOCK_TYPE_LED:        return "LED";
        case BLOCK_TYPE_MUSIC:      return "MUSIC";
        case BLOCK_TYPE_DISCO:      return "DISCO";
        case BLOCK_TYPE_SPEAKER:    return "SPEAKER";
        case BLOCK_TYPE_VIBRATION:  return "VIBRATION";
        default:                    return "UNKNOWN";
    }
}

// Convert command to string (for logging)
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
        
        // LED Matrix commands
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