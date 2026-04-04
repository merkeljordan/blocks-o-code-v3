#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "brain_block.h"
#include "device_registry.h"
#include "block_config_manager.h"
#include "brain_event_handler.h"
#include "tft_ui.h"
#include "brain_event_handler.h"
#include "audio_speaker.h"
#include "battery_monitor.h"
#include "led_matrix.h"
#include "status_strip.h"
#include "led_contract.h"

extern void initArduino(void);

static const char *TAG = "BRAIN";
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
QueueHandle_t demo_cmd_queue = NULL;
#define ENABLE_DEBUG_REGISTRY_SCAN_TASK 0
#define ENABLE_BACKGROUND_BLOCK_SCAN_TASK 0
#define BACKGROUND_BLOCK_SCAN_INTERVAL_MS 5000
#define ENABLE_BRAIN_EXECUTOR_TASK 1
#define ENABLE_BRAIN_EXECUTOR_DEMO_VALIDATION_BYPASS 0
#define ENABLE_BRAIN_EXECUTOR_DEMO_AUTO_START 0
#define BRAIN_EXECUTOR_TICK_INTERVAL_MS 10
#define BLOCK_EVENT_POLL_INTERVAL_MS_IDLE    40
#define BLOCK_EVENT_POLL_INTERVAL_MS_ACTIVE 120
#define BRAIN_STATUS_STRIP_GPIO      GPIO_NUM_13
#define BRAIN_STATUS_STRIP_LED_COUNT 30

typedef led_contract_rgb_t led_rgb_t;

static const status_strip_config_t kBrainStatusStripConfig = {
    .gpio_num = BRAIN_STATUS_STRIP_GPIO,
    .led_count = BRAIN_STATUS_STRIP_LED_COUNT,
};

static bool executor_state_is_active(brain_executor_state_t state);
static uint8_t brain_led_idle_brightness(void);
static led_rgb_t scale_led_color(led_rgb_t color, uint8_t brightness);
static int brain_led_runtime_highlight_index(const brain_runtime_snapshot_t *runtime,
                                             const block_config_state_t *cfg);
static void brain_led_refresh_local_matrix(const brain_runtime_snapshot_t *runtime);

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
        for (int i = 0; i < registry->count; i++) {
            const device_entry_t *entry = &registry->devices[i];
            if (!entry->present) {
                continue;
            }
            if (entry->type != BLOCK_TYPE_LED_FLASH &&
                entry->type != BLOCK_TYPE_NOTE &&
                entry->type != BLOCK_TYPE_BUTTON &&
                entry->type != BLOCK_TYPE_DELAY &&
                entry->type != BLOCK_TYPE_LOOP) {
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

        // Idle: poll often so submits reach the Brain before START. Active run: back off I²C so
        // executor dispatch (NOTE waits, LED, etc.) and the config scan task see less contention.
        uint32_t poll_ms = brain_executor_prefers_i2c_yield() ? BLOCK_EVENT_POLL_INTERVAL_MS_ACTIVE
                                                              : BLOCK_EVENT_POLL_INTERVAL_MS_IDLE;
        vTaskDelay(pdMS_TO_TICKS(poll_ms));
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
    return 32U;
}

static void brain_led_show_boot_ready_strip(void)
{
    esp_err_t err = status_strip_ensure_ready(&kBrainStatusStripConfig);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Brain status strip init failed during boot: %s", esp_err_to_name(err));
        return;
    }

    led_rgb_t brain_color = scale_led_color(led_contract_identity_color(BLOCK_TYPE_BRAIN),
                                            brain_led_idle_brightness());

    status_strip_clear();
    (void)status_strip_show();
    status_strip_set_brightness(255U);
    status_strip_fill(brain_color.r, brain_color.g, brain_color.b);
    (void)status_strip_show();
}

static void brain_led_show_boot_ready_matrix(void)
{
    led_rgb_t brain_color = scale_led_color(led_contract_identity_color(BLOCK_TYPE_BRAIN),
                                            brain_led_idle_brightness());

    matrix_clear();
    matrix_show();
    matrix_fill(brain_color.r, brain_color.g, brain_color.b);
    matrix_show();
}

static led_rgb_t scale_led_color(led_rgb_t color, uint8_t brightness)
{
    return (led_rgb_t) {
        .r = (uint8_t)(((uint16_t)color.r * brightness) / 255U),
        .g = (uint8_t)(((uint16_t)color.g * brightness) / 255U),
        .b = (uint8_t)(((uint16_t)color.b * brightness) / 255U),
    };
}

static int brain_led_runtime_highlight_index(const brain_runtime_snapshot_t *runtime,
                                             const block_config_state_t *cfg)
{
    if (runtime == NULL || cfg == NULL || cfg->block_count == 0) {
        return -1;
    }

    if (runtime->pc == BRAIN_RUNTIME_PC_NONE) {
        return (runtime->state == BRAIN_RUNTIME_RUNNING ||
                runtime->state == BRAIN_RUNTIME_STEP)
                   ? 0
                   : -1;
    }

    if (runtime->pc >= cfg->block_count) {
        return (int)(cfg->block_count - 1);
    }

    return (int)runtime->pc;
}

static void brain_led_refresh_local_strip(const block_config_state_t *cfg,
                                          const brain_runtime_snapshot_t *runtime)
{
    static brain_runtime_broadcast_state_t s_last_render_state = BRAIN_RUNTIME_IDLE;
    static uint8_t s_last_render_pc = 0xFF;
    static uint8_t s_last_render_step_type = BLOCK_TYPE_UNKNOWN;
    static uint8_t s_last_render_block_count = 0xFF;
    static esp_err_t s_last_init_err = ESP_OK;

    static uint8_t s_debounce_block_count = 0;
    static uint8_t s_debounce_consecutive = 0;
    #define BLOCK_COUNT_DEBOUNCE_THRESHOLD 5

    brain_runtime_broadcast_state_t state = (runtime != NULL) ? runtime->state : BRAIN_RUNTIME_IDLE;
    uint8_t pc = (runtime != NULL) ? runtime->pc : BRAIN_RUNTIME_PC_NONE;
    uint8_t step_type = (runtime != NULL) ? (uint8_t)runtime->step_type : BLOCK_TYPE_UNKNOWN;
    uint8_t raw_block_count = (cfg != NULL) ? cfg->block_count : 0;

    if (raw_block_count == s_debounce_block_count) {
        s_debounce_consecutive++;
    } else {
        s_debounce_block_count = raw_block_count;
        s_debounce_consecutive = 1;
    }

    uint8_t block_count = s_last_render_block_count;
    if (block_count == 0xFF) {
        block_count = raw_block_count;
    }
    if (s_debounce_consecutive >= BLOCK_COUNT_DEBOUNCE_THRESHOLD) {
        block_count = raw_block_count;
    }

    bool block_count_changed = (block_count != s_last_render_block_count);
    bool state_changed = (s_last_render_state != state ||
                          s_last_render_pc != pc ||
                          s_last_render_step_type != step_type);
    if (!block_count_changed && !state_changed) {
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
    } else if (state == BRAIN_RUNTIME_DONE ||
               state == BRAIN_RUNTIME_ERROR ||
               state == BRAIN_RUNTIME_STOP) {
        led_rgb_t terminal_color = status_strip_runtime_color(state, BLOCK_TYPE_BRAIN, step_type);
        status_strip_fill(terminal_color.r, terminal_color.g, terminal_color.b);
        status_strip_set_brightness(status_strip_runtime_brightness(state));
    } else {
        uint16_t led_count = status_strip_get_led_count();
        int highlight_index = brain_led_runtime_highlight_index(runtime, cfg);
        uint8_t active_brightness = status_strip_runtime_brightness(state);
        uint8_t idle_brightness = brain_led_idle_brightness();

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
            uint8_t brightness = idle_brightness;
            if (highlight_index >= 0) {
                brightness = ((int)block_index == highlight_index)
                                 ? active_brightness
                                 : idle_brightness;
            }

            led_rgb_t scaled = scale_led_color(color, brightness);
            status_strip_set_pixel(led_idx, scaled.r, scaled.g, scaled.b);
        }
    }

    err = status_strip_show();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Brain status strip show failed: %s", esp_err_to_name(err));
    }

    s_last_render_state = state;
    s_last_render_pc = pc;
    s_last_render_step_type = step_type;
    s_last_render_block_count = block_count;
}

static void brain_led_refresh_local_matrix(const brain_runtime_snapshot_t *runtime)
{
    static brain_runtime_broadcast_state_t s_last_matrix_state = BRAIN_RUNTIME_IDLE;
    static uint8_t s_last_matrix_step_type = BLOCK_TYPE_UNKNOWN;
    static uint8_t s_last_matrix_pc = 0xFF;

    brain_runtime_broadcast_state_t state = (runtime != NULL) ? runtime->state : BRAIN_RUNTIME_IDLE;
    uint8_t step_type = (runtime != NULL) ? (uint8_t)runtime->step_type : BLOCK_TYPE_BRAIN;
    uint8_t pc = (runtime != NULL) ? runtime->pc : BRAIN_RUNTIME_PC_NONE;

    if (state == s_last_matrix_state &&
        step_type == s_last_matrix_step_type &&
        pc == s_last_matrix_pc) {
        return;
    }

    (void)status_strip_render_runtime_visuals(TAG,
                                              NULL,
                                              BLOCK_TYPE_BRAIN,
                                              state,
                                              pc,
                                              step_type);

    s_last_matrix_state = state;
    s_last_matrix_step_type = step_type;
    s_last_matrix_pc = pc;
}

static void brain_led_refresh_child_blocks(const block_config_state_t *cfg,
                                           const brain_executor_context_t *ctx)
{
    (void)cfg;
    (void)ctx;
    // Child-facing runtime parity is now driven by brain_event_handler.c via
    // CMD_RUNTIME_BROADCAST. Keep the local Brain strip renderer here, but
    // avoid maintaining a second child mirroring path from main.c.
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
        const brain_runtime_snapshot_t *runtime = brain_event_handler_get_runtime_snapshot();

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
        runtime = brain_event_handler_get_runtime_snapshot();

        brain_led_refresh_local_strip(cfg, runtime);
        brain_led_refresh_local_matrix(runtime);
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

    startup_power_guard();

    initArduino();
    peripherals_boot_feedback();
    esp_err_t matrix_err = led_matrix_init();
    if (matrix_err != ESP_OK) {
        ESP_LOGW(TAG, "led_matrix_init failed: %s", esp_err_to_name(matrix_err));
    } else {
        brain_led_show_boot_ready_matrix();
    }
    brain_led_show_boot_ready_strip();
    
    // Initialize I²C Master
    ESP_ERROR_CHECK(i2c_master_init());
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // Initialize device registry
    device_registry_init();
    
    // Initial scan
    i2c_safe_scan();

    battery_monitor_start();

    tft_ui_start();   // starts LVGL + GUI task (returns after creating tasks)
    ESP_LOGI(TAG, "tft_ui_start() returned");

    // Forward child block-originated events (e.g., LED flash submit) to brain_event_handler.
    xTaskCreatePinnedToCore(block_event_poll_task, "block_evt", 8192, NULL, 5, NULL, 0);

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
    xTaskCreatePinnedToCore(brain_executor_task, "brain_exec", 8192, NULL, 3, NULL, 0);
#endif
    
    ESP_LOGI(TAG, "Brain Block initialized!");
}
