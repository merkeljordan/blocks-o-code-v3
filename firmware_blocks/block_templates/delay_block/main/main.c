#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "i2c_protocol.h"
#include "audio_speaker.h"
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

#define BLOCK_NAME            "DELAY"
#define BLOCK_I2C_ADDRESS     0x0D  // I2C address for Delay block
#define BLOCK_TYPE            BLOCK_TYPE_DELAY

static const char *TAG = "DELAY_BLOCK";
#define STATUS_STRIP_GPIO      GPIO_NUM_13
#define STATUS_STRIP_LED_COUNT 16

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

static uint8_t g_status_flags = STATUS_READY;

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

static void set_status_flags(uint8_t status_flags)
{
    g_status_flags = status_flags;
    render_status_strip(g_status_flags);
}

// ============================================================================
// BLOCK -> BRAIN EVENT (STATUS_DATA_READY + CMD_GET_DATA)
// ============================================================================
static struct {
    bool has_event;
    uint8_t event_id;
    uint8_t payload[8];
    size_t payload_len;
} g_pending_event;

static void publish_delay_ms_event(uint32_t delay_ms)
{
    g_pending_event.has_event = true;
    g_pending_event.event_id = BRAIN_BLOCK_EVENT_DELAY_MS_SUBMIT;
    g_pending_event.payload[0] = (uint8_t)(delay_ms & 0xFFu);
    g_pending_event.payload[1] = (uint8_t)((delay_ms >> 8) & 0xFFu);
    g_pending_event.payload[2] = (uint8_t)((delay_ms >> 16) & 0xFFu);
    g_pending_event.payload[3] = (uint8_t)((delay_ms >> 24) & 0xFFu);
    g_pending_event.payload_len = 4;
    g_status_flags |= STATUS_DATA_READY;
}

static void on_local_delay_ms_changed(uint32_t delay_ms)
{
    g_config.delay_ms = delay_ms;
    g_config_valid = true;
    g_status_flags = STATUS_READY | (g_pending_event.has_event ? STATUS_DATA_READY : 0);
    publish_delay_ms_event(g_config.delay_ms);
    speaker_beep_ok();
}

// Entry point for TFT/UI code running on this block:
// call this when the user picks a new delay value in milliseconds.
void delay_block_set_delay_ms_from_ui(uint32_t delay_ms)
{
    on_local_delay_ms_changed(delay_ms);
}

static void config_reset(void)
{
    g_config.delay_ms = 500;
    g_config_valid = true;
    g_status_flags = STATUS_READY;
    publish_delay_ms_event(g_config.delay_ms);
}
static bool config_is_valid(void) { return g_config_valid; }
static size_t config_get_payload(uint8_t *out, size_t max_len) {
    if (out == NULL || max_len < 4) {
        return 0;
    }
    // Little-endian uint32 delay_ms
    out[0] = (uint8_t)(g_config.delay_ms & 0xFFu);
    out[1] = (uint8_t)((g_config.delay_ms >> 8) & 0xFFu);
    out[2] = (uint8_t)((g_config.delay_ms >> 16) & 0xFFu);
    out[3] = (uint8_t)((g_config.delay_ms >> 24) & 0xFFu);
    return 4;
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

    led_contract_rgb_t identity = led_contract_identity_color(BLOCK_TYPE_DELAY);
    matrix_fill(identity.r, identity.g, identity.b);
    matrix_show();
    vTaskDelay(pdMS_TO_TICKS(120));
    matrix_clear();
    matrix_show();
}

static void handle_brain_broadcast(const uint8_t *rx, size_t rx_len)
{
    if (rx == NULL || rx_len < 1U) {
        return;
    }

    led_contract_rgb_t identity = led_contract_identity_color(BLOCK_TYPE_DELAY);
    switch ((brain_broadcast_event_t)rx[0]) {
        case BRAIN_BROADCAST_IDLE:
            matrix_clear();
            matrix_show();
            break;
        case BRAIN_BROADCAST_RUNNING:
            matrix_fill(identity.r, identity.g, identity.b);
            matrix_show();
            (void)speaker_play_boot_sound();
            break;
        case BRAIN_BROADCAST_STEP:
            matrix_fill(identity.r, identity.g, identity.b);
            matrix_show();
            (void)speaker_play_tone(880U, 35U);
            break;
        case BRAIN_BROADCAST_DONE:
            matrix_fill(0U, 255U, 0U);
            matrix_show();
            speaker_beep_ok();
            break;
        case BRAIN_BROADCAST_ERROR:
            matrix_fill(255U, 0U, 0U);
            matrix_show();
            speaker_beep_error();
            break;
        case BRAIN_BROADCAST_STOP:
            matrix_fill(255U, 128U, 0U);
            matrix_show();
            (void)speaker_play_tone(220U, 120U);
            break;
        default:
            break;
    }
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

        case CMD_SET_DELAY:
            if (rx_len >= 4) {
                uint32_t v = 0;
                v |= (uint32_t)rx[0];
                v |= ((uint32_t)rx[1] << 8);
                v |= ((uint32_t)rx[2] << 16);
                v |= ((uint32_t)rx[3] << 24);
                g_config.delay_ms = v;
                g_config_valid = true;
                set_status_flags(STATUS_READY);
                publish_delay_ms_event(g_config.delay_ms);
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

        case CMD_BRAIN_BROADCAST:
            handle_brain_broadcast(rx, rx_len);
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

    // Initialize speaker early for boot/error beeps
    esp_err_t ret = speaker_init();
    if (ret == ESP_OK) {
        speaker_beep_ok();
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
