#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "led_strip.h"
#include "i2c_protocol.h"

static const char *TAG = "CHILD_1";

// Block Identity
#define MY_ADDRESS      0x08
#define MY_BLOCK_TYPE   BLOCK_TYPE_LED

// LED Matric Configuration 
#define LED_GPIO            18 // Data pin for LED matrix 
#define LED_MATRIX_SIZE     16 //4x4 = 16 LEDs
#define LED_MATRIX_WIDTH    4 // 4 wide
#define LED_MATRIX_HEIGHT   4 // 4 tall

// State Variables
static uint8_t led_r = 0, led_g = 0, led_b = 0;
static uint8_t current_status = STATUS_READY;
static uint8_t matrix_brightness = 50; // 0 - 255 (starting around 20%)
static led_strip_handle_t led_strip = NULL;
// ------ LED Matrix Initialization ------

esp_err_t led_matrix_init(void) {
    ESP_LOGI(TAG, "Initializing LED Matrix (4x4 = 16 LEDs) on GPIO%d", LED_GPIO);
    
    // LED strip configuration for WS2812B
    led_strip_config_t strip_config = {
        .strip_gpio_num = LED_GPIO,
        .max_leds = LED_MATRIX_SIZE,
        .led_pixel_format = LED_PIXEL_FORMAT_GRB,  // WS2812B uses GRB
        .led_model = LED_MODEL_WS2812,
        .flags.invert_out = false,
    };

    // RMT configuration for WS2812B timing
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,  // 10MHz
        .flags.with_dma = false,
    };
    
    esp_err_t err = led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create LED strip: %s", esp_err_to_name(err));
        return err;
    }
    
    // Clear matrix on startup
    led_strip_clear(led_strip);
    
    ESP_LOGI(TAG, "LED Matrix initialized successfully!");
    return ESP_OK;
}

// ------- LED Matrix Helper Functions --------
void matrix_fill(uint8_t r, uint8_t g, uint8_t b) {
    ESP_LOGI(TAG, "Filling matrix RGB(%d, %d, %d) @ brightness %d", r, g, b, matrix_brightness);
    
    // Apply brightness
    r = (r * matrix_brightness) / 255;
    g = (g * matrix_brightness) / 255;
    b = (b * matrix_brightness) / 255;
    
    for (int i = 0; i < LED_MATRIX_SIZE; i++) {
        led_strip_set_pixel(led_strip, i, r, g, b);
    }
}

void matrix_clear(void) {
    ESP_LOGI(TAG, "Clearing matrix");
    led_strip_clear(led_strip);
}

void matrix_show(void) {
    led_strip_refresh(led_strip);
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

// Command Handler
void handle_command(uint8_t *buffer, int len) {
    if (len < 1) return;
    
    uint8_t cmd = buffer[0];
    
    ESP_LOGI(TAG, "Command: %s (0x%02X), Length: %d bytes", 
             command_to_string(cmd), cmd, len);
    
    switch (cmd) {
        case CMD_PING:
            ESP_LOGI(TAG, "  PING");
            current_status = STATUS_READY;
            break;
            
        case CMD_GET_TYPE:
            ESP_LOGI(TAG, "  GET_TYPE");
            break;
            
        case CMD_SET_LED:
            if (len >= 4) {
                led_r = buffer[1];
                led_g = buffer[2];
                led_b = buffer[3];
                ESP_LOGI(TAG, "  SET_LED RGB(%d, %d, %d)", led_r, led_g, led_b);
            }
            break;
            
        // LED MATRIX COMMANDS
        case CMD_MATRIX_FILL:
            if (len >= 4) {
                uint8_t r = buffer[1];
                uint8_t g = buffer[2];
                uint8_t b = buffer[3];
                matrix_fill(r, g, b);
                matrix_show();
            }
            break;
            
        case CMD_MATRIX_CLEAR:
            matrix_clear();
            matrix_show();
            break;
            
        case CMD_MATRIX_BRIGHTNESS:
            if (len >= 2) {
                matrix_brightness = buffer[1];
                ESP_LOGI(TAG, "  BRIGHTNESS set to %d", matrix_brightness);
            }
            break;
            
        case CMD_MATRIX_SHOW:
            matrix_show();
            break;
            
        case CMD_RESET:
            ESP_LOGI(TAG, "  RESET");
            led_r = 0;
            led_g = 0;
            led_b = 0;
            matrix_clear();
            matrix_show();
            current_status = STATUS_READY;
            break;
            
        default:
            ESP_LOGW(TAG, "  Unknown command: 0x%02X", cmd);
            break;
    }
}


// I²C receive task
void i2c_task(void *arg) {
    uint8_t buffer[128];
    
    while (1) {
        int len = i2c_slave_read_buffer(I2C_NUM_0, buffer, 128, pdMS_TO_TICKS(100));
        
        if (len > 0) {
            ESP_LOGI(TAG, "Received %d bytes", len);
            
            // Call the command handler
            handle_command(buffer, len);
        }
    }
}

// LED status task
void led_task(void *arg) {
    ESP_LOGI(TAG, "LED status task started");
    
    while (1) {
        ESP_LOGI(TAG, " Status: %s | LED: RGB(%d,%d,%d) | Brightness: %d%%",
                 (current_status & STATUS_READY) ? "READY" : "BUSY",
                 led_r, led_g, led_b,
                 (matrix_brightness * 100) / 255);
        
        vTaskDelay(pdMS_TO_TICKS(10000));  // Every 10 seconds
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "    CHILD BLOCK 1 - LED MATRIX");
    ESP_LOGI(TAG, "    Address: 0x%02X", MY_ADDRESS);
    ESP_LOGI(TAG, "    Type: %s", block_type_to_string(MY_BLOCK_TYPE));
    ESP_LOGI(TAG, "========================================");
    
    // Initialize LED Matrix FIRST
    esp_err_t ret = led_matrix_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize LED matrix!");
        return;
    }
    
    // Show startup animation (3 red flashes)
    ESP_LOGI(TAG, "Startup animation...");
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < LED_MATRIX_SIZE; j++) {
            led_strip_set_pixel(led_strip, j, 10, 0, 0);  // Dim red
        }
        led_strip_refresh(led_strip);
        vTaskDelay(pdMS_TO_TICKS(200));
        
        led_strip_clear(led_strip);
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    
    // Initialize I²C slave
    ret = i2c_slave_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize I²C slave!");
        return;
    }
    
    vTaskDelay(pdMS_TO_TICKS(500));
    
    ESP_LOGI(TAG, "Child Block 1 ready and waiting for commands!");
    ESP_LOGI(TAG, "");
    
    // Create tasks
    xTaskCreate(i2c_task, "i2c", 4096, NULL, 5, NULL);
    xTaskCreate(led_task, "led_status", 2048, NULL, 3, NULL);
    
    ESP_LOGI(TAG, "All tasks created successfully!");
}