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
extern uint8_t delay_block_get_status_flags(void);
extern uint8_t delay_block_get_pending_data_len(void);
extern uint32_t delay_block_get_delay_ms_for_brain(void);

static const char *TAG = "DELAY_BLOCK";

// TODO: Change per block
#define MY_ADDRESS      BLOCK_BOOT_I2C_ADDR_DELAY_BLOCK
#define MY_BLOCK_TYPE   BLOCK_TYPE_DELAY

// ============================================================================
// REGISTER MAP - What the Brain can read from us
// ============================================================================
static uint8_t registers[16] = {0};
static uint8_t s_runtime_address = 0u;
static uint32_t s_device_uid = 0u;

static void populate_identity_registers(void) {
    if (s_device_uid == 0u) {
        s_device_uid = block_compute_device_uid(MY_BLOCK_TYPE);
    }

    registers[REG_UID0] = (uint8_t)(s_device_uid & 0xFFu);
    registers[REG_UID1] = (uint8_t)((s_device_uid >> 8) & 0xFFu);
    registers[REG_UID2] = (uint8_t)((s_device_uid >> 16) & 0xFFu);
    registers[REG_UID3] = (uint8_t)((s_device_uid >> 24) & 0xFFu);
    registers[REG_ASSIGNED_ADDR] = s_runtime_address;
}

static void init_registers(void) {
    if (s_runtime_address == 0u) {
        s_runtime_address = BLOCK_BOOT_I2C_ADDR_DELAY_BLOCK;
    }
    registers[REG_WHOAMI]   = MY_BLOCK_TYPE;  // Block type
    registers[REG_STATUS]   = STATUS_READY;   // Status
    registers[REG_FW_MAJOR] = 1;              // Firmware major
    registers[REG_FW_MINOR] = 0;              // Firmware minor
    populate_identity_registers();
}

static void refresh_dynamic_registers(void)
{
    registers[REG_STATUS] = delay_block_get_status_flags();
    registers[REG_DATA_LEN] = delay_block_get_pending_data_len();
    uint32_t ms = delay_block_get_delay_ms_for_brain();
    registers[REG_DELAY_MS0] = (uint8_t)(ms & 0xFFu);
    registers[REG_DELAY_MS1] = (uint8_t)((ms >> 8) & 0xFFu);
    registers[REG_DELAY_MS2] = (uint8_t)((ms >> 16) & 0xFFu);
    registers[REG_DELAY_MS3] = (uint8_t)((ms >> 24) & 0xFFu);
}

static esp_err_t rebind_i2c_slave_address(uint8_t new_address) {
    if (!block_is_valid_child_address(new_address)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (new_address == s_runtime_address) {
        return ESP_OK;
    }

    (void)i2c_driver_delete(I2C_NUM_0);

    i2c_config_t conf = {
        .mode = I2C_MODE_SLAVE,
        .sda_io_num = I2C_SDA_PIN,
        .scl_io_num = I2C_SCL_PIN,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .slave.addr_10bit_en = 0,
        .slave.slave_addr = new_address,
    };

    esp_err_t err = i2c_param_config(I2C_NUM_0, &conf);
    if (err != ESP_OK) {
        return err;
    }

    err = i2c_driver_install(I2C_NUM_0, conf.mode, 128, 128, 0);
    if (err != ESP_OK) {
        return err;
    }

    s_runtime_address = new_address;
    registers[REG_ASSIGNED_ADDR] = new_address;
    ESP_LOGI(TAG, "Rebound child address to 0x%02X", new_address);
    return ESP_OK;
}

// ============================================================================
// I²C SLAVE INITIALIZATION
// ============================================================================
esp_err_t i2c_slave_init(void) {
    /* Release port if Arduino/Wire or a prior boot path already installed a driver. */
    (void)i2c_driver_delete(I2C_NUM_0);

    init_registers();

    ESP_LOGI(TAG, "Init I²C Slave at 0x%02X (type=%s)",
             s_runtime_address, block_type_to_string(MY_BLOCK_TYPE));

    i2c_config_t conf = {
        .mode = I2C_MODE_SLAVE,
        .sda_io_num = I2C_SDA_PIN,
        .scl_io_num = I2C_SCL_PIN,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .slave.addr_10bit_en = 0,
        .slave.slave_addr = s_runtime_address,
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

    ESP_LOGI(TAG, "I²C slave initialized!");
    return ESP_OK;
}

// ============================================================================
// FRAME PARSER - Handles coalesced I²C FIFO reads
// ============================================================================

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
        case CMD_SET_LED:
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

static void reset_i2c_fifos(void)
{
    (void)i2c_reset_rx_fifo(I2C_NUM_0);
    (void)i2c_reset_tx_fifo(I2C_NUM_0);
}

// ============================================================================
// I²C RECEIVE TASK - Frame-based parser (handles FIFO coalescence)
// ============================================================================
void i2c_task(void *arg) {
    (void)arg;
    ESP_LOGI(TAG, "i2c_task running on core %d", xPortGetCoreID());
    reset_i2c_fifos();

    uint8_t rx_buf[128];
    uint8_t rx_carry[16];
    size_t rx_carry_len = 0U;
    uint8_t work_buf[sizeof(rx_buf) + sizeof(rx_carry)];
    uint8_t tx_buf[16];

    while (1) {
        int len = i2c_slave_read_buffer(I2C_NUM_0, rx_buf, sizeof(rx_buf), pdMS_TO_TICKS(100));
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

        size_t offset = 0U;
        while (offset < total_len) {
            uint8_t head = work_buf[offset];

            if (is_register_index_byte(head)) {
                refresh_dynamic_registers();
                uint8_t value = registers[head];
                (void)i2c_reset_tx_fifo(I2C_NUM_0);
                (void)i2c_slave_write_buffer(I2C_NUM_0, &value, 1, 0);
                if (head == REG_STATUS || head == REG_DATA_LEN) {
                    ESP_LOGI(TAG, "REG READ: reg=0x%02X -> 0x%02X (g_status=0x%02X pending_len=%u)",
                             head, value,
                             delay_block_get_status_flags(),
                             (unsigned)delay_block_get_pending_data_len());
                }
                offset += 1U;
                continue;
            }

            i2c_command_t cmd = (i2c_command_t)head;
            if (!is_command_byte(head)) {
                ESP_LOGW(TAG,
                         "RX frame off=%u unknown byte 0x%02X; dropping %u trailing bytes",
                         (unsigned)offset,
                         (unsigned)head,
                         (unsigned)(total_len - offset));
                break;
            }

            size_t frame_len = command_frame_len(cmd);
            if (frame_len == 0U) {
                ESP_LOGW(TAG,
                         "RX frame off=%u unsupported cmd 0x%02X; dropping %u trailing bytes",
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
                }
                ESP_LOGW(TAG,
                         "RX frame off=%u truncated cmd 0x%02X expected=%u available=%u; carrying=%u",
                         (unsigned)offset,
                         (unsigned)head,
                         (unsigned)frame_len,
                         (unsigned)(total_len - offset),
                         (unsigned)rx_carry_len);
                break;
            }

            const uint8_t *payload = (frame_len > 1U) ? &work_buf[offset + 1U] : NULL;
            size_t payload_len = (frame_len > 1U) ? (frame_len - 1U) : 0U;

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

            size_t tx_len = 0U;
            command_handle(cmd, payload, payload_len, tx_buf, &tx_len);

            if (tx_len > 0U) {
                (void)i2c_reset_tx_fifo(I2C_NUM_0);
                (void)i2c_slave_write_buffer(I2C_NUM_0, tx_buf, tx_len, pdMS_TO_TICKS(100));

                /* CMD_GET_DATA responses are multi-byte (up to 5). If the master
                 * clocks fewer bytes than we wrote, the leftovers sit in the TX
                 * FIFO and poison the next register read (the master sees
                 * stale payload bytes instead of the requested register).
                 * Give the master time to finish reading, then flush residuals. */
                if (cmd == CMD_GET_DATA) {
                    vTaskDelay(pdMS_TO_TICKS(5));
                    (void)i2c_reset_tx_fifo(I2C_NUM_0);
                    ESP_LOGI(TAG, "CMD_GET_DATA: flushed residual TX FIFO after response");
                }
            } else {
                (void)i2c_reset_tx_fifo(I2C_NUM_0);
            }

            offset += frame_len;
        }
    }
}
