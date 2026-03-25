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
#include "status_strip.h"
#include "led_contract.h"

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
#define BRAIN_STATUS_STRIP_GPIO      GPIO_NUM_13
#define BRAIN_STATUS_STRIP_LED_COUNT 30

typedef led_contract_rgb_t led_rgb_t;

static const status_strip_config_t kBrainStatusStripConfig = {
    .gpio_num = BRAIN_STATUS_STRIP_GPIO,
    .led_count = BRAIN_STATUS_STRIP_LED_COUNT,
};

static bool executor_state_is_active(brain_executor_state_t state);
static uint8_t brain_led_idle_brightness(void);
static uint8_t brain_led_active_brightness(void);
static uint8_t brain_led_inactive_running_brightness(void);

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
    // Child blocks can return up to 17 bytes for NOTE sequence events:
    //   [event_id, count, note0..note14]
    uint8_t payload[32] = {0};
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
                entry->type != BLOCK_TYPE_LOOP &&
                entry->type != BLOCK_TYPE_DELAY &&
                entry->type != BLOCK_TYPE_BUTTON &&
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
            // For NOTE blocks we rely on REG_DATA_LEN being accurate.
            // If it's invalid (0/too small/too big), avoid falling back to a fixed
            // read length, otherwise we'd read random bytes (slave returns no payload)
            // and Brain would parse garbage as an event_id.
            if (data_len < 2 || data_len > sizeof(payload)) {
                if (entry->type == BLOCK_TYPE_NOTE) {
                    continue;
                }
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

        // Keep polling frequent so NOTE/sequence submissions are picked up
        // before the next app-triggered runtime START.
        vTaskDelay(pdMS_TO_TICKS(40));
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

static uint8_t brain_led_idle_brightness(void)
{
    return 96U;
}

static uint8_t brain_led_active_brightness(void)
{
    return 255U;
}

static uint8_t brain_led_inactive_running_brightness(void)
{
    return 48U;
}

static led_rgb_t scale_led_color(led_rgb_t color, uint8_t brightness)
{
    return (led_rgb_t) {
        .r = (uint8_t)(((uint16_t)color.r * brightness) / 255U),
        .g = (uint8_t)(((uint16_t)color.g * brightness) / 255U),
        .b = (uint8_t)(((uint16_t)color.b * brightness) / 255U),
    };
}

static int brain_led_highlight_index(const brain_executor_context_t *ctx)
{
    /* Map executor state to the logical program step that should be highlighted.
     *
     * Why this helper exists:
     * - The executor does not always sit on a "normal running" step.
     * - During wait states (delay, input wait), the child block that owns the
     *   visible output is usually the previous program step, not the next one.
     * - When the executor has advanced past the last step, we still want the
     *   final block to remain highlighted briefly instead of showing "nothing".
     *
     * Result:
     * - RUNNING       -> highlight current pc
     * - WAIT_*        -> highlight previous logical step
     * - past-the-end  -> highlight final step
     * - idle/inactive -> no highlight
     */
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

static void brain_led_refresh_local_strip(const block_config_state_t *cfg,
                                          const brain_executor_context_t *ctx)
{
    static uint64_t s_last_render_scan_ts = UINT64_MAX;
    static brain_executor_state_t s_last_render_state = EXECUTOR_IDLE;
    static uint8_t s_last_render_pc = 0xFF;
    static uint8_t s_last_render_block_count = 0xFF;
    static esp_err_t s_last_init_err = ESP_OK;

    brain_executor_state_t state = (ctx != NULL) ? ctx->state : EXECUTOR_IDLE;
    uint8_t pc = (ctx != NULL) ? ctx->pc : 0xFF;
    uint64_t scan_ts = (cfg != NULL) ? cfg->last_scan_timestamp : 0;
    uint8_t block_count = (cfg != NULL) ? cfg->block_count : 0;

    if (s_last_render_scan_ts == scan_ts &&
        s_last_render_state == state &&
        s_last_render_pc == pc &&
        s_last_render_block_count == block_count) {
        return;
    }

    esp_err_t err = status_strip_ensure_ready(&kBrainStatusStripConfig);
    if (err != ESP_OK) {
        if (err != s_last_init_err) {
            ESP_LOGW(TAG, "Brain status strip init failed: %s", esp_err_to_name(err));
            s_last_init_err = err;
        }
        return;
    }
    s_last_init_err = ESP_OK;

    status_strip_set_brightness(255U);

    /* Brain local strip renderer
     *
     * This is the Brain-side answer to "show me the built program locally".
     * The strip is not hardcoded to one fixed program layout. Instead, every
     * refresh derives its output from the latest scanned block configuration:
     *
     * 1. Read the latest scanned config (`cfg->blocks[]`, `cfg->block_count`).
     * 2. Divide the Brain strip into `block_count` visual segments.
     * 3. Color each segment using the shared block_type -> RGB palette.
     * 4. If the executor is active, brighten only the active logical step and
     *    dim the rest so the strip mirrors the current program position.
     *
     * Because the segment assignment is calculated from `block_count` every
     * time, this automatically adapts to different user-built programs:
     * - short programs -> larger segments per block
     * - long programs  -> smaller segments per block
     * - reordered blocks -> colors move with the new scanned order
     *
     * Fallback behavior:
     * - before any blocks are scanned, show the Brain's own color as a solid
     *   strip so the user still gets "I am alive" feedback at boot.
     */
    if (cfg == NULL || cfg->block_count == 0 || status_strip_get_led_count() == 0U) {
        led_rgb_t brain_color = scale_led_color(led_contract_identity_color(BLOCK_TYPE_BRAIN),
                                                brain_led_idle_brightness());
        status_strip_fill(brain_color.r, brain_color.g, brain_color.b);
    } else {
        uint16_t led_count = status_strip_get_led_count();
        int highlight_index = brain_led_highlight_index(ctx);
        bool is_active_run = executor_state_is_active(state);

        /* Each physical LED is mapped back to a program step by proportion:
         *
         *   block_index = led_idx * block_count / led_count
         *
         * That makes the renderer configuration-driven instead of assuming a
         * fixed number of blocks or a fixed LED allocation per block. */
        for (uint16_t led_idx = 0; led_idx < led_count; led_idx++) {
            uint8_t block_index = (uint8_t)(((uint32_t)led_idx * cfg->block_count) / led_count);
            if (block_index >= cfg->block_count) {
                block_index = (uint8_t)(cfg->block_count - 1);
            }

            const block_config_entry_t *entry = &cfg->blocks[block_index];
            led_rgb_t color = entry->present
                                  ? led_contract_identity_color(entry->block_type)
                                  : led_contract_identity_color(BLOCK_TYPE_BRAIN);

            /* Idle: every segment uses the same medium brightness so the full
             * program remains readable.
             *
             * Busy/running: only the active step gets full brightness. All
             * inactive steps stay visible but dim, which mirrors executor
             * progress without losing the overall program map. */
            uint8_t brightness = brain_led_idle_brightness();
            if (is_active_run) {
                brightness = (block_index == highlight_index)
                                 ? brain_led_active_brightness()
                                 : brain_led_inactive_running_brightness();
            }

            led_rgb_t scaled = scale_led_color(color, brightness);
            status_strip_set_pixel(led_idx, scaled.r, scaled.g, scaled.b);
        }
    }

    err = status_strip_show();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Brain status strip show failed: %s", esp_err_to_name(err));
    }

    s_last_render_scan_ts = scan_ts;
    s_last_render_state = state;
    s_last_render_pc = pc;
    s_last_render_block_count = block_count;
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
    bool is_active_run = executor_state_is_active(state);

    /* Child rendering stays config-driven too:
     * - scan order determines visual order
     * - block type determines idle color
     * - executor state determines highlight brightness
     *
     * So the Brain local strip and the child strips are both derived from the
     * same scanned program shape and the same executor state, just rendered to
     * different hardware paths. */
    for (int i = 0; i < cfg->block_count; i++) {
        const block_config_entry_t *entry = &cfg->blocks[i];
        if (!entry->present || !led_contract_supports_brain_mirroring(entry->block_type)) {
            continue;
        }

        led_rgb_t color = led_contract_identity_color(entry->block_type);
        uint8_t brightness = brain_led_idle_brightness();
        if (is_active_run) {
            brightness = (i == highlight_index)
                             ? brain_led_active_brightness()
                             : brain_led_inactive_running_brightness();
        }

        esp_err_t fill_ret = i2c_matrix_fill(entry->i2c_address, color.r, color.g, color.b);
        if (fill_ret != ESP_OK) {
            ESP_LOGD(TAG, "LED mirror fill failed addr=0x%02X type=%s ret=%s",
                     entry->i2c_address,
                     block_type_to_string(entry->block_type),
                     esp_err_to_name(fill_ret));
            continue;
        }

        esp_err_t brightness_ret = i2c_matrix_set_brightness(entry->i2c_address, brightness);
        if (brightness_ret != ESP_OK) {
            ESP_LOGD(TAG, "LED mirror brightness failed addr=0x%02X type=%s ret=%s",
                     entry->i2c_address,
                     block_type_to_string(entry->block_type),
                     esp_err_to_name(brightness_ret));
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

    uint64_t last_scan_ts = 0;
    bool demo_auto_started_once = false;

    while (1) {
        const block_config_state_t *cfg = block_config_manager_get_state();
        const brain_executor_context_t *ctx = brain_executor_get_context();

        if (cfg != NULL && cfg->last_scan_timestamp != 0 && cfg->last_scan_timestamp != last_scan_ts) {
            last_scan_ts = cfg->last_scan_timestamp;
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

        /* Keep both LED surfaces in lockstep:
         * - local Brain strip shows the whole program map on the Brain itself
         * - child strip refresh pushes the same logical state out over I2C */
        brain_led_refresh_local_strip(cfg, ctx);
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
    brain_led_refresh_local_strip(NULL, NULL);
    
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
