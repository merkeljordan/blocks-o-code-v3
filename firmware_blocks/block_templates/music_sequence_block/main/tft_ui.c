/*
 * tft_ui.c  --  Music Sequence Block: TFT Touchscreen UI (LVGL v9)
 *
 * Drives an ILI9341 (240x320) TFT with XPT2046 touch via LVGL v9.
 * All rendering and touch processing run in a dedicated FreeRTOS task
 * pinned to core 1 so they never block the I2C / audio tasks on core 0.
 *
 * SCREEN FLOW
 * ~~~~~~~~~~~
 *   Screen 1 (Intro)    Dark splash with "Blocks o' Code (v3)" branding
 *                        and a big cyan START button.
 *
 *   Screen 2 (Songs)    Song selector with left/right arrows, Play preview,
 *                        and Select/confirm. Shows song name and index.
 *
 * HARDWARE
 * ~~~~~~~~
 *   Display : ILI9341 240x320 SPI
 *   Touch   : XPT2046 SPI
 *   Backlight: GPIO 32, active-high
 */

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"

#include "lvgl.h"
#include "esp_lcd_ili9341.h"
#include "esp_lcd_touch_xpt2046.h"

#include "../speaker.h"
#include "tft_ui.h"

#define TAG "MUSIC_SEQ_UI_V9"

/* TFT + touch wiring and runtime config. */
#define TFT_SPI_HOST               SPI3_HOST
#define TFT_PIXEL_CLOCK_HZ         (20 * 1000 * 1000)
#define TFT_CMD_BITS               8
#define TFT_PARAM_BITS             8
#define TFT_H_RES                  240
#define TFT_V_RES                  320
#define TFT_DRAW_BUF_LINES         20
#define TFT_TICK_PERIOD_MS         2
#define TFT_TASK_STACK_SIZE        (6 * 1024)
#define TFT_TASK_PRIORITY          5
#define PREVIEW_TASK_STACK_SIZE    4096
#define PREVIEW_TASK_PRIORITY      4
#define TFT_BACKLIGHT_ON_LEVEL     1

#define PIN_NUM_BK_LIGHT           32
#define PIN_NUM_SCLK               18
#define PIN_NUM_MOSI               23
#define PIN_NUM_MISO               19
#define PIN_NUM_LCD_DC             14
#define PIN_NUM_LCD_RST            4
#define PIN_NUM_LCD_CS             27
#define PIN_NUM_TOUCH_CS           26
#define PIN_NUM_TOUCH_IRQ          36

#define UI_ACTION_QUEUE_LEN        8
#define UI_PREVIEW_QUEUE_LEN       1

#if CONFIG_FREERTOS_UNICORE
#define MUSIC_UI_CORE_ID           0
#define MUSIC_AUDIO_CORE_ID        0
#else
#define MUSIC_UI_CORE_ID           1
#define MUSIC_AUDIO_CORE_ID        0
#endif

/* ── Driver handles ─────────────────────────────────────────────────── */
static bool                     s_ui_started      = false;
static lv_display_t            *s_display         = NULL;
static esp_lcd_panel_handle_t   s_panel_handle    = NULL;
static esp_lcd_touch_handle_t   s_touch_handle    = NULL;
static esp_timer_handle_t       s_lvgl_tick_timer = NULL;

/* ── Shared state ───────────────────────────────────────────────────── */
static SemaphoreHandle_t s_state_mutex   = NULL;
static QueueHandle_t     s_action_queue  = NULL;
static QueueHandle_t     s_preview_queue = NULL;

static uint8_t  s_song_index   = 0;
static bool     s_config_valid = false;
static bool     s_is_playing   = false;
static char     s_status_text[96];

/* ── Persistent widget handles ──────────────────────────────────────── */
static lv_obj_t *s_intro_screen    = NULL;
static lv_obj_t *s_song_screen     = NULL;
static lv_obj_t *s_song_name_label = NULL;
static lv_obj_t *s_song_num_label  = NULL;
static lv_obj_t *s_status_label    = NULL;
static lv_obj_t *s_play_btn        = NULL;

/* Forward declarations */
static lv_obj_t *create_intro_screen(void);
static lv_obj_t *create_song_screen(void);
static void open_song_screen(void);

/* ══════════════════════════════════════════════════════════════════════
 *  HELPERS
 * ══════════════════════════════════════════════════════════════════════ */

static void copy_text_safe(char *dst, size_t dst_len, const char *src)
{
    // Called by: many UI state update paths.
    // Purpose: safe bounded copy into fixed-size status buffers.
    if (dst_len == 0) return;
    if (src == NULL) { dst[0] = '\0'; return; }
    snprintf(dst, dst_len, "%s", src);
}

/*
 * Push a compact UI intent to the execution task.
 * We intentionally send only "what changed" + current song index.
 */
static void push_action(music_ui_action_type_t type)
{
    if (s_action_queue == NULL || type == MUSIC_UI_ACTION_NONE) return;
    music_ui_action_t action = {
        .type = type,
        .song_index = s_song_index,
    };
    (void)xQueueSend(s_action_queue, &action, 0);
}

static void anim_obj_y(void *obj, int32_t value)
{
    // Called by: LVGL animation engine.
    // Purpose: tiny helper to animate Y position.
    lv_obj_set_y((lv_obj_t *)obj, (lv_coord_t)value);
}

static void animate_button_bounce(lv_obj_t *obj, int32_t delta_y)
{
    // Called by: button callbacks (prev/next/play/select/start).
    // Purpose: short visual feedback that a tap was accepted.
    const int32_t y0 = (int32_t)lv_obj_get_y(obj);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_exec_cb(&a, anim_obj_y);
    lv_anim_set_values(&a, y0, y0 - delta_y);
    lv_anim_set_time(&a, 70);
    lv_anim_set_playback_time(&a, 90);
    lv_anim_set_repeat_count(&a, 1);
    lv_anim_start(&a);
}

static void lvgl_tick_cb(void *arg)
{
    // Called by: ESP timer in tft_ui_start().
    // Purpose: advance LVGL internal millisecond tick.
    (void)arg;
    lv_tick_inc(TFT_TICK_PERIOD_MS);
}

static bool lcd_flush_ready_cb(esp_lcd_panel_io_handle_t panel_io,
                               esp_lcd_panel_io_event_data_t *edata,
                               void *user_ctx)
{
    // Called by: esp_lcd when SPI color transfer completes.
    // Purpose: notify LVGL that current flush finished.
    (void)panel_io;
    (void)edata;
    lv_display_t *disp = (lv_display_t *)user_ctx;
    lv_display_flush_ready(disp);
    return false;
}

static void lcd_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    // Called by: LVGL whenever a dirty region must be pushed to display.
    // Calls: esp_lcd_panel_draw_bitmap().
    esp_lcd_panel_handle_t panel_handle =
        (esp_lcd_panel_handle_t)lv_display_get_user_data(disp);
    const int x1 = area->x1;
    const int y1 = area->y1;
    const int x2 = area->x2;
    const int y2 = area->y2;
    lv_draw_sw_rgb565_swap(px_map, (x2 + 1 - x1) * (y2 + 1 - y1));
    esp_lcd_panel_draw_bitmap(panel_handle, x1, y1, x2 + 1, y2 + 1, px_map);
}

static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    // Called by: LVGL input polling timer.
    // Purpose: pull latest touch sample from XPT2046 and map into LVGL data struct.
    esp_lcd_touch_handle_t tp =
        (esp_lcd_touch_handle_t)lv_indev_get_user_data(indev);
    esp_lcd_touch_point_data_t point_data[1] = {0};
    uint8_t point_cnt = 0;

    data->state = LV_INDEV_STATE_RELEASED;
    data->continue_reading = false;

    if (tp == NULL) return;
    if (esp_lcd_touch_read_data(tp) != ESP_OK) return;

    if (esp_lcd_touch_get_data(tp, point_data, &point_cnt, 1) == ESP_OK
        && point_cnt > 0)
    {
        // Report first touch point as current pointer location.
        data->point.x = point_data[0].x;
        data->point.y = point_data[0].y;
        data->state = LV_INDEV_STATE_PRESSED;
    }
}

/* ══════════════════════════════════════════════════════════════════════
 *  SCREEN 2 -- SONG SELECTOR
 * ══════════════════════════════════════════════════════════════════════ */

static void prev_btn_cb(lv_event_t *e)
{
    // Called by: LVGL click event on left-arrow button.
    // Calls: speaker_get_song_count() + push_action(MUSIC_UI_ACTION_SONG_CHANGED)
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    lv_obj_t *btn = lv_event_get_target_obj(e);
    animate_button_bounce(btn, 5);

    if (xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        size_t count = speaker_get_song_count();
        if (count > 0) {
            s_song_index = (s_song_index == 0) ? (uint8_t)(count - 1) : (uint8_t)(s_song_index - 1);
            s_config_valid = false;
            copy_text_safe(s_status_text, sizeof(s_status_text), "Tap Play to hear it!");
            push_action(MUSIC_UI_ACTION_SONG_CHANGED);
        }
        xSemaphoreGive(s_state_mutex);
    }
}

static void next_btn_cb(lv_event_t *e)
{
    // Called by: LVGL click event on right-arrow button.
    // Calls: speaker_get_song_count() + push_action(MUSIC_UI_ACTION_SONG_CHANGED)
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    lv_obj_t *btn = lv_event_get_target_obj(e);
    animate_button_bounce(btn, 5);

    if (xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        size_t count = speaker_get_song_count();
        if (count > 0) {
            s_song_index = (uint8_t)((s_song_index + 1) % count);
            s_config_valid = false;
            copy_text_safe(s_status_text, sizeof(s_status_text), "Tap Play to hear it!");
            push_action(MUSIC_UI_ACTION_SONG_CHANGED);
        }
        xSemaphoreGive(s_state_mutex);
    }
}

static void play_btn_cb(lv_event_t *e)
{
    // Called by: LVGL click event on Play button.
    // Calls: queue write to s_preview_queue (actual audio happens in preview_task()).
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    lv_obj_t *btn = lv_event_get_target_obj(e);
    animate_button_bounce(btn, 5);

    /* Capture selected song index under mutex and schedule playback on worker. */
    uint8_t idx = 0;
    if (xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        if (s_is_playing) {
            copy_text_safe(s_status_text, sizeof(s_status_text), "Already playing...");
            xSemaphoreGive(s_state_mutex);
            return;
        }
        idx = s_song_index;
        copy_text_safe(s_status_text, sizeof(s_status_text), "Playing...");
        xSemaphoreGive(s_state_mutex);
    }
    /* Keep only the latest preview request; older previews are superseded. */
    (void)xQueueOverwrite(s_preview_queue, &idx);
}

static void select_btn_cb(lv_event_t *e)
{
    // Called by: LVGL click event on Select button.
    // Calls: push_action(MUSIC_UI_ACTION_SONG_SELECTED) for main execution_task().
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    lv_obj_t *btn = lv_event_get_target_obj(e);
    animate_button_bounce(btn, 5);

    if (xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        s_config_valid = true;
        copy_text_safe(s_status_text, sizeof(s_status_text), "Song selected!");
        push_action(MUSIC_UI_ACTION_SONG_SELECTED);
        xSemaphoreGive(s_state_mutex);
    }
}

static lv_obj_t *make_btn(lv_obj_t *parent, const char *text,
                           lv_coord_t w, lv_coord_t h,
                           lv_event_cb_t cb, uint32_t bg_hex)
{
    // Called by: create_song_screen() and intro screen builders.
    // Purpose: centralize button style + callback attachment.
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_style_radius(btn, 16, 0);
    lv_obj_set_style_border_width(btn, 2, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(0x0F172A), 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(bg_hex), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_80, LV_STATE_PRESSED);
    lv_obj_set_style_text_color(btn, lv_color_hex(0x000000), 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    return btn;
}

static lv_obj_t *create_song_screen(void)
{
    // Called by: open_song_screen() when song screen is first needed.
    // Purpose: build full selector UI and keep key widget handles in globals.
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_set_style_radius(scr, 0, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x12203A), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    /* Title */
    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, LV_SYMBOL_AUDIO " Music Maker");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFDE68A), 0);
    lv_obj_set_style_bg_color(title, lv_color_hex(0x0B1220), 0);
    lv_obj_set_style_bg_opa(title, LV_OPA_50, 0);
    lv_obj_set_style_pad_hor(title, 6, 0);
    lv_obj_set_style_pad_ver(title, 2, 0);
    lv_obj_set_style_radius(title, 6, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    /* Song number label  "Song 1 of N" */
    s_song_num_label = lv_label_create(scr);
    lv_label_set_text(s_song_num_label, "Song 1 of 1");
    lv_obj_set_style_text_color(s_song_num_label, lv_color_hex(0xBBD0FF), 0);
    lv_obj_set_style_bg_color(s_song_num_label, lv_color_hex(0x0B1220), 0);
    lv_obj_set_style_bg_opa(s_song_num_label, LV_OPA_30, 0);
    lv_obj_set_style_pad_hor(s_song_num_label, 6, 0);
    lv_obj_set_style_pad_ver(s_song_num_label, 2, 0);
    lv_obj_set_style_radius(s_song_num_label, 6, 0);
    lv_obj_align(s_song_num_label, LV_ALIGN_TOP_MID, 0, 42);

    /* Song name card */
    lv_obj_t *card = lv_obj_create(scr);
    lv_obj_set_size(card, 220, 60);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x243B63), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x88A8FF), 0);
    lv_obj_set_style_radius(card, 14, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 68);

    s_song_name_label = lv_label_create(card);
    lv_obj_set_width(s_song_name_label, 200);
    lv_label_set_long_mode(s_song_name_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(s_song_name_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(s_song_name_label, "Baby Shark");
    lv_obj_set_style_text_color(s_song_name_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(s_song_name_label);

    /* Left / Right arrow buttons */
    lv_obj_t *left_btn = make_btn(scr, LV_SYMBOL_LEFT, 44, 44, prev_btn_cb, 0xA5D8FF);
    lv_obj_set_pos(left_btn, 10, 76);

    lv_obj_t *right_btn = make_btn(scr, LV_SYMBOL_RIGHT, 44, 44, next_btn_cb, 0xA5D8FF);
    lv_obj_set_pos(right_btn, 186, 76);

    /* Play button */
    s_play_btn = make_btn(scr, LV_SYMBOL_PLAY " Play", 108, 48, play_btn_cb, 0x93C5FD);
    lv_obj_set_pos(s_play_btn, 10, 148);

    /* Select button */
    lv_obj_t *sel_btn = make_btn(scr, LV_SYMBOL_OK " Select", 108, 48, select_btn_cb, 0x86EFAC);
    lv_obj_set_pos(sel_btn, 124, 148);

    /* Status label at bottom */
    s_status_label = lv_label_create(scr);
    lv_obj_set_width(s_status_label, 228);
    lv_label_set_long_mode(s_status_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(s_status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(s_status_label, "Pick a song and tap Play!");
    lv_obj_set_style_text_color(s_status_label, lv_color_hex(0xFDE68A), 0);
    lv_obj_set_style_bg_color(s_status_label, lv_color_hex(0x0B1220), 0);
    lv_obj_set_style_bg_opa(s_status_label, LV_OPA_40, 0);
    lv_obj_set_style_pad_hor(s_status_label, 6, 0);
    lv_obj_set_style_pad_ver(s_status_label, 2, 0);
    lv_obj_set_style_radius(s_status_label, 6, 0);
    lv_obj_align(s_status_label, LV_ALIGN_BOTTOM_MID, 0, -12);

    return scr;
}

/* ══════════════════════════════════════════════════════════════════════
 *  SCREEN 1 -- INTRO / SPLASH
 * ══════════════════════════════════════════════════════════════════════ */

static void start_btn_event_cb(lv_event_t *e)
{
    // Called by: LVGL click event on intro START button.
    // Calls: open_song_screen()
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    lv_obj_t *btn = lv_event_get_target_obj(e);
    animate_button_bounce(btn, 8);
    open_song_screen();
}

static lv_obj_t *create_intro_screen(void)
{
    // Called by: lvgl_task() on first UI load.
    // Purpose: build splash screen and START button.
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_set_style_radius(scr, 0, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0F0F23), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "Blocks o' Code");
    lv_obj_set_style_text_color(title, lv_color_hex(0x00E5FF), 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -70);

    lv_obj_t *version = lv_label_create(scr);
    lv_label_set_text(version, "(v3)");
    lv_obj_set_style_text_color(version, lv_color_hex(0x80D0FF), 0);
    lv_obj_align_to(version, title, LV_ALIGN_OUT_BOTTOM_MID, 0, 4);

    lv_obj_t *tagline = lv_label_create(scr);
    lv_label_set_text(tagline, LV_SYMBOL_AUDIO " Music Sequence Block");
    lv_obj_set_style_text_color(tagline, lv_color_hex(0x888888), 0);
    lv_obj_align_to(tagline, version, LV_ALIGN_OUT_BOTTOM_MID, 0, 14);

    lv_obj_t *btn = lv_button_create(scr);
    lv_obj_set_size(btn, 180, 60);
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, 60);
    lv_obj_set_style_radius(btn, 18, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x00E5FF), 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x00B8D4), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(btn, 3, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_shadow_width(btn, 12, 0);
    lv_obj_set_style_shadow_color(btn, lv_color_hex(0x00E5FF), 0);
    lv_obj_set_style_shadow_opa(btn, LV_OPA_60, 0);
    lv_obj_add_event_cb(btn, start_btn_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_label = lv_label_create(btn);
    lv_label_set_text(btn_label, "START");
    lv_obj_set_style_text_color(btn_label, lv_color_hex(0x0F0F23), 0);
    lv_obj_center(btn_label);

    return scr;
}

static void open_song_screen(void)
{
    // Called by: start_btn_event_cb().
    // Calls: create_song_screen() lazily and then animates to it.
    if (s_song_screen == NULL) {
        s_song_screen = create_song_screen();
    }

    /*
     * Entering song screen resets local selection flow:
     * kid should preview and then explicitly Select to mark config valid.
     */
    if (xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        s_song_index = 0;
        s_config_valid = false;
        copy_text_safe(s_status_text, sizeof(s_status_text), "Pick a song and tap Play!");
        xSemaphoreGive(s_state_mutex);
    }

    lv_screen_load_anim(s_song_screen, LV_SCR_LOAD_ANIM_MOVE_LEFT, 250, 0, false);
}

/* ══════════════════════════════════════════════════════════════════════
 *  UI REFRESH (called from LVGL task context)
 * ══════════════════════════════════════════════════════════════════════ */

static void gui_refresh_from_state(void)
{
    // Called by: lvgl_task() every iteration.
    // Purpose: mirror shared state into visible labels.
    if (s_song_screen == NULL) return;

    uint8_t idx = 0;
    char status_buf[sizeof(s_status_text)];
    status_buf[0] = '\0';

    if (xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
        idx = s_song_index;
        copy_text_safe(status_buf, sizeof(status_buf), s_status_text);
        xSemaphoreGive(s_state_mutex);
    }

    /* Song count/name come from speaker_music.c catalog helpers. */
    size_t count = speaker_get_song_count();

    if (s_song_name_label) {
        lv_label_set_text(s_song_name_label, speaker_get_song_name(idx));
    }
    if (s_song_num_label) {
        char num_text[32];
        snprintf(num_text, sizeof(num_text), "Song %u of %u", (unsigned)(idx + 1), (unsigned)count);
        lv_label_set_text(s_song_num_label, num_text);
    }
    if (s_status_label) {
        lv_label_set_text(s_status_label, status_buf);
    }
}

/* ══════════════════════════════════════════════════════════════════════
 *  PREVIEW WORKER TASK (plays songs off the LVGL thread)
 * ══════════════════════════════════════════════════════════════════════ */

static void preview_task(void *arg)
{
    // Called by: FreeRTOS task spawned in tft_ui_start().
    // Waits on s_preview_queue and performs blocking speaker_play_song() work.
    (void)arg;
    uint8_t idx;

    while (1) {
        // Block until Play button enqueues a song index.
        if (xQueueReceive(s_preview_queue, &idx, portMAX_DELAY) != pdTRUE) continue;

        if (xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
            s_is_playing = true;
            xSemaphoreGive(s_state_mutex);
        }

        /*
         * Playback is synchronous in current audio API, so this worker owns the
         * blocking call to keep LVGL task responsive.
         */
        esp_err_t err = speaker_play_song(idx);

        if (xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
            s_is_playing = false;
            if (err == ESP_OK) {
                copy_text_safe(s_status_text, sizeof(s_status_text), "Done! Tap Select to confirm.");
            } else {
                copy_text_safe(s_status_text, sizeof(s_status_text), "Playback error.");
            }
            xSemaphoreGive(s_state_mutex);
        }
    }
}

/* ══════════════════════════════════════════════════════════════════════
 *  LVGL TASK
 * ══════════════════════════════════════════════════════════════════════ */

static void lvgl_task(void *arg)
{
    // Called by: FreeRTOS task spawned in tft_ui_start().
    // Runs the LVGL event/render loop.
    (void)arg;
    ESP_LOGI(TAG, "LVGL task started");

    if (s_intro_screen == NULL) {
        s_intro_screen = create_intro_screen();
    }
    lv_screen_load(s_intro_screen);

    while (1) {
        // 1) Sync labels from shared state.
        gui_refresh_from_state();
        // 2) Let LVGL process timers/events and suggest next delay.
        uint32_t delay_ms = lv_timer_handler();
        if (delay_ms < 5)       delay_ms = 5;
        else if (delay_ms > 30) delay_ms = 30;
        // 3) Sleep to avoid maxing CPU.
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

/* ══════════════════════════════════════════════════════════════════════
 *  PUBLIC API
 * ══════════════════════════════════════════════════════════════════════ */

esp_err_t tft_ui_start(void)
{
    // Called by: app_main().
    // Creates display/touch drivers, shared queues/mutex, then GUI + preview tasks.
    if (s_ui_started) {
        ESP_LOGW(TAG, "TFT UI already started");
        return ESP_OK;
    }

    s_state_mutex = xSemaphoreCreateMutex();
    if (s_state_mutex == NULL) return ESP_ERR_NO_MEM;

    s_action_queue  = xQueueCreate(UI_ACTION_QUEUE_LEN, sizeof(music_ui_action_t));
    s_preview_queue = xQueueCreate(UI_PREVIEW_QUEUE_LEN, sizeof(uint8_t));
    if (s_action_queue == NULL || s_preview_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create UI queues");
        return ESP_ERR_NO_MEM;
    }

    s_song_index = 0;
    s_config_valid = false;
    s_is_playing = false;
    copy_text_safe(s_status_text, sizeof(s_status_text), "Pick a song and tap Play!");

    ESP_LOGI(TAG, "Starting LVGL v9 TFT UI");

    /* Backlight off during panel initialization. */
    gpio_config_t bk_cfg = {
        .pin_bit_mask = 1ULL << PIN_NUM_BK_LIGHT,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&bk_cfg));
    gpio_set_level(PIN_NUM_BK_LIGHT, !TFT_BACKLIGHT_ON_LEVEL);

    /* Initialize SPI bus (shared by LCD and touch). */
    spi_bus_config_t buscfg = {
        .sclk_io_num = PIN_NUM_SCLK,
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = PIN_NUM_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = TFT_H_RES * TFT_DRAW_BUF_LINES * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(TFT_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));

    /* LCD panel IO over SPI. */
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num = PIN_NUM_LCD_DC,
        .cs_gpio_num = PIN_NUM_LCD_CS,
        .pclk_hz = TFT_PIXEL_CLOCK_HZ,
        .lcd_cmd_bits = TFT_CMD_BITS,
        .lcd_param_bits = TFT_PARAM_BITS,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(
        (esp_lcd_spi_bus_handle_t)TFT_SPI_HOST, &io_cfg, &io_handle));

    /* ILI9341 panel driver. */
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = PIN_NUM_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(io_handle, &panel_cfg, &s_panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(s_panel_handle, true, false));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel_handle, true));

    /* Backlight on. */
    gpio_set_level(PIN_NUM_BK_LIGHT, TFT_BACKLIGHT_ON_LEVEL);

    /* Initialize LVGL. */
    lv_init();

    s_display = lv_display_create(TFT_H_RES, TFT_V_RES);
    assert(s_display != NULL);

    const size_t draw_buf_sz = TFT_H_RES * TFT_DRAW_BUF_LINES * sizeof(lv_color16_t);
    void *buf1 = spi_bus_dma_memory_alloc(TFT_SPI_HOST, draw_buf_sz, 0);
    void *buf2 = spi_bus_dma_memory_alloc(TFT_SPI_HOST, draw_buf_sz, 0);
    assert(buf1 != NULL);
    assert(buf2 != NULL);

    lv_display_set_buffers(s_display, buf1, buf2, draw_buf_sz,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_user_data(s_display, s_panel_handle);
    lv_display_set_color_format(s_display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(s_display, lcd_flush_cb);

    const esp_lcd_panel_io_callbacks_t io_callbacks = {
        .on_color_trans_done = lcd_flush_ready_cb,
    };
    ESP_ERROR_CHECK(esp_lcd_panel_io_register_event_callbacks(
        io_handle, &io_callbacks, s_display));

    /* XPT2046 touch. */
    esp_lcd_panel_io_handle_t tp_io_handle = NULL;
    esp_lcd_panel_io_spi_config_t tp_io_cfg =
        ESP_LCD_TOUCH_IO_SPI_XPT2046_CONFIG(PIN_NUM_TOUCH_CS);
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(
        (esp_lcd_spi_bus_handle_t)TFT_SPI_HOST, &tp_io_cfg, &tp_io_handle));

    esp_lcd_touch_config_t tp_cfg = {
        .x_max = TFT_H_RES,
        .y_max = TFT_V_RES,
        .rst_gpio_num = -1,
        .int_gpio_num = PIN_NUM_TOUCH_IRQ,
        .flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 1,
        },
    };
    ESP_ERROR_CHECK(esp_lcd_touch_new_spi_xpt2046(tp_io_handle, &tp_cfg,
                                                   &s_touch_handle));

    lv_indev_t *indev = lv_indev_create();
    assert(indev != NULL);
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(indev, s_display);
    lv_indev_set_user_data(indev, s_touch_handle);
    lv_indev_set_long_press_time(indev, 800);
    lv_indev_set_read_cb(indev, touch_read_cb);

    lv_timer_t *read_timer = lv_indev_get_read_timer(indev);
    if (read_timer != NULL) {
        lv_timer_set_period(read_timer, 10);
    }

    /* LVGL tick timer. */
    const esp_timer_create_args_t tick_args = {
        .callback = lvgl_tick_cb,
        .name = "lvgl_tick",
    };
    ESP_ERROR_CHECK(esp_timer_create(&tick_args, &s_lvgl_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(s_lvgl_tick_timer,
                                             TFT_TICK_PERIOD_MS * 1000));

    /* Spawn GUI task and preview task on separate cores in dual-core builds. */
    BaseType_t ok_gui = xTaskCreatePinnedToCore(lvgl_task, "music_ui_v9",
                                                TFT_TASK_STACK_SIZE, NULL,
                                                TFT_TASK_PRIORITY, NULL,
                                                MUSIC_UI_CORE_ID);
    BaseType_t ok_preview = xTaskCreatePinnedToCore(preview_task, "music_preview",
                                                    PREVIEW_TASK_STACK_SIZE, NULL,
                                                    PREVIEW_TASK_PRIORITY, NULL,
                                                    MUSIC_AUDIO_CORE_ID);

    if (ok_gui != pdPASS || ok_preview != pdPASS) {
        ESP_LOGE(TAG, "Failed to create UI/preview tasks");
        return ESP_FAIL;
    }

    s_ui_started = true;
    ESP_LOGI(TAG, "LVGL/UI core=%d, preview/audio core=%d", MUSIC_UI_CORE_ID, MUSIC_AUDIO_CORE_ID);
    return ESP_OK;
}

bool tft_ui_take_action(music_ui_action_t *out_action, uint32_t timeout_ms)
{
    // Called by: execution_task() in main.c to consume UI decisions.
    if (out_action == NULL || s_action_queue == NULL) return false;
    return xQueueReceive(s_action_queue, out_action, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void tft_ui_set_playback_state(const music_playback_state_t *state)
{
    // Called by: optional external control paths that want to override playback state.
    if (state == NULL || s_state_mutex == NULL) return;
    if (xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        s_is_playing = state->is_playing;
        xSemaphoreGive(s_state_mutex);
    }
}

void tft_ui_set_status_message(const char *msg)
{
    // Called by: optional external control paths that want status text updates.
    if (s_state_mutex == NULL) return;
    if (xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        copy_text_safe(s_status_text, sizeof(s_status_text), msg);
        xSemaphoreGive(s_state_mutex);
    }
}
