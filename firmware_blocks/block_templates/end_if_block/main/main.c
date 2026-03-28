#include <stdio.h>
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

extern void initArduino(void);

// I2C slave transport implemented in i2c_comm.c
extern esp_err_t i2c_slave_init(void);
extern void i2c_task(void *arg);

#define BLOCK_NAME            "END_IF"
#define BLOCK_I2C_ADDRESS     0x11  // I2C address for End If block
#define BLOCK_TYPE            BLOCK_TYPE_END_IF

static const char *TAG = "END_IF_BLOCK";
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

// ============================================================================
// CONFIG (no payload for END_IF)
// ============================================================================
typedef struct {
    uint8_t unused;
} block_config_t;

static block_config_t g_config;
static bool g_config_valid = true;

static uint8_t g_status_flags = STATUS_READY;

static void render_status_strip(uint8_t status_flags)
{
    led_contract_rgb_t identity = led_contract_identity_color(BLOCK_TYPE_END_IF);
    led_contract_rgb_t color = led_contract_status_color(status_flags, identity);
    if (status_strip_ensure_ready(&kStatusStripConfig) != ESP_OK) {
        return;
    }
    status_strip_fill(color.r, color.g, color.b);
    status_strip_set_brightness(led_contract_status_brightness(status_flags));
    (void)status_strip_show();
}

static void set_status_flags(uint8_t status_flags)
{
    g_status_flags = status_flags;
    render_status_strip(g_status_flags);
}

static void config_reset(void)
{
    g_config.unused = 0;
    g_config_valid = true;
    g_status_flags = STATUS_READY;
}
static bool config_is_valid(void) { return g_config_valid; }
static size_t config_get_payload(uint8_t *out, size_t max_len) {
    (void)out;
    (void)max_len;
    return 0;
}

// ============================================================================
// PERIPHERALS
// ============================================================================
static void peripherals_init(void) {
    initArduino();
    speaker_init();
}
static void peripherals_boot_feedback(void) { speaker_play_boot_sound(); }
static void peripherals_error_feedback(void) { speaker_beep_error(); }
static void peripherals_ok_feedback(void) { speaker_beep_ok(); }
static void peripherals_show_running(void)
{
    tft_ui_trigger_execute();

    led_contract_rgb_t identity = led_contract_identity_color(BLOCK_TYPE_END_IF);
    matrix_fill(identity.r, identity.g, identity.b);
    matrix_show();
    vTaskDelay(pdMS_TO_TICKS(120));
    matrix_clear();
    matrix_show();
}

// ============================================================================
// STATUS ACCESSOR (used by i2c_comm.c register map)
// ============================================================================
uint8_t end_if_block_get_status_flags(void)
{
    return g_status_flags;
}

// ============================================================================
// COMMAND HANDLER
// ============================================================================
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

    (void)status_strip_handle_matrix_command(TAG, &kStatusStripConfig, cmd, rx, rx_len);

    switch (cmd) {
        case CMD_PING:
            set_status_flags(STATUS_READY);
            peripherals_ok_feedback();
            break;

        case CMD_GET_STATUS:
            if (tx && tx_len) {
                tx[0] = g_status_flags;
                *tx_len = 1;
            }
            break;

        case CMD_GET_DATA:
            // END_IF block has no payload; return zero-length response.
            break;

        case CMD_EXECUTE:
            if (!config_is_valid()) {
                set_status_flags(STATUS_ERROR);
                peripherals_error_feedback();
                break;
            }
            set_status_flags(STATUS_BUSY);
            peripherals_show_running();
            set_status_flags(STATUS_READY);
            break;

        case CMD_MATRIX_FILL:
            if (rx_len >= 3U) {
                matrix_fill(rx[0], rx[1], rx[2]);
            }
            break;

        case CMD_MATRIX_CLEAR:
            matrix_clear();
            break;

        case CMD_MATRIX_BRIGHTNESS:
            if (rx_len >= 1U) {
                matrix_set_brightness(rx[0]);
            }
            break;

        case CMD_MATRIX_SHOW:
            matrix_show();
            break;

        case CMD_RESET:
            config_reset();
            (void)status_strip_reset(&kStatusStripConfig);
            tft_ui_set_idle();
            matrix_clear();
            matrix_show();
            set_status_flags(STATUS_READY);
            break;

        default:
            break;
    }
}

// ============================================================================
// MAIN
// ============================================================================
void app_main(void) {
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "    END_IF BLOCK BOOT");
    ESP_LOGI(TAG, "========================================");

    startup_power_guard();

    initArduino();

    // Initialize speaker early for boot/error beeps
    esp_err_t ret = speaker_init();
    if (ret == ESP_OK) {
        speaker_play_boot_sound();
    }

    // Initialize LED Matrix
    ret = led_matrix_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize LED matrix!");
        speaker_beep_error();
        return;
    }

    tft_ui_start();
    tft_ui_set_idle();
    render_status_strip(g_status_flags);

    battery_monitor_start();

    // Initialize I²C slave
    ret = i2c_slave_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize I²C slave!");
        speaker_beep_error();
        return;
    }

    vTaskDelay(pdMS_TO_TICKS(500));
    ESP_LOGI(TAG, "Block ready and waiting for commands!\n");

    // Create tasks
    xTaskCreate(i2c_task, "i2c", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "All tasks created successfully!");
}
