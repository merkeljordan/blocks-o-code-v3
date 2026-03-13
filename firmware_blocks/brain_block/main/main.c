#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "brain_block.h"
#include "device_registry.h"
#include "block_config_manager.h"
#include "brain_event_handler.h"
#include "tft_ui.h"
#include "brain_event_handler.h"
#include "audio_speaker.h"

extern void initArduino(void);

static const char *TAG = "BRAIN";
QueueHandle_t demo_cmd_queue = NULL;
#define ENABLE_DEBUG_REGISTRY_SCAN_TASK 0
#define ENABLE_BACKGROUND_BLOCK_SCAN_TASK 0
#define BACKGROUND_BLOCK_SCAN_INTERVAL_MS 5000
#define ENABLE_BRAIN_EXECUTOR_TASK 1
#define ENABLE_BRAIN_EXECUTOR_DEMO_VALIDATION_BYPASS 0
#define ENABLE_BRAIN_EXECUTOR_DEMO_AUTO_START 1
#define BRAIN_EXECUTOR_TICK_INTERVAL_MS 10

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} led_rgb_t;

static bool executor_state_is_active(brain_executor_state_t state);

// ============================================================================
// REGISTRY SCAN TASK - Scans every 1 second and prints results
// ============================================================================
static void registry_scan_task(void *arg) {
    while (1) {
        device_registry_scan();
        device_registry_print();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// Poll child blocks for DATA_READY and forward block-originated events.
static void block_event_poll_task(void *arg) {
    (void)arg;
    uint8_t status = 0;
    uint8_t payload[2] = {0};

    while (1) {
        const device_registry_t *registry = device_registry_get();
        for (int i = 0; i < DEVICE_REGISTRY_MAX_DEVICES; i++) {
            const device_entry_t *entry = &registry->devices[i];
            if (!entry->present || entry->type != BLOCK_TYPE_LED_FLASH) {
                continue;
            }

            if (i2c_read_reg(entry->address, REG_STATUS, &status, 1) != ESP_OK) {
                continue;
            }

            if ((status & STATUS_DATA_READY) == 0) {
                continue;
            }

            if (i2c_get_data(entry->address, payload, sizeof(payload)) == ESP_OK) {
                // payload[0] = event_id, payload[1] = event value (selection digit)
                bool queued = brain_event_handle_block_event(entry->address, payload[0], &payload[1], 1);
                if (!queued) {
                    ESP_LOGW(TAG, "Failed to enqueue block event from 0x%02X (id=0x%02X, val=%u)",
                             entry->address, payload[0], payload[1]);
                }
            } else {
                ESP_LOGW(TAG, "CMD_GET_DATA failed for 0x%02X while STATUS_DATA_READY set", entry->address);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(120));
    }
}

// ============================================================================
// BLOCK CONFIG SCAN TASK - Keeps scans running even when app/TCP is disconnected
// ============================================================================
static void block_scan_task(void *arg) {
    (void)arg;
    // start_network_client() initializes block_config_manager before this task is created,
    // but give startup a moment to settle before the first scan.
    vTaskDelay(pdMS_TO_TICKS(1000));

    while (1) {
        esp_err_t err = block_config_manager_scan();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "block_config_manager_scan failed: %s", esp_err_to_name(err));
        }
        vTaskDelay(pdMS_TO_TICKS(BACKGROUND_BLOCK_SCAN_INTERVAL_MS));
    }
}

static bool config_has_block_type(const block_config_state_t *cfg, block_type_t type)
{
    if (cfg == NULL) {
        return false;
    }

    for (int i = 0; i < cfg->block_count; i++) {
        if (cfg->blocks[i].present && cfg->blocks[i].block_type == type) {
            return true;
        }
    }
    return false;
}

static bool block_type_supports_led_mirroring(block_type_t type)
{
    switch (type) {
        case BLOCK_TYPE_IF:
        case BLOCK_TYPE_THEN:
        case BLOCK_TYPE_END_IF:
        case BLOCK_TYPE_LOOP:
        case BLOCK_TYPE_END_LOOP:
        case BLOCK_TYPE_DELAY:
        case BLOCK_TYPE_BUTTON:
        case BLOCK_TYPE_NOTE:
        case BLOCK_TYPE_LED_FLASH:
            return true;
        default:
            return false;
    }
}

static led_rgb_t block_type_led_color(block_type_t type)
{
    switch (type) {
        case BLOCK_TYPE_IF:
            return (led_rgb_t){40, 100, 255};
        case BLOCK_TYPE_THEN:
            return (led_rgb_t){0, 170, 110};
        case BLOCK_TYPE_END_IF:
            return (led_rgb_t){0, 210, 170};
        case BLOCK_TYPE_LOOP:
            return (led_rgb_t){0, 180, 60};
        case BLOCK_TYPE_END_LOOP:
            return (led_rgb_t){120, 220, 80};
        case BLOCK_TYPE_DELAY:
            return (led_rgb_t){255, 170, 0};
        case BLOCK_TYPE_BUTTON:
            return (led_rgb_t){255, 80, 130};
        case BLOCK_TYPE_NOTE:
            return (led_rgb_t){255, 220, 0};
        case BLOCK_TYPE_LED_FLASH:
            return (led_rgb_t){180, 70, 255};
        default:
            return (led_rgb_t){32, 32, 32};
    }
}

static led_rgb_t highlight_led_color(led_rgb_t base)
{
    led_rgb_t highlight = {
        .r = (uint8_t)((base.r + 255U) / 2U),
        .g = (uint8_t)((base.g + 255U) / 2U),
        .b = (uint8_t)((base.b + 255U) / 2U),
    };

    if (highlight.r < 96U) {
        highlight.r = 96U;
    }
    if (highlight.g < 96U) {
        highlight.g = 96U;
    }
    if (highlight.b < 96U) {
        highlight.b = 96U;
    }

    return highlight;
}

static int brain_led_highlight_index(const brain_executor_context_t *ctx)
{
    if (ctx == NULL || !executor_state_is_active(ctx->state) || ctx->program_len == 0) {
        return -1;
    }

    if (ctx->state == EXECUTOR_WAIT_DELAY || ctx->state == EXECUTOR_WAIT_INPUT) {
        return (ctx->pc > 0) ? (int)(ctx->pc - 1) : 0;
    }

    if (ctx->pc >= ctx->program_len) {
        return (ctx->program_len > 0) ? (int)(ctx->program_len - 1) : -1;
    }

    return (int)ctx->pc;
}

static void brain_led_refresh_child_blocks(const block_config_state_t *cfg,
                                           const brain_executor_context_t *ctx)
{
    static uint64_t s_last_render_scan_ts = 0;
    static brain_executor_state_t s_last_render_state = EXECUTOR_IDLE;
    static uint8_t s_last_render_pc = 0xFF;
    static uint8_t s_last_render_block_count = 0xFF;

    if (cfg == NULL) {
        return;
    }

    brain_executor_state_t state = (ctx != NULL) ? ctx->state : EXECUTOR_IDLE;
    uint8_t pc = (ctx != NULL) ? ctx->pc : 0xFF;
    if (s_last_render_scan_ts == cfg->last_scan_timestamp &&
        s_last_render_state == state &&
        s_last_render_pc == pc &&
        s_last_render_block_count == cfg->block_count) {
        return;
    }

    int highlight_index = brain_led_highlight_index(ctx);

    for (int i = 0; i < cfg->block_count; i++) {
        const block_config_entry_t *entry = &cfg->blocks[i];
        if (!entry->present || !block_type_supports_led_mirroring(entry->block_type)) {
            continue;
        }

        led_rgb_t color = block_type_led_color(entry->block_type);
        if (i == highlight_index) {
            color = highlight_led_color(color);
        }

        esp_err_t fill_ret = i2c_matrix_fill(entry->i2c_address, color.r, color.g, color.b);
        if (fill_ret != ESP_OK) {
            ESP_LOGD(TAG, "LED mirror fill failed addr=0x%02X type=%s ret=%s",
                     entry->i2c_address,
                     block_type_to_string(entry->block_type),
                     esp_err_to_name(fill_ret));
            continue;
        }

        esp_err_t show_ret = i2c_matrix_show(entry->i2c_address);
        if (show_ret != ESP_OK) {
            ESP_LOGD(TAG, "LED mirror show failed addr=0x%02X type=%s ret=%s",
                     entry->i2c_address,
                     block_type_to_string(entry->block_type),
                     esp_err_to_name(show_ret));
        }
    }

    s_last_render_scan_ts = cfg->last_scan_timestamp;
    s_last_render_state = state;
    s_last_render_pc = pc;
    s_last_render_block_count = cfg->block_count;
}

static bool executor_state_is_active(brain_executor_state_t state)
{
    return (state == EXECUTOR_RUNNING ||
            state == EXECUTOR_WAIT_DELAY ||
            state == EXECUTOR_WAIT_INPUT);
}

static void brain_executor_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "brain_executor_task started");

    brain_event_handler_init();

#if ENABLE_BRAIN_EXECUTOR_DEMO_VALIDATION_BYPASS
    brain_event_handler_set_config_validation(true, 0, (uint64_t)(esp_timer_get_time() / 1000));
    ESP_LOGW(TAG, "Demo mode: app validation bypass enabled for executor");
#endif

    uint64_t last_event_map_ts = 0;
    uint64_t last_scan_ts = 0;
    bool demo_auto_started_once = false;

    while (1) {
        const block_config_state_t *cfg = block_config_manager_get_state();
        const block_event_map_t *event_map = block_config_manager_get_event_map();
        const brain_executor_context_t *ctx = brain_executor_get_context();

        if (cfg != NULL && cfg->last_scan_timestamp != 0 && cfg->last_scan_timestamp != last_scan_ts) {
            last_scan_ts = cfg->last_scan_timestamp;
        }

        if (event_map != NULL &&
            event_map->generated_at_ms != 0 &&
            event_map->generated_at_ms != last_event_map_ts &&
            (ctx == NULL || !executor_state_is_active(ctx->state))) {
            brain_event_handler_refresh_config_event_map(event_map);
            last_event_map_ts = event_map->generated_at_ms;
        }

#if ENABLE_BRAIN_EXECUTOR_DEMO_AUTO_START
        ctx = brain_executor_get_context();
        if (!demo_auto_started_once &&
            cfg != NULL &&
            cfg->block_count > 0 &&
            config_has_block_type(cfg, BLOCK_TYPE_MUSIC_SEQ) &&
            ctx != NULL &&
            !executor_state_is_active(ctx->state) &&
            (ctx->state == EXECUTOR_IDLE || ctx->state == EXECUTOR_DONE || ctx->state == EXECUTOR_STOPPED)) {
            esp_err_t err = brain_executor_start();
            if (err == ESP_OK) {
                demo_auto_started_once = true;
                ESP_LOGI(TAG, "Auto-started brain executor for demo scan ts=%llu",
                         (unsigned long long)cfg->last_scan_timestamp);
            } else if (err != ESP_ERR_INVALID_STATE) {
                ESP_LOGW(TAG, "brain_executor_start failed: %s", esp_err_to_name(err));
            }
        }
#endif

        brain_executor_tick();
        ctx = brain_executor_get_context();
        cfg = block_config_manager_get_state();
        brain_led_refresh_child_blocks(cfg, ctx);
        vTaskDelay(pdMS_TO_TICKS(BRAIN_EXECUTOR_TICK_INTERVAL_MS));
    }
}

static void peripherals_boot_feedback(void)
{
    esp_err_t err = speaker_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "speaker_init failed: %s", esp_err_to_name(err));
        return;
    }

    /* Match music sequence block startup volume behavior. */
    speaker_set_volume(30);

    err = speaker_play_boot_sound();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "speaker_play_boot_sound failed: %s", esp_err_to_name(err));
    }
}

// ============================================================================
// MAIN - Only initialization and task creation
// ============================================================================
void app_main(void) {
    ESP_LOGI(TAG, "=== BRAIN BLOCK ===");
    esp_log_level_set("XPT2046", ESP_LOG_DEBUG);
    esp_log_level_set("xpt2046", ESP_LOG_DEBUG);

    initArduino();
    peripherals_boot_feedback();
    
    // Initialize I²C Master
    ESP_ERROR_CHECK(i2c_master_init());
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // Initialize device registry
    device_registry_init();
    
    // Initial scan
    i2c_safe_scan();

    tft_ui_start();   // starts LVGL + GUI task (returns after creating tasks)
    ESP_LOGI(TAG, "tft_ui_start() returned");

    // Forward child block-originated events (e.g., LED flash submit) to brain_event_handler.
    xTaskCreatePinnedToCore(block_event_poll_task, "block_evt", 4096, NULL, 5, NULL, 0);

    // Optional debug-only registry logger task.
#if ENABLE_DEBUG_REGISTRY_SCAN_TASK
    xTaskCreatePinnedToCore(registry_scan_task, "reg_scan", 4096, NULL, 4, NULL, 0);
#endif


    // Create network client task
    start_network_client();

#if ENABLE_BACKGROUND_BLOCK_SCAN_TASK
    xTaskCreatePinnedToCore(block_scan_task, "block_scan", 4096, NULL, 3, NULL, 0);
#endif

#if ENABLE_BRAIN_EXECUTOR_TASK
    xTaskCreatePinnedToCore(brain_executor_task, "brain_exec", 6144, NULL, 3, NULL, 0);
#endif
    
    ESP_LOGI(TAG, "Brain Block initialized!");
}
