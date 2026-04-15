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

#define BLOCK_NAME            "DELAY"
#define BLOCK_TYPE            BLOCK_TYPE_DELAY
#define BLOCK_I2C_ADDRESS     BLOCK_BOOT_I2C_ADDR_DELAY_BLOCK

static const char *TAG = "DELAY_BLOCK";

#define STATUS_STRIP_GPIO      GPIO_NUM_13
#define STATUS_STRIP_LED_COUNT 30

static const status_strip_config_t kStatusStripConfig = {
    .gpio_num = STATUS_STRIP_GPIO,
    .led_count = STATUS_STRIP_LED_COUNT,
};

// ============================================================================
// CONFIG (payload: delay_ms)
// ============================================================================
typedef struct {
    uint32_t delay_ms; // TODO: choose ms or seconds input
} block_config_t;

static block_config_t g_config;
static bool g_config_valid = false;

static volatile uint8_t g_status_flags = STATUS_READY;

static struct {
    volatile bool has_event;
    uint8_t event_id;
    uint8_t payload[BRAIN_BLOCK_EVENT_DELAY_MS_SUBMIT_PAYLOAD_LEN];
    size_t payload_len;
} g_pending_event;

static portMUX_TYPE s_pending_event_spinlock = portMUX_INITIALIZER_UNLOCKED;

static void set_status_flags(uint8_t status_flags);
static void config_reset(void);
static void publish_delay_ms_event(uint32_t delay_ms);

static bool config_is_valid(void)
{
    return g_config_valid;
}

static void peripherals_show_running(void);

// ============================================================================
// ASYNCHRONOUS EXECUTION (delay locally on CMD_EXECUTE)
// ============================================================================
typedef struct {
    uint32_t delay_ms;
} delay_exec_request_t;

static QueueHandle_t s_exec_queue = NULL;

static void delay_exec_task(void *arg)
{
    (void)arg;
    delay_exec_request_t req;
    for (;;) {
        if (xQueueReceive(s_exec_queue, &req, portMAX_DELAY) == pdTRUE) {
            peripherals_show_running();
            vTaskDelay(pdMS_TO_TICKS(req.delay_ms));
            set_status_flags((uint8_t)(STATUS_READY | STATUS_IDLE));
        }
    }
}

void delay_block_set_delay_ms_from_ui(uint32_t delay_ms)
{
    ESP_LOGI(TAG, "UI submit: delay_ms=%lu", (unsigned long)delay_ms);
    g_config.delay_ms = delay_ms;
    g_config_valid = true;
    set_status_flags(STATUS_READY);
    publish_delay_ms_event(delay_ms);
}

static void config_reset(void)
{
    memset(&g_config, 0, sizeof(g_config));
    g_config_valid = false;
    portENTER_CRITICAL(&s_pending_event_spinlock);
    memset(&g_pending_event, 0, sizeof(g_pending_event));
    portEXIT_CRITICAL(&s_pending_event_spinlock);
}

static void publish_delay_ms_event(uint32_t delay_ms)
{
    portENTER_CRITICAL(&s_pending_event_spinlock);
    g_pending_event.event_id = BRAIN_BLOCK_EVENT_DELAY_MS_SUBMIT;
    g_pending_event.payload[0] = (uint8_t)(delay_ms & 0xFFU);
    g_pending_event.payload[1] = (uint8_t)((delay_ms >> 8)  & 0xFFU);
    g_pending_event.payload[2] = (uint8_t)((delay_ms >> 16) & 0xFFU);
    g_pending_event.payload[3] = (uint8_t)((delay_ms >> 24) & 0xFFU);
    g_pending_event.payload_len = BRAIN_BLOCK_EVENT_DELAY_MS_SUBMIT_PAYLOAD_LEN;
    g_pending_event.has_event = true;
    portEXIT_CRITICAL(&s_pending_event_spinlock);
    ESP_LOGI(TAG, "publish event: delay_ms=%lu bytes=[%02X %02X %02X %02X] event_id=0x%02X",
             (unsigned long)delay_ms,
             g_pending_event.payload[0], g_pending_event.payload[1],
             g_pending_event.payload[2], g_pending_event.payload[3],
             g_pending_event.event_id);
    set_status_flags((uint8_t)(g_status_flags | STATUS_DATA_READY));
}

uint8_t delay_block_get_pending_data_len(void)
{
    portENTER_CRITICAL(&s_pending_event_spinlock);
    uint8_t len = g_pending_event.has_event ? (uint8_t)(1 + g_pending_event.payload_len) : 0;
    portEXIT_CRITICAL(&s_pending_event_spinlock);
    return len;
}

// uint32_t LE in I2C regs REG_DELAY_MS0..3 — Brain reads at program start.
uint32_t delay_block_get_delay_ms_for_brain(void)
{
    if (!g_config_valid || g_config.delay_ms == 0U) {
        return 500U;
    }
    return g_config.delay_ms;
}

static void render_status_strip(uint8_t status_flags)
{
    led_contract_rgb_t identity = led_contract_identity_color(BLOCK_TYPE_DELAY);
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
    led_contract_rgb_t identity = led_contract_identity_color(BLOCK_TYPE_DELAY);
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

    led_contract_rgb_t identity = led_contract_identity_color(BLOCK_TYPE_DELAY);
    animate_control_flow_pulse(identity, 3U, 55U, 55U);
}

// ============================================================================
// STATUS ACCESSOR (used by i2c_comm.c register map)
// ============================================================================
uint8_t delay_block_get_status_flags(void)
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
    if (tx_len) {
        *tx_len = 0;
    }

    (void)status_strip_handle_matrix_command(TAG, &kStatusStripConfig, cmd, rx, rx_len);
    if (status_strip_handle_runtime_broadcast(TAG, &kStatusStripConfig, BLOCK_TYPE_DELAY, cmd, rx, rx_len)) {
        return;
    }

    switch (cmd) {
        case CMD_PING:
            // PING is passive; do not modify status flags.
            peripherals_ok_feedback();
            break;

        case CMD_GET_STATUS:
            if (tx && tx_len) {
                tx[0] = g_status_flags;
                *tx_len = 1;
            }
            break;

        case CMD_SET_DELAY:
            if (rx_len >= 4) {
                uint32_t v = 0;
                v |= (uint32_t)rx[0];
                v |= ((uint32_t)rx[1] << 8);
                v |= ((uint32_t)rx[2] << 16);
                v |= ((uint32_t)rx[3] << 24);
                ESP_LOGI(TAG, "CMD_SET_DELAY rx=[%02X %02X %02X %02X] -> %lu ms",
                         rx[0], rx[1], rx[2], rx[3], (unsigned long)v);
                g_config.delay_ms = v;
                g_config_valid = true;
                set_status_flags(STATUS_READY);
                publish_delay_ms_event(g_config.delay_ms);
            }
            break;

        case CMD_GET_DATA:
            if (tx && tx_len) {
                portENTER_CRITICAL(&s_pending_event_spinlock);
                if (g_pending_event.has_event) {
                    tx[0] = g_pending_event.event_id;
                    if (g_pending_event.payload_len > 0) {
                        memcpy(&tx[1], g_pending_event.payload, g_pending_event.payload_len);
                    }
                    *tx_len = 1 + g_pending_event.payload_len;
                    g_pending_event.has_event = false;
                    g_pending_event.payload_len = 0;
                    portEXIT_CRITICAL(&s_pending_event_spinlock);

                    ESP_LOGI(TAG, "CMD_GET_DATA tx_len=%u event_id=0x%02X payload=[%02X %02X %02X %02X]",
                             (unsigned)*tx_len, tx[0],
                             (*tx_len > 1) ? tx[1] : 0,
                             (*tx_len > 2) ? tx[2] : 0,
                             (*tx_len > 3) ? tx[3] : 0,
                             (*tx_len > 4) ? tx[4] : 0);

                    set_status_flags(g_status_flags & (uint8_t)~STATUS_DATA_READY);
                } else {
                    portEXIT_CRITICAL(&s_pending_event_spinlock);
                    tx[0] = 0x00;
                    tx[1] = 0x00;
                    *tx_len = 2;
                }
            }
            break;

        case CMD_EXECUTE:
            if (!config_is_valid()) {
                set_status_flags(STATUS_ERROR);
                peripherals_error_feedback();
                break;
            }
            if ((g_status_flags & STATUS_BUSY) != 0U) {
                break;
            }
            if (s_exec_queue == NULL) {
                set_status_flags(STATUS_ERROR);
                peripherals_error_feedback();
                break;
            }
            {
                uint32_t delay_ms = delay_block_get_delay_ms_for_brain();
                set_status_flags(STATUS_BUSY);
                delay_exec_request_t req = {.delay_ms = delay_ms};
                if (xQueueSendToBack(s_exec_queue, &req, 0) != pdTRUE) {
                    set_status_flags(STATUS_ERROR);
                    peripherals_error_feedback();
                }
            }
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
    ESP_LOGI(TAG, "    DELAY BLOCK BOOT");
    ESP_LOGI(TAG, "========================================");

    startup_power_guard();

    initArduino();

    // Initialize speaker early for boot/error beeps
    esp_err_t ret = speaker_init();
    if (ret == ESP_OK) {
        speaker_play_boot_sound();
    }

    /* Start battery sampling early so TFT gets a percent quickly. */
    esp_err_t bat_err = battery_monitor_start();
    if (bat_err != ESP_OK) {
        ESP_LOGW(TAG, "battery_monitor_start failed: %s", esp_err_to_name(bat_err));
    }

    /* I²C before optional matrix: brain discovery must work even if WS2812 fails. */
    ret = i2c_slave_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize I²C slave!");
        speaker_beep_error();
        return;
    }
    xTaskCreate(i2c_task, "i2c", 4096, NULL, 5, NULL);

    s_exec_queue = xQueueCreate(2, sizeof(delay_exec_request_t));
    if (s_exec_queue != NULL) {
        xTaskCreate(delay_exec_task, "delay_exec", 3072, NULL, 4, NULL);
    } else {
        ESP_LOGE(TAG, "Failed to create exec queue");
    }

    ret = led_matrix_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "LED matrix init failed; I²C slave still active");
        speaker_beep_error();
    } else {
        show_status_matrix(g_status_flags);
    }

    tft_ui_start();
    tft_ui_set_idle();
    set_status_flags(g_status_flags);

    vTaskDelay(pdMS_TO_TICKS(500));
    ESP_LOGI(TAG, "Block ready and waiting for commands!\n");

    ESP_LOGI(TAG, "All tasks created successfully!");
}
