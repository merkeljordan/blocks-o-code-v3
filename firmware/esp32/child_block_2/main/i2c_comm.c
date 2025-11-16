#include "driver/i2c_slave.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "i2c_protocol.h"

// ESP32-S3 uses different I²C pins
#undef I2C_SDA_PIN
#undef I2C_SCL_PIN
#define I2C_SDA_PIN     8   // ESP32-S3 I²C pins
#define I2C_SCL_PIN     9

// Forward declarations
extern void handle_command(uint8_t *buffer, int len);
extern bool is_valid_command(uint8_t cmd);
extern SemaphoreHandle_t display_get_mutex(void);
extern void display_dashboard(void);

#define MY_ADDRESS      0x09
#define I2C_SLAVE_RX_BUF_LEN 128

static const char *TAG = "I2C_COMM";
static i2c_slave_dev_handle_t slave_handle = NULL;

// ============================================================================
// I²C EVENT CALLBACK (Called from ISR context)
// ============================================================================
static IRAM_ATTR bool i2c_slave_rx_done_callback(i2c_slave_dev_handle_t i2c_slave, const i2c_slave_rx_done_event_data_t *evt_data, void *arg)
{
    BaseType_t high_task_wakeup = pdFALSE;
    QueueHandle_t receive_queue = (QueueHandle_t)arg;
    
    // Send buffer pointer to queue (don't copy data in ISR)
    xQueueSendFromISR(receive_queue, &evt_data->buffer, &high_task_wakeup);
    
    return high_task_wakeup == pdTRUE;
}

// ============================================================================
// I²C SLAVE INITIALIZATION
// ============================================================================
esp_err_t i2c_slave_init(void) {
    ESP_LOGI(TAG, "Init I²C Slave at 0x%02X", MY_ADDRESS);
    
    i2c_slave_config_t slave_config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = I2C_SDA_PIN,
        .scl_io_num = I2C_SCL_PIN,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .send_buf_depth = 128,
        .slave_addr = MY_ADDRESS,
        .addr_bit_len = I2C_ADDR_BIT_LEN_7,
        .flags.broadcast_en = false,
    };
    
    gpio_set_pull_mode(I2C_SDA_PIN, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(I2C_SCL_PIN, GPIO_PULLUP_ONLY);

    esp_err_t err = i2c_new_slave_device(&slave_config, &slave_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I²C slave init failed: %s", esp_err_to_name(err));
        return err;
    }
    
    ESP_LOGI(TAG, "I²C slave initialized successfully!");
    return ESP_OK;
}

// ============================================================================
// I²C RECEIVE TASK (Using Event Queue)
// ============================================================================
void i2c_task(void *arg) {
    ESP_LOGI(TAG, "I²C task started");
    
    // Create queue for receiving I²C data
    QueueHandle_t receive_queue = xQueueCreate(10, sizeof(uint8_t*));
    if (receive_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create receive queue!");
        vTaskDelete(NULL);
        return;
    }
    
    // Register callback for when data is received
    i2c_slave_event_callbacks_t cbs = {
        .on_recv_done = i2c_slave_rx_done_callback,
    };
    ESP_ERROR_CHECK(i2c_slave_register_event_callbacks(slave_handle, &cbs, receive_queue));
    
    // Allocate receive buffer
    uint8_t *receive_buffer = (uint8_t*)malloc(I2C_SLAVE_RX_BUF_LEN);
    if (receive_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate receive buffer!");
        vTaskDelete(NULL);
        return;
    }
    
    // Start receiving
    ESP_ERROR_CHECK(i2c_slave_receive(slave_handle, receive_buffer, I2C_SLAVE_RX_BUF_LEN));
    
    uint8_t *recv_buf_ptr = NULL;
    while (1) {
        // Wait for data from queue
        if (xQueueReceive(receive_queue, &recv_buf_ptr, pdMS_TO_TICKS(1000)) == pdTRUE) {
            
            uint8_t cmd = recv_buf_ptr[0];
            
            if (is_valid_command(cmd)) {
                ESP_LOGI(TAG, "Valid I2C command: 0x%02X", cmd);
                
                // Copy buffer before processing (since it will be reused)
                uint8_t cmd_copy[I2C_SLAVE_RX_BUF_LEN];
                memcpy(cmd_copy, recv_buf_ptr, I2C_SLAVE_RX_BUF_LEN);
                
                SemaphoreHandle_t mutex = display_get_mutex();
                if (xSemaphoreTake(mutex, pdMS_TO_TICKS(1000))) {
                    handle_command(cmd_copy, I2C_SLAVE_RX_BUF_LEN);
                    display_dashboard();
                    xSemaphoreGive(mutex);
                }
            } else {
                ESP_LOGD(TAG, "Ignoring invalid data: 0x%02X", cmd);
            }
            
            // Re-arm receive for next transmission
            ESP_ERROR_CHECK(i2c_slave_receive(slave_handle, receive_buffer, I2C_SLAVE_RX_BUF_LEN));
        }
        
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    free(receive_buffer);
    vQueueDelete(receive_queue);
}