#include <stdio.h>
#include <stddef.h>

#include "driver/i2c.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "i2c_protocol.h"

// Forward declaration from main.c (music block command logic stays there for now).
extern void command_handle(i2c_command_t cmd,
                           const uint8_t *rx,
                           size_t rx_len,
                           uint8_t *tx,
                           size_t *tx_len);
extern uint8_t music_block_get_status_flags(void);

static const char *TAG = "MUSIC_SEQUENCE_BLOCK";
#define I2C_VERBOSE_LOGS 0
#define I2C_SLAVE_RX_BUF_SIZE 256
#define I2C_SLAVE_TX_BUF_SIZE 256

// Fixed child-bus address/type for the music sequence block.
#define MY_ADDRESS      0x12
#define MY_BLOCK_TYPE   BLOCK_TYPE_MUSIC_SEQ

// Simple register map for Brain-side WHOAMI/status reads.
static uint8_t s_registers[16] = {0};

static void init_registers(void)
{
    s_registers[REG_WHOAMI] = MY_BLOCK_TYPE;
    s_registers[REG_STATUS] = STATUS_READY;
    s_registers[REG_FW_MAJOR] = 1;
    s_registers[REG_FW_MINOR] = 0;
}

static void refresh_dynamic_registers(void)
{
    s_registers[REG_STATUS] = music_block_get_status_flags();
}

static void reset_i2c_fifos(void)
{
    (void)i2c_reset_rx_fifo(I2C_PORT_NUM);
    (void)i2c_reset_tx_fifo(I2C_PORT_NUM);
}


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

    err = i2c_driver_install(I2C_PORT_NUM,
                             conf.mode,
                             I2C_SLAVE_RX_BUF_SIZE,
                             I2C_SLAVE_TX_BUF_SIZE,
                             0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C driver install failed: %s", esp_err_to_name(err));
        return err;
    }

    reset_i2c_fifos();

    ESP_LOGI(TAG, "I2C slave initialized");
    return ESP_OK;
}

void i2c_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "i2c_task running on core %d", xPortGetCoreID());
    reset_i2c_fifos();

    uint8_t rx_buf[128];
    uint8_t tx_buf[64];

    while (1) {
        int len = i2c_slave_read_buffer(I2C_PORT_NUM, rx_buf, sizeof(rx_buf), pdMS_TO_TICKS(100));
        if (len <= 0) {
            continue;
        }

#if I2C_VERBOSE_LOGS
        ESP_LOGI(TAG, "Received %d bytes: [0]=0x%02X", len, rx_buf[0]);
#endif

        // Keep STATUS register synced with runtime state managed in main.c.
        refresh_dynamic_registers();

        // Single-byte message with value < 0x10 is a register read request.
        if (len == 1 && rx_buf[0] < 0x10) {
            uint8_t reg = rx_buf[0];
            uint8_t value = s_registers[reg];

            (void)i2c_slave_write_buffer(I2C_PORT_NUM, &value, 1, pdMS_TO_TICKS(100));

#if I2C_VERBOSE_LOGS
            ESP_LOGI(TAG, "Register 0x%02X -> 0x%02X", reg, value);
#endif
            continue;
        }

        size_t tx_len = 0;
        i2c_command_t cmd = (i2c_command_t)rx_buf[0];
        const uint8_t *payload = (len > 1) ? &rx_buf[1] : NULL;
        size_t payload_len = (len > 1) ? (size_t)(len - 1) : 0U;

        command_handle(cmd, payload, payload_len, tx_buf, &tx_len);

        if (tx_len > 0U) {
            (void)i2c_slave_write_buffer(I2C_PORT_NUM, tx_buf, tx_len, pdMS_TO_TICKS(100));
#if I2C_VERBOSE_LOGS
            ESP_LOGI(TAG, "Sent %u response bytes", (unsigned)tx_len);
#endif
        }
    }
}
