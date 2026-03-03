/*
 * tft_ui.c  --  LED Color Flash Block: TFT Touchscreen UI
 *
 * Drives an ILI9341 (240x320) TFT with XPT2046 touch via LVGL v7.
 * All rendering and touch processing run in a dedicated FreeRTOS task
 * pinned to core 1 so they never block the I2C / LED tasks on core 0.
 *
 * SCREEN FLOW
 * ~~~~~~~~~~~
 *   Screen 1 (Intro)   Dark splash with "Blocks o' Code (v3)" branding
 *                       and a big cyan START button.
 *
 *   Screen 2 (Numpad)  Colour-coded 0-9 keypad.  Tapping a number:
 *                         - shows the pattern name in the status label
 *                         - fires a quick LED preview pulse
 *                       Tapping SUBMIT:
 *                         - runs the full WS2812FX animation
 *                         - makes the selected color_id available for
 *                           the brain to read over I2C (CMD_GET_DATA)
 *
 * HARDWARE
 * ~~~~~~~~
 *   Display : ILI9341 240x320 SPI (configured via sdkconfig / menuconfig)
 *   Touch   : XPT2046 SPI
 *   Backlight: GPIO 32, active-high
 *
 * DEPENDENCIES
 * ~~~~~~~~~~~~
 *   command_handler.h  -- command_handler_enqueue_preview(), command_handler_submit_selection(), command_handler_enqueue_execute_digit()
 *   led_matrix.h       -- led_pattern_name() for the status label text
 */

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
 
 #include "command_handler.h"
 #include "led_matrix.h"
 #include "tft_ui.h"
 
 #define TAG "LED_FLASH_UI"
 #define LCD_BACKLIGHT 32          /* GPIO that controls TFT backlight     */
 #define LV_TICK_PERIOD_MS 1       /* LVGL timebase tick interval          */
 
 /* ── Persistent widget handles ─────────────────────────────────────────
  *  Kept at file scope so callbacks can update them from any screen.    */
 
 static lv_obj_t *s_intro_screen  = NULL;  /* Screen 1 object   */
 static lv_obj_t *s_numpad_screen = NULL;  /* Screen 2 object   */
 static lv_obj_t *s_status_label  = NULL;  /* Yellow feedback text on screen 2 */
 static bool      s_ui_started    = false; /* Prevents double-init            */
 static int8_t    s_selected_digit = -1;   /* -1 = nothing selected yet       */
 
 /* ── Numpad key colours ────────────────────────────────────────────────
  *  Each button gets a hue matching its pattern colour so the grid
  *  looks fun and gives a visual hint about the LED colour.            */
 
 static const lv_color_t s_key_colors[10] = {
     LV_COLOR_MAKE(100, 100, 100), /* 0 - grey  (off)   */
     LV_COLOR_MAKE(255,  60,  60), /* 1 - red           */
     LV_COLOR_MAKE( 60, 220,  60), /* 2 - green         */
     LV_COLOR_MAKE( 60,  80, 255), /* 3 - blue          */
     LV_COLOR_MAKE(255, 165,  40), /* 4 - orange        */
     LV_COLOR_MAKE(175,  60, 255), /* 5 - purple        */
     LV_COLOR_MAKE( 40, 230, 230), /* 6 - cyan          */
     LV_COLOR_MAKE(255, 240,  50), /* 7 - yellow        */
     LV_COLOR_MAKE(100, 140, 255), /* 8 - sky           */
     LV_COLOR_MAKE(240, 240, 240), /* 9 - white         */
 };
 
 /* ══════════════════════════════════════════════════════════════════════════
  *  HELPERS
  * ══════════════════════════════════════════════════════════════════════════ */
 
 /* LVGL animation callback: moves a widget on the Y axis.
  * Used for the playful "pop" bounce on button presses. */
 static void anim_obj_y(void *obj, lv_anim_value_t value)
 {
     lv_obj_set_y((lv_obj_t *)obj, value);
 }
 
 /* esp_timer callback: feeds the LVGL tick counter. */
 static void lv_tick_task(void *arg)
 {
     (void)arg;
     lv_tick_inc(LV_TICK_PERIOD_MS);
 }
 
 /* ══════════════════════════════════════════════════════════════════════════
  *  SCREEN 2 -- NUMPAD
  * ══════════════════════════════════════════════════════════════════════════ */
 
 /* Called when a numpad key (0-9) is tapped.
  * - Plays a bounce micro-animation on the button.
  * - Updates the status label with the pattern name.
  * - Fires a quick LED preview pulse via command_handler. */
 static void numpad_btn_cb(lv_obj_t *obj, lv_event_t event)
 {
     if (event != LV_EVENT_CLICKED) return;
 
     const char *txt = lv_label_get_text(lv_obj_get_child(obj, NULL));
     if (!txt || txt[0] < '0' || txt[0] > '9') return;
 
     uint8_t digit = (uint8_t)(txt[0] - '0');
 
     /* Bounce animation */
     lv_coord_t y0 = lv_obj_get_y(obj);
     lv_anim_t a;
     lv_anim_init(&a);
     lv_anim_set_var(&a, obj);
     lv_anim_set_exec_cb(&a, anim_obj_y);
     lv_anim_set_values(&a, y0, y0 - 5);
     lv_anim_set_time(&a, 70);
     lv_anim_set_playback_time(&a, 90);
     lv_anim_set_repeat_count(&a, 1);
     lv_anim_start(&a);
 
     s_selected_digit = (int8_t)digit;
 
     /* Show pattern name in the status label */
     const char *name = led_pattern_name(digit);
     char status[80];
     snprintf(status, sizeof(status), "#%u: %s\nTap SUBMIT to run!", (unsigned)digit, name);
     if (s_status_label) {
         lv_label_set_text(s_status_label, status);
     }
 
    /* Preview pulse on the LED strip */
    command_handler_enqueue_preview(digit);
 }
 
 /* Called when the green SUBMIT button is tapped.
  * Runs the full WS2812FX animation for the currently selected pattern. */
 static void submit_btn_cb(lv_obj_t *obj, lv_event_t event)
 {
     (void)obj;
     if (event != LV_EVENT_CLICKED) return;
 
     if (s_selected_digit < 0 || s_selected_digit > 9) {
         if (s_status_label)
             lv_label_set_text(s_status_label, "Pick a number first!");
         return;
     }
 
    uint8_t digit = (uint8_t)s_selected_digit;
    command_handler_submit_selection(digit);
    command_handler_enqueue_execute_digit(digit);
 
     char status[64];
     snprintf(status, sizeof(status), "Running #%u: %s",
              (unsigned)digit, led_pattern_name(digit));
     if (s_status_label)
         lv_label_set_text(s_status_label, status);
 }
 
 /* Factory for a single numpad key button at (x, y). */
 static lv_obj_t *create_key(lv_obj_t *parent, const char *text,
                              lv_coord_t x, lv_coord_t y)
 {
     lv_obj_t *btn = lv_btn_create(parent, NULL);
     lv_obj_set_size(btn, 68, 48);
     lv_obj_set_pos(btn, x, y);
     lv_obj_set_event_cb(btn, numpad_btn_cb);
     lv_obj_set_style_local_radius(btn, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, 12);
     lv_obj_set_style_local_border_width(btn, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, 2);
     lv_obj_set_style_local_border_color(btn, LV_BTN_PART_MAIN, LV_STATE_DEFAULT,
                                         LV_COLOR_WHITE);
 
     uint8_t d = 0;
     if (text && text[0] >= '0' && text[0] <= '9')
         d = (uint8_t)(text[0] - '0');
 
     lv_obj_set_style_local_bg_color(btn, LV_BTN_PART_MAIN, LV_STATE_DEFAULT,
                                     s_key_colors[d]);
     lv_obj_set_style_local_bg_color(btn, LV_BTN_PART_MAIN, LV_STATE_PRESSED,
                                     lv_color_darken(s_key_colors[d], LV_OPA_30));
     lv_obj_set_style_local_text_color(btn, LV_BTN_PART_MAIN, LV_STATE_DEFAULT,
                                       LV_COLOR_BLACK);
 
     lv_obj_t *label = lv_label_create(btn, NULL);
     lv_label_set_text(label, text);
     return btn;
 }
 
 /* Build the full numpad screen (title, status label, 10 keys, SUBMIT). */
 static lv_obj_t *create_numpad_screen(void)
 {
     lv_obj_t *scr = lv_obj_create(NULL, NULL);
     lv_obj_set_style_local_bg_color(scr, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT,
                                     lv_color_hex(0x1A1A2E));
     lv_obj_set_style_local_bg_opa(scr, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT,
                                   LV_OPA_COVER);
 
     /* Title */
     lv_obj_t *title = lv_label_create(scr, NULL);
     lv_label_set_text(title, "Choose a Pattern");
     lv_obj_set_style_local_text_color(title, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT,
                                       LV_COLOR_WHITE);
     lv_obj_align(title, NULL, LV_ALIGN_IN_TOP_MID, 0, 8);
 
     /* Yellow feedback label -- updated by numpad_btn_cb / submit_btn_cb */
     s_status_label = lv_label_create(scr, NULL);
     lv_label_set_text(s_status_label, "Pick a number, then SUBMIT.");
     lv_obj_set_style_local_text_color(s_status_label, LV_LABEL_PART_MAIN,
                                       LV_STATE_DEFAULT, lv_color_hex(0xFFE88A));
     lv_obj_align(s_status_label, NULL, LV_ALIGN_IN_TOP_MID, 0, 30);
     lv_obj_set_width(s_status_label, 220);
     lv_label_set_long_mode(s_status_label, LV_LABEL_LONG_BREAK);
     lv_label_set_align(s_status_label, LV_LABEL_ALIGN_CENTER);
 
     /* Numpad grid: 3 columns x 4 rows (row 4 has only "0" centered) */
     lv_coord_t x0 = 12, y0 = 60, dx = 76, dy = 52;
 
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
 
     /* Green SUBMIT button at the bottom */
     lv_obj_t *submit_btn = lv_btn_create(scr, NULL);
     lv_obj_set_size(submit_btn, 180, 40);
     lv_obj_align(submit_btn, NULL, LV_ALIGN_IN_BOTTOM_MID, 0, -6);
     lv_obj_set_event_cb(submit_btn, submit_btn_cb);
     lv_obj_set_style_local_radius(submit_btn, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, 14);
     lv_obj_set_style_local_bg_color(submit_btn, LV_BTN_PART_MAIN, LV_STATE_DEFAULT,
                                     lv_color_hex(0x7CF29A));
     lv_obj_set_style_local_bg_color(submit_btn, LV_BTN_PART_MAIN, LV_STATE_PRESSED,
                                     lv_color_hex(0x4ED86F));
     lv_obj_set_style_local_border_width(submit_btn, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, 2);
     lv_obj_set_style_local_border_color(submit_btn, LV_BTN_PART_MAIN, LV_STATE_DEFAULT,
                                         LV_COLOR_WHITE);
 
     lv_obj_t *submit_lbl = lv_label_create(submit_btn, NULL);
     lv_label_set_text(submit_lbl, "SUBMIT");
 
     return scr;
 }
 
 /* ══════════════════════════════════════════════════════════════════════════
  *  SCREEN 1 -- INTRO / SPLASH
  * ══════════════════════════════════════════════════════════════════════════ */
 
 /* START button callback: lazy-creates the numpad screen and slides to it. */
 static void start_btn_cb(lv_obj_t *obj, lv_event_t event)
 {
     (void)obj;
     if (event != LV_EVENT_CLICKED) return;
 
     /* Bounce animation */
     lv_coord_t y0 = lv_obj_get_y(obj);
     lv_anim_t a;
     lv_anim_init(&a);
     lv_anim_set_var(&a, obj);
     lv_anim_set_exec_cb(&a, anim_obj_y);
     lv_anim_set_values(&a, y0, y0 - 8);
     lv_anim_set_time(&a, 80);
     lv_anim_set_playback_time(&a, 100);
     lv_anim_set_repeat_count(&a, 1);
     lv_anim_start(&a);
 
     if (!s_numpad_screen) {
         s_numpad_screen = create_numpad_screen();
     }
     lv_scr_load_anim(s_numpad_screen, LV_SCR_LOAD_ANIM_MOVE_LEFT, 250, 0, false);
 }
 
 /* Build the intro splash screen. */
 static lv_obj_t *create_intro_screen(void)
 {
     lv_obj_t *scr = lv_obj_create(NULL, NULL);
     lv_obj_set_style_local_bg_color(scr, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT,
                                     lv_color_hex(0x0F0F23));
     lv_obj_set_style_local_bg_opa(scr, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT,
                                   LV_OPA_COVER);
 
     /* Brand title: "Blocks o' Code" in cyan */
     lv_obj_t *title = lv_label_create(scr, NULL);
     lv_label_set_text(title, "Blocks o' Code");
     lv_obj_set_style_local_text_color(title, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT,
                                       lv_color_hex(0x00E5FF));
     lv_obj_align(title, NULL, LV_ALIGN_CENTER, 0, -70);
 
     /* Version badge */
     lv_obj_t *version = lv_label_create(scr, NULL);
     lv_label_set_text(version, "(v3)");
     lv_obj_set_style_local_text_color(version, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT,
                                       lv_color_hex(0x80D0FF));
     lv_obj_align(version, title, LV_ALIGN_OUT_BOTTOM_MID, 0, 4);
 
     /* Subtle tagline */
     lv_obj_t *tagline = lv_label_create(scr, NULL);
     lv_label_set_text(tagline, "LED Pattern Block");
     lv_obj_set_style_local_text_color(tagline, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT,
                                       lv_color_hex(0x888888));
     lv_obj_align(tagline, version, LV_ALIGN_OUT_BOTTOM_MID, 0, 14);
 
     /* Big START button with cyan glow */
     lv_obj_t *btn = lv_btn_create(scr, NULL);
     lv_obj_set_size(btn, 180, 60);
     lv_obj_align(btn, NULL, LV_ALIGN_CENTER, 0, 60);
     lv_obj_set_event_cb(btn, start_btn_cb);
     lv_obj_set_style_local_radius(btn, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, 18);
     lv_obj_set_style_local_bg_color(btn, LV_BTN_PART_MAIN, LV_STATE_DEFAULT,
                                     lv_color_hex(0x00E5FF));
     lv_obj_set_style_local_bg_color(btn, LV_BTN_PART_MAIN, LV_STATE_PRESSED,
                                     lv_color_hex(0x00B8D4));
     lv_obj_set_style_local_border_width(btn, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, 3);
     lv_obj_set_style_local_border_color(btn, LV_BTN_PART_MAIN, LV_STATE_DEFAULT,
                                         lv_color_hex(0xFFFFFF));
     lv_obj_set_style_local_shadow_width(btn, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, 12);
     lv_obj_set_style_local_shadow_color(btn, LV_BTN_PART_MAIN, LV_STATE_DEFAULT,
                                         lv_color_hex(0x00E5FF));
     lv_obj_set_style_local_shadow_opa(btn, LV_BTN_PART_MAIN, LV_STATE_DEFAULT,
                                       LV_OPA_60);
 
     lv_obj_t *btn_label = lv_label_create(btn, NULL);
     lv_label_set_text(btn_label, "START");
     lv_obj_set_style_local_text_color(btn_label, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT,
                                       lv_color_hex(0x0F0F23));
 
     return scr;
 }
 
 /* ══════════════════════════════════════════════════════════════════════════
  *  LVGL TASK
  * ══════════════════════════════════════════════════════════════════════════ */
 
 /* Dedicated FreeRTOS task (core 1) that pumps the LVGL task handler.
  * Loads the intro screen on first run, then loops at ~100 Hz. */
 static void gui_task(void *arg)
 {
     (void)arg;
     if (!s_intro_screen) {
         s_intro_screen = create_intro_screen();
     }
     lv_scr_load(s_intro_screen);
 
     while (1) {
         lv_task_handler();
         vTaskDelay(pdMS_TO_TICKS(10));
     }
 }
 
 /* ══════════════════════════════════════════════════════════════════════════
  *  PUBLIC ENTRY POINT
  * ══════════════════════════════════════════════════════════════════════════ */
 
 /**
  * Initialise LVGL, the display/touch drivers, enable the backlight,
  * and spawn the GUI task.  Call once from app_main(); returns immediately.
  *
  * Uses double-buffered DMA memory for smooth SPI flush throughput.
  * The GUI task runs on core 1 to keep core 0 free for I2C + LED work.
  */
 void tft_ui_start(void)
 {
     if (s_ui_started) {
         ESP_LOGW(TAG, "TFT UI already started");
         return;
     }
 
     lv_init();
     lvgl_driver_init();
 
     /* Allocate two DMA-capable display buffers for double-buffering. */
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
 
     /* Register display driver */
     lv_disp_drv_t disp_drv;
     lv_disp_drv_init(&disp_drv);
     disp_drv.flush_cb = disp_driver_flush;
     disp_drv.buffer = &disp_buf;
     lv_disp_drv_register(&disp_drv);
 
     /* Register touch input driver */
     lv_indev_drv_t indev_drv;
     lv_indev_drv_init(&indev_drv);
     indev_drv.type = LV_INDEV_TYPE_POINTER;
     indev_drv.read_cb = touch_driver_read;
     lv_indev_drv_register(&indev_drv);
 
     /* Start the LVGL tick timer (1 ms period via esp_timer). */
     const esp_timer_create_args_t periodic_timer_args = {
         .callback = &lv_tick_task,
         .name = "lv_tick"
     };
     esp_timer_handle_t periodic_timer;
     ESP_ERROR_CHECK(esp_timer_create(&periodic_timer_args, &periodic_timer));
     ESP_ERROR_CHECK(esp_timer_start_periodic(periodic_timer, LV_TICK_PERIOD_MS * 1000));
 
     /* Turn on the TFT backlight (GPIO 32, active-high). */
     gpio_reset_pin(LCD_BACKLIGHT);
     gpio_set_direction(LCD_BACKLIGHT, GPIO_MODE_OUTPUT);
     gpio_set_level(LCD_BACKLIGHT, 1);
 
     /* Spawn the GUI task on core 1 (4 KB stack, priority 5). */
     BaseType_t ok = xTaskCreatePinnedToCore(gui_task, "led_ui", 4096, NULL, 5, NULL, 1);
     if (ok != pdPASS) {
         ESP_LOGE(TAG, "Failed to create GUI task");
         return;
     }
 
     s_ui_started = true;
     ESP_LOGI(TAG, "TFT UI started");
 }
 