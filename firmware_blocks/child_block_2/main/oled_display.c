#include "ssd1306.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_err.h"
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

// Pin definitions
#define OLED_MOSI_PIN   11
#define OLED_SCLK_PIN   12
#define OLED_DC_PIN     16
#define OLED_RST_PIN    17
#define OLED_CS_PIN     5

static const char *TAG = "OLED_DISPLAY";

// Module-private state
static SSD1306_t oled_dev;
static SemaphoreHandle_t display_mutex = NULL;
static uint8_t led_r = 0, led_g = 0, led_b = 0;
static uint8_t brightness = 50;
static uint8_t num_devices = 2;
static uint32_t uptime_seconds = 0;
static char current_command[50] = "Waiting...";
static bool device_list[4] = {false};

// ============================================================================
// HELPER: Get Color Name
// ============================================================================
static const char* get_color_name(uint8_t r, uint8_t g, uint8_t b) {
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

// ============================================================================
// OLED INITIALIZATION
// ============================================================================
esp_err_t oled_display_init(void) {
    ESP_LOGI(TAG, "Initializing SPI OLED display...");
    
    // Create mutex first
    display_mutex = xSemaphoreCreateMutex();
    if (display_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create display mutex!");
        return ESP_FAIL;
    }
    
    // Set device to SPI mode
    oled_dev._address = SPI_ADDRESS;
    
    // Initialize SPI
    spi_master_init(&oled_dev, 
                    OLED_MOSI_PIN,
                    OLED_SCLK_PIN,
                    OLED_CS_PIN,
                    OLED_DC_PIN,
                    OLED_RST_PIN);
    
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Initialize display
    spi_init(&oled_dev, 128, 64);
    ssd1306_clear_screen(&oled_dev, false);
    ssd1306_contrast(&oled_dev, 0xFF);
    
    // Startup screen
    ssd1306_display_text(&oled_dev, 0, "   INITIALIZING", 15, false);
    ssd1306_display_text(&oled_dev, 2, "  Child Block 2", 15, false);
    ssd1306_display_text(&oled_dev, 3, "  OLED Display", 14, false);
    ssd1306_display_text(&oled_dev, 5, "  Address: 0x09", 15, false);
    ssd1306_display_text(&oled_dev, 7, "    Ready!", 10, false);
    
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // Initialize device list
    device_list[0] = true;  // LED Matrix
    device_list[1] = true;  // OLED Display
    
    ESP_LOGI(TAG, "OLED initialized successfully!");
    return ESP_OK;
}

// ============================================================================
// DISPLAY DASHBOARD
// ============================================================================
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
    
    // Current Command
    ssd1306_display_text(&oled_dev, 4, "Command:", 8, false);
    char cmd_str[64];
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

// ============================================================================
// DISPLAY UPDATE TASK
// ============================================================================
void display_task(void *arg) {
    ESP_LOGI(TAG, "Display update task started");
    
    while (1) {
        uptime_seconds++;
        
        if (xSemaphoreTake(display_mutex, pdMS_TO_TICKS(1000))) {
            display_dashboard();
            xSemaphoreGive(display_mutex);
        }
        
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// ============================================================================
// SETTERS (Called by command_handler)
// ============================================================================
void display_set_color(uint8_t r, uint8_t g, uint8_t b) {
    led_r = r;
    led_g = g;
    led_b = b;
    
    const char* color_name = get_color_name(r, g, b);
    snprintf(current_command, sizeof(current_command), "Fill %s", color_name);
}

void display_set_brightness(uint8_t b) {
    brightness = b;
    snprintf(current_command, sizeof(current_command), "Brightness %d%%", 
             (brightness * 100) / 255);
}

void display_set_command(const char *cmd_name) {
    snprintf(current_command, sizeof(current_command), "%s", cmd_name);
}

// ============================================================================
// GET MUTEX (For command_handler to use)
// ============================================================================
SemaphoreHandle_t display_get_mutex(void) {
    return display_mutex;
}