#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "i2c_protocol.h"

static const char *TAG = "BRAIN";

// Initialize as the I²C Master on the bus
// All Child Blocks are I²C Slaves listening on the same bus
esp_err_t i2c_master_init(void) {
    ESP_LOGI(TAG, "Init I²C Master: SDA=%d, SCL=%d", I2C_SDA_PIN, I2C_SCL_PIN);
    
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,        // This ESp32 is the boss
        .sda_io_num = I2C_SDA_PIN,      // Data line (GPIO 21)
        .scl_io_num = I2C_SCL_PIN,      // Clock line (GPIO 22)
        .sda_pullup_en = GPIO_PULLUP_ENABLE,    // Enable internal pull-up resistors
        .scl_pullup_en = GPIO_PULLUP_ENABLE,    // These keep the bus at HIGH when idle
        .master.clk_speed = I2C_FREQ_HZ,    // 100 kHz standard mode I2C
    };
    
    // Apply configuration and install the driver
    i2c_param_config(I2C_NUM_0, &conf);
    return i2c_driver_install(I2C_NUM_0, conf.mode, 0, 0, 0);
}

// I2C Ping (check if device responds) 
// Checks if a device exists at a specific I2C address
// Returns ESP_OK if device responds, else error code
esp_err_t i2c_ping(uint8_t addr) {
    // Create an I2C command sequence
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);      // Send START condition

    // Try to address the device (write mode)
    // If device acknowledges, it exists
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);

    i2c_master_stop(cmd);       // Send STOP condition
    
    // Execute the command, if no ACK recieved, returns timeout error
    esp_err_t ret = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd); // Clean up
    return ret;
}

void i2c_safe_scan(void) {
    ESP_LOGI(TAG, "=== SAFE I²C SCAN ===");

    int found = 0;

    for (uint8_t addr = 0x08; addr <= 0x0F; addr++) {

        // harmless 1-byte ping command
        uint8_t data = CMD_PING;

        esp_err_t ret = i2c_master_write_to_device(
            I2C_NUM_0,
            addr,
            &data,
            1,
            pdMS_TO_TICKS(25)
        );

        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Detected device at 0x%02X", addr);
            found++;
        }
    }

    ESP_LOGI(TAG, "Devices detected: %d", found);
}

/*
// I2C Bus Scanner
// Scans all possible Child Block addresses ( 0x08 to 0x0F) to see what's connected
// This runs at startup and periodically to detect connected Child Blocks
// For the demo: Shows the system can auto-detect connected blocks
void i2c_scan(void) {
    ESP_LOGI(TAG, "=== I²C SCAN ===");
    int found = 0;
    
    // Loop through our designated Child Block address range
    for (uint8_t addr = 0x08; addr <= 0x0F; addr++) {
        // Ping each address, if device responds, its connected
        if (i2c_ping(addr) == ESP_OK) {
            ESP_LOGI(TAG, "Found device at 0x%02X", addr);
            found++;
        }
    }
    
    ESP_LOGI(TAG, "Total devices: %d", found);
}
*/

// Fill Matrix with Color
// Sends CMD_MATRIX_FILL command to specified Child Block address
// Packet format: [CMD_MATRIX_FILL, R, G, B]
//                 1 byte           1   1   1   = 4 bytes total

// This command works for both:
// Child Block 1 (LED Matrix) : Lights up with the specified color
// Child Block 2 (OLED Display): Shows the color info on screen
esp_err_t i2c_matrix_fill(uint8_t address, uint8_t r, uint8_t g, uint8_t b) {
    // Build the 4-byte command packet
    uint8_t data[4] = {CMD_MATRIX_FILL, r, g, b};
    
    // Create I2C command sequence
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);

    // Address the target Child Block (left shift + write bit)
    i2c_master_write_byte(cmd, (address << 1) | I2C_MASTER_WRITE, true);

    // Send the 4-byte command
    i2c_master_write(cmd, data, 4, true);
    i2c_master_stop(cmd);
    
    // Execute the command and wait up to 100ms for ACK
    esp_err_t ret = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    
    return ret;
}


// Clear Matrix
// Sends CMD_MATRIX_CLEAR command to turn off all LEDs
// Packet format: [CMD_MATRIX_CLEAR] = 1 byte only
// No parameters needed since clear always means RGB (0,0,0)
esp_err_t i2c_matrix_clear(uint8_t address) {
    uint8_t data[1] = {CMD_MATRIX_CLEAR};
    
    // Same I2C transmission process as above just 1 byte instead of 4
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (address << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write(cmd, data, 1, true);
    i2c_master_stop(cmd);
    
    esp_err_t ret = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    
    return ret;
}


// Set Matrix Brightness
// Sends CMD_MATRIX_BRIGHTNESS to adjust LED intensity
// Packet format: [CMD_MATRIX_BRIGHTNESS, brightness_value]
//                 1 byte                 1 byte       = 2 bytes total
esp_err_t i2c_matrix_set_brightness(uint8_t address, uint8_t brightness) {
    uint8_t data[2] = {CMD_MATRIX_BRIGHTNESS, brightness};
    
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (address << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write(cmd, data, 2, true);
    i2c_master_stop(cmd);
    
    esp_err_t ret = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    
    return ret;
}

// ----------------------------------------
// Send Text to OLED Child Block (0xF1)
// Format: [CMD_OLED_TEXT][LEN][chars...]
// ----------------------------------------
esp_err_t i2c_oled_text(uint8_t address, const char *msg) {
    uint8_t len = strlen(msg);
    if (len > 30) len = 30;  // Prevent overflow

    uint8_t data[32];
    data[0] = CMD_OLED_TEXT; // Command ID
    data[1] = len;           // Length byte
    memcpy(&data[2], msg, len);

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (address << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write(cmd, data, len + 2, true);
    i2c_master_stop(cmd);

    esp_err_t ret = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return ret;
}


// Main Communication Task
// This is the brains main loop, continuously runs the demo sequence

// Demo Flow:
// 1. Scan I2C bus for connected Child Blocks
// 2. If Child Block 1 (LED Matrix) is found: Control LED Matrix (RED, GREEN, BLUE, CLEAR)
// 3. If Child Block 2 (OLED Display) is found: Control OLED Display (show color info)
// 4. Wait 2 seconds and repeat

// This demonstrates:
// - Auto-detection of Child Blocks
// - Multi-device coordination via I2C commands
// - Modularity: Brain can work with any combination of Child Blocks
void comm_task(void *arg) {
    vTaskDelay(pdMS_TO_TICKS(2000)); // Wait for children
    
    while (1) {
        ESP_LOGI(TAG, "\n--- NEW CYCLE ---");
        
        // Scan to see what's connected right now
        i2c_safe_scan();
        
        // Child Block 1 - LED Matrix (Address 0x08)
        // If Child 1 responds to ping, its connected and ready
        if (i2c_ping(CHILD_1_ADDR) == ESP_OK) {

            ESP_LOGI(TAG, "Child 1 detected!");

            // Set brightness to 30% (So its not blinding during demo and to show we can control power)
            ESP_LOGI(TAG, "Child 1: Setting brightness to 30%%");
            i2c_matrix_set_brightness(CHILD_1_ADDR, 76);  // 76/255 = ~30%
            vTaskDelay(pdMS_TO_TICKS(500));
            
            // Cycle through primary colors
            // RED - Fills all 16 LEDS with red
            ESP_LOGI(TAG, "Child 1: RED");
            i2c_matrix_fill(CHILD_1_ADDR, 255, 0, 0);
            vTaskDelay(pdMS_TO_TICKS(1000));
            
            //GREEN
            ESP_LOGI(TAG, "Child 1: GREEN");
            i2c_matrix_fill(CHILD_1_ADDR, 0, 255, 0);
            vTaskDelay(pdMS_TO_TICKS(1000));
            
            //BLUE
            ESP_LOGI(TAG, "Child 1: BLUE");
            i2c_matrix_fill(CHILD_1_ADDR, 0, 0, 255);
            vTaskDelay(pdMS_TO_TICKS(1000));
            
            //CLEAR - turn off all LEDs
            ESP_LOGI(TAG, "Child 1: CLEAR");
            i2c_matrix_clear(CHILD_1_ADDR);
            vTaskDelay(pdMS_TO_TICKS(500));
        }
        
        // Child Block 2 - OLED Display (Address 0x09)
        // If Child 2 responds to ping, its connected and ready
        // Child 2 will show what colors are being displayed on the LED Matrix
        if (i2c_ping(CHILD_2_ADDR) == ESP_OK) {

            ESP_LOGI(TAG, "Child 2 detected!");

            // Same commands, except Child 2 interprets them to show info on OLED
            // Instead of lighting LEDs, it updates the OLED display
            ESP_LOGI(TAG, "Child 2: Setting brightness");
            i2c_matrix_set_brightness(CHILD_2_ADDR, 76);
            vTaskDelay(pdMS_TO_TICKS(500));
            
            // Send different colors to demo - OLED will show these on screen
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
        
        // Wait 2 seconds before starting next cycle
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

// Main Application Entry Point
// Runs once at app startup - initializes I2C master and starts comm task
void app_main(void) {
    ESP_LOGI(TAG, "=== BRAIN BLOCK ===");
    
    // Initialize I2C Master interface
    ESP_ERROR_CHECK(i2c_master_init());
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // Do initial I2C scan to see what's connected
    i2c_safe_scan();
    
    // Create the communication task (runs continuously in background)
    // Stack size : 4096 bytes, Priority : 5 (high)
    xTaskCreate(comm_task, "comm", 4096, NULL, 5, NULL);
}