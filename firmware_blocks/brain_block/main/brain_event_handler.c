#include "brain_event_handler.h"

#include <string.h>
#include <strings.h>
#include "esp_timer.h"
#include <stdlib.h>

#if defined(_WIN32) && !defined(strcasecmp)
#define strcasecmp _stricmp
#endif

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "brain_block.h"
#include "audio_speaker.h"
#include "device_registry.h"
#include "status_strip.h"

// Queued block events (NOTE sequences need up to 1 + 15 note bytes after event_id is stripped).
#define BRAIN_EVENT_QUEUE_PAYLOAD_MAX 32U

static const char *TAG = "brain_evt";
static brain_validation_state_t s_validation_state;
static brain_executor_context_t s_executor_ctx;
static brain_executor_params_t s_executor_params;
static brain_runtime_snapshot_t s_runtime_snapshot;

// Per-program-position control-flow parameters (indexed by executor pc).
static uint16_t s_loop_count_by_pc[BRAIN_EXECUTOR_MAX_PROGRAM_BLOCKS];
static bool s_loop_count_valid_by_pc[BRAIN_EXECUTOR_MAX_PROGRAM_BLOCKS];

// Stash tables keyed by 7-bit child address. Use the same span as device_registry_scan()
// (0x08–0x77), not CHILD_I2C_ADDR_MAX (0x16), so submits work after address reassignment.
#define BRAIN_STASH_ADDR_SLOT_COUNT ((size_t)(DEVICE_REGISTRY_ADDR_MAX - DEVICE_REGISTRY_ADDR_MIN + 1u))
static uint16_t s_loop_count_stash_by_addr[BRAIN_STASH_ADDR_SLOT_COUNT];
static bool s_loop_count_stash_valid_by_addr[BRAIN_STASH_ADDR_SLOT_COUNT];
static uint32_t s_delay_ms_by_pc[BRAIN_EXECUTOR_MAX_PROGRAM_BLOCKS];
static bool s_delay_ms_valid_by_pc[BRAIN_EXECUTOR_MAX_PROGRAM_BLOCKS];
static uint32_t s_delay_ms_stash_by_addr[BRAIN_STASH_ADDR_SLOT_COUNT];
static bool s_delay_ms_stash_valid_by_addr[BRAIN_STASH_ADDR_SLOT_COUNT];

// Snapshot of I2C targets at executor START (program index -> child); scan may advance during a run.
static uint8_t s_program_addr[BRAIN_EXECUTOR_MAX_PROGRAM_BLOCKS];
static bool s_program_present[BRAIN_EXECUTOR_MAX_PROGRAM_BLOCKS];

static uint8_t s_led_color_by_pc[BRAIN_EXECUTOR_MAX_PROGRAM_BLOCKS];
static bool s_led_color_valid_by_pc[BRAIN_EXECUTOR_MAX_PROGRAM_BLOCKS];
static uint8_t s_led_color_stash_by_addr[BRAIN_STASH_ADDR_SLOT_COUNT];
static bool s_led_color_stash_valid_by_addr[BRAIN_STASH_ADDR_SLOT_COUNT];

typedef struct {
    uint8_t note_id;
    uint8_t note_seq_len;
    uint8_t note_seq[15];
} brain_note_config_t;

static brain_note_config_t s_note_by_pc[BRAIN_EXECUTOR_MAX_PROGRAM_BLOCKS];
static bool s_note_valid_by_pc[BRAIN_EXECUTOR_MAX_PROGRAM_BLOCKS];
static brain_note_config_t s_note_stash_by_addr[BRAIN_STASH_ADDR_SLOT_COUNT];
static bool s_note_stash_valid_by_addr[BRAIN_STASH_ADDR_SLOT_COUNT];

// IF condition: last BUTTON_PRESS source (consumed when THEN is evaluated).
static uint8_t s_last_button_press_addr;
static bool s_last_button_press_valid;

// Pending IF frames: condition is evaluated at THEN after BUTTON wait (IF -> BUTTON -> THEN -> ...).
#define BRAIN_EXECUTOR_MAX_IF_DEPTH 4
typedef struct {
    uint8_t end_if_pc;
    uint8_t then_pc;
    uint8_t bound_button_pc; // 0xFF = no BUTTON slot between IF and THEN
} brain_if_frame_t;
static brain_if_frame_t s_if_stack[BRAIN_EXECUTOR_MAX_IF_DEPTH];
static uint8_t s_if_depth;

// Throttling for validation-gate start warnings to avoid log flooding.
#define BRAIN_EXEC_VALIDATION_LOG_INTERVAL_MS 2000U
static uint64_t s_last_validation_block_log_ms;
static uint32_t s_validation_block_attempts_since_last_log;

// START/STOP and executor_start share s_dispatch_mutex with executor tick + block events; allow headroom
// when tick is doing I²C (broadcast, scans racing) so the UI does not intermittently lose START.
#define BRAIN_DISPATCH_MUTEX_TIMEOUT_MS 2000U

#define MUSIC_EXEC_BUSY_TIMEOUT_MS   200U
#define BRAIN_EXECUTOR_LOOP_COUNT_MAX 64U

static bool loop_count_addr_to_slot(uint8_t addr, size_t *out_slot);
static bool program_slot_effectively_present(uint8_t pc);
static void dispatch_note_like_main_branch(uint8_t pc);

uint64_t now_ms(void) {
    return (uint64_t)(esp_timer_get_time() / 1000);
}

static esp_err_t wait_for_status_busy(uint8_t addr, uint32_t timeout_ms, uint32_t *out_elapsed_ms, uint8_t *out_status)
{
    uint32_t start = (uint32_t)now_ms();
    uint8_t status = 0;
    uint32_t error_count = 0;

    while (now_ms() < start + timeout_ms) {
        if (i2c_read_reg(addr, REG_STATUS, &status, 1) == ESP_OK) {
            error_count = 0;
            if ((status & ~0x0F) != 0U) {
                ESP_LOGE(TAG, "wait_for_status_busy(0x%02X): invalid status byte 0x%02X ignored", addr, status);
            }
            if ((status & STATUS_BUSY) != 0U) {
                if (out_elapsed_ms != NULL) {
                    *out_elapsed_ms = (uint32_t)(now_ms() - start);
                }
                if (out_status != NULL) {
                    *out_status = status;
                }
                return ESP_OK;
            }
        } else {
            error_count++;
            if (error_count > 10) {
                ESP_LOGE(TAG, "wait_for_status_busy(0x%02X): persistent I2C errors", addr);
                return ESP_FAIL;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    if (out_elapsed_ms != NULL) {
        *out_elapsed_ms = (uint32_t)(now_ms() - start);
    }
    if (out_status != NULL) {
        *out_status = status;
    }
    return ESP_ERR_TIMEOUT;
}

static esp_err_t wait_for_status_idle(uint8_t addr,
                                      uint32_t timeout_ms,
                                      uint32_t *out_elapsed_ms,
                                      uint8_t *out_status)
{
    uint32_t start = (uint32_t)now_ms();
    uint8_t status = 0;
    uint32_t error_count = 0;

    while (now_ms() < start + timeout_ms) {
        // If we can't read the status (e.g. slave is blocking bus), we treat it as BUSY.
        // We only return ESP_OK if we explicitly read a status byte that has STATUS_BUSY cleared.
        if (i2c_read_reg(addr, REG_STATUS, &status, 1) == ESP_OK) {
            error_count = 0;
            if ((status & ~0x0F) != 0U) {
                ESP_LOGE(TAG, "wait_for_status_idle(0x%02X): invalid status byte 0x%02X ignored", addr, status);
            }
            if ((status & STATUS_BUSY) == 0U) {
                if (out_elapsed_ms != NULL) {
                    *out_elapsed_ms = (uint32_t)(now_ms() - start);
                }
                if (out_status != NULL) {
                    *out_status = status;
                }
                return ESP_OK;
            }
        } else {
            error_count++;
            if (error_count > 50) { // More lenient for idle polling
                ESP_LOGE(TAG, "wait_for_status_idle(0x%02X): persistent I2C errors (status=0x%02X)", addr, status);
                return ESP_FAIL;
            }
        }
        if (s_executor_ctx.stop_requested) {
            ESP_LOGW(TAG, "wait_for_status_idle(0x%02X): stop requested, aborting wait", addr);
            return ESP_ERR_TIMEOUT;
        }
        // Yield more during long playback so other tasks continue.
        vTaskDelay(pdMS_TO_TICKS(10));
        
        static uint64_t last_log = 0;
        if ((now_ms() - start) > 2000 && (now_ms() - last_log) > 1000) {
            ESP_LOGI(TAG, "wait_for_status_idle(0x%02X): still waiting, elapsed=%llu ms, last_status=0x%02X",
                     addr, (now_ms() - start), status);
            last_log = now_ms();
        }
    }
    ESP_LOGW(TAG, "wait_for_status_idle(0x%02X): TIMEOUT after %lu ms", addr, (unsigned long)timeout_ms);

    if (out_elapsed_ms != NULL) {
        *out_elapsed_ms = (uint32_t)(now_ms() - start);
    }
    if (out_status != NULL) {
        *out_status = status;
    }
    return ESP_ERR_TIMEOUT;
}

// Matches `main` branch: NOTE steps use CMD_EXECUTE on each NOTE child (plays its SUBMIT'd sequence
// in firmware). No blocking wait on the Brain (the child I²C slave cannot serve REG_STATUS while
// synchronous play runs inside CMD_EXECUTE). If no NOTE child accepts EXECUTE, fall back to
// Brain-side note params + CMD_PLAY_NOTE broadcast + Brain speaker.
static void dispatch_note_like_main_branch(uint8_t pc)
{
    block_config_state_t config_snapshot;
    if (block_config_manager_get_state_snapshot(&config_snapshot) != ESP_OK ||
        config_snapshot.block_count == 0) {
        ESP_LOGW(TAG, "NOTE pc=%u: no config snapshot", (unsigned)pc);
        return;
    }

    uint8_t executed_note_blocks = 0;
    for (int i = 0; i < config_snapshot.block_count; i++) {
        const block_config_entry_t *entry = &config_snapshot.blocks[i];
        if (!entry->present || entry->block_type != BLOCK_TYPE_NOTE) {
            continue;
        }
        esp_err_t exec_ret = i2c_execute(entry->i2c_address);
        if (exec_ret == ESP_OK) {
            executed_note_blocks++;
        } else {
            ESP_LOGW(TAG,
                     "NOTE CMD_EXECUTE failed addr=0x%02X (ret=%d)",
                     entry->i2c_address,
                     (int)exec_ret);
        }
    }

    if (executed_note_blocks > 0) {
        ESP_LOGI(TAG,
                 "NOTE pc=%u: CMD_EXECUTE on %u NOTE block(s) (main-branch semantics)",
                 (unsigned)pc,
                 (unsigned)executed_note_blocks);
        return;
    }

    static const uint32_t k_note_freq_hz[7] = {
        220U, 247U, 262U, 294U, 330U, 349U, 392U,
    };
    uint8_t played = 0;
    uint8_t seq_len = s_executor_params.note_seq_len;
    if (seq_len > 15U) {
        seq_len = 15U;
    }

    if (seq_len == 0U) {
        uint8_t note_id = s_executor_params.note_id;
        if (note_id >= 7U) {
            note_id = 0U;
        }
        for (int i = 0; i < config_snapshot.block_count; i++) {
            const block_config_entry_t *entry = &config_snapshot.blocks[i];
            if (!entry->present) {
                continue;
            }
            if (entry->block_type != BLOCK_TYPE_NOTE && entry->block_type != BLOCK_TYPE_MUSIC_SEQ) {
                continue;
            }
            esp_err_t play_ret = i2c_play_note(entry->i2c_address, note_id);
            if (play_ret != ESP_OK) {
                ESP_LOGW(TAG,
                         "NOTE fallback PLAY_NOTE failed addr=0x%02X (ret=%d)",
                         entry->i2c_address,
                         (int)play_ret);
            } else {
                played++;
            }
        }
        (void)speaker_play_tone(k_note_freq_hz[note_id], 400U);
        ESP_LOGI(TAG,
                 "NOTE fallback pc=%u played=%u note_id=%u",
                 (unsigned)pc,
                 (unsigned)played,
                 (unsigned)note_id);
        return;
    }

    for (uint8_t s = 0; s < seq_len; s++) {
        if (s_executor_ctx.stop_requested) {
            break;
        }
        uint8_t note_id = s_executor_params.note_seq[s];
        if (note_id >= 7U) {
            note_id = 0U;
        }
        for (int i = 0; i < config_snapshot.block_count; i++) {
            const block_config_entry_t *entry = &config_snapshot.blocks[i];
            if (!entry->present) {
                continue;
            }
            if (entry->block_type != BLOCK_TYPE_NOTE && entry->block_type != BLOCK_TYPE_MUSIC_SEQ) {
                continue;
            }
            esp_err_t play_ret = i2c_play_note(entry->i2c_address, note_id);
            if (play_ret != ESP_OK) {
                ESP_LOGW(TAG,
                         "NOTE fallback PLAY_NOTE failed addr=0x%02X (ret=%d)",
                         entry->i2c_address,
                         (int)play_ret);
            } else {
                played++;
            }
        }
        (void)speaker_play_tone(k_note_freq_hz[note_id], 400U);
    }
    ESP_LOGI(TAG,
             "NOTE fallback pc=%u played=%u seq_len=%u",
             (unsigned)pc,
             (unsigned)played,
             (unsigned)seq_len);
}

static void set_default_validation_state(void) {
    memset(&s_validation_state, 0, sizeof(s_validation_state));
    s_validation_state.app_config_valid = false;
    s_validation_state.has_received_validation = false;
}

static void brain_executor_reset_context(brain_executor_state_t state) {
    memset(&s_executor_ctx, 0, sizeof(s_executor_ctx));
    s_executor_ctx.state = state;
    s_executor_ctx.stop_requested = false;
    s_last_button_press_valid = false;
    s_last_button_press_addr = 0;
    s_if_depth = 0;
}

static void clear_per_pc_params(void)
{
    memset(s_loop_count_by_pc, 0, sizeof(s_loop_count_by_pc));
    memset(s_loop_count_valid_by_pc, 0, sizeof(s_loop_count_valid_by_pc));
    memset(s_delay_ms_by_pc, 0, sizeof(s_delay_ms_by_pc));
    memset(s_delay_ms_valid_by_pc, 0, sizeof(s_delay_ms_valid_by_pc));
    memset(s_loop_count_stash_valid_by_addr, 0, sizeof(s_loop_count_stash_valid_by_addr));
    memset(s_delay_ms_stash_valid_by_addr, 0, sizeof(s_delay_ms_stash_valid_by_addr));
    memset(s_led_color_by_pc, 0, sizeof(s_led_color_by_pc));
    memset(s_led_color_valid_by_pc, 0, sizeof(s_led_color_valid_by_pc));
    memset(s_led_color_stash_valid_by_addr, 0, sizeof(s_led_color_stash_valid_by_addr));
    memset(s_note_by_pc, 0, sizeof(s_note_by_pc));
    memset(s_note_valid_by_pc, 0, sizeof(s_note_valid_by_pc));
    memset(s_note_stash_valid_by_addr, 0, sizeof(s_note_stash_valid_by_addr));
    s_last_button_press_valid = false;
    s_last_button_press_addr = 0;
}

static bool loop_count_addr_to_slot(uint8_t addr, size_t *out_slot)
{
    if (addr < DEVICE_REGISTRY_ADDR_MIN || addr > DEVICE_REGISTRY_ADDR_MAX || out_slot == NULL) {
        return false;
    }
    *out_slot = (size_t)(addr - DEVICE_REGISTRY_ADDR_MIN);
    return true;
}

// START snapshot can mark a slot !present if the scan raced; still dispatch if live config agrees.
// After rescans, row index `pc` in committed config can disagree with the frozen program while the
// child at s_program_addr[pc] is still the right device — use the registry as a third source.
static bool program_slot_effectively_present(uint8_t pc)
{
    if (pc >= s_executor_ctx.program_len) {
        return false;
    }
    if (s_program_present[pc]) {
        return true;
    }
    block_config_state_t live;
    if (block_config_manager_get_state_snapshot(&live) == ESP_OK && (int)pc < live.block_count) {
        const block_config_entry_t *e = &live.blocks[pc];
        if (e->present && e->block_type == s_executor_ctx.program[pc]) {
            return true;
        }
    }

    uint8_t addr = s_program_addr[pc];
    if (addr < DEVICE_REGISTRY_ADDR_MIN || addr > DEVICE_REGISTRY_ADDR_MAX) {
        return false;
    }
    const device_entry_t *de = device_registry_find(addr);
    if (de == NULL || !de->present) {
        return false;
    }
    block_type_t want = s_executor_ctx.program[pc];
    if (de->type == want) {
        return true;
    }
    // Reassigned addresses often read as UNKNOWN in the registry until WHOAMI is folded in; still run
    // output opcodes so NOTE/LED/MUSIC_SEQ are not dropped after address churn.
    if (de->type == BLOCK_TYPE_UNKNOWN &&
        (want == BLOCK_TYPE_NOTE || want == BLOCK_TYPE_MUSIC_SEQ || want == BLOCK_TYPE_LED_FLASH)) {
        ESP_LOGW(TAG,
                 "present pc=%u: registry UNKNOWN at 0x%02X but frozen step is %s — allowing dispatch",
                 (unsigned)pc,
                 addr,
                 block_type_to_string(want));
        return true;
    }
    return false;
}

// How many NOTE submit stashes are present; if exactly one, *out_only_slot receives its index.
static int note_stash_valid_slot_count(size_t *out_only_slot)
{
    int n = 0;
    size_t only = 0;
    for (size_t s = 0; s < BRAIN_STASH_ADDR_SLOT_COUNT; s++) {
        if (s_note_stash_valid_by_addr[s]) {
            n++;
            only = s;
        }
    }
    if (out_only_slot != NULL) {
        *out_only_slot = only;
    }
    return n;
}

// Run after refresh_loop_iteration_counts_from_i2c_registers(). When the user tapped Submit on
// the loop block, the stash reflects that intent and must override a stale/wrong REG_LOOP_COUNT read.
static void apply_loop_count_stash_to_program(void)
{
    block_config_state_t snap;
    if (block_config_manager_get_state_snapshot(&snap) != ESP_OK) {
        return;
    }

    int stash_n = 0;
    size_t sole_slot = 0;
    for (size_t s = 0; s < BRAIN_STASH_ADDR_SLOT_COUNT; s++) {
        if (s_loop_count_stash_valid_by_addr[s]) {
            stash_n++;
            sole_slot = s;
        }
    }

    int loop_block_rows = 0;
    for (int i = 0; i < snap.block_count && i < BRAIN_EXECUTOR_MAX_PROGRAM_BLOCKS; i++) {
        const block_config_entry_t *e = &snap.blocks[i];
        if (e->present && e->block_type == BLOCK_TYPE_LOOP) {
            loop_block_rows++;
        }
    }

    for (int i = 0; i < snap.block_count && i < BRAIN_EXECUTOR_MAX_PROGRAM_BLOCKS; i++) {
        const block_config_entry_t *entry = &snap.blocks[i];
        if (!entry->present || entry->block_type != BLOCK_TYPE_LOOP) {
            continue;
        }

        size_t slot = 0;
        bool by_addr = loop_count_addr_to_slot(entry->i2c_address, &slot) &&
                       s_loop_count_stash_valid_by_addr[slot];
        if (by_addr) {
            s_loop_count_by_pc[i] = s_loop_count_stash_by_addr[slot];
            s_loop_count_valid_by_pc[i] = true;
            ESP_LOGI(TAG,
                     "LOOP iterations from submit stash (overrides REG): pc=%u addr=0x%02X count=%u",
                     (unsigned)i,
                     entry->i2c_address,
                     (unsigned)s_loop_count_by_pc[i]);
        } else if (stash_n == 1 && loop_block_rows == 1) {
            s_loop_count_by_pc[i] = s_loop_count_stash_by_addr[sole_slot];
            s_loop_count_valid_by_pc[i] = true;
            ESP_LOGI(TAG,
                     "LOOP iterations from sole submit stash: pc=%u addr=0x%02X count=%u",
                     (unsigned)i,
                     entry->i2c_address,
                     (unsigned)s_loop_count_by_pc[i]);
        }
    }
}

// Poll loop block firmware over I2C so iteration count does not depend on DATA_READY events.
static void refresh_loop_iteration_counts_from_i2c_registers(void)
{
    block_config_state_t snap;
    if (block_config_manager_get_state_snapshot(&snap) != ESP_OK) {
        return;
    }

    for (int i = 0; i < snap.block_count && i < BRAIN_EXECUTOR_MAX_PROGRAM_BLOCKS; i++) {
        const block_config_entry_t *entry = &snap.blocks[i];
        if (!entry->present || entry->block_type != BLOCK_TYPE_LOOP) {
            continue;
        }

        uint8_t v = 1;
        esp_err_t err = i2c_read_reg(entry->i2c_address, REG_LOOP_COUNT, &v, 1);
        if (err != ESP_OK) {
            ESP_LOGW(TAG,
                     "REG_LOOP_COUNT read failed pc=%u addr=0x%02X (%s) — loop iterations may stay at 1",
                     (unsigned)i,
                     entry->i2c_address,
                     esp_err_to_name(err));
            continue;
        }
        if (v == 0) {
            v = 1;
        }
        s_loop_count_by_pc[i] = v;
        s_loop_count_valid_by_pc[i] = true;
        ESP_LOGI(TAG,
                 "LOOP iterations from REG_LOOP_COUNT: pc=%u addr=0x%02X count=%u",
                 (unsigned)i,
                 entry->i2c_address,
                 (unsigned)v);
    }
}

// Run after refresh_delay_ms_from_i2c_registers(); submit stash wins over REG when present.
static void apply_delay_stash_to_program(void)
{
    block_config_state_t snap;
    if (block_config_manager_get_state_snapshot(&snap) != ESP_OK) {
        return;
    }

    for (int i = 0; i < snap.block_count && i < BRAIN_EXECUTOR_MAX_PROGRAM_BLOCKS; i++) {
        const block_config_entry_t *entry = &snap.blocks[i];
        if (!entry->present || entry->block_type != BLOCK_TYPE_DELAY) {
            continue;
        }

        size_t slot = 0;
        if (!loop_count_addr_to_slot(entry->i2c_address, &slot)) {
            continue;
        }
        if (!s_delay_ms_stash_valid_by_addr[slot]) {
            continue;
        }

        s_delay_ms_by_pc[i] = s_delay_ms_stash_by_addr[slot];
        s_delay_ms_valid_by_pc[i] = true;
        ESP_LOGI(TAG,
                 "DELAY ms from submit stash (overrides REG): pc=%u addr=0x%02X delay_ms=%lu",
                 (unsigned)i,
                 entry->i2c_address,
                 (unsigned long)s_delay_ms_by_pc[i]);
    }
}

static void refresh_delay_ms_from_i2c_registers(void)
{
    block_config_state_t snap;
    if (block_config_manager_get_state_snapshot(&snap) != ESP_OK) {
        return;
    }

    for (int i = 0; i < snap.block_count && i < BRAIN_EXECUTOR_MAX_PROGRAM_BLOCKS; i++) {
        const block_config_entry_t *entry = &snap.blocks[i];
        if (!entry->present || entry->block_type != BLOCK_TYPE_DELAY) {
            continue;
        }

        uint8_t b[4];
        if (i2c_read_reg(entry->i2c_address, REG_DELAY_MS0, &b[0], 1) != ESP_OK) {
            continue;
        }
        if (i2c_read_reg(entry->i2c_address, REG_DELAY_MS1, &b[1], 1) != ESP_OK) {
            continue;
        }
        if (i2c_read_reg(entry->i2c_address, REG_DELAY_MS2, &b[2], 1) != ESP_OK) {
            continue;
        }
        if (i2c_read_reg(entry->i2c_address, REG_DELAY_MS3, &b[3], 1) != ESP_OK) {
            continue;
        }

        uint32_t v = (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) |
                     ((uint32_t)b[3] << 24);
        if (v == 0U) {
            v = (s_executor_params.delay_ms != 0U) ? s_executor_params.delay_ms : 500U;
        }

        s_delay_ms_by_pc[i] = v;
        s_delay_ms_valid_by_pc[i] = true;
        ESP_LOGI(TAG,
                 "DELAY ms from REG_DELAY_MS*: pc=%u addr=0x%02X delay_ms=%lu",
                 (unsigned)i,
                 entry->i2c_address,
                 (unsigned long)v);
    }
}

static void apply_led_stash_to_program(void)
{
    block_config_state_t snap;
    if (block_config_manager_get_state_snapshot(&snap) != ESP_OK) {
        return;
    }

    for (int i = 0; i < snap.block_count && i < BRAIN_EXECUTOR_MAX_PROGRAM_BLOCKS; i++) {
        const block_config_entry_t *entry = &snap.blocks[i];
        if (!entry->present || entry->block_type != BLOCK_TYPE_LED_FLASH) {
            continue;
        }

        size_t slot = 0;
        if (!loop_count_addr_to_slot(entry->i2c_address, &slot)) {
            continue;
        }
        if (!s_led_color_stash_valid_by_addr[slot]) {
            continue;
        }

        s_led_color_by_pc[i] = s_led_color_stash_by_addr[slot];
        s_led_color_valid_by_pc[i] = true;
        ESP_LOGI(TAG,
                 "LED color from submit stash: pc=%u addr=0x%02X color_id=%u",
                 (unsigned)i,
                 entry->i2c_address,
                 (unsigned)s_led_color_by_pc[i]);
    }
}

static void apply_note_stash_to_program(void)
{
    block_config_state_t snap;
    if (block_config_manager_get_state_snapshot(&snap) != ESP_OK) {
        return;
    }

    size_t sole_slot = 0;
    const int sole_stashes = note_stash_valid_slot_count(&sole_slot);

    for (int i = 0; i < snap.block_count && i < BRAIN_EXECUTOR_MAX_PROGRAM_BLOCKS; i++) {
        const block_config_entry_t *entry = &snap.blocks[i];
        if (!entry->present || entry->block_type != BLOCK_TYPE_NOTE) {
            continue;
        }

        size_t slot = 0;
        bool by_addr = loop_count_addr_to_slot(entry->i2c_address, &slot) &&
                       s_note_stash_valid_by_addr[slot];
        if (by_addr) {
            s_note_by_pc[i] = s_note_stash_by_addr[slot];
            s_note_valid_by_pc[i] = true;
            ESP_LOGI(TAG,
                     "NOTE config from submit stash: pc=%u addr=0x%02X",
                     (unsigned)i,
                     entry->i2c_address);
        } else if (sole_stashes == 1) {
            s_note_by_pc[i] = s_note_stash_by_addr[sole_slot];
            s_note_valid_by_pc[i] = true;
            ESP_LOGI(TAG,
                     "NOTE config from sole submit stash: pc=%u addr=0x%02X (stash key != current addr)",
                     (unsigned)i,
                     entry->i2c_address);
        }
    }
}

static void set_runtime_snapshot(brain_runtime_broadcast_state_t state,
                                 uint8_t pc,
                                 block_type_t step_type)
{
    s_runtime_snapshot.state = state;
    s_runtime_snapshot.pc = pc;
    s_runtime_snapshot.step_type = step_type;
    s_runtime_snapshot.updated_at_ms = now_ms();
}

static bool is_output_block(block_type_t type) {
    return type == BLOCK_TYPE_LED_FLASH ||
           type == BLOCK_TYPE_NOTE ||
           type == BLOCK_TYPE_MUSIC_SEQ;
}

static uint8_t executor_broadcast_pc(void)
{
    if (s_executor_ctx.program_len == 0) {
        return BRAIN_RUNTIME_PC_NONE;
    }

    // DELAY and BUTTON wait hold pc on that opcode; highlight matches strict pc semantics.
    if (s_executor_ctx.state == EXECUTOR_WAIT_DELAY || s_executor_ctx.state == EXECUTOR_WAIT_INPUT) {
        if (s_executor_ctx.pc >= s_executor_ctx.program_len) {
            return (uint8_t)(s_executor_ctx.program_len - 1U);
        }
        return s_executor_ctx.pc;
    }

    if (s_executor_ctx.pc >= s_executor_ctx.program_len) {
        return (uint8_t)(s_executor_ctx.program_len - 1U);
    }

    return s_executor_ctx.pc;
}

static block_type_t executor_broadcast_step_type(void)
{
    if (s_executor_ctx.program_len == 0) {
        return BLOCK_TYPE_UNKNOWN;
    }

    uint8_t pc = executor_broadcast_pc();
    if (pc == BRAIN_RUNTIME_PC_NONE || pc >= s_executor_ctx.program_len) {
        return BLOCK_TYPE_UNKNOWN;
    }

    return s_executor_ctx.program[pc];
}

void broadcast_runtime_state(brain_runtime_broadcast_state_t state, block_type_t step_type)
{
    block_config_state_t config_snapshot;
    uint8_t pc = executor_broadcast_pc();

    if (step_type == BLOCK_TYPE_UNKNOWN) {
        step_type = executor_broadcast_step_type();
    }

    set_runtime_snapshot(state, pc, step_type);
    status_strip_play_runtime_audio_event(state, pc, (uint8_t)step_type);

    if (block_config_manager_get_state_snapshot(&config_snapshot) != ESP_OK ||
        config_snapshot.block_count == 0) {
        return;
    }

    for (int i = 0; i < config_snapshot.block_count; i++) {
        const block_config_entry_t *entry = &config_snapshot.blocks[i];
        if (!entry->present) {
            continue;
        }

        esp_err_t ret = i2c_runtime_broadcast(entry->i2c_address, state, pc, step_type);
        if (ret != ESP_OK) {
            ESP_LOGD(TAG,
                     "Runtime broadcast failed addr=0x%02X state=%u pc=%u step=%s ret=%d",
                     entry->i2c_address,
                     (unsigned)state,
                     (unsigned)pc,
                     block_type_to_string(step_type),
                     (int)ret);
        }
    }
}

static int find_matching_end_index(uint8_t start_index, block_type_t start_type) {
    block_type_t end_type = BLOCK_TYPE_UNKNOWN;
    if (start_type == BLOCK_TYPE_IF) {
        end_type = BLOCK_TYPE_END_IF;
    } else if (start_type == BLOCK_TYPE_LOOP) {
        end_type = BLOCK_TYPE_END_LOOP;
    } else {
        return -1;
    }

    int depth = 0;
    for (int i = start_index + 1; i < s_executor_ctx.program_len; i++) {
        block_type_t type = s_executor_ctx.program[i];
        if (type == start_type) {
            depth++;
        } else if (type == end_type) {
            if (depth == 0) {
                return i;
            }
            depth--;
        }
    }
    return -1;
}

static int find_then_index(uint8_t start_index, uint8_t end_index) {
    for (int i = start_index + 1; i < end_index; i++) {
        if (s_executor_ctx.program[i] == BLOCK_TYPE_THEN) {
            return i;
        }
    }
    return -1;
}

// Canonical: IF -> BUTTON -> THEN. Bind the BUTTON immediately after IF (must lie before THEN).
static int find_bound_button_pc_after_if(uint8_t if_pc, uint8_t then_pc)
{
    if ((uint16_t)if_pc + 1U >= (uint16_t)then_pc) {
        return -1;
    }
    if (s_executor_ctx.program[if_pc + 1U] != BLOCK_TYPE_BUTTON) {
        return -1;
    }
    return (int)if_pc + 1;
}

static block_type_t get_block_type_by_addr(uint8_t addr)
{
    const block_config_state_t *config = block_config_manager_get_state();
    if (config == NULL) {
        return BLOCK_TYPE_UNKNOWN;
    }

    for (int i = 0; i < config->block_count; i++) {
        const block_config_entry_t *entry = &config->blocks[i];
        if (!entry->present) {
            continue;
        }
        if (entry->i2c_address == addr) {
            return entry->block_type;
        }
    }
    return BLOCK_TYPE_UNKNOWN;
}

// Prefer committed config; fall back to the device registry so I²C submits work before/without TCP validation.
static block_type_t resolve_block_type_for_event(uint8_t addr)
{
    block_type_t t = get_block_type_by_addr(addr);
    if (t != BLOCK_TYPE_UNKNOWN) {
        return t;
    }
    const device_entry_t *de = device_registry_find(addr);
    if (de != NULL && de->present) {
        return de->type;
    }
    return BLOCK_TYPE_UNKNOWN;
}

// NOTE block publish: [count, note0..] with 1 <= count <= 15 and full frame in one read (not used for LED: len 1).
static bool note_selection_submit_payload_is_valid(const uint8_t *payload, size_t payload_len)
{
    if (payload == NULL || payload_len < 2U) {
        return false;
    }
    uint8_t c = payload[0];
    if (c < 1U || c > 15U) {
        return false;
    }
    return payload_len >= (size_t)(1U + c);
}

static int program_loop_opcode_count(void)
{
    int n = 0;
    for (uint8_t i = 0; i < s_executor_ctx.program_len; i++) {
        if (s_executor_ctx.program[i] == BLOCK_TYPE_LOOP) {
            n++;
        }
    }
    return n;
}

// Iteration count for the LOOP opcode at `loop_pc`: addr stash (Submit) wins over a live REG read,
// then START-time per-pc tables (which can mismatch the physical block after config reorder).
static uint16_t resolve_loop_iteration_count(uint8_t loop_pc, const char **out_src)
{
    if (out_src != NULL) {
        *out_src = "glob";
    }

    if (loop_pc >= s_executor_ctx.program_len) {
        uint16_t g = s_executor_params.loop_count;
        return (g == 0U) ? 1U : g;
    }

    uint8_t addr = s_program_addr[loop_pc];
    size_t slot = 0;

    if (loop_count_addr_to_slot(addr, &slot) && s_loop_count_stash_valid_by_addr[slot]) {
        if (out_src != NULL) {
            *out_src = "addr_stash";
        }
        return s_loop_count_stash_by_addr[slot];
    }

    // One LOOP opcode + one stash entry: use it even if submit addr != frozen START addr (rebind).
    if (program_loop_opcode_count() == 1) {
        int stash_n = 0;
        size_t only_slot = 0;
        for (size_t s = 0; s < BRAIN_STASH_ADDR_SLOT_COUNT; s++) {
            if (s_loop_count_stash_valid_by_addr[s]) {
                stash_n++;
                only_slot = s;
            }
        }
        if (stash_n == 1) {
            if (out_src != NULL) {
                *out_src = "sole_stash";
            }
            return s_loop_count_stash_by_addr[only_slot];
        }
    }

    // REG_LOOP_COUNT only exists on LOOP firmware. Other blocks alias 0x0B to different meaning;
    // reading it from a NOTE block produced bogus huge counts (felt "infinite") and wrong audio.
    bool try_reg = (resolve_block_type_for_event(addr) == BLOCK_TYPE_LOOP);
    if (!try_reg && loop_pc < s_executor_ctx.program_len &&
        s_executor_ctx.program[loop_pc] == BLOCK_TYPE_LOOP) {
        try_reg = true;
    }
    if (try_reg) {
        uint8_t who = 0xFFu;
        if (i2c_read_reg(addr, REG_WHOAMI, &who, 1) == ESP_OK && who == (uint8_t)BLOCK_TYPE_LOOP) {
            uint8_t v = 1;
            if (i2c_read_reg(addr, REG_LOOP_COUNT, &v, 1) == ESP_OK) {
                if (v == 0U) {
                    v = 1U;
                }
                if (out_src != NULL) {
                    *out_src = "reg";
                }
                return (uint16_t)v;
            }
        }
    }

    if (loop_pc < BRAIN_EXECUTOR_MAX_PROGRAM_BLOCKS && s_loop_count_valid_by_pc[loop_pc] &&
        s_loop_count_by_pc[loop_pc] != 0U) {
        if (out_src != NULL) {
            *out_src = "pc_snap";
        }
        return s_loop_count_by_pc[loop_pc];
    }

    uint16_t g = s_executor_params.loop_count;
    if (out_src != NULL) {
        *out_src = "glob";
    }
    return (g == 0U) ? 1U : g;
}

// I²C target for this program index: prefer the latest scan row when it still matches the opcode
// frozen at START. Submit stash is keyed by the child's *current* address; stale START addresses
// caused wrong stash lookup (fallback to globals) and CMD_PLAY_NOTE sent to the wrong device.
static uint8_t resolve_exec_child_addr_for_pc(uint8_t pc)
{
    if (pc >= s_executor_ctx.program_len) {
        return 0;
    }

    block_config_state_t live;
    if (block_config_manager_get_state_snapshot(&live) != ESP_OK) {
        return s_program_addr[pc];
    }
    if ((int)pc >= live.block_count) {
        return s_program_addr[pc];
    }

    const block_config_entry_t *e = &live.blocks[pc];
    if (!e->present || e->block_type != s_executor_ctx.program[pc]) {
        return s_program_addr[pc];
    }

    if (e->i2c_address != s_program_addr[pc]) {
        ESP_LOGI(TAG,
                 "exec addr pc=%u: live 0x%02X (replacing START snapshot 0x%02X)",
                 (unsigned)pc,
                 e->i2c_address,
                 s_program_addr[pc]);
    }
    return e->i2c_address;
}

// Action I2C only to the program slot at `pc` (snapshot from START). Runtime broadcast still fans out.
static void dispatch_output_action(uint8_t pc, block_type_t step_type)
{
    ESP_LOGI(TAG, "dispatch_output_action: pc=%u type=%s", (unsigned)pc, block_type_to_string(step_type));
    if (pc >= s_executor_ctx.program_len || step_type == BLOCK_TYPE_UNKNOWN) {
        ESP_LOGW(TAG, "dispatch_output_action: bad pc=%u len=%u", (unsigned)pc,
                 (unsigned)s_executor_ctx.program_len);
        return;
    }

    if (!program_slot_effectively_present(pc)) {
        ESP_LOGW(TAG,
                 "dispatch_output_action: no present target at pc=%u type=%s",
                 (unsigned)pc,
                 block_type_to_string(step_type));
        return;
    }

    uint8_t addr = resolve_exec_child_addr_for_pc(pc);
    if (s_executor_ctx.program[pc] != step_type) {
        ESP_LOGW(TAG,
                 "dispatch_output_action: program/type mismatch pc=%u prog=%s step=%s",
                 (unsigned)pc,
                 block_type_to_string(s_executor_ctx.program[pc]),
                 block_type_to_string(step_type));
    }

    switch (step_type) {
        case BLOCK_TYPE_LED_FLASH: {
            uint8_t color_id = s_executor_params.color_id;
            if (pc < BRAIN_EXECUTOR_MAX_PROGRAM_BLOCKS && s_led_color_valid_by_pc[pc]) {
                color_id = s_led_color_by_pc[pc];
            }
            esp_err_t set_ret = i2c_set_led_color_id(addr, color_id);
            if (set_ret != ESP_OK) {
                ESP_LOGW(TAG, "LED_FLASH set color failed addr=0x%02X (ret=%d)", addr, (int)set_ret);
            }
            esp_err_t exec_ret = i2c_execute(addr);
            ESP_LOGD(TAG,
                     "SEQUENTIAL LED step pc=%u addr=0x%02X color=%u exec_ret=%d",
                     (unsigned)pc,
                     addr,
                     (unsigned)color_id,
                     (int)exec_ret);

            uint32_t wait_ms = 0;
            uint8_t status = 0;
            esp_err_t busy_ret = wait_for_status_busy(addr, 200U, &wait_ms, &status);
            if (busy_ret == ESP_OK) {
                ESP_LOGI(TAG, "LED_FLASH latency dispatch->BUSY = %u ms (addr=0x%02X status=0x%02X)", (unsigned)wait_ms, addr, (unsigned)status);
            } else {
                ESP_LOGW(TAG, "LED_FLASH did not report BUSY within 200 ms (addr=0x%02X ret=%d status=0x%02X)", addr, (int)busy_ret, (unsigned)status);
            }

            esp_err_t idle_ret = wait_for_status_idle(addr, 10000U, &wait_ms, &status);
            if (idle_ret == ESP_OK) {
                ESP_LOGI(TAG, "LED_FLASH finished (addr=0x%02X elapsed=%u ms)", addr, (unsigned)wait_ms);
            } else {
                ESP_LOGW(TAG, "LED_FLASH idle wait failed (addr=0x%02X ret=%d elapsed=%u ms status=0x%02X)", addr, (int)idle_ret, (unsigned)wait_ms, (unsigned)status);
            }
            break;
        }
        case BLOCK_TYPE_NOTE: {
            esp_err_t exec_ret = i2c_execute(addr);
            ESP_LOGD(TAG,
                     "SEQUENTIAL NOTE step pc=%u addr=0x%02X exec_ret=%d",
                     (unsigned)pc,
                     addr,
                     (int)exec_ret);

            uint32_t wait_ms = 0;
            uint8_t status = 0;
            esp_err_t busy_ret = wait_for_status_busy(addr, 200U, &wait_ms, &status);
            if (busy_ret == ESP_OK) {
                ESP_LOGI(TAG, "NOTE latency dispatch->BUSY = %u ms (addr=0x%02X status=0x%02X)", (unsigned)wait_ms, addr, (unsigned)status);
            } else {
                ESP_LOGW(TAG, "NOTE did not report BUSY within 200 ms (addr=0x%02X ret=%d status=0x%02X)", addr, (int)busy_ret, (unsigned)status);
            }

            esp_err_t idle_ret = wait_for_status_idle(addr, 10000U, &wait_ms, &status);
            if (idle_ret == ESP_OK) {
                ESP_LOGI(TAG, "NOTE finished (addr=0x%02X elapsed=%u ms)", addr, (unsigned)wait_ms);
            } else {
                ESP_LOGW(TAG, "NOTE idle wait failed (addr=0x%02X ret=%d elapsed=%u ms status=0x%02X)", addr, (int)idle_ret, (unsigned)wait_ms, (unsigned)status);
            }
            break;
        }
        case BLOCK_TYPE_MUSIC_SEQ: {
            esp_err_t exec_ret = i2c_execute(addr);
            ESP_LOGD(TAG,
                     "SEQUENTIAL MUSIC_SEQ step pc=%u addr=0x%02X exec_ret=%d",
                     (unsigned)pc,
                     addr,
                     (int)exec_ret);

            uint32_t elapsed_ms = 0;
            uint8_t status = 0;
            esp_err_t busy_ret = wait_for_status_busy(addr,
                                                      MUSIC_EXEC_BUSY_TIMEOUT_MS,
                                                      &elapsed_ms,
                                                      &status);
            if (busy_ret == ESP_OK) {
                ESP_LOGI(TAG,
                         "MUSIC_SEQ latency dispatch->BUSY = %u ms (addr=0x%02X status=0x%02X)",
                         (unsigned)elapsed_ms,
                         addr,
                         (unsigned)status);
            } else {
                ESP_LOGW(TAG,
                         "MUSIC_SEQ did not report BUSY within %u ms (addr=0x%02X ret=%d status=0x%02X)",
                         (unsigned)MUSIC_EXEC_BUSY_TIMEOUT_MS,
                         addr,
                         (int)busy_ret,
                         (unsigned)status);
            }

            esp_err_t idle_ret = wait_for_status_idle(addr, 60000U, &elapsed_ms, &status);
            if (idle_ret == ESP_OK) {
                ESP_LOGI(TAG, "MUSIC_SEQ finished (addr=0x%02X elapsed=%u ms)", addr, (unsigned)elapsed_ms);
            } else {
                ESP_LOGW(TAG, "MUSIC_SEQ idle wait failed (addr=0x%02X ret=%d elapsed=%u ms status=0x%02X)",
                         addr, (int)idle_ret, (unsigned)elapsed_ms, (unsigned)status);
            }
            break;
        }
        default:
            ESP_LOGW(TAG, "dispatch_output_action: unsupported step %s", block_type_to_string(step_type));
            break;
    }
}

static bool load_program_from_config(void) {
    block_config_state_t config_snapshot;
    if (block_config_manager_get_state_snapshot(&config_snapshot) != ESP_OK ||
        config_snapshot.block_count == 0) {
        return false;
    }

    memset(s_program_addr, 0, sizeof(s_program_addr));
    memset(s_program_present, 0, sizeof(s_program_present));

    s_executor_ctx.program_len = config_snapshot.block_count;
    for (int i = 0; i < config_snapshot.block_count && i < BRAIN_EXECUTOR_MAX_PROGRAM_BLOCKS; i++) {
        s_executor_ctx.program[i] = config_snapshot.blocks[i].block_type;
        s_program_addr[i] = config_snapshot.blocks[i].i2c_address;
        s_program_present[i] = config_snapshot.blocks[i].present;
    }
    return true;
}

static bool get_program_index_for_block_addr(uint8_t block_addr, uint8_t *out_index)
{
    if (out_index == NULL) {
        return false;
    }

    block_config_state_t config_snapshot;
    if (block_config_manager_get_state_snapshot(&config_snapshot) != ESP_OK ||
        config_snapshot.block_count == 0) {
        return false;
    }

    for (int i = 0; i < config_snapshot.block_count && i < BRAIN_EXECUTOR_MAX_PROGRAM_BLOCKS; i++) {
        const block_config_entry_t *entry = &config_snapshot.blocks[i];
        if (!entry->present) {
            continue;
        }
        if (entry->i2c_address == block_addr) {
            *out_index = (uint8_t)i;
            return true;
        }
    }

    return false;
}

typedef enum {
    EVT_SRC_MESSAGE = 0,
    EVT_SRC_BLOCK   = 1,
} brain_event_src_t;

typedef struct {
    brain_event_src_t src;
    union {
        char message[96];
        struct {
            uint8_t block_addr;
            uint8_t event_id;
            uint8_t payload[BRAIN_EVENT_QUEUE_PAYLOAD_MAX];
            uint8_t payload_len;
        } block;
    } data;
} brain_event_t;

static QueueHandle_t s_event_queue = NULL;
static SemaphoreHandle_t s_dispatch_mutex = NULL;

static bool parse_u8_token(const char *s, uint8_t *out) {
    if (!s || !out) {
        return false;
    }
    char *end = NULL;
    long value = strtol(s, &end, 0); // accepts decimal and 0x-prefixed hex
    if (end == s || *end != '\0' || value < 0 || value > 255) {
        return false;
    }
    *out = (uint8_t)value;
    return true;
}

static bool process_message_event(const char *message) {
    if (!message || message[0] == '\0') {
        return false;
    }

    if (strcasecmp(message, "START") == 0) {
        const block_config_state_t *cfg = block_config_manager_get_state();
        ESP_LOGI(TAG,
                 "START requested: validation_received=%s validation_ok=%s last_errors=%lu executor_state=%d block_count=%u scan_errors=%u queue_depth=%u",
                 s_validation_state.has_received_validation ? "true" : "false",
                 s_validation_state.app_config_valid ? "true" : "false",
                 (unsigned long)s_validation_state.last_error_count,
                 (int)s_executor_ctx.state,
                 (unsigned)((cfg != NULL) ? cfg->block_count : 0),
                 (unsigned)((cfg != NULL) ? cfg->error_count : 0),
                 (unsigned)((s_event_queue != NULL) ? uxQueueMessagesWaiting(s_event_queue) : 0));
        esp_err_t err = brain_executor_start();
        if (err != ESP_OK) {
            ESP_LOGW(TAG,
                     "START rejected: %s (validation_gate=%s)",
                     esp_err_to_name(err),
                     brain_event_handler_can_start_execution() ? "pass" : "fail");
            return false;
        }
        ESP_LOGI(TAG, "Handled START: executor started");
        broadcast_runtime_state(BRAIN_RUNTIME_RUNNING, executor_broadcast_step_type());
        broadcast_runtime_state(BRAIN_RUNTIME_RUNNING, executor_broadcast_step_type());
        return true;
    }

    if (strcasecmp(message, "STOP") == 0) {
        broadcast_runtime_state(BRAIN_RUNTIME_STOP, executor_broadcast_step_type());
        broadcast_runtime_state(BRAIN_RUNTIME_STOP, executor_broadcast_step_type());
        brain_executor_stop();
        ESP_LOGI(TAG, "Handled STOP: executor stop requested");
        return true;
    }

    // Optional direct-control commands for quick testing from app:
    //   SET_LED <addr> <color_id>
    //   EXEC <addr>
    //   RESET <addr>
    //   BRIGHT <addr> <brightness_0_255>
    char cmd[16] = {0};
    char arg1[16] = {0};
    char arg2[16] = {0};
    int n = sscanf(message, "%15s %15s %15s", cmd, arg1, arg2);

    if (n >= 2 && strcasecmp(cmd, "EXEC") == 0) {
        uint8_t addr = 0;
        if (!parse_u8_token(arg1, &addr)) {
            return false;
        }
        return i2c_execute(addr) == ESP_OK;
    }

    if (n >= 2 && strcasecmp(cmd, "RESET") == 0) {
        uint8_t addr = 0;
        if (!parse_u8_token(arg1, &addr)) {
            return false;
        }
        return i2c_reset(addr) == ESP_OK;
    }

    if (n >= 3 && strcasecmp(cmd, "SET_LED") == 0) {
        uint8_t addr = 0;
        uint8_t color_id = 0;
        if (!parse_u8_token(arg1, &addr) || !parse_u8_token(arg2, &color_id)) {
            return false;
        }
        return i2c_set_led_color_id(addr, color_id) == ESP_OK;
    }

    if (n >= 3 && strcasecmp(cmd, "BRIGHT") == 0) {
        uint8_t addr = 0;
        uint8_t brightness = 0;
        if (!parse_u8_token(arg1, &addr) || !parse_u8_token(arg2, &brightness)) {
            return false;
        }
        return i2c_matrix_set_brightness(addr, brightness) == ESP_OK;
    }

    ESP_LOGW(TAG, "Unhandled message: %s", message);
    return false;
}

static bool process_block_event(uint8_t block_addr,
                                uint8_t event_id,
                                const uint8_t *payload,
                                size_t payload_len) {
    if (event_id == BRAIN_BLOCK_EVENT_SELECTION_SUBMIT && payload && payload_len >= 1) {
        block_type_t type = resolve_block_type_for_event(block_addr);
        ESP_LOGI(TAG, "Block 0x%02X (%s) selection submit (len=%u)",
                 block_addr, block_type_to_string(type), (unsigned)payload_len);

        if (type == BLOCK_TYPE_LED_FLASH && payload_len == 1U) {
            uint8_t selection = payload[0];
            size_t slot = 0;
            if (loop_count_addr_to_slot(block_addr, &slot)) {
                s_led_color_stash_by_addr[slot] = selection;
                s_led_color_stash_valid_by_addr[slot] = true;
            }
            uint8_t pc_idx = 0;
            if (get_program_index_for_block_addr(block_addr, &pc_idx)) {
                s_led_color_by_pc[pc_idx] = selection;
                s_led_color_valid_by_pc[pc_idx] = true;
            }
            s_executor_params.color_id = selection;
            esp_err_t set_ret = i2c_set_led_color_id(block_addr, selection);
            esp_err_t exec_ret = i2c_execute(block_addr);
            return (set_ret == ESP_OK) && (exec_ret == ESP_OK);
        }

        const bool note_by_type = (type == BLOCK_TYPE_NOTE);
        const bool note_by_shape = note_selection_submit_payload_is_valid(payload, payload_len);
        if (note_by_type || note_by_shape) {
            if (!note_by_type && note_by_shape) {
                ESP_LOGI(TAG,
                         "NOTE selection submit inferred from payload (addr=0x%02X registry/config was %s)",
                         block_addr,
                         block_type_to_string(type));
            }
            // NOTE block wire format is always [count, note0, …] (even for one note: [1, id]).
            // A truncated I²C read often yields len==1 (only the count byte). Treating that byte
            // as a legacy single note_id produced wrong pitch and broke sequences.
            if (payload_len < 2U) {
                ESP_LOGW(TAG,
                         "NOTE submit ignored at 0x%02X: need len>=2 [count,note…], got len=%u",
                         block_addr,
                         (unsigned)payload_len);
                return false;
            }

            brain_note_config_t note_cfg = {0};
            uint8_t count = payload[0];
            if (count > 15U) {
                count = 15U;
            }
            if ((size_t)(1U + count) > payload_len) {
                count = (uint8_t)(payload_len - 1U);
                if (count > 15U) {
                    count = 15U;
                }
            }
            if (count < 1U) {
                ESP_LOGW(TAG, "NOTE submit ignored at 0x%02X: count clamped to zero", block_addr);
                return false;
            }

            note_cfg.note_seq_len = count;
            for (uint8_t i = 0; i < count; i++) {
                note_cfg.note_seq[i] = (uint8_t)(payload[1 + i] % 7U);
            }
            note_cfg.note_id = note_cfg.note_seq[count - 1U];

            s_executor_params.note_seq_len = count;
            memcpy(s_executor_params.note_seq, note_cfg.note_seq, sizeof(note_cfg.note_seq));
            s_executor_params.note_id = note_cfg.note_id;
            ESP_LOGI(TAG, "Executor note sequence updated len=%u", (unsigned)count);

            size_t slot = 0;
            if (loop_count_addr_to_slot(block_addr, &slot)) {
                s_note_stash_by_addr[slot] = note_cfg;
                s_note_stash_valid_by_addr[slot] = true;
            }
            uint8_t pc_idx = 0;
            if (get_program_index_for_block_addr(block_addr, &pc_idx)) {
                s_note_by_pc[pc_idx] = note_cfg;
                s_note_valid_by_pc[pc_idx] = true;
            }
            return true;
        }
    }

    if (event_id == BRAIN_BLOCK_EVENT_LOOP_COUNT_SUBMIT && payload && payload_len >= 1) {
        uint16_t loop_count = (payload[0] == 0) ? 1 : payload[0];

        size_t slot = 0;
        if (loop_count_addr_to_slot(block_addr, &slot)) {
            s_loop_count_stash_by_addr[slot] = loop_count;
            s_loop_count_stash_valid_by_addr[slot] = true;
        }

        // Keep executor globals in sync so any fallback path (scan !present, etc.) sees latest submit.
        s_executor_params.loop_count = loop_count;
        if (s_executor_params.loop_count == 0U) {
            s_executor_params.loop_count = 1U;
        }

        uint8_t pc = 0;
        if (get_program_index_for_block_addr(block_addr, &pc)) {
            s_loop_count_by_pc[pc] = loop_count;
            s_loop_count_valid_by_pc[pc] = true;
            ESP_LOGI(TAG, "LOOP_COUNT submit: addr=0x%02X pc=%u loop_count=%u",
                     block_addr, (unsigned)pc, (unsigned)loop_count);
        } else {
            ESP_LOGI(TAG,
                     "LOOP_COUNT stashed for addr=0x%02X count=%u (binds to program on START)",
                     block_addr, (unsigned)loop_count);
        }
        return true;
    }

    if (event_id == BRAIN_BLOCK_EVENT_DELAY_MS_SUBMIT && payload && payload_len >= 4) {
        uint32_t v = 0;
        v |= (uint32_t)payload[0];
        v |= ((uint32_t)payload[1] << 8);
        v |= ((uint32_t)payload[2] << 16);
        v |= ((uint32_t)payload[3] << 24);

        size_t slot = 0;
        if (loop_count_addr_to_slot(block_addr, &slot)) {
            s_delay_ms_stash_by_addr[slot] = v;
            s_delay_ms_stash_valid_by_addr[slot] = true;
        }

        uint8_t pc = 0;
        if (get_program_index_for_block_addr(block_addr, &pc)) {
            s_delay_ms_by_pc[pc] = v;
            s_delay_ms_valid_by_pc[pc] = true;
            ESP_LOGI(TAG, "DELAY_MS submit: addr=0x%02X pc=%u delay_ms=%lu",
                     block_addr, (unsigned)pc, (unsigned long)v);
        } else {
            ESP_LOGI(TAG,
                     "DELAY_MS stashed for addr=0x%02X delay_ms=%lu (binds to program on START)",
                     block_addr, (unsigned long)v);
        }
        return true;
    }

    if (event_id == BRAIN_BLOCK_EVENT_BUTTON_PRESS) {
        uint8_t pressed = 1;
        if (payload && payload_len >= 1) {
            pressed = payload[0];
        }
        if (pressed != 0) {
            bool update_if_latch = true;
            if (s_executor_ctx.state == EXECUTOR_WAIT_INPUT &&
                s_executor_ctx.pc < s_executor_ctx.program_len &&
                s_executor_ctx.program[s_executor_ctx.pc] == BLOCK_TYPE_BUTTON &&
                s_program_present[s_executor_ctx.pc]) {
                if (block_addr == s_program_addr[s_executor_ctx.pc]) {
                    s_executor_ctx.button_pressed = true;
                } else {
                    // Wrong button while waiting on a BUTTON step: do not arm IF latch.
                    update_if_latch = false;
                }
            }
            if (update_if_latch) {
                s_last_button_press_addr = block_addr;
                s_last_button_press_valid = true;
            }
        }
        ESP_LOGI(TAG, "BUTTON_PRESS: addr=0x%02X pressed=%u", block_addr, (unsigned)pressed);
        return true;
    }

    ESP_LOGW(TAG, "Unhandled block event: addr=0x%02X id=0x%02X len=%u",
             block_addr, event_id, (unsigned)payload_len);
    return false;
}

static void brain_event_task(void *arg) {
    (void)arg;
    brain_event_t evt;
    while (1) {
        if (xQueueReceive(s_event_queue, &evt, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (evt.src == EVT_SRC_MESSAGE) {
            process_message_event(evt.data.message);
        } else if (s_dispatch_mutex != NULL &&
                   xSemaphoreTakeRecursive(s_dispatch_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
            process_block_event(evt.data.block.block_addr,
                                evt.data.block.event_id,
                                evt.data.block.payload,
                                evt.data.block.payload_len);
            xSemaphoreGiveRecursive(s_dispatch_mutex);
        }
    }
}

void brain_event_handler_init(void) {
    set_default_validation_state();
    brain_executor_reset_context(EXECUTOR_IDLE);
    clear_per_pc_params();
    memset(&s_executor_params, 0, sizeof(s_executor_params));
    s_executor_params.loop_count = 1;
    s_executor_params.delay_ms = 500;
    set_runtime_snapshot(BRAIN_RUNTIME_IDLE, BRAIN_RUNTIME_PC_NONE, BLOCK_TYPE_BRAIN);
    set_runtime_snapshot(BRAIN_RUNTIME_IDLE, BRAIN_RUNTIME_PC_NONE, BLOCK_TYPE_BRAIN);
    ESP_LOGI(TAG, "brain_event_handler_init");

    if (s_event_queue) {
        ESP_LOGI(TAG, "brain_event_handler already initialized");
        return;
    }

    if (s_dispatch_mutex == NULL) {
        s_dispatch_mutex = xSemaphoreCreateRecursiveMutex();
        if (s_dispatch_mutex == NULL) {
            ESP_LOGE(TAG, "Failed to create dispatch recursive mutex");
            return;
        }
    }

    s_event_queue = xQueueCreate(24, sizeof(brain_event_t));
    if (!s_event_queue) {
        ESP_LOGE(TAG, "Failed to create brain event queue");
        return;
    }

    // Keep Brain event orchestration on Core 0; GUI runs on Core 1.
    BaseType_t ok_evt = xTaskCreatePinnedToCore(brain_event_task, "brain_evt", 8192, NULL, 5, NULL, 0);
    if (ok_evt != pdPASS) {
        ESP_LOGE(TAG, "Failed to create brain event task");
        return;
    }

    // brain_executor_tick() is driven from main.c (brain_executor_task) when
    // ENABLE_BRAIN_EXECUTOR_TASK is enabled — avoid a second tick task here.

    ESP_LOGI(TAG, "brain_event_handler initialized");
}

void brain_event_handler_reset_validation(void) {
    set_default_validation_state();
    ESP_LOGW(TAG, "Validation state reset to invalid");
}

void brain_event_handler_set_config_validation(bool is_valid, uint32_t error_count, uint64_t timestamp_ms) {
    s_validation_state.app_config_valid = is_valid;
    s_validation_state.last_error_count = error_count;
    s_validation_state.last_validation_ts_ms = timestamp_ms;
    s_validation_state.has_received_validation = true;
    ESP_LOGI(TAG, "Validation updated: valid=%s errors=%lu ts=%llu",
             is_valid ? "true" : "false",
             (unsigned long)error_count,
             (unsigned long long)timestamp_ms);
}

const brain_validation_state_t *brain_event_handler_get_validation_state(void) {
    return &s_validation_state;
}

bool brain_event_handler_can_start_execution(void) {
    if (s_validation_state.has_received_validation && s_validation_state.app_config_valid) {
        return true;
    }
    // Standalone (no companion / no valid app validation): allow START if the committed stack has
    // at least one present, typed child. Requiring error_count==0 was too strict — scans often bump
    // error_count for transient missing_block / identity churn while the physical chain still runs.
    const block_config_state_t *cfg = block_config_manager_get_state();
    if (cfg == NULL || cfg->block_count == 0U) {
        return false;
    }
    for (int i = 0; i < cfg->block_count && i < BLOCK_CONFIG_MAX_BLOCKS; i++) {
        const block_config_entry_t *e = &cfg->blocks[i];
        if (e->present && e->block_type != BLOCK_TYPE_UNKNOWN) {
            return true;
        }
    }
    return false;
}

const brain_runtime_snapshot_t *brain_event_handler_get_runtime_snapshot(void) {
    return &s_runtime_snapshot;
}

void brain_executor_set_params(const brain_executor_params_t *params) {
    if (params == NULL || s_dispatch_mutex == NULL) {
        return;
    }
    if (xSemaphoreTakeRecursive(s_dispatch_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }
    s_executor_params = *params;
    if (s_executor_params.loop_count == 0) {
        s_executor_params.loop_count = 1;
    }
    xSemaphoreGiveRecursive(s_dispatch_mutex);
}

const brain_executor_context_t *brain_executor_get_context(void) {
    return &s_executor_ctx;
}

bool brain_executor_prefers_i2c_yield(void)
{
    switch (s_executor_ctx.state) {
        case EXECUTOR_RUNNING:
        case EXECUTOR_WAIT_DELAY:
        case EXECUTOR_WAIT_INPUT:
            return true;
        default:
            return false;
    }
}

void brain_executor_set_button_state(bool is_pressed) {
    if (s_dispatch_mutex == NULL) {
        return;
    }
    if (xSemaphoreTakeRecursive(s_dispatch_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }
    s_executor_ctx.button_pressed = is_pressed;
    xSemaphoreGiveRecursive(s_dispatch_mutex);
}

static esp_err_t brain_executor_start_nolock(void) {
    if (!brain_event_handler_can_start_execution()) {
        uint64_t now = now_ms();
        s_validation_block_attempts_since_last_log++;

        bool should_log = false;
        if (s_last_validation_block_log_ms == 0U ||
            (now - s_last_validation_block_log_ms) >= BRAIN_EXEC_VALIDATION_LOG_INTERVAL_MS) {
            should_log = true;
        }

        if (should_log) {
            const block_config_state_t *cfg = block_config_manager_get_state();
            uint32_t suppressed = (s_validation_block_attempts_since_last_log > 0U)
                                      ? (s_validation_block_attempts_since_last_log - 1U)
                                      : 0U;
            ESP_LOGW(TAG,
                     "brain_executor_start blocked by validation gate: validation_received=%s validation_ok=%s block_count=%u scan_errors=%u suppressed_since_last_log=%u",
                     s_validation_state.has_received_validation ? "true" : "false",
                     s_validation_state.app_config_valid ? "true" : "false",
                     (unsigned)((cfg != NULL) ? cfg->block_count : 0),
                     (unsigned)((cfg != NULL) ? cfg->error_count : 0),
                     (unsigned)suppressed);
            ESP_LOGD(TAG,
                     "brain_executor_start blocked (attempts_since_last_log=%u suppressed=%u)",
                     (unsigned)s_validation_block_attempts_since_last_log,
                     (unsigned)suppressed);
            s_last_validation_block_log_ms = now;
            s_validation_block_attempts_since_last_log = 0U;
        } else {
            ESP_LOGD(TAG, "brain_executor_start blocked (throttled)");
        }
        return ESP_ERR_INVALID_STATE;
    }

    brain_executor_reset_context(EXECUTOR_IDLE);
    if (!load_program_from_config()) {
        const block_config_state_t *cfg = block_config_manager_get_state();
        ESP_LOGW(TAG,
                 "Cannot start executor: no program blocks (block_count=%u scan_errors=%u)",
                 (unsigned)((cfg != NULL) ? cfg->block_count : 0),
                 (unsigned)((cfg != NULL) ? cfg->error_count : 0));
        return ESP_ERR_INVALID_STATE;
    }

    memset(s_loop_count_by_pc, 0, sizeof(s_loop_count_by_pc));
    memset(s_loop_count_valid_by_pc, 0, sizeof(s_loop_count_valid_by_pc));
    refresh_loop_iteration_counts_from_i2c_registers();
    apply_loop_count_stash_to_program();

    memset(s_delay_ms_by_pc, 0, sizeof(s_delay_ms_by_pc));
    memset(s_delay_ms_valid_by_pc, 0, sizeof(s_delay_ms_valid_by_pc));
    refresh_delay_ms_from_i2c_registers();
    apply_delay_stash_to_program();

    apply_led_stash_to_program();
    apply_note_stash_to_program();

    {
        block_config_state_t snap;
        if (block_config_manager_get_state_snapshot(&snap) == ESP_OK) {
            for (int i = 0; i < s_executor_ctx.program_len && i < BRAIN_EXECUTOR_MAX_PROGRAM_BLOCKS; i++) {
                if (s_executor_ctx.program[i] != BLOCK_TYPE_LOOP) {
                    continue;
                }
                if (s_loop_count_valid_by_pc[i]) {
                    continue;
                }
                uint8_t addr = (i < snap.block_count) ? snap.blocks[i].i2c_address : 0U;
                ESP_LOGW(TAG,
                         "LOOP at pc=%u (addr=0x%02X) has no iteration count after stash+I2C; using default 1",
                         (unsigned)i, addr);
            }
        }
    }

    s_executor_ctx.state = EXECUTOR_RUNNING;
    ESP_LOGI(TAG, "Executor started with %u blocks", s_executor_ctx.program_len);
    for (int i = 0; i < s_executor_ctx.program_len && i < BRAIN_EXECUTOR_MAX_PROGRAM_BLOCKS; i++) {
        ESP_LOGI(TAG, "  program[%d]: type=%s addr=0x%02X present=%d",
                 i,
                 block_type_to_string(s_executor_ctx.program[i]),
                 s_program_addr[i],
                 (int)s_program_present[i]);
    }
    return ESP_OK;
}

esp_err_t brain_executor_start(void) {
    if (s_dispatch_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTakeRecursive(s_dispatch_mutex, pdMS_TO_TICKS(BRAIN_DISPATCH_MUTEX_TIMEOUT_MS)) !=
        pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t err = brain_executor_start_nolock();
    xSemaphoreGiveRecursive(s_dispatch_mutex);
    return err;
}

void brain_executor_stop(void) {
    // Do not take s_dispatch_mutex: brain_executor_tick_nolock may hold it for a long time
    // during NOTE sequence playback; STOP must still set the flag so waits can abort.
    s_executor_ctx.stop_requested = true;
}

static void brain_executor_tick_nolock(void) {
    if (s_executor_ctx.stop_requested) {
        broadcast_runtime_state(BRAIN_RUNTIME_STOP, executor_broadcast_step_type());
        broadcast_runtime_state(BRAIN_RUNTIME_STOP, executor_broadcast_step_type());
        brain_executor_reset_context(EXECUTOR_STOPPED);
        ESP_LOGI(TAG, "Executor stopped");
        return;
    }

    if (s_executor_ctx.state == EXECUTOR_WAIT_DELAY) {
        if (now_ms() >= s_executor_ctx.wait_until_ms) {
            s_executor_ctx.state = EXECUTOR_RUNNING;
            s_executor_ctx.pc++;
            broadcast_runtime_state(BRAIN_RUNTIME_RUNNING, executor_broadcast_step_type());
        }
        return;
    }

    if (s_executor_ctx.state == EXECUTOR_WAIT_INPUT) {
        if (s_executor_ctx.button_pressed) {
            s_executor_ctx.state = EXECUTOR_RUNNING;
            s_executor_ctx.button_pressed = false;
            s_executor_ctx.pc++;
            broadcast_runtime_state(BRAIN_RUNTIME_RUNNING, executor_broadcast_step_type());
        }
        return;
    }

    if (s_executor_ctx.state != EXECUTOR_RUNNING) {
        return;
    }

    if (s_executor_ctx.pc >= s_executor_ctx.program_len) {
        broadcast_runtime_state(BRAIN_RUNTIME_DONE, executor_broadcast_step_type());
        broadcast_runtime_state(BRAIN_RUNTIME_DONE, executor_broadcast_step_type());
        s_executor_ctx.state = EXECUTOR_DONE;
        ESP_LOGI(TAG, "Executor done");
        return;
    }

    block_type_t current = s_executor_ctx.program[s_executor_ctx.pc];
    ESP_LOGI(TAG, "EXEC tick pc=%u type=%s addr=0x%02X present=%d",
             s_executor_ctx.pc,
             block_type_to_string(current),
             s_program_addr[s_executor_ctx.pc],
             (int)s_program_present[s_executor_ctx.pc]);

    if (current == BLOCK_TYPE_BUTTON) {
        if (s_executor_ctx.pc < BRAIN_EXECUTOR_MAX_PROGRAM_BLOCKS && s_program_present[s_executor_ctx.pc]) {
            (void)i2c_execute(s_program_addr[s_executor_ctx.pc]);
        }
        s_executor_ctx.button_pressed = false;
        s_executor_ctx.state = EXECUTOR_WAIT_INPUT;
        return;
    }

    if (current == BLOCK_TYPE_THEN) {
        if (s_if_depth > 0U && s_if_stack[s_if_depth - 1U].then_pc == s_executor_ctx.pc) {
            brain_if_frame_t *fr = &s_if_stack[s_if_depth - 1U];
            bool condition_true = false;
            if (fr->bound_button_pc != 0xFFu &&
                (size_t)fr->bound_button_pc < BRAIN_EXECUTOR_MAX_PROGRAM_BLOCKS &&
                s_executor_ctx.program[fr->bound_button_pc] == BLOCK_TYPE_BUTTON &&
                s_program_present[fr->bound_button_pc] &&
                s_last_button_press_valid &&
                s_last_button_press_addr == s_program_addr[fr->bound_button_pc]) {
                condition_true = true;
            }
            s_last_button_press_valid = false;
            if (!condition_true) {
                s_executor_ctx.pc = (uint8_t)(fr->end_if_pc + 1U);
                s_if_depth--;
                return;
            }
            s_executor_ctx.pc++;
            return;
        }
        s_executor_ctx.pc++;
        return;
    }

    if (is_output_block(current)) {
        dispatch_output_action(s_executor_ctx.pc, current);
        if (s_executor_ctx.stop_requested) {
            return;
        }
        s_executor_ctx.pc++;
        if (s_executor_ctx.pc < s_executor_ctx.program_len) {
            broadcast_runtime_state(BRAIN_RUNTIME_RUNNING, executor_broadcast_step_type());
        }
        return;
    }

    switch (current) {
        case BLOCK_TYPE_DELAY: {
            uint32_t delay_ms = s_executor_params.delay_ms;
            if (s_executor_params.delay_ms == 0U) {
                delay_ms = 500U;
            }
            if (s_executor_ctx.pc < BRAIN_EXECUTOR_MAX_PROGRAM_BLOCKS &&
                s_delay_ms_valid_by_pc[s_executor_ctx.pc]) {
                delay_ms = s_delay_ms_by_pc[s_executor_ctx.pc];
            }
            ESP_LOGI(TAG,
                     "DELAY pc=%u wait_ms=%lu",
                     (unsigned)s_executor_ctx.pc,
                     (unsigned long)delay_ms);
            s_executor_ctx.wait_until_ms = now_ms() + delay_ms;
            s_executor_ctx.state = EXECUTOR_WAIT_DELAY;
            return;
        }

        case BLOCK_TYPE_IF: {
            int end_if_index = find_matching_end_index(s_executor_ctx.pc, BLOCK_TYPE_IF);
            if (end_if_index < 0) {
                ESP_LOGW(TAG, "IF without END_IF at pc=%u", s_executor_ctx.pc);
                broadcast_runtime_state(BRAIN_RUNTIME_ERROR, current);
                s_executor_ctx.state = EXECUTOR_ERROR;
                return;
            }

            int then_index = find_then_index(s_executor_ctx.pc, (uint8_t)end_if_index);
            if (then_index < 0) {
                ESP_LOGW(TAG, "IF without THEN at pc=%u", s_executor_ctx.pc);
                broadcast_runtime_state(BRAIN_RUNTIME_ERROR, current);
                s_executor_ctx.state = EXECUTOR_ERROR;
                return;
            }

            if (s_if_depth >= BRAIN_EXECUTOR_MAX_IF_DEPTH) {
                ESP_LOGW(TAG, "IF stack overflow at pc=%u", s_executor_ctx.pc);
                broadcast_runtime_state(BRAIN_RUNTIME_ERROR, current);
                s_executor_ctx.state = EXECUTOR_ERROR;
                return;
            }

            int bound_btn_pc = find_bound_button_pc_after_if(s_executor_ctx.pc, (uint8_t)then_index);
            uint8_t bound = (bound_btn_pc >= 0) ? (uint8_t)bound_btn_pc : 0xFFu;

            s_if_stack[s_if_depth++] = (brain_if_frame_t){
                .end_if_pc = (uint8_t)end_if_index,
                .then_pc = (uint8_t)then_index,
                .bound_button_pc = bound,
            };
            s_executor_ctx.pc++;
            return;
        }

        case BLOCK_TYPE_END_IF:
            if (s_if_depth > 0U && s_if_stack[s_if_depth - 1U].end_if_pc == s_executor_ctx.pc) {
                s_if_depth--;
            }
            s_executor_ctx.pc++;
            return;

        case BLOCK_TYPE_LOOP: {
            int end_loop_index = find_matching_end_index(s_executor_ctx.pc, BLOCK_TYPE_LOOP);
            if (end_loop_index < 0) {
                ESP_LOGW(TAG, "LOOP without END_LOOP at pc=%u", s_executor_ctx.pc);
                broadcast_runtime_state(BRAIN_RUNTIME_ERROR, current);
                s_executor_ctx.state = EXECUTOR_ERROR;
                return;
            }

            const char *lc_src = NULL;
            uint16_t loop_count = resolve_loop_iteration_count(s_executor_ctx.pc, &lc_src);
            if (loop_count > BRAIN_EXECUTOR_LOOP_COUNT_MAX) {
                ESP_LOGW(TAG,
                         "LOOP pc=%u capping iterations %u -> %u (src=%s)",
                         (unsigned)s_executor_ctx.pc,
                         (unsigned)loop_count,
                         (unsigned)BRAIN_EXECUTOR_LOOP_COUNT_MAX,
                         lc_src != NULL ? lc_src : "?");
                loop_count = (uint16_t)BRAIN_EXECUTOR_LOOP_COUNT_MAX;
            }

            if (loop_count == 0) {
                s_executor_ctx.pc = (uint8_t)(end_loop_index + 1);
                return;
            }

            if (s_executor_ctx.loop_depth >= BRAIN_EXECUTOR_MAX_LOOP_DEPTH) {
                ESP_LOGW(TAG, "Loop stack overflow");
                broadcast_runtime_state(BRAIN_RUNTIME_ERROR, current);
                s_executor_ctx.state = EXECUTOR_ERROR;
                return;
            }

            brain_loop_frame_t *frame = &s_executor_ctx.loop_stack[s_executor_ctx.loop_depth++];
            frame->loop_start_pc = s_executor_ctx.pc;
            frame->loop_end_pc = (uint8_t)end_loop_index;
            frame->remaining_iterations = loop_count;
            ESP_LOGI(TAG,
                     "LOOP pc=%u body through pc=%d iterations=%u src=%s",
                     (unsigned)frame->loop_start_pc,
                     end_loop_index,
                     (unsigned)loop_count,
                     lc_src != NULL ? lc_src : "?");
            s_executor_ctx.pc++;
            return;
        }

        case BLOCK_TYPE_END_LOOP:
            if (s_executor_ctx.loop_depth == 0) {
                s_executor_ctx.pc++;
                return;
            } else {
                brain_loop_frame_t *frame = &s_executor_ctx.loop_stack[s_executor_ctx.loop_depth - 1];
                if (frame->loop_end_pc != s_executor_ctx.pc) {
                    ESP_LOGW(TAG,
                             "END_LOOP pc=%u does not match expected %u — advancing (stack not popped)",
                             (unsigned)s_executor_ctx.pc,
                             (unsigned)frame->loop_end_pc);
                    s_executor_ctx.pc++;
                    return;
                }
                if (frame->remaining_iterations > 1) {
                    frame->remaining_iterations--;
                    s_executor_ctx.pc = (uint8_t)(frame->loop_start_pc + 1);
                } else {
                    s_executor_ctx.loop_depth--;
                    s_executor_ctx.pc++;
                }
            }
            return;

        default:
            s_executor_ctx.pc++;
            return;
    }
}

void brain_executor_tick(void) {
    if (s_dispatch_mutex == NULL) {
        brain_executor_tick_nolock();
        return;
    }
    if (xSemaphoreTakeRecursive(s_dispatch_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        return;
    }
    brain_executor_tick_nolock();
    xSemaphoreGiveRecursive(s_dispatch_mutex);
}

bool brain_event_handle_message(const char *message) {
    if (!message) {
        ESP_LOGW(TAG, "brain_event_handle_message called with NULL message");
        return false;
    }

    // START/STOP run here (not via the event queue) so the return value reflects whether
    // the executor actually accepted the command. Queuing them only reported enqueue success,
    // which made the UI/TCP ACK lie when the queue was full or executor_start failed later.
    if (strcasecmp(message, "START") == 0 || strcasecmp(message, "STOP") == 0) {
        if (strcasecmp(message, "STOP") == 0) {
            // Arm immediately so NOTE playback (which holds s_dispatch_mutex) can see STOP
            // without waiting for this path to acquire the mutex.
            s_executor_ctx.stop_requested = true;
        }
        if (s_dispatch_mutex == NULL) {
            ESP_LOGW(TAG, "brain_event_handle_message: dispatch mutex not ready (msg=%s)", message);
            return strcasecmp(message, "STOP") == 0;
        }
        if (xSemaphoreTakeRecursive(s_dispatch_mutex, pdMS_TO_TICKS(BRAIN_DISPATCH_MUTEX_TIMEOUT_MS)) !=
            pdTRUE) {
            ESP_LOGW(TAG, "brain_event_handle_message: dispatch busy (msg=%s)", message);
            return strcasecmp(message, "STOP") == 0;
        }
        bool ok = process_message_event(message);
        xSemaphoreGiveRecursive(s_dispatch_mutex);
        return ok;
    }

    if (!s_event_queue) {
        ESP_LOGW(TAG, "brain_event_handle_message queue not initialized (msg=%s)", message);
        return false;
    }

    brain_event_t evt = {0};
    evt.src = EVT_SRC_MESSAGE;
    strncpy(evt.data.message, message, sizeof(evt.data.message) - 1);
    evt.data.message[sizeof(evt.data.message) - 1] = '\0';

    BaseType_t sent = xQueueSend(s_event_queue, &evt, pdMS_TO_TICKS(50));
    if (sent != pdTRUE) {
        ESP_LOGW(TAG, "Failed to queue message '%s' (queue_depth=%u)",
                 evt.data.message,
                 (unsigned)uxQueueMessagesWaiting(s_event_queue));
        return false;
    }

    ESP_LOGD(TAG, "Queued message '%s' (queue_depth=%u)",
             evt.data.message,
             (unsigned)uxQueueMessagesWaiting(s_event_queue));
    return true;
}

bool brain_event_handle_block_event(uint8_t block_addr,
                                    uint8_t event_id,
                                    const uint8_t *payload,
                                    size_t payload_len) {
    if (!s_event_queue || payload_len > BRAIN_EVENT_QUEUE_PAYLOAD_MAX) {
        if (payload_len > BRAIN_EVENT_QUEUE_PAYLOAD_MAX) {
            ESP_LOGW(TAG,
                     "Block event dropped: payload_len=%u > max %u (addr=0x%02X id=0x%02X)",
                     (unsigned)payload_len,
                     (unsigned)BRAIN_EVENT_QUEUE_PAYLOAD_MAX,
                     block_addr,
                     event_id);
        }
        return false;
    }

    brain_event_t evt = {0};
    evt.src = EVT_SRC_BLOCK;
    evt.data.block.block_addr = block_addr;
    evt.data.block.event_id = event_id;
    evt.data.block.payload_len = (uint8_t)payload_len;
    if (payload && payload_len > 0) {
        memcpy(evt.data.block.payload, payload, payload_len);
    }

    return xQueueSend(s_event_queue, &evt, pdMS_TO_TICKS(50)) == pdTRUE;
}
