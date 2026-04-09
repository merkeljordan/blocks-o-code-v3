#include "esp_err.h"
#include "esp_log.h"
#include <stdint.h>
#include <stdio.h>
#include "i2c_protocol.h"
#include "block_config_manager.h"

static const char* TAG = "MOCK_I2C";

static uint8_t mock_status_register = STATUS_READY;

// ERROR INJECTION CONTROLS
uint8_t g_mock_i2c_fail_address = 0x00;    // If set, this address will return ESP_FAIL
int     g_mock_i2c_busy_delay_ms = 0;      // If > 0, status will stay BUSY for this long
uint64_t g_mock_i2c_busy_start_ms = 0;

// SPY TRACKING:
int g_mock_i2c_execute_counts[256] = {0};

void mock_i2c_reset_spies(void) {
    for (int i = 0; i < 256; i++) {
        g_mock_i2c_execute_counts[i] = 0;
    }
    g_mock_i2c_fail_address = 0x00;
    g_mock_i2c_busy_delay_ms = 0;
    g_mock_i2c_busy_start_ms = 0;
}


esp_err_t i2c_set_led_color_id(uint8_t address, uint8_t color_id) {
    ESP_LOGI(TAG, "i2c_set_led_color_id(addr=0x%02X, color_id=%u)", address, color_id);
    return ESP_OK;
}

esp_err_t i2c_execute(uint8_t address) {
    if (address == g_mock_i2c_fail_address) return ESP_FAIL;
    ESP_LOGI(TAG, "i2c_execute(addr=0x%02X)", address);
    g_mock_i2c_execute_counts[address]++;
    
    mock_status_register = STATUS_BUSY;
    extern uint64_t now_ms(void);
    g_mock_i2c_busy_start_ms = now_ms();
    
    return ESP_OK;
}

esp_err_t i2c_play_note(uint8_t address, uint8_t note_id) {
    ESP_LOGI(TAG, "i2c_play_note(addr=0x%02X, note_id=%u)", address, note_id);
    return ESP_OK;
}

esp_err_t i2c_runtime_broadcast(uint8_t address,
                                brain_runtime_broadcast_state_t state,
                                uint8_t pc,
                                block_type_t step_type) {
    ESP_LOGI(TAG, "i2c_runtime_broadcast(addr=0x%02X, state=%u, pc=%u, step_type=%u)", 
             address, state, pc, step_type);
    return ESP_OK;
}

esp_err_t i2c_read_reg(uint8_t addr, uint8_t reg, uint8_t *out, size_t len) {
    if (addr == g_mock_i2c_fail_address) return ESP_FAIL;
    if (len == 0) return ESP_FAIL;
    
    if (reg == REG_STATUS) {
        if (g_mock_i2c_busy_delay_ms > 0) {
            extern uint64_t now_ms(void);
            if (now_ms() - g_mock_i2c_busy_start_ms < (uint64_t)g_mock_i2c_busy_delay_ms) {
                *out = STATUS_BUSY;
                return ESP_OK;
            }
        }
        *out = mock_status_register;
        mock_status_register = STATUS_READY;
        return ESP_OK;
    }
    if (reg == REG_WHOAMI) {
        const block_config_state_t* cfg = block_config_manager_get_state();
        if (cfg) {
            for (int i=0; i<cfg->block_count; i++) {
                if (cfg->blocks[i].i2c_address == addr) {
                    *out = cfg->blocks[i].block_type;
                    return ESP_OK;
                }
            }
        }
        *out = BLOCK_TYPE_UNKNOWN;
        return ESP_OK;
    }
    if (reg == REG_LOOP_COUNT) {
        *out = 3; 
        return ESP_OK;
    }
    return ESP_FAIL;
}

esp_err_t i2c_reset(uint8_t addr) {
    ESP_LOGI(TAG, "i2c_reset(addr=0x%02X)", addr);
    return ESP_OK;
}

esp_err_t i2c_matrix_set_brightness(uint8_t address, uint8_t brightness) {
    return ESP_OK;
}