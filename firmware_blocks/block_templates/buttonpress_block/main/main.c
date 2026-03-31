#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "driver/gpio.h"
#include "i2c_protocol.h"
#include "audio_speaker.h"
#include "battery_monitor.h"
#include "led_matrix.h"
#include "status_strip.h"
#include "led_contract.h"

#if defined(CONTROL_FLOW_TFT_UI_ENABLED)
#include "tft_ui.h"
#else
static inline void tft_ui_start(void) {}
static inline void tft_ui_trigger_execute(void) {}
static inline void tft_ui_set_idle(void) {}
#endif

#if defined(CONTROL_FLOW_TFT_UI_ENABLED)
#include "tft_ui.h"
#else
static inline void tft_ui_start(void) {}
static inline void tft_ui_trigger_execute(void) {}
static inline void tft_ui_set_idle(void) {}
#endif

extern void initArduino(void);

// I2C slave transport implemented in i2c_comm.c
extern esp_err_t i2c_slave_init(void);
extern void i2c_task(void *arg);

static const char *TAG = "BUTTON_BLOCK";
#define STARTUP_GUARD_SETTLE_MS 120
static void startup_power_guard(void)
{
    static const gpio_num_t k_quiet_pins[] = { GPIO_NUM_13, GPIO_NUM_15, GPIO_NUM_18 };
    gpio_config_t io_cfg = {
        .pin_bit_mask = 0,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    io_cfg.pin_bit_mask = (1ULL << GPIO_NUM_5);
    (void)gpio_config(&io_cfg);
    (void)gpio_set_level(GPIO_NUM_5, 1);

    for (size_t i = 0; i < (sizeof(k_quiet_pins) / sizeof(k_quiet_pins[0])); ++i) {
        io_cfg.pin_bit_mask = (1ULL << k_quiet_pins[i]);
        (void)gpio_config(&io_cfg);
        (void)gpio_set_level(k_quiet_pins[i], 0);
    }

    vTaskDelay(pdMS_TO_TICKS(STARTUP_GUARD_SETTLE_MS));
}
#define STATUS_STRIP_GPIO      GPIO_NUM_13
#define STATUS_STRIP_LED_COUNT 30

static const status_strip_config_t kStatusStripConfig = {
    .gpio_num = STATUS_STRIP_GPIO,
    .led_count = STATUS_STRIP_LED_COUNT,
};

static uint8_t g_status_flags = STATUS_READY;

static void render_status_strip(uint8_t status_flags)
{
    led_contract_rgb_t identity = led_contract_identity_color(BLOCK_TYPE_BUTTON);
    led_contract_rgb_t color = led_contract_status_color(status_flags, identity);
    if (status_strip_ensure_ready(&kStatusStripConfig) != ESP_OK) {
        return;
    }
    status_strip_fill(color.r, color.g, color.b);
    status_strip_set_brightness(led_contract_status_brightness(status_flags));
    (void)status_strip_show();
}

static void show_status_matrix(uint8_t status_flags)
{
    led_contract_rgb_t identity = led_contract_identity_color(BLOCK_TYPE_BUTTON);
    led_contract_rgb_t color = led_contract_status_color(status_flags, identity);
    matrix_set_brightness(led_contract_status_brightness(status_flags));
    matrix_clear();
    matrix_show();
    matrix_fill(color.r, color.g, color.b);
    matrix_show();
}

static void set_status_flags(uint8_t status_flags)
{
    g_status_flags = status_flags;
    if ((status_flags & STATUS_BUSY) == 0U) {
        show_status_matrix(g_status_flags);
    }
    render_status_strip(g_status_flags);
}

static struct {
    bool has_event;
    uint8_t event_id;
    uint8_t payload[8];
    size_t payload_len;
} g_pending_event;

static void publish_button_press_event(uint8_t pressed)
{
    g_pending_event.has_event = true;
    g_pending_event.event_id = BRAIN_BLOCK_EVENT_BUTTON_PRESS;
    g_pending_event.payload[0] = pressed ? 1 : 0;
    g_pending_event.payload_len = 1;
    set_status_flags(STATUS_DATA_READY);
}

uint8_t button_block_get_status_flags(void)
{
    return g_status_flags;
}

uint8_t button_block_get_pending_data_len(void)
{
    return g_pending_event.has_event ? (uint8_t)(1 + g_pending_event.payload_len) : 0;
}

// TFT/UI integration points:
// - call button_block_execute_from_ui() when the user taps "Execute" (true).
// - call button_block_pass_from_ui() when the user taps "Pass" (false).
void button_block_execute_from_ui(void)
{
    publish_button_press_event(1);
    speaker_beep_ok();
}

void button_block_pass_from_ui(void)
{
    publish_button_press_event(0);
    speaker_beep_ok();
}

void command_handle(i2c_command_t cmd,
                    const uint8_t *rx,
                    size_t rx_len,
                    uint8_t *tx,
                    size_t *tx_len)
{
    (void)rx;
    (void)rx_len;
    if (tx_len) {
        *tx_len = 0;
    }

    switch (cmd) {
        case CMD_PING:
            set_status_flags(g_pending_event.has_event ? STATUS_DATA_READY : STATUS_READY);
            break;

        case CMD_GET_STATUS:
            if (tx && tx_len) {
                tx[0] = g_status_flags;
                *tx_len = 1;
            }
            break;

        case CMD_GET_DATA:
            if (tx && tx_len && g_pending_event.has_event) {
                tx[0] = g_pending_event.event_id;
                if (g_pending_event.payload_len > 0) {
                    memcpy(&tx[1], g_pending_event.payload, g_pending_event.payload_len);
                }
                *tx_len = 1 + g_pending_event.payload_len;

                // Clear DATA_READY after Brain consumes the event.
                g_pending_event.has_event = false;
                g_pending_event.payload_len = 0;
                set_status_flags(STATUS_READY);
            }
            break;

        case CMD_RESET:
            g_pending_event.has_event = false;
            g_pending_event.payload_len = 0;
            set_status_flags(STATUS_READY);
            tft_ui_set_idle();
            break;

        case CMD_EXECUTE:
            set_status_flags(STATUS_BUSY);
            tft_ui_trigger_execute();
            break;

        case CMD_MATRIX_FILL:
            if (rx_len >= 3U) {
                matrix_fill(rx[0], rx[1], rx[2]);
                (void)status_strip_handle_matrix_command(TAG, &kStatusStripConfig, cmd, rx, rx_len);
            }
            break;

        case CMD_MATRIX_CLEAR:
            matrix_clear();
            (void)status_strip_handle_matrix_command(TAG, &kStatusStripConfig, cmd, rx, rx_len);
            break;

        case CMD_MATRIX_BRIGHTNESS:
            if (rx_len >= 1U) {
                matrix_set_brightness(rx[0]);
            }
            (void)status_strip_handle_matrix_command(TAG, &kStatusStripConfig, cmd, rx, rx_len);
            break;

        case CMD_MATRIX_SHOW:
            matrix_show();
            (void)status_strip_handle_matrix_command(TAG, &kStatusStripConfig, cmd, rx, rx_len);
            break;
        default:
            break;
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "    BUTTON BLOCK BOOT");
    ESP_LOGI(TAG, "========================================");

    startup_power_guard();

    initArduino();

    esp_err_t ret = speaker_init();
    if (ret == ESP_OK) {
        speaker_play_boot_sound();
    }

    ret = led_matrix_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize LED matrix!");
        speaker_beep_error();
        return;
    }
    tft_ui_start();
    tft_ui_set_idle();

    battery_monitor_start();

    ret = i2c_slave_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize I²C slave!");
        speaker_beep_error();
        return;
    }

    // Repaint after all bring-up steps finish in case a later init touched the LED line.
    set_status_flags(g_status_flags);

    vTaskDelay(pdMS_TO_TICKS(500));
    set_status_flags(g_status_flags);
    ESP_LOGI(TAG, "Block ready and waiting for commands!\n");

    xTaskCreate(i2c_task, "i2c", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "All tasks created successfully!");
}
