// Transport layer for I2C communication.

#include <stdio.h>
#include <string.h>
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "i2c_protocol.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static const char *TAG = "I2C_MASTER";

static SemaphoreHandle_t s_i2c_mutex = NULL;
#define I2C_MUTEX_TIMEOUT_MS 150

static esp_err_t i2c_lock(void) {
    if (s_i2c_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTakeRecursive(s_i2c_mutex, pdMS_TO_TICKS(I2C_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

static void i2c_unlock(void) {
    if (s_i2c_mutex != NULL) {
        xSemaphoreGiveRecursive(s_i2c_mutex);
    }
}

// ============================================================================
// I2C MASTER INITIALIZATION
// ============================================================================
esp_err_t i2c_master_init(void) {
    ESP_LOGI(TAG, "Init I2C Master: SDA=%d, SCL=%d", I2C_SDA_PIN, I2C_SCL_PIN);

    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_SDA_PIN,
        .scl_io_num = I2C_SCL_PIN,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_FREQ_HZ,
    };

    esp_err_t ret = i2c_param_config(I2C_PORT_NUM, &conf);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = i2c_driver_install(I2C_PORT_NUM, conf.mode, 0, 0, 0);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        return ret;
    }

    if (s_i2c_mutex == NULL) {
        s_i2c_mutex = xSemaphoreCreateRecursiveMutex();
        if (s_i2c_mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    return ESP_OK;
}

// ============================================================================
// I2C PING
// ============================================================================
esp_err_t i2c_ping(uint8_t addr) {
    esp_err_t lock_ret = i2c_lock();
    if (lock_ret != ESP_OK) {
        return lock_ret;
    }

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    if (cmd == NULL) {
        i2c_unlock();
        return ESP_ERR_NO_MEM;
    }

    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_stop(cmd);

    esp_err_t ret = i2c_master_cmd_begin(I2C_PORT_NUM, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    i2c_unlock();
    return ret;
}

// ============================================================================
// I2C SCAN
// ============================================================================
void i2c_safe_scan(void) {
    ESP_LOGI(TAG, "=== SAFE I2C SCAN ===");
    int found = 0;

    for (uint8_t addr = 0x08; addr <= 0x0F; addr++) {
        if (i2c_ping(addr) == ESP_OK) {
            ESP_LOGI(TAG, "Detected device at 0x%02X", addr);
            found++;
        }
    }

    ESP_LOGI(TAG, "Devices detected: %d", found);
}

// ============================================================================
// I2C REGISTER READ
// ============================================================================
esp_err_t i2c_read_reg(uint8_t addr, uint8_t reg, uint8_t *out, size_t len) {
    esp_err_t lock_ret = i2c_lock();
    if (lock_ret != ESP_OK) {
        return lock_ret;
    }

    esp_err_t ret = i2c_master_write_to_device(
        I2C_PORT_NUM,
        addr,
        &reg,
        1,
        pdMS_TO_TICKS(50)
    );
    if (ret != ESP_OK) {
        i2c_unlock();
        return ret;
    }

    vTaskDelay(pdMS_TO_TICKS(5));

    ret = i2c_master_read_from_device(
        I2C_PORT_NUM,
        addr,
        out,
        len,
        pdMS_TO_TICKS(50)
    );
    i2c_unlock();
    return ret;
}

// ============================================================================
// MATRIX FILL
// ============================================================================
esp_err_t i2c_matrix_fill(uint8_t address, uint8_t r, uint8_t g, uint8_t b) {
    esp_err_t lock_ret = i2c_lock();
    if (lock_ret != ESP_OK) {
        return lock_ret;
    }

    uint8_t data[4] = {CMD_MATRIX_FILL, r, g, b};
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    if (cmd == NULL) {
        i2c_unlock();
        return ESP_ERR_NO_MEM;
    }

    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (address << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write(cmd, data, 4, true);
    i2c_master_stop(cmd);

    esp_err_t ret = i2c_master_cmd_begin(I2C_PORT_NUM, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    i2c_unlock();
    return ret;
}

// ============================================================================
// MATRIX CLEAR
// ============================================================================
esp_err_t i2c_matrix_clear(uint8_t address) {
    esp_err_t lock_ret = i2c_lock();
    if (lock_ret != ESP_OK) {
        return lock_ret;
    }

    uint8_t data[1] = {CMD_MATRIX_CLEAR};
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    if (cmd == NULL) {
        i2c_unlock();
        return ESP_ERR_NO_MEM;
    }

    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (address << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write(cmd, data, 1, true);
    i2c_master_stop(cmd);

    esp_err_t ret = i2c_master_cmd_begin(I2C_PORT_NUM, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    i2c_unlock();
    return ret;
}

// ============================================================================
// MATRIX BRIGHTNESS
// ============================================================================
esp_err_t i2c_matrix_set_brightness(uint8_t address, uint8_t brightness) {
    esp_err_t lock_ret = i2c_lock();
    if (lock_ret != ESP_OK) {
        return lock_ret;
    }

    uint8_t data[2] = {CMD_MATRIX_BRIGHTNESS, brightness};
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    if (cmd == NULL) {
        i2c_unlock();
        return ESP_ERR_NO_MEM;
    }

    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (address << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write(cmd, data, 2, true);
    i2c_master_stop(cmd);

    esp_err_t ret = i2c_master_cmd_begin(I2C_PORT_NUM, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    i2c_unlock();
    return ret;
}

// ============================================================================
// GENERIC COMMAND (no payload)
// ============================================================================
static esp_err_t i2c_send_cmd(uint8_t address, uint8_t cmd_byte) {
    esp_err_t lock_ret = i2c_lock();
    if (lock_ret != ESP_OK) {
        return lock_ret;
    }

    uint8_t data[1] = {cmd_byte};
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    if (cmd == NULL) {
        i2c_unlock();
        return ESP_ERR_NO_MEM;
    }

    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (address << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write(cmd, data, 1, true);
    i2c_master_stop(cmd);

    esp_err_t ret = i2c_master_cmd_begin(I2C_PORT_NUM, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    i2c_unlock();
    return ret;
}

// ============================================================================
// SET LED COLOR ID (LED_FLASH block)
// ============================================================================
esp_err_t i2c_set_led_color_id(uint8_t address, uint8_t color_id) {
    esp_err_t lock_ret = i2c_lock();
    if (lock_ret != ESP_OK) {
        return lock_ret;
    }

    uint8_t data[2] = {CMD_SET_LED, color_id};
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    if (cmd == NULL) {
        i2c_unlock();
        return ESP_ERR_NO_MEM;
    }

    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (address << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write(cmd, data, 2, true);
    i2c_master_stop(cmd);

    esp_err_t ret = i2c_master_cmd_begin(I2C_PORT_NUM, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    i2c_unlock();
    return ret;
}

// ============================================================================
// EXECUTE / RESET
// ============================================================================
esp_err_t i2c_execute(uint8_t address) {
    return i2c_send_cmd(address, CMD_EXECUTE);
}

esp_err_t i2c_reset(uint8_t address) {
    return i2c_send_cmd(address, CMD_RESET);
}

// ============================================================================
// OLED TEXT
// ============================================================================
esp_err_t i2c_oled_text(uint8_t address, const char *msg) {
    if (msg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t lock_ret = i2c_lock();
    if (lock_ret != ESP_OK) {
        return lock_ret;
    }

    size_t len = strlen(msg);
    if (len > 30) {
        len = 30;
    }

    uint8_t data[32];
    data[0] = CMD_OLED_TEXT;
    data[1] = (uint8_t)len;
    memcpy(&data[2], msg, len);

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    if (cmd == NULL) {
        i2c_unlock();
        return ESP_ERR_NO_MEM;
    }

    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (address << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write(cmd, data, len + 2, true);
    i2c_master_stop(cmd);

    esp_err_t ret = i2c_master_cmd_begin(I2C_PORT_NUM, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    i2c_unlock();
    return ret;
}
