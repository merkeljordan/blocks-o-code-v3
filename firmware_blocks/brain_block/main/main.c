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

static uint8_t brain_led_idle_brightness(void);
static led_rgb_t scale_led_color(led_rgb_t color, uint8_t brightness);
static void brain_led_refresh_local_matrix(const brain_runtime_snapshot_t *runtime);
#if ENABLE_BRAIN_EXECUTOR_DEMO_AUTO_START
static bool executor_state_is_active(brain_executor_state_t state);
#endif

// ============================================================================
// REGISTRY SCAN TASK - Scans every 1 second and prints results
// ============================================================================
#if ENABLE_DEBUG_REGISTRY_SCAN_TASK
static void registry_scan_task(void *arg) {
    while (1) {
        device_registry_scan();
        device_registry_print();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
#endif

// Poll child blocks for DATA_READY and forward block-originated events.
static void block_event_poll_task(void *arg) {
    (void)arg;
    uint8_t status = 0;
    // Child blocks can return up to 17 bytes for NOTE sequence events:
    //   [event_id, count, note0..note14]
    uint8_t payload[32] = {0};
    uint8_t data_len = 0;

    while (1) {
        const brain_executor_context_t *exec_ctx = brain_executor_get_context();
        bool waiting_for_button =
            (exec_ctx != NULL && exec_ctx->state == EXECUTOR_WAIT_INPUT);
        uint8_t expected_button_addr = brain_executor_get_expected_button_addr();
        // After STOP, the latch is cleared and we must not keep reading the BUTTON block:
        // the child may still alternate READY vs BUSY|DATA_READY (UI / pending event), which
        // spams BTN_POLL and can repeatedly consume events that no longer drive the executor.
        bool skip_button_poll_while_stopped =
            (exec_ctx != NULL && exec_ctx->state == EXECUTOR_STOPPED);

        // During active execution, avoid competing REG_STATUS/GET_DATA reads that can
        // perturb slave TX FIFO sequencing for the executor's wait loops.
        // Exception: when waiting for BUTTON input we must keep polling events.
        if (brain_executor_prefers_i2c_yield() && !waiting_for_button) {
            vTaskDelay(pdMS_TO_TICKS(BLOCK_EVENT_POLL_INTERVAL_MS_ACTIVE));
            continue;
        }

        const device_registry_t *registry = device_registry_get();
        uint8_t active_poll_addr = brain_executor_get_active_poll_addr();
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

            // Gate BUTTON polling to the executor's expected button address at the current PC.
            // This prevents consuming UI/status flapping from other times and ensures we only
            // poll when execution has actually reached a BUTTON step.
            if (entry->type == BLOCK_TYPE_BUTTON) {
                if (!waiting_for_button || expected_button_addr == 0 || entry->address != expected_button_addr) {
                    continue;
                }
            }

            if (skip_button_poll_while_stopped && entry->type == BLOCK_TYPE_BUTTON) {
                continue;
            }

            // Skip the address the executor is actively polling to prevent TX FIFO poisoning.
            // Button blocks are exempt: EXECUTOR_WAIT_INPUT uses a different code path and
            // active_poll_addr is 0 during that state.
            if (active_poll_addr != 0 && entry->address == active_poll_addr) {
                continue;
            }

            if (i2c_read_reg(entry->address, REG_STATUS, &status, 1) != ESP_OK) {
                continue;
            }

            if (entry->type == BLOCK_TYPE_BUTTON) {
                static uint8_t s_last_btn_status[CHILD_I2C_ADDR_MAX + 1];
                static bool s_initialized = false;
                if (!s_initialized) {
                    memset(s_last_btn_status, 0xFF, sizeof(s_last_btn_status));
                    s_initialized = true;
                }
                
                if (status != s_last_btn_status[entry->address]) {
                    ESP_LOGI(TAG, "BTN_POLL: addr=0x%02X status=0x%02X (was 0x%02X)%s", 
                             entry->address, status, s_last_btn_status[entry->address],
                             (status & STATUS_DATA_READY) ? " [DATA_READY]" : "");
                    s_last_btn_status[entry->address] = status;
                }

                if ((status & STATUS_DATA_READY) == 0) {
                    continue;
                }

                /* Choice is encoded directly in the status byte —
                 * immune to TX ring buffer corruption. */
                uint8_t pressed = (status & STATUS_BTN_EXECUTE) ? 1 : 0;

                /* Write-only CMD_RESET to clear DATA_READY + pending event
                 * on the slave (~1ms vs ~26ms for CMD_GET_DATA's read phase). */
                (void)i2c_reset(entry->address);

                ESP_LOGI("BTN_RX", "addr=0x%02X choice=%u (from status 0x%02X)",
                         entry->address, pressed, status);
                bool queued = brain_event_handle_block_event(
                    entry->address, BRAIN_BLOCK_EVENT_BUTTON_PRESS, &pressed, 1U);
                if (!queued) {
                    ESP_LOGW(TAG, "Failed to enqueue button event from 0x%02X", entry->address);
                }
                continue;
            }

            if ((status & STATUS_DATA_READY) == 0) {
                continue;
            }

            // Confirm DATA_READY with a second read to guard against stale TX FIFO bytes.
            {
                uint8_t status_confirm = 0;
                if (i2c_read_reg(entry->address, REG_STATUS, &status_confirm, 1) != ESP_OK ||
                    (status_confirm & STATUS_DATA_READY) == 0) {
                    continue;
                }
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
            // For NOTE/BUTTON/DELAY blocks we rely on REG_DATA_LEN being accurate.
            // If it's invalid (0/too small/too big), avoid falling back to a fixed
            // read length, otherwise we'd read random bytes (slave returns no payload)
            // and Brain would parse garbage as an event_id.
            if (data_len < 2 || data_len > sizeof(payload)) {
                ESP_LOGW(TAG,
                         "REG_DATA_LEN invalid after retries: addr=0x%02X type=%u raw=%u — %s",
                         entry->address, (unsigned)entry->type, (unsigned)data_len,
                         (entry->type == BLOCK_TYPE_NOTE || entry->type == BLOCK_TYPE_DELAY)
                             ? "skipping this poll cycle"
                             : "falling back to 2-byte read");
                if (entry->type == BLOCK_TYPE_NOTE || entry->type == BLOCK_TYPE_DELAY) {
                    continue;
                }
                data_len = 2;
            }

            if (i2c_get_data(entry->address, payload, data_len) == ESP_OK) {
                if (entry->type == BLOCK_TYPE_BUTTON) {
                    ESP_LOGI("BTN_RX", "addr=0x%02X len=%u event_id=0x%02X choice=0x%02X",
                             entry->address, (unsigned)data_len, payload[0],
                             (data_len >= 2) ? payload[1] : 0xFF);
                }
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
        uint32_t poll_ms = BLOCK_EVENT_POLL_INTERVAL_MS_IDLE;
        vTaskDelay(pdMS_TO_TICKS(poll_ms));
    }
}

// ============================================================================
// BLOCK CONFIG SCAN TASK - Keeps scans running even when app/TCP is disconnected
// ============================================================================
#if ENABLE_BACKGROUND_BLOCK_SCAN_TASK
static void block_scan_task(void *arg) {
    (void)arg;
    // start_network_client() initializes block_config_manager before this task is created,
    // but give startup a moment to settle before the first scan.
    vTaskDelay(pdMS_TO_TICKS(1000));

    while (1) {
        // Scans issue I2C traffic to many addresses; pause while executor is active
        // to keep step-level BUSY/IDLE reads deterministic.
        if (brain_executor_prefers_i2c_yield()) {
            vTaskDelay(pdMS_TO_TICKS(BACKGROUND_BLOCK_SCAN_INTERVAL_MS));
            continue;
        }

        esp_err_t err = block_config_manager_scan();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "block_config_manager_scan failed: %s", esp_err_to_name(err));
        }
        vTaskDelay(pdMS_TO_TICKS(BACKGROUND_BLOCK_SCAN_INTERVAL_MS));
    }
}
#endif

#if ENABLE_BRAIN_EXECUTOR_DEMO_AUTO_START
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
#endif

static uint8_t brain_led_idle_brightness(void)
{
    return 80U; // Single flat brightness for all states
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
        uint8_t brightness = brain_led_idle_brightness(); // Flat brightness -- no active/idle dimming

        for (uint16_t led_idx = 0; led_idx < led_count; led_idx++) {
            uint8_t block_index = (uint8_t)(((uint32_t)led_idx * cfg->block_count) / led_count);
            if (block_index >= cfg->block_count) {
                block_index = (uint8_t)(cfg->block_count - 1);
            }

            const block_config_entry_t *entry = &cfg->blocks[block_index];
            led_rgb_t color = entry->present
                                  ? led_contract_identity_color(entry->block_type)
                                  : led_contract_identity_color(BLOCK_TYPE_BRAIN);

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
    brain_runtime_broadcast_state_t state = (runtime != NULL) ? runtime->state : BRAIN_RUNTIME_IDLE;
    uint8_t step_type = (runtime != NULL) ? (uint8_t)runtime->step_type : (uint8_t)BLOCK_TYPE_UNKNOWN;

    // Use shared logic from status_strip to ensure Brain matrix matches identity or status
    led_contract_rgb_t color = status_strip_runtime_color(state, BLOCK_TYPE_BRAIN, step_type);
    uint8_t brightness = status_strip_runtime_brightness(state);

    matrix_set_brightness(brightness);
    matrix_fill(color.r, color.g, color.b);
    matrix_show();
}

static void brain_led_refresh_child_blocks(const block_config_state_t *cfg,
                                           const brain_executor_context_t *ctx)
{
    (void)cfg;
    if (ctx == NULL) {
        return;
    }

    // Terminal-state UX for children:
    // 1) broadcast terminal state once on entry so users can see completion/error.
    // 2) shortly after, restore children to runtime IDLE so each block returns to
    //    its standalone identity/default visuals.
    static brain_executor_state_t s_last_state = EXECUTOR_IDLE;
    static bool s_terminal_restore_sent = false;
    static uint32_t s_terminal_enter_ms = 0;
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);

    bool is_terminal = (ctx->state == EXECUTOR_DONE ||
                        ctx->state == EXECUTOR_STOPPED ||
                        ctx->state == EXECUTOR_ERROR);

    bool state_just_became_terminal = (is_terminal && !((s_last_state == EXECUTOR_DONE ||
                                                          s_last_state == EXECUTOR_STOPPED ||
                                                          s_last_state == EXECUTOR_ERROR)));
    s_last_state = ctx->state;

    if (is_terminal && state_just_became_terminal) {
        s_terminal_enter_ms = now;
        s_terminal_restore_sent = false;
        brain_runtime_broadcast_state_t broadcast_state = BRAIN_RUNTIME_DONE;
        if (ctx->state == EXECUTOR_STOPPED) broadcast_state = BRAIN_RUNTIME_STOP;
        if (ctx->state == EXECUTOR_ERROR) broadcast_state = BRAIN_RUNTIME_ERROR;

        ESP_LOGD(TAG, "Terminal state broadcast: %d", (int)broadcast_state);
        // Step through all present blocks to broadcast terminal state.
        // Child blocks show their full-matrix status color when pc is BRAIN_RUNTIME_PC_NONE.
        if (cfg != NULL) {
            for (int i = 0; i < cfg->block_count; i++) {
                if (cfg->blocks[i].present) {
                    (void)i2c_runtime_broadcast(cfg->blocks[i].i2c_address, broadcast_state, BRAIN_RUNTIME_PC_NONE, BLOCK_TYPE_UNKNOWN);
                }
            }
        }
    }

    if (is_terminal && !s_terminal_restore_sent && (now - s_terminal_enter_ms >= 500U)) {
        s_terminal_restore_sent = true;
        ESP_LOGD(TAG, "Terminal restore broadcast: IDLE");
        if (cfg != NULL) {
            for (int i = 0; i < cfg->block_count; i++) {
                if (cfg->blocks[i].present) {
                    (void)i2c_runtime_broadcast(cfg->blocks[i].i2c_address,
                                                BRAIN_RUNTIME_IDLE,
                                                BRAIN_RUNTIME_PC_NONE,
                                                BLOCK_TYPE_UNKNOWN);
                }
            }
        }
    }

    if (!is_terminal) {
        s_terminal_restore_sent = false;
    }
}

#if ENABLE_BRAIN_EXECUTOR_DEMO_AUTO_START
static bool executor_state_is_active(brain_executor_state_t state)
{
    return (state == EXECUTOR_RUNNING ||
            state == EXECUTOR_WAIT_DELAY ||
            state == EXECUTOR_WAIT_INPUT);
}
#endif

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
#if ENABLE_BRAIN_EXECUTOR_DEMO_AUTO_START
    bool demo_auto_started_once = false;
#endif

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
            (ctx->state == EXECUTOR_IDLE || ctx->state == EXECUTOR_DONE || ctx->state == EXECUTOR_STOPPED || ctx->state == EXECUTOR_ERROR)) {
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

        // --- Periodic Heartbeat Log (Clean Summary) ---
        static uint32_t s_last_heartbeat_ms = 0;
        uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
        if (now - s_last_heartbeat_ms >= 2000) {
            s_last_heartbeat_ms = now;
            int block_count = (cfg != NULL) ? cfg->block_count : 0;
            const char *state_str = "UNKNOWN";
            if (ctx != NULL) {
                switch(ctx->state) {
                    case EXECUTOR_IDLE:    state_str = "IDLE"; break;
                    case EXECUTOR_RUNNING: state_str = "RUNNING"; break;
                    case EXECUTOR_DONE:    state_str = "DONE"; break;
                    case EXECUTOR_ERROR:   state_str = "ERROR"; break;
                    case EXECUTOR_STOPPED: state_str = "STOPPED"; break;
                    default:               state_str = "WAITING"; break;
                }
            }
            ESP_LOGI("BRAIN_STATE", "HEARTBEAT: [Chain: %d blocks] [Executor: %s @ PC %d]", 
                     block_count, state_str, (ctx != NULL ? ctx->pc : 0));
        }

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
    // Keep scan task active but suppress repetitive scan INFO spam during runtime.
    esp_log_level_set("BLOCK_CONFIG", ESP_LOG_WARN);

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
