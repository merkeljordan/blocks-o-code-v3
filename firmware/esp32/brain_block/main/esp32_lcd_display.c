// libc includes
#include <time.h>
//#include <errno.h>
//#include <sys/fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// freee RTOS related includes
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"

// esp-idf includes
#include "esp_timer.h"
#include "esp_system.h"
//#include "esp_wifi.h"
//#include "esp_event.h"
//#include "esp_sntp.h"
#include "esp_log.h"

#include "driver/gpio.h"
//#include "nvs_flash.h"

// LVGL related include
#include "lvgl.h"
#include "lvgl_helpers.h"

#include "lcd_gui.h"
#include "tft_ui.h"


#define LCD_BACKLIGHT 32
#define PUSH_BUTTON 15

#define LV_TICK_PERIOD_MS 1

#define MAX_FAILURES 		10

static const char wifi_tag[] = "[WIFI Connect]";

SemaphoreHandle_t clock_semaphore;
SemaphoreHandle_t tab_semaphore;

static EventGroupHandle_t wifi_event_group;
static int s_retry_num = 0;
lv_obj_t* sys_stat_tab;

static lv_obj_t *time_label = NULL;
static lv_obj_t *touch_status_label = NULL;
static int touch_count = 0;
static lv_obj_t *status_label = NULL;
static int64_t last_touch_us = 0;
static lv_obj_t *intro_screen = NULL;
static lv_obj_t *home_screen = NULL;
static lv_obj_t *detail_screen = NULL;
static lv_obj_t *detail_title_label = NULL;
static lv_obj_t *notification_panel = NULL;
static bool notification_open = false;
static lv_obj_t *wifi_button = NULL;

static void back_button_cb(lv_obj_t *obj, lv_event_t event);
static void show_notification_panel(bool show);

static void update_time_label(void)
{
    if (!time_label) return;

    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

    char buf[16];
    // HH:MM:SS (24-hour). If you want 12-hour, tell me.
    strftime(buf, sizeof(buf), "%I:%M %p", &timeinfo);
    if (buf[0] == '0') {
        memmove(buf, buf + 1, strlen(buf));
    }

    lv_label_set_text(time_label, buf);
}

static void touch_test_cb(lv_obj_t *obj, lv_event_t event)
{
    (void) obj;

    if (event != LV_EVENT_CLICKED) {
        return;
    }

    int64_t now_us = esp_timer_get_time();
    if ((now_us - last_touch_us) < 200000) {
        return;
    }
    last_touch_us = now_us;

    touch_count += 1;
    if (touch_status_label) {
        char buf[32];
        snprintf(buf, sizeof(buf), "Touch: %d", touch_count);
        lv_label_set_text(touch_status_label, buf);
    }
}

static void nav_button_cb(lv_obj_t *obj, lv_event_t event)
{
    if (event != LV_EVENT_CLICKED) {
        return;
    }

    int64_t now_us = esp_timer_get_time();
    if ((now_us - last_touch_us) < 200000) {
        return;
    }
    last_touch_us = now_us;

    if (!status_label) {
        return;
    }

    lv_obj_t *icon_label = lv_obj_get_child(obj, NULL);
    lv_obj_t *text_label = icon_label ? lv_obj_get_child(obj, icon_label) : NULL;
    const char *text = text_label ? lv_label_get_text(text_label) : "Tap";

    char buf[48];
    snprintf(buf, sizeof(buf), "Selected: %s", text);
    lv_label_set_text(status_label, buf);

    if (!detail_screen) {
        detail_screen = lv_obj_create(NULL, NULL);
        lv_obj_set_style_local_bg_color(detail_screen, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(0xF2F1F7));
        lv_obj_set_style_local_bg_opa(detail_screen, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_COVER);

        lv_obj_t *header = lv_cont_create(detail_screen, NULL);
        lv_obj_set_size(header, LV_HOR_RES_MAX, 28);
        lv_obj_align(header, NULL, LV_ALIGN_IN_TOP_MID, 0, 0);
        lv_obj_set_style_local_bg_color(header, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(0x3A3D5C));
        lv_obj_set_style_local_bg_opa(header, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_COVER);
        lv_obj_set_style_local_border_width(header, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 0);

        lv_obj_t *back_btn = lv_btn_create(header, NULL);
        lv_obj_set_size(back_btn, 28, 24);
        lv_obj_align(back_btn, header, LV_ALIGN_IN_LEFT_MID, 4, 0);
        lv_obj_set_event_cb(back_btn, back_button_cb);

        lv_obj_t *back_label = lv_label_create(back_btn, NULL);
        lv_label_set_text(back_label, LV_SYMBOL_LEFT);

        detail_title_label = lv_label_create(header, NULL);
        lv_label_set_text(detail_title_label, "Detail");
        lv_obj_set_style_local_text_color(detail_title_label, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE);
        lv_obj_align(detail_title_label, header, LV_ALIGN_CENTER, 0, 0);

        lv_obj_t *body = lv_label_create(detail_screen, NULL);
        lv_label_set_text(body, "Coming soon...");
        lv_obj_set_style_local_text_color(body, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(0x2D2F45));
        lv_obj_align(body, detail_screen, LV_ALIGN_CENTER, 0, 20);
    }

    if (detail_title_label) {
        lv_label_set_text(detail_title_label, text);
    }

    lv_scr_load_anim(detail_screen, LV_SCR_LOAD_ANIM_MOVE_LEFT, 250, 0, false);
}

static void back_button_cb(lv_obj_t *obj, lv_event_t event)
{
    (void) obj;

    if (event != LV_EVENT_CLICKED) {
        return;
    }

    int64_t now_us = esp_timer_get_time();
    if ((now_us - last_touch_us) < 200000) {
        return;
    }
    last_touch_us = now_us;

    if (home_screen) {
        lv_scr_load_anim(home_screen, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 250, 0, false);
        update_time_label();
    }
}

static lv_obj_t *create_nav_button(lv_obj_t *parent, const char *text, const char *icon, lv_coord_t x, lv_coord_t y)
{
    lv_obj_t *btn = lv_btn_create(parent, NULL);
    lv_obj_set_size(btn, 100, 80);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_event_cb(btn, nav_button_cb);
    lv_obj_set_style_local_bg_color(btn, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(0xFFFFFF));
    lv_obj_set_style_local_border_color(btn, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(0xD0D0D0));
    lv_obj_set_style_local_border_width(btn, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, 1);
    lv_obj_set_style_local_radius(btn, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, 8);

    lv_obj_t *icon_label = lv_label_create(btn, NULL);
    lv_label_set_text(icon_label, icon);
    lv_obj_set_style_local_text_color(icon_label, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(0x2D2F45));
    lv_obj_align(icon_label, btn, LV_ALIGN_IN_TOP_MID, 0, 6);

    lv_obj_t *label = lv_label_create(btn, NULL);
    lv_label_set_text(label, text);
    lv_obj_set_style_local_text_color(label, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(0x2D2F45));
    lv_obj_align(label, btn, LV_ALIGN_IN_BOTTOM_MID, 0, -6);

    return btn;
}


static lv_obj_t *create_status_card(lv_obj_t *parent, const char *title, const char *value)
{
    lv_obj_t *card = lv_cont_create(parent, NULL);
    lv_obj_set_size(card, 150, 70);
    lv_cont_set_layout(card, LV_LAYOUT_OFF);
    lv_obj_set_style_local_bg_color(card, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(0xFFFFFF));
    lv_obj_set_style_local_bg_opa(card, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_COVER);
    lv_obj_set_style_local_border_width(card, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 1);
    lv_obj_set_style_local_border_color(card, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(0xD0D0D0));
    lv_obj_set_style_local_radius(card, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 6);
    lv_obj_set_style_local_pad_left(card, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 6);
    lv_obj_set_style_local_pad_right(card, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 6);
    lv_obj_set_style_local_pad_top(card, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 6);
    lv_obj_set_style_local_pad_bottom(card, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 6);

    lv_obj_t *title_label = lv_label_create(card, NULL);
    lv_label_set_text(title_label, title);
    lv_obj_set_style_local_text_color(title_label, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(0xF699CD));
    lv_obj_align(title_label, card, LV_ALIGN_IN_TOP_LEFT, 0, 0);

    lv_obj_t *value_label = lv_label_create(card, NULL);
    lv_label_set_text(value_label, value);
    lv_obj_set_style_local_text_color(value_label, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(0x111111));
    lv_obj_align(value_label, card, LV_ALIGN_IN_BOTTOM_LEFT, 0, 0);

    return card;
}

static void anim_border_opa(void *obj, lv_anim_value_t value)
{
    lv_obj_set_style_local_border_opa(obj, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, value);
}

static void anim_obj_y(void *obj, lv_anim_value_t value)
{
    lv_obj_set_y(obj, value);
}

static void notification_panel_cb(lv_obj_t *obj, lv_event_t event)
{
    (void) obj;

    if (event == LV_EVENT_CLICKED) {
        show_notification_panel(false);
    }
}

static void home_gesture_cb(lv_obj_t *obj, lv_event_t event)
{
    (void) obj;

    if (event != LV_EVENT_GESTURE) {
        return;
    }

    lv_indev_t *indev = lv_indev_get_act();
    if (!indev) {
        return;
    }

    lv_gesture_dir_t dir = lv_indev_get_gesture_dir(indev);
    if (dir == LV_GESTURE_DIR_BOTTOM) {
        show_notification_panel(true);
    } else if (dir == LV_GESTURE_DIR_TOP) {
        show_notification_panel(false);
    }
}

static void wifi_button_cb(lv_obj_t *obj, lv_event_t event)
{
    (void) obj;

    if (event != LV_EVENT_CLICKED) {
        return;
    }

    show_notification_panel(!notification_open);
}

static void show_notification_panel(bool show)
{
    if (!notification_panel || notification_open == show) {
        return;
    }

    notification_open = show;
    if (show) {
        lv_obj_move_foreground(notification_panel);
    }
    lv_coord_t target_y = show ? 120 : -200;

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, notification_panel);
    lv_anim_set_exec_cb(&anim, anim_obj_y);
    lv_anim_set_values(&anim, lv_obj_get_y(notification_panel), target_y);
    lv_anim_set_time(&anim, 200);
    lv_anim_start(&anim);
}

static lv_obj_t *create_home_screen(void)
{
    lv_obj_t *scr = lv_obj_create(NULL, NULL);

    // Screen background: soft lavender
    lv_obj_set_style_local_bg_color(scr, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(0xF699CD));
    lv_obj_set_style_local_bg_opa(scr, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_COVER);

    // ---------- Header ----------
    lv_obj_t *header = lv_cont_create(scr, NULL);
    lv_obj_set_size(header, LV_HOR_RES_MAX, 28);
    lv_obj_align(header, NULL, LV_ALIGN_IN_TOP_MID, 0, 0);
    lv_obj_set_style_local_bg_color(header, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(0x3A3D5C));
    lv_obj_set_style_local_bg_opa(header, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_COVER);
    lv_obj_set_style_local_border_width(header, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 0);

    lv_obj_t *title = lv_label_create(header, NULL);
    lv_label_set_text(title, "Brain Block");
    lv_obj_set_style_local_text_color(title, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE);
    lv_obj_align(title, header, LV_ALIGN_IN_LEFT_MID, 8, 0);

    lv_obj_t *wifi_label = lv_label_create(header, NULL);
    lv_label_set_text(wifi_label, LV_SYMBOL_WIFI);
    lv_obj_set_style_local_text_color(wifi_label, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE);
    lv_obj_align(wifi_label, header, LV_ALIGN_IN_RIGHT_MID, -8, 0);

    wifi_button = lv_btn_create(header, NULL);
    lv_obj_set_size(wifi_button, 28, 24);
    lv_obj_align(wifi_button, header, LV_ALIGN_IN_RIGHT_MID, -4, 0);
    lv_obj_set_style_local_bg_opa(wifi_button, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_TRANSP);
    lv_obj_set_style_local_border_width(wifi_button, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, 0);
    lv_obj_set_event_cb(wifi_button, wifi_button_cb);
    lv_obj_move_foreground(wifi_label);

    time_label = lv_label_create(header, NULL);
    lv_label_set_text(time_label, "--:-- --");
    lv_obj_set_style_local_text_color(time_label, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE);
    lv_obj_align(time_label, header, LV_ALIGN_IN_RIGHT_MID, -48, 0);
    update_time_label();

    lv_obj_set_event_cb(scr, home_gesture_cb);

    // ---------- Notification panel ----------
    notification_panel = lv_cont_create(scr, NULL);
    lv_obj_set_size(notification_panel, LV_HOR_RES_MAX, 200);
    lv_obj_set_pos(notification_panel, 0, -200);
    lv_obj_set_style_local_bg_color(notification_panel, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(0xF2F1F7));
    lv_obj_set_style_local_bg_opa(notification_panel, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_COVER);
    lv_obj_set_style_local_border_width(notification_panel, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 1);
    lv_obj_set_style_local_border_color(notification_panel, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(0xD0D0D0));
    lv_obj_set_style_local_radius(notification_panel, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 0);
    lv_obj_set_event_cb(notification_panel, notification_panel_cb);

    lv_obj_t *notif_title = lv_label_create(notification_panel, NULL);
    lv_label_set_text(notif_title, "Notifications");
    lv_obj_set_style_local_text_color(notif_title, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(0x2D2F45));
    lv_obj_align(notif_title, notification_panel, LV_ALIGN_IN_TOP_LEFT, 10, 8);

    lv_obj_t *notif_body = lv_label_create(notification_panel, NULL);
    lv_label_set_text(notif_body, LV_SYMBOL_WARNING "  Battery low");
    lv_obj_set_style_local_text_color(notif_body, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(0xB53389));
    lv_obj_align(notif_body, notification_panel, LV_ALIGN_IN_TOP_LEFT, 10, 34);

    // ---------- Content ----------
    lv_obj_t *content = lv_cont_create(scr, NULL);
    lv_obj_set_pos(content, 0, 28);
    lv_obj_set_size(content, LV_HOR_RES_MAX, LV_VER_RES_MAX - 28);
    lv_cont_set_layout(content, LV_LAYOUT_OFF);
    lv_obj_set_style_local_bg_color(content, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(0xFCBACB));
    lv_obj_set_style_local_bg_opa(content, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_COVER);
    lv_obj_set_style_local_border_width(content, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 0);

    lv_obj_t *subtitle = lv_label_create(content, NULL);
    lv_label_set_text(subtitle, "Tap a block to begin");
    lv_obj_set_style_local_text_color(subtitle, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(0x6B6E8A));
    lv_obj_align(subtitle, content, LV_ALIGN_IN_TOP_MID, 0, 6);

    create_nav_button(content, "Start", LV_SYMBOL_PLAY, 10, 30);
    create_nav_button(content, "Blocks", LV_SYMBOL_LIST, 130, 30);
    create_nav_button(content, "Sound", LV_SYMBOL_VOLUME_MID, 10, 120);
    create_nav_button(content, "Help", LV_SYMBOL_SETTINGS, 130, 120);

    status_label = lv_label_create(content, NULL);
    lv_label_set_text(status_label, "Selected: None");
    lv_obj_set_style_local_text_color(status_label, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(0x3A3D5C));
    lv_obj_align(status_label, content, LV_ALIGN_IN_BOTTOM_LEFT, 10, -26);

    lv_obj_t *btn = lv_btn_create(content, NULL);
    lv_obj_set_size(btn, 140, 36);
    lv_obj_align(btn, content, LV_ALIGN_IN_BOTTOM_RIGHT, -10, -10);
    lv_obj_set_event_cb(btn, touch_test_cb);

    lv_obj_t *btn_label = lv_label_create(btn, NULL);
    lv_label_set_text(btn_label, "Touch test");

    touch_status_label = lv_label_create(content, NULL);
    lv_label_set_text(touch_status_label, "Touch: 0");
    lv_obj_set_style_local_text_color(touch_status_label, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(0x6B6E8A));
    lv_obj_align(touch_status_label, content, LV_ALIGN_IN_BOTTOM_LEFT, 10, -8);

    return scr;
}

static void begin_button_cb(lv_obj_t *obj, lv_event_t event)
{
    (void) obj;

    if (event != LV_EVENT_CLICKED) {
        return;
    }

    int64_t now_us = esp_timer_get_time();
    if ((now_us - last_touch_us) < 200000) {
        return;
    }
    last_touch_us = now_us;

    if (!home_screen) {
        home_screen = create_home_screen();
    }

    lv_scr_load_anim(home_screen, LV_SCR_LOAD_ANIM_MOVE_LEFT, 250, 0, false);
    update_time_label();
}

static lv_obj_t *create_intro_screen(void)
{
    lv_obj_t *scr = lv_obj_create(NULL, NULL);
    lv_obj_set_style_local_bg_color(scr, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(0xF699CD));
    lv_obj_set_style_local_bg_opa(scr, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_COVER);

    lv_obj_t *title = lv_label_create(scr, NULL);
    lv_label_set_text(title, "Welcome to\nBlocks of Code");
    lv_label_set_align(title, LV_LABEL_ALIGN_CENTER);
    lv_obj_set_style_local_text_font(title, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &lv_font_montserrat_24);
    lv_obj_set_style_local_bg_opa(title, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_TRANSP);
    lv_obj_set_style_local_text_opa(title, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_COVER);
    lv_obj_set_style_local_text_color(title, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(0x2D2F45));
    lv_obj_align(title, scr, LV_ALIGN_IN_TOP_MID, 0, 30);

    lv_obj_t *subtitle = lv_label_create(scr, NULL);
    lv_label_set_text(subtitle, "Tap Begin to start");
    lv_obj_set_style_local_text_color(subtitle, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(0x6B6E8A));
    lv_obj_align(subtitle, scr, LV_ALIGN_IN_TOP_MID, 0, 90);

    lv_coord_t block_size = 34;
    lv_coord_t y_pos = (LV_VER_RES_MAX / 2) - 8;
    lv_coord_t left_start = 20;
    lv_coord_t left_end = (LV_HOR_RES_MAX / 2) - block_size - 2;
    lv_coord_t right_start = LV_HOR_RES_MAX - block_size - 20;
    lv_coord_t right_end = (LV_HOR_RES_MAX / 2) + 2;

    lv_obj_t *frame = lv_cont_create(scr, NULL);
    lv_obj_set_size(frame, LV_HOR_RES_MAX - 8, LV_VER_RES_MAX - 8);
    lv_obj_align(frame, scr, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_local_bg_opa(frame, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_TRANSP);
    lv_obj_set_style_local_border_width(frame, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 2);
    lv_obj_set_style_local_border_color(frame, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(0xFF8FAB));
    lv_obj_set_style_local_border_opa(frame, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_0);
    lv_obj_set_style_local_radius(frame, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 10);

    lv_anim_t frame_anim;
    lv_anim_init(&frame_anim);
    lv_anim_set_var(&frame_anim, frame);
    lv_anim_set_exec_cb(&frame_anim, anim_border_opa);
    lv_anim_set_values(&frame_anim, LV_OPA_0, LV_OPA_80);
    lv_anim_set_time(&frame_anim, 900);
    lv_anim_set_playback_time(&frame_anim, 900);
    lv_anim_set_repeat_count(&frame_anim, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&frame_anim);

    lv_obj_t *block_left = lv_cont_create(scr, NULL);
    lv_obj_set_size(block_left, block_size, block_size);
    lv_obj_set_pos(block_left, left_start, y_pos);
    lv_obj_set_style_local_bg_color(block_left, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(0xFDE295));
    lv_obj_set_style_local_bg_opa(block_left, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_COVER);
    lv_obj_set_style_local_radius(block_left, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 6);
    lv_obj_set_style_local_border_width(block_left, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 2);
    lv_obj_set_style_local_border_color(block_left, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(0xFDE295));
    lv_obj_set_style_local_border_opa(block_left, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_0);

    lv_obj_t *block_right = lv_cont_create(scr, NULL);
    lv_obj_set_size(block_right, block_size, block_size);
    lv_obj_set_pos(block_right, right_start, y_pos);
    lv_obj_set_style_local_bg_color(block_right, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(0x6EC6FF));
    lv_obj_set_style_local_bg_opa(block_right, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_COVER);
    lv_obj_set_style_local_radius(block_right, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 6);
    lv_obj_set_style_local_border_width(block_right, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 2);
    lv_obj_set_style_local_border_color(block_right, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(0xFDE295));
    lv_obj_set_style_local_border_opa(block_right, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_0);

    lv_anim_t anim_left;
    lv_anim_init(&anim_left);
    lv_anim_set_var(&anim_left, block_left);
    lv_anim_set_exec_cb(&anim_left, (lv_anim_exec_xcb_t) lv_obj_set_x);
    lv_anim_set_values(&anim_left, left_start, left_end);
    lv_anim_set_time(&anim_left, 800);
    lv_anim_set_playback_delay(&anim_left, 250);
    lv_anim_set_playback_time(&anim_left, 600);
    lv_anim_set_repeat_delay(&anim_left, 250);
    lv_anim_set_repeat_count(&anim_left, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&anim_left);

    lv_anim_t anim_right;
    lv_anim_init(&anim_right);
    lv_anim_set_var(&anim_right, block_right);
    lv_anim_set_exec_cb(&anim_right, (lv_anim_exec_xcb_t) lv_obj_set_x);
    lv_anim_set_values(&anim_right, right_start, right_end);
    lv_anim_set_time(&anim_right, 800);
    lv_anim_set_playback_delay(&anim_right, 250);
    lv_anim_set_playback_time(&anim_right, 600);
    lv_anim_set_repeat_delay(&anim_right, 250);
    lv_anim_set_repeat_count(&anim_right, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&anim_right);

    lv_anim_t glow_left;
    lv_anim_init(&glow_left);
    lv_anim_set_var(&glow_left, block_left);
    lv_anim_set_exec_cb(&glow_left, anim_border_opa);
    lv_anim_set_values(&glow_left, LV_OPA_0, LV_OPA_80);
    lv_anim_set_delay(&glow_left, 800);
    lv_anim_set_time(&glow_left, 120);
    lv_anim_set_playback_time(&glow_left, 120);
    lv_anim_set_repeat_delay(&glow_left, 860);
    lv_anim_set_repeat_count(&glow_left, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&glow_left);

    lv_anim_t glow_right;
    lv_anim_init(&glow_right);
    lv_anim_set_var(&glow_right, block_right);
    lv_anim_set_exec_cb(&glow_right, anim_border_opa);
    lv_anim_set_values(&glow_right, LV_OPA_0, LV_OPA_80);
    lv_anim_set_delay(&glow_right, 800);
    lv_anim_set_time(&glow_right, 120);
    lv_anim_set_playback_time(&glow_right, 120);
    lv_anim_set_repeat_delay(&glow_right, 860);
    lv_anim_set_repeat_count(&glow_right, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&glow_right);

    lv_anim_t snap_left;
    lv_anim_init(&snap_left);
    lv_anim_set_var(&snap_left, block_left);
    lv_anim_set_exec_cb(&snap_left, (lv_anim_exec_xcb_t) lv_obj_set_y);
    lv_anim_set_values(&snap_left, y_pos, y_pos - 6);
    lv_anim_set_delay(&snap_left, 800);
    lv_anim_set_time(&snap_left, 120);
    lv_anim_set_playback_time(&snap_left, 120);
    lv_anim_set_repeat_delay(&snap_left, 860);
    lv_anim_set_repeat_count(&snap_left, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&snap_left);

    lv_anim_t snap_right;
    lv_anim_init(&snap_right);
    lv_anim_set_var(&snap_right, block_right);
    lv_anim_set_exec_cb(&snap_right, (lv_anim_exec_xcb_t) lv_obj_set_y);
    lv_anim_set_values(&snap_right, y_pos, y_pos - 6);
    lv_anim_set_delay(&snap_right, 800);
    lv_anim_set_time(&snap_right, 120);
    lv_anim_set_playback_time(&snap_right, 120);
    lv_anim_set_repeat_delay(&snap_right, 860);
    lv_anim_set_repeat_count(&snap_right, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&snap_right);

    lv_obj_t *begin_btn = lv_btn_create(scr, NULL);
    lv_obj_set_size(begin_btn, 140, 44);
    lv_obj_align(begin_btn, scr, LV_ALIGN_IN_BOTTOM_MID, 0, -24);
    lv_obj_set_event_cb(begin_btn, begin_button_cb);

    lv_obj_t *begin_label = lv_label_create(begin_btn, NULL);
    lv_label_set_text(begin_label, "Begin");

    return scr;
}

static void create_demo_application(void *pvParameters)
{
    (void) pvParameters;

    if (!intro_screen) {
        intro_screen = create_intro_screen();
    }
    lv_scr_load(intro_screen);

    // ---------- LVGL loop + time update ----------
    int ms = 0;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10));
        lv_task_handler();

        ms += 10;
        if (ms >= 1000) {
            ms = 0;
            update_time_label();
        }
    }
}



void time_sync_notification_cb(struct timeval *tv)
{
   	ESP_LOGI(wifi_tag, "Notification of a time synchronization event");
}

/*static void initialize_sntp(void)
{
	ESP_LOGI(wifi_tag, "Initializing SNTP");
	sntp_setoperatingmode(SNTP_OPMODE_POLL);
	sntp_setservername(0, "pool.ntp.org");
	sntp_set_time_sync_notification_cb(time_sync_notification_cb);
	sntp_init();

} */

 /*void button_push_handler(void* arg)
{
	while(1)
	{
		int value = gpio_get_level(PUSH_BUTTON);
		if (value == 1)
			gpio_set_level(LCD_BACKLIGHT,1);
		else if(value == 0)
	  		gpio_set_level(LCD_BACKLIGHT,0);

		vTaskDelay(100 / portTICK_PERIOD_MS);
	}
} */





static void lv_tick_task(void *arg) 
{
    (void) arg;
    lv_tick_inc(LV_TICK_PERIOD_MS);
}



/*static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
	if(event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
	{
		ESP_LOGI(wifi_tag,"Connecting to AP....");
		esp_wifi_connect();
	}
	else if(event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
	{
		if(s_retry_num < MAX_FAILURES)
		{
			ESP_LOGI(wifi_tag,"Reconnecting to AP....");
			esp_wifi_connect();
			s_retry_num++;

		}	
		else
		{
			ESP_LOGI(wifi_tag,"Could not connect to WIFI!!");
			xEventGroupSetBits(wifi_event_group,WIFI_FAILURE);
		}
	}
} */

/* static void ip_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
	if(event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
	{
		ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;
		ESP_LOGI(wifi_tag,"STA IP: " IPSTR,IP2STR(&event->ip_info.ip));
		s_retry_num=0;
		xEventGroupSetBits(wifi_event_group,WIFI_SUCCESS);
		
	}
} */


 /* static int initialise_wifi(void)
{

	int status = WIFI_FAILURE;
	ESP_ERROR_CHECK(esp_netif_init());
	
	ESP_ERROR_CHECK(esp_event_loop_create_default());

	esp_netif_create_default_wifi_sta();

	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_wifi_init(&cfg));
	
	// create event group
	wifi_event_group = xEventGroupCreate();
	
	// set connect to wifi event handler	
	esp_event_handler_instance_t wifi_handler_event_instance;
	ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,ESP_EVENT_ANY_ID,&wifi_event_handler,NULL,&wifi_handler_event_instance));

	// set obtained IP event handler
	esp_event_handler_instance_t got_ip_event_instance;
	ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,IP_EVENT_STA_GOT_IP,&ip_event_handler,NULL,&got_ip_event_instance));

	
	wifi_config_t wifi_config = {
		.sta = {
			.ssid = "Destiny",
			.password="Poetry1129!",
			//.threshold.authmode = WIFI_AUTH_WPA2_PSK,
			.pmf_cfg = {
				.capable=true,
				.required=false
			},
			},
		};

	// set wifi mode to wifi station	
	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
	// set the configuration
	ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA,&wifi_config));
	// start the wifi
	ESP_ERROR_CHECK(esp_wifi_start());
	ESP_LOGI(wifi_tag,"STA initialization complete");
	
	// wait until either WIFI_SUCCESS or WIFI_FAILURE bits are set in the wifi_event_group
	EventBits_t bits = xEventGroupWaitBits(wifi_event_group,WIFI_SUCCESS|WIFI_FAILURE,pdFALSE,pdFALSE,portMAX_DELAY);
	
	// check the set value inside bits
	if (bits & WIFI_SUCCESS)
		status = WIFI_SUCCESS;
	else if(bits & WIFI_FAILURE)
		status = WIFI_FAILURE;	
	else
	{
		ESP_LOGE(TAG,"Unexpected event");
		status = WIFI_FAILURE;
	}

	// deregister handlers and delete event group
	ESP_ERROR_CHECK(esp_event_handler_instance_unregister(IP_EVENT,IP_EVENT_STA_GOT_IP,got_ip_event_instance));
	ESP_ERROR_CHECK(esp_event_handler_instance_unregister(WIFI_EVENT,ESP_EVENT_ANY_ID,wifi_handler_event_instance));
	vEventGroupDelete(wifi_event_group);

	return status;
} */


/*static int obtain_time(void)
{	
	if (initialise_wifi() == WIFI_SUCCESS)
		initialize_sntp();
	
	time_t now;
    	struct tm timeinfo;
    	time(&now);
    	localtime_r(&now, &timeinfo);

	int retry = 0;
    	const int retry_count = 20;

	while(timeinfo.tm_year < (2022 - 1900) && ++retry < retry_count) {
		vTaskDelay(500 / portTICK_PERIOD_MS);
	        time(&now);
	    	localtime_r(&now, &timeinfo);
	}

    	if (timeinfo.tm_year < (2022 - 1900)) {
    		ESP_LOGI(wifi_tag, "System time NOT set.");
		wifi_connect_status = WIFI_FAILURE;
    	}
    	else 
	{
    		ESP_LOGI(wifi_tag, "System time is set.");

		setenv("TZ", "EST5EDT,M3.2.0/2,M11.1.0/2", 1);
tzset();

    		localtime_r(&now, &timeinfo);
		wifi_connect_status = WIFI_SUCCESS;
    	}
	return 1;
} */


void prvStatTask(void* para)
{
	xSemaphoreTake( tab_semaphore, portMAX_DELAY );
	display_system_runtime_stat(sys_stat_tab);

}


void tft_ui_start(void)
{
	clock_semaphore = xSemaphoreCreateMutex();
	tab_semaphore = xSemaphoreCreateBinary();


	lv_init();
	setenv("TZ", "EST5EDT,M3.2.0/2,M11.1.0/2", 1);
	tzset();

    	lvgl_driver_init();

    	lv_color_t* buf1 = heap_caps_malloc(DISP_BUF_SIZE * sizeof(lv_color_t), MALLOC_CAP_DMA);
    	assert(buf1 != NULL);

    	lv_color_t* buf2 = heap_caps_malloc(DISP_BUF_SIZE * sizeof(lv_color_t), MALLOC_CAP_DMA);
    	assert(buf2 != NULL);

    	static lv_disp_buf_t disp_buf;

    	uint32_t size_in_px = DISP_BUF_SIZE;


    	/* Initialize the working buffer depending on the selected display.
     	* NOTE: buf2 == NULL when using monochrome displays. */
    	lv_disp_buf_init(&disp_buf, buf1, buf2, size_in_px);

    	lv_disp_drv_t disp_drv;
    	lv_disp_drv_init(&disp_drv);
    	disp_drv.flush_cb = disp_driver_flush;

    	disp_drv.buffer = &disp_buf;
    	lv_disp_drv_register(&disp_drv);

	lv_indev_drv_t indev_drv;
	lv_indev_drv_init(&indev_drv);
	indev_drv.read_cb = touch_driver_read;
	indev_drv.type = LV_INDEV_TYPE_POINTER;
	lv_indev_drv_register(&indev_drv);
	
    	/* Create and start a periodic timer interrupt to call lv_tick_inc */
    	const esp_timer_create_args_t periodic_timer_args = {
	        .callback = &lv_tick_task,
	        .name = "periodic_gui"
    	};

	esp_timer_handle_t periodic_timer;
	ESP_ERROR_CHECK(esp_timer_create(&periodic_timer_args, &periodic_timer));
	ESP_ERROR_CHECK(esp_timer_start_periodic(periodic_timer, LV_TICK_PERIOD_MS * 1000));


	gpio_reset_pin(LCD_BACKLIGHT);
	gpio_set_direction(LCD_BACKLIGHT,GPIO_MODE_OUTPUT);	

	//BaseType_t push_button_check_handle = xTaskCreatePinnedToCore(button_push_handler, "CheckButtonStatus", 1024,  xTaskGetCurrentTaskHandle(), 10,NULL,0);
	//if(push_button_check_handle == pdPASS)
		//ESP_LOGI(TAG,"Push button check task created");

	//BaseType_t display_stat_handle = xTaskCreatePinnedToCore(prvStatTask, "DisplayRunTimeStat", 1024*2,  xTaskGetCurrentTaskHandle(), 10,NULL,0);
	//if(display_stat_handle == pdPASS)
		//ESP_LOGI(TAG,"Display Runtime Stat task created");

	// run LVGL GUI on core 1 of ESP32	
	BaseType_t gui_task_handle = xTaskCreatePinnedToCore(create_demo_application,"GUITask", 1024*4, NULL, 10, NULL, 1);
	if(gui_task_handle == pdPASS)
		ESP_LOGI(TAG,"GUI task created");

	//ESP_ERROR_CHECK( nvs_flash_init() );

	/*if (pdTRUE == xSemaphoreTake(clock_semaphore, portMAX_DELAY)) 
	{
		obtain_time();
		xSemaphoreGive(clock_semaphore);
	} */



    	//* A task should NEVER return */
    	//free(buf1);
    	//free(buf2);
    	//vTaskDelete(NULL);

}


