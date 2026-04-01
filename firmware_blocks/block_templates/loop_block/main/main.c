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

#include "startup_guard.h"

extern void initArduino(void);

// I2C slave transport implemented in i2c_comm.c
extern esp_err_t i2c_slave_init(void);
extern void i2c_task(void *arg);

#define BLOCK_NAME            "LOOP"
#define BLOCK_TYPE            BLOCK_TYPE_LOOP
#define BLOCK_I2C_ADDRESS     block_compute_i2c_address(BLOCK_TYPE)

static const char *TAG = "LOOP_BLOCK";

#define STATUS_STRIP_GPIO      GPIO_NUM_13
#define STATUS_STRIP_LED_COUNT 30
#define BATTERY_UART_TASK_STACK 2048
#define BATTERY_UART_TASK_PRIO  2
#define BATTERY_UART_PERIOD_MS  2000

static const status_strip_config_t kStatusStripConfig = {
    .gpio_num = STATUS_STRIP_GPIO,
    .led_count = STATUS_STRIP_LED_COUNT,
};

// ============================================================================
// CONFIG (payload: loop_count)
// ============================================================================
typedef struct {
    uint8_t loop_count; // TODO: 1-99 typical
} block_config_t;

static block_config_t g_config;
static bool g_config_valid = false;

static uint8_t g_status_flags = STATUS_READY;
static struct {
    bool has_event;
    uint8_t event_id;
    uint8_t payload[BRAIN_BLOCK_EVENT_LOOP_COUNT_SUBMIT_PAYLOAD_LEN];
    size_t payload_len;
} g_pending_event;

static bool config_is_valid(void);
static void config_reset(void);
static void publish_loop_count_event(uint8_t loop_count);

static void render_status_strip(uint8_t status_flags)
{
    led_contract_rgb_t identity = led_contract_identity_color(BLOCK_TYPE_LOOP);
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
    led_contract_rgb_t identity = led_contract_identity_color(BLOCK_TYPE_LOOP);
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

static void battery_uart_log_task(void *arg)
{
    (void)arg;

    while (true) {
        const uint8_t percent = battery_monitor_get_percent();
        const bool charging = battery_monitor_is_charging();
        ESP_LOGI(TAG, "BATTERY: %u%% charging=%s", (unsigned)percent, charging ? "yes" : "no");
        vTaskDelay(pdMS_TO_TICKS(BATTERY_UART_PERIOD_MS));
    }
}

static bool config_is_valid(void)
{
    return g_config_valid;
}

static void config_reset(void)
{
    memset(&g_config, 0, sizeof(g_config));
    g_config_valid = false;
    memset(&g_pending_event, 0, sizeof(g_pending_event));
}

static void publish_loop_count_event(uint8_t loop_count)
{
    g_pending_event.has_event = true;
    g_pending_event.event_id = BRAIN_BLOCK_EVENT_LOOP_COUNT_SUBMIT;
    g_pending_event.payload[0] = loop_count;
    g_pending_event.payload_len = BRAIN_BLOCK_EVENT_LOOP_COUNT_SUBMIT_PAYLOAD_LEN;
    set_status_flags((uint8_t)(g_status_flags | STATUS_DATA_READY));
}

void loop_block_set_loop_count_from_ui(uint8_t loop_count)
{
    g_config.loop_count = (loop_count == 0U) ? 1U : loop_count;
    g_config_valid = true;
    set_status_flags(STATUS_READY);
    publish_loop_count_event(g_config.loop_count);
}

// =====================================================================// PERIPHERALS
// ============================================================================
static void peripherals_init(void) {
    initArduino();
    speaker_init();
}
static void peripherals_boot_feedback(void) { speaker_play_boot_sound(); }
static void peripherals_error_feedback(void) { speaker_beep_error(); }
static void peripherals_ok_feedback(void) { speaker_beep_ok(); }
static void animate_control_flow_pulse(led_contract_rgb_t color, uint8_t pulses, uint32_t on_ms, uint32_t off_ms)
{
    for (uint8_t pulse = 0; pulse < pulses; ++pulse) {
        matrix_fill(color.r, color.g, color.b);
        matrix_show();
        vTaskDelay(pdMS_TO_TICKS(on_ms));
        matrix_clear();
        matrix_show();
        if (pulse + 1U < pulses) {
            vTaskDelay(pdMS_TO_TICKS(off_ms));
        }
    }
}
static void peripherals_show_running(void)
{
    tft_ui_trigger_execute();

    led_contract_rgb_t identity = led_contract_identity_color(BLOCK_TYPE_LOOP);
    animate_control_flow_pulse(identity, 2U, 110U, 40U);
}

// ============================================================================
// STATUS ACCESSOR (used by i2c_comm.c register map)
// ============================================================================
uint8_t loop_block_get_status_flags(void)
{
    return g_status_flags;
}

uint8_t loop_block_get_pending_data_len(void)
{
    if (!g_pending_event.has_event) {
        return 0U;
    }

    return (uint8_t)(1U + g_pending_event.payload_len);
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
    if (tx_len) {
        *tx_len = 0;
    }

    (void)status_strip_handle_matrix_command(TAG, &kStatusStripConfig, cmd, rx, rx_len);
    if (status_strip_handle_runtime_broadcast(TAG, &kStatusStripConfig, BLOCK_TYPE_LOOP, cmd, rx, rx_len)) {
        return;
    }

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

        case CMD_SET_LOOP:
            if (rx_len >= 1) {
                g_config.loop_count = (rx[0] == 0) ? 1 : rx[0];
                g_config_valid = true;
                set_status_flags(STATUS_READY);
                publish_loop_count_event(g_config.loop_count);
            }
            break;

        case CMD_GET_DATA:
            if (tx && tx_len && g_pending_event.has_event) {
                // tx[0] = event_id, tx[1..] = payload
                tx[0] = g_pending_event.event_id;
                if (g_pending_event.payload_len > 0) {
                    memcpy(&tx[1], g_pending_event.payload, g_pending_event.payload_len);
                }
                *tx_len = 1 + g_pending_event.payload_len;

                // Clear DATA_READY after Brain consumes the event.
                g_pending_event.has_event = false;
                g_pending_event.payload_len = 0;
                g_status_flags &= (uint8_t)~STATUS_DATA_READY;
            }
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
    ESP_LOGI(TAG, "    LOOP BLOCK BOOT");
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
    set_status_flags(g_status_flags);

    battery_monitor_start();

    // Initialize I²C slave
    ret = i2c_slave_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize I²C slave!");
        speaker_beep_error();
        return;
    }

    set_status_flags(g_status_flags);

    vTaskDelay(pdMS_TO_TICKS(500));
    set_status_flags(g_status_flags);
    ESP_LOGI(TAG, "Block ready and waiting for commands!\n");

    // Create tasks
    xTaskCreate(i2c_task, "i2c", 4096, NULL, 5, NULL);
    xTaskCreate(battery_uart_log_task, "battery_uart", BATTERY_UART_TASK_STACK, NULL,
                BATTERY_UART_TASK_PRIO, NULL);

    ESP_LOGI(TAG, "All tasks created successfully!");
}
