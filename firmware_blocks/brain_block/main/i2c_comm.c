// Transport Layer for I²C Communication

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
// Allow more time for other I2C users (event poll, config scan, etc.).
#define I2C_MUTEX_TIMEOUT_MS 500

// Recovery pulses if a slave is holding SDA low
#define I2C_BUS_RECOVERY_SCL_PULSES 9
#define I2C_BUS_RECOVERY_DELAY_MS   2

// Scan window for development boards; must match device_registry.h
#define DEVICE_REGISTRY_ADDR_MIN    0x08
#define DEVICE_REGISTRY_ADDR_MAX    0x16

static esp_err_t i2c_bus_recover_locked(void) {
    // NOTE: caller should hold the recursive I2C mutex so no other task uses I2C during recovery.
    ESP_LOGW(TAG, "I2C bus timeout; attempting bus recovery (toggle SCL %d pulses)", I2C_BUS_RECOVERY_SCL_PULSES);

    // Best-effort delete; ignore failures (driver may already be stopped).
    (void)i2c_driver_delete(I2C_PORT_NUM);

    // Temporarily switch pins to GPIO open-drain mode and clock out stuck bits.
    gpio_set_direction(I2C_SDA_PIN, GPIO_MODE_INPUT_OUTPUT_OD);
    gpio_set_pull_mode(I2C_SDA_PIN, GPIO_PULLUP_ONLY);
    gpio_set_direction(I2C_SCL_PIN, GPIO_MODE_INPUT_OUTPUT_OD);
    gpio_set_pull_mode(I2C_SCL_PIN, GPIO_PULLUP_ONLY);

    gpio_set_level(I2C_SDA_PIN, 1);
    gpio_set_level(I2C_SCL_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(I2C_BUS_RECOVERY_DELAY_MS));

    for (int i = 0; i < I2C_BUS_RECOVERY_SCL_PULSES; i++) {
        gpio_set_level(I2C_SCL_PIN, 0);
        vTaskDelay(pdMS_TO_TICKS(I2C_BUS_RECOVERY_DELAY_MS));
        gpio_set_level(I2C_SCL_PIN, 1);
        vTaskDelay(pdMS_TO_TICKS(I2C_BUS_RECOVERY_DELAY_MS));
    }

    // Re-init I2C master driver (restores pins to I2C function).
    esp_err_t ret = i2c_master_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C bus recovery: re-init failed (%s)", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "I2C bus recovery: re-init OK");
    return ESP_OK;
}

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

static esp_err_t i2c_send_payload(uint8_t address, const uint8_t *data, size_t len, TickType_t timeout_ticks) {
    if (data == NULL || len == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

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
    i2c_master_write_byte(cmd, (address << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write(cmd, data, len, true);
    i2c_master_stop(cmd);

    esp_err_t ret = i2c_master_cmd_begin(I2C_PORT_NUM, cmd, timeout_ticks);
    if (ret == ESP_ERR_TIMEOUT) {
        // This is a bus timeout (not the mutex timeout), since we already acquired the lock.
        (void)i2c_bus_recover_locked();
    }
    i2c_cmd_link_delete(cmd);
    i2c_unlock();
    return ret;
}

// ============================================================================
// I²C MASTER INITIALIZATION
// ============================================================================
esp_err_t i2c_master_init(void) {
    ESP_LOGI(TAG, "Init I²C Master: SDA=%d, SCL=%d", I2C_SDA_PIN, I2C_SCL_PIN);

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

    // Harden I2C master against slow edges / chatter.
    // - max timeout: tolerate slow rise times / clock stretching
    // - glitch filter: reject short pulses on SCL/SDA
    // - drive strength: improve edges when using pogo pins / long traces
    (void)i2c_set_timeout(I2C_PORT_NUM, 0xFFFFF);
    (void)i2c_filter_enable(I2C_PORT_NUM, 7);
    (void)gpio_set_drive_capability(I2C_SDA_PIN, GPIO_DRIVE_CAP_3);
    (void)gpio_set_drive_capability(I2C_SCL_PIN, GPIO_DRIVE_CAP_3);

    if (s_i2c_mutex == NULL) {
        s_i2c_mutex = xSemaphoreCreateRecursiveMutex();
        if (s_i2c_mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    return ESP_OK;
}

// ============================================================================
// I²C PING
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

    /* Short timeout so missing device (removal) is detected quickly (~25 ms) */
    esp_err_t ret = i2c_master_cmd_begin(I2C_PORT_NUM, cmd, pdMS_TO_TICKS(25));
    if (ret == ESP_ERR_TIMEOUT) {
        // This is a bus timeout (not the mutex timeout), since we already acquired the lock.
        (void)i2c_bus_recover_locked();
    }
    i2c_cmd_link_delete(cmd);
    i2c_unlock();
    return ret;
}

// ============================================================================
// I²C SCAN
// ============================================================================
void i2c_safe_scan(void) {
    ESP_LOGI(TAG, "=== SAFE I²C SCAN ===");
    int found = 0;

    for (uint8_t addr = DEVICE_REGISTRY_ADDR_MIN; addr <= DEVICE_REGISTRY_ADDR_MAX; addr++) {
        if (i2c_ping(addr) == ESP_OK) {
            ESP_LOGI(TAG, "Detected device at 0x%02X", addr);
            found++;
        }
    }

    ESP_LOGI(TAG, "Devices detected: %d", found);
}

// ============================================================================
// I2C WHOAMI READ REGISTER
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
        if (ret == ESP_ERR_TIMEOUT) {
            (void)i2c_bus_recover_locked();
        }
        i2c_unlock();
        return ret;
    }

    // Small delay between write and read
    vTaskDelay(pdMS_TO_TICKS(5));

    // Read data from register
    ret = i2c_master_read_from_device(
        I2C_PORT_NUM,
        addr,
        out,
        len,
        pdMS_TO_TICKS(50)
    );
    if (ret == ESP_ERR_TIMEOUT) {
        (void)i2c_bus_recover_locked();
    }
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
    if (ret == ESP_ERR_TIMEOUT) {
        (void)i2c_bus_recover_locked();
    }
    i2c_cmd_link_delete(cmd);
    i2c_unlock();
    return ret;
}

// ============================================================================
// MATRIX SHOW
// ============================================================================
esp_err_t i2c_matrix_show(uint8_t address) {
    esp_err_t lock_ret = i2c_lock();
    if (lock_ret != ESP_OK) {
        return lock_ret;
    }

    uint8_t data[1] = {CMD_MATRIX_SHOW};
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
    if (ret == ESP_ERR_TIMEOUT) {
        (void)i2c_bus_recover_locked();
    }
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
    if (ret == ESP_ERR_TIMEOUT) {
        (void)i2c_bus_recover_locked();
    }
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
    if (ret == ESP_ERR_TIMEOUT) {
        (void)i2c_bus_recover_locked();
    }
    i2c_cmd_link_delete(cmd);
    i2c_unlock();
    return ret;
}

// ============================================================================
// SET LED BY COLOR ID (palette index)
// ============================================================================
esp_err_t i2c_set_led_color_id(uint8_t address, uint8_t color_id) {
    uint8_t data[2] = {CMD_SET_LED, color_id};
    return i2c_send_payload(address, data, sizeof(data), pdMS_TO_TICKS(100));
}

// ============================================================================
// PLAY NOTE (note_id)
// ============================================================================
esp_err_t i2c_play_note(uint8_t address, uint8_t note_id) {
    uint8_t data[2] = {CMD_PLAY_NOTE, note_id};
    return i2c_send_payload(address, data, sizeof(data), pdMS_TO_TICKS(100));
}

esp_err_t i2c_runtime_broadcast(uint8_t address,
                                brain_runtime_broadcast_state_t state,
                                uint8_t pc,
                                block_type_t step_type) {
    uint8_t data[BRAIN_RUNTIME_BROADCAST_PAYLOAD_LEN + 1] = {
        CMD_RUNTIME_BROADCAST,
        (uint8_t)state,
        pc,
        (uint8_t)step_type,
    };
    return i2c_send_payload(address, data, sizeof(data), pdMS_TO_TICKS(100));
}

// ============================================================================
// SET LED (RGB)
// ============================================================================
esp_err_t i2c_set_led(uint8_t address, uint8_t r, uint8_t g, uint8_t b) {
    uint8_t data[4] = {CMD_SET_LED, r, g, b};
    return i2c_send_payload(address, data, sizeof(data), pdMS_TO_TICKS(100));
}

// ============================================================================
// GET DATA (event payload from child)
// ============================================================================
esp_err_t i2c_get_data(uint8_t addr, uint8_t *out, size_t len) {
    if (out == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t lock_ret = i2c_lock();
    if (lock_ret != ESP_OK) {
        return lock_ret;
    }

    uint8_t cmd_byte = CMD_GET_DATA;

    // Send CMD_GET_DATA
    esp_err_t ret = i2c_master_write_to_device(
        I2C_PORT_NUM,
        addr,
        &cmd_byte,
        1,
        pdMS_TO_TICKS(50)
    );
    if (ret != ESP_OK) {
        if (ret == ESP_ERR_TIMEOUT) {
            (void)i2c_bus_recover_locked();
        }
        i2c_unlock();
        return ret;
    }

    // Small delay between write and read to let child prepare payload
    vTaskDelay(pdMS_TO_TICKS(5));

    // Read response payload
    ret = i2c_master_read_from_device(
        I2C_PORT_NUM,
        addr,
        out,
        len,
        pdMS_TO_TICKS(50)
    );
    if (ret == ESP_ERR_TIMEOUT) {
        (void)i2c_bus_recover_locked();
    }

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
    if (ret == ESP_ERR_TIMEOUT) {
        (void)i2c_bus_recover_locked();
    }
    i2c_cmd_link_delete(cmd);
    i2c_unlock();
    return ret;
}

// ============================================================================
// EXECUTE (Trigger configured action)
// ============================================================================
esp_err_t i2c_execute(uint8_t address) {
    return i2c_send_cmd(address, CMD_EXECUTE);
}

// ============================================================================
// RESET (optional helper)
// ============================================================================
esp_err_t i2c_reset(uint8_t address) {
    return i2c_send_cmd(address, CMD_RESET);
}

esp_err_t i2c_set_child_address(uint8_t current_address, uint8_t new_address) {
    if (!block_is_valid_child_address(current_address) ||
        !block_is_valid_child_address(new_address)) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t data[2] = {CMD_SET_I2C_ADDRESS, new_address};
    return i2c_send_payload(current_address, data, sizeof(data), pdMS_TO_TICKS(100));
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

    uint8_t len = strlen(msg);
    if (len > 30) {
        len = 30;
    }

    uint8_t data[32];
    data[0] = CMD_OLED_TEXT;
    data[1] = len;
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
    if (ret == ESP_ERR_TIMEOUT) {
        (void)i2c_bus_recover_locked();
    }
    i2c_cmd_link_delete(cmd);
    i2c_unlock();
    return ret;
}
