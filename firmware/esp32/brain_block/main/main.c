#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "i2c_protocol.h"

static const char *TAG = "BRAIN";

// Initialize I²C Master
esp_err_t i2c_master_init(void) {
    ESP_LOGI(TAG, "Init I²C Master: SDA=%d, SCL=%d", I2C_SDA_PIN, I2C_SCL_PIN);
    
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_SDA_PIN,
        .scl_io_num = I2C_SCL_PIN,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_FREQ_HZ,
    };
    
    i2c_param_config(I2C_NUM_0, &conf);
    return i2c_driver_install(I2C_NUM_0, conf.mode, 0, 0, 0);
}

// Ping device
esp_err_t i2c_ping(uint8_t addr) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_stop(cmd);
    
    esp_err_t ret = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return ret;
}

// Send LED color
esp_err_t i2c_set_led(uint8_t addr, uint8_t r, uint8_t g, uint8_t b) {
    uint8_t data[4] = {CMD_SET_LED, r, g, b};
    
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write(cmd, data, 4, true);
    i2c_master_stop(cmd);
    
    esp_err_t ret = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return ret;
}

// Scan bus
void i2c_scan(void) {
    ESP_LOGI(TAG, "=== I²C SCAN ===");
    int found = 0;
    
    for (uint8_t addr = 0x08; addr <= 0x0F; addr++) {
        if (i2c_ping(addr) == ESP_OK) {
            ESP_LOGI(TAG, "Found device at 0x%02X", addr);
            found++;
        }
    }
    
    ESP_LOGI(TAG, "Total devices: %d", found);
}

// Main task
void comm_task(void *arg) {
    vTaskDelay(pdMS_TO_TICKS(2000)); // Wait for children
    
    while (1) {
        ESP_LOGI(TAG, "\n--- NEW CYCLE ---");
        
        i2c_scan();
        
        // Test Child 1
        if (i2c_ping(CHILD_1_ADDR) == ESP_OK) {
            ESP_LOGI(TAG, "Child 1: RED");
            i2c_set_led(CHILD_1_ADDR, 255, 0, 0);
            vTaskDelay(pdMS_TO_TICKS(1000));
            
            ESP_LOGI(TAG, "Child 1: GREEN");
            i2c_set_led(CHILD_1_ADDR, 0, 255, 0);
            vTaskDelay(pdMS_TO_TICKS(1000));
            
            ESP_LOGI(TAG, "Child 1: BLUE");
            i2c_set_led(CHILD_1_ADDR, 0, 0, 255);
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
        
        // Test Child 2 (if you add it later)
        if (i2c_ping(CHILD_2_ADDR) == ESP_OK) {
            ESP_LOGI(TAG, "Child 2: YELLOW");
            i2c_set_led(CHILD_2_ADDR, 255, 255, 0);
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
        
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "=== BRAIN BLOCK ===");
    
    ESP_ERROR_CHECK(i2c_master_init());
    vTaskDelay(pdMS_TO_TICKS(500));
    
    i2c_scan();
    
    xTaskCreate(comm_task, "comm", 4096, NULL, 5, NULL);
}