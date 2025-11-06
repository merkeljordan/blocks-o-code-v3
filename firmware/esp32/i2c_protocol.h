#ifndef I2C_PROTOCOL_H
#define I2C_PROTOCOL_H

#include <stdint.h>

// I²C Pins
#define I2C_SDA_PIN         21
#define I2C_SCL_PIN         22
#define I2C_FREQ_HZ         100000

// Child Addresses
#define CHILD_1_ADDR        0x08
#define CHILD_2_ADDR        0x09

// Commands
#define CMD_PING            0x00
#define CMD_GET_TYPE        0x01
#define CMD_SET_LED         0x02
#define CMD_GET_STATUS      0x03

// Block Types
#define BLOCK_TYPE_BRAIN    0x00
#define BLOCK_TYPE_LED      0x20

#endif