#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "../../i2c_protocol.h"

static const char *TAG = "CHILD_1";

#define MY_ADDRESS      0x08

static uint8_t led_r = 0, led_g = 0, led_b = 0;

// Initialize I²C Slave
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
    
    i2c_param_config(I2C_NUM_0, &conf);
    return i2c_driver_install(I2C_NUM_0, conf.mode, 128, 128, 0);
}

// I²C receive task
void i2c_task(void *arg) {
    uint8_t buffer[128];
    
    while (1) {
        int len = i2c_slave_read_buffer(I2C_NUM_0, buffer, 128, pdMS_TO_TICKS(100));
        
        if (len > 0) {
            ESP_LOGI(TAG, "Received %d bytes", len);
            
            uint8_t cmd = buffer[0];
            
            if (cmd == CMD_SET_LED && len >= 4) {
                led_r = buffer[1];
                led_g = buffer[2];
                led_b = buffer[3];
                ESP_LOGI(TAG, "LED: R=%d G=%d B=%d", led_r, led_g, led_b);
            }
        }
    }
}

// LED status task
void led_task(void *arg) {
    while (1) {
        ESP_LOGI(TAG, "Current color: RGB(%d,%d,%d)", led_r, led_g, led_b);
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "=== CHILD BLOCK 1 (0x08) ===");
    
    ESP_ERROR_CHECK(i2c_slave_init());
    
    xTaskCreate(i2c_task, "i2c", 4096, NULL, 5, NULL);
    xTaskCreate(led_task, "led", 2048, NULL, 3, NULL);
}