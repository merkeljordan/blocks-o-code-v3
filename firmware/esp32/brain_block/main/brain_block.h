/*
 * brain_block.h
 *
 * Shared API for the Brain Block component.
 * Exposes I2C primitives, the demo task and the network client task.
 */

#ifndef BRAIN_BLOCK_H
#define BRAIN_BLOCK_H

#include "esp_err.h"
#include <stdint.h>
#include "i2c_protocol.h"

/* I2C primitives (implemented in i2c_comm.c) */
esp_err_t i2c_master_init(void);
esp_err_t i2c_ping(uint8_t addr);
void i2c_safe_scan(void);
esp_err_t i2c_matrix_fill(uint8_t address, uint8_t r, uint8_t g, uint8_t b);
esp_err_t i2c_matrix_clear(uint8_t address);
esp_err_t i2c_matrix_set_brightness(uint8_t address, uint8_t brightness);
esp_err_t i2c_oled_text(uint8_t address, const char *msg);
esp_err_t i2c_read_reg(uint8_t addr, uint8_t reg, uint8_t *out, size_t len);

// Command Queue and definitions
typedef enum {
    CMD_NONE = 0,
    CMD_START,
    CMD_STOP
} demo_cmd_t;

extern QueueHandle_t demo_cmd_queue;

/* Demo task (implemented in demo_task.c) */
void demo_task(void *arg);

/* Network client startup (implemented in app.c) */
void start_network_client(void);

#endif // BRAIN_BLOCK_H
