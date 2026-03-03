#include "brain_event_handler.h"

#include <string.h>
#include <strings.h>
#include "esp_timer.h"
#include <stdlib.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "brain_block.h"

static const char *TAG = "brain_evt";
static brain_validation_state_t s_validation_state;
static block_event_map_t s_event_map;
static brain_executor_context_t s_executor_ctx;
static brain_executor_params_t s_executor_params;

static uint64_t now_ms(void) {
    return (uint64_t)(esp_timer_get_time() / 1000);
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

static void dispatch_output_action(block_type_t type) {
    // Placeholder: this is where the I2C broadcast to compatible blocks is called.
    switch (type) {
        case BLOCK_TYPE_LED_FLASH:
            ESP_LOGI(TAG, "ACTION led_flash color_id=%u", s_executor_params.color_id);
            break;
        case BLOCK_TYPE_NOTE:
            ESP_LOGI(TAG, "ACTION note note_id=%u", s_executor_params.note_id);
            break;
        case BLOCK_TYPE_MUSIC_SEQ:
            ESP_LOGI(TAG, "ACTION music_sequence sequence_id=%u", s_executor_params.music_sequence_id);
            break;
        default:
            break;
    }
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
        esp_err_t err = brain_executor_start();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "START rejected: executor cannot start (err=%d)", (int)err);
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
        uint8_t selection = payload[0];
        ESP_LOGI(TAG, "Block 0x%02X selection submit: %u", block_addr, selection);
        esp_err_t set_ret = i2c_set_led_color_id(block_addr, selection);
        esp_err_t exec_ret = i2c_execute(block_addr);
        return (set_ret == ESP_OK) && (exec_ret == ESP_OK);
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
    set_default_validation_state();
    memset(&s_event_map, 0, sizeof(s_event_map));
    brain_executor_reset_context(EXECUTOR_IDLE);
    memset(&s_executor_params, 0, sizeof(s_executor_params));
    s_executor_params.loop_count = 1;
    s_executor_params.delay_ms = 500;
    ESP_LOGI(TAG, "brain_event_handler_init");

    if (s_event_queue) {
        ESP_LOGI(TAG, "brain_event_handler already initialized");
        return;
    }

    s_event_queue = xQueueCreate(12, sizeof(brain_event_t));
    if (!s_event_queue) {
        ESP_LOGE(TAG, "Failed to create brain event queue");
        return;
    }

    // Keep Brain event orchestration on Core 0; GUI runs on Core 1.
    BaseType_t ok = xTaskCreatePinnedToCore(brain_event_task, "brain_evt", 4096, NULL, 5, NULL, 0);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create brain event task");
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

void brain_event_handler_refresh_config_event_map(const block_event_map_t *event_map) {
    if (event_map == NULL) {
        memset(&s_event_map, 0, sizeof(s_event_map));
        return;
    }

    memcpy(&s_event_map, event_map, sizeof(s_event_map));
    if (s_executor_ctx.state == EXECUTOR_RUNNING ||
        s_executor_ctx.state == EXECUTOR_WAIT_DELAY ||
        s_executor_ctx.state == EXECUTOR_WAIT_INPUT) {
        ESP_LOGW(TAG, "Config changed during execution; stopping executor");
        brain_executor_stop();
    }
    ESP_LOGI(TAG, "Config event map refreshed: seq=%u", s_event_map.sequence_count);
}

const block_event_map_t *brain_event_handler_get_config_event_map(void) {
    return &s_event_map;
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
        return ESP_ERR_INVALID_STATE;
    }

    brain_executor_reset_context(EXECUTOR_IDLE);
    if (!load_program_from_config()) {
        ESP_LOGW(TAG, "Cannot start executor: no program blocks");
        return ESP_ERR_INVALID_STATE;
    }

    s_executor_ctx.state = EXECUTOR_RUNNING;
    ESP_LOGI(TAG, "Executor started with %u blocks", s_executor_ctx.program_len);
    return ESP_OK;
}

void brain_executor_stop(void) {
    s_executor_ctx.stop_requested = true;
}

void brain_executor_tick(void) {
    if (s_executor_ctx.stop_requested) {
        brain_executor_reset_context(EXECUTOR_STOPPED);
        ESP_LOGI(TAG, "Executor stopped");
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
        } else {
            return;
        }
    }

    if (s_executor_ctx.state != EXECUTOR_RUNNING) {
        return;
    }

    if (s_executor_ctx.pc >= s_executor_ctx.program_len) {
        s_executor_ctx.state = EXECUTOR_DONE;
        ESP_LOGI(TAG, "Executor done");
        return;
    }

    block_type_t current = s_executor_ctx.program[s_executor_ctx.pc];
    ESP_LOGD(TAG, "EXEC pc=%u type=%u", s_executor_ctx.pc, current);

    if (is_output_block(current)) {
        dispatch_output_action(current);
        s_executor_ctx.pc++;
        return;
    }

    switch (current) {
        case BLOCK_TYPE_DELAY:
            s_executor_ctx.wait_until_ms = now_ms() + s_executor_params.delay_ms;
            s_executor_ctx.state = EXECUTOR_WAIT_DELAY;
            s_executor_ctx.pc++;
            return;

        case BLOCK_TYPE_IF: {
            int end_if_index = find_matching_end_index(s_executor_ctx.pc, BLOCK_TYPE_IF);
            if (end_if_index < 0) {
                ESP_LOGW(TAG, "IF without END_IF at pc=%u", s_executor_ctx.pc);
                s_executor_ctx.state = EXECUTOR_DONE;
                return;
            }

            if (!s_executor_ctx.button_pressed) {
                // Condition false: skip the IF body.
                s_executor_ctx.pc = (uint8_t)(end_if_index + 1);
                return;
            }

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
                s_executor_ctx.state = EXECUTOR_DONE;
                return;
            }

            if (s_executor_params.loop_count == 0) {
                s_executor_ctx.pc = (uint8_t)(end_loop_index + 1);
                return;
            }

            if (s_executor_ctx.loop_depth >= BRAIN_EXECUTOR_MAX_LOOP_DEPTH) {
                ESP_LOGW(TAG, "Loop stack overflow");
                s_executor_ctx.state = EXECUTOR_DONE;
                return;
            }

            brain_loop_frame_t *frame = &s_executor_ctx.loop_stack[s_executor_ctx.loop_depth++];
            frame->loop_start_pc = s_executor_ctx.pc;
            frame->loop_end_pc = (uint8_t)end_loop_index;
            frame->remaining_iterations = s_executor_params.loop_count;
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
    if (!s_event_queue || !message) {
        return false;
    }

    brain_event_t evt = {0};
    evt.src = EVT_SRC_MESSAGE;
    strncpy(evt.data.message, message, sizeof(evt.data.message) - 1);
    evt.data.message[sizeof(evt.data.message) - 1] = '\0';

    return xQueueSend(s_event_queue, &evt, 0) == pdTRUE;
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
