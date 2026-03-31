#include <stdio.h>
#include <stddef.h>

#include "driver/i2c.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "i2c_protocol.h"

// Command dispatcher implemented in main.c
extern void command_handle(i2c_command_t cmd,
                           const uint8_t *rx,
                           size_t rx_len,
                           uint8_t *tx,
                           size_t *tx_len);
extern uint8_t note_block_get_status_flags(void);
extern uint8_t note_block_get_pending_event_len(void);

static const char *TAG = "NOTE_BLOCK";

// Fixed child-bus address/type for the Note block.
#define MY_ADDRESS      0x0E
#define MY_BLOCK_TYPE   BLOCK_TYPE_NOTE

// Simple register map for Brain-side WHOAMI/status reads.
static uint8_t s_registers[16] = {0};

static void init_registers(void)
{
    s_registers[REG_WHOAMI]   = MY_BLOCK_TYPE;
    s_registers[REG_STATUS]   = STATUS_READY;
    s_registers[REG_FW_MAJOR] = 1;
    s_registers[REG_FW_MINOR] = 0;
    s_registers[REG_DATA_LEN] = 0;
}

/**
 * @brief Returns nonzero if the provided byte corresponds to a valid
 *        i2c_command_t value.
 *
 * This is used to disambiguate 1-byte writes that could otherwise be
 * interpreted as either a command ID or a register index.
 */
static int is_command_byte(uint8_t b)
{
    switch ((i2c_command_t)b) {
        case CMD_PING:
<<<<<<< HEAD
=======
        case CMD_GET_STATUS:
>>>>>>> origin/main
        case CMD_GET_TYPE:
        case CMD_SET_LED:
        case CMD_GET_STATUS:
        case CMD_GET_DATA:
        case CMD_PLAY_NOTE:
        case CMD_EXECUTE:
        case CMD_RESET:
        case CMD_SET_DELAY:
        case CMD_SET_LOOP:
        case CMD_MATRIX_FILL:
        case CMD_MATRIX_SET_PIXEL:
        case CMD_MATRIX_CLEAR:
        case CMD_MATRIX_SET_ROW:
        case CMD_MATRIX_SET_COLUMN:
        case CMD_MATRIX_DRAW_PATTERN:
        case CMD_MATRIX_BRIGHTNESS:
        case CMD_MATRIX_SHOW:
            return 1;
        default:
            return 0;
    }
}

static void refresh_dynamic_registers(void)
{
    s_registers[REG_STATUS] = note_block_get_status_flags();
    s_registers[REG_DATA_LEN] = note_block_get_pending_event_len();
}

static bool is_register_index_byte(uint8_t v)
{
    return (v < 0x10U);
}

// ============================================================================
// I²C SLAVE INITIALIZATION
// ============================================================================
esp_err_t i2c_slave_init(void)
{
    ESP_LOGI(TAG, "Init I2C Slave at 0x%02X (type=%s)",
             MY_ADDRESS, block_type_to_string(MY_BLOCK_TYPE));

    init_registers();

    i2c_config_t conf = {
        .mode = I2C_MODE_SLAVE,
        .sda_io_num = I2C_SDA_PIN,
        .scl_io_num = I2C_SCL_PIN,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .slave.addr_10bit_en = 0,
        .slave.slave_addr = MY_ADDRESS,
    };

    esp_err_t err = i2c_param_config(I2C_PORT_NUM, &conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C config failed: %s", esp_err_to_name(err));
        return err;
    }

    err = i2c_driver_install(I2C_PORT_NUM, conf.mode, 128, 128, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C driver install failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "I2C slave initialized");
    return ESP_OK;
}

// ============================================================================
// I²C RECEIVE TASK - Handles commands and register reads
// ============================================================================
void i2c_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "i2c_task running on core %d", xPortGetCoreID());

    uint8_t rx_buf[128];
    // NOTE block may return up to 17 bytes for a custom sequence:
    //   [event_id, count, note0..note14]
    uint8_t tx_buf[32];

    while (1) {
        int len = i2c_slave_read_buffer(I2C_PORT_NUM, rx_buf, sizeof(rx_buf), pdMS_TO_TICKS(100));
        if (len <= 0) {
            continue;
        }

        ESP_LOGI(TAG, "Received %d bytes: [0]=0x%02X", len, rx_buf[0]);

        // Treat single-byte buffers that contain only a register index
        // specially and reply with the value of that register.
        refresh_dynamic_registers();
        if ((len == 1) && is_register_index_byte(rx_buf[0]) && !is_command_byte(rx_buf[0])) {
            uint8_t reg = rx_buf[0];
            uint8_t value = s_registers[reg];

            (void)i2c_slave_write_buffer(I2C_PORT_NUM, &value, 1, pdMS_TO_TICKS(100));

            ESP_LOGI(TAG, "Register 0x%02X -> 0x%02X", reg, value);
            continue;
        }

        size_t tx_len = 0;
        i2c_command_t cmd = (i2c_command_t)rx_buf[0];
        const uint8_t *payload = (len > 1) ? &rx_buf[1] : NULL;
        size_t payload_len = (len > 1) ? (size_t)(len - 1) : 0U;

        command_handle(cmd, payload, payload_len, tx_buf, &tx_len);

        if (tx_len > sizeof(tx_buf)) {
            ESP_LOGE(TAG,
                     "command_handle() attempted to set tx_len=%u (max %u); dropping response",
                     (unsigned)tx_len,
                     (unsigned)sizeof(tx_buf));
            continue;
        }

        if (tx_len > 0U) {
            (void)i2c_slave_write_buffer(I2C_PORT_NUM, tx_buf, tx_len, pdMS_TO_TICKS(100));
            ESP_LOGI(TAG, "Sent %u response bytes", (unsigned)tx_len);
        }

        size_t tx_len = 0;
        i2c_command_t cmd = (i2c_command_t)rx_buf[0];
        const uint8_t *payload = (len > 1) ? &rx_buf[1] : NULL;
        size_t payload_len = (len > 1) ? (size_t)(len - 1) : 0U;

        command_handle(cmd, payload, payload_len, tx_buf, &tx_len);

        if (tx_len > 0U) {
            (void)i2c_slave_write_buffer(I2C_PORT_NUM, tx_buf, tx_len, pdMS_TO_TICKS(100));
            ESP_LOGI(TAG, "Sent %u response bytes", (unsigned)tx_len);
        }
    }
}
