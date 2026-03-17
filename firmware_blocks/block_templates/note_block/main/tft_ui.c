/*
 * tft_ui.c  --  NOTE Block: TFT Touchscreen UI (LVGL v9)
 *
 * Mirrors the LED Color Flash block UX:
 * - Intro screen with START
 * - Note picker screen (A–G)
 *   - Tap a note to preview the tone
 *   - Tap SUBMIT to publish selection to the Brain
 */
 
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

#if !defined(NOTE_UI_SIMULATOR)
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"

#include "esp_lcd_ili9341.h"
#include "esp_lcd_touch_xpt2046.h"
#endif

#include "tft_ui.h"

// Implemented in main.c
extern void note_block_preview_note(uint8_t note_id);
extern bool note_block_submit_selection(uint8_t note_id);

#define TAG "NOTE_UI_V9"

#if !defined(NOTE_UI_SIMULATOR)
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

static bool                   s_ui_started = false;
static lv_display_t          *s_display = NULL;
static esp_lcd_panel_handle_t s_panel_handle = NULL;
static esp_lcd_touch_handle_t s_touch_handle = NULL;
static esp_timer_handle_t     s_lvgl_tick_timer = NULL;
#else
static bool s_ui_started = false;
#endif

static lv_obj_t *s_intro_screen = NULL;
static lv_obj_t *s_picker_screen = NULL;
static lv_obj_t *s_status_label = NULL;
static int8_t s_selected_note = -1; // 0..6
static lv_obj_t *s_wave_container = NULL;
static lv_obj_t *s_wave_bars[5] = {0};

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

static void wave_hide_timer_cb(lv_timer_t *t)
{
    if (s_wave_container) {
        lv_obj_add_flag(s_wave_container, LV_OBJ_FLAG_HIDDEN);
    }
    if (t) {
        lv_timer_del(t);
    }
}

static void animate_wave_once(void)
{
    if (!s_wave_container) {
        return;
    }

    lv_obj_clear_flag(s_wave_container, LV_OBJ_FLAG_HIDDEN);

    static const int32_t heights[5] = {18, 28, 40, 28, 18};
    for (int i = 0; i < 5; i++) {
        if (!s_wave_bars[i]) continue;

        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, s_wave_bars[i]);
        lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_height);
        lv_anim_set_values(&a, 8, heights[i]);
        lv_anim_set_time(&a, 120);
        lv_anim_set_playback_time(&a, 120);
        lv_anim_set_delay(&a, (uint32_t)(i * 20));
        lv_anim_set_repeat_count(&a, 2);
        lv_anim_start(&a);
    }

    // Let the animation run longer than the ~400ms tone.
    (void)lv_timer_create(wave_hide_timer_cb, 800, NULL);
}

#if !defined(NOTE_UI_SIMULATOR)
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

    if (tp == NULL) return;
    if (esp_lcd_touch_read_data(tp) != ESP_OK) return;

    if (esp_lcd_touch_get_data(tp, point_data, &point_cnt, 1) == ESP_OK &&
        point_cnt > 0) {
        data->point.x = point_data[0].x;
        data->point.y = point_data[0].y;
        data->state = LV_INDEV_STATE_PRESSED;
    }
}
#endif

static const char *note_name(uint8_t note_id)
{
    static const char *k_names[7] = {"A", "B", "C", "D", "E", "F", "G"};
    if (note_id < 7) return k_names[note_id];
    return "?";
}

static lv_obj_t *create_intro_screen(void);
static lv_obj_t *create_picker_screen(void);

static void open_picker_screen(void)
{
    if (s_picker_screen == NULL) {
        s_picker_screen = create_picker_screen();
    }
    s_selected_note = -1;
    if (s_status_label) {
        lv_label_set_text(s_status_label, "Tap A-G to preview, then SUBMIT.");
    }
    lv_screen_load_anim(s_picker_screen, LV_SCR_LOAD_ANIM_MOVE_LEFT, 250, 0, false);
}

static void start_btn_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    open_picker_screen();
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

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "Blocks o' Code");
    lv_obj_set_style_text_color(title, lv_color_hex(0x00E5FF), 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -70);

    lv_obj_t *subtitle = lv_label_create(scr);
    lv_label_set_text(subtitle, "NOTE Block");
    lv_obj_set_style_text_color(subtitle, lv_color_hex(0x888888), 0);
    lv_obj_align_to(subtitle, title, LV_ALIGN_OUT_BOTTOM_MID, 0, 14);

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

static void note_btn_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

    lv_obj_t *btn = lv_event_get_target_obj(e);
    const uint8_t note_id = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    s_selected_note = (int8_t)note_id;

    if (s_status_label) {
        lv_label_set_text_fmt(s_status_label, "Selected %s. Tap SUBMIT to send.", note_name(note_id));
    }

    animate_button_bounce(btn, 5);
    animate_wave_once();

    // Preview immediately (runs on UI task core 1).
    note_block_preview_note(note_id);
}

static lv_obj_t *create_note_key(lv_obj_t *parent, const char *text,
                                 lv_coord_t x, lv_coord_t y, lv_color_t color,
                                 uint8_t note_id)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, 68, 48);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_style_radius(btn, 12, 0);
    lv_obj_set_style_border_width(btn, 2, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_color(btn, color, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(btn, lv_color_darken(color, LV_OPA_30), LV_STATE_PRESSED);
    lv_obj_set_style_text_color(btn, lv_color_hex(0x000000), 0);
    lv_obj_add_event_cb(btn, note_btn_event_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)note_id);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    return btn;
}

static void submit_btn_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

    if (s_selected_note < 0 || s_selected_note > 6) {
        if (s_status_label) lv_label_set_text(s_status_label, "Pick A–G first!");
        return;
    }

    bool ok = note_block_submit_selection((uint8_t)s_selected_note);
    if (!ok) {
        if (s_status_label) lv_label_set_text(s_status_label, "Busy, try again.");
        return;
    }

    if (s_status_label) {
        lv_label_set_text_fmt(s_status_label, "Submitted %s", note_name((uint8_t)s_selected_note));
    }
}

static lv_obj_t *create_picker_screen(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_set_style_radius(scr, 0, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x1A1A2E), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "Choose a Note");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

    s_status_label = lv_label_create(scr);
    lv_label_set_text(s_status_label, "Tap A-G to preview, then SUBMIT.");
    lv_obj_set_width(s_status_label, 220);
    lv_label_set_long_mode(s_status_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(s_status_label, lv_color_hex(0xFFE88A), 0);
    lv_obj_set_style_text_align(s_status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_status_label, LV_ALIGN_TOP_MID, 0, 30);

    // Little "sound wave" animation (hidden until a note is tapped).
    s_wave_container = lv_obj_create(scr);
    lv_obj_set_size(s_wave_container, 120, 48);
    // Place between note keys and SUBMIT (bottom gap).
    lv_obj_align(s_wave_container, LV_ALIGN_BOTTOM_MID, 12, -48);
    lv_obj_set_style_bg_opa(s_wave_container, LV_OPA_0, 0);
    lv_obj_set_style_border_width(s_wave_container, 0, 0);
    lv_obj_set_style_pad_all(s_wave_container, 0, 0);
    lv_obj_add_flag(s_wave_container, LV_OBJ_FLAG_HIDDEN);

    for (int i = 0; i < 5; i++) {
        lv_obj_t *bar = lv_obj_create(s_wave_container);
        s_wave_bars[i] = bar;
        lv_obj_set_size(bar, 12, 8);
        lv_obj_set_style_radius(bar, 6, 0);
        lv_obj_set_style_border_width(bar, 0, 0);
        lv_obj_set_style_bg_color(bar, lv_color_hex(0x00E5FF), 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_90, 0);
        lv_obj_align(bar, LV_ALIGN_BOTTOM_LEFT, i * 22, 0);
    }

    // Layout: 3 columns; A B C / D E F / G centered
    const lv_coord_t x0 = 12, y0 = 70, dx = 76, dy = 52;
    create_note_key(scr, "A", x0 + dx * 0, y0 + dy * 0, lv_color_hex(0xFFB3BA), 0);
    create_note_key(scr, "B", x0 + dx * 1, y0 + dy * 0, lv_color_hex(0xFFDFBA), 1);
    create_note_key(scr, "C", x0 + dx * 2, y0 + dy * 0, lv_color_hex(0xFFFFBA), 2);
    create_note_key(scr, "D", x0 + dx * 0, y0 + dy * 1, lv_color_hex(0xBAFFC9), 3);
    create_note_key(scr, "E", x0 + dx * 1, y0 + dy * 1, lv_color_hex(0xBAE1FF), 4);
    create_note_key(scr, "F", x0 + dx * 2, y0 + dy * 1, lv_color_hex(0xD7BAFF), 5);
    create_note_key(scr, "G", x0 + dx * 1, y0 + dy * 2, lv_color_hex(0xB8F2E6), 6);

    lv_obj_t *submit_btn = lv_button_create(scr);
    lv_obj_set_size(submit_btn, 180, 40);
    lv_obj_align(submit_btn, LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_obj_set_style_radius(submit_btn, 14, 0);
    lv_obj_set_style_bg_color(submit_btn, lv_color_hex(0x7CF29A), 0);
    lv_obj_set_style_bg_color(submit_btn, lv_color_hex(0x4ED86F), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(submit_btn, 2, 0);
    lv_obj_set_style_border_color(submit_btn, lv_color_hex(0xFFFFFF), 0);
    lv_obj_add_event_cb(submit_btn, submit_btn_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *submit_label = lv_label_create(submit_btn);
    lv_label_set_text(submit_label, "SUBMIT");
    lv_obj_center(submit_label);

    return scr;
}

#if !defined(NOTE_UI_SIMULATOR)
static void lvgl_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "LVGL task started");

    if (s_intro_screen == NULL) {
        s_intro_screen = create_intro_screen();
    }
    lv_screen_load(s_intro_screen);

    while (1) {
        uint32_t delay_ms = lv_timer_handler();
        if (delay_ms < 5) delay_ms = 5;
        else if (delay_ms > 30) delay_ms = 30;
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}
#endif

void tft_ui_start(void)
{
#if defined(NOTE_UI_SIMULATOR)
    if (s_ui_started) {
        return;
    }
    s_ui_started = true;

    if (s_intro_screen == NULL) {
        s_intro_screen = create_intro_screen();
    }
    lv_screen_load(s_intro_screen);
    return;
#else
    if (s_ui_started) {
        ESP_LOGW(TAG, "TFT UI already started");
        return;
    }
    s_ui_started = true;

    ESP_LOGI(TAG, "Starting LVGL v9 TFT UI");

    gpio_config_t bk_cfg = {
        .pin_bit_mask = 1ULL << PIN_NUM_BK_LIGHT,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&bk_cfg));
    gpio_set_level(PIN_NUM_BK_LIGHT, !TFT_BACKLIGHT_ON_LEVEL);

    spi_bus_config_t buscfg = {
        .sclk_io_num = PIN_NUM_SCLK,
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = PIN_NUM_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = TFT_H_RES * TFT_DRAW_BUF_LINES * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(TFT_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));

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
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)TFT_SPI_HOST, &io_cfg, &io_handle));

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

    gpio_set_level(PIN_NUM_BK_LIGHT, TFT_BACKLIGHT_ON_LEVEL);

    lv_init();

    s_display = lv_display_create(TFT_H_RES, TFT_V_RES);
    assert(s_display != NULL);

    const size_t draw_buf_sz = TFT_H_RES * TFT_DRAW_BUF_LINES * sizeof(lv_color16_t);
    void *buf1 = spi_bus_dma_memory_alloc(TFT_SPI_HOST, draw_buf_sz, 0);
    void *buf2 = spi_bus_dma_memory_alloc(TFT_SPI_HOST, draw_buf_sz, 0);
    assert(buf1 != NULL);
    assert(buf2 != NULL);

    lv_display_set_buffers(s_display, buf1, buf2, draw_buf_sz, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_user_data(s_display, s_panel_handle);
    lv_display_set_color_format(s_display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(s_display, lcd_flush_cb);

    const esp_lcd_panel_io_callbacks_t io_callbacks = {
        .on_color_trans_done = lcd_flush_ready_cb,
    };
    ESP_ERROR_CHECK(esp_lcd_panel_io_register_event_callbacks(io_handle, &io_callbacks, s_display));

    esp_lcd_panel_io_handle_t tp_io_handle = NULL;
    esp_lcd_panel_io_spi_config_t tp_io_cfg = ESP_LCD_TOUCH_IO_SPI_XPT2046_CONFIG(PIN_NUM_TOUCH_CS);
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)TFT_SPI_HOST, &tp_io_cfg, &tp_io_handle));

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
    ESP_ERROR_CHECK(esp_lcd_touch_new_spi_xpt2046(tp_io_handle, &tp_cfg, &s_touch_handle));

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

    const esp_timer_create_args_t tick_args = {
        .callback = lvgl_tick_cb,
        .name = "lvgl_tick",
    };
    ESP_ERROR_CHECK(esp_timer_create(&tick_args, &s_lvgl_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(s_lvgl_tick_timer, TFT_TICK_PERIOD_MS * 1000));

    BaseType_t ok = xTaskCreatePinnedToCore(lvgl_task, "note_ui_v9",
                                           TFT_TASK_STACK_SIZE, NULL,
                                           TFT_TASK_PRIORITY, NULL, 1);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create LVGL task");
        abort();
    }

    ESP_LOGI(TAG, "LVGL v9 TFT UI started");
#endif
}

