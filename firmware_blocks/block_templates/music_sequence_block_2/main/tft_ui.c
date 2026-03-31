/*
 * tft_ui.c  --  Music Sequence Block: TFT Touchscreen UI (LVGL v9)
 *
 * Shared UI code used by both:
 * - ESP32 firmware (ILI9341 + XPT2046 + FreeRTOS tasks)
 * - desktop SDL simulator builds (`MUSIC_SEQ_UI_SIMULATOR`)
 */

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lvgl.h"
#include "tft_ui.h"

#if defined(MUSIC_SEQ_UI_SIMULATOR)
size_t speaker_get_song_count(void);
const char *speaker_get_song_name(size_t index);
music_age_range_t speaker_get_song_age_range(size_t index);
const char *speaker_get_age_range_label(music_age_range_t age_range);
esp_err_t speaker_play_song(size_t index);

#define ESP_LOGI(tag, fmt, ...) fprintf(stdout, "[%s] " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) fprintf(stdout, "[%s] " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGE(tag, fmt, ...) fprintf(stderr, "[%s] " fmt "\n", tag, ##__VA_ARGS__)
static uint8_t battery_monitor_get_percent(void)
{
    return 100U;
}
#else
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "esp_lcd_ili9341.h"
#include "esp_lcd_touch_xpt2046.h"

#include "battery_monitor.h"
#include "../speaker.h"
#endif

#define TAG "MUSIC_SEQ_UI_V9"

#define TFT_H_RES 240
#define TFT_V_RES 320
#define BATTERY_REFRESH_MS 3000U

#define UI_ACTION_QUEUE_LEN  8
#define UI_PREVIEW_QUEUE_LEN 1

#if !defined(MUSIC_SEQ_UI_SIMULATOR)
/* TFT + touch wiring and runtime config. */
#define TFT_SPI_HOST               SPI3_HOST
#define TFT_PIXEL_CLOCK_HZ         (20 * 1000 * 1000)
#define TFT_CMD_BITS               8
#define TFT_PARAM_BITS             8
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

#if CONFIG_FREERTOS_UNICORE
#define MUSIC_UI_CORE_ID           0
#define MUSIC_AUDIO_CORE_ID        0
#else
#define MUSIC_UI_CORE_ID           1
#define MUSIC_AUDIO_CORE_ID        0
#endif

static lv_display_t            *s_display         = NULL;
static esp_lcd_panel_handle_t   s_panel_handle    = NULL;
static esp_lcd_touch_handle_t   s_touch_handle    = NULL;
static esp_timer_handle_t       s_lvgl_tick_timer = NULL;
static SemaphoreHandle_t        s_state_mutex     = NULL;
static QueueHandle_t            s_action_queue    = NULL;
static QueueHandle_t            s_preview_queue   = NULL;
#else
typedef struct {
    music_ui_action_t items[UI_ACTION_QUEUE_LEN];
    uint8_t head;
    uint8_t count;
} sim_action_queue_t;

static sim_action_queue_t s_action_queue = {0};
#endif

static bool s_ui_started = false;
static uint8_t s_song_index = 0;
static bool s_config_valid = false;
static bool s_is_playing = false;
static music_age_range_t s_age_filter = MUSIC_AGE_RANGE_ALL;
static char s_status_text[96];

static lv_obj_t *s_intro_screen = NULL;
static lv_obj_t *s_song_screen = NULL;
static lv_obj_t *s_age_filter_label = NULL;
static lv_obj_t *s_song_name_label = NULL;
static lv_obj_t *s_song_age_label = NULL;
static lv_obj_t *s_song_num_label = NULL;
static lv_obj_t *s_status_label = NULL;

typedef struct {
    lv_obj_t *root;
    lv_obj_t *fill;
    lv_obj_t *text;
} battery_indicator_t;

static battery_indicator_t s_intro_battery = {0};
static battery_indicator_t s_song_battery = {0};

static lv_obj_t *create_intro_screen(void);
static lv_obj_t *create_song_screen(void);
static void open_song_screen(void);
static void gui_refresh_from_state(void);
static void push_action(music_ui_action_type_t type);
static bool song_matches_filter(size_t song_index, music_age_range_t age_filter);
static size_t get_filtered_song_count(music_age_range_t age_filter);
static bool get_filtered_song_index(music_age_range_t age_filter,
                                    size_t filtered_position,
                                    uint8_t *out_song_index);
static size_t get_filtered_song_position(uint8_t song_index, music_age_range_t age_filter);
static void select_filtered_song_by_delta(int delta);
static void change_age_filter(int delta);
static void create_battery_indicator(lv_obj_t *parent, battery_indicator_t *indicator);
static void update_battery_indicator(battery_indicator_t *indicator, unsigned percent);
static void refresh_battery_indicators(void);

static void copy_text_safe(char *dst, size_t dst_len, const char *src)
{
    if (dst_len == 0U) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    (void)snprintf(dst, dst_len, "%s", src);
}

static bool song_matches_filter(size_t song_index, music_age_range_t age_filter)
{
    if (song_index >= speaker_get_song_count()) {
        return false;
    }

    if (age_filter == MUSIC_AGE_RANGE_ALL) {
        return true;
    }

    return speaker_get_song_age_range(song_index) == age_filter;
}

static size_t get_filtered_song_count(music_age_range_t age_filter)
{
    size_t count = 0;
    size_t total = speaker_get_song_count();

    for (size_t i = 0; i < total; i++) {
        if (song_matches_filter(i, age_filter)) {
            count++;
        }
    }

    return count;
}

static bool get_filtered_song_index(music_age_range_t age_filter,
                                    size_t filtered_position,
                                    uint8_t *out_song_index)
{
    size_t total = speaker_get_song_count();
    size_t seen = 0;

    for (size_t i = 0; i < total; i++) {
        if (!song_matches_filter(i, age_filter)) {
            continue;
        }

        if (seen == filtered_position) {
            if (out_song_index != NULL) {
                *out_song_index = (uint8_t)i;
            }
            return true;
        }
        seen++;
    }

    return false;
}

static size_t get_filtered_song_position(uint8_t song_index, music_age_range_t age_filter)
{
    size_t total = speaker_get_song_count();
    size_t seen = 0;

    for (size_t i = 0; i < total; i++) {
        if (!song_matches_filter(i, age_filter)) {
            continue;
        }

        if ((uint8_t)i == song_index) {
            return seen;
        }
        seen++;
    }

    return 0;
}

static void select_filtered_song_by_delta(int delta)
{
    size_t filtered_count = get_filtered_song_count(s_age_filter);
    size_t current_position = 0;
    size_t next_position = 0;
    uint8_t next_song_index = 0;

    if (filtered_count == 0U) {
        return;
    }

    current_position = get_filtered_song_position(s_song_index, s_age_filter);
    if (delta < 0) {
        next_position = (current_position == 0U) ? (filtered_count - 1U)
                                                 : (current_position - 1U);
    } else {
        next_position = (current_position + 1U) % filtered_count;
    }

    if (get_filtered_song_index(s_age_filter, next_position, &next_song_index)) {
        s_song_index = next_song_index;
    }
}

static void change_age_filter(int delta)
{
    int next_filter = (int)s_age_filter + delta;

    if (next_filter < (int)MUSIC_AGE_RANGE_ALL) {
        next_filter = (int)MUSIC_AGE_RANGE_COUNT - 1;
    } else if (next_filter >= (int)MUSIC_AGE_RANGE_COUNT) {
        next_filter = (int)MUSIC_AGE_RANGE_ALL;
    }

    s_age_filter = (music_age_range_t)next_filter;
    if (!get_filtered_song_index(s_age_filter, 0, &s_song_index)) {
        s_song_index = 0;
    }

    s_config_valid = false;
    copy_text_safe(s_status_text, sizeof(s_status_text), "Pick a song for this age range.");
    push_action(MUSIC_UI_ACTION_SONG_CHANGED);
}

static void state_lock(void)
{
#if !defined(MUSIC_SEQ_UI_SIMULATOR)
    if (s_state_mutex != NULL) {
        (void)xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(20));
    }
#endif
}

static void state_unlock(void)
{
#if !defined(MUSIC_SEQ_UI_SIMULATOR)
    if (s_state_mutex != NULL) {
        xSemaphoreGive(s_state_mutex);
    }
#endif
}

static void sim_queue_push_action(music_ui_action_type_t type)
{
#if defined(MUSIC_SEQ_UI_SIMULATOR)
    if (type == MUSIC_UI_ACTION_NONE) {
        return;
    }

    if (s_action_queue.count >= UI_ACTION_QUEUE_LEN) {
        s_action_queue.head = (uint8_t)((s_action_queue.head + 1U) % UI_ACTION_QUEUE_LEN);
        s_action_queue.count--;
    }

    {
        uint8_t tail = (uint8_t)((s_action_queue.head + s_action_queue.count) % UI_ACTION_QUEUE_LEN);
        s_action_queue.items[tail] = (music_ui_action_t) {
            .type = type,
            .song_index = s_song_index,
        };
        s_action_queue.count++;
    }
#else
    (void)type;
#endif
}

static void push_action(music_ui_action_type_t type)
{
#if defined(MUSIC_SEQ_UI_SIMULATOR)
    sim_queue_push_action(type);
#else
    if (s_action_queue == NULL || type == MUSIC_UI_ACTION_NONE) {
        return;
    }

    {
        music_ui_action_t action = {
            .type = type,
            .song_index = s_song_index,
        };
        (void)xQueueSend(s_action_queue, &action, 0);
    }
#endif
}

static void anim_obj_y(void *obj, int32_t value)
{
    lv_obj_set_y((lv_obj_t *)obj, (lv_coord_t)value);
}

static void animate_button_bounce(lv_obj_t *obj, int32_t delta_y)
{
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

static lv_color_t battery_color_for_percent(unsigned percent)
{
    if (percent <= 20U) {
        return lv_color_hex(0xFF5C5C);
    }
    if (percent <= 50U) {
        return lv_color_hex(0xFFD166);
    }
    return lv_color_hex(0x69F0AE);
}

static void create_battery_indicator(lv_obj_t *parent, battery_indicator_t *indicator)
{
    lv_obj_t *body = NULL;
    lv_obj_t *cap = NULL;

    indicator->root = lv_obj_create(parent);
    lv_obj_remove_style_all(indicator->root);
    lv_obj_set_size(indicator->root, 72, 20);
    lv_obj_align(indicator->root, LV_ALIGN_TOP_RIGHT, -8, 8);
    lv_obj_clear_flag(indicator->root, LV_OBJ_FLAG_SCROLLABLE);

    indicator->text = lv_label_create(indicator->root);
    lv_label_set_text(indicator->text, "100%");
    lv_obj_set_style_text_color(indicator->text, lv_color_hex(0xBBD0FF), 0);
    lv_obj_align(indicator->text, LV_ALIGN_LEFT_MID, 0, 0);

    body = lv_obj_create(indicator->root);
    lv_obj_remove_style_all(body);
    lv_obj_set_size(body, 24, 12);
    lv_obj_align(body, LV_ALIGN_RIGHT_MID, -6, 0);
    lv_obj_set_style_border_width(body, 2, 0);
    lv_obj_set_style_border_color(body, lv_color_hex(0xE2E8F0), 0);
    lv_obj_set_style_radius(body, 3, 0);
    lv_obj_set_style_bg_color(body, lv_color_hex(0x0B1220), 0);
    lv_obj_set_style_bg_opa(body, LV_OPA_70, 0);
    lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);

    indicator->fill = lv_obj_create(body);
    lv_obj_remove_style_all(indicator->fill);
    lv_obj_set_size(indicator->fill, 20, 8);
    lv_obj_align(indicator->fill, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_radius(indicator->fill, 1, 0);
    lv_obj_set_style_bg_color(indicator->fill, battery_color_for_percent(100U), 0);
    lv_obj_set_style_bg_opa(indicator->fill, LV_OPA_COVER, 0);

    cap = lv_obj_create(indicator->root);
    lv_obj_remove_style_all(cap);
    lv_obj_set_size(cap, 3, 6);
    lv_obj_align(cap, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_radius(cap, 1, 0);
    lv_obj_set_style_bg_color(cap, lv_color_hex(0xE2E8F0), 0);
    lv_obj_set_style_bg_opa(cap, LV_OPA_COVER, 0);
}

static void update_battery_indicator(battery_indicator_t *indicator, unsigned percent)
{
    uint32_t fill_width = 0;
    lv_color_t fill_color = battery_color_for_percent(percent);

    if (indicator->root == NULL) {
        return;
    }

    fill_width = (percent * 20U) / 100U;
    if (percent > 0U && fill_width == 0U) {
        fill_width = 1U;
    }

    lv_label_set_text_fmt(indicator->text, "%u%%", percent);
    lv_obj_set_size(indicator->fill, (lv_coord_t)fill_width, 8);
    lv_obj_set_style_bg_color(indicator->fill, fill_color, 0);
}

static void refresh_battery_indicators(void)
{
    const unsigned percent = (unsigned)battery_monitor_get_percent();

    update_battery_indicator(&s_intro_battery, percent);
    update_battery_indicator(&s_song_battery, percent);
}

#if !defined(MUSIC_SEQ_UI_SIMULATOR)
static void lvgl_tick_cb(void *arg)
{
    (void)arg;
    lv_tick_inc(TFT_TICK_PERIOD_MS);
}

static bool lcd_flush_ready_cb(esp_lcd_panel_io_handle_t panel_io,
                               esp_lcd_panel_io_event_data_t *edata,
                               void *user_ctx)
{
    (void)panel_io;
    (void)edata;
    lv_display_t *disp = (lv_display_t *)user_ctx;
    lv_display_flush_ready(disp);
    return false;
}

static void lcd_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
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
    esp_lcd_touch_handle_t tp =
        (esp_lcd_touch_handle_t)lv_indev_get_user_data(indev);
    esp_lcd_touch_point_data_t point_data[1] = {0};
    uint8_t point_cnt = 0;

    data->state = LV_INDEV_STATE_RELEASED;
    data->continue_reading = false;

    if (tp == NULL) {
        return;
    }
    if (esp_lcd_touch_read_data(tp) != ESP_OK) {
        return;
    }

    if (esp_lcd_touch_get_data(tp, point_data, &point_cnt, 1) == ESP_OK &&
        point_cnt > 0U)
    {
        data->point.x = point_data[0].x;
        data->point.y = point_data[0].y;
        data->state = LV_INDEV_STATE_PRESSED;
    }
}
#endif

static void request_preview(uint8_t idx)
{
#if defined(MUSIC_SEQ_UI_SIMULATOR)
    if (s_is_playing) {
        copy_text_safe(s_status_text, sizeof(s_status_text), "Already playing...");
        gui_refresh_from_state();
        return;
    }

    s_is_playing = true;
    copy_text_safe(s_status_text, sizeof(s_status_text), "Playing...");
    gui_refresh_from_state();

    {
        esp_err_t err = speaker_play_song(idx);
        if (err != ESP_OK) {
            s_is_playing = false;
            copy_text_safe(s_status_text, sizeof(s_status_text), "Playback error.");
        }
    }
#else
    if (s_preview_queue != NULL) {
        (void)xQueueOverwrite(s_preview_queue, &idx);
    }
#endif

    gui_refresh_from_state();
}

static void prev_btn_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    animate_button_bounce(lv_event_get_target_obj(e), 5);

    state_lock();
    if (get_filtered_song_count(s_age_filter) > 0U) {
        select_filtered_song_by_delta(-1);
        s_config_valid = false;
        copy_text_safe(s_status_text, sizeof(s_status_text), "Tap Play to hear it!");
        push_action(MUSIC_UI_ACTION_SONG_CHANGED);
    }
    state_unlock();
    gui_refresh_from_state();
}

static void next_btn_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    animate_button_bounce(lv_event_get_target_obj(e), 5);

    state_lock();
    if (get_filtered_song_count(s_age_filter) > 0U) {
        select_filtered_song_by_delta(1);
        s_config_valid = false;
        copy_text_safe(s_status_text, sizeof(s_status_text), "Tap Play to hear it!");
        push_action(MUSIC_UI_ACTION_SONG_CHANGED);
    }
    state_unlock();
    gui_refresh_from_state();
}

static void age_prev_btn_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    animate_button_bounce(lv_event_get_target_obj(e), 4);

    state_lock();
    change_age_filter(-1);
    state_unlock();
    gui_refresh_from_state();
}

static void age_next_btn_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    animate_button_bounce(lv_event_get_target_obj(e), 4);

    state_lock();
    change_age_filter(1);
    state_unlock();
    gui_refresh_from_state();
}

static void play_btn_cb(lv_event_t *e)
{
    uint8_t idx = 0;

    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    animate_button_bounce(lv_event_get_target_obj(e), 5);

    state_lock();
    if (get_filtered_song_count(s_age_filter) == 0U) {
        copy_text_safe(s_status_text, sizeof(s_status_text), "No songs in this age range yet.");
        state_unlock();
        gui_refresh_from_state();
        return;
    }
    if (s_is_playing) {
        copy_text_safe(s_status_text, sizeof(s_status_text), "Already playing...");
        state_unlock();
        gui_refresh_from_state();
        return;
    }
    idx = s_song_index;
    copy_text_safe(s_status_text, sizeof(s_status_text), "Playing...");
    state_unlock();

    request_preview(idx);
}

static void select_btn_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    animate_button_bounce(lv_event_get_target_obj(e), 5);

    state_lock();
    if (get_filtered_song_count(s_age_filter) == 0U) {
        copy_text_safe(s_status_text, sizeof(s_status_text), "Pick another age range.");
        state_unlock();
        gui_refresh_from_state();
        return;
    }
    s_config_valid = true;
    copy_text_safe(s_status_text, sizeof(s_status_text), "Song selected!");
    push_action(MUSIC_UI_ACTION_SONG_SELECTED);
    state_unlock();
    gui_refresh_from_state();
}

static lv_obj_t *make_btn(lv_obj_t *parent,
                          const char *text,
                          lv_coord_t w,
                          lv_coord_t h,
                          lv_event_cb_t cb,
                          uint32_t bg_hex)
{
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

    {
        lv_obj_t *label = lv_label_create(btn);
        lv_label_set_text(label, text);
        lv_obj_center(label);
    }
    return btn;
}

static lv_obj_t *create_song_screen(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_set_style_radius(scr, 0, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x12203A), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    create_battery_indicator(scr, &s_song_battery);
    refresh_battery_indicators();

    {
        lv_obj_t *title = lv_label_create(scr);
        lv_label_set_text(title, LV_SYMBOL_AUDIO " Music Maker");
        lv_obj_set_style_text_color(title, lv_color_hex(0xFDE68A), 0);
        lv_obj_set_style_bg_color(title, lv_color_hex(0x0B1220), 0);
        lv_obj_set_style_bg_opa(title, LV_OPA_50, 0);
        lv_obj_set_style_pad_hor(title, 6, 0);
        lv_obj_set_style_pad_ver(title, 2, 0);
        lv_obj_set_style_radius(title, 6, 0);
        lv_obj_align(title, LV_ALIGN_TOP_LEFT, 12, 10);
    }

    s_song_num_label = lv_label_create(scr);
    lv_label_set_text(s_song_num_label, "Song 1 of 1");
    lv_obj_set_style_text_color(s_song_num_label, lv_color_hex(0xBBD0FF), 0);
    lv_obj_set_style_bg_color(s_song_num_label, lv_color_hex(0x0B1220), 0);
    lv_obj_set_style_bg_opa(s_song_num_label, LV_OPA_30, 0);
    lv_obj_set_style_pad_hor(s_song_num_label, 6, 0);
    lv_obj_set_style_pad_ver(s_song_num_label, 2, 0);
    lv_obj_set_style_radius(s_song_num_label, 6, 0);
    lv_obj_align(s_song_num_label, LV_ALIGN_TOP_MID, 0, 74);

    {
        lv_obj_t *age_prev_btn = make_btn(scr, LV_SYMBOL_LEFT, 38, 34, age_prev_btn_cb, 0xFDE68A);
        lv_obj_t *age_next_btn = make_btn(scr, LV_SYMBOL_RIGHT, 38, 34, age_next_btn_cb, 0xFDE68A);
        lv_obj_set_pos(age_prev_btn, 16, 38);
        lv_obj_set_pos(age_next_btn, 186, 38);
    }

    s_age_filter_label = lv_label_create(scr);
    lv_obj_set_width(s_age_filter_label, 120);
    lv_obj_set_style_text_align(s_age_filter_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(s_age_filter_label, "All Ages");
    lv_obj_set_style_text_color(s_age_filter_label, lv_color_hex(0xFDE68A), 0);
    lv_obj_set_style_bg_color(s_age_filter_label, lv_color_hex(0x0B1220), 0);
    lv_obj_set_style_bg_opa(s_age_filter_label, LV_OPA_40, 0);
    lv_obj_set_style_pad_hor(s_age_filter_label, 8, 0);
    lv_obj_set_style_pad_ver(s_age_filter_label, 5, 0);
    lv_obj_set_style_radius(s_age_filter_label, 8, 0);
    lv_obj_align(s_age_filter_label, LV_ALIGN_TOP_MID, 0, 40);

    {
        lv_obj_t *card = lv_obj_create(scr);
        lv_obj_set_size(card, 220, 82);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_color(card, lv_color_hex(0x243B63), 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(card, 2, 0);
        lv_obj_set_style_border_color(card, lv_color_hex(0x88A8FF), 0);
        lv_obj_set_style_radius(card, 14, 0);
        lv_obj_set_style_pad_all(card, 0, 0);
        lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 102);

        s_song_name_label = lv_label_create(card);
        lv_obj_set_width(s_song_name_label, 200);
        lv_label_set_long_mode(s_song_name_label, LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_align(s_song_name_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(s_song_name_label, "Baby Shark");
        lv_obj_set_style_text_color(s_song_name_label, lv_color_hex(0xFFFFFF), 0);
        lv_obj_align(s_song_name_label, LV_ALIGN_TOP_MID, 0, 14);

        s_song_age_label = lv_label_create(card);
        lv_label_set_text(s_song_age_label, "Ages 2-4");
        lv_obj_set_style_text_color(s_song_age_label, lv_color_hex(0xC7D2FE), 0);
        lv_obj_set_style_bg_color(s_song_age_label, lv_color_hex(0x13233F), 0);
        lv_obj_set_style_bg_opa(s_song_age_label, LV_OPA_60, 0);
        lv_obj_set_style_pad_hor(s_song_age_label, 8, 0);
        lv_obj_set_style_pad_ver(s_song_age_label, 3, 0);
        lv_obj_set_style_radius(s_song_age_label, 10, 0);
        lv_obj_align(s_song_age_label, LV_ALIGN_BOTTOM_MID, 0, -10);
    }

    {
        lv_obj_t *left_btn = make_btn(scr, LV_SYMBOL_LEFT, 44, 44, prev_btn_cb, 0xA5D8FF);
        lv_obj_set_pos(left_btn, 10, 120);

        lv_obj_t *right_btn = make_btn(scr, LV_SYMBOL_RIGHT, 44, 44, next_btn_cb, 0xA5D8FF);
        lv_obj_set_pos(right_btn, 186, 120);
    }

    {
        lv_obj_t *play_btn = make_btn(scr, LV_SYMBOL_PLAY " Play", 108, 48, play_btn_cb, 0x93C5FD);
        lv_obj_t *select_btn = make_btn(scr, LV_SYMBOL_OK " Select", 108, 48, select_btn_cb, 0x86EFAC);
        lv_obj_set_pos(play_btn, 10, 196);
        lv_obj_set_pos(select_btn, 124, 196);
    }

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

    gui_refresh_from_state();
    return scr;
}

static void start_btn_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    animate_button_bounce(lv_event_get_target_obj(e), 8);
    open_song_screen();
}

static lv_obj_t *create_intro_screen(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_set_style_radius(scr, 0, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0F0F23), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    create_battery_indicator(scr, &s_intro_battery);
    refresh_battery_indicators();

    {
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
    }

    {
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

        {
            lv_obj_t *btn_label = lv_label_create(btn);
            lv_label_set_text(btn_label, "START");
            lv_obj_set_style_text_color(btn_label, lv_color_hex(0x0F0F23), 0);
            lv_obj_center(btn_label);
        }
    }

    return scr;
}

static void open_song_screen(void)
{
    state_lock();
    s_age_filter = MUSIC_AGE_RANGE_ALL;
    if (!get_filtered_song_index(s_age_filter, 0, &s_song_index)) {
        s_song_index = 0;
    }
    s_config_valid = false;
    copy_text_safe(s_status_text, sizeof(s_status_text), "Pick a song and tap Play!");
    state_unlock();

    if (s_song_screen == NULL) {
        s_song_screen = create_song_screen();
    } else {
        gui_refresh_from_state();
    }

    lv_screen_load_anim(s_song_screen, LV_SCR_LOAD_ANIM_MOVE_LEFT, 250, 0, false);
}

static void gui_refresh_from_state(void)
{
    uint8_t idx;
    bool is_playing;
    music_age_range_t age_filter;
    char status_buf[sizeof(s_status_text)];
    size_t count;
    size_t filtered_position = 0;

    if (s_song_screen == NULL) {
        return;
    }

    state_lock();
    idx = s_song_index;
    is_playing = s_is_playing;
    age_filter = s_age_filter;
    copy_text_safe(status_buf, sizeof(status_buf), s_status_text);
    state_unlock();

    count = get_filtered_song_count(age_filter);
    if (count > 0U && !song_matches_filter(idx, age_filter)) {
        (void)get_filtered_song_index(age_filter, 0, &idx);
    }

    if (count > 0U) {
        filtered_position = get_filtered_song_position(idx, age_filter);
    }

    if (s_age_filter_label != NULL) {
        lv_label_set_text(s_age_filter_label, speaker_get_age_range_label(age_filter));
    }
    if (s_song_name_label != NULL) {
        if (count == 0U) {
            lv_label_set_text(s_song_name_label, "No songs in this range");
        } else {
            lv_label_set_text(s_song_name_label, speaker_get_song_name(idx));
        }
    }
    if (s_song_age_label != NULL) {
        if (age_filter != MUSIC_AGE_RANGE_ALL) {
            lv_label_set_text(s_song_age_label, "");
            lv_obj_set_size(s_song_age_label, 0, 0);
            lv_obj_add_flag(s_song_age_label, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(s_song_age_label, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_size(s_song_age_label, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            if (count == 0U) {
                lv_label_set_text(s_song_age_label, speaker_get_age_range_label(age_filter));
            } else {
                lv_label_set_text(s_song_age_label,
                                  speaker_get_age_range_label(speaker_get_song_age_range(idx)));
            }
        }
    }
    if (s_song_num_label != NULL) {
        char num_text[32];
        if (count == 0U) {
            (void)snprintf(num_text, sizeof(num_text), "0 songs in filter");
        } else {
            (void)snprintf(num_text, sizeof(num_text), "Song %u of %u",
                           (unsigned)(filtered_position + 1U), (unsigned)count);
        }
        lv_label_set_text(s_song_num_label, num_text);
    }
    if (s_status_label != NULL) {
        lv_label_set_text(s_status_label, status_buf);
    }

    if (s_status_label != NULL && is_playing) {
        lv_obj_set_style_text_color(s_status_label, lv_color_hex(0x93C5FD), 0);
    } else if (s_status_label != NULL) {
        lv_obj_set_style_text_color(s_status_label, lv_color_hex(0xFDE68A), 0);
    }
}

#if !defined(MUSIC_SEQ_UI_SIMULATOR)
static void preview_task(void *arg)
{
    (void)arg;
    uint8_t idx;

    while (1) {
        if (xQueueReceive(s_preview_queue, &idx, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        state_lock();
        s_is_playing = true;
        state_unlock();

        {
            esp_err_t err = speaker_play_song(idx);

            state_lock();
            s_is_playing = false;
            if (err == ESP_OK) {
                copy_text_safe(s_status_text, sizeof(s_status_text), "Done! Tap Select to confirm.");
            } else {
                copy_text_safe(s_status_text, sizeof(s_status_text), "Playback error.");
            }
            state_unlock();
        }
    }
}

static void lvgl_task(void *arg)
{
    (void)arg;
    TickType_t last_battery_refresh = 0;
    ESP_LOGI(TAG, "LVGL task started");

    if (s_intro_screen == NULL) {
        s_intro_screen = create_intro_screen();
    }
    lv_screen_load(s_intro_screen);
    refresh_battery_indicators();
    last_battery_refresh = xTaskGetTickCount();

    while (1) {
        TickType_t now = xTaskGetTickCount();
        uint32_t delay_ms;

        gui_refresh_from_state();
        if ((now - last_battery_refresh) >= pdMS_TO_TICKS(BATTERY_REFRESH_MS)) {
            refresh_battery_indicators();
            last_battery_refresh = now;
        }
        delay_ms = lv_timer_handler();
        if (delay_ms < 5U) {
            delay_ms = 5U;
        } else if (delay_ms > 30U) {
            delay_ms = 30U;
        }
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}
#endif

esp_err_t tft_ui_start(void)
{
    if (s_ui_started) {
        ESP_LOGW(TAG, "TFT UI already started");
        return ESP_OK;
    }

    s_age_filter = MUSIC_AGE_RANGE_ALL;
    s_song_index = 0;
    (void)get_filtered_song_index(s_age_filter, 0, &s_song_index);
    s_config_valid = false;
    s_is_playing = false;
    copy_text_safe(s_status_text, sizeof(s_status_text), "Pick a song and tap Play!");

#if defined(MUSIC_SEQ_UI_SIMULATOR)
    memset(&s_action_queue, 0, sizeof(s_action_queue));
    if (s_intro_screen == NULL) {
        s_intro_screen = create_intro_screen();
    }
    lv_screen_load(s_intro_screen);
    s_ui_started = true;
    ESP_LOGI(TAG, "Music sequence simulator UI ready");
    return ESP_OK;
#else
    ESP_LOGI(TAG, "Starting LVGL v9 TFT UI");

    s_state_mutex = xSemaphoreCreateMutex();
    if (s_state_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    s_action_queue  = xQueueCreate(UI_ACTION_QUEUE_LEN, sizeof(music_ui_action_t));
    s_preview_queue = xQueueCreate(UI_PREVIEW_QUEUE_LEN, sizeof(uint8_t));
    if (s_action_queue == NULL || s_preview_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create UI queues");
        return ESP_ERR_NO_MEM;
    }

    {
        gpio_config_t bk_cfg = {
            .pin_bit_mask = 1ULL << PIN_NUM_BK_LIGHT,
            .mode = GPIO_MODE_OUTPUT,
        };
        ESP_ERROR_CHECK(gpio_config(&bk_cfg));
        gpio_set_level(PIN_NUM_BK_LIGHT, !TFT_BACKLIGHT_ON_LEVEL);
    }

    {
        spi_bus_config_t buscfg = {
            .sclk_io_num = PIN_NUM_SCLK,
            .mosi_io_num = PIN_NUM_MOSI,
            .miso_io_num = PIN_NUM_MISO,
            .quadwp_io_num = -1,
            .quadhd_io_num = -1,
            .max_transfer_sz = TFT_H_RES * TFT_DRAW_BUF_LINES * sizeof(uint16_t),
        };
        ESP_ERROR_CHECK(spi_bus_initialize(TFT_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    {
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

        {
            esp_lcd_panel_dev_config_t panel_cfg = {
                .reset_gpio_num = PIN_NUM_LCD_RST,
                .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
                .bits_per_pixel = 16,
            };
            ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(io_handle, &panel_cfg, &s_panel_handle));
        }

        ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel_handle));
        ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel_handle));
        ESP_ERROR_CHECK(esp_lcd_panel_mirror(s_panel_handle, true, false));
        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel_handle, true));
        gpio_set_level(PIN_NUM_BK_LIGHT, TFT_BACKLIGHT_ON_LEVEL);

        lv_init();
        s_display = lv_display_create(TFT_H_RES, TFT_V_RES);
        assert(s_display != NULL);

        {
            const size_t draw_buf_sz = TFT_H_RES * TFT_DRAW_BUF_LINES * sizeof(lv_color16_t);
            void *buf1 = spi_bus_dma_memory_alloc(TFT_SPI_HOST, draw_buf_sz, 0);
            void *buf2 = spi_bus_dma_memory_alloc(TFT_SPI_HOST, draw_buf_sz, 0);
            assert(buf1 != NULL);
            assert(buf2 != NULL);

            lv_display_set_buffers(s_display, buf1, buf2, draw_buf_sz,
                                   LV_DISPLAY_RENDER_MODE_PARTIAL);
        }

        lv_display_set_user_data(s_display, s_panel_handle);
        lv_display_set_color_format(s_display, LV_COLOR_FORMAT_RGB565);
        lv_display_set_flush_cb(s_display, lcd_flush_cb);

        {
            const esp_lcd_panel_io_callbacks_t io_callbacks = {
                .on_color_trans_done = lcd_flush_ready_cb,
            };
            ESP_ERROR_CHECK(esp_lcd_panel_io_register_event_callbacks(
                io_handle, &io_callbacks, s_display));
        }

        {
            esp_lcd_panel_io_handle_t tp_io_handle = NULL;
            esp_lcd_panel_io_spi_config_t tp_io_cfg =
                ESP_LCD_TOUCH_IO_SPI_XPT2046_CONFIG(PIN_NUM_TOUCH_CS);
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

            ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(
                (esp_lcd_spi_bus_handle_t)TFT_SPI_HOST, &tp_io_cfg, &tp_io_handle));
            ESP_ERROR_CHECK(esp_lcd_touch_new_spi_xpt2046(tp_io_handle, &tp_cfg,
                                                          &s_touch_handle));
        }
    }

    {
        lv_indev_t *indev = lv_indev_create();
        lv_timer_t *read_timer;
        assert(indev != NULL);

        lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_display(indev, s_display);
        lv_indev_set_user_data(indev, s_touch_handle);
        lv_indev_set_long_press_time(indev, 800);
        lv_indev_set_read_cb(indev, touch_read_cb);

        read_timer = lv_indev_get_read_timer(indev);
        if (read_timer != NULL) {
            lv_timer_set_period(read_timer, 10);
        }
    }

    {
        const esp_timer_create_args_t tick_args = {
            .callback = lvgl_tick_cb,
            .name = "lvgl_tick",
        };
        ESP_ERROR_CHECK(esp_timer_create(&tick_args, &s_lvgl_tick_timer));
        ESP_ERROR_CHECK(esp_timer_start_periodic(s_lvgl_tick_timer,
                                                 TFT_TICK_PERIOD_MS * 1000));
    }

    {
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
    }

    s_ui_started = true;
    ESP_LOGI(TAG, "LVGL/UI core=%d, preview/audio core=%d", MUSIC_UI_CORE_ID, MUSIC_AUDIO_CORE_ID);
    return ESP_OK;
#endif
}

bool tft_ui_take_action(music_ui_action_t *out_action, uint32_t timeout_ms)
{
    (void)timeout_ms;

    if (out_action == NULL) {
        return false;
    }

#if defined(MUSIC_SEQ_UI_SIMULATOR)
    if (s_action_queue.count == 0U) {
        return false;
    }

    *out_action = s_action_queue.items[s_action_queue.head];
    s_action_queue.head = (uint8_t)((s_action_queue.head + 1U) % UI_ACTION_QUEUE_LEN);
    s_action_queue.count--;
    return true;
#else
    if (s_action_queue == NULL) {
        return false;
    }
    return xQueueReceive(s_action_queue, out_action, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
#endif
}

void tft_ui_set_playback_state(const music_playback_state_t *state)
{
    if (state == NULL) {
        return;
    }

    state_lock();
    s_is_playing = state->is_playing;
    state_unlock();
#if defined(MUSIC_SEQ_UI_SIMULATOR)
    gui_refresh_from_state();
#endif
}

void tft_ui_set_status_message(const char *msg)
{
    state_lock();
    copy_text_safe(s_status_text, sizeof(s_status_text), msg);
    state_unlock();
#if defined(MUSIC_SEQ_UI_SIMULATOR)
    gui_refresh_from_state();
#endif
}
