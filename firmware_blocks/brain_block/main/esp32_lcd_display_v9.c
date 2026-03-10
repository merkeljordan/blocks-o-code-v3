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
#include "brain_block.h"
#include "brain_event_handler.h"

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
 *    - A simple home screen using LVGL v9 objects, styles, flex layout, and events
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
#define TOUCH_PRESET_DEFAULT_INDEX     6U
/* Software touch normalization helps when edge hits are compressed (common on XPT2046).
 * These are safe defaults; adjust if tester still shows corner drift. */
#define TOUCH_SW_CAL_ENABLE            1
#define TOUCH_SW_CAL_MIN_X             10
#define TOUCH_SW_CAL_MAX_X             (BRAIN_LCD_H_RES - 10)
#define TOUCH_SW_CAL_MIN_Y             12
#define TOUCH_SW_CAL_MAX_Y             (BRAIN_LCD_V_RES - 12)
#define HOME_ACTION_DEBOUNCE_US        150000

/*
 * Touch coordinate pipeline (important when debugging wrong touch orientation):
 *
 * 1) XPT2046 controller produces raw coordinates.
 * 2) esp_lcd_touch applies transform flags (swap_xy/mirror_x/mirror_y) and axis limits (x_max/y_max).
 * 3) normalize_touch_point() performs optional software calibration scaling to full 240x320 logical space.
 * 4) LVGL receives the normalized coordinate via touch_read_cb().
 *
 * Notes:
 * - The `tp_cfg.flags` values in tft_ui_start() are just the initial bootstrap config.
 * - apply_touch_transform_preset() is called immediately after touch init and becomes the active mapping.
 * - If touch feels rotated/mirrored, adjust TOUCH_PRESET_DEFAULT_INDEX for your panel stack-up.
 */

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
    /* Preset naming:
     * - S = swap_xy
     * - Mx = mirror_x
     * - My = mirror_y
     * The Recommended preset is a known-good candidate for common 2.8" ILI9341 + XPT2046 boards.
     */
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
static lv_obj_t *s_wifi_icon_label = NULL;
static lv_obj_t *s_status_label = NULL;
static lv_obj_t *s_blocks_status_label = NULL;
static bool s_last_wifi_connected = false;

static touch_debug_state_t s_touch_debug = {
    .last_read_err = ESP_OK,
    .transform_preset_index = TOUCH_PRESET_DEFAULT_INDEX,
};
static bool s_touch_prev_pressed = false;
static int64_t s_last_home_action_us = 0;
static const char *s_last_home_action_name = NULL;

/* Forward declarations */
static void apply_touch_transform_preset(uint8_t preset_index);
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
    /* Map controller-space coordinates to the exact LVGL display resolution.
     * This soft calibration is intentionally lightweight: edge clamp + linear scale. */
    *out_x = map_touch_axis(raw_x, TOUCH_SW_CAL_MIN_X, TOUCH_SW_CAL_MAX_X, BRAIN_LCD_H_RES);
    *out_y = map_touch_axis(raw_y, TOUCH_SW_CAL_MIN_Y, TOUCH_SW_CAL_MAX_Y, BRAIN_LCD_V_RES);
#else
    *out_x = raw_x;
    *out_y = raw_y;
#endif
}

/* LVGL input read callback (touch -> LVGL pointer events)
 * LVGL polls this function regularly from lv_timer_handler().
 * We read the latest touch controller state and translate it into LVGL's format.
 *
 * Uses esp_lcd_touch_get_data() (the non-deprecated API) which returns
 * esp_lcd_touch_point_data_t structs instead of separate x/y/strength arrays. */
static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    esp_lcd_touch_handle_t tp = (esp_lcd_touch_handle_t)lv_indev_get_user_data(indev);
    esp_lcd_touch_point_data_t point_data[1] = {0};
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

    esp_err_t get_err = esp_lcd_touch_get_data(tp, point_data, &point_cnt, 1);
    s_touch_debug.last_read_err = (get_err != ESP_OK) ? get_err : ESP_OK;
    s_touch_debug.point_count = point_cnt;

    if (get_err == ESP_OK && point_cnt > 0) {
        uint16_t norm_x = point_data[0].x;
        uint16_t norm_y = point_data[0].y;
        /* point_data[] here is already transformed by the active preset.
         * normalize_touch_point() then handles calibration scaling before LVGL sees it. */
        normalize_touch_point(point_data[0].x, point_data[0].y, &norm_x, &norm_y);

        s_touch_debug.raw_x = point_data[0].x;
        s_touch_debug.raw_y = point_data[0].y;
        s_touch_debug.x = norm_x;
        s_touch_debug.y = norm_y;
        s_touch_debug.strength = point_data[0].strength;
        s_touch_debug.pressed = true;

        data->point.x = norm_x;
        data->point.y = norm_y;
        data->state = LV_INDEV_STATE_PRESSED;

        if (!s_touch_prev_pressed) {
            s_touch_debug.pressed_count++;
            s_touch_prev_pressed = true;
            ESP_LOGI(TAG, "TOUCH DOWN  raw=(%u,%u) norm=(%u,%u) Z=%u preset=%u",
                     (unsigned)point_data[0].x, (unsigned)point_data[0].y,
                     (unsigned)norm_x, (unsigned)norm_y,
                     (unsigned)point_data[0].strength,
                     (unsigned)s_touch_debug.transform_preset_index);
        }
    } else {
        s_touch_debug.raw_x = 0;
        s_touch_debug.raw_y = 0;
        s_touch_debug.strength = 0;
        s_touch_debug.pressed = false;
        if (s_touch_prev_pressed) {
            s_touch_debug.released_count++;
            s_touch_prev_pressed = false;
            ESP_LOGI(TAG, "TOUCH UP    last_norm=(%u,%u) total_presses=%lu",
                     (unsigned)s_touch_debug.x, (unsigned)s_touch_debug.y,
                     (unsigned long)s_touch_debug.pressed_count);
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
        if (s_wifi_icon_label != NULL) {
            bool connected = brain_companion_is_connected();
            if (connected != s_last_wifi_connected) {
                if (connected) {
                    lv_obj_clear_flag(s_wifi_icon_label, LV_OBJ_FLAG_HIDDEN);
                } else {
                    lv_obj_add_flag(s_wifi_icon_label, LV_OBJ_FLAG_HIDDEN);
                }
                s_last_wifi_connected = connected;
            }
        }

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
     * x_max/y_max define the post-transform coordinate frame and should match orientation for each preset.
     * mirror/swap use helper setters to keep internal driver state consistent. */
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

    if (strcmp(action_name, "Start") == 0) {
        ESP_LOGI(TAG, "Home action pressed: Start");
        bool handled = brain_event_handle_message("START");
        if (s_status_label != NULL) {
            if (handled) {
                lv_label_set_text(s_status_label, "START requested");
            } else {
                const brain_validation_state_t *validation = brain_event_handler_get_validation_state();
                if (validation != NULL && !validation->has_received_validation) {
                    lv_label_set_text(s_status_label, "START blocked: waiting for validation");
                } else if (validation != NULL && !validation->app_config_valid) {
                    lv_label_set_text(s_status_label, "START blocked: config invalid");
                } else {
                    lv_label_set_text(s_status_label, "START rejected: queue/state");
                }
            }
        }
        return;
    }

    if (strcmp(action_name, "Stop") == 0) {
        ESP_LOGI(TAG, "Home action pressed: Stop");
        bool handled = brain_event_handle_message("STOP");
        if (s_status_label != NULL) {
            if (handled) {
                lv_label_set_text(s_status_label, "STOP requested");
            } else {
                lv_label_set_text(s_status_label, "STOP rejected: queue/state");
            }
        }
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
 * This demonstrates: header bar, flex layouts, reusable cards, and symbol buttons. */
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

    s_wifi_icon_label = lv_label_create(header);
    lv_label_set_text(s_wifi_icon_label, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_color(s_wifi_icon_label, UI_COLOR_SUCCESS, 0);
    lv_obj_align(s_wifi_icon_label, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_flag(s_wifi_icon_label, LV_OBJ_FLAG_HIDDEN);
    s_last_wifi_connected = false;

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

    /* Keep two rows of actions:
     * row 1: Start + Stop (side by side)
     * row 2: Scan spans the full width */
    lv_obj_t *start_btn = create_action_tile(action_grid, LV_SYMBOL_PLAY, "Start", UI_COLOR_SUCCESS);
    lv_obj_set_width(start_btn, LV_PCT(48));

    lv_obj_t *stop_btn = create_action_tile(action_grid, LV_SYMBOL_STOP, "Stop", UI_COLOR_ALERT);
    lv_obj_set_width(stop_btn, LV_PCT(48));

    lv_obj_t *scan_btn = create_action_tile(action_grid, LV_SYMBOL_REFRESH, "Scan", UI_COLOR_WARN);
    lv_obj_set_width(scan_btn, LV_PCT(100));

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
        .x_max = BRAIN_LCD_H_RES,
        .y_max = BRAIN_LCD_V_RES,
        .rst_gpio_num = -1,
        .int_gpio_num = PIN_NUM_TOUCH_IRQ,
        .flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 1,
        },
    };
    esp_lcd_touch_handle_t tp = NULL;
    ESP_ERROR_CHECK(esp_lcd_touch_new_spi_xpt2046(tp_io_handle, &tp_cfg, &tp));
    s_touch_handle = tp;
    /* Runtime preset is the authoritative mapping.
     * Keep TOUCH_PRESET_DEFAULT_INDEX aligned with your specific panel/touch stack-up. */
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
