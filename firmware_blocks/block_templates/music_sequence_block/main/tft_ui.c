#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "sdkconfig.h"

#include "driver/gpio.h"

#include "lvgl.h"
#include "lvgl_helpers.h"

#include "tft_ui.h"
#include "../speaker.h"

#define TAG "MUSIC_SEQ_UI"
#define LCD_BACKLIGHT 32
#define LV_TICK_PERIOD_MS 1

#define UI_TASK_STACK_SIZE        6144
#define PREVIEW_TASK_STACK_SIZE   4096
#define UI_TASK_PRIORITY          5
#define PREVIEW_TASK_PRIORITY     4

#define UI_ACTION_QUEUE_LEN       8
#define UI_PREVIEW_QUEUE_LEN      1   // xQueueOverwrite is used

#define PREVIEW_GAP_MS_DEFAULT    35U
#define COMPOSE_STEP_MS_DEFAULT   250U

#if CONFIG_FREERTOS_UNICORE
#define MUSIC_UI_CORE_ID          0
#else
#define MUSIC_UI_CORE_ID          1
#endif

#define MUSIC_EXEC_CORE_ID        0

typedef enum {
    PREVIEW_JOB_PRESET = 0,
    PREVIEW_JOB_COMPOSE,
} preview_job_type_t;

typedef struct {
    preview_job_type_t type;
    music_seq_config_t config_snapshot;
} preview_job_t;

// ---------------------------------------------------------------------------
// Shared state (UI core + execution core)
// ---------------------------------------------------------------------------
static SemaphoreHandle_t s_state_mutex = NULL;
static QueueHandle_t s_action_queue = NULL;
static QueueHandle_t s_preview_queue = NULL;

static bool s_ui_started = false;

static music_seq_config_t s_config;
static music_playback_state_t s_playback_state;
static char s_status_text[96];

// ---------------------------------------------------------------------------
// LVGL objects (GUI task only)
// ---------------------------------------------------------------------------
static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_mode_btn_label = NULL;

static lv_obj_t *s_preset_panel = NULL;
static lv_obj_t *s_preset_name_label = NULL;
static lv_obj_t *s_preset_hint_label = NULL;

static lv_obj_t *s_compose_panel = NULL;
static lv_obj_t *s_slot_label = NULL;
static lv_obj_t *s_note_label = NULL;
static lv_obj_t *s_compose_hint_label = NULL;

static lv_obj_t *s_tempo_label = NULL;
static lv_obj_t *s_status_label = NULL;
static lv_obj_t *s_exec_label = NULL;

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------
static void gui_task(void *arg);
static void preview_task(void *arg);
static void gui_refresh_from_state(void);
static void push_action_event(music_ui_action_type_t type);
static void queue_preview_request(preview_job_type_t type);

// ---------------------------------------------------------------------------
// Utility helpers
// ---------------------------------------------------------------------------
static void lv_tick_task(void *arg)
{
    (void)arg;
    lv_tick_inc(LV_TICK_PERIOD_MS);
}

static void copy_text_safe(char *dst, size_t dst_len, const char *src)
{
    if (dst_len == 0) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    snprintf(dst, dst_len, "%s", src);
}

static uint8_t clamp_tempo_pct(uint8_t tempo_pct)
{
    if (tempo_pct < 50) {
        return 50;
    }
    if (tempo_pct > 200) {
        return 200;
    }
    return tempo_pct;
}

static uint8_t current_sequence_id_from_config(const music_seq_config_t *cfg)
{
    if (cfg->mode == MUSIC_UI_MODE_COMPOSE) {
        return MUSIC_PRESET_CUSTOM_1;
    }
    return cfg->selected_preset_id;
}

static size_t preset_count(void)
{
    return speaker_get_preset_count();
}

static size_t preset_index_for_id(uint8_t preset_id)
{
    size_t count = preset_count();
    for (size_t i = 0; i < count; i++) {
        const music_preset_t *preset = speaker_get_preset_by_index(i);
        if (preset && preset->preset_id == preset_id) {
            return i;
        }
    }
    return 0;
}

static void config_defaults_locked(void)
{
    memset(&s_config, 0, sizeof(s_config));
    memset(&s_playback_state, 0, sizeof(s_playback_state));

    s_config.mode = MUSIC_UI_MODE_PRESET;
    s_config.tempo_pct = 100;
    s_config.loop_count = 1;
    s_config.compose_step_duration_ms = COMPOSE_STEP_MS_DEFAULT;
    s_config.compose_step_count = MUSIC_COMPOSE_MAX_STEPS;
    s_config.compose_cursor_index = 0;
    s_config.config_valid = false;

    for (uint8_t i = 0; i < MUSIC_COMPOSE_MAX_STEPS; i++) {
        s_config.compose_notes[i] = NOTE_REST;
    }
    // Friendly starter pattern for compose mode.
    if (MUSIC_COMPOSE_MAX_STEPS >= 4) {
        s_config.compose_notes[0] = NOTE_C4;
        s_config.compose_notes[1] = NOTE_D4;
        s_config.compose_notes[2] = NOTE_E4;
        s_config.compose_notes[3] = NOTE_G4;
    }

    if (preset_count() > 0) {
        const music_preset_t *preset = speaker_get_preset_by_index(0);
        s_config.selected_preset_id = preset ? preset->preset_id : 0;
    } else {
        s_config.selected_preset_id = 0;
    }

    copy_text_safe(s_status_text, sizeof(s_status_text),
                   "Use arrows to pick a song. Preview, then Select.");
}

static void push_action_event(music_ui_action_type_t type)
{
    if (s_action_queue == NULL || type == MUSIC_UI_ACTION_NONE) {
        return;
    }

    music_ui_action_t action = {
        .type = type,
        .mode = (uint8_t)s_config.mode,
        .sequence_id = current_sequence_id_from_config(&s_config),
        .tempo_pct = s_config.tempo_pct,
        .config_valid = s_config.config_valid ? 1U : 0U,
    };

    (void)xQueueSend(s_action_queue, &action, 0);
}

static void set_status_locked(const char *msg)
{
    copy_text_safe(s_status_text, sizeof(s_status_text), msg);
}

static void cycle_preset_locked(int delta)
{
    size_t count = preset_count();
    if (count == 0) {
        set_status_locked("No presets found.");
        return;
    }

    size_t cur = preset_index_for_id(s_config.selected_preset_id);
    size_t next = cur;
    if (delta > 0) {
        next = (cur + 1U) % count;
    } else if (delta < 0) {
        next = (cur == 0U) ? (count - 1U) : (cur - 1U);
    }

    const music_preset_t *preset = speaker_get_preset_by_index(next);
    if (preset) {
        s_config.selected_preset_id = preset->preset_id;
        s_config.config_valid = false;  // require re-select to commit new preset
        set_status_locked("Preset changed. Tap Preview or Select.");
    }
}

static void set_mode_locked(music_ui_mode_t mode)
{
    if (s_config.mode == mode) {
        return;
    }
    s_config.mode = mode;
    s_config.config_valid = false;  // mode change requires re-save/re-select
    if (mode == MUSIC_UI_MODE_PRESET) {
        set_status_locked("Preset Mode: choose a song with arrows.");
    } else {
        set_status_locked("Compose Mode: edit notes, then Save.");
    }
}

static void adjust_compose_cursor_locked(int delta)
{
    if (s_config.compose_step_count == 0) {
        s_config.compose_step_count = MUSIC_COMPOSE_MAX_STEPS;
    }
    uint8_t count = s_config.compose_step_count;
    uint8_t cur = s_config.compose_cursor_index;
    if (cur >= count) {
        cur = 0;
    }

    if (delta > 0) {
        cur = (uint8_t)((cur + 1U) % count);
    } else if (delta < 0) {
        cur = (cur == 0U) ? (uint8_t)(count - 1U) : (uint8_t)(cur - 1U);
    }
    s_config.compose_cursor_index = cur;
    set_status_locked("Compose Mode: choose a slot, then change the note.");
}

static void adjust_compose_note_locked(int delta)
{
    uint8_t idx = s_config.compose_cursor_index;
    if (idx >= MUSIC_COMPOSE_MAX_STEPS) {
        idx = 0;
        s_config.compose_cursor_index = 0;
    }

    int cur = (int)s_config.compose_notes[idx];
    if (cur < 0 || cur >= (int)NOTE_COUNT) {
        cur = NOTE_REST;
    }

    cur += (delta > 0) ? 1 : -1;
    if (cur >= (int)NOTE_COUNT) {
        cur = NOTE_REST;
    } else if (cur < (int)NOTE_REST) {
        cur = (int)NOTE_COUNT - 1;
    }

    s_config.compose_notes[idx] = (note_id_t)cur;
    s_config.config_valid = false;  // compose changed, must save
    set_status_locked("Compose changed. Tap Preview or Select/Save.");
}

static void adjust_tempo_locked(int delta_pct)
{
    int tempo = (int)s_config.tempo_pct + delta_pct;
    if (tempo < 50) {
        tempo = 50;
    } else if (tempo > 200) {
        tempo = 200;
    }
    s_config.tempo_pct = (uint8_t)tempo;
    set_status_locked("Tempo updated.");
}

static void queue_preview_request(preview_job_type_t type)
{
    if (s_preview_queue == NULL) {
        return;
    }

    preview_job_t job;
    memset(&job, 0, sizeof(job));
    job.type = type;

    if (xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        job.config_snapshot = s_config;
        if (s_playback_state.is_playing) {
            set_status_locked("Block is executing. Preview is disabled right now.");
            xSemaphoreGive(s_state_mutex);
            return;
        }
        if (type == PREVIEW_JOB_PRESET) {
            set_status_locked("Previewing preset...");
        } else {
            set_status_locked("Previewing custom sequence...");
        }
        xSemaphoreGive(s_state_mutex);
    } else {
        return;
    }

    (void)xQueueOverwrite(s_preview_queue, &job);
}

static void build_preset_hint_text(const music_preset_t *preset, char *out, size_t out_len)
{
    if (out_len == 0) {
        return;
    }
    if (preset == NULL || preset->steps == NULL || preset->step_count == 0) {
        snprintf(out, out_len, "No preset notes available.");
        return;
    }

    size_t n = (preset->step_count < 8U) ? preset->step_count : 8U;
    int written = snprintf(out, out_len, "♪ ");
    if (written < 0) {
        out[0] = '\0';
        return;
    }

    size_t used = (size_t)written;
    for (size_t i = 0; i < n && used < out_len; i++) {
        const char *lbl = "??";
        note_id_t note = preset->steps[i].note;
        if ((uint32_t)note < NOTE_COUNT && note_labels[note] != NULL) {
            lbl = note_labels[note];
        }
        written = snprintf(out + used, out_len - used, "%s%s", (i == 0) ? "" : " ", lbl);
        if (written < 0) {
            break;
        }
        used += (size_t)written;
    }
}

static void build_compose_hint_text(const music_seq_config_t *cfg, char *out, size_t out_len)
{
    if (out_len == 0) {
        return;
    }

    size_t used = 0;
    int written = snprintf(out, out_len, "♪ ");
    if (written < 0) {
        out[0] = '\0';
        return;
    }
    used = (size_t)written;

    uint8_t count = cfg->compose_step_count;
    if (count == 0 || count > MUSIC_COMPOSE_MAX_STEPS) {
        count = MUSIC_COMPOSE_MAX_STEPS;
    }

    for (uint8_t i = 0; i < count && used < out_len; i++) {
        note_id_t note = cfg->compose_notes[i];
        const char *lbl = ((uint32_t)note < NOTE_COUNT && note_labels[note]) ? note_labels[note] : "??";
        bool selected = (i == cfg->compose_cursor_index);
        written = snprintf(out + used, out_len - used, "%s%s%s%s",
                           (i == 0) ? "" : " ",
                           selected ? "[" : "",
                           lbl,
                           selected ? "]" : "");
        if (written < 0) {
            break;
        }
        used += (size_t)written;
    }
}

// ---------------------------------------------------------------------------
// LVGL UI creation helpers (GUI task only)
// ---------------------------------------------------------------------------
static lv_obj_t *create_text_button(lv_obj_t *parent,
                                    const char *text,
                                    lv_coord_t x,
                                    lv_coord_t y,
                                    lv_coord_t w,
                                    lv_coord_t h,
                                    lv_event_cb_t cb,
                                    lv_color_t bg)
{
    lv_obj_t *btn = lv_btn_create(parent, NULL);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_event_cb(btn, cb);
    lv_obj_set_style_local_radius(btn, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, 10);
    lv_obj_set_style_local_border_width(btn, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, 2);
    lv_obj_set_style_local_border_color(btn, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE);
    lv_obj_set_style_local_bg_color(btn, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, bg);
    lv_obj_set_style_local_bg_opa(btn, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_COVER);

    lv_obj_t *label = lv_label_create(btn, NULL);
    lv_label_set_text(label, text);
    lv_obj_align(label, NULL, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_local_text_color(btn, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_BLACK);
    return btn;
}

static void preset_left_btn_cb(lv_obj_t *obj, lv_event_t event);
static void preset_right_btn_cb(lv_obj_t *obj, lv_event_t event);
static void preview_btn_cb(lv_obj_t *obj, lv_event_t event);
static void commit_btn_cb(lv_obj_t *obj, lv_event_t event);
static void mode_btn_cb(lv_obj_t *obj, lv_event_t event);
static void tempo_minus_btn_cb(lv_obj_t *obj, lv_event_t event);
static void tempo_plus_btn_cb(lv_obj_t *obj, lv_event_t event);
static void slot_left_btn_cb(lv_obj_t *obj, lv_event_t event);
static void slot_right_btn_cb(lv_obj_t *obj, lv_event_t event);
static void note_down_btn_cb(lv_obj_t *obj, lv_event_t event);
static void note_up_btn_cb(lv_obj_t *obj, lv_event_t event);

static lv_obj_t *create_music_screen(void)
{
    lv_obj_t *scr = lv_obj_create(NULL, NULL);
    lv_obj_set_style_local_bg_color(scr, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(0x12203A));
    lv_obj_set_style_local_bg_opa(scr, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_COVER);

    lv_obj_t *title_label = lv_label_create(scr, NULL);
    lv_label_set_text(title_label, "Music Sequence");
    lv_obj_set_style_local_text_color(title_label, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(0xFDE68A));
    lv_obj_align(title_label, NULL, LV_ALIGN_IN_TOP_LEFT, 10, 8);

    lv_obj_t *mode_btn = create_text_button(scr, "Preset", 150, 4, 80, 30, mode_btn_cb, lv_color_hex(0x9AE6B4));
    s_mode_btn_label = lv_obj_get_child(mode_btn, NULL);

    lv_obj_t *subtitle = lv_label_create(scr, NULL);
    lv_label_set_text(subtitle, "Core1: UI   Core0: I2C + Execute");
    lv_obj_set_style_local_text_color(subtitle, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(0xBBD0FF));
    lv_obj_align(subtitle, NULL, LV_ALIGN_IN_TOP_LEFT, 10, 30);

    // Preset panel
    s_preset_panel = lv_cont_create(scr, NULL);
    lv_obj_set_pos(s_preset_panel, 8, 54);
    lv_obj_set_size(s_preset_panel, 224, 92);
    lv_obj_set_style_local_bg_color(s_preset_panel, LV_CONT_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(0x243B63));
    lv_obj_set_style_local_bg_opa(s_preset_panel, LV_CONT_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_COVER);
    lv_obj_set_style_local_border_width(s_preset_panel, LV_CONT_PART_MAIN, LV_STATE_DEFAULT, 2);
    lv_obj_set_style_local_border_color(s_preset_panel, LV_CONT_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(0x88A8FF));
    lv_obj_set_style_local_radius(s_preset_panel, LV_CONT_PART_MAIN, LV_STATE_DEFAULT, 10);

    create_text_button(s_preset_panel, "<", 6, 8, 34, 34, preset_left_btn_cb, lv_color_hex(0xA5D8FF));
    create_text_button(s_preset_panel, ">", 184, 8, 34, 34, preset_right_btn_cb, lv_color_hex(0xA5D8FF));

    s_preset_name_label = lv_label_create(s_preset_panel, NULL);
    lv_label_set_text(s_preset_name_label, "Twinkle");
    lv_obj_set_style_local_text_color(s_preset_name_label, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE);
    lv_obj_align(s_preset_name_label, NULL, LV_ALIGN_IN_TOP_MID, 0, 14);

    s_preset_hint_label = lv_label_create(s_preset_panel, NULL);
    lv_obj_set_width(s_preset_hint_label, 204);
    lv_label_set_long_mode(s_preset_hint_label, LV_LABEL_LONG_BREAK);
    lv_label_set_align(s_preset_hint_label, LV_LABEL_ALIGN_CENTER);
    lv_label_set_text(s_preset_hint_label, "♪ C4 C4 G4 G4 A4 A4 G4");
    lv_obj_set_style_local_text_color(s_preset_hint_label, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(0xE0E7FF));
    lv_obj_align(s_preset_hint_label, NULL, LV_ALIGN_IN_BOTTOM_MID, 0, -8);

    // Compose panel
    s_compose_panel = lv_cont_create(scr, NULL);
    lv_obj_set_pos(s_compose_panel, 8, 150);
    lv_obj_set_size(s_compose_panel, 224, 94);
    lv_obj_set_style_local_bg_color(s_compose_panel, LV_CONT_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(0x2D254D));
    lv_obj_set_style_local_bg_opa(s_compose_panel, LV_CONT_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_COVER);
    lv_obj_set_style_local_border_width(s_compose_panel, LV_CONT_PART_MAIN, LV_STATE_DEFAULT, 2);
    lv_obj_set_style_local_border_color(s_compose_panel, LV_CONT_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(0xB28DFF));
    lv_obj_set_style_local_radius(s_compose_panel, LV_CONT_PART_MAIN, LV_STATE_DEFAULT, 10);

    s_slot_label = lv_label_create(s_compose_panel, NULL);
    lv_label_set_text(s_slot_label, "Slot 1/8");
    lv_obj_set_style_local_text_color(s_slot_label, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE);
    lv_obj_align(s_slot_label, NULL, LV_ALIGN_IN_TOP_LEFT, 8, 8);

    s_note_label = lv_label_create(s_compose_panel, NULL);
    lv_label_set_text(s_note_label, "C4");
    lv_obj_set_style_local_text_color(s_note_label, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(0xFDE68A));
    lv_obj_align(s_note_label, NULL, LV_ALIGN_IN_TOP_RIGHT, -10, 8);

    create_text_button(s_compose_panel, "< Slot", 8, 30, 64, 28, slot_left_btn_cb, lv_color_hex(0xF5C2E7));
    create_text_button(s_compose_panel, "Slot >", 80, 30, 64, 28, slot_right_btn_cb, lv_color_hex(0xF5C2E7));
    create_text_button(s_compose_panel, "Note -", 152, 30, 64, 28, note_down_btn_cb, lv_color_hex(0xF9E2AF));
    create_text_button(s_compose_panel, "Note +", 152, 62, 64, 28, note_up_btn_cb, lv_color_hex(0xA6E3A1));

    s_compose_hint_label = lv_label_create(s_compose_panel, NULL);
    lv_obj_set_width(s_compose_hint_label, 140);
    lv_label_set_long_mode(s_compose_hint_label, LV_LABEL_LONG_BREAK);
    lv_label_set_text(s_compose_hint_label, "♪ [C4] D4 E4 G4 REST REST REST REST");
    lv_obj_set_style_local_text_color(s_compose_hint_label, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(0xF5E0DC));
    lv_obj_align(s_compose_hint_label, NULL, LV_ALIGN_IN_BOTTOM_LEFT, 8, -2);

    // Shared controls (preview/select/tempo) near bottom
    create_text_button(scr, "Preview", 8, 248, 72, 32, preview_btn_cb, lv_color_hex(0x93C5FD));
    create_text_button(scr, "Select/Save", 84, 248, 92, 32, commit_btn_cb, lv_color_hex(0x86EFAC));
    create_text_button(scr, "-", 180, 248, 24, 32, tempo_minus_btn_cb, lv_color_hex(0xFCA5A5));
    create_text_button(scr, "+", 208, 248, 24, 32, tempo_plus_btn_cb, lv_color_hex(0xFDE68A));

    s_tempo_label = lv_label_create(scr, NULL);
    lv_label_set_text(s_tempo_label, "Tempo 100%");
    lv_obj_set_style_local_text_color(s_tempo_label, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(0xE2E8F0));
    lv_obj_align(s_tempo_label, NULL, LV_ALIGN_IN_LEFT_MID, 8, 68);

    s_exec_label = lv_label_create(scr, NULL);
    lv_obj_set_width(s_exec_label, 224);
    lv_label_set_long_mode(s_exec_label, LV_LABEL_LONG_BREAK);
    lv_label_set_text(s_exec_label, "Exec: Idle");
    lv_obj_set_style_local_text_color(s_exec_label, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(0xBFDBFE));
    lv_obj_align(s_exec_label, NULL, LV_ALIGN_IN_BOTTOM_LEFT, 8, -34);

    s_status_label = lv_label_create(scr, NULL);
    lv_obj_set_width(s_status_label, 224);
    lv_label_set_long_mode(s_status_label, LV_LABEL_LONG_BREAK);
    lv_label_set_text(s_status_label, "Use arrows to pick a song.");
    lv_obj_set_style_local_text_color(s_status_label, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(0xFDE68A));
    lv_obj_align(s_status_label, NULL, LV_ALIGN_IN_BOTTOM_LEFT, 8, -10);

    return scr;
}

// ---------------------------------------------------------------------------
// UI state rendering (GUI task only)
// ---------------------------------------------------------------------------
static void gui_refresh_from_state(void)
{
    if (s_screen == NULL) {
        return;
    }

    music_seq_config_t cfg;
    music_playback_state_t playback;
    char status_text[sizeof(s_status_text)];
    memset(&cfg, 0, sizeof(cfg));
    memset(&playback, 0, sizeof(playback));
    status_text[0] = '\0';

    if (xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
        cfg = s_config;
        playback = s_playback_state;
        copy_text_safe(status_text, sizeof(status_text), s_status_text);
        xSemaphoreGive(s_state_mutex);
    }

    // Mode button label + panel visibility
    if (s_mode_btn_label) {
        lv_label_set_text(s_mode_btn_label,
                          (cfg.mode == MUSIC_UI_MODE_PRESET) ? "Preset" : "Compose");
    }
    if (s_preset_panel) {
        lv_obj_set_hidden(s_preset_panel, cfg.mode != MUSIC_UI_MODE_PRESET);
    }
    if (s_compose_panel) {
        lv_obj_set_hidden(s_compose_panel, cfg.mode != MUSIC_UI_MODE_COMPOSE);
    }

    // Preset labels
    if (cfg.mode == MUSIC_UI_MODE_PRESET) {
        const music_preset_t *preset = speaker_get_preset_by_id(cfg.selected_preset_id);
        if (s_preset_name_label) {
            lv_label_set_text(s_preset_name_label, preset ? preset->name : "No Preset");
        }
        if (s_preset_hint_label) {
            char hint[96];
            build_preset_hint_text(preset, hint, sizeof(hint));
            lv_label_set_text(s_preset_hint_label, hint);
        }
    }

    // Compose labels
    if (cfg.mode == MUSIC_UI_MODE_COMPOSE) {
        uint8_t count = cfg.compose_step_count;
        if (count == 0 || count > MUSIC_COMPOSE_MAX_STEPS) {
            count = MUSIC_COMPOSE_MAX_STEPS;
        }
        uint8_t idx = cfg.compose_cursor_index;
        if (idx >= count) {
            idx = 0;
        }

        if (s_slot_label) {
            char slot_text[24];
            snprintf(slot_text, sizeof(slot_text), "Slot %u/%u", (unsigned)(idx + 1U), (unsigned)count);
            lv_label_set_text(s_slot_label, slot_text);
        }
        if (s_note_label) {
            note_id_t note = cfg.compose_notes[idx];
            const char *lbl = ((uint32_t)note < NOTE_COUNT && note_labels[note]) ? note_labels[note] : "??";
            lv_label_set_text(s_note_label, lbl);
        }
        if (s_compose_hint_label) {
            char hint[128];
            build_compose_hint_text(&cfg, hint, sizeof(hint));
            lv_label_set_text(s_compose_hint_label, hint);
        }
    }

    // Tempo label
    if (s_tempo_label) {
        char tempo_text[32];
        snprintf(tempo_text, sizeof(tempo_text), "Tempo %u%%", (unsigned)clamp_tempo_pct(cfg.tempo_pct));
        lv_label_set_text(s_tempo_label, tempo_text);
    }

    // Execution status label (set by core0 execution task)
    if (s_exec_label) {
        char exec_text[96];
        if (playback.is_playing) {
            const music_preset_t *p = speaker_get_preset_by_id(playback.active_preset_id);
            snprintf(exec_text, sizeof(exec_text),
                     "Exec: Playing %s step %u",
                     p ? p->name : "Sequence",
                     (unsigned)(playback.step_index + 1U));
        } else {
            snprintf(exec_text, sizeof(exec_text), "Exec: Idle (pin I2C/exec tasks to core %d)", MUSIC_EXEC_CORE_ID);
        }
        lv_label_set_text(s_exec_label, exec_text);
    }

    if (s_status_label) {
        lv_label_set_text(s_status_label, status_text);
    }
}

// ---------------------------------------------------------------------------
// LVGL callbacks (GUI task context only)
// ---------------------------------------------------------------------------
static void preset_left_btn_cb(lv_obj_t *obj, lv_event_t event)
{
    (void)obj;
    if (event != LV_EVENT_CLICKED) {
        return;
    }

    if (xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        set_mode_locked(MUSIC_UI_MODE_PRESET);
        cycle_preset_locked(-1);
        push_action_event(MUSIC_UI_ACTION_CONFIG_UPDATED);
        xSemaphoreGive(s_state_mutex);
    }
}

static void preset_right_btn_cb(lv_obj_t *obj, lv_event_t event)
{
    (void)obj;
    if (event != LV_EVENT_CLICKED) {
        return;
    }

    if (xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        set_mode_locked(MUSIC_UI_MODE_PRESET);
        cycle_preset_locked(+1);
        push_action_event(MUSIC_UI_ACTION_CONFIG_UPDATED);
        xSemaphoreGive(s_state_mutex);
    }
}

static void preview_btn_cb(lv_obj_t *obj, lv_event_t event)
{
    (void)obj;
    if (event != LV_EVENT_CLICKED) {
        return;
    }

    music_ui_mode_t mode = MUSIC_UI_MODE_PRESET;
    if (xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        mode = s_config.mode;
        xSemaphoreGive(s_state_mutex);
    }

    queue_preview_request((mode == MUSIC_UI_MODE_COMPOSE) ? PREVIEW_JOB_COMPOSE : PREVIEW_JOB_PRESET);
}

static void commit_btn_cb(lv_obj_t *obj, lv_event_t event)
{
    (void)obj;
    if (event != LV_EVENT_CLICKED) {
        return;
    }

    if (xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        s_config.config_valid = true;
        if (s_config.mode == MUSIC_UI_MODE_PRESET) {
            set_status_locked("Preset selected. Brain can execute this sequence.");
        } else {
            set_status_locked("Custom sequence saved. Brain can execute Custom 1.");
        }
        push_action_event(MUSIC_UI_ACTION_CONFIG_COMMITTED);
        xSemaphoreGive(s_state_mutex);
    }
}

static void mode_btn_cb(lv_obj_t *obj, lv_event_t event)
{
    (void)obj;
    if (event != LV_EVENT_CLICKED) {
        return;
    }

    if (xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        music_ui_mode_t next = (s_config.mode == MUSIC_UI_MODE_PRESET)
                                 ? MUSIC_UI_MODE_COMPOSE
                                 : MUSIC_UI_MODE_PRESET;
        set_mode_locked(next);
        push_action_event(MUSIC_UI_ACTION_CONFIG_UPDATED);
        xSemaphoreGive(s_state_mutex);
    }
}

static void tempo_minus_btn_cb(lv_obj_t *obj, lv_event_t event)
{
    (void)obj;
    if (event != LV_EVENT_CLICKED) {
        return;
    }
    if (xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        adjust_tempo_locked(-10);
        push_action_event(MUSIC_UI_ACTION_CONFIG_UPDATED);
        xSemaphoreGive(s_state_mutex);
    }
}

static void tempo_plus_btn_cb(lv_obj_t *obj, lv_event_t event)
{
    (void)obj;
    if (event != LV_EVENT_CLICKED) {
        return;
    }
    if (xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        adjust_tempo_locked(+10);
        push_action_event(MUSIC_UI_ACTION_CONFIG_UPDATED);
        xSemaphoreGive(s_state_mutex);
    }
}

static void slot_left_btn_cb(lv_obj_t *obj, lv_event_t event)
{
    (void)obj;
    if (event != LV_EVENT_CLICKED) {
        return;
    }
    if (xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        set_mode_locked(MUSIC_UI_MODE_COMPOSE);
        adjust_compose_cursor_locked(-1);
        push_action_event(MUSIC_UI_ACTION_CONFIG_UPDATED);
        xSemaphoreGive(s_state_mutex);
    }
}

static void slot_right_btn_cb(lv_obj_t *obj, lv_event_t event)
{
    (void)obj;
    if (event != LV_EVENT_CLICKED) {
        return;
    }
    if (xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        set_mode_locked(MUSIC_UI_MODE_COMPOSE);
        adjust_compose_cursor_locked(+1);
        push_action_event(MUSIC_UI_ACTION_CONFIG_UPDATED);
        xSemaphoreGive(s_state_mutex);
    }
}

static void note_down_btn_cb(lv_obj_t *obj, lv_event_t event)
{
    (void)obj;
    if (event != LV_EVENT_CLICKED) {
        return;
    }
    if (xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        set_mode_locked(MUSIC_UI_MODE_COMPOSE);
        adjust_compose_note_locked(-1);
        push_action_event(MUSIC_UI_ACTION_CONFIG_UPDATED);
        xSemaphoreGive(s_state_mutex);
    }
}

static void note_up_btn_cb(lv_obj_t *obj, lv_event_t event)
{
    (void)obj;
    if (event != LV_EVENT_CLICKED) {
        return;
    }
    if (xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        set_mode_locked(MUSIC_UI_MODE_COMPOSE);
        adjust_compose_note_locked(+1);
        push_action_event(MUSIC_UI_ACTION_CONFIG_UPDATED);
        xSemaphoreGive(s_state_mutex);
    }
}

// ---------------------------------------------------------------------------
// Preview worker (pinned to UI core, separate from LVGL task)
// ---------------------------------------------------------------------------
static void preview_task(void *arg)
{
    (void)arg;
    preview_job_t job;

    while (1) {
        if (xQueueReceive(s_preview_queue, &job, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        esp_err_t err = ESP_OK;
        if (job.type == PREVIEW_JOB_PRESET) {
            err = speaker_play_preset(job.config_snapshot.selected_preset_id, job.config_snapshot.tempo_pct);
        } else {
            music_step_t steps[MUSIC_COMPOSE_MAX_STEPS];
            uint8_t count = job.config_snapshot.compose_step_count;
            if (count == 0 || count > MUSIC_COMPOSE_MAX_STEPS) {
                count = MUSIC_COMPOSE_MAX_STEPS;
            }
            for (uint8_t i = 0; i < count; i++) {
                steps[i].note = job.config_snapshot.compose_notes[i];
                steps[i].duration_ms = (job.config_snapshot.compose_step_duration_ms == 0U)
                                         ? COMPOSE_STEP_MS_DEFAULT
                                         : job.config_snapshot.compose_step_duration_ms;
                steps[i].gap_ms = PREVIEW_GAP_MS_DEFAULT;
            }
            err = speaker_play_sequence(steps, count, job.config_snapshot.tempo_pct);
        }

        if (xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
            if (err == ESP_OK) {
                set_status_locked("Preview complete.");
            } else {
                set_status_locked("Preview failed. Check speaker init/pin config.");
            }
            xSemaphoreGive(s_state_mutex);
        }
    }
}

// ---------------------------------------------------------------------------
// GUI task (LVGL core)
// ---------------------------------------------------------------------------
static void gui_task(void *arg)
{
    (void)arg;
    if (s_screen == NULL) {
        s_screen = create_music_screen();
    }
    lv_scr_load(s_screen);

    while (1) {
        gui_refresh_from_state();
        lv_task_handler();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// ---------------------------------------------------------------------------
// Public API (thread-safe, non-LVGL)
// ---------------------------------------------------------------------------
esp_err_t tft_ui_start(void)
{
    if (s_ui_started) {
        ESP_LOGW(TAG, "TFT UI already started");
        return ESP_OK;
    }

    s_state_mutex = xSemaphoreCreateMutex();
    if (s_state_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    s_action_queue = xQueueCreate(UI_ACTION_QUEUE_LEN, sizeof(music_ui_action_t));
    s_preview_queue = xQueueCreate(UI_PREVIEW_QUEUE_LEN, sizeof(preview_job_t));
    if (s_action_queue == NULL || s_preview_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create UI queues");
        return ESP_ERR_NO_MEM;
    }

    if (xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        config_defaults_locked();
        xSemaphoreGive(s_state_mutex);
    }

    lv_init();
    lvgl_driver_init();

    lv_color_t *buf1 = heap_caps_malloc(DISP_BUF_SIZE * sizeof(lv_color_t), MALLOC_CAP_DMA);
    lv_color_t *buf2 = heap_caps_malloc(DISP_BUF_SIZE * sizeof(lv_color_t), MALLOC_CAP_DMA);
    if (buf1 == NULL || buf2 == NULL) {
        ESP_LOGE(TAG, "Failed to allocate LVGL buffers");
        free(buf1);
        free(buf2);
        return ESP_ERR_NO_MEM;
    }

    static lv_disp_buf_t disp_buf;
    lv_disp_buf_init(&disp_buf, buf1, buf2, DISP_BUF_SIZE);

    lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.flush_cb = disp_driver_flush;
    disp_drv.buffer = &disp_buf;
    lv_disp_drv_register(&disp_drv);

    // Touch input is optional. Only register the LVGL input device if a touch
    // controller is enabled in menuconfig; otherwise the symbol may not be
    // compiled into lvgl_esp32_drivers and link will fail.
#if CONFIG_LV_TOUCH_CONTROLLER
    lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touch_driver_read;
    lv_indev_drv_register(&indev_drv);
    ESP_LOGI(TAG, "Touch input enabled (touch_driver_read registered)");
#else
    ESP_LOGW(TAG, "Touch input disabled in menuconfig; UI will render but not receive touch events");
#endif

    const esp_timer_create_args_t periodic_timer_args = {
        .callback = &lv_tick_task,
        .name = "music_lv_tick"
    };
    esp_timer_handle_t periodic_timer;
    ESP_ERROR_CHECK(esp_timer_create(&periodic_timer_args, &periodic_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(periodic_timer, LV_TICK_PERIOD_MS * 1000));

    gpio_reset_pin(LCD_BACKLIGHT);
    gpio_set_direction(LCD_BACKLIGHT, GPIO_MODE_OUTPUT);
    gpio_set_level(LCD_BACKLIGHT, 1);

    BaseType_t ok_gui = xTaskCreatePinnedToCore(gui_task, "music_ui",
                                                UI_TASK_STACK_SIZE, NULL,
                                                UI_TASK_PRIORITY, NULL,
                                                MUSIC_UI_CORE_ID);
    BaseType_t ok_preview = xTaskCreatePinnedToCore(preview_task, "music_preview",
                                                    PREVIEW_TASK_STACK_SIZE, NULL,
                                                    PREVIEW_TASK_PRIORITY, NULL,
                                                    MUSIC_UI_CORE_ID);

    if (ok_gui != pdPASS || ok_preview != pdPASS) {
        ESP_LOGE(TAG, "Failed to create UI/preview tasks");
        return ESP_FAIL;
    }

    s_ui_started = true;
    ESP_LOGI(TAG, "TFT UI started (UI core=%d, execution core expected=%d)",
             MUSIC_UI_CORE_ID, MUSIC_EXEC_CORE_ID);
    return ESP_OK;
}

bool tft_ui_get_config_snapshot(music_seq_config_t *out_config)
{
    if (out_config == NULL || s_state_mutex == NULL) {
        return false;
    }

    if (xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(20)) != pdTRUE) {
        return false;
    }

    *out_config = s_config;
    xSemaphoreGive(s_state_mutex);
    return true;
}

bool tft_ui_take_action(music_ui_action_t *out_action, uint32_t timeout_ms)
{
    if (out_action == NULL || s_action_queue == NULL) {
        return false;
    }

    TickType_t wait_ticks = pdMS_TO_TICKS(timeout_ms);
    return xQueueReceive(s_action_queue, out_action, wait_ticks) == pdTRUE;
}

void tft_ui_set_playback_state(const music_playback_state_t *state)
{
    if (state == NULL || s_state_mutex == NULL) {
        return;
    }

    if (xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        s_playback_state = *state;
        xSemaphoreGive(s_state_mutex);
    }
}

void tft_ui_set_status_message(const char *msg)
{
    if (s_state_mutex == NULL) {
        return;
    }

    if (xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        set_status_locked(msg);
        xSemaphoreGive(s_state_mutex);
    }
}
