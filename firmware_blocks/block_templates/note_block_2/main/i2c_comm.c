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
#define I2C_VERBOSE_LOGS 0

// Fixed child-bus address/type for the Note block (see BLOCK_BOOT_I2C_ADDR_NOTE_BLOCK).
#define MY_ADDRESS      BLOCK_BOOT_I2C_ADDR_NOTE_BLOCK_2
#define MY_BLOCK_TYPE   BLOCK_TYPE_NOTE

// Simple register map for Brain-side WHOAMI/status reads.
static uint8_t s_registers[16] = {0};
static uint8_t s_runtime_address = 0u;
static uint32_t s_device_uid = 0u;

static void populate_identity_registers(void)
{
    if (s_device_uid == 0u) {
        s_device_uid = block_compute_device_uid(MY_BLOCK_TYPE);
    }

    s_registers[REG_UID0] = (uint8_t)(s_device_uid & 0xFFu);
    s_registers[REG_UID1] = (uint8_t)((s_device_uid >> 8) & 0xFFu);
    s_registers[REG_UID2] = (uint8_t)((s_device_uid >> 16) & 0xFFu);
    s_registers[REG_UID3] = (uint8_t)((s_device_uid >> 24) & 0xFFu);
    s_registers[REG_ASSIGNED_ADDR] = s_runtime_address;
}

static void init_registers(void)
{
    if (s_runtime_address == 0u) {
        s_runtime_address = BLOCK_BOOT_I2C_ADDR_NOTE_BLOCK_2;
    }
    s_registers[REG_WHOAMI]   = MY_BLOCK_TYPE;
    s_registers[REG_STATUS]   = STATUS_READY;
    s_registers[REG_FW_MAJOR] = 1;
    s_registers[REG_FW_MINOR] = 0;
    s_registers[REG_DATA_LEN] = 0;
    populate_identity_registers();
}

static esp_err_t rebind_i2c_slave_address(uint8_t new_address)
{
    if (!block_is_valid_child_address(new_address)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (new_address == s_runtime_address) {
        return ESP_OK;
    }

    (void)i2c_driver_delete(I2C_PORT_NUM);

    i2c_config_t conf = {
        .mode = I2C_MODE_SLAVE,
        .sda_io_num = I2C_SDA_PIN,
        .scl_io_num = I2C_SCL_PIN,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .slave.addr_10bit_en = 0,
        .slave.slave_addr = new_address,
    };

    esp_err_t err = i2c_param_config(I2C_PORT_NUM, &conf);
    if (err != ESP_OK) {
        return err;
    }

    err = i2c_driver_install(I2C_PORT_NUM, conf.mode, 128, 128, 0);
    if (err != ESP_OK) {
        return err;
    }

    s_runtime_address = new_address;
    s_registers[REG_ASSIGNED_ADDR] = new_address;
    ESP_LOGI(TAG, "Rebound child address to 0x%02X", new_address);
    return ESP_OK;
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
        case CMD_GET_TYPE:
        case CMD_SET_LED:
        case CMD_GET_STATUS:
        case CMD_GET_DATA:
        case CMD_PLAY_NOTE:
        case CMD_EXECUTE:
        case CMD_RESET:
        case CMD_SET_DELAY:
        case CMD_SET_LOOP:
        case CMD_SET_I2C_ADDRESS:
        case CMD_MATRIX_FILL:
        case CMD_MATRIX_SET_PIXEL:
        case CMD_MATRIX_CLEAR:
        case CMD_MATRIX_SET_ROW:
        case CMD_MATRIX_SET_COLUMN:
        case CMD_MATRIX_DRAW_PATTERN:
        case CMD_MATRIX_BRIGHTNESS:
        case CMD_MATRIX_SHOW:
        case CMD_RUNTIME_BROADCAST:
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

static size_t command_frame_len(i2c_command_t cmd)
{
    switch (cmd) {
        case CMD_PING:
        case CMD_GET_TYPE:
        case CMD_GET_STATUS:
        case CMD_GET_DATA:
        case CMD_EXECUTE:
        case CMD_RESET:
        case CMD_MATRIX_CLEAR:
        case CMD_MATRIX_SHOW:
            return 1U;
        case CMD_PLAY_NOTE:
        case CMD_SET_LED:  // Palette color-id payload in this firmware path.
        case CMD_SET_DELAY:
        case CMD_SET_LOOP:
        case CMD_SET_I2C_ADDRESS:
        case CMD_MATRIX_BRIGHTNESS:
            return 2U;
        case CMD_MATRIX_FILL:
        case CMD_RUNTIME_BROADCAST:
            return 4U;
        default:
            return 0U;
    }
}

#if I2C_VERBOSE_LOGS
static void log_rx_bytes(const uint8_t *buf, size_t len)
{
    char line[3 * 128 + 1];
    size_t out = 0U;
    for (size_t i = 0; i < len && i < 128U; i++) {
        if (out + 4U >= sizeof(line)) {
            break;
        }
        int n = snprintf(&line[out], sizeof(line) - out, "%02X%s", buf[i], (i + 1U < len) ? " " : "");
        if (n <= 0) {
            break;
        }
        out += (size_t)n;
    }
    line[(out < sizeof(line)) ? out : (sizeof(line) - 1U)] = '\0';
    ESP_LOGI(TAG, "RX raw len=%u bytes=[%s]", (unsigned)len, line);
}
#endif

// ============================================================================
// I²C SLAVE INITIALIZATION
// ============================================================================
esp_err_t i2c_slave_init(void)
{
    ESP_LOGI(TAG, "Init I2C Slave at 0x%02X (type=%s)",
             s_runtime_address, block_type_to_string(MY_BLOCK_TYPE));

    init_registers();

    i2c_config_t conf = {
        .mode = I2C_MODE_SLAVE,
        .sda_io_num = I2C_SDA_PIN,
        .scl_io_num = I2C_SCL_PIN,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .slave.addr_10bit_en = 0,
        .slave.slave_addr = s_runtime_address,
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
    uint8_t rx_carry[16];
    size_t rx_carry_len = 0U;
    // NOTE block may return up to 17 bytes for a custom sequence:
    //   [event_id, count, note0..note14]
    uint8_t work_buf[sizeof(rx_buf) + sizeof(rx_carry)];
    uint8_t tx_buf[32];

    while (1) {
        int len = i2c_slave_read_buffer(I2C_PORT_NUM, rx_buf, sizeof(rx_buf), pdMS_TO_TICKS(100));
        if (len <= 0) {
            continue;
        }

        size_t read_len = (size_t)len;
        if (rx_carry_len + read_len > sizeof(work_buf)) {
            ESP_LOGW(TAG,
                     "RX carry overflow (carry=%u read=%u cap=%u); dropping carry",
                     (unsigned)rx_carry_len,
                     (unsigned)read_len,
                     (unsigned)sizeof(work_buf));
            rx_carry_len = 0U;
        }
        if (rx_carry_len > 0U) {
            memcpy(work_buf, rx_carry, rx_carry_len);
        }
        memcpy(work_buf + rx_carry_len, rx_buf, read_len);
        size_t total_len = rx_carry_len + read_len;
        rx_carry_len = 0U;

        refresh_dynamic_registers();
#if I2C_VERBOSE_LOGS
        log_rx_bytes(work_buf, total_len);
#endif

        size_t offset = 0U;
        while (offset < total_len) {
            uint8_t head = work_buf[offset];

            if (is_register_index_byte(head)) {
                refresh_dynamic_registers();
                uint8_t value = s_registers[head];
#if I2C_VERBOSE_LOGS
                ESP_LOGI(TAG,
                         "RX frame off=%u type=reg_read reg=0x%02X frame_len=1",
                         (unsigned)offset,
                         (unsigned)head);
#endif
                (void)i2c_reset_tx_fifo(I2C_PORT_NUM);
                (void)i2c_slave_write_buffer(I2C_PORT_NUM, &value, 1, 0);
                offset += 1U;
                continue;
            }

            i2c_command_t cmd = (i2c_command_t)head;
            if (!is_command_byte(head)) {
                ESP_LOGW(TAG,
                         "RX frame off=%u unknown command byte 0x%02X; dropping %u trailing bytes",
                         (unsigned)offset,
                         (unsigned)head,
                         (unsigned)(total_len - offset));
                break;
            }

            size_t frame_len = command_frame_len(cmd);
            if (frame_len == 0U) {
                ESP_LOGW(TAG,
                         "RX frame off=%u unsupported command 0x%02X; dropping %u trailing bytes",
                         (unsigned)offset,
                         (unsigned)head,
                         (unsigned)(total_len - offset));
                break;
            }
            if (offset + frame_len > total_len) {
                size_t remain = total_len - offset;
                if (remain <= sizeof(rx_carry)) {
                    memcpy(rx_carry, &work_buf[offset], remain);
                    rx_carry_len = remain;
                } else {
                    ESP_LOGW(TAG,
                             "RX frame off=%u truncated command 0x%02X remain=%u exceeds carry=%u; dropping",
                             (unsigned)offset,
                             (unsigned)head,
                             (unsigned)remain,
                             (unsigned)sizeof(rx_carry));
                    rx_carry_len = 0U;
                }
                ESP_LOGW(TAG,
                         "RX frame off=%u truncated command 0x%02X expected=%u available=%u; carrying=%u",
                         (unsigned)offset,
                         (unsigned)head,
                         (unsigned)frame_len,
                         (unsigned)(total_len - offset),
                         (unsigned)rx_carry_len);
                break;
            }

            const uint8_t *payload = (frame_len > 1U) ? &work_buf[offset + 1U] : NULL;
            size_t payload_len = (frame_len > 1U) ? (frame_len - 1U) : 0U;
#if I2C_VERBOSE_LOGS
            ESP_LOGI(TAG,
                     "RX frame off=%u type=cmd cmd=0x%02X frame_len=%u payload_len=%u",
                     (unsigned)offset,
                     (unsigned)head,
                     (unsigned)frame_len,
                     (unsigned)payload_len);
#endif

            if (cmd == CMD_SET_I2C_ADDRESS) {
                esp_err_t err = rebind_i2c_slave_address(payload[0]);
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to apply assigned address 0x%02X: %s",
                             payload[0], esp_err_to_name(err));
                }
                offset += frame_len;
                continue;
            }

            refresh_dynamic_registers();
            uint8_t status_before = s_registers[REG_STATUS];

            size_t tx_len = 0U;
            command_handle(cmd, payload, payload_len, tx_buf, &tx_len);

            refresh_dynamic_registers();
            uint8_t status_after = s_registers[REG_STATUS];
            if (status_before != status_after) {
#if I2C_VERBOSE_LOGS
                ESP_LOGI(TAG,
                         "Status transition cmd=0x%02X old=0x%02X new=0x%02X",
                         (unsigned)head,
                         (unsigned)status_before,
                         (unsigned)status_after);
#endif
            }

            if (tx_len > sizeof(tx_buf)) {
                ESP_LOGE(TAG,
                         "command_handle() attempted to set tx_len=%u (max %u); dropping response",
                         (unsigned)tx_len,
                         (unsigned)sizeof(tx_buf));
                offset += frame_len;
                continue;
            }

            if (tx_len > 0U) {
                (void)i2c_reset_tx_fifo(I2C_PORT_NUM);
                (void)i2c_slave_write_buffer(I2C_PORT_NUM, tx_buf, tx_len, pdMS_TO_TICKS(100));
#if I2C_VERBOSE_LOGS
                ESP_LOGI(TAG, "Sent %u response bytes", (unsigned)tx_len);
#endif
            }

            offset += frame_len;
        }
    }
}
