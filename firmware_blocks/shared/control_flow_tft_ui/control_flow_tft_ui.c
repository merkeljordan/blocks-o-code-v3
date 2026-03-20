#include "control_flow_tft_ui.h"

#include <stdio.h>
#include <string.h>

#define CONTROL_FLOW_ANIM_PERIOD_MS 95U
#define CONTROL_FLOW_ANIM_TICKS     16U
#define CONTROL_FLOW_CONFETTI_COUNT 8U

#if LV_FONT_MONTSERRAT_28
#define CONTROL_FLOW_TITLE_FONT (&lv_font_montserrat_28)
#elif LV_FONT_MONTSERRAT_24
#define CONTROL_FLOW_TITLE_FONT (&lv_font_montserrat_24)
#elif LV_FONT_MONTSERRAT_16
#define CONTROL_FLOW_TITLE_FONT (&lv_font_montserrat_16)
#else
#define CONTROL_FLOW_TITLE_FONT LV_FONT_DEFAULT
#endif

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} rgb8_t;

static lv_obj_t *s_screen;
static lv_obj_t *s_card;
static lv_obj_t *s_status_label;
static lv_obj_t *s_title_label;
static lv_obj_t *s_value_label;
static lv_obj_t *s_preview_button;
static lv_obj_t *s_submit_button;
static lv_obj_t *s_minus_button;
static lv_obj_t *s_plus_button;
static lv_obj_t *s_confetti[CONTROL_FLOW_CONFETTI_COUNT];
static lv_timer_t *s_anim_timer;
static lv_timer_t *s_state_timer;
static control_flow_ui_config_t s_cfg;
static uint32_t s_current_value;
static uint32_t s_anim_tick;
static bool s_running;
static volatile bool s_pending_execute;
static volatile bool s_pending_idle;
static volatile bool s_pending_value_refresh;

static const uint32_t k_party_palette[] = {
    0xFF5D8Fu,
    0xF97316u,
    0xFDE047u,
    0x2DD4BFu,
    0x60A5FAu,
    0xA78BFAu,
};

static void set_button_palette(lv_obj_t *button, uint32_t color);
static void set_preview_button_visible(bool visible);
static void start_running_state(void);

static uint32_t clamp_value(uint32_t value)
{
    if (!s_cfg.supports_value) {
        return value;
    }

    if (value < s_cfg.min_value) {
        value = s_cfg.min_value;
    }
    if (value > s_cfg.max_value) {
        value = s_cfg.max_value;
    }
    return value;
}

static rgb8_t rgb_from_hex(uint32_t hex)
{
    rgb8_t rgb = {
        .r = (uint8_t)((hex >> 16) & 0xFFu),
        .g = (uint8_t)((hex >> 8) & 0xFFu),
        .b = (uint8_t)(hex & 0xFFu),
    };
    return rgb;
}

static uint32_t blend_hex(uint32_t a, uint32_t b, uint8_t amount)
{
    rgb8_t rgb_a = rgb_from_hex(a);
    rgb8_t rgb_b = rgb_from_hex(b);
    uint32_t inv = 255u - amount;
    uint32_t r = ((uint32_t)rgb_a.r * inv + (uint32_t)rgb_b.r * amount) / 255u;
    uint32_t g = ((uint32_t)rgb_a.g * inv + (uint32_t)rgb_b.g * amount) / 255u;
    uint32_t b_out = ((uint32_t)rgb_a.b * inv + (uint32_t)rgb_b.b * amount) / 255u;
    return (r << 16) | (g << 8) | b_out;
}

static uint32_t party_color(uint32_t phase, uint32_t offset, uint8_t accent_mix)
{
    uint32_t count = sizeof(k_party_palette) / sizeof(k_party_palette[0]);
    uint32_t base = k_party_palette[(phase + offset) % count];
    return blend_hex(base, s_cfg.accent_color, accent_mix);
}

static void set_label_text(lv_obj_t *label, const char *text)
{
    if (label == NULL) {
        return;
    }
    lv_label_set_text(label, (text != NULL) ? text : "");
}

static void update_value_label(void)
{
    if (s_value_label == NULL) {
        return;
    }

    char buffer[32];
    const char *suffix = (s_cfg.value_suffix != NULL) ? s_cfg.value_suffix : "";
    snprintf(buffer, sizeof(buffer), "%lu%s", (unsigned long)s_current_value, suffix);
    lv_label_set_text(s_value_label, buffer);
}

static void set_status_text(const char *text)
{
    set_label_text(s_status_label, text);
}

static void apply_idle_palette(void)
{
    uint32_t accent = s_cfg.accent_color;
    uint32_t bg_start = blend_hex(0x0C1220u, accent, 20u);
    uint32_t bg_end = blend_hex(0x090D17u, accent, 8u);
    uint32_t card_bg = blend_hex(0x151B2Du, accent, 32u);
    uint32_t card_grad = blend_hex(card_bg, accent, 72u);

    lv_obj_set_style_bg_color(s_screen, lv_color_hex(bg_start), 0);
    lv_obj_set_style_bg_grad_color(s_screen, lv_color_hex(bg_end), 0);
    lv_obj_set_style_bg_grad_dir(s_screen, LV_GRAD_DIR_VER, 0);

    lv_obj_set_style_bg_color(s_card, lv_color_hex(card_bg), 0);
    lv_obj_set_style_bg_grad_color(s_card, lv_color_hex(card_grad), 0);
    lv_obj_set_style_bg_grad_dir(s_card, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_shadow_color(s_card, lv_color_hex(accent), 0);
    lv_obj_set_style_shadow_width(s_card, 24, 0);
    lv_obj_set_style_border_color(s_card, lv_color_hex(blend_hex(0xFFFFFFu, accent, 128u)), 0);
    lv_obj_set_style_border_width(s_card, 2, 0);
    lv_obj_set_style_translate_x(s_card, 0, 0);
    lv_obj_set_style_translate_y(s_card, 0, 0);

    lv_obj_set_style_text_color(s_title_label, lv_color_hex(0xF8FAFCu), 0);
    lv_obj_set_style_text_color(s_status_label, lv_color_hex(0xD4DCE8u), 0);
    if (s_value_label != NULL) {
        lv_obj_set_style_text_color(s_value_label, lv_color_hex(0xF8FAFCu), 0);
    }

    for (uint32_t i = 0; i < CONTROL_FLOW_CONFETTI_COUNT; i++) {
        if (s_confetti[i] != NULL) {
            lv_obj_add_flag(s_confetti[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_size(s_confetti[i], 10, 10);
            lv_obj_set_style_radius(s_confetti[i], LV_RADIUS_CIRCLE, 0);
        }
    }

    if (s_preview_button != NULL) {
        set_button_palette(s_preview_button,
                           s_cfg.supports_value ? blend_hex(s_cfg.accent_color, 0xFFFFFFu, 70u)
                                                : s_cfg.accent_color);
        lv_obj_set_style_shadow_width(s_preview_button, 14, 0);
    }
    if (s_submit_button != NULL) {
        set_button_palette(s_submit_button, s_cfg.accent_color);
        lv_obj_set_style_shadow_width(s_submit_button, 14, 0);
    }
    if (s_minus_button != NULL) {
        set_button_palette(s_minus_button, blend_hex(s_cfg.accent_color, 0xFFFFFFu, 96u));
    }
    if (s_plus_button != NULL) {
        set_button_palette(s_plus_button, blend_hex(s_cfg.accent_color, 0xFFFFFFu, 96u));
    }
}

static void set_button_palette(lv_obj_t *button, uint32_t color)
{
    if (button == NULL) {
        return;
    }

    uint32_t pressed = blend_hex(color, 0x000000u, 50u);
    lv_obj_set_style_bg_color(button, lv_color_hex(color), 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(pressed), LV_STATE_PRESSED);
    lv_obj_set_style_text_color(button, lv_color_hex(0x081018u), 0);
}

static void set_button_label(lv_obj_t *button, const char *text)
{
    lv_obj_t *label = lv_obj_get_child(button, 0);
    if (label != NULL) {
        lv_label_set_text(label, text);
    }
}

static void set_preview_button_visible(bool visible)
{
    if (s_preview_button == NULL) {
        return;
    }

    if (visible) {
        lv_obj_clear_flag(s_preview_button, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_state(s_preview_button, LV_STATE_DISABLED);
    } else {
        lv_obj_add_flag(s_preview_button, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_state(s_preview_button, LV_STATE_DISABLED);
    }
}

static void update_idle_status(void)
{
    if (s_cfg.supports_value) {
        set_status_text("Adjust, submit, or preview the run.");
    } else {
        set_status_text("Preview the run-state animation.");
    }
}

static void animate_running_frame(void)
{
    static const int16_t k_confetti_x[CONTROL_FLOW_CONFETTI_COUNT] = {18, 170, 36, 148, 22, 176, 58, 128};
    static const int16_t k_confetti_y[CONTROL_FLOW_CONFETTI_COUNT] = {18, 24, 48, 54, 114, 106, 132, 120};
    static const uint8_t k_confetti_size[CONTROL_FLOW_CONFETTI_COUNT] = {10, 8, 12, 9, 11, 8, 10, 9};
    uint32_t phase = s_anim_tick % 6U;
    uint32_t bg_a = party_color(phase, 0U, 20u);
    uint32_t bg_b = party_color(phase, 2U, 16u);
    uint32_t card_a = party_color(phase, 1U, 72u);
    uint32_t card_b = party_color(phase, 3U, 84u);
    uint32_t pulse = party_color(phase, 4U, 48u);
    uint32_t text_flash = party_color(phase, 5U, 34u);
    lv_opa_t accent_opa = (phase & 1U) ? LV_OPA_90 : LV_OPA_50;

    lv_obj_set_style_bg_color(s_screen, lv_color_hex(blend_hex(0x06070Eu, bg_a, 96u)), 0);
    lv_obj_set_style_bg_grad_color(s_screen, lv_color_hex(blend_hex(0x131A2Bu, bg_b, 132u)), 0);
    lv_obj_set_style_bg_grad_dir(s_screen, LV_GRAD_DIR_VER, 0);

    lv_obj_set_style_bg_color(s_card, lv_color_hex(blend_hex(0x151B2Du, card_a, 108u)), 0);
    lv_obj_set_style_bg_grad_color(s_card, lv_color_hex(blend_hex(card_b, 0xFFFFFFu, 24u)), 0);
    lv_obj_set_style_bg_grad_dir(s_card, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_shadow_color(s_card, lv_color_hex(pulse), 0);
    lv_obj_set_style_shadow_width(s_card, 26 + (int32_t)((phase % 3U) * 6U), 0);
    lv_obj_set_style_border_color(s_card, lv_color_hex(text_flash), 0);
    lv_obj_set_style_border_width(s_card, 2 + (int32_t)(phase % 2U), 0);
    lv_obj_set_style_translate_x(s_card, 0, 0);
    lv_obj_set_style_translate_y(s_card, 0, 0);
    lv_obj_set_style_translate_y(s_title_label, 0, 0);
    lv_obj_set_style_text_color(s_title_label, lv_color_hex(blend_hex(0xFFFFFFu, text_flash, 96u)), 0);
    lv_obj_set_style_text_color(s_status_label, lv_color_hex(blend_hex(0xF8FAFCu, pulse, 72u)), 0);
    if (s_value_label != NULL) {
        lv_obj_set_style_text_color(s_value_label, lv_color_hex(blend_hex(0xFFFFFFu, card_b, 90u)), 0);
    }

    if (s_submit_button != NULL) {
        set_button_palette(s_submit_button, party_color(phase, 3U, 48u));
        lv_obj_set_style_shadow_color(s_submit_button, lv_color_hex(party_color(phase, 5U, 30u)), 0);
        lv_obj_set_style_shadow_width(s_submit_button, 16 + (int32_t)(((phase + 2U) % 6U) * 2U), 0);
    }

    for (uint32_t i = 0; i < CONTROL_FLOW_CONFETTI_COUNT; i++) {
        lv_obj_t *dot = s_confetti[i];
        if (dot == NULL) {
            continue;
        }

        uint32_t color = party_color(phase, i, (uint8_t)(20U + i * 12U));
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(dot, k_confetti_x[i], k_confetti_y[i]);
        lv_obj_set_size(dot, k_confetti_size[i], k_confetti_size[i]);
        lv_obj_set_style_bg_color(dot, lv_color_hex(color), 0);
        lv_obj_set_style_bg_opa(dot, accent_opa, 0);
        lv_obj_set_style_shadow_color(dot, lv_color_hex(color), 0);
        lv_obj_set_style_shadow_width(dot, 8 + (int32_t)(i % 2U) * 4, 0);
        lv_obj_set_style_radius(dot, ((i & 1U) == 0U) ? LV_RADIUS_CIRCLE : 4, 0);
    }
}

static void running_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    if (!s_running) {
        return;
    }

    animate_running_frame();
    s_anim_tick++;
    if (s_anim_tick >= CONTROL_FLOW_ANIM_TICKS) {
        control_flow_tft_ui_set_idle();
    }
}

static void apply_idle_state(void)
{
    s_running = false;
    s_anim_tick = 0;

    if (s_anim_timer != NULL) {
        lv_timer_pause(s_anim_timer);
    }

    apply_idle_palette();
    lv_obj_set_style_translate_y(s_title_label, 0, 0);
    update_value_label();
    update_idle_status();
    set_preview_button_visible(true);

    if (s_preview_button != NULL) {
        set_button_label(s_preview_button, s_cfg.supports_value ? "Preview" : "Preview Run");
    }
    if (s_submit_button != NULL) {
        set_button_label(s_submit_button, "Submit");
    }
}

static void state_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    if (s_screen == NULL) {
        return;
    }

    if (s_pending_idle) {
        s_pending_idle = false;
        s_pending_execute = false;
        apply_idle_state();
    }

    if (s_pending_value_refresh) {
        s_pending_value_refresh = false;
        update_value_label();
    }

    if (s_pending_execute) {
        s_pending_execute = false;
        if (s_running) {
            s_anim_tick = 0;
        } else {
            start_running_state();
        }
    }
}

static void start_running_state(void)
{
    s_running = true;
    s_anim_tick = 0;
    set_status_text("Disco time! Block is running!");
    set_preview_button_visible(false);

    if (s_anim_timer == NULL) {
        s_anim_timer = lv_timer_create(running_timer_cb, CONTROL_FLOW_ANIM_PERIOD_MS, NULL);
    } else {
        lv_timer_set_period(s_anim_timer, CONTROL_FLOW_ANIM_PERIOD_MS);
        lv_timer_resume(s_anim_timer);
    }

    animate_running_frame();
}

static void preview_button_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }
    control_flow_tft_ui_trigger_execute();
}

static void submit_button_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    if (s_cfg.submit_cb == NULL) {
        set_status_text("No submit callback connected.");
        return;
    }

    bool ok = s_cfg.submit_cb(s_current_value);
    if (ok) {
        char buffer[48];
        const char *suffix = (s_cfg.value_suffix != NULL) ? s_cfg.value_suffix : "";
        snprintf(buffer, sizeof(buffer), "Submitted %lu%s", (unsigned long)s_current_value, suffix);
        set_status_text(buffer);
    } else {
        set_status_text("Submit failed.");
    }
}

static void bump_value(int32_t delta_steps)
{
    if (!s_cfg.supports_value || s_cfg.step == 0U) {
        return;
    }

    int64_t candidate = (int64_t)s_current_value + (int64_t)delta_steps * (int64_t)s_cfg.step;
    if (candidate < 0) {
        candidate = 0;
    }

    control_flow_tft_ui_set_value((uint32_t)candidate);
}

static void minus_button_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }
    bump_value(-1);
}

static void plus_button_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }
    bump_value(1);
}

static lv_obj_t *create_button(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h, const char *text)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_size(button, w, h);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_style_radius(button, 18, 0);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_set_style_shadow_width(button, 14, 0);
    lv_obj_set_style_shadow_opa(button, LV_OPA_30, 0);
    lv_obj_set_style_text_font(button, LV_FONT_DEFAULT, 0);

    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    return button;
}

void control_flow_tft_ui_start(const control_flow_ui_config_t *cfg)
{
    if (cfg == NULL) {
        return;
    }

    memset(&s_cfg, 0, sizeof(s_cfg));
    s_cfg = *cfg;
    s_current_value = s_cfg.default_value;
    s_running = false;
    s_anim_tick = 0;
    s_pending_execute = false;
    s_pending_idle = false;
    s_pending_value_refresh = false;
    s_screen = NULL;
    s_card = NULL;
    s_status_label = NULL;
    s_title_label = NULL;
    s_value_label = NULL;
    s_preview_button = NULL;
    s_submit_button = NULL;
    s_minus_button = NULL;
    s_plus_button = NULL;
    for (uint32_t i = 0; i < CONTROL_FLOW_CONFETTI_COUNT; i++) {
        s_confetti[i] = NULL;
    }

    if (s_cfg.supports_value) {
        s_current_value = clamp_value(s_current_value);
    }

    if (s_anim_timer != NULL) {
        lv_timer_del(s_anim_timer);
        s_anim_timer = NULL;
    }
    if (s_state_timer != NULL) {
        lv_timer_del(s_state_timer);
        s_state_timer = NULL;
    }

    s_screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_screen);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);

    s_card = lv_obj_create(s_screen);
    lv_obj_set_size(s_card, 212, 160);
    lv_obj_set_pos(s_card, 14, 14);
    lv_obj_set_style_radius(s_card, 28, 0);
    lv_obj_set_style_border_width(s_card, 2, 0);
    lv_obj_set_style_pad_all(s_card, 0, 0);
    lv_obj_set_style_shadow_width(s_card, 24, 0);
    lv_obj_set_style_shadow_opa(s_card, LV_OPA_40, 0);
    s_title_label = lv_label_create(s_card);
    lv_label_set_text(s_title_label, (s_cfg.title != NULL) ? s_cfg.title : "BLOCK");
    lv_obj_set_width(s_title_label, 180);
    lv_obj_set_pos(s_title_label, 16, 24);
    lv_obj_set_style_text_align(s_title_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(s_title_label, CONTROL_FLOW_TITLE_FONT, 0);

    s_status_label = lv_label_create(s_card);
    lv_obj_set_width(s_status_label, 178);
    lv_obj_set_pos(s_status_label, 17, 98);
    lv_obj_set_style_text_align(s_status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(s_status_label, LV_FONT_DEFAULT, 0);
    lv_obj_set_style_text_color(s_status_label, lv_color_hex(0xD4DCE8u), 0);
    lv_label_set_long_mode(s_status_label, LV_LABEL_LONG_WRAP);

    for (uint32_t i = 0; i < CONTROL_FLOW_CONFETTI_COUNT; i++) {
        s_confetti[i] = lv_obj_create(s_card);
        lv_obj_set_size(s_confetti[i], 10, 10);
        lv_obj_set_style_radius(s_confetti[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(s_confetti[i], 0, 0);
        lv_obj_set_style_shadow_width(s_confetti[i], 12, 0);
        lv_obj_add_flag(s_confetti[i], LV_OBJ_FLAG_HIDDEN);
    }

    if (s_cfg.supports_value) {
        lv_obj_t *control_card = lv_obj_create(s_screen);
        lv_obj_set_size(control_card, 212, 118);
        lv_obj_set_pos(control_card, 14, 188);
        lv_obj_set_style_radius(control_card, 24, 0);
        lv_obj_set_style_bg_color(control_card, lv_color_hex(0x101728u), 0);
        lv_obj_set_style_bg_grad_color(control_card, lv_color_hex(0x17213Au), 0);
        lv_obj_set_style_bg_grad_dir(control_card, LV_GRAD_DIR_VER, 0);
        lv_obj_set_style_border_width(control_card, 1, 0);
        lv_obj_set_style_border_color(control_card, lv_color_hex(0x24324Du), 0);

        s_minus_button = create_button(control_card, 18, 16, 42, 38, "-");
        s_plus_button = create_button(control_card, 152, 16, 42, 38, "+");
        set_button_palette(s_minus_button, blend_hex(s_cfg.accent_color, 0xFFFFFFu, 96u));
        set_button_palette(s_plus_button, blend_hex(s_cfg.accent_color, 0xFFFFFFu, 96u));
        lv_obj_add_event_cb(s_minus_button, minus_button_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_add_event_cb(s_plus_button, plus_button_cb, LV_EVENT_CLICKED, NULL);

        s_value_label = lv_label_create(control_card);
        lv_obj_set_width(s_value_label, 88);
        lv_obj_set_pos(s_value_label, 62, 20);
        lv_obj_set_style_text_align(s_value_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(s_value_label, LV_FONT_DEFAULT, 0);
        lv_obj_set_style_text_color(s_value_label, lv_color_hex(0xF8FAFCu), 0);

        s_preview_button = create_button(control_card, 18, 68, 84, 34, "Preview");
        s_submit_button = create_button(control_card, 110, 68, 84, 34, "Submit");
        set_button_palette(s_preview_button, blend_hex(s_cfg.accent_color, 0xFFFFFFu, 70u));
        set_button_palette(s_submit_button, s_cfg.accent_color);
        lv_obj_add_event_cb(s_preview_button, preview_button_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_add_event_cb(s_submit_button, submit_button_cb, LV_EVENT_CLICKED, NULL);
    } else {
        s_value_label = NULL;
        s_minus_button = NULL;
        s_plus_button = NULL;
        s_submit_button = NULL;
        s_preview_button = create_button(s_screen, 26, 228, 188, 56, "Preview Run");
        set_button_palette(s_preview_button, s_cfg.accent_color);
        lv_obj_add_event_cb(s_preview_button, preview_button_cb, LV_EVENT_CLICKED, NULL);
    }

    s_state_timer = lv_timer_create(state_timer_cb, 16, NULL);
    apply_idle_state();
    lv_screen_load(s_screen);
}

void control_flow_tft_ui_trigger_execute(void)
{
    s_pending_idle = false;
    s_pending_execute = true;
}

void control_flow_tft_ui_set_idle(void)
{
    s_pending_execute = false;
    s_pending_idle = true;
}

void control_flow_tft_ui_set_value(uint32_t value)
{
    if (!s_cfg.supports_value) {
        return;
    }

    s_current_value = clamp_value(value);
    s_pending_value_refresh = true;
}
