#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "i2c_protocol.h"
#include "brain_block.h"

// Forward declarations from i2c_comm.c
extern esp_err_t i2c_ping(uint8_t addr);
extern void i2c_safe_scan(void);
extern esp_err_t i2c_matrix_fill(uint8_t address, uint8_t r, uint8_t g, uint8_t b);
extern esp_err_t i2c_matrix_clear(uint8_t address);
extern esp_err_t i2c_matrix_set_brightness(uint8_t address, uint8_t brightness);

static const char *TAG = "DEMO";



// ============================================================================
// DEMO TASK - Cycles through colors on connected blocks
// ============================================================================
void demo_task(void *arg) {
    vTaskDelay(pdMS_TO_TICKS(2000)); // Wait for children
    demo_cmd_t cmd;
    while (1) {
        ESP_LOGI(TAG, "\n--- NEW CYCLE ---");                      
        // Scan to see what's connected
        i2c_safe_scan();
        // Wait indefinitely for a command
        if (xQueueReceive(demo_cmd_queue, &cmd, pdMS_TO_TICKS(10))) {

            if (cmd == CMD_START) {
                ESP_LOGI(TAG, "Demo START received");
                
                // ========== Child Block 1 - LED Matrix ==========
                if (i2c_ping(CHILD_1_ADDR) == ESP_OK) {
                    ESP_LOGI(TAG, "Child 1 detected!");

                    ESP_LOGI(TAG, "Child 1: Setting brightness to 30%%");
                    i2c_matrix_set_brightness(CHILD_1_ADDR, 76);
                    vTaskDelay(pdMS_TO_TICKS(500));
                    
                    ESP_LOGI(TAG, "Child 1: RED");
                    i2c_matrix_fill(CHILD_1_ADDR, 255, 0, 0);
                    vTaskDelay(pdMS_TO_TICKS(1000));
                    
                    ESP_LOGI(TAG, "Child 1: GREEN");
                    i2c_matrix_fill(CHILD_1_ADDR, 0, 255, 0);
                    vTaskDelay(pdMS_TO_TICKS(1000));
                    
                    ESP_LOGI(TAG, "Child 1: BLUE");
                    i2c_matrix_fill(CHILD_1_ADDR, 0, 0, 255);
                    vTaskDelay(pdMS_TO_TICKS(1000));
                    
                    ESP_LOGI(TAG, "Child 1: CLEAR");
                    i2c_matrix_clear(CHILD_1_ADDR);
                    vTaskDelay(pdMS_TO_TICKS(500));
                }
                
                // ========== Child Block 2 - OLED Display ==========
                if (i2c_ping(CHILD_2_ADDR) == ESP_OK) {
                    ESP_LOGI(TAG, "Child 2 detected!");

                    ESP_LOGI(TAG, "Child 2: Setting brightness");
                    i2c_matrix_set_brightness(CHILD_2_ADDR, 76);
                    vTaskDelay(pdMS_TO_TICKS(500));
                    
                    ESP_LOGI(TAG, "Child 2: YELLOW");
                    i2c_matrix_fill(CHILD_2_ADDR, 255, 255, 0);
                    vTaskDelay(pdMS_TO_TICKS(1000));
                    
                    ESP_LOGI(TAG, "Child 2: CYAN");
                    i2c_matrix_fill(CHILD_2_ADDR, 0, 255, 255);
                    vTaskDelay(pdMS_TO_TICKS(1000));
                    
                    ESP_LOGI(TAG, "Child 2: MAGENTA");
                    i2c_matrix_fill(CHILD_2_ADDR, 255, 0, 255);
                    vTaskDelay(pdMS_TO_TICKS(1000));
                    
                    ESP_LOGI(TAG, "Child 2: CLEAR");
                    i2c_matrix_clear(CHILD_2_ADDR);
                }
            }
            else if (cmd == CMD_STOP) {
                ESP_LOGI(TAG, "Demo STOP received");

                i2c_matrix_clear(CHILD_1_ADDR);
                i2c_matrix_clear(CHILD_2_ADDR);
            }
        }
        // Wait before next cycle
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}