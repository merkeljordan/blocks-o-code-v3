#include <stdio.h>
#include <stddef.h>
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i2c_protocol.h"
#include "command_handler.h"

static const char *TAG = "I2C_COMM";

#define MY_ADDRESS      BLOCK_BOOT_I2C_ADDR_LED_COLOR_FLASH_BLOCK_2
#define MY_BLOCK_TYPE   BLOCK_TYPE_LED_FLASH

// ============================================================================
// REGISTER MAP - What the Brain can read from us
// ============================================================================
static uint8_t registers[16] = {0};
static uint8_t s_runtime_address = 0u;
static uint32_t s_device_uid = 0u;

static void populate_identity_registers(void) {
    if (s_device_uid == 0u) {
        s_device_uid = block_compute_device_uid(MY_BLOCK_TYPE);
    }

    registers[REG_UID0] = (uint8_t)(s_device_uid & 0xFFu);
    registers[REG_UID1] = (uint8_t)((s_device_uid >> 8) & 0xFFu);
    registers[REG_UID2] = (uint8_t)((s_device_uid >> 16) & 0xFFu);
    registers[REG_UID3] = (uint8_t)((s_device_uid >> 24) & 0xFFu);
    registers[REG_ASSIGNED_ADDR] = s_runtime_address;
}

static void init_registers(void) {
    if (s_runtime_address == 0u) {
        s_runtime_address = BLOCK_BOOT_I2C_ADDR_LED_COLOR_FLASH_BLOCK_2;
    }
    registers[REG_WHOAMI]   = MY_BLOCK_TYPE;  // Block type
    registers[REG_STATUS]   = STATUS_READY;   // Status
    registers[REG_FW_MAJOR] = 1;              // Firmware major
    registers[REG_FW_MINOR] = 0;              // Firmware minor
    populate_identity_registers();
}

static esp_err_t rebind_i2c_slave_address(uint8_t new_address) {
    if (!block_is_valid_child_address(new_address)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (new_address == s_runtime_address) {
        return ESP_OK;
    }

    (void)i2c_driver_delete(I2C_NUM_0);

    i2c_config_t conf = {
        .mode = I2C_MODE_SLAVE,
        .sda_io_num = I2C_SDA_PIN,
        .scl_io_num = I2C_SCL_PIN,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .slave.addr_10bit_en = 0,
        .slave.slave_addr = new_address,
    };

    esp_err_t err = i2c_param_config(I2C_NUM_0, &conf);
    if (err != ESP_OK) {
        return err;
    }

    err = i2c_driver_install(I2C_NUM_0, conf.mode, 128, 128, 0);
    if (err != ESP_OK) {
        return err;
    }

    s_runtime_address = new_address;
    registers[REG_ASSIGNED_ADDR] = new_address;
    ESP_LOGI(TAG, "Rebound child address to 0x%02X", new_address);
    return ESP_OK;
}

// ============================================================================
// I²C SLAVE INITIALIZATION
// ============================================================================
esp_err_t i2c_slave_init(void) {
    ESP_LOGI(TAG, "Init I²C Slave at 0x%02X (type=%s)",
             s_runtime_address, block_type_to_string(MY_BLOCK_TYPE));

    init_registers();

    i2c_config_t conf = {
        .mode = I2C_MODE_SLAVE,
        .sda_io_num = I2C_SDA_PIN,
        .scl_io_num = I2C_SCL_PIN,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .slave.addr_10bit_en = 0,
        .slave.slave_addr = s_runtime_address,
    };

    esp_err_t err = i2c_param_config(I2C_NUM_0, &conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I²C config failed: %s", esp_err_to_name(err));
        return err;
    }

    err = i2c_driver_install(I2C_NUM_0, conf.mode, 128, 128, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I²C driver install failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "I²C slave initialized!");
    return ESP_OK;
}

// ============================================================================
// I²C RECEIVE TASK - Handles commands and register reads
// ============================================================================
/*
 * i2c_task packet routing model:
 *
 * A) Register read path:
 *    - Brain writes 1 byte < 0x10 (register index).
 *    - Child immediately returns 1-byte value.
 *    - REG_STATUS is dynamic and comes from command_handler_get_status().
 *
 * B) Command path:
 *    - Multi-byte packet where byte[0] is CMD_*.
 *    - Forwarded to handle_command() for parsing/execution enqueue.
 *    - CMD_GET_DATA triggers an immediate payload response from get_data_payload().
 */
void i2c_task(void *arg) {
    uint8_t buffer[128];

    while (1) {
        int len = i2c_slave_read_buffer(I2C_NUM_0, buffer, 128, pdMS_TO_TICKS(100));

        if (len > 0) {
            ESP_LOGI(TAG, "Received %d bytes: [0]=0x%02X", len, buffer[0]);

            if (len >= 2 && ((i2c_command_t)buffer[0]) == CMD_SET_I2C_ADDRESS) {
                esp_err_t err = rebind_i2c_slave_address(buffer[1]);
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to apply assigned address 0x%02X: %s",
                             buffer[1], esp_err_to_name(err));
                }
                continue;
            }

            // Register reads let Brain poll identity, status, and firmware metadata.
            if (len == 1 && buffer[0] < 0x10) {
                uint8_t reg = buffer[0];
                uint8_t response = registers[reg];
                // STATUS is live runtime state, so fetch it from command handler.
                if (reg == REG_STATUS) {
                    response = command_handler_get_status();
                }
                i2c_slave_write_buffer(I2C_NUM_0, &response, 1, pdMS_TO_TICKS(100));
                ESP_LOGI(TAG, "Register 0x%02X -> 0x%02X", reg, response);
            } else {
                // Command packets are parsed by child-side command handler.
                handle_command(buffer, len);

                if (buffer[0] == CMD_GET_DATA) {
                    uint8_t response[16] = {0};
                    size_t resp_len = get_data_payload(response, sizeof(response));
                    if (resp_len > 0) {
                        i2c_slave_write_buffer(I2C_NUM_0, response, resp_len,
                                               pdMS_TO_TICKS(100));
                        ESP_LOGI(TAG, "Sent %d bytes of data payload", (int)resp_len);
                    }
                }
            }
        }
    }
}
