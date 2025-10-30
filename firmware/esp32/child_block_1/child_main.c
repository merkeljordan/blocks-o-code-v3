// child_main.c — ESP-IDF (v4/v5 legacy I2C slave API) Child block
// Role: I2C SLAVE endpoint that fronts local SPI peripherals.
// Emulates simple register map for the Brain master.

#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c.h"          // legacy API (works on IDF 4.x and 5.x)
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"

// ====== CONFIG (edit for your PCB) ==========================================
#define I2C_PORT            I2C_NUM_0
#define I2C_SDA_PIN         21
#define I2C_SCL_PIN         22
#define I2C_SLAVE_ADDR      0x30     // unique per Child (0x30..0x3E)

#define I2C_RX_BUF_LEN      128
#define I2C_TX_BUF_LEN      128

#define GPIO_INT_N          4        // shared open-drain INT# to Brain (optional)
#define USE_INT_LINE        0        // set 1 if wired (remember pull-up)

// Local SPI (stub device)
// VSPI default pins on many devkits: SCK=18, MISO=19, MOSI=23; choose a CS:
#define SPI_HOST_USED       VSPI_HOST
#define SPI_SCK_PIN         18
#define SPI_MISO_PIN        19
#define SPI_MOSI_PIN        23
#define SPI_CS_PIN          5

// ====== Register Map ========================================================
// 0x00 WHOAMI: [0]=0xC1, [1]=FW_VER
// 0x01 STATUS: bit0 DATA_RDY, bit1 ERR
// 0x02 CMD:    write-only; 0x01 = sample sensor (SPI)
// 0x03 LEN:    number of data bytes in DATA
// 0x04.. DATA: payload bytes, followed by CRC8 when Brain requests
#define REG_WHOAMI          0x00
#define REG_STATUS          0x01
#define REG_CMD             0x02
#define REG_LEN             0x03
#define REG_DATA            0x04

#define ST_DATA_RDY         (1<<0)
#define ST_ERR              (1<<1)

#define FW_VER              1
#define MAX_PAYLOAD         64

static const char* TAG = "CHILD";

// ====== Globals backing the register file ===================================
static uint8_t g_status = 0;
static uint8_t g_len    = 0;
static uint8_t g_data[MAX_PAYLOAD];   // payload (CRC8 is computed on the fly)

// Optional INT# helper
static inline void int_assert(void){
#if USE_INT_LINE
    gpio_set_level(GPIO_INT_N, 0); // active low
#endif
}
static inline void int_deassert(void){
#if USE_INT_LINE
    gpio_set_level(GPIO_INT_N, 1);
#endif
}

// ====== CRC8 (poly 0x07) ====================================================
static uint8_t crc8(const uint8_t* p, size_t n){
    uint8_t crc = 0x00;
    for(size_t i=0;i<n;i++){
        crc ^= p[i];
        for(int b=0;b<8;b++)
            crc = (crc & 0x80) ? (uint8_t)((crc<<1)^0x07) : (uint8_t)(crc<<1);
    }
    return crc;
}

// ====== SPI stub: read a few bytes from a device (replace with real driver) ==
static spi_device_handle_t g_spi;

static void spi_init_stub(void){
    spi_bus_config_t bus = {
        .mosi_io_num = SPI_MOSI_PIN,
        .miso_io_num = SPI_MISO_PIN,
        .sclk_io_num = SPI_SCK_PIN,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI_HOST_USED, &bus, SPI_DMA_CH_AUTO));

    spi_device_interface_config_t dev = {
        .clock_speed_hz = 4*1000*1000,
        .mode = 0,
        .spics_io_num = SPI_CS_PIN,
        .queue_size = 2
    };
    ESP_ERROR_CHECK(spi_bus_add_device(SPI_HOST_USED, &dev, &g_spi));
}

// Fill g_data with something from SPI (here it just echoes a pattern)
static bool spi_sample_payload(uint8_t* out, uint8_t* out_len){
    // Example: read 8 bytes from a device register 0x00
    uint8_t txbuf[9] = {0x00}; // register 0
    uint8_t rxbuf[9] = {0};
    spi_transaction_t t = {
        .length = 9*8,
        .tx_buffer = txbuf,
        .rx_buffer = rxbuf
    };
    esp_err_t err = spi_device_transmit(g_spi, &t);
    if (err != ESP_OK) return false;

    // Put 8 bytes into payload (skip echoed reg byte)
    *out_len = 8;
    for(int i=0;i<8;i++) out[i] = rxbuf[i+1];
    return true;
}

// ====== I2C Slave init (legacy) =============================================
static void i2c_slave_init(void){
    i2c_config_t conf = {
        .mode = I2C_MODE_SLAVE,
        .sda_io_num = I2C_SDA_PIN,
        .scl_io_num = I2C_SCL_PIN,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .slave = {
            .slave_addr = I2C_SLAVE_ADDR,
            .addr_10bit_en = 0,
            .maximum_speed = 100000   // hint; bus is master-controlled
        }
    };
    ESP_ERROR_CHECK(i2c_param_config(I2C_PORT, &conf));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_PORT, conf.mode, I2C_RX_BUF_LEN, I2C_TX_BUF_LEN, 0));
}

// ====== TX preload helpers ===================================================
// Brain does: write(<reg>), then read(N). We detect the write, then preload TX.
static void preload_tx_whoami(void){
    uint8_t buf[2] = {0xC1, FW_VER};
    i2c_reset_tx_fifo(I2C_PORT);
    i2c_slave_write_buffer(I2C_PORT, buf, sizeof(buf), 0);
}
static void preload_tx_status(void){
    uint8_t b = g_status;
    i2c_reset_tx_fifo(I2C_PORT);
    i2c_slave_write_buffer(I2C_PORT, &b, 1, 0);
}
static void preload_tx_len(void){
    uint8_t b = g_len;
    i2c_reset_tx_fifo(I2C_PORT);
    i2c_slave_write_buffer(I2C_PORT, &b, 1, 0);
}
static void preload_tx_data_with_crc(void){
    uint8_t tmp[MAX_PAYLOAD+1];
    if (g_len > MAX_PAYLOAD) g_len = 0;
    memcpy(tmp, g_data, g_len);
    tmp[g_len] = crc8(g_data, g_len);
    i2c_reset_tx_fifo(I2C_PORT);
    i2c_slave_write_buffer(I2C_PORT, tmp, g_len+1, 0);
}

// ====== INT# state based on STATUS ==========================================
static void update_int_line(void){
#if USE_INT_LINE
    if (g_status & ST_DATA_RDY) int_assert();
    else                        int_deassert();
#endif
}

// ====== Command handling =====================================================
static void handle_cmd(uint8_t cmd){
    // Example: 0x01 => perform SPI sample and make data available
    if (cmd == 0x01){
        uint8_t len = 0;
        bool ok = spi_sample_payload(g_data, &len);
        if (ok){
            g_len = len;
            g_status |= ST_DATA_RDY;
        } else {
            g_status |= ST_ERR;
        }
        update_int_line();
    }
}

// ====== I2C service task =====================================================
// This task continuously looks for master WRITEs (register selects / CMD)
// and preloads the TX buffer for the subsequent master READ.
static void i2c_service_task(void*){
    uint8_t rx[I2C_RX_BUF_LEN];

    while(1){
        int n = i2c_slave_read_buffer(I2C_PORT, rx, sizeof(rx), pdMS_TO_TICKS(10));
        if (n <= 0){ vTaskDelay(pdMS_TO_TICKS(1)); continue; }

        // We expect: first byte = register index; optional payload follows.
        uint8_t reg = rx[0];

        switch(reg){
            case REG_WHOAMI:
                preload_tx_whoami();
                break;
            case REG_STATUS:
                // Reading STATUS does not auto-clear DATA_RDY; Brain decides protocol.
                preload_tx_status();
                break;
            case REG_LEN:
                preload_tx_len();
                break;
            case REG_DATA:
                preload_tx_data_with_crc();
                // Optionally clear DATA_RDY after Brain reads DATA; simplest:
                // clear here so the next STATUS read shows cleared.
                g_status &= ~ST_DATA_RDY;
                update_int_line();
                break;
            case REG_CMD:
                if (n >= 2){
                    handle_cmd(rx[1]);  // use first byte of payload as the command
                }
                // Preload STATUS so Brain can immediately read it
                preload_tx_status();
                break;
            default:
                // Unknown reg; return a zero byte to be safe.
                {
                    uint8_t zero = 0;
                    i2c_reset_tx_fifo(I2C_PORT);
                    i2c_slave_write_buffer(I2C_PORT, &zero, 1, 0);
                }
                break;
        }
    }
}

// ====== Optional: INT# GPIO init ============================================
static void int_gpio_init(void){
#if USE_INT_LINE
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << GPIO_INT_N,
        .mode = GPIO_MODE_OUTPUT_OD, // open-drain
        .pull_up_en = 1,             // enable if no external PU; else set to 0
        .pull_down_en = 0,
        .intr_type = GPIO_INTR_DISABLE
    };
    ESP_ERROR_CHECK(gpio_config(&io));
    int_deassert();
#endif
}

// ====== app_main =============================================================
void app_main(void){
    ESP_LOGI(TAG, "Child start (addr=0x%02X)", I2C_SLAVE_ADDR);

    spi_init_stub();
    i2c_slave_init();
    int_gpio_init();

    // Initial payload (optional)
    const char* hello = "HELLO";
    memcpy(g_data, hello, 5);
    g_len = 5;
    g_status = ST_DATA_RDY;
    update_int_line();

    xTaskCreatePinnedToCore(i2c_service_task, "i2c_service", 4096, NULL, 5, NULL, tskNO_AFFINITY);
}
