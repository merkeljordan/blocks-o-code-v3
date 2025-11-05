#include <stdio.h>
#include "driver.i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define I2C_MASTER_SCL_IO           22          // GPIO number for I2C master clock
#define I2C_MASTER_SDA_IO           21          // GPIO number for I2C master data 
#define I2C_MASTER_NUM              I2C_NUM_0   // I2C port   number for master dev 
#define I2C_MASTER_FREQ_HZ          100000      // I2C master clock frequency
#define I2C_MASTER_TX_BUF_DISABLE   0           // I2C master doesn't need buffer            
#define I2C_MASTER_RX_BUF_DISABLE   0           // I2C master doesn't need buffer             
#define I2C_MASTER_TIMEOUT_MS       1000        // I2C master timeout in milliseconds 
#define CHILD_ADDR                  0x04        // Example Child Block I2C address

static const char *TAG = "BRAIN_I2C";

void i2c_master_init(void) {
    i2c
}