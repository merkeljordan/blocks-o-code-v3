#include "control_flow_tft_hw.h"

#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lcd_ili9341.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_touch_xpt2046.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#define TAG "CTRL_FLOW_TFT_HW"

#define TFT_SPI_HOST            SPI3_HOST
#define TFT_PIXEL_CLOCK_HZ      (20 * 1000 * 1000)
#define TFT_CMD_BITS            8
#define TFT_PARAM_BITS          8
#define TFT_H_RES               240
#define TFT_V_RES               320
#define TFT_DRAW_BUF_LINES      20
#define TFT_TICK_PERIOD_MS      2
#define TFT_TASK_STACK_SIZE     (6 * 1024)
#define TFT_TASK_PRIORITY       5
#define TFT_BACKLIGHT_ON_LEVEL  1

#ifndef CONTROL_FLOW_TFT_PANEL_MIRROR_X
#define CONTROL_FLOW_TFT_PANEL_MIRROR_X 1
#endif

#ifndef CONTROL_FLOW_TFT_PANEL_MIRROR_Y
#define CONTROL_FLOW_TFT_PANEL_MIRROR_Y 0
#endif

#ifndef CONTROL_FLOW_TFT_TOUCH_SWAP_XY
#define CONTROL_FLOW_TFT_TOUCH_SWAP_XY 0
#endif

#ifndef CONTROL_FLOW_TFT_TOUCH_MIRROR_X
#define CONTROL_FLOW_TFT_TOUCH_MIRROR_X 0
#endif

#ifndef CONTROL_FLOW_TFT_TOUCH_MIRROR_Y
#define CONTROL_FLOW_TFT_TOUCH_MIRROR_Y 1
#endif

#define PIN_NUM_BK_LIGHT        32
#define PIN_NUM_SCLK            18
#define PIN_NUM_MOSI            23
#define PIN_NUM_MISO            19
#define PIN_NUM_LCD_DC          14
#define PIN_NUM_LCD_RST         4
#define PIN_NUM_LCD_CS          27
#define PIN_NUM_TOUCH_CS        26
#define PIN_NUM_TOUCH_IRQ       36

#if CONFIG_FREERTOS_UNICORE
#define CONTROL_FLOW_UI_CORE_ID 0
#else
#define CONTROL_FLOW_UI_CORE_ID 1
#endif

static bool s_ui_started = false;
static lv_display_t *s_display = NULL;
static esp_lcd_panel_handle_t s_panel_handle = NULL;
static esp_lcd_touch_handle_t s_touch_handle = NULL;
static esp_timer_handle_t s_lvgl_tick_timer = NULL;
static control_flow_ui_config_t s_cfg;

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
        point_cnt > 0) {
        data->point.x = point_data[0].x;
        data->point.y = point_data[0].y;
        data->state = LV_INDEV_STATE_PRESSED;
    }
}

static void lvgl_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "LVGL task started");

    control_flow_tft_ui_start(&s_cfg);
    control_flow_tft_ui_set_idle();

    while (1) {
        uint32_t delay_ms = lv_timer_handler();
        if (delay_ms < 5) {
            delay_ms = 5;
        } else if (delay_ms > 30) {
            delay_ms = 30;
        }
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

esp_err_t control_flow_tft_hw_start(const control_flow_ui_config_t *cfg)
{
    esp_err_t err;

    if (cfg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_ui_started) {
        return ESP_OK;
    }

    memset(&s_cfg, 0, sizeof(s_cfg));
    s_cfg = *cfg;

    {
        gpio_config_t bk_cfg = {
            .pin_bit_mask = 1ULL << PIN_NUM_BK_LIGHT,
            .mode = GPIO_MODE_OUTPUT,
        };
        err = gpio_config(&bk_cfg);
        if (err != ESP_OK) {
            return err;
        }
    }
    gpio_set_level(PIN_NUM_BK_LIGHT, !TFT_BACKLIGHT_ON_LEVEL);

    {
        spi_bus_config_t buscfg = {
            .sclk_io_num = PIN_NUM_SCLK,
            .mosi_io_num = PIN_NUM_MOSI,
            .miso_io_num = PIN_NUM_MISO,
            .quadwp_io_num = -1,
            .quadhd_io_num = -1,
            .max_transfer_sz = TFT_H_RES * TFT_DRAW_BUF_LINES * sizeof(uint16_t),
        };
        err = spi_bus_initialize(TFT_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO);
        if (err != ESP_OK) {
            return err;
        }
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
        err = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)TFT_SPI_HOST, &io_cfg, &io_handle);
        if (err != ESP_OK) {
            return err;
        }

        {
            esp_lcd_panel_dev_config_t panel_cfg = {
                .reset_gpio_num = PIN_NUM_LCD_RST,
                .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
                .bits_per_pixel = 16,
            };
            err = esp_lcd_new_panel_ili9341(io_handle, &panel_cfg, &s_panel_handle);
            if (err != ESP_OK) {
                return err;
            }
        }

        err = esp_lcd_panel_reset(s_panel_handle);
        if (err != ESP_OK) {
            return err;
        }
        err = esp_lcd_panel_init(s_panel_handle);
        if (err != ESP_OK) {
            return err;
        }
        err = esp_lcd_panel_mirror(s_panel_handle,
                                   CONTROL_FLOW_TFT_PANEL_MIRROR_X,
                                   CONTROL_FLOW_TFT_PANEL_MIRROR_Y);
        if (err != ESP_OK) {
            return err;
        }
        err = esp_lcd_panel_disp_on_off(s_panel_handle, true);
        if (err != ESP_OK) {
            return err;
        }

        lv_init();

        s_display = lv_display_create(TFT_H_RES, TFT_V_RES);
        if (s_display == NULL) {
            return ESP_ERR_NO_MEM;
        }

        {
            const size_t draw_buf_sz = TFT_H_RES * TFT_DRAW_BUF_LINES * sizeof(lv_color16_t);
            void *buf1 = spi_bus_dma_memory_alloc(TFT_SPI_HOST, draw_buf_sz, 0);
            void *buf2 = spi_bus_dma_memory_alloc(TFT_SPI_HOST, draw_buf_sz, 0);
            if (buf1 == NULL || buf2 == NULL) {
                return ESP_ERR_NO_MEM;
            }
            lv_display_set_buffers(s_display, buf1, buf2, draw_buf_sz, LV_DISPLAY_RENDER_MODE_PARTIAL);
        }

        lv_display_set_user_data(s_display, s_panel_handle);
        lv_display_set_color_format(s_display, LV_COLOR_FORMAT_RGB565);
        lv_display_set_flush_cb(s_display, lcd_flush_cb);

        {
            const esp_lcd_panel_io_callbacks_t io_callbacks = {
                .on_color_trans_done = lcd_flush_ready_cb,
            };
            err = esp_lcd_panel_io_register_event_callbacks(io_handle, &io_callbacks, s_display);
            if (err != ESP_OK) {
                return err;
            }
        }

        {
            esp_lcd_panel_io_handle_t tp_io_handle = NULL;
            esp_lcd_panel_io_spi_config_t tp_io_cfg =
                ESP_LCD_TOUCH_IO_SPI_XPT2046_CONFIG(PIN_NUM_TOUCH_CS);
            err = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)TFT_SPI_HOST, &tp_io_cfg, &tp_io_handle);
            if (err != ESP_OK) {
                return err;
            }

            {
                esp_lcd_touch_config_t tp_cfg = {
                    .x_max = TFT_H_RES,
                    .y_max = TFT_V_RES,
                    .rst_gpio_num = -1,
                    .int_gpio_num = PIN_NUM_TOUCH_IRQ,
                    .flags = {
                        .swap_xy = CONTROL_FLOW_TFT_TOUCH_SWAP_XY,
                        .mirror_x = CONTROL_FLOW_TFT_TOUCH_MIRROR_X,
                        .mirror_y = CONTROL_FLOW_TFT_TOUCH_MIRROR_Y,
                    },
                };
                err = esp_lcd_touch_new_spi_xpt2046(tp_io_handle, &tp_cfg, &s_touch_handle);
                if (err != ESP_OK) {
                    return err;
                }
            }
        }
    }

    gpio_set_level(PIN_NUM_BK_LIGHT, TFT_BACKLIGHT_ON_LEVEL);

    {
        lv_indev_t *indev = lv_indev_create();
        if (indev == NULL) {
            return ESP_ERR_NO_MEM;
        }
        lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_display(indev, s_display);
        lv_indev_set_user_data(indev, s_touch_handle);
        lv_indev_set_long_press_time(indev, 800);
        lv_indev_set_read_cb(indev, touch_read_cb);

        {
            lv_timer_t *read_timer = lv_indev_get_read_timer(indev);
            if (read_timer != NULL) {
                lv_timer_set_period(read_timer, 10);
            }
        }
    }

    {
        const esp_timer_create_args_t tick_args = {
            .callback = lvgl_tick_cb,
            .name = "ctrl_lvgl_tick",
        };
        err = esp_timer_create(&tick_args, &s_lvgl_tick_timer);
        if (err != ESP_OK) {
            return err;
        }
        err = esp_timer_start_periodic(s_lvgl_tick_timer, TFT_TICK_PERIOD_MS * 1000);
        if (err != ESP_OK) {
            return err;
        }
    }

    if (xTaskCreatePinnedToCore(lvgl_task,
                                "control_flow_ui",
                                TFT_TASK_STACK_SIZE,
                                NULL,
                                TFT_TASK_PRIORITY,
                                NULL,
                                CONTROL_FLOW_UI_CORE_ID) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    s_ui_started = true;
    ESP_LOGI(TAG, "Control-flow TFT UI started on core %d", CONTROL_FLOW_UI_CORE_ID);
    return ESP_OK;
}
