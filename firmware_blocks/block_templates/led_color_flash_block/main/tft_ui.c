#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"

#include "driver/gpio.h"

#include "lvgl.h"
#include "lvgl_helpers.h"

#include "tft_ui.h"
#include "command_handler.h"

#define TAG "LED_FLASH_UI"
#define LCD_BACKLIGHT 32
#define LV_TICK_PERIOD_MS 1

/*
 * This module owns the TFT UX flow for the LED Color Flash block:
 *   Intro screen -> Start -> Numpad screen -> select/submit actions.
 *
 * Responsibility split:
 * - LVGL rendering + touch processing: this file (Core 1 GUI task).
 * - LED execution timing: command_handler worker queue (Core 0).
 *
 * This keeps UI callbacks non-blocking and reduces visible touch latency.
 */

extern const lv_img_dsc_t illustration_of_cheerful_kids_dancing_together_png;

// Persisted UI object handles and init guard.
static lv_obj_t *s_intro_screen = NULL;
static lv_obj_t *s_numpad_screen = NULL;
static lv_obj_t *s_status_label = NULL;
static bool s_ui_started = false;
static int8_t s_selected_digit = -1;

static const lv_color_t s_key_colors[10] = {
    LV_COLOR_MAKE(120, 120, 120), // 0
    LV_COLOR_MAKE(255, 99, 132),  // 1
    LV_COLOR_MAKE(99, 255, 99),   // 2
    LV_COLOR_MAKE(99, 160, 255),  // 3
    LV_COLOR_MAKE(255, 180, 60),  // 4
    LV_COLOR_MAKE(220, 120, 255), // 5
    LV_COLOR_MAKE(80, 220, 220),  // 6
    LV_COLOR_MAKE(255, 230, 80),  // 7
    LV_COLOR_MAKE(130, 180, 255), // 8
    LV_COLOR_MAKE(255, 255, 255), // 9
};

// Small helper used by LVGL animations to move a widget vertically.
static void anim_obj_y(void *obj, lv_anim_value_t value)
{
    lv_obj_set_y((lv_obj_t *)obj, value);
}

// LVGL timebase callback called by esp_timer every LV_TICK_PERIOD_MS.
static void lv_tick_task(void *arg)
{
    (void)arg;
    lv_tick_inc(LV_TICK_PERIOD_MS);
}

// Numpad key click handler: decode digit, cache selection, queue preview.
static void numpad_btn_cb(lv_obj_t *obj, lv_event_t event)
{
    if (event != LV_EVENT_CLICKED) {
        return;
    }

    const char *txt = lv_label_get_text(lv_obj_get_child(obj, NULL));
    if (!txt || txt[0] < '0' || txt[0] > '9') {
        return;
    }
    uint8_t digit = (uint8_t)(txt[0] - '0');

    // Playful micro-animation: key "jumps" up and back down.
    lv_coord_t y0 = lv_obj_get_y(obj);
    lv_anim_t pop_anim;
    lv_anim_init(&pop_anim);
    lv_anim_set_var(&pop_anim, obj);
    lv_anim_set_exec_cb(&pop_anim, anim_obj_y);
    lv_anim_set_values(&pop_anim, y0, y0 - 5);
    lv_anim_set_time(&pop_anim, 70);
    lv_anim_set_playback_time(&pop_anim, 90);
    lv_anim_set_repeat_count(&pop_anim, 1);
    lv_anim_start(&pop_anim);

    s_selected_digit = (int8_t)digit;

    // Child-friendly response text to confirm selection.
    const char *message = "Great!";
    switch (digit) {
        case 0: message = "Selected: Lights Off"; break;
        case 1: message = "Selected: Red Rocket!"; break;
        case 2: message = "Selected: Green Glow!"; break;
        case 3: message = "Selected: Blue Boom!"; break;
        case 4: message = "Selected: Orange Pop!"; break;
        case 5: message = "Selected: Purple Power!"; break;
        case 6: message = "Selected: Cyan Splash!"; break;
        case 7: message = "Selected: Chase Flash Random!"; break;
        case 8: message = "Selected: Chase Rainbow White!"; break;
        case 9: message = "Selected: Comet!"; break;
        default: break;
    }

    char status[72];
    snprintf(status, sizeof(status), "%s  (%u)\nTap SUBMIT to run.", message, (unsigned)digit);
    if (s_status_label) {
        lv_label_set_text(s_status_label, status);
    }

    // Preview is asynchronous; queue full means worker is still busy.
    if (!command_handler_enqueue_preview(digit)) {
        if (s_status_label) {
            lv_label_set_text(s_status_label, "Preview busy. Tap again.");
        }
    }
}

// Submit button click handler: enqueue full execute for selected digit.
static void submit_btn_cb(lv_obj_t *obj, lv_event_t event)
{
    (void)obj;
    if (event != LV_EVENT_CLICKED) {
        return;
    }

    if (s_selected_digit < 0 || s_selected_digit > 9) {
        if (s_status_label) {
            lv_label_set_text(s_status_label, "Pick a number first, then tap SUBMIT.");
        }
        return;
    }

    uint8_t digit = (uint8_t)s_selected_digit;
    // Brain-authoritative flow: publish selection as DATA_READY payload.
    if (command_handler_submit_selection(digit)) {
        char status[64];
        snprintf(status, sizeof(status), "Submitted! Sent to Brain (%u)", (unsigned)digit);
        if (s_status_label) {
            lv_label_set_text(s_status_label, status);
        }
    } else if (s_status_label) {
        lv_label_set_text(s_status_label, "Busy now. Try submit again.");
    }
}

// Creates one key button at the requested screen position.
static lv_obj_t *create_key(lv_obj_t *parent, const char *text, lv_coord_t x, lv_coord_t y)
{
    lv_obj_t *btn = lv_btn_create(parent, NULL);
    lv_obj_set_size(btn, 68, 48);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_event_cb(btn, numpad_btn_cb);
    lv_obj_set_style_local_radius(btn, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, 12);
    lv_obj_set_style_local_border_width(btn, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, 2);
    lv_obj_set_style_local_border_color(btn, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE);

    uint8_t digit = 0;
    if (text && text[0] >= '0' && text[0] <= '9') {
        digit = (uint8_t)(text[0] - '0');
    }
    lv_obj_set_style_local_bg_color(btn, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, s_key_colors[digit]);
    lv_obj_set_style_local_bg_color(btn, LV_BTN_PART_MAIN, LV_STATE_PRESSED, lv_color_darken(s_key_colors[digit], LV_OPA_30));
    lv_obj_set_style_local_text_color(btn, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_BLACK);
    lv_obj_set_style_local_text_color(btn, LV_BTN_PART_MAIN, LV_STATE_PRESSED, LV_COLOR_BLACK);

    lv_obj_t *label = lv_label_create(btn, NULL);
    lv_label_set_text(label, text);
    return btn;
}

// Builds the numeric keypad UI used to drive color flash behaviors.
static lv_obj_t *create_numpad_screen(void)
{
    lv_obj_t *scr = lv_obj_create(NULL, NULL);
    lv_obj_set_style_local_bg_color(scr, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(0x1F2A44));
    lv_obj_set_style_local_bg_opa(scr, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_COVER);

    lv_obj_t *title = lv_label_create(scr, NULL);
    lv_label_set_text(title, "Color Magic Pad");
    lv_obj_set_style_local_text_color(title, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE);
    lv_obj_align(title, NULL, LV_ALIGN_IN_TOP_MID, 0, 10);

    // Status label doubles as user guidance + queue/state feedback.
    s_status_label = lv_label_create(scr, NULL);
    lv_label_set_text(s_status_label, "Pick a number, then tap SUBMIT.");
    lv_obj_set_style_local_text_color(s_status_label, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(0xFFE88A));
    lv_obj_align(s_status_label, NULL, LV_ALIGN_IN_TOP_MID, 0, 34);
    lv_obj_set_width(s_status_label, 220);
    lv_label_set_long_mode(s_status_label, LV_LABEL_LONG_BREAK);
    lv_label_set_align(s_status_label, LV_LABEL_ALIGN_CENTER);

    lv_coord_t x0 = 12;
    lv_coord_t y0 = 62;
    lv_coord_t dx = 76;
    lv_coord_t dy = 52;

    create_key(scr, "1", x0 + dx * 0, y0 + dy * 0);
    create_key(scr, "2", x0 + dx * 1, y0 + dy * 0);
    create_key(scr, "3", x0 + dx * 2, y0 + dy * 0);
    create_key(scr, "4", x0 + dx * 0, y0 + dy * 1);
    create_key(scr, "5", x0 + dx * 1, y0 + dy * 1);
    create_key(scr, "6", x0 + dx * 2, y0 + dy * 1);
    create_key(scr, "7", x0 + dx * 0, y0 + dy * 2);
    create_key(scr, "8", x0 + dx * 1, y0 + dy * 2);
    create_key(scr, "9", x0 + dx * 2, y0 + dy * 2);
    create_key(scr, "0", x0 + dx * 1, y0 + dy * 3);

    // Separate SUBMIT keeps number taps lightweight and intentional.
    lv_obj_t *submit_btn = lv_btn_create(scr, NULL);
    lv_obj_set_size(submit_btn, 170, 38);
    lv_obj_align(submit_btn, NULL, LV_ALIGN_IN_BOTTOM_MID, 0, -8);
    lv_obj_set_event_cb(submit_btn, submit_btn_cb);
    lv_obj_set_style_local_radius(submit_btn, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, 12);
    lv_obj_set_style_local_bg_color(submit_btn, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(0x7CF29A));
    lv_obj_set_style_local_bg_color(submit_btn, LV_BTN_PART_MAIN, LV_STATE_PRESSED, lv_color_hex(0x4ED86F));
    lv_obj_set_style_local_border_width(submit_btn, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, 2);
    lv_obj_set_style_local_border_color(submit_btn, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE);

    lv_obj_t *submit_label = lv_label_create(submit_btn, NULL);
    lv_label_set_text(submit_label, "SUBMIT");

    return scr;
}

// Intro screen Start button callback: lazy-create and open numpad screen.
static void start_btn_cb(lv_obj_t *obj, lv_event_t event)
{
    (void)obj;
    if (event != LV_EVENT_CLICKED) {
        return;
    }

    // Give the Start button a quick bounce before screen transition.
    lv_coord_t y0 = lv_obj_get_y(obj);
    lv_anim_t start_pop_anim;
    lv_anim_init(&start_pop_anim);
    lv_anim_set_var(&start_pop_anim, obj);
    lv_anim_set_exec_cb(&start_pop_anim, anim_obj_y);
    lv_anim_set_values(&start_pop_anim, y0, y0 - 6);
    lv_anim_set_time(&start_pop_anim, 80);
    lv_anim_set_playback_time(&start_pop_anim, 90);
    lv_anim_set_repeat_count(&start_pop_anim, 1);
    lv_anim_start(&start_pop_anim);

    if (!s_numpad_screen) {
        s_numpad_screen = create_numpad_screen();
    }
    lv_scr_load_anim(s_numpad_screen, LV_SCR_LOAD_ANIM_MOVE_LEFT, 200, 0, false);
}

// Builds the initial landing screen shown on boot.
static lv_obj_t *create_intro_screen(void)
{
    lv_obj_t *scr = lv_obj_create(NULL, NULL);
    lv_obj_set_style_local_bg_color(scr, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(0x7AD7F0));
    lv_obj_set_style_local_bg_opa(scr, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_COVER);

    // Intro image is embedded as C array and scaled to TFT bounds.
    lv_obj_t *hero_img = lv_img_create(scr, NULL);
    lv_img_set_src(hero_img, &illustration_of_cheerful_kids_dancing_together_png);
    // Source image is larger than the TFT; scale it down to fit the intro layout.
#if LV_USE_IMG_TRANSFORM
    lv_img_set_zoom(hero_img, 109); // 256 = 100%; 109 ~= 42.5% (fits 240x320 UI)
    lv_img_set_antialias(hero_img, true);
#endif
    lv_obj_align(hero_img, NULL, LV_ALIGN_IN_TOP_MID, 0, 30);

    lv_obj_t *title = lv_label_create(scr, NULL);
    lv_label_set_text(title, "Welcome!");
    lv_obj_set_style_local_text_color(title, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(0x1A2340));
    lv_obj_align(title, NULL, LV_ALIGN_IN_TOP_MID, 0, 12);

    lv_obj_t *subtitle = lv_label_create(scr, NULL);
    lv_label_set_text(subtitle, "Tap START and choose a number to build your own program !");
    lv_obj_set_style_local_text_color(subtitle, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(0x1A2340));
    lv_obj_align(subtitle, title, LV_ALIGN_OUT_BOTTOM_MID, 0, 4);

    lv_obj_t *start_btn = lv_btn_create(scr, NULL);
    lv_obj_set_size(start_btn, 150, 50);
    lv_obj_align(start_btn, NULL, LV_ALIGN_IN_BOTTOM_MID, 0, -24);
    lv_obj_set_event_cb(start_btn, start_btn_cb);
    lv_obj_set_style_local_radius(start_btn, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, 14);
    lv_obj_set_style_local_bg_color(start_btn, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(0xFFD166));
    lv_obj_set_style_local_bg_color(start_btn, LV_BTN_PART_MAIN, LV_STATE_PRESSED, lv_color_hex(0xF4B942));
    lv_obj_set_style_local_border_width(start_btn, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, 2);
    lv_obj_set_style_local_border_color(start_btn, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(0xFFFFFF));

    lv_obj_t *btn_label = lv_label_create(start_btn, NULL);
    lv_label_set_text(btn_label, "START");

    return scr;
}

/*
 * Dedicated LVGL task:
 * - Loads initial screen
 * - Pumps LVGL task handler in a regular loop
 */
static void gui_task(void *arg)
{
    (void)arg;
    if (!s_intro_screen) {
        s_intro_screen = create_intro_screen();
    }
    lv_scr_load(s_intro_screen);

    while (1) {
        // LVGL main pump; all LVGL API calls stay in this thread.
        lv_task_handler();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void tft_ui_start(void)
{
    if (s_ui_started) {
        ESP_LOGW(TAG, "TFT UI already started");
        return;
    }

    lv_init();
    lvgl_driver_init();

    // Double buffering in DMA-capable memory for smoother display flushes.
    lv_color_t *buf1 = heap_caps_malloc(DISP_BUF_SIZE * sizeof(lv_color_t), MALLOC_CAP_DMA);
    lv_color_t *buf2 = heap_caps_malloc(DISP_BUF_SIZE * sizeof(lv_color_t), MALLOC_CAP_DMA);
    if (!buf1 || !buf2) {
        ESP_LOGE(TAG, "Failed to allocate LVGL display buffers");
        free(buf1);
        free(buf2);
        return;
    }

    static lv_disp_buf_t disp_buf;
    lv_disp_buf_init(&disp_buf, buf1, buf2, DISP_BUF_SIZE);

    lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.flush_cb = disp_driver_flush;
    disp_drv.buffer = &disp_buf;
    lv_disp_drv_register(&disp_drv);

    // Register touch input device for the numpad interactions.
    lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touch_driver_read;
    lv_indev_drv_register(&indev_drv);

    const esp_timer_create_args_t periodic_timer_args = {
        .callback = &lv_tick_task,
        .name = "lv_tick"
    };
    esp_timer_handle_t periodic_timer;
    ESP_ERROR_CHECK(esp_timer_create(&periodic_timer_args, &periodic_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(periodic_timer, LV_TICK_PERIOD_MS * 1000));

    // Ensure TFT backlight is explicitly enabled.
    gpio_reset_pin(LCD_BACKLIGHT);
    gpio_set_direction(LCD_BACKLIGHT, GPIO_MODE_OUTPUT);
    gpio_set_level(LCD_BACKLIGHT, 1);

    // Pin GUI to Core 1 so Core 0 can service I2C + action worker tasks.
    BaseType_t ok = xTaskCreatePinnedToCore(gui_task, "led_ui", 4096, NULL, 5, NULL, 1);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create GUI task");
        return;
    }

    s_ui_started = true;
    ESP_LOGI(TAG, "TFT UI started");
}
