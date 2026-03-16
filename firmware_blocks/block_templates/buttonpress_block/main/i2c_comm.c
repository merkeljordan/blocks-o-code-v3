#include <stdio.h>
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i2c_protocol.h"

// Forward declaration from command_handler.c
extern void handle_command(uint8_t *buffer, int len);

static const char *TAG = "BUTTONPRESS_BLOCK";

// TODO: Change per block
#define MY_ADDRESS      0x0E
#define MY_BLOCK_TYPE   BLOCK_TYPE_BUTTON

// ============================================================================
// REGISTER MAP - What the Brain can read from us
// ============================================================================
static uint8_t registers[16] = {0};

static void init_registers(void) {
    registers[REG_WHOAMI]   = MY_BLOCK_TYPE;  // Block type
    registers[REG_STATUS]   = STATUS_READY;   // Status
    registers[REG_FW_MAJOR] = 1;              // Firmware major
    registers[REG_FW_MINOR] = 0;              // Firmware minor
}

// ============================================================================
// I²C SLAVE INITIALIZATION
// ============================================================================
esp_err_t i2c_slave_init(void) {
    ESP_LOGI(TAG, "Init I²C Slave at 0x%02X (type=%s)",
             MY_ADDRESS, block_type_to_string(MY_BLOCK_TYPE));

    init_registers();

    i2c_config_t conf = {
        .mode = I2C_MODE_SLAVE,
        .sda_io_num = I2C_SDA_PIN,
        .scl_io_num = I2C_SCL_PIN,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .slave.addr_10bit_en = 0,
        .slave.slave_addr = MY_ADDRESS,
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
    uint8_t buffer[128];

    while (1) {
        int len = i2c_slave_read_buffer(I2C_NUM_0, buffer, 128, pdMS_TO_TICKS(100));

        if (len > 0) {
            ESP_LOGI(TAG, "Received %d bytes: [0]=0x%02X", len, buffer[0]);

            // Single byte < 0x10 = register read request
            if (len == 1 && buffer[0] < 0x10) {
                uint8_t reg = buffer[0];
                uint8_t response = registers[reg];
                i2c_slave_write_buffer(I2C_NUM_0, &response, 1, pdMS_TO_TICKS(100));
                ESP_LOGI(TAG, "Register 0x%02X -> 0x%02X", reg, response);
            } else {
                // Command
                handle_command(buffer, len);
            }
        }
    }
}
