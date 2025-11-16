#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_slave.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "ssd1306.h"
#include "i2c_protocol.h"

static const char *TAG = "CHILD_2";

// ------------------- PIN DEFINITIONS -------------------
#define OLED_MOSI_PIN   11
#define OLED_SCLK_PIN   12
#define OLED_CS_PIN     5
#define OLED_DC_PIN     16
#define OLED_RST_PIN    17

#undef I2C_SDA_PIN
#undef I2C_SCL_PIN
#define I2C_SDA_PIN     8
#define I2C_SCL_PIN     9

#define MY_ADDRESS      0x09
#define CMD_OLED_TEXT   0xF1      // <-- REQUIRED!

// ------------------- GLOBALS -------------------
static SSD1306_t oled;
static i2c_slave_dev_handle_t i2c_dev;

// --------------------------------------------------------
// OLED INIT
// --------------------------------------------------------
void oled_init_screen(void)
{
    oled._address = SPI_ADDRESS;

    spi_master_init(&oled, OLED_MOSI_PIN, OLED_SCLK_PIN,
                    OLED_CS_PIN, OLED_DC_PIN, OLED_RST_PIN);

    spi_init(&oled, 128, 64);
    ssd1306_clear_screen(&oled, false);
    ssd1306_contrast(&oled, 0xFF);
}

// --------------------------------------------------------
// OLED TEXT
// --------------------------------------------------------
void oled_show_text(const char *msg)
{
    ssd1306_clear_screen(&oled, false);

    char line[32];
    snprintf(line, sizeof(line), "%.30s", msg);

    ssd1306_display_text(&oled, 2, line, strlen(line), false);
}

// --------------------------------------------------------
// I2C SLAVE INIT
// --------------------------------------------------------
esp_err_t init_i2c_slave(void)
{
    i2c_slave_config_t cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = I2C_SDA_PIN,
        .scl_io_num = I2C_SCL_PIN,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .send_buf_depth = 64,
        .slave_addr = MY_ADDRESS,
        .addr_bit_len = I2C_ADDR_BIT_LEN_7,
    };

    gpio_set_pull_mode(I2C_SDA_PIN, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(I2C_SCL_PIN, GPIO_PULLUP_ONLY);

    return i2c_new_slave_device(&cfg, &i2c_dev);
}

// --------------------------------------------------------
// COMMAND HANDLER
// --------------------------------------------------------
void handle_i2c_command(uint8_t *buf, size_t len)
{
    if (len == 0) return;

    uint8_t cmd = buf[0];

    // ONLY process OLED commands
    if (cmd == CMD_OLED_TEXT)
    {
        if (len < 2) return;  // need length byte

        uint8_t text_len = buf[1];

        // avoid overflow
        if (text_len > 30) text_len = 30;
        if (len < text_len + 2) return; // incomplete packet

        char msg[32] = {0};
        memcpy(msg, &buf[2], text_len);

        ESP_LOGI(TAG, "Display text: %s", msg);
        oled_show_text(msg);
        return;
    }

    // IGNORE ALL OTHER COMMANDS
    ESP_LOGW(TAG, "Ignoring non-text cmd 0x%02X", cmd);
}


// --------------------------------------------------------
// I2C LISTENER TASK
// --------------------------------------------------------
void task_i2c(void *arg)
{
    uint8_t buf[64];

    while (1)
    {
        memset(buf, 0, sizeof(buf));

        esp_err_t ret = i2c_slave_receive(i2c_dev, buf, sizeof(buf));

        if (ret == ESP_OK)
        {
            // buf[1] = length byte
            size_t msg_len = buf[1] + 2;
            if (msg_len > sizeof(buf)) msg_len = sizeof(buf);

            handle_i2c_command(buf, msg_len);
        }

        vTaskDelay(1);
    }
}

// --------------------------------------------------------
// STARTUP SCREEN TASK  (IMPORTANT!)
// --------------------------------------------------------
void task_startup(void *arg)
{
    oled_show_text("Blocks of Code");
    vTaskDelay(pdMS_TO_TICKS(3000));

    oled_show_text("Child Block 2");
    vTaskDelay(pdMS_TO_TICKS(3000));

    vTaskDelete(NULL);
}

// --------------------------------------------------------
// MAIN
// --------------------------------------------------------
void app_main(void)
{
    ESP_LOGI(TAG, "Child Block 2 Booting…");

    // ONLY init hardware here — NO delays, NO OLED drawing!
    oled_init_screen();

    ESP_ERROR_CHECK(init_i2c_slave());
    ESP_LOGI(TAG, "I2C Slave Ready at 0x%02X", MY_ADDRESS);

    // Now safe to use tasks
    xTaskCreate(task_startup, "startup", 4096, NULL, 3, NULL);
    xTaskCreate(task_i2c, "i2c_task", 4096, NULL, 3, NULL);

    ESP_LOGI(TAG, "Child Block 2 Ready!");
}
