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
#include "freertos/task.h"

#include "brain_block.h"
#include "audio_speaker.h"

static const char *TAG = "brain_evt";
static brain_validation_state_t s_validation_state;
static brain_executor_context_t s_executor_ctx;
static brain_executor_params_t s_executor_params;
static brain_executor_state_t s_last_broadcast_state = EXECUTOR_IDLE;
static uint8_t s_last_broadcast_pc = 0xFF;
static bool s_last_broadcast_valid = false;

// Per-program-position control-flow parameters (indexed by executor pc).
static uint16_t s_loop_count_by_pc[BRAIN_EXECUTOR_MAX_PROGRAM_BLOCKS];
static bool s_loop_count_valid_by_pc[BRAIN_EXECUTOR_MAX_PROGRAM_BLOCKS];
static uint32_t s_delay_ms_by_pc[BRAIN_EXECUTOR_MAX_PROGRAM_BLOCKS];
static bool s_delay_ms_valid_by_pc[BRAIN_EXECUTOR_MAX_PROGRAM_BLOCKS];

// Throttling for validation-gate start warnings to avoid log flooding.
#define BRAIN_EXEC_VALIDATION_LOG_INTERVAL_MS 2000U
static uint64_t s_last_validation_block_log_ms;
static uint32_t s_validation_block_attempts_since_last_log;

#define MUSIC_EXEC_BUSY_TIMEOUT_MS   200U
#define MUSIC_EXEC_LATENCY_TARGET_MS 50U

static uint64_t now_ms(void) {
    return (uint64_t)(esp_timer_get_time() / 1000);
}

static esp_err_t wait_for_status_busy(uint8_t addr, uint32_t timeout_ms, uint32_t *out_elapsed_ms, uint8_t *out_status)
{
    uint64_t start = now_ms();
    uint8_t status = 0;
    esp_err_t last_err = ESP_FAIL;

    while ((now_ms() - start) <= timeout_ms) {
        last_err = i2c_read_reg(addr, REG_STATUS, &status, 1);
        if (last_err == ESP_OK) {
            if ((status & STATUS_BUSY) != 0U) {
                if (out_elapsed_ms != NULL) {
                    *out_elapsed_ms = (uint32_t)(now_ms() - start);
                }
                if (out_status != NULL) {
                    *out_status = status;
                }
                return ESP_OK;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    if (out_elapsed_ms != NULL) {
        *out_elapsed_ms = (uint32_t)(now_ms() - start);
    }
    if (out_status != NULL) {
        *out_status = status;
    }
    return last_err;
}

static void set_default_validation_state(void) {
    memset(&s_validation_state, 0, sizeof(s_validation_state));
    s_validation_state.app_config_valid = false;
    s_validation_state.has_received_validation = false;
}

static void brain_executor_reset_context(brain_executor_state_t state) {
    memset(&s_executor_ctx, 0, sizeof(s_executor_ctx));
    s_executor_ctx.state = state;
}

static void clear_per_pc_params(void)
{
    memset(s_loop_count_by_pc, 0, sizeof(s_loop_count_by_pc));
    memset(s_loop_count_valid_by_pc, 0, sizeof(s_loop_count_valid_by_pc));
    memset(s_delay_ms_by_pc, 0, sizeof(s_delay_ms_by_pc));
    memset(s_delay_ms_valid_by_pc, 0, sizeof(s_delay_ms_valid_by_pc));
}

static bool is_output_block(block_type_t type) {
    return type == BLOCK_TYPE_LED_FLASH ||
           type == BLOCK_TYPE_NOTE ||
           type == BLOCK_TYPE_MUSIC_SEQ;
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

static bool find_first_block_address_by_type(block_type_t type, uint8_t *out_addr) {
    if (out_addr == NULL) {
        return false;
    }

    const block_config_state_t *config = block_config_manager_get_state();
    if (config == NULL) {
        return false;
    }

    for (int i = 0; i < config->block_count; i++) {
        const block_config_entry_t *entry = &config->blocks[i];
        if (!entry->present) {
            continue;
        }
        if (entry->block_type != type) {
            continue;
        }
        *out_addr = entry->i2c_address;
        return true;
    }

    return false;
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

static void play_brain_broadcast_audio(brain_broadcast_event_t event_id)
{
    static const uint32_t k_step_tick_hz = 880U;

    switch (event_id) {
        case BRAIN_BROADCAST_RUNNING:
            (void)speaker_play_boot_sound();
            break;
        case BRAIN_BROADCAST_STEP:
            (void)speaker_play_tone(k_step_tick_hz, 35U);
            break;
        case BRAIN_BROADCAST_DONE:
            speaker_beep_ok();
            break;
        case BRAIN_BROADCAST_ERROR:
            speaker_beep_error();
            break;
        case BRAIN_BROADCAST_STOP:
            (void)speaker_play_tone(220U, 120U);
            break;
        case BRAIN_BROADCAST_IDLE:
        default:
            break;
    }
}

static void broadcast_runtime_event(brain_broadcast_event_t event_id, uint8_t pc)
{
    block_config_state_t config_snapshot;
    if (block_config_manager_get_state_snapshot(&config_snapshot) != ESP_OK ||
        config_snapshot.block_count == 0) {
        return;
    }

    uint8_t sent = 0;
    for (int i = 0; i < config_snapshot.block_count; i++) {
        const block_config_entry_t *entry = &config_snapshot.blocks[i];
        if (!entry->present) {
            continue;
        }

        esp_err_t ret = i2c_broadcast_runtime_event(entry->i2c_address, (uint8_t)event_id, pc);
        if (ret == ESP_OK) {
            sent++;
        } else {
            ESP_LOGD(TAG, "BROADCAST failed addr=0x%02X type=%s ret=%s",
                     entry->i2c_address,
                     block_type_to_string(entry->block_type),
                     esp_err_to_name(ret));
        }
    }

    play_brain_broadcast_audio(event_id);
    ESP_LOGD(TAG, "BROADCAST event=%u pc=%u sent=%u",
             (unsigned)event_id, (unsigned)pc, (unsigned)sent);
}

static void broadcast_state_snapshot(void)
{
    const brain_executor_context_t *ctx = &s_executor_ctx;
    brain_broadcast_event_t event_id = BRAIN_BROADCAST_IDLE;
    bool should_send = false;

    if (!s_last_broadcast_valid ||
        ctx->state != s_last_broadcast_state ||
        ctx->pc != s_last_broadcast_pc) {
        should_send = true;
    }

    if (!should_send) {
        return;
    }

    switch (ctx->state) {
        case EXECUTOR_IDLE:
            event_id = BRAIN_BROADCAST_IDLE;
            break;
        case EXECUTOR_RUNNING:
            if (!s_last_broadcast_valid ||
                s_last_broadcast_state != EXECUTOR_RUNNING) {
                event_id = BRAIN_BROADCAST_RUNNING;
            } else {
                event_id = BRAIN_BROADCAST_STEP;
            }
            break;
        case EXECUTOR_WAIT_INPUT:
        case EXECUTOR_WAIT_DELAY:
            event_id = BRAIN_BROADCAST_RUNNING;
            break;
        case EXECUTOR_DONE:
            event_id = BRAIN_BROADCAST_DONE;
            break;
        case EXECUTOR_STOPPED:
            event_id = BRAIN_BROADCAST_STOP;
            break;
        default:
            event_id = BRAIN_BROADCAST_ERROR;
            break;
    }

    broadcast_runtime_event(event_id, ctx->pc);
    s_last_broadcast_state = ctx->state;
    s_last_broadcast_pc = ctx->pc;
    s_last_broadcast_valid = true;
}

static void dispatch_output_action(block_type_t step_type) {
    /*
     * Broadcast execution model:
     * - Output steps are broadcast to all blocks of the relevant output type.
     * - Some output types require a pre-exec config push (e.g., LED_FLASH color_id).
     */
    block_config_state_t config_snapshot;
    if (block_config_manager_get_state_snapshot(&config_snapshot) != ESP_OK ||
        config_snapshot.block_count == 0) {
        ESP_LOGW(TAG, "dispatch_output_action: no config available");
        return;
    }

    uint8_t block_present = 0;
    uint8_t exec_ok = 0;

    // NOTE: Prefer executing the child NOTE block's configured playback (CMD_EXECUTE),
    // so it always plays the exact sequence stored in the NOTE block UI (after SUBMIT).
    // If no NOTE blocks are present, fall back to the legacy behavior that plays
    // from Brain-side remembered parameters (CMD_PLAY_NOTE).
    if (step_type == BLOCK_TYPE_NOTE) {
        uint8_t executed_note_blocks = 0;
        for (int i = 0; i < config_snapshot.block_count; i++) {
            const block_config_entry_t *entry = &config_snapshot.blocks[i];
            if (!entry->present) {
                continue;
            }
            if (entry->block_type != BLOCK_TYPE_NOTE) {
                continue;
            }

            esp_err_t exec_ret = i2c_execute(entry->i2c_address);
            if (exec_ret == ESP_OK) {
                executed_note_blocks++;
            } else {
                ESP_LOGW(TAG, "NOTE CMD_EXECUTE failed addr=0x%02X (ret=%d)",
                         entry->i2c_address, (int)exec_ret);
            }
        }

        if (executed_note_blocks > 0) {
            ESP_LOGI(TAG, "NOTE executed on %u NOTE block(s)", (unsigned)executed_note_blocks);
            return;
        }

        // Legacy fallback: play tones based on Brain-side note parameters.
        uint8_t played = 0;
        static const uint32_t k_note_freq_hz[7] = {
            220U, /* A */
            247U, /* B (246.94) */
            262U, /* C (261.63) */
            294U, /* D (293.66) */
            330U, /* E (329.63) */
            349U, /* F (349.32) */
            392U, /* G */
        };

        uint8_t seq_len = s_executor_params.note_seq_len;
        if (seq_len > 15) {
            seq_len = 15;
        }

        if (seq_len == 0) {
            uint8_t note_id = s_executor_params.note_id;
            if (note_id >= 7) {
                note_id = 0;
            }

            for (int i = 0; i < config_snapshot.block_count; i++) {
                const block_config_entry_t *entry = &config_snapshot.blocks[i];
                if (!entry->present) {
                    continue;
                }
                // Any block type with a speaker should respond to CMD_PLAY_NOTE.
                if (entry->block_type != BLOCK_TYPE_NOTE &&
                    entry->block_type != BLOCK_TYPE_MUSIC_SEQ) {
                    continue;
                }
                esp_err_t play_ret = i2c_play_note(entry->i2c_address, note_id);
                if (play_ret != ESP_OK) {
                    ESP_LOGW(TAG, "NOTE play failed addr=0x%02X (ret=%d)",
                             entry->i2c_address, (int)play_ret);
                } else {
                    played++;
                }
            }

            // Also play on the Brain speaker (only for fallback mode).
            (void)speaker_play_tone(k_note_freq_hz[note_id], 400U);

            ESP_LOGI(TAG, "NOTE fallback broadcast played=%u note_id=%u",
                     (unsigned)played, (unsigned)note_id);
        } else {
            for (uint8_t s = 0; s < seq_len; s++) {
                uint8_t note_id = s_executor_params.note_seq[s];
                if (note_id >= 7) {
                    note_id = 0;
                }

                for (int i = 0; i < config_snapshot.block_count; i++) {
                    const block_config_entry_t *entry = &config_snapshot.blocks[i];
                    if (!entry->present) {
                        continue;
                    }
                    if (entry->block_type != BLOCK_TYPE_NOTE &&
                        entry->block_type != BLOCK_TYPE_MUSIC_SEQ) {
                        continue;
                    }
                    esp_err_t play_ret = i2c_play_note(entry->i2c_address, note_id);
                    if (play_ret != ESP_OK) {
                        ESP_LOGW(TAG, "NOTE play failed addr=0x%02X (ret=%d)",
                                 entry->i2c_address, (int)play_ret);
                    } else {
                        played++;
                    }
                }

                (void)speaker_play_tone(k_note_freq_hz[note_id], 400U);
            }

            ESP_LOGI(TAG, "NOTE fallback broadcast played=%u seq_len=%u",
                     (unsigned)played, (unsigned)seq_len);
        }

        return;
    }

    // Phase 1: push shared config updates (only needed for some step types).
    if (step_type == BLOCK_TYPE_LED_FLASH) {
        for (int i = 0; i < config_snapshot.block_count; i++) {
            const block_config_entry_t *entry = &config_snapshot.blocks[i];
            if (!entry->present) {
                continue;
            }
            if (entry->block_type != BLOCK_TYPE_LED_FLASH) {
                continue;
            }

            esp_err_t set_ret = i2c_set_led_color_id(entry->i2c_address, s_executor_params.color_id);
            if (set_ret != ESP_OK) {
                ESP_LOGW(TAG, "LED_FLASH set color failed addr=0x%02X (ret=%d)",
                         entry->i2c_address, (int)set_ret);
            }
        }
    }

    // Phase 2: broadcast execute to all present blocks.
    bool measured_music_latency = false;
    for (int i = 0; i < config_snapshot.block_count; i++) {
        const block_config_entry_t *entry = &config_snapshot.blocks[i];
        if (!entry->present) {
            continue;
        }

        block_present++;

        esp_err_t exec_ret = i2c_execute(entry->i2c_address);
        if (exec_ret == ESP_OK) {
            exec_ok++;
        } else {
            ESP_LOGW(TAG, "Broadcast EXECUTE failed addr=0x%02X type=%s (ret=%d)",
                     entry->i2c_address, block_type_to_string(entry->block_type), (int)exec_ret);
        }

        // Optional demo/metrics: only for MUSIC_SEQ steps, measure just the first block.
        if (!measured_music_latency &&
            step_type == BLOCK_TYPE_MUSIC_SEQ &&
            entry->block_type == BLOCK_TYPE_MUSIC_SEQ) {
            uint32_t elapsed_ms = 0;
            uint8_t status = 0;
            esp_err_t busy_ret = wait_for_status_busy(entry->i2c_address,
                                                      MUSIC_EXEC_BUSY_TIMEOUT_MS,
                                                      &elapsed_ms,
                                                      &status);
            if (busy_ret == ESP_OK) {
                ESP_LOGI(TAG,
                         "MUSIC_SEQ latency dispatch->BUSY = %u ms (addr=0x%02X status=0x%02X)",
                         (unsigned)elapsed_ms, entry->i2c_address, (unsigned)status);
            } else {
                ESP_LOGW(TAG,
                         "MUSIC_SEQ did not report BUSY within %u ms (addr=0x%02X ret=%d status=0x%02X)",
                         (unsigned)MUSIC_EXEC_BUSY_TIMEOUT_MS, entry->i2c_address,
                         (int)busy_ret, (unsigned)status);
            }
            measured_music_latency = true;
        }
    }

    ESP_LOGI(TAG, "BROADCAST action step=%s -> blocks=%u exec_ok=%u",
             block_type_to_string(step_type), (unsigned)block_present, (unsigned)exec_ok);
}

static bool load_program_from_config(void) {
    block_config_state_t config_snapshot;
    if (block_config_manager_get_state_snapshot(&config_snapshot) != ESP_OK ||
        config_snapshot.block_count == 0) {
        return false;
    }

    s_executor_ctx.program_len = config_snapshot.block_count;
    for (int i = 0; i < config_snapshot.block_count && i < BRAIN_EXECUTOR_MAX_PROGRAM_BLOCKS; i++) {
        s_executor_ctx.program[i] = config_snapshot.blocks[i].block_type;
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
            uint8_t payload[16];
            uint8_t payload_len;
        } block;
    } data;
} brain_event_t;

static QueueHandle_t s_event_queue = NULL;

static void brain_executor_task(void *arg) {
    (void)arg;
    const TickType_t tick_delay = pdMS_TO_TICKS(20);
    while (1) {
        brain_executor_tick();
        vTaskDelay(tick_delay);
    }
}

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
                     "START rejected: executor cannot start (err=%d, validation_gate=%s)",
                     (int)err,
                     brain_event_handler_can_start_execution() ? "pass" : "fail");
            return false;
        }
        ESP_LOGI(TAG, "Handled START: executor started");
        return true;
    }

    if (strcasecmp(message, "STOP") == 0) {
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
        block_type_t type = get_block_type_by_addr(block_addr);
        ESP_LOGI(TAG, "Block 0x%02X (%s) selection submit (len=%u)",
                 block_addr, block_type_to_string(type), (unsigned)payload_len);

        if (type == BLOCK_TYPE_LED_FLASH) {
            uint8_t selection = payload[0];
            esp_err_t set_ret = i2c_set_led_color_id(block_addr, selection);
            esp_err_t exec_ret = i2c_execute(block_addr);
            return (set_ret == ESP_OK) && (exec_ret == ESP_OK);
        }

        if (type == BLOCK_TYPE_NOTE) {
            if (payload_len == 1) {
                uint8_t note_id = payload[0];
                s_executor_params.note_seq_len = 0;
                s_executor_params.note_id = note_id;
                ESP_LOGI(TAG, "Executor note_id updated to %u", (unsigned)s_executor_params.note_id);
            } else {
                uint8_t count = payload[0];
                if (count > 15) {
                    count = 15;
                }
                if ((size_t)(1 + count) > payload_len) {
                    count = (payload_len > 1) ? (uint8_t)(payload_len - 1) : 0;
                    if (count > 15) {
                        count = 15;
                    }
                }

                s_executor_params.note_seq_len = count;
                for (uint8_t i = 0; i < count; i++) {
                    s_executor_params.note_seq[i] = (uint8_t)(payload[1 + i] % 7);
                }
                // Keep note_id in sync with the last note for any legacy paths/logs.
                if (count > 0) {
                    s_executor_params.note_id = s_executor_params.note_seq[count - 1];
                }
                ESP_LOGI(TAG, "Executor note sequence updated len=%u", (unsigned)count);
            }
            return true;
        }
    }

    if (event_id == BRAIN_BLOCK_EVENT_LOOP_COUNT_SUBMIT && payload && payload_len >= 1) {
        uint8_t pc = 0;
        if (!get_program_index_for_block_addr(block_addr, &pc)) {
            ESP_LOGW(TAG, "LOOP_COUNT submit from unknown addr=0x%02X", block_addr);
            return false;
        }
        uint16_t loop_count = (payload[0] == 0) ? 1 : payload[0];
        s_loop_count_by_pc[pc] = loop_count;
        s_loop_count_valid_by_pc[pc] = true;
        ESP_LOGI(TAG, "LOOP_COUNT submit: addr=0x%02X pc=%u loop_count=%u",
                 block_addr, (unsigned)pc, (unsigned)loop_count);
        return true;
    }

    if (event_id == BRAIN_BLOCK_EVENT_DELAY_MS_SUBMIT && payload && payload_len >= 4) {
        uint8_t pc = 0;
        if (!get_program_index_for_block_addr(block_addr, &pc)) {
            ESP_LOGW(TAG, "DELAY_MS submit from unknown addr=0x%02X", block_addr);
            return false;
        }
        uint32_t v = 0;
        v |= (uint32_t)payload[0];
        v |= ((uint32_t)payload[1] << 8);
        v |= ((uint32_t)payload[2] << 16);
        v |= ((uint32_t)payload[3] << 24);
        s_delay_ms_by_pc[pc] = v;
        s_delay_ms_valid_by_pc[pc] = true;
        ESP_LOGI(TAG, "DELAY_MS submit: addr=0x%02X pc=%u delay_ms=%lu",
                 block_addr, (unsigned)pc, (unsigned long)v);
        return true;
    }

    if (event_id == BRAIN_BLOCK_EVENT_BUTTON_PRESS) {
        // Any press sets the condition true; executor will clear after consuming.
        uint8_t pressed = 1;
        if (payload && payload_len >= 1) {
            pressed = payload[0];
        }
        if (pressed != 0) {
            brain_executor_set_button_state(true);
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
        } else {
            process_block_event(evt.data.block.block_addr,
                                evt.data.block.event_id,
                                evt.data.block.payload,
                                evt.data.block.payload_len);
        }
    }
}

void brain_event_handler_init(void) {
    if (s_event_queue) {
        ESP_LOGI(TAG, "brain_event_handler already initialized");
        return;
    }

    set_default_validation_state();
    brain_executor_reset_context(EXECUTOR_IDLE);
    clear_per_pc_params();
    memset(&s_executor_params, 0, sizeof(s_executor_params));
    s_executor_params.loop_count = 1;
    s_executor_params.delay_ms = 500;
    s_last_broadcast_state = EXECUTOR_IDLE;
    s_last_broadcast_pc = 0;
    s_last_broadcast_valid = false;
    broadcast_runtime_event(BRAIN_BROADCAST_IDLE, 0);
    ESP_LOGI(TAG, "brain_event_handler_init");

    s_event_queue = xQueueCreate(12, sizeof(brain_event_t));
    if (!s_event_queue) {
        ESP_LOGE(TAG, "Failed to create brain event queue");
        return;
    }

    // Keep Brain event orchestration on Core 0; GUI runs on Core 1.
    BaseType_t ok_evt = xTaskCreatePinnedToCore(brain_event_task, "brain_evt", 4096, NULL, 5, NULL, 0);
    if (ok_evt != pdPASS) {
        ESP_LOGE(TAG, "Failed to create brain event task");
        return;
    }

    // Tick-based executor task: periodically advances the program counter.
    BaseType_t ok_exec = xTaskCreatePinnedToCore(brain_executor_task, "brain_exec", 3072, NULL, 5, NULL, 0);
    if (ok_exec != pdPASS) {
        ESP_LOGE(TAG, "Failed to create brain executor task");
        return;
    }

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
    return s_validation_state.has_received_validation && s_validation_state.app_config_valid;
}

void brain_executor_set_params(const brain_executor_params_t *params) {
    if (params == NULL) {
        return;
    }
    s_executor_params = *params;
    if (s_executor_params.loop_count == 0) {
        s_executor_params.loop_count = 1;
    }
}

const brain_executor_context_t *brain_executor_get_context(void) {
    return &s_executor_ctx;
}

void brain_executor_set_button_state(bool is_pressed) {
    s_executor_ctx.button_pressed = is_pressed;
}

esp_err_t brain_executor_start(void) {
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

    s_executor_ctx.state = EXECUTOR_RUNNING;
    ESP_LOGI(TAG, "Executor started with %u blocks", s_executor_ctx.program_len);
    broadcast_state_snapshot();
    return ESP_OK;
}

void brain_executor_stop(void) {
    s_executor_ctx.stop_requested = true;
}

void brain_executor_tick(void) {
    if (s_executor_ctx.stop_requested) {
        brain_executor_reset_context(EXECUTOR_STOPPED);
        ESP_LOGI(TAG, "Executor stopped");
        broadcast_state_snapshot();
        return;
    }

    if (s_executor_ctx.state == EXECUTOR_WAIT_DELAY) {
        if (now_ms() >= s_executor_ctx.wait_until_ms) {
            s_executor_ctx.state = EXECUTOR_RUNNING;
        } else {
            return;
        }
    } else if (s_executor_ctx.state == EXECUTOR_WAIT_INPUT) {
        if (s_executor_ctx.button_pressed) {
            s_executor_ctx.state = EXECUTOR_RUNNING;
            // Auto-clear after a single press is consumed.
            s_executor_ctx.button_pressed = false;
        } else {
            return;
        }
    }

    if (s_executor_ctx.state != EXECUTOR_RUNNING) {
        broadcast_state_snapshot();
        return;
    }

    if (s_executor_ctx.pc >= s_executor_ctx.program_len) {
        s_executor_ctx.state = EXECUTOR_DONE;
        ESP_LOGI(TAG, "Executor done");
        broadcast_state_snapshot();
        return;
    }

    block_type_t current = s_executor_ctx.program[s_executor_ctx.pc];
    ESP_LOGD(TAG, "EXEC pc=%u type=%u", s_executor_ctx.pc, current);

    if (!s_last_broadcast_valid ||
        s_last_broadcast_state != EXECUTOR_RUNNING ||
        s_last_broadcast_pc != s_executor_ctx.pc) {
        broadcast_runtime_event(BRAIN_BROADCAST_STEP, s_executor_ctx.pc);
        s_last_broadcast_state = EXECUTOR_RUNNING;
        s_last_broadcast_pc = s_executor_ctx.pc;
        s_last_broadcast_valid = true;
    }

    if (is_output_block(current)) {
        dispatch_output_action(current);
        s_executor_ctx.pc++;
        return;
    }

    switch (current) {
        case BLOCK_TYPE_DELAY:
        {
            uint32_t delay_ms = s_executor_params.delay_ms;
            if (s_executor_ctx.pc < BRAIN_EXECUTOR_MAX_PROGRAM_BLOCKS &&
                s_delay_ms_valid_by_pc[s_executor_ctx.pc]) {
                delay_ms = s_delay_ms_by_pc[s_executor_ctx.pc];
            }
            s_executor_ctx.wait_until_ms = now_ms() + delay_ms;
            s_executor_ctx.state = EXECUTOR_WAIT_DELAY;
            s_executor_ctx.pc++;
            return;
        }

        case BLOCK_TYPE_IF: {
            int end_if_index = find_matching_end_index(s_executor_ctx.pc, BLOCK_TYPE_IF);
            if (end_if_index < 0) {
                ESP_LOGW(TAG, "IF without END_IF at pc=%u", s_executor_ctx.pc);
                broadcast_runtime_event(BRAIN_BROADCAST_ERROR, s_executor_ctx.pc);
                s_executor_ctx.state = EXECUTOR_DONE;
                broadcast_state_snapshot();
                return;
            }

            if (!s_executor_ctx.button_pressed) {
                // Condition false: skip the IF body.
                s_executor_ctx.pc = (uint8_t)(end_if_index + 1);
                return;
            }

            // Condition true: consume the press so IF gates on "any press" once.
            s_executor_ctx.button_pressed = false;
            int then_index = find_then_index(s_executor_ctx.pc, (uint8_t)end_if_index);
            s_executor_ctx.pc = (then_index >= 0) ? (uint8_t)(then_index + 1) : (uint8_t)(s_executor_ctx.pc + 1);
            return;
        }

        case BLOCK_TYPE_THEN:
        case BLOCK_TYPE_END_IF:
            s_executor_ctx.pc++;
            return;

        case BLOCK_TYPE_LOOP: {
            int end_loop_index = find_matching_end_index(s_executor_ctx.pc, BLOCK_TYPE_LOOP);
            if (end_loop_index < 0) {
                ESP_LOGW(TAG, "LOOP without END_LOOP at pc=%u", s_executor_ctx.pc);
                broadcast_runtime_event(BRAIN_BROADCAST_ERROR, s_executor_ctx.pc);
                s_executor_ctx.state = EXECUTOR_DONE;
                broadcast_state_snapshot();
                return;
            }

            uint16_t loop_count = s_executor_params.loop_count;
            if (s_executor_ctx.pc < BRAIN_EXECUTOR_MAX_PROGRAM_BLOCKS &&
                s_loop_count_valid_by_pc[s_executor_ctx.pc]) {
                loop_count = s_loop_count_by_pc[s_executor_ctx.pc];
            }

            if (loop_count == 0) {
                s_executor_ctx.pc = (uint8_t)(end_loop_index + 1);
                return;
            }

            if (s_executor_ctx.loop_depth >= BRAIN_EXECUTOR_MAX_LOOP_DEPTH) {
                ESP_LOGW(TAG, "Loop stack overflow");
                broadcast_runtime_event(BRAIN_BROADCAST_ERROR, s_executor_ctx.pc);
                s_executor_ctx.state = EXECUTOR_DONE;
                broadcast_state_snapshot();
                return;
            }

            brain_loop_frame_t *frame = &s_executor_ctx.loop_stack[s_executor_ctx.loop_depth++];
            frame->loop_start_pc = s_executor_ctx.pc;
            frame->loop_end_pc = (uint8_t)end_loop_index;
            frame->remaining_iterations = loop_count;
            s_executor_ctx.pc++;
            return;
        }

        case BLOCK_TYPE_END_LOOP:
            if (s_executor_ctx.loop_depth == 0) {
                s_executor_ctx.pc++;
                return;
            } else {
                brain_loop_frame_t *frame = &s_executor_ctx.loop_stack[s_executor_ctx.loop_depth - 1];
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

bool brain_event_handle_message(const char *message) {
    if (!message) {
        ESP_LOGW(TAG, "brain_event_handle_message called with NULL message");
        return false;
    }

    if (!s_event_queue) {
        ESP_LOGW(TAG, "brain_event_handle_message queue not initialized (msg=%s)", message);
        return false;
    }

    brain_event_t evt = {0};
    evt.src = EVT_SRC_MESSAGE;
    strncpy(evt.data.message, message, sizeof(evt.data.message) - 1);
    evt.data.message[sizeof(evt.data.message) - 1] = '\0';

    BaseType_t sent = xQueueSend(s_event_queue, &evt, 0);
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
    if (!s_event_queue || payload_len > 16) {
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

    return xQueueSend(s_event_queue, &evt, 0) == pdTRUE;
}
