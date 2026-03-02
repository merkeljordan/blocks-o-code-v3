#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"

#include "lvgl.h"
#include "esp_lcd_ili9341.h"
#include "esp_lcd_touch_xpt2046.h"

#include "tft_ui.h"

/*
 * Brain Block LVGL v9 display/touch bring-up + basic home screen
 * ---------------------------------------------------------------
 * This file has two responsibilities:
 * 1) Hardware/UI integration:
 *    - SPI bus
 *    - ILI9341 display panel
 *    - XPT2046 touch controller
 *    - LVGL display + input registration
 *    - LVGL tick + LVGL task loop
 * 2) UI construction:
 *    - A simple home screen using LVGL v9 objects, styles, flex layout, events, and timers
 *
 * If you are learning the flow, read in this order:
 *   - tft_ui_start()              (startup sequence / wiring)
 *   - lcd_flush_cb()              (how LVGL renders to the panel)
 *   - touch_read_cb()             (how touch input reaches LVGL)
 *   - lvgl_task()                 (LVGL main loop)
 *   - create_home_screen()        (screen UI layout and widgets)
 */
static const char *TAG = "brain_ui_v9";

/* Hardware + LVGL runtime configuration
 * These values mirror your current board wiring and panel size.
 * Keep these grouped here so the bring-up code below stays readable. */
#define BRAIN_LCD_HOST                 SPI3_HOST
#define BRAIN_LCD_PIXEL_CLOCK_HZ       (20 * 1000 * 1000)
#define BRAIN_LCD_CMD_BITS             8
#define BRAIN_LCD_PARAM_BITS           8
#define BRAIN_LCD_H_RES                240
#define BRAIN_LCD_V_RES                320
#define BRAIN_LVGL_DRAW_BUF_LINES      20
#define BRAIN_LVGL_TICK_PERIOD_MS      2
#define BRAIN_LVGL_TASK_STACK_SIZE     (6 * 1024)
#define BRAIN_LVGL_TASK_PRIORITY       5
#define BRAIN_LCD_BACKLIGHT_ON_LEVEL   1

#define PIN_NUM_BK_LIGHT               32
#define PIN_NUM_SCLK                   18
#define PIN_NUM_MOSI                   23
#define PIN_NUM_MISO                   19
#define PIN_NUM_LCD_DC                 14
#define PIN_NUM_LCD_RST                4
#define PIN_NUM_LCD_CS                 27
#define PIN_NUM_TOUCH_CS               26
#define PIN_NUM_TOUCH_IRQ              36

/* Kid-friendly UI palette (semantic color meanings help kids learn the UI fast)
 * blue   = info/navigation/brain
 * green  = go/success/connected
 * yellow = activity/scan/music energy
 * purple = settings/fun
 * pink   = alerts/attention */
#define UI_COLOR_BG_TOP                lv_color_hex(0xCFEFFF)
#define UI_COLOR_BG_BOTTOM             lv_color_hex(0xFFE7A8)
#define UI_COLOR_SURFACE               lv_color_hex(0xFFFDF8)
#define UI_COLOR_HEADER                lv_color_hex(0x2A4B9B)
#define UI_COLOR_TEXT_PRIMARY          lv_color_hex(0x1F2542)
#define UI_COLOR_TEXT_SECONDARY        lv_color_hex(0x5F6787)
#define UI_COLOR_BORDER_SOFT           lv_color_hex(0xDDE5F4)
#define UI_COLOR_CARD_BG               lv_color_hex(0xFFFFFF)
#define UI_COLOR_HERO_BG               lv_color_hex(0xEAF7FF)
#define UI_COLOR_STATUS_BG             lv_color_hex(0xFFFFFF)
#define UI_COLOR_SUCCESS               lv_color_hex(0x42C98A)
#define UI_COLOR_INFO                  lv_color_hex(0x5AA8FF)
#define UI_COLOR_FUN                   lv_color_hex(0xD07BFF)
#define UI_COLOR_WARN                  lv_color_hex(0xFFB347)
#define UI_COLOR_ALERT                 lv_color_hex(0xFF7DAE)

/* Touch debug + transform presets
 * Presets let us test different mirror/swap combinations at runtime without reflashing. */
#define TOUCH_PRESET_DEFAULT_INDEX     1U
#define TOUCH_DEBUG_REFRESH_MS         75U
/* Software touch normalization helps when edge hits are compressed (common on XPT2046).
 * These are safe defaults; adjust if tester still shows corner drift. */
#define TOUCH_SW_CAL_ENABLE            1
#define TOUCH_SW_CAL_MIN_X             10
#define TOUCH_SW_CAL_MAX_X             (BRAIN_LCD_H_RES - 10)
#define TOUCH_SW_CAL_MIN_Y             12
#define TOUCH_SW_CAL_MAX_Y             (BRAIN_LCD_V_RES - 12)
#define HOME_ACTION_DEBOUNCE_US        150000

typedef struct {
    const char *name;
    uint16_t x_max;
    uint16_t y_max;
    bool swap_xy;
    bool mirror_x;
    bool mirror_y;
} touch_transform_preset_t;

typedef struct {
    bool pressed;
    uint16_t raw_x;
    uint16_t raw_y;
    uint16_t x;
    uint16_t y;
    uint16_t strength;
    uint8_t point_count;
    esp_err_t last_read_err;
    uint32_t read_count;
    uint32_t pressed_count;   /* transition count (release -> press) */
    uint32_t released_count;  /* transition count (press -> release) */
    uint64_t last_update_us;
    uint8_t transform_preset_index;
} touch_debug_state_t;

static const touch_transform_preset_t s_touch_presets[] = {
    { "P0 Base",        BRAIN_LCD_H_RES, BRAIN_LCD_V_RES, true,  false, true  },
    { "P1 Recommended", BRAIN_LCD_V_RES, BRAIN_LCD_H_RES, true,  true,  false },
    { "P2 S1 Mx0 My1",  BRAIN_LCD_V_RES, BRAIN_LCD_H_RES, true,  false, true  },
    { "P3 S1 Mx1 My1",  BRAIN_LCD_V_RES, BRAIN_LCD_H_RES, true,  true,  true  },
    { "P4 S0 Mx0 My0",  BRAIN_LCD_H_RES, BRAIN_LCD_V_RES, false, false, false },
    { "P5 S0 Mx1 My0",  BRAIN_LCD_H_RES, BRAIN_LCD_V_RES, false, true,  false },
    { "P6 S0 Mx0 My1",  BRAIN_LCD_H_RES, BRAIN_LCD_V_RES, false, false, true  },
    { "P7 S0 Mx1 My1",  BRAIN_LCD_H_RES, BRAIN_LCD_V_RES, false, true,  true  },
};

static const uint8_t s_touch_preset_count = (uint8_t)(sizeof(s_touch_presets) / sizeof(s_touch_presets[0]));

/* Static handles/state
 * We store these so callbacks/timers can access the display and UI objects later.
 * This is common in embedded LVGL apps where there is one global UI instance. */
static bool s_ui_started = false;
static lv_display_t *s_display = NULL;
static esp_lcd_panel_handle_t s_panel_handle = NULL;
static esp_lcd_touch_handle_t s_touch_handle = NULL;
static esp_timer_handle_t s_lvgl_tick_timer = NULL;
static lv_obj_t *s_home_screen = NULL;
static lv_obj_t *s_blocks_screen = NULL;
static lv_obj_t *s_touch_tester_screen = NULL;
static lv_obj_t *s_time_label = NULL;
static lv_obj_t *s_status_label = NULL;
static lv_obj_t *s_blocks_status_label = NULL;
static lv_timer_t *s_uptime_timer = NULL;
static lv_timer_t *s_touch_debug_timer = NULL;

/* Home-screen touch diagnostics overlay (visible by default during touch bring-up) */
static lv_obj_t *s_touch_overlay_panel = NULL;
static lv_obj_t *s_touch_overlay_label = NULL;
static lv_obj_t *s_home_touch_marker = NULL;

/* Full-screen touch tester mode objects */
static lv_obj_t *s_touch_tester_label = NULL;
static lv_obj_t *s_touch_tester_preset_label = NULL;
static lv_obj_t *s_touch_tester_marker = NULL;

static touch_debug_state_t s_touch_debug = {
    .last_read_err = ESP_OK,
    .transform_preset_index = TOUCH_PRESET_DEFAULT_INDEX,
};
static bool s_touch_prev_pressed = false;
static int64_t s_last_home_action_us = 0;
static const char *s_last_home_action_name = NULL;

/* Forward declarations for touch debug/tester helpers */
static void touch_debug_ui_timer_cb(lv_timer_t *timer);
static lv_obj_t *create_touch_marker(lv_obj_t *parent, lv_color_t color);
static void update_touch_debug_ui(void);
static void apply_touch_transform_preset(uint8_t preset_index);
static void cycle_touch_transform_preset(void);
static void touch_next_preset_event_cb(lv_event_t *e);
static void header_title_long_press_cb(lv_event_t *e);
static void touch_tester_back_event_cb(lv_event_t *e);
static void open_touch_tester_screen(void);
static lv_obj_t *create_touch_tester_screen(void);
static void create_home_touch_debug_overlay(lv_obj_t *home_screen_root);
static void open_blocks_screen(void);
static lv_obj_t *create_blocks_screen(void);
static void blocks_back_event_cb(lv_event_t *e);
static void block_card_event_cb(lv_event_t *e);

/* ESP timer callback that feeds LVGL's internal timing system.
 * LVGL uses this timing for animations, input repeat, timers, etc. */
static void brain_lvgl_tick_cb(void *arg)
{
    (void)arg;
    lv_tick_inc(BRAIN_LVGL_TICK_PERIOD_MS);
}

/* Called by esp_lcd when an asynchronous SPI color transfer is complete.
 * LVGL waits for this signal before reusing the draw buffer. */
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

/* LVGL -> display flush callback
 * LVGL gives us a rectangle (area) and a pixel buffer (px_map). We must copy that
 * pixel data to the physical LCD. This is the core render bridge between LVGL and ILI9341. */
static void lcd_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t)lv_display_get_user_data(disp);

    const int x1 = area->x1;
    const int y1 = area->y1;
    const int x2 = area->x2;
    const int y2 = area->y2;

    /* The panel expects RGB565 bytes in big-endian order on SPI.
     * LVGL's software renderer fills the buffer in host order, so we swap before sending. */
    lv_draw_sw_rgb565_swap(px_map, (x2 + 1 - x1) * (y2 + 1 - y1));

    /* esp_lcd uses end-exclusive coordinates (x2+1, y2+1). */
    esp_lcd_panel_draw_bitmap(panel_handle, x1, y1, x2 + 1, y2 + 1, px_map);
}

static uint16_t map_touch_axis(uint16_t value, uint16_t in_min, uint16_t in_max, uint16_t out_max_exclusive)
{
    if (out_max_exclusive == 0 || in_max <= in_min) {
        return 0;
    }
    if (value <= in_min) {
        return 0;
    }
    if (value >= in_max) {
        return (uint16_t)(out_max_exclusive - 1U);
    }

    const uint32_t num = (uint32_t)(value - in_min) * (uint32_t)(out_max_exclusive - 1U);
    const uint32_t den = (uint32_t)(in_max - in_min);
    return (uint16_t)(num / den);
}

static void normalize_touch_point(uint16_t raw_x, uint16_t raw_y, uint16_t *out_x, uint16_t *out_y)
{
#if TOUCH_SW_CAL_ENABLE
    *out_x = map_touch_axis(raw_x, TOUCH_SW_CAL_MIN_X, TOUCH_SW_CAL_MAX_X, BRAIN_LCD_H_RES);
    *out_y = map_touch_axis(raw_y, TOUCH_SW_CAL_MIN_Y, TOUCH_SW_CAL_MAX_Y, BRAIN_LCD_V_RES);
#else
    *out_x = raw_x;
    *out_y = raw_y;
#endif
}

/* LVGL input read callback (touch -> LVGL pointer events)
 * LVGL polls this function regularly from lv_timer_handler().
 * We read the latest touch controller state and translate it into LVGL's format. */
static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    esp_lcd_touch_handle_t tp = (esp_lcd_touch_handle_t)lv_indev_get_user_data(indev);
    uint16_t x[1] = {0};
    uint16_t y[1] = {0};
    uint16_t strength[1] = {0};
    uint8_t point_cnt = 0;

    data->state = LV_INDEV_STATE_RELEASED;
    data->continue_reading = false;

    s_touch_debug.read_count++;
    s_touch_debug.last_update_us = (uint64_t)esp_timer_get_time();

    if (tp == NULL) {
        s_touch_debug.last_read_err = ESP_ERR_INVALID_STATE;
        s_touch_debug.raw_x = 0;
        s_touch_debug.raw_y = 0;
        s_touch_debug.point_count = 0;
        s_touch_debug.pressed = false;
        return;
    }

    esp_err_t read_err = esp_lcd_touch_read_data(tp);
    s_touch_debug.last_read_err = read_err;
    if (read_err != ESP_OK) {
        s_touch_debug.raw_x = 0;
        s_touch_debug.raw_y = 0;
        s_touch_debug.point_count = 0;
        s_touch_debug.pressed = false;
        if (s_touch_prev_pressed) {
            s_touch_debug.released_count++;
            s_touch_prev_pressed = false;
        }
        return;
    }

    bool touched = esp_lcd_touch_get_coordinates(tp, x, y, strength, &point_cnt, 1);
    s_touch_debug.last_read_err = ESP_OK;
    s_touch_debug.point_count = touched ? point_cnt : 0;

    if (touched && point_cnt > 0) {
        uint16_t norm_x = x[0];
        uint16_t norm_y = y[0];
        normalize_touch_point(x[0], y[0], &norm_x, &norm_y);

        s_touch_debug.raw_x = x[0];
        s_touch_debug.raw_y = y[0];
        s_touch_debug.x = norm_x;
        s_touch_debug.y = norm_y;
        s_touch_debug.strength = strength[0];
        s_touch_debug.pressed = true;

        data->point.x = norm_x;
        data->point.y = norm_y;
        data->state = LV_INDEV_STATE_PRESSED;

        if (!s_touch_prev_pressed) {
            s_touch_debug.pressed_count++;
            s_touch_prev_pressed = true;
        }
    } else {
        s_touch_debug.raw_x = 0;
        s_touch_debug.raw_y = 0;
        s_touch_debug.strength = 0;
        s_touch_debug.pressed = false;
        if (s_touch_prev_pressed) {
            s_touch_debug.released_count++;
            s_touch_prev_pressed = false;
        }
    }
}

/* Dedicated LVGL task
 * Rule of thumb: keep all LVGL object manipulation in one task/thread.
 * lv_timer_handler() runs LVGL's internal work (drawing, animations, events, timers)
 * and returns how long we can sleep until it should run again. */
static void lvgl_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "LVGL task started");

    while (1) {
        uint32_t delay_ms = lv_timer_handler();
        /* Clamp sleep to avoid both busy-looping and sleeping too long.
         * Keep max poll interval tight for resistive touch responsiveness. */
        if (delay_ms < 5) {
            delay_ms = 5;
        } else if (delay_ms > 30) {
            delay_ms = 30;
        }
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

/* Touch debug helpers ----------------------------------------------------- */

static lv_obj_t *create_touch_marker(lv_obj_t *parent, lv_color_t color)
{
    lv_obj_t *marker = lv_obj_create(parent);
    lv_obj_set_size(marker, 12, 12);
    lv_obj_clear_flag(marker, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(marker, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_radius(marker, 6, 0);
    lv_obj_set_style_border_width(marker, 2, 0);
    lv_obj_set_style_border_color(marker, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_color(marker, color, 0);
    lv_obj_set_style_bg_opa(marker, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_width(marker, 8, 0);
    lv_obj_set_style_shadow_opa(marker, LV_OPA_30, 0);
    lv_obj_set_style_shadow_color(marker, color, 0);
    lv_obj_move_foreground(marker);
    return marker;
}

static void set_touch_marker_position(lv_obj_t *marker, bool visible, uint16_t x, uint16_t y)
{
    if (marker == NULL) {
        return;
    }

    if (!visible) {
        lv_obj_add_flag(marker, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    int x_pos = (int)x - 6;
    int y_pos = (int)y - 6;

    if (x_pos < 0) x_pos = 0;
    if (y_pos < 0) y_pos = 0;
    if (x_pos > (BRAIN_LCD_H_RES - 12)) x_pos = BRAIN_LCD_H_RES - 12;
    if (y_pos > (BRAIN_LCD_V_RES - 12)) y_pos = BRAIN_LCD_V_RES - 12;

    lv_obj_set_pos(marker, x_pos, y_pos);
    lv_obj_clear_flag(marker, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(marker);
}

static void apply_touch_transform_preset(uint8_t preset_index)
{
    if (s_touch_handle == NULL || s_touch_preset_count == 0) {
        return;
    }

    if (preset_index >= s_touch_preset_count) {
        preset_index = 0;
    }

    const touch_transform_preset_t *p = &s_touch_presets[preset_index];
    s_touch_debug.transform_preset_index = preset_index;

    /* esp_lcd_touch exposes the touch handle struct publicly, so we can update x/y limits in-place.
     * mirror/swap use the helper setters to keep driver state consistent. */
    s_touch_handle->config.x_max = p->x_max;
    s_touch_handle->config.y_max = p->y_max;

    esp_err_t err_swap = esp_lcd_touch_set_swap_xy(s_touch_handle, p->swap_xy);
    esp_err_t err_mx = esp_lcd_touch_set_mirror_x(s_touch_handle, p->mirror_x);
    esp_err_t err_my = esp_lcd_touch_set_mirror_y(s_touch_handle, p->mirror_y);

    if (err_swap != ESP_OK || err_mx != ESP_OK || err_my != ESP_OK) {
        ESP_LOGW(TAG, "Touch preset apply warnings swap=%s mx=%s my=%s",
                 esp_err_to_name(err_swap), esp_err_to_name(err_mx), esp_err_to_name(err_my));
    }

    ESP_LOGI(TAG, "Touch preset %u applied: %s (x_max=%u y_max=%u swap=%d mx=%d my=%d)",
             (unsigned)preset_index, p->name, p->x_max, p->y_max,
             (int)p->swap_xy, (int)p->mirror_x, (int)p->mirror_y);

    update_touch_debug_ui();
}

static void cycle_touch_transform_preset(void)
{
    uint8_t next = (uint8_t)((s_touch_debug.transform_preset_index + 1U) % s_touch_preset_count);
    apply_touch_transform_preset(next);

    if (s_status_label != NULL) {
        lv_label_set_text_fmt(s_status_label, "Touch map: %s", s_touch_presets[next].name);
    }
}

static void touch_next_preset_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    cycle_touch_transform_preset();
}

static void format_touch_debug_lines(char *line1, size_t line1_sz,
                                     char *line2, size_t line2_sz)
{
    const touch_transform_preset_t *p = &s_touch_presets[s_touch_debug.transform_preset_index % s_touch_preset_count];
    const char *read_name = esp_err_to_name(s_touch_debug.last_read_err);

    snprintf(line1, line1_sz,
             "Touch:%s  Pts:%u  Z:%u\nXY:%u,%u  Raw:%u,%u",
             s_touch_debug.pressed ? "PRESSED" : "RELEASED",
             (unsigned)s_touch_debug.point_count,
             (unsigned)s_touch_debug.strength,
             (unsigned)s_touch_debug.x,
             (unsigned)s_touch_debug.y,
             (unsigned)s_touch_debug.raw_x,
             (unsigned)s_touch_debug.raw_y);

    snprintf(line2, line2_sz,
             "Preset:%s  Read:%s\nx/y=%u/%u  S%d X%d Y%d  T:%lu R:%lu",
             p->name,
             (s_touch_debug.last_read_err == ESP_OK) ? "OK" : (read_name ? read_name : "ERR"),
             (unsigned)p->x_max, (unsigned)p->y_max,
             (int)p->swap_xy, (int)p->mirror_x, (int)p->mirror_y,
             (unsigned long)s_touch_debug.pressed_count,
             (unsigned long)s_touch_debug.released_count);
}

static void update_touch_debug_ui(void)
{
    char line1[128];
    char line2[128];
    format_touch_debug_lines(line1, sizeof(line1), line2, sizeof(line2));

    if (s_touch_overlay_label != NULL) {
        lv_label_set_text_fmt(s_touch_overlay_label, "%s\n%s", line1, line2);
    }

    if (s_touch_tester_label != NULL) {
        lv_label_set_text(s_touch_tester_label, line1);
    }

    if (s_touch_tester_preset_label != NULL) {
        lv_label_set_text(s_touch_tester_preset_label, line2);
    }

    set_touch_marker_position(s_home_touch_marker, s_touch_debug.pressed, s_touch_debug.x, s_touch_debug.y);
    set_touch_marker_position(s_touch_tester_marker, s_touch_debug.pressed, s_touch_debug.x, s_touch_debug.y);
}

static void touch_debug_ui_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    update_touch_debug_ui();
}

static void touch_tester_back_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    if (s_home_screen != NULL) {
        lv_screen_load_anim(s_home_screen, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 220, 0, false);
    }
}

static void header_title_long_press_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_LONG_PRESSED) {
        return;
    }
    open_touch_tester_screen();
}

static lv_obj_t *create_touch_tester_screen(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_set_style_radius(scr, 0, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0xEEF6FF), 0);
    lv_obj_set_style_bg_grad_color(scr, lv_color_hex(0xFFF7D8), 0);
    lv_obj_set_style_bg_grad_dir(scr, LV_GRAD_DIR_VER, 0);

    lv_obj_t *header = lv_obj_create(scr);
    lv_obj_set_size(header, BRAIN_LCD_H_RES, 36);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_bg_color(header, UI_COLOR_HEADER, 0);
    lv_obj_set_style_pad_all(header, 6, 0);

    lv_obj_t *back_btn = lv_button_create(header);
    lv_obj_set_size(back_btn, 58, 24);
    lv_obj_align(back_btn, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_add_event_cb(back_btn, touch_tester_back_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back_lbl = lv_label_create(back_btn);
    lv_label_set_text(back_lbl, LV_SYMBOL_LEFT " Home");
    lv_obj_center(back_lbl);

    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "Touch Tester");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *debug_panel = lv_obj_create(scr);
    lv_obj_set_size(debug_panel, BRAIN_LCD_H_RES - 12, 82);
    lv_obj_align(debug_panel, LV_ALIGN_TOP_MID, 0, 42);
    lv_obj_clear_flag(debug_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(debug_panel, 12, 0);
    lv_obj_set_style_border_width(debug_panel, 1, 0);
    lv_obj_set_style_border_color(debug_panel, UI_COLOR_BORDER_SOFT, 0);
    lv_obj_set_style_bg_color(debug_panel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_pad_all(debug_panel, 6, 0);

    s_touch_tester_label = lv_label_create(debug_panel);
    lv_obj_set_width(s_touch_tester_label, LV_PCT(100));
    lv_label_set_long_mode(s_touch_tester_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(s_touch_tester_label, UI_COLOR_TEXT_PRIMARY, 0);
    lv_obj_align(s_touch_tester_label, LV_ALIGN_TOP_LEFT, 0, 0);

    s_touch_tester_preset_label = lv_label_create(debug_panel);
    lv_obj_set_width(s_touch_tester_preset_label, LV_PCT(100));
    lv_label_set_long_mode(s_touch_tester_preset_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(s_touch_tester_preset_label, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_align(s_touch_tester_preset_label, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    /* Corner markers make it obvious whether the touch transform matches screen orientation. */
    lv_obj_t *tl = lv_label_create(scr);
    lv_label_set_text(tl, "TL");
    lv_obj_align(tl, LV_ALIGN_TOP_LEFT, 6, 130);
    lv_obj_set_style_text_color(tl, UI_COLOR_TEXT_PRIMARY, 0);

    lv_obj_t *tr = lv_label_create(scr);
    lv_label_set_text(tr, "TR");
    lv_obj_align(tr, LV_ALIGN_TOP_RIGHT, -6, 130);
    lv_obj_set_style_text_color(tr, UI_COLOR_TEXT_PRIMARY, 0);

    lv_obj_t *bl = lv_label_create(scr);
    lv_label_set_text(bl, "BL");
    lv_obj_align(bl, LV_ALIGN_BOTTOM_LEFT, 6, -40);
    lv_obj_set_style_text_color(bl, UI_COLOR_TEXT_PRIMARY, 0);

    lv_obj_t *br = lv_label_create(scr);
    lv_label_set_text(br, "BR");
    lv_obj_align(br, LV_ALIGN_BOTTOM_RIGHT, -6, -40);
    lv_obj_set_style_text_color(br, UI_COLOR_TEXT_PRIMARY, 0);

    lv_obj_t *hint = lv_label_create(scr);
    lv_label_set_text(hint, "Tap corners and center. Use Next Map until the dot matches your finger.");
    lv_obj_set_width(hint, BRAIN_LCD_H_RES - 16);
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -58);
    lv_obj_set_style_text_color(hint, UI_COLOR_TEXT_SECONDARY, 0);

    lv_obj_t *next_btn = lv_button_create(scr);
    lv_obj_set_size(next_btn, 120, 34);
    lv_obj_align(next_btn, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_obj_set_style_bg_color(next_btn, UI_COLOR_WARN, 0);
    lv_obj_set_style_border_color(next_btn, UI_COLOR_WARN, 0);
    lv_obj_add_event_cb(next_btn, touch_next_preset_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *next_lbl = lv_label_create(next_btn);
    lv_label_set_text(next_lbl, "Next Touch Map");
    lv_obj_center(next_lbl);

    s_touch_tester_marker = create_touch_marker(scr, UI_COLOR_ALERT);
    return scr;
}

static void open_touch_tester_screen(void)
{
    if (s_touch_tester_screen == NULL) {
        s_touch_tester_screen = create_touch_tester_screen();
    }
    lv_screen_load_anim(s_touch_tester_screen, LV_SCR_LOAD_ANIM_MOVE_LEFT, 220, 0, false);
    update_touch_debug_ui();
}

static void create_home_touch_debug_overlay(lv_obj_t *home_screen_root)
{
    s_touch_overlay_panel = lv_obj_create(home_screen_root);
    lv_obj_set_size(s_touch_overlay_panel, 176, 112);
    lv_obj_align(s_touch_overlay_panel, LV_ALIGN_TOP_LEFT, 4, 46);
    lv_obj_clear_flag(s_touch_overlay_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(s_touch_overlay_panel, 12, 0);
    lv_obj_set_style_border_width(s_touch_overlay_panel, 1, 0);
    lv_obj_set_style_border_color(s_touch_overlay_panel, UI_COLOR_BORDER_SOFT, 0);
    lv_obj_set_style_bg_color(s_touch_overlay_panel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(s_touch_overlay_panel, LV_OPA_90, 0);
    lv_obj_set_style_pad_all(s_touch_overlay_panel, 6, 0);
    lv_obj_move_foreground(s_touch_overlay_panel);

    s_touch_overlay_label = lv_label_create(s_touch_overlay_panel);
    lv_obj_set_width(s_touch_overlay_label, LV_PCT(100));
    lv_label_set_long_mode(s_touch_overlay_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(s_touch_overlay_label, UI_COLOR_TEXT_PRIMARY, 0);
    lv_obj_align(s_touch_overlay_label, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *hint = lv_label_create(s_touch_overlay_panel);
    lv_label_set_text(hint, "Long-press title for tester");
    lv_obj_set_style_text_color(hint, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    lv_obj_t *next_btn = lv_button_create(s_touch_overlay_panel);
    lv_obj_set_size(next_btn, 84, 24);
    lv_obj_align(next_btn, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(next_btn, UI_COLOR_WARN, 0);
    lv_obj_set_style_border_color(next_btn, UI_COLOR_WARN, 0);
    lv_obj_add_event_cb(next_btn, touch_next_preset_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *next_lbl = lv_label_create(next_btn);
    lv_label_set_text(next_lbl, "Next Map");
    lv_obj_center(next_lbl);

    s_home_touch_marker = create_touch_marker(home_screen_root, UI_COLOR_ALERT);
}

/* Update the small clock/uptime label in the header.
 * This is a good first LVGL v9 pattern: use a timer to refresh UI state. */
static void uptime_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    if (s_time_label == NULL) {
        return;
    }

    uint32_t total_seconds = (uint32_t)(esp_timer_get_time() / 1000000ULL);
    uint32_t hours = (total_seconds / 3600U) % 100U;
    uint32_t minutes = (total_seconds / 60U) % 60U;
    uint32_t seconds = total_seconds % 60U;

    char buf[24];
    snprintf(buf, sizeof(buf), LV_SYMBOL_REFRESH " %02lu:%02lu:%02lu",
             (unsigned long)hours,
             (unsigned long)minutes,
             (unsigned long)seconds);
    lv_label_set_text(s_time_label, buf);
}

/* One event callback can be reused across many buttons.
 * We pass the button name as user_data when wiring the callback. */
static void home_action_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_PRESSED) {
        return;
    }

    const char *action_name = (const char *)lv_event_get_user_data(e);
    if (action_name == NULL) {
        action_name = "Unknown";
    }

    const int64_t now_us = esp_timer_get_time();
    if (s_last_home_action_name == action_name &&
        (now_us - s_last_home_action_us) < HOME_ACTION_DEBOUNCE_US) {
        return;
    }
    s_last_home_action_name = action_name;
    s_last_home_action_us = now_us;

    if (strcmp(action_name, "Blocks") == 0) {
        open_blocks_screen();
        return;
    }

    if (s_status_label) {
        lv_label_set_text_fmt(s_status_label, "You picked: %s", action_name);
    }
}

/* Helper: create a compact status/metric card.
 * Reusable components like this are the fastest way to build nicer UIs. */
static lv_obj_t *create_metric_card(lv_obj_t *parent,
                                    const char *title,
                                    const char *value,
                                    lv_color_t accent_color)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, 104, 66);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, UI_COLOR_BORDER_SOFT, 0);
    lv_obj_set_style_bg_color(card, UI_COLOR_CARD_BG, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(card, 8, 0);

    /* Small accent strip at the top gives each card a visual identity. */
    lv_obj_t *accent = lv_obj_create(card);
    lv_obj_set_size(accent, LV_PCT(100), 4);
    lv_obj_align(accent, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_clear_flag(accent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(accent, 6, 0);
    lv_obj_set_style_border_width(accent, 0, 0);
    lv_obj_set_style_bg_color(accent, accent_color, 0);

    lv_obj_t *title_label = lv_label_create(card);
    lv_label_set_text(title_label, title);
    lv_obj_set_style_text_color(title_label, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_align(title_label, LV_ALIGN_TOP_LEFT, 0, 10);

    lv_obj_t *value_label = lv_label_create(card);
    lv_label_set_text(value_label, value);
    lv_obj_set_style_text_color(value_label, UI_COLOR_TEXT_PRIMARY, 0);
    lv_obj_align(value_label, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    return card;
}

/* Helper: create one quick-action tile with a built-in LVGL symbol + text.
 * Symbols are just strings, so they work nicely inside labels/buttons. */
static lv_obj_t *create_action_tile(lv_obj_t *parent,
                                    const char *symbol_text,
                                    const char *label_text,
                                    lv_color_t accent_color)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, 104, 70);
    lv_obj_set_style_radius(btn, 14, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, accent_color, 0);
    lv_obj_set_style_bg_color(btn, UI_COLOR_CARD_BG, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_width(btn, 10, 0);
    lv_obj_set_style_shadow_opa(btn, LV_OPA_10, 0);
    lv_obj_set_style_shadow_color(btn, UI_COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_shadow_ofs_y(btn, 2, 0);

    /* Use a small internal flex column so icon + text stay aligned even if we resize later. */
    lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_top(btn, 8, 0);
    lv_obj_set_style_pad_bottom(btn, 8, 0);
    lv_obj_set_style_pad_gap(btn, 4, 0);

    /* Small top stripe = easy color cue for kids (green=start, blue=blocks, etc.) */
    lv_obj_t *stripe = lv_obj_create(btn);
    lv_obj_set_size(stripe, LV_PCT(100), 4);
    lv_obj_clear_flag(stripe, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(stripe, 8, 0);
    lv_obj_set_style_border_width(stripe, 0, 0);
    lv_obj_set_style_bg_color(stripe, accent_color, 0);

    lv_obj_t *icon = lv_label_create(btn);
    lv_label_set_text(icon, symbol_text);
    lv_obj_set_style_text_color(icon, accent_color, 0);

    lv_obj_t *text = lv_label_create(btn);
    lv_label_set_text(text, label_text);
    lv_obj_set_style_text_color(text, UI_COLOR_TEXT_PRIMARY, 0);

    lv_obj_add_event_cb(btn, home_action_event_cb, LV_EVENT_PRESSED, (void *)label_text);
    return btn;
}

/* Blocks screen helpers --------------------------------------------------- */

static void blocks_back_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_CLICKED && code != LV_EVENT_PRESSED) {
        return;
    }

    if (s_home_screen != NULL) {
        lv_screen_load_anim(s_home_screen, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 220, 0, false);
    }
}

static void block_card_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    const char *block_name = (const char *)lv_event_get_user_data(e);
    if (block_name == NULL) {
        block_name = "Block";
    }

    if (s_blocks_status_label != NULL) {
        lv_label_set_text_fmt(s_blocks_status_label, "%s selected. Hook real status/actions next.", block_name);
    }
}

static lv_obj_t *create_block_card(lv_obj_t *parent,
                                   const char *symbol_text,
                                   const char *title,
                                   const char *subtitle,
                                   const char *status_text,
                                   lv_color_t accent_color)
{
    lv_obj_t *card = lv_button_create(parent);
    lv_obj_set_size(card, 104, 74);
    lv_obj_set_style_radius(card, 14, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, accent_color, 0);
    lv_obj_set_style_bg_color(card, UI_COLOR_CARD_BG, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_width(card, 10, 0);
    lv_obj_set_style_shadow_opa(card, LV_OPA_10, 0);
    lv_obj_set_style_shadow_color(card, UI_COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_shadow_ofs_y(card, 2, 0);
    lv_obj_set_style_pad_all(card, 8, 0);

    lv_obj_t *stripe = lv_obj_create(card);
    lv_obj_set_size(stripe, LV_PCT(100), 4);
    lv_obj_align(stripe, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_clear_flag(stripe, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(stripe, 8, 0);
    lv_obj_set_style_border_width(stripe, 0, 0);
    lv_obj_set_style_bg_color(stripe, accent_color, 0);

    lv_obj_t *icon = lv_label_create(card);
    lv_label_set_text(icon, symbol_text);
    lv_obj_set_style_text_color(icon, accent_color, 0);
    lv_obj_align(icon, LV_ALIGN_TOP_LEFT, 0, 8);

    lv_obj_t *title_label = lv_label_create(card);
    lv_label_set_text(title_label, title);
    lv_obj_set_style_text_color(title_label, UI_COLOR_TEXT_PRIMARY, 0);
    lv_obj_align(title_label, LV_ALIGN_TOP_LEFT, 22, 8);

    lv_obj_t *subtitle_label = lv_label_create(card);
    lv_label_set_text(subtitle_label, subtitle);
    lv_obj_set_style_text_color(subtitle_label, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_align(subtitle_label, LV_ALIGN_TOP_LEFT, 0, 26);

    lv_obj_t *status_chip = lv_obj_create(card);
    lv_obj_set_size(status_chip, 68, 18);
    lv_obj_align(status_chip, LV_ALIGN_BOTTOM_RIGHT, 0, -2);
    lv_obj_clear_flag(status_chip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(status_chip, 9, 0);
    lv_obj_set_style_border_width(status_chip, 0, 0);
    lv_obj_set_style_bg_color(status_chip, accent_color, 0);
    lv_obj_set_style_bg_opa(status_chip, LV_OPA_20, 0);
    lv_obj_set_style_pad_all(status_chip, 0, 0);

    lv_obj_t *status_label = lv_label_create(status_chip);
    lv_label_set_text(status_label, status_text);
    lv_obj_set_style_text_color(status_label, accent_color, 0);
    lv_obj_center(status_label);

    lv_obj_add_event_cb(card, block_card_event_cb, LV_EVENT_CLICKED, (void *)title);
    return card;
}

static lv_obj_t *create_blocks_screen(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_set_style_radius(scr, 0, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_set_style_bg_color(scr, UI_COLOR_BG_TOP, 0);
    lv_obj_set_style_bg_grad_color(scr, UI_COLOR_BG_BOTTOM, 0);
    lv_obj_set_style_bg_grad_dir(scr, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_obj_t *header = lv_obj_create(scr);
    lv_obj_set_size(header, BRAIN_LCD_H_RES, 42);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_bg_color(header, UI_COLOR_HEADER, 0);
    lv_obj_set_style_pad_left(header, 8, 0);
    lv_obj_set_style_pad_right(header, 8, 0);
    lv_obj_set_style_pad_top(header, 0, 0);
    lv_obj_set_style_pad_bottom(header, 0, 0);

    /* Resistive touch + top-left corner = use a larger invisible hit zone than the visible pill button. */
    lv_obj_t *back_hit_zone = lv_button_create(header);
    lv_obj_set_size(back_hit_zone, 100, 36);
    lv_obj_align(back_hit_zone, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_radius(back_hit_zone, 14, 0);
    lv_obj_set_style_bg_opa(back_hit_zone, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(back_hit_zone, 0, 0);
    lv_obj_set_style_shadow_width(back_hit_zone, 0, 0);
    lv_obj_set_style_pad_all(back_hit_zone, 0, 0);
    lv_obj_add_event_cb(back_hit_zone, blocks_back_event_cb, LV_EVENT_PRESSED, NULL);

    lv_obj_t *back_btn = lv_obj_create(back_hit_zone);
    lv_obj_set_size(back_btn, 84, 32);
    lv_obj_align(back_btn, LV_ALIGN_LEFT_MID, 2, 0);
    lv_obj_clear_flag(back_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(back_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(back_btn, 12, 0);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(back_btn, LV_OPA_20, 0);
    lv_obj_set_style_border_width(back_btn, 1, 0);
    lv_obj_set_style_border_color(back_btn, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_pad_all(back_btn, 0, 0);

    lv_obj_t *back_lbl = lv_label_create(back_btn);
    lv_label_set_text(back_lbl, LV_SYMBOL_LEFT " Home");
    lv_obj_set_style_text_color(back_lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(back_lbl);

    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "Blocks");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *count_chip = lv_obj_create(header);
    lv_obj_set_size(count_chip, 56, 22);
    lv_obj_align(count_chip, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_clear_flag(count_chip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(count_chip, 11, 0);
    lv_obj_set_style_border_width(count_chip, 0, 0);
    lv_obj_set_style_bg_color(count_chip, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(count_chip, LV_OPA_20, 0);
    lv_obj_t *count_lbl = lv_label_create(count_chip);
    lv_label_set_text(count_lbl, "4 slots");
    lv_obj_set_style_text_color(count_lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(count_lbl);

    lv_obj_t *content = lv_obj_create(scr);
    lv_obj_set_size(content, BRAIN_LCD_H_RES - 12, BRAIN_LCD_V_RES - 54);
    lv_obj_align(content, LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(content, 16, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_bg_color(content, UI_COLOR_SURFACE, 0);
    lv_obj_set_style_bg_opa(content, LV_OPA_90, 0);
    lv_obj_set_style_pad_all(content, 6, 0);
    lv_obj_set_style_pad_gap(content, 6, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *hero = lv_obj_create(content);
    lv_obj_set_width(hero, LV_PCT(100));
    lv_obj_set_height(hero, 36);
    lv_obj_clear_flag(hero, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(hero, 12, 0);
    lv_obj_set_style_border_width(hero, 0, 0);
    lv_obj_set_style_bg_color(hero, UI_COLOR_HERO_BG, 0);
    lv_obj_set_style_pad_left(hero, 10, 0);
    lv_obj_set_style_pad_right(hero, 10, 0);
    lv_obj_set_style_pad_top(hero, 4, 0);
    lv_obj_set_style_pad_bottom(hero, 4, 0);

    lv_obj_t *hero_title = lv_label_create(hero);
    lv_label_set_text(hero_title, LV_SYMBOL_LIST " Meet Your Helper Blocks");
    lv_obj_set_style_text_color(hero_title, UI_COLOR_TEXT_PRIMARY, 0);
    lv_obj_align(hero_title, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *hero_sub = lv_label_create(hero);
    lv_label_set_text(hero_sub, "Tap a block card to see what it does.");
    lv_obj_set_width(hero_sub, LV_PCT(100));
    lv_label_set_long_mode(hero_sub, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(hero_sub, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_align(hero_sub, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    lv_obj_t *grid = lv_obj_create(content);
    lv_obj_set_width(grid, LV_PCT(100));
    lv_obj_set_height(grid, 156);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 0, 0);
    lv_obj_set_style_pad_gap(grid, 8, 0);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    create_block_card(grid, LV_SYMBOL_AUDIO,    "Music",  "Songs + sounds", "Ready",       UI_COLOR_SUCCESS);
    create_block_card(grid, LV_SYMBOL_SETTINGS, "Lights", "Colors + glow",  "Waiting",     UI_COLOR_WARN);
    create_block_card(grid, LV_SYMBOL_OK,       "Buttons","Press to play",  "Ready",       UI_COLOR_INFO);
    create_block_card(grid, LV_SYMBOL_REFRESH,  "New Slot","Find a block",  "Scan next",   UI_COLOR_FUN);

    lv_obj_t *footer = lv_obj_create(content);
    lv_obj_set_width(footer, LV_PCT(100));
    lv_obj_set_height(footer, 36);
    lv_obj_clear_flag(footer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(footer, 10, 0);
    lv_obj_set_style_border_width(footer, 1, 0);
    lv_obj_set_style_border_color(footer, UI_COLOR_BORDER_SOFT, 0);
    lv_obj_set_style_bg_color(footer, UI_COLOR_STATUS_BG, 0);
    lv_obj_set_style_bg_opa(footer, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_left(footer, 10, 0);
    lv_obj_set_style_pad_right(footer, 10, 0);
    lv_obj_set_style_pad_top(footer, 0, 0);
    lv_obj_set_style_pad_bottom(footer, 0, 0);

    s_blocks_status_label = lv_label_create(footer);
    lv_label_set_text(s_blocks_status_label, "Tap a block to explore what it does.");
    lv_obj_set_width(s_blocks_status_label, LV_PCT(100));
    lv_label_set_long_mode(s_blocks_status_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_color(s_blocks_status_label, UI_COLOR_TEXT_PRIMARY, 0);
    lv_obj_align(s_blocks_status_label, LV_ALIGN_LEFT_MID, 0, 0);

    return scr;
}

static void open_blocks_screen(void)
{
    if (s_blocks_screen == NULL) {
        s_blocks_screen = create_blocks_screen();
    }

    if (s_blocks_status_label != NULL) {
        lv_label_set_text(s_blocks_status_label, "Tap a block to explore what it does.");
    }

    lv_screen_load_anim(s_blocks_screen, LV_SCR_LOAD_ANIM_MOVE_LEFT, 220, 0, false);
}

/* Build a basic but clean "home screen" for the Brain Block.
 * This demonstrates: header bar, flex layouts, reusable cards, symbol buttons, and a live timer label. */
static lv_obj_t *create_home_screen(void)
{
    /* Root screen object (parent = NULL means "screen"). */
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_set_style_radius(scr, 0, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_set_style_bg_color(scr, UI_COLOR_BG_TOP, 0);
    lv_obj_set_style_bg_grad_color(scr, UI_COLOR_BG_BOTTOM, 0);
    lv_obj_set_style_bg_grad_dir(scr, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    /* ---------------- Header ----------------
     * A fixed top bar makes the UI feel like an app shell. */
    lv_obj_t *header = lv_obj_create(scr);
    lv_obj_set_size(header, BRAIN_LCD_H_RES, 42);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_left(header, 8, 0);
    lv_obj_set_style_pad_right(header, 8, 0);
    lv_obj_set_style_pad_top(header, 0, 0);
    lv_obj_set_style_pad_bottom(header, 0, 0);
    lv_obj_set_style_bg_color(header, UI_COLOR_HEADER, 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);

    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "Brain Block");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 0, 0);
    /* Hidden maker entry: long-press title opens the full-screen touch tester. */
    lv_obj_add_flag(title, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(title, header_title_long_press_cb, LV_EVENT_LONG_PRESSED, NULL);

    lv_obj_t *wifi = lv_label_create(header);
    lv_label_set_text(wifi, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_color(wifi, UI_COLOR_SUCCESS, 0);
    lv_obj_align(wifi, LV_ALIGN_RIGHT_MID, 0, 0);

    s_time_label = lv_label_create(header);
    lv_label_set_text(s_time_label, LV_SYMBOL_REFRESH " 00:00:00");
    lv_obj_set_style_text_color(s_time_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(s_time_label, LV_ALIGN_RIGHT_MID, -26, 0);

    /* ---------------- Main content shell ----------------
     * Use a flex column container so sections stack naturally.
     * This replaces a lot of manual x/y positioning from older LVGL styles. */
    lv_obj_t *content = lv_obj_create(scr);
    lv_obj_set_size(content, BRAIN_LCD_H_RES - 12, BRAIN_LCD_V_RES - 54);
    lv_obj_align(content, LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(content, 16, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_bg_color(content, UI_COLOR_SURFACE, 0);
    lv_obj_set_style_bg_opa(content, LV_OPA_90, 0);
    lv_obj_set_style_pad_all(content, 6, 0);
    lv_obj_set_style_pad_gap(content, 6, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    /* "Hero" status card: a top callout panel for state/summary text. */
    lv_obj_t *hero = lv_obj_create(content);
    lv_obj_set_width(hero, LV_PCT(100));
    lv_obj_set_height(hero, 40);
    lv_obj_clear_flag(hero, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(hero, 12, 0);
    lv_obj_set_style_border_width(hero, 0, 0);
    lv_obj_set_style_bg_color(hero, UI_COLOR_HERO_BG, 0);
    lv_obj_set_style_pad_left(hero, 10, 0);
    lv_obj_set_style_pad_right(hero, 10, 0);
    lv_obj_set_style_pad_top(hero, 4, 0);
    lv_obj_set_style_pad_bottom(hero, 4, 0);

    lv_obj_t *hero_title = lv_label_create(hero);
    lv_label_set_text(hero_title, LV_SYMBOL_HOME " Ready");
    lv_obj_set_style_text_color(hero_title, UI_COLOR_TEXT_PRIMARY, 0);
    lv_obj_align(hero_title, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *hero_subtitle = lv_label_create(hero);
    lv_label_set_text(hero_subtitle, "Pick an option to start!");
    lv_obj_set_style_text_color(hero_subtitle, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_align(hero_subtitle, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    /* Action grid using flex row wrap.
     * This is a simple way to build a dashboard grid without manually placing every button. */
    lv_obj_t *action_grid = lv_obj_create(content);
    lv_obj_set_width(action_grid, LV_PCT(100));
    lv_obj_set_height(action_grid, 154);
    lv_obj_clear_flag(action_grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(action_grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(action_grid, 0, 0);
    lv_obj_set_style_pad_all(action_grid, 0, 0);
    lv_obj_set_style_pad_gap(action_grid, 8, 0);
    lv_obj_set_flex_flow(action_grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(action_grid, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    create_action_tile(action_grid, LV_SYMBOL_PLAY, "Start", UI_COLOR_SUCCESS);
    create_action_tile(action_grid, LV_SYMBOL_LIST, "Blocks", UI_COLOR_INFO);
    create_action_tile(action_grid, LV_SYMBOL_SETTINGS, "Settings", UI_COLOR_FUN);
    create_action_tile(action_grid, LV_SYMBOL_REFRESH, "Scan", UI_COLOR_WARN);

    /* Footer status panel: this is where later you can show live brain state / errors / latency. */
    lv_obj_t *status_panel = lv_obj_create(content);
    lv_obj_set_width(status_panel, LV_PCT(100));
    lv_obj_set_height(status_panel, 34);
    lv_obj_clear_flag(status_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(status_panel, 10, 0);
    lv_obj_set_style_border_width(status_panel, 1, 0);
    lv_obj_set_style_border_color(status_panel, UI_COLOR_BORDER_SOFT, 0);
    lv_obj_set_style_bg_color(status_panel, UI_COLOR_STATUS_BG, 0);
    lv_obj_set_style_pad_left(status_panel, 10, 0);
    lv_obj_set_style_pad_right(status_panel, 10, 0);
    lv_obj_set_style_pad_top(status_panel, 0, 0);
    lv_obj_set_style_pad_bottom(status_panel, 0, 0);

    s_status_label = lv_label_create(status_panel);
    lv_label_set_text(s_status_label, "Tap a button to start!");
    lv_obj_set_width(s_status_label, BRAIN_LCD_H_RES - 56);
    lv_label_set_long_mode(s_status_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_color(s_status_label, UI_COLOR_TEXT_PRIMARY, 0);
    lv_obj_align(s_status_label, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *hint_label = lv_label_create(status_panel);
    lv_label_set_text(hint_label, LV_SYMBOL_WARNING);
    lv_obj_set_style_text_color(hint_label, UI_COLOR_ALERT, 0);
    lv_obj_align(hint_label, LV_ALIGN_RIGHT_MID, 0, 0);

    /* Touch tester is still available via long-press on the title, but we keep the
     * home screen clean now that touch mapping is verified. */

    /* Prime the timer-driven header label immediately so it doesn't wait 1s for first update. */
    uptime_timer_cb(NULL);
    update_touch_debug_ui();
    return scr;
}

static void create_boot_screen(void)
{
    /* Keep the function name for now so tft_ui_start() doesn't need more changes.
     * Internally we now create and load a real v9 home screen. */
    if (s_home_screen == NULL) {
        s_home_screen = create_home_screen();
    }

    lv_screen_load(s_home_screen);

    if (s_uptime_timer == NULL) {
        /* LVGL timer runs on the LVGL thread (inside lv_timer_handler), so it is safe to update labels here. */
        s_uptime_timer = lv_timer_create(uptime_timer_cb, 1000, NULL);
    }

    if (s_touch_debug_timer == NULL) {
        /* Poll-only touch debug UI updater. Keeps the overlay/tester labels and crosshair in sync. */
        s_touch_debug_timer = lv_timer_create(touch_debug_ui_timer_cb, TOUCH_DEBUG_REFRESH_MS, NULL);
    }

    update_touch_debug_ui();
}

void tft_ui_start(void)
{
    if (s_ui_started) {
        ESP_LOGW(TAG, "tft_ui_start already called");
        return;
    }
    s_ui_started = true;

    ESP_LOGI(TAG, "Starting LVGL v9 bring-up");

    /* --------------------------------------------------------------------
     * STEP 1: Backlight GPIO
     * Keep backlight off during panel init to avoid flicker/garbage on screen.
     * -------------------------------------------------------------------- */
    gpio_config_t bk_cfg = {
        .pin_bit_mask = 1ULL << PIN_NUM_BK_LIGHT,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&bk_cfg));
    gpio_set_level(PIN_NUM_BK_LIGHT, !BRAIN_LCD_BACKLIGHT_ON_LEVEL);

    /* --------------------------------------------------------------------
     * STEP 2: SPI bus init (shared by LCD and touch controller)
     * max_transfer_sz should be large enough for one LVGL draw chunk.
     * -------------------------------------------------------------------- */
    spi_bus_config_t buscfg = {
        .sclk_io_num = PIN_NUM_SCLK,
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = PIN_NUM_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = BRAIN_LCD_H_RES * BRAIN_LVGL_DRAW_BUF_LINES * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(BRAIN_LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

    /* --------------------------------------------------------------------
     * STEP 3: Create LCD panel IO (SPI command/data channel)
     * This is the low-level transport used by the ILI9341 panel driver.
     * -------------------------------------------------------------------- */
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = PIN_NUM_LCD_DC,
        .cs_gpio_num = PIN_NUM_LCD_CS,
        .pclk_hz = BRAIN_LCD_PIXEL_CLOCK_HZ,
        .lcd_cmd_bits = BRAIN_LCD_CMD_BITS,
        .lcd_param_bits = BRAIN_LCD_PARAM_BITS,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)BRAIN_LCD_HOST, &io_config, &io_handle));

    /* --------------------------------------------------------------------
     * STEP 4: Create + initialize ILI9341 panel driver
     * s_panel_handle is stored globally so the LVGL flush callback can use it.
     * -------------------------------------------------------------------- */
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = PIN_NUM_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(io_handle, &panel_config, &s_panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel_handle));

    /* Mirror is board/orientation dependent. Keep this aligned with touch flags below. */
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(s_panel_handle, true, false));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel_handle, true));

    /* Turn backlight on only after the panel is ready. */
    gpio_set_level(PIN_NUM_BK_LIGHT, BRAIN_LCD_BACKLIGHT_ON_LEVEL);

    /* --------------------------------------------------------------------
     * STEP 5: Initialize LVGL core + create LVGL display object
     * LVGL v9 uses lv_display_t (instead of the old v7 lv_disp_drv + lv_disp_buf APIs).
     * -------------------------------------------------------------------- */
    lv_init();

    s_display = lv_display_create(BRAIN_LCD_H_RES, BRAIN_LCD_V_RES);
    assert(s_display != NULL);

    /* Two DMA-capable draw buffers for partial rendering (double buffering).
     * Partial mode is memory-efficient and a good default on ESP32 + SPI TFTs. */
    const size_t draw_buf_sz = BRAIN_LCD_H_RES * BRAIN_LVGL_DRAW_BUF_LINES * sizeof(lv_color16_t);
    void *buf1 = spi_bus_dma_memory_alloc(BRAIN_LCD_HOST, draw_buf_sz, 0);
    void *buf2 = spi_bus_dma_memory_alloc(BRAIN_LCD_HOST, draw_buf_sz, 0);
    assert(buf1 != NULL);
    assert(buf2 != NULL);

    /* Wire LVGL display to our panel + flush callback. */
    lv_display_set_buffers(s_display, buf1, buf2, draw_buf_sz, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_user_data(s_display, s_panel_handle);
    lv_display_set_color_format(s_display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(s_display, lcd_flush_cb);

    /* Register "transfer complete" callback so LVGL knows when SPI flush finished. */
    const esp_lcd_panel_io_callbacks_t io_callbacks = {
        .on_color_trans_done = lcd_flush_ready_cb,
    };
    ESP_ERROR_CHECK(esp_lcd_panel_io_register_event_callbacks(io_handle, &io_callbacks, s_display));

    /* --------------------------------------------------------------------
     * STEP 6: Touch controller init (XPT2046 on same SPI bus, separate CS)
     * -------------------------------------------------------------------- */
    esp_lcd_panel_io_handle_t tp_io_handle = NULL;
    esp_lcd_panel_io_spi_config_t tp_io_config = ESP_LCD_TOUCH_IO_SPI_XPT2046_CONFIG(PIN_NUM_TOUCH_CS);
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)BRAIN_LCD_HOST, &tp_io_config, &tp_io_handle));

    esp_lcd_touch_config_t tp_cfg = {
        /* Recommended first candidate for this board orientation:
         * x/y max are set in the pre-swap axis frame because esp_lcd_touch
         * applies mirror first, then swap_xy. */
        .x_max = BRAIN_LCD_V_RES,
        .y_max = BRAIN_LCD_H_RES,
        .rst_gpio_num = -1,
        .int_gpio_num = PIN_NUM_TOUCH_IRQ,
        .flags = {
            /* These compensate for panel orientation + touch wiring.
             * If touches feel rotated/inverted, these are the first values to test. */
            .swap_xy = 1,
            .mirror_x = 1,
            .mirror_y = 0,
        },
    };
    esp_lcd_touch_handle_t tp = NULL;
    ESP_ERROR_CHECK(esp_lcd_touch_new_spi_xpt2046(tp_io_handle, &tp_cfg, &tp));
    s_touch_handle = tp;
    apply_touch_transform_preset(TOUCH_PRESET_DEFAULT_INDEX);

    /* Register LVGL input device (pointer) and connect it to our touch callback. */
    lv_indev_t *indev = lv_indev_create();
    assert(indev != NULL);
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(indev, s_display);
    lv_indev_set_user_data(indev, tp);
    lv_indev_set_long_press_time(indev, 800);
    lv_indev_set_read_cb(indev, touch_read_cb);

    /* LVGL indev uses its own read timer; tighten it for resistive touch responsiveness. */
    lv_timer_t *read_timer = lv_indev_get_read_timer(indev);
    if (read_timer != NULL) {
        lv_timer_set_period(read_timer, 10);
    }

    /* --------------------------------------------------------------------
     * STEP 7: Start LVGL tick source
     * LVGL needs periodic ticks (separate from lv_timer_handler()).
     * -------------------------------------------------------------------- */
    const esp_timer_create_args_t tick_args = {
        .callback = brain_lvgl_tick_cb,
        .name = "lvgl_tick",
    };
    ESP_ERROR_CHECK(esp_timer_create(&tick_args, &s_lvgl_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(s_lvgl_tick_timer, BRAIN_LVGL_TICK_PERIOD_MS * 1000));

    /* --------------------------------------------------------------------
     * STEP 8: Build and load the first screen
     * -------------------------------------------------------------------- */
    create_boot_screen();

    /* --------------------------------------------------------------------
     * STEP 9: Start the LVGL task loop
     * After this, LVGL begins handling rendering, touch events, and timers.
     * -------------------------------------------------------------------- */
    BaseType_t ok = xTaskCreatePinnedToCore(lvgl_task, "lvgl_v9", BRAIN_LVGL_TASK_STACK_SIZE, NULL,
                                            BRAIN_LVGL_TASK_PRIORITY, NULL, 1);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create LVGL task");
        abort();
    }

    ESP_LOGI(TAG, "LVGL v9 bring-up complete");
}
