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
    uint8_t payload[16] = {0};
    uint8_t data_len = 0;

    while (1) {
        const device_registry_t *registry = device_registry_get();
        for (int i = 0; i < DEVICE_REGISTRY_MAX_DEVICES; i++) {
            const device_entry_t *entry = &registry->devices[i];
            if (!entry->present) {
                continue;
            }
            // Poll blocks that can emit selection-submit events.
            if (entry->type != BLOCK_TYPE_LED_FLASH &&
                entry->type != BLOCK_TYPE_NOTE) {
                continue;
            }

            if (i2c_read_reg(entry->address, REG_STATUS, &status, 1) != ESP_OK) {
                continue;
            }

            if ((status & STATUS_DATA_READY) == 0) {
                continue;
            }

            // Read how many bytes the child will return for CMD_GET_DATA.
            data_len = 0;
            // REG_DATA_LEN is optional; retry a couple times to avoid falling back
            // to the legacy 2-byte read for sequence events.
            for (int attempt = 0; attempt < 3; attempt++) {
                if (i2c_read_reg(entry->address, REG_DATA_LEN, &data_len, 1) == ESP_OK &&
                    data_len >= 2 && data_len <= sizeof(payload)) {
                    break;
                }
                data_len = 0;
                vTaskDelay(pdMS_TO_TICKS(2));
            }
            if (data_len < 2 || data_len > sizeof(payload)) {
                data_len = 2;
            }

            if (i2c_get_data(entry->address, payload, data_len) == ESP_OK) {
                // payload[0] = event_id, remaining bytes are event payload.
                size_t event_payload_len = (data_len >= 1) ? (size_t)(data_len - 1) : 0U;
                bool queued = brain_event_handle_block_event(entry->address,
                                                            payload[0],
                                                            (event_payload_len > 0) ? &payload[1] : NULL,
                                                            event_payload_len);
                if (!queued) {
                    ESP_LOGW(TAG, "Failed to enqueue block event from 0x%02X (id=0x%02X, len=%u)",
                             entry->address, payload[0], (unsigned)event_payload_len);
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
