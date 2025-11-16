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

static const char *TAG = "I2C_COMM";

#define MY_ADDRESS  0x08

// ============================================================================
// I²C SLAVE INITIALIZATION
// ============================================================================
esp_err_t i2c_slave_init(void) {
    ESP_LOGI(TAG, "Init I²C Slave at 0x%02X", MY_ADDRESS);
    
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
    
    ESP_LOGI(TAG, "I²C slave initialized successfully!");
    return ESP_OK;
}

// ============================================================================
// I²C RECEIVE TASK
// ============================================================================
void i2c_task(void *arg) {
    uint8_t buffer[128];
    
    while (1) {
        int len = i2c_slave_read_buffer(I2C_NUM_0, buffer, 128, pdMS_TO_TICKS(100));
        
        if (len > 0) {
            ESP_LOGI(TAG, "Received %d bytes", len);
            handle_command(buffer, len);
        }
    }
}