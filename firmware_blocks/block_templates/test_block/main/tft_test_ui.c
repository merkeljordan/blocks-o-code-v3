#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

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

#include "battery_monitor.h"
#include "tft_test_ui.h"

static const char *TAG = "TFT_TEST_UI";

/* TFT + touch wiring and runtime config (matches existing blocks). */
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
#define BATTERY_REFRESH_MS         3000U

#define PIN_NUM_BK_LIGHT           32
#define PIN_NUM_SCLK               18
#define PIN_NUM_MOSI               23
#define PIN_NUM_MISO               19
#define PIN_NUM_LCD_DC             14
#define PIN_NUM_LCD_RST            4
#define PIN_NUM_LCD_CS             27
#define PIN_NUM_TOUCH_CS           26
#define PIN_NUM_TOUCH_IRQ          36

static bool s_ui_started = false;

static lv_display_t *s_display = NULL;
static esp_lcd_panel_handle_t s_panel_handle = NULL;
static esp_lcd_touch_handle_t s_touch_handle = NULL;
static esp_timer_handle_t s_lvgl_tick_timer = NULL;

static SemaphoreHandle_t s_state_mutex = NULL;
static volatile bool s_dirty = false;

static char s_lines[TFT_TEST_LINE_COUNT][64];
static lv_obj_t *s_labels[TFT_TEST_LINE_COUNT];
static lv_obj_t *s_touch_catcher = NULL;

typedef struct {
    lv_obj_t *root;
    lv_obj_t *fill;
    lv_obj_t *text;
} battery_indicator_t;

static battery_indicator_t s_battery = {0};

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

static void refresh_battery_indicator(void)
{
    update_battery_indicator(&s_battery, (unsigned)battery_monitor_get_percent());
}

static void lvgl_tick_cb(void *arg)
{
    (void)arg;
    lv_tick_inc(TFT_TICK_PERIOD_MS);
}

static void anim_obj_y(void *obj, int32_t value)
{
    lv_obj_set_y((lv_obj_t *)obj, (lv_coord_t)value);
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

    if (esp_lcd_touch_get_data(tp, point_data, &point_cnt, 1) == ESP_OK && point_cnt > 0) {
        data->point.x = point_data[0].x;
        data->point.y = point_data[0].y;
        data->state = LV_INDEV_STATE_PRESSED;
    }
}

static void touch_catcher_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_PRESSED) return;
    /* Keep it simple: the goal is to validate the touch stack wires up. */
    tft_test_ui_set_line(TFT_TEST_LINE_TOUCH, "Touch detected!");
}

static void gui_refresh_labels_if_dirty(void)
{
    if (!s_dirty) return;
    if (s_state_mutex == NULL) return;

    if (xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(5)) != pdTRUE) return;

    for (int i = 0; i < (int)TFT_TEST_LINE_COUNT; i++) {
        if (s_labels[i]) {
            lv_label_set_text(s_labels[i], s_lines[i]);
        }
    }
    s_dirty = false;
    xSemaphoreGive(s_state_mutex);
}

static void lvgl_task(void *arg)
{
    (void)arg;
    TickType_t last_battery_refresh = 0;
    ESP_LOGI(TAG, "LVGL task started");

    refresh_battery_indicator();
    last_battery_refresh = xTaskGetTickCount();

    while (1) {
        TickType_t now = xTaskGetTickCount();
        gui_refresh_labels_if_dirty();

        uint32_t delay_ms = lv_timer_handler();
        if ((now - last_battery_refresh) >= pdMS_TO_TICKS(BATTERY_REFRESH_MS)) {
            refresh_battery_indicator();
            last_battery_refresh = now;
        }
        if (delay_ms < 5)       delay_ms = 5;
        else if (delay_ms > 30) delay_ms = 30;
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

esp_err_t tft_test_ui_start(void)
{
    if (s_ui_started) {
        ESP_LOGW(TAG, "TFT UI already started");
        return ESP_OK;
    }
    s_ui_started = true;

    s_state_mutex = xSemaphoreCreateMutex();
    if (s_state_mutex == NULL) return ESP_ERR_NO_MEM;

    /* Default line texts */
    snprintf(s_lines[TFT_TEST_LINE_MATRIX], sizeof(s_lines[0]),  "Matrix: (pending)");
    snprintf(s_lines[TFT_TEST_LINE_LEDSTRIP], sizeof(s_lines[0]), "Ledstrip: (pending)");
    snprintf(s_lines[TFT_TEST_LINE_SPEAKER], sizeof(s_lines[0]), "Speaker: (pending)");
    snprintf(s_lines[TFT_TEST_LINE_TOUCH], sizeof(s_lines[0]),  "Touch: tap screen");

    ESP_LOGI(TAG, "Starting TFT test UI");

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
    ESP_ERROR_CHECK(esp_lcd_panel_io_register_event_callbacks(io_handle, &io_callbacks, s_display));

    /* XPT2046 touch on same SPI bus with separate CS. */
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
    ESP_ERROR_CHECK(esp_lcd_touch_new_spi_xpt2046(tp_io_handle, &tp_cfg, &s_touch_handle));

    /* Register touch as LVGL pointer input device. */
    lv_indev_t *indev = lv_indev_create();
    assert(indev != NULL);
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(indev, s_display);
    lv_indev_set_user_data(indev, s_touch_handle);
    lv_indev_set_long_press_time(indev, 800);
    lv_indev_set_read_cb(indev, touch_read_cb);

    /* Full-screen invisible touch catcher for event callback. */
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0F0F23), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "Peripheral Test");
    lv_obj_set_style_text_color(title, lv_color_hex(0x00E5FF), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 14);

    const char *initial_text[TFT_TEST_LINE_COUNT] = {s_lines[0], s_lines[1], s_lines[2], s_lines[3]};
    (void)initial_text;

    for (int i = 0; i < (int)TFT_TEST_LINE_COUNT; i++) {
        s_labels[i] = lv_label_create(scr);
        lv_label_set_text(s_labels[i], s_lines[i]);
        lv_obj_set_style_text_color(s_labels[i], lv_color_hex(0xFFFFFF), 0);
        lv_obj_align(s_labels[i], LV_ALIGN_TOP_MID, 0, 56 + 28 * i);
    }

    s_touch_catcher = lv_obj_create(scr);
    lv_obj_set_size(s_touch_catcher, TFT_H_RES, TFT_V_RES);
    lv_obj_set_style_bg_opa(s_touch_catcher, 0, 0);
    lv_obj_add_event_cb(s_touch_catcher, touch_catcher_event_cb, LV_EVENT_PRESSED, NULL);

    create_battery_indicator(scr, &s_battery);
    refresh_battery_indicator();
    if (s_battery.root != NULL) {
        lv_obj_move_foreground(s_battery.root);
    }

    /* Start LVGL tick timer. */
    const esp_timer_create_args_t tick_args = {
        .callback = lvgl_tick_cb,
        .name = "lvgl_tick",
    };
    ESP_ERROR_CHECK(esp_timer_create(&tick_args, &s_lvgl_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(s_lvgl_tick_timer, TFT_TICK_PERIOD_MS * 1000));

    /* Spawn LVGL task on core 1. */
    BaseType_t ok = xTaskCreatePinnedToCore(lvgl_task, "tft_test_ui", TFT_TASK_STACK_SIZE, NULL,
                                            TFT_TASK_PRIORITY, NULL, 1);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create LVGL task");
        abort();
    }

    s_dirty = true;
    ESP_LOGI(TAG, "TFT test UI started");
    return ESP_OK;
}

void tft_test_ui_set_line(tft_test_line_t line, const char *text)
{
    if (line < 0 || line >= TFT_TEST_LINE_COUNT) return;
    if (text == NULL) text = "";
    if (s_state_mutex == NULL) return;

    if (xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(20)) != pdTRUE) return;

    snprintf(s_lines[line], sizeof(s_lines[line]), "%s", text);
    s_dirty = true;

    xSemaphoreGive(s_state_mutex);
}

