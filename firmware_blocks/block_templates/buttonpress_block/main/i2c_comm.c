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

static const char *TAG = "BUTTONPRESS_BLOCK";

// Dynamic child-bus address (derived from chip identity + block type)
#define MY_ADDRESS      BLOCK_BOOT_I2C_ADDR_BUTTONPRESS_BLOCK
#define MY_BLOCK_TYPE   BLOCK_TYPE_BUTTON

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
        s_runtime_address = BLOCK_BOOT_I2C_ADDR_BUTTONPRESS_BLOCK;
    }
    registers[REG_WHOAMI]   = MY_BLOCK_TYPE;  // Block type
    registers[REG_STATUS]   = STATUS_READY;   // Status
    registers[REG_FW_MAJOR] = 1;              // Firmware major
    registers[REG_FW_MINOR] = 0;              // Firmware minor
    populate_identity_registers();
}

static void refresh_dynamic_registers(void)
{
    registers[REG_STATUS] = button_block_get_status_flags();
    registers[REG_DATA_LEN] = button_block_get_pending_data_len();
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
void i2c_task(void *arg) {
    (void)arg;
    uint8_t buffer[128];
    uint8_t tx_buf[16];

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

            refresh_dynamic_registers();
            if (len == 1 && buffer[0] < 0x10) {
                uint8_t reg = buffer[0];
                uint8_t response = registers[reg];
                (void)i2c_slave_write_buffer(I2C_NUM_0, &response, 1, pdMS_TO_TICKS(100));
                ESP_LOGI(TAG, "Register 0x%02X -> 0x%02X", reg, response);
                continue;
            }

            size_t tx_len = 0;
            i2c_command_t cmd = (i2c_command_t)buffer[0];
            const uint8_t *payload = (len > 1) ? &buffer[1] : NULL;
            size_t payload_len = (len > 1) ? (size_t)(len - 1) : 0U;

            command_handle(cmd, payload, payload_len, tx_buf, &tx_len);
            if (tx_len > 0U) {
                (void)i2c_slave_write_buffer(I2C_NUM_0, tx_buf, tx_len, pdMS_TO_TICKS(100));
                ESP_LOGI(TAG, "Sent %u response bytes", (unsigned)tx_len);
            }
        }
    }
}
