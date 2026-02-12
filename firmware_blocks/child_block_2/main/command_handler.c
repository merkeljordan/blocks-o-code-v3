#include "i2c_protocol.h"
#include "esp_log.h"
#include <stdint.h>
#include <stdbool.h>

// Forward declarations from oled_display.c
extern void display_set_color(uint8_t r, uint8_t g, uint8_t b);
extern void display_set_brightness(uint8_t brightness);
extern void display_set_command(const char *cmd_name);

static const char *TAG = "CMD_HANDLER";

// ============================================================================
// VALIDATE COMMAND
// ============================================================================
bool is_valid_command(uint8_t cmd) {
    return (cmd == CMD_PING || 
            cmd == CMD_SET_LED || 
            cmd == CMD_MATRIX_FILL ||
            cmd == CMD_MATRIX_BRIGHTNESS ||
            cmd == CMD_MATRIX_CLEAR ||
            cmd == 0xF0);
}

// ============================================================================
// HANDLE COMMAND
// ============================================================================
void handle_command(uint8_t *buffer, int len) {
    if (len < 1) return;
    
    uint8_t cmd = buffer[0];
    ESP_LOGI(TAG, "Command: 0x%02X, Length: %d bytes", cmd, len);
    
    switch (cmd) {
        case CMD_PING:
            ESP_LOGI(TAG, "  → PING");
            display_set_command("PING");
            break;
            
        case CMD_SET_LED:
        case CMD_MATRIX_FILL:
            if (len >= 4) {
                uint8_t r = buffer[1];
                uint8_t g = buffer[2];
                uint8_t b = buffer[3];
                
                display_set_color(r, g, b);
                ESP_LOGI(TAG, "  → Color: RGB(%d,%d,%d)", r, g, b);
            }
            break;
            
        case CMD_MATRIX_BRIGHTNESS:
            if (len >= 2) {
                uint8_t brightness = buffer[1];
                display_set_brightness(brightness);
                ESP_LOGI(TAG, "  → Brightness: %d", brightness);
            }
            break;
            
        case CMD_MATRIX_CLEAR:
            display_set_color(0, 0, 0);
            display_set_command("Clear Matrix");
            ESP_LOGI(TAG, "  → CLEAR");
            break;
            
        case 0xF0:
            ESP_LOGI(TAG, "  → Custom command");
            break;
            
        default:
            ESP_LOGW(TAG, "  → Unknown command: 0x%02X", cmd);
            break;
    }
}