#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "ssd1306.h"  // OLED driver
#include "i2c_protocol.h"

static const char *TAG = "CHILD_2";

// Block Identity
#define MY_ADDRESS      0x09
#define MY_BLOCK_TYPE   BLOCK_TYPE_DISPLAY

// OLED Configuration (uses different I2C as master to talk to OLED)
#define OLED_I2C_NUM     I2C_NUM_1
#define OLED_SDA_PIN     21
#define OLED_SCL_PIN     22
#define OLED_I2C_ADDR    0x3C

// State Variables
static uint8_t led_r = 0, led_g = 0, led_b = 0;
static uint8_t brightness = 50;
static uint8_t num_devices = 0;
static uint32_t uptime_seconds = 0;
static char current_command[32] = "Waiting...";
static bool device_list[4] = {false}; // Track up to 4 devices
static SSD1306_t oled_dev;

// ------ OLED Helper Functions ------

const char* get_color_name(uint8_t r, uint8_t g, uint8_t b) {
    if (r > 200 && g < 50 && b < 50) return "RED";
    if (g > 200 && r < 50 && b < 50) return "GREEN";
    if (b > 200 && r < 50 && g < 50) return "BLUE";
    if (r > 200 && g > 200 && b < 50) return "YELLOW";
    if (r > 200 && b > 200 && g < 50) return "MAGENTA";
    if (g > 200 && b > 200 && r < 50) return "CYAN";
    if (r > 200 && g > 200 && b > 200) return "WHITE";
    if (r < 30 && g < 30 && b < 30) return "OFF";
    return "CUSTOM";
}

void display_dashboard(void) {
    ssd1306_clear_screen(&oled_dev, false);
    
    // Header
    ssd1306_display_text(&oled_dev, 0, "CONTROL SYSTEM", 14, false);
    ssd1306_display_text(&oled_dev, 1, "==================", 18, false);
    
    // Device Status
    char devices_str[32];
    snprintf(devices_str, sizeof(devices_str), "Devices: %d online", num_devices);
    ssd1306_display_text(&oled_dev, 2, devices_str, strlen(devices_str), false);
    
    if (device_list[0]) {
        ssd1306_display_text(&oled_dev, 3, " LED Matrix  [OK]", 17, false);
    }
    if (device_list[1]) {
        ssd1306_display_text(&oled_dev, 3, " OLED Disp   [OK]", 17, false);
    }
    
    // Current Command
    ssd1306_display_text(&oled_dev, 4, "Command:", 8, false);
    char cmd_str[32];
    snprintf(cmd_str, sizeof(cmd_str), " %s", current_command);
    ssd1306_display_text(&oled_dev, 5, cmd_str, strlen(cmd_str), false);
    
    // Color Info
    char color_str[32];
    snprintf(color_str, sizeof(color_str), " RGB(%d,%d,%d)", led_r, led_g, led_b);
    ssd1306_display_text(&oled_dev, 6, color_str, strlen(color_str), false);
    
    // Uptime
    char uptime_str[32];
    uint32_t mins = uptime_seconds / 60;
    uint32_t secs = uptime_seconds % 60;
    snprintf(uptime_str, sizeof(uptime_str), "Uptime: %02lu:%02lu", mins, secs);
    ssd1306_display_text(&oled_dev, 7, uptime_str, strlen(uptime_str), false);
}

// ------ OLED Initialization ------

esp_err_t oled_init(void) {
    ESP_LOGI(TAG, "Initializing OLED display...");
    
    // Initialize I2C for OLED (as master on different pins)
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = OLED_SDA_PIN,
        .scl_io_num = OLED_SCL_PIN,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 400000,
    };
    
    esp_err_t err = i2c_param_config(OLED_I2C_NUM, &conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C config failed: %s", esp_err_to_name(err));
        return err;
    }
    
    err = i2c_driver_install(OLED_I2C_NUM, conf.mode, 0, 0, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C driver install failed: %s", esp_err_to_name(err));
        return err;
    }
    
    // Initialize SSD1306 OLED
    ssd1306_init(&oled_dev, 128, 64);
    i2c_master_init(&oled_dev, OLED_I2C_NUM, OLED_SDA_PIN, OLED_SCL_PIN, -1);
    
    ssd1306_clear_screen(&oled_dev, false);
    ssd1306_contrast(&oled_dev, 0xFF);
    
    // Startup screen
    ssd1306_display_text(&oled_dev, 0, "   INITIALIZING", 15, false);
    ssd1306_display_text(&oled_dev, 2, "  Child Block 2", 15, false);
    ssd1306_display_text(&oled_dev, 3, "  OLED Display", 14, false);
    ssd1306_display_text(&oled_dev, 5, "  Address: 0x09", 15, false);
    ssd1306_display_text(&oled_dev, 7, "    Ready!", 10, false);
    
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    ESP_LOGI(TAG, "OLED initialized successfully!");
    return ESP_OK;
}

// ------ Command Handler ------

void handle_command(uint8_t *buffer, int len) {
    if (len < 1) return;
    
    uint8_t cmd = buffer[0];
    
    ESP_LOGI(TAG, "Command: 0x%02X, Length: %d bytes", cmd, len);
    
    switch (cmd) {
        case CMD_PING:
            ESP_LOGI(TAG, "  → PING");
            snprintf(current_command, sizeof(current_command), "PING");
            break;
            
        case CMD_SET_LED:
        case CMD_MATRIX_FILL:
            if (len >= 4) {
                led_r = buffer[1];
                led_g = buffer[2];
                led_b = buffer[3];
                const char* color_name = get_color_name(led_r, led_g, led_b);
                snprintf(current_command, sizeof(current_command), "Fill %s", color_name);
                ESP_LOGI(TAG, "  → Color: %s RGB(%d,%d,%d)", color_name, led_r, led_g, led_b);
            }
            break;
            
        case CMD_MATRIX_BRIGHTNESS:
            if (len >= 2) {
                brightness = buffer[1];
                snprintf(current_command, sizeof(current_command), "Brightness %d%%", (brightness * 100) / 255);
                ESP_LOGI(TAG, "  → Brightness: %d", brightness);
            }
            break;
            
        case CMD_MATRIX_CLEAR:
            led_r = 0;
            led_g = 0;
            led_b = 0;
            snprintf(current_command, sizeof(current_command), "Clear Matrix");
            ESP_LOGI(TAG, "  → CLEAR");
            break;
            
        // Custom command to update device count
        case 0xF0:  // CMD_UPDATE_DEVICE_COUNT
            if (len >= 2) {
                num_devices = buffer[1];
                ESP_LOGI(TAG, "  → Device count: %d", num_devices);
            }
            break;
            
        default:
            ESP_LOGW(TAG, "  → Unknown command: 0x%02X", cmd);
            break;
    }
    
    // Update display after command
    display_dashboard();
}

// ------ Initialize I²C Slave ------

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

// ------ I²C Receive Task ------

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

// ------ Display Update Task ------

void display_task(void *arg) {
    ESP_LOGI(TAG, "Display update task started");
    
    while (1) {
        uptime_seconds++;
        display_dashboard();
        vTaskDelay(pdMS_TO_TICKS(1000));  // Update every second
    }
}

// ------ Main ------

void app_main(void) {
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "    CHILD BLOCK 2 - OLED DISPLAY");
    ESP_LOGI(TAG, "    Address: 0x%02X", MY_ADDRESS);
    ESP_LOGI(TAG, "    Type: %s", block_type_to_string(MY_BLOCK_TYPE));
    ESP_LOGI(TAG, "========================================");
    
    // Initialize OLED FIRST (on I2C_NUM_1)
    esp_err_t ret = oled_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize OLED!");
        return;
    }
    
    // Initialize I²C slave (on I2C_NUM_0)
    ret = i2c_slave_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize I²C slave!");
        return;
    }
    
    // Set initial device count
    num_devices = 2;  // Assume Brain detects us + LED matrix
    device_list[0] = true;  // LED Matrix
    device_list[1] = true;  // Us (OLED)
    
    vTaskDelay(pdMS_TO_TICKS(500));
    
    ESP_LOGI(TAG, "Child Block 2 ready and waiting for commands!");
    ESP_LOGI(TAG, "");
    
    // Create tasks
    xTaskCreate(i2c_task, "i2c", 4096, NULL, 5, NULL);
    xTaskCreate(display_task, "display", 4096, NULL, 3, NULL);
    
    ESP_LOGI(TAG, "All tasks created successfully!");
}