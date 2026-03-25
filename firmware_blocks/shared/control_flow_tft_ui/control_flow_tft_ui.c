#include "control_flow_tft_ui.h"

#include <stdio.h>
#include <string.h>

LV_FONT_DECLARE(mochi_boom_28);
LV_FONT_DECLARE(mochi_boom_34);

#define CONTROL_FLOW_ANIM_PERIOD_MS 70U
#define CONTROL_FLOW_ANIM_TICKS     28U
#define CONTROL_FLOW_SCREEN_W       240
#define CONTROL_FLOW_SCREEN_H       320
#define CONTROL_FLOW_CARD_IDLE_X    14
#define CONTROL_FLOW_CARD_IDLE_Y    14
#define CONTROL_FLOW_CARD_IDLE_W    212
#define CONTROL_FLOW_CARD_IDLE_H    160
#define CONTROL_FLOW_CARD_CENTERED_Y ((CONTROL_FLOW_SCREEN_H - CONTROL_FLOW_CARD_IDLE_H) / 2)

#define DISCO_TILE_COLS 6U
#define DISCO_TILE_ROWS 4U
#define DISCO_TILE_COUNT ((DISCO_TILE_COLS) * (DISCO_TILE_ROWS))
#define DISCO_SPOT_COUNT 3U
#define DISCO_FLOOR_H 176
#define DISCO_TILE_W ((CONTROL_FLOW_SCREEN_W) / (DISCO_TILE_COLS))
#define DISCO_TILE_H ((DISCO_FLOOR_H) / (DISCO_TILE_ROWS))
#define DISCO_SPOT_SIZE 56
#define DISCO_SPOT_R   ((DISCO_SPOT_SIZE) / 2)

#define CONTROL_FLOW_TITLE_FONT (&mochi_boom_34)

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} rgb8_t;

static lv_obj_t *s_screen;
static lv_obj_t *s_disco_layer;
static lv_obj_t *s_disco_tiles[DISCO_TILE_COUNT];
static lv_obj_t *s_disco_spots[DISCO_SPOT_COUNT];
static lv_obj_t *s_card;
static lv_obj_t *s_control_card;
static lv_obj_t *s_status_label;
static lv_obj_t *s_running_status_label;
static lv_obj_t *s_title_label;
static lv_obj_t *s_center_icon_label;
static lv_obj_t *s_value_label;
static lv_obj_t *s_submit_button;
static lv_obj_t *s_secondary_button;
static lv_obj_t *s_minus_button;
static lv_obj_t *s_plus_button;
static lv_timer_t *s_anim_timer;
static lv_timer_t *s_state_timer;
static control_flow_ui_config_t s_cfg;
static uint32_t s_current_value;
static uint32_t s_anim_tick;
static bool s_running;
static volatile bool s_pending_execute;
static volatile bool s_pending_idle;
static volatile bool s_pending_value_refresh;

#define CFUI_LOG(fmt, ...) printf("[control_flow_tft_ui] " fmt "\n", ##__VA_ARGS__)

static void set_button_palette(lv_obj_t *button, uint32_t color);
static void start_running_state(void);
static void disco_layer_create(lv_obj_t *parent);
static void disco_anim_apply(uint32_t tick);

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

static uint32_t disco_neon(uint32_t index)
{
    static const uint32_t k_neon[] = {
        0xFF00EEu, 0x00F5FFu, 0xFFF000u, 0xFF3377u, 0x33FFAAu, 0x8899FFu,
        0xFFAA33u, 0xCC66FFu,
    };
    return k_neon[index % (sizeof(k_neon) / sizeof(k_neon[0]))];
}

/* Integer triangle wave in [0, span] for smooth bouncing without libm. */
static uint32_t tri_wave(uint32_t tick, uint32_t span, uint32_t period)
{
    if (period < 2U) {
        return 0U;
    }
    uint32_t p = tick % period;
    uint32_t half = period / 2U;
    if (p < half) {
        return (p * span) / half;
    }
    return ((period - p) * span) / (period - half);
}

static void disco_anim_apply(uint32_t tick)
{
    uint32_t accent = s_cfg.accent_color;
    uint32_t pulse = tri_wave(tick, 48U, 14U);
    uint32_t bg_top = blend_hex(0x120828u, disco_neon(tick / 3U), 55U + (pulse / 4U));
    uint32_t bg_bot = blend_hex(0x000010u, accent, 28U + (tick % 7U) * 4U);

    lv_obj_set_style_bg_color(s_screen, lv_color_hex(bg_top), 0);
    lv_obj_set_style_bg_grad_color(s_screen, lv_color_hex(bg_bot), 0);
    lv_obj_set_style_bg_grad_dir(s_screen, LV_GRAD_DIR_VER, 0);

    for (uint32_t i = 0U; i < DISCO_TILE_COUNT; i++) {
        uint32_t col = i % DISCO_TILE_COLS;
        uint32_t row = i / DISCO_TILE_COLS;
        uint32_t wave = (col + row + tick) % 8U;
        uint32_t c = blend_hex(disco_neon(wave + (tick % 4U)), accent, 38U + (uint32_t)((col + row + tick) % 3U) * 6U);
        lv_obj_set_style_bg_color(s_disco_tiles[i], lv_color_hex(c), 0);
    }

    for (uint32_t s = 0U; s < DISCO_SPOT_COUNT; s++) {
        uint32_t t = tick + s * 7U;
        uint32_t x_max = (uint32_t)(CONTROL_FLOW_SCREEN_W - DISCO_SPOT_SIZE);
        uint32_t y_max = (uint32_t)(DISCO_FLOOR_H - DISCO_SPOT_SIZE);
        int32_t x = (int32_t)tri_wave(t * (11U + s * 3U), x_max, 38U + s * 5U);
        int32_t y = (int32_t)tri_wave(t * (13U + s * 2U), y_max, 44U + s * 4U);
        uint32_t spot_c = blend_hex(disco_neon(s + tick), 0xFFFFFFu, 110U);
        lv_obj_set_pos(s_disco_spots[s], x, y);
        lv_obj_set_style_bg_color(s_disco_spots[s], lv_color_hex(spot_c), 0);
    }
}

static void disco_layer_create(lv_obj_t *parent)
{
    s_disco_layer = lv_obj_create(parent);
    lv_obj_remove_style_all(s_disco_layer);
    lv_obj_set_size(s_disco_layer, CONTROL_FLOW_SCREEN_W, CONTROL_FLOW_SCREEN_H);
    lv_obj_set_pos(s_disco_layer, 0, 0);
    lv_obj_set_style_bg_opa(s_disco_layer, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(s_disco_layer, LV_OBJ_FLAG_SCROLLABLE);

    for (uint32_t row = 0U; row < DISCO_TILE_ROWS; row++) {
        for (uint32_t col = 0U; col < DISCO_TILE_COLS; col++) {
            uint32_t i = row * DISCO_TILE_COLS + col;
            lv_obj_t *tile = lv_obj_create(s_disco_layer);
            lv_obj_remove_style_all(tile);
            lv_obj_set_size(tile, (int32_t)DISCO_TILE_W, (int32_t)DISCO_TILE_H);
            lv_obj_set_pos(tile, (int32_t)(col * DISCO_TILE_W), (int32_t)(row * DISCO_TILE_H));
            lv_obj_set_style_radius(tile, 6, 0);
            lv_obj_set_style_border_width(tile, 1, 0);
            lv_obj_set_style_border_opa(tile, LV_OPA_30, 0);
            lv_obj_set_style_border_color(tile, lv_color_hex(0x000000u), 0);
            lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
            s_disco_tiles[i] = tile;
        }
    }

    for (uint32_t s = 0U; s < DISCO_SPOT_COUNT; s++) {
        lv_obj_t *spot = lv_obj_create(s_disco_layer);
        lv_obj_remove_style_all(spot);
        lv_obj_set_size(spot, DISCO_SPOT_SIZE, DISCO_SPOT_SIZE);
        lv_obj_set_style_radius(spot, DISCO_SPOT_R, 0);
        lv_obj_set_style_bg_opa(spot, LV_OPA_50, 0);
        lv_obj_set_style_border_width(spot, 0, 0);
        lv_obj_clear_flag(spot, LV_OBJ_FLAG_SCROLLABLE);
        s_disco_spots[s] = spot;
    }

    lv_obj_add_flag(s_disco_layer, LV_OBJ_FLAG_HIDDEN);
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
    lv_obj_set_style_bg_opa(s_card, LV_OPA_COVER, 0);
    int32_t card_y = CONTROL_FLOW_CARD_IDLE_Y;
    if (s_cfg.center_icon_text != NULL && !s_cfg.supports_value) {
        card_y = CONTROL_FLOW_CARD_CENTERED_Y;
    }
    lv_obj_set_pos(s_card, CONTROL_FLOW_CARD_IDLE_X, card_y);
    lv_obj_set_size(s_card, CONTROL_FLOW_CARD_IDLE_W, CONTROL_FLOW_CARD_IDLE_H);
    lv_obj_set_style_radius(s_card, 28, 0);
    lv_obj_set_style_clip_corner(s_card, false, 0);
    lv_obj_set_style_shadow_color(s_card, lv_color_hex(accent), 0);
    lv_obj_set_style_shadow_width(s_card, 24, 0);
    lv_obj_set_style_shadow_opa(s_card, LV_OPA_40, 0);
    lv_obj_set_style_border_color(s_card, lv_color_hex(blend_hex(0xFFFFFFu, accent, 128u)), 0);
    lv_obj_set_style_border_width(s_card, 2, 0);
    lv_obj_set_style_border_opa(s_card, LV_OPA_COVER, 0);
    lv_obj_set_style_translate_x(s_card, 0, 0);
    lv_obj_set_style_translate_y(s_card, 0, 0);

    lv_obj_set_style_text_color(s_title_label, lv_color_hex(0xF8FAFCu), 0);
    lv_obj_set_style_text_color(s_status_label, lv_color_hex(0xD4DCE8u), 0);
    if (s_center_icon_label != NULL) {
        lv_obj_set_style_text_color(s_center_icon_label, lv_color_hex(0xF8FAFCu), 0);
    }
    if (s_running_status_label != NULL) {
        lv_obj_add_flag(s_running_status_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_text_color(s_running_status_label, lv_color_hex(0xF8FAFCu), 0);
    }
    if (s_value_label != NULL) {
        lv_obj_set_style_text_color(s_value_label, lv_color_hex(0xF8FAFCu), 0);
    }
    if (s_control_card != NULL) {
        lv_obj_clear_flag(s_control_card, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_status_label != NULL) {
        lv_obj_clear_flag(s_status_label, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_title_label != NULL) {
        lv_obj_clear_flag(s_title_label, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_disco_layer != NULL) {
        lv_obj_add_flag(s_disco_layer, LV_OBJ_FLAG_HIDDEN);
    }

    if (s_submit_button != NULL) {
        set_button_palette(s_submit_button, s_cfg.accent_color);
        lv_obj_set_style_shadow_width(s_submit_button, 14, 0);
    }
    if (s_secondary_button != NULL) {
        set_button_palette(s_secondary_button, blend_hex(s_cfg.accent_color, 0xFFFFFFu, 96u));
        lv_obj_set_style_shadow_width(s_secondary_button, 14, 0);
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

static void update_idle_status(void)
{
    if (s_cfg.center_icon_text != NULL) {
        set_status_text("");
        return;
    }
    if (s_cfg.supports_value) {
        set_status_text("Adjust and submit value.");
    } else if (s_cfg.supports_dual_action) {
        set_status_text("Choose branch: Execute or Skip.");
    } else {
        set_status_text("Ready. Awaiting run command.");
    }
}

static void running_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    if (!s_running) {
        return;
    }

    s_anim_tick++;
    disco_anim_apply(s_anim_tick);
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
    lv_obj_set_style_translate_y(s_status_label, 0, 0);
    if (s_running_status_label != NULL) {
        lv_obj_add_flag(s_running_status_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_translate_y(s_running_status_label, 0, 0);
    }
    if (s_control_card != NULL) {
        lv_obj_clear_flag(s_control_card, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_cfg.center_icon_text != NULL) {
        if (s_card != NULL) {
            lv_obj_clear_flag(s_card, LV_OBJ_FLAG_HIDDEN);
        }
        if (s_title_label != NULL) {
            lv_obj_add_flag(s_title_label, LV_OBJ_FLAG_HIDDEN);
        }
        if (s_status_label != NULL) {
            lv_obj_add_flag(s_status_label, LV_OBJ_FLAG_HIDDEN);
        }
        if (s_center_icon_label != NULL) {
            lv_obj_clear_flag(s_center_icon_label, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(s_center_icon_label);
        }
    } else {
        if (s_card != NULL) {
            lv_obj_clear_flag(s_card, LV_OBJ_FLAG_HIDDEN);
        }
        if (s_title_label != NULL) {
            lv_obj_clear_flag(s_title_label, LV_OBJ_FLAG_HIDDEN);
        }
        if (s_status_label != NULL) {
            lv_obj_clear_flag(s_status_label, LV_OBJ_FLAG_HIDDEN);
        }
        if (s_center_icon_label != NULL) {
            lv_obj_add_flag(s_center_icon_label, LV_OBJ_FLAG_HIDDEN);
        }
    }
    update_value_label();
    update_idle_status();
    if (s_submit_button != NULL) {
        const char *primary_label = s_cfg.supports_dual_action
            ? ((s_cfg.primary_action_label != NULL) ? s_cfg.primary_action_label : "Execute")
            : "Submit";
        set_button_label(s_submit_button, primary_label);
    }
    if (s_secondary_button != NULL) {
        const char *secondary_label = (s_cfg.secondary_action_label != NULL)
            ? s_cfg.secondary_action_label
            : "Skip";
        set_button_label(s_secondary_button, secondary_label);
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
    CFUI_LOG("start_running_state: disco_layer=%p", (void *)s_disco_layer);

    if (s_disco_layer != NULL) {
        lv_obj_clear_flag(s_disco_layer, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_background(s_disco_layer);
    }
    disco_anim_apply(0U);

    if (s_card != NULL) {
        lv_obj_add_flag(s_card, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_center_icon_label != NULL) {
        lv_obj_add_flag(s_center_icon_label, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_running_status_label != NULL) {
        lv_label_set_text(s_running_status_label, "Disco time! Block is running!");
        lv_obj_clear_flag(s_running_status_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_text_color(s_running_status_label, lv_color_hex(0xFFFFFFu), 0);
        lv_obj_set_style_translate_y(s_running_status_label, 0, 0);
    }
    if (s_control_card != NULL) {
        lv_obj_add_flag(s_control_card, LV_OBJ_FLAG_HIDDEN);
    }

    if (s_running_status_label != NULL) {
        lv_obj_move_foreground(s_running_status_label);
    }

    if (s_anim_timer == NULL) {
        s_anim_timer = lv_timer_create(running_timer_cb, CONTROL_FLOW_ANIM_PERIOD_MS, NULL);
    } else {
        lv_timer_set_period(s_anim_timer, CONTROL_FLOW_ANIM_PERIOD_MS);
        lv_timer_resume(s_anim_timer);
    }
    lv_timer_reset(s_anim_timer);
    lv_timer_ready(s_anim_timer);
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

static void primary_action_button_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    if (s_cfg.primary_action_cb == NULL) {
        set_status_text("No primary action callback connected.");
        return;
    }

    if (s_cfg.primary_action_cb()) {
        set_status_text("Execute selected.");
    } else {
        set_status_text("Execute action failed.");
    }
}

static void secondary_action_button_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    if (s_cfg.secondary_action_cb == NULL) {
        set_status_text("No secondary action callback connected.");
        return;
    }

    if (s_cfg.secondary_action_cb()) {
        set_status_text("Skip selected.");
    } else {
        set_status_text("Skip action failed.");
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
    lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);
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
    s_disco_layer = NULL;
    memset(s_disco_tiles, 0, sizeof(s_disco_tiles));
    memset(s_disco_spots, 0, sizeof(s_disco_spots));
    s_card = NULL;
    s_control_card = NULL;
    s_status_label = NULL;
    s_running_status_label = NULL;
    s_title_label = NULL;
    s_center_icon_label = NULL;
    s_value_label = NULL;
    s_submit_button = NULL;
    s_secondary_button = NULL;
    s_minus_button = NULL;
    s_plus_button = NULL;

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
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    disco_layer_create(s_screen);

    s_card = lv_obj_create(s_screen);
    lv_obj_set_size(s_card, 212, 160);
    lv_obj_set_pos(s_card, 14, 14);
    lv_obj_set_style_radius(s_card, 28, 0);
    lv_obj_set_style_border_width(s_card, 2, 0);
    lv_obj_set_style_pad_all(s_card, 0, 0);
    lv_obj_set_style_shadow_width(s_card, 24, 0);
    lv_obj_set_style_shadow_opa(s_card, LV_OPA_40, 0);
    lv_obj_clear_flag(s_card, LV_OBJ_FLAG_SCROLLABLE);
    s_title_label = lv_label_create(s_card);
    lv_label_set_text(s_title_label, (s_cfg.title != NULL) ? s_cfg.title : "BLOCK");
    lv_obj_set_width(s_title_label, 180);
    lv_obj_set_pos(s_title_label, 16, 34);
    lv_obj_set_style_text_align(s_title_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(s_title_label, CONTROL_FLOW_TITLE_FONT, 0);

    s_status_label = lv_label_create(s_card);
    lv_obj_set_width(s_status_label, 178);
    lv_obj_set_pos(s_status_label, 17, 98);
    lv_obj_set_style_text_align(s_status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(s_status_label, LV_FONT_DEFAULT, 0);
    lv_obj_set_style_text_color(s_status_label, lv_color_hex(0xD4DCE8u), 0);
    lv_label_set_long_mode(s_status_label, LV_LABEL_LONG_WRAP);

    s_center_icon_label = lv_label_create(s_card);
    lv_label_set_text(s_center_icon_label,
                      (s_cfg.center_icon_text != NULL) ? s_cfg.center_icon_text : "");
    lv_obj_set_width(s_center_icon_label, 180);
    lv_obj_set_style_text_align(s_center_icon_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(s_center_icon_label, CONTROL_FLOW_TITLE_FONT, 0);
    lv_obj_set_style_text_color(s_center_icon_label, lv_color_hex(0xF8FAFCu), 0);
    lv_obj_center(s_center_icon_label);
    if (s_cfg.center_icon_text == NULL) {
        lv_obj_add_flag(s_center_icon_label, LV_OBJ_FLAG_HIDDEN);
    }

    s_running_status_label = lv_label_create(s_screen);
    lv_obj_set_width(s_running_status_label, 224);
    lv_obj_set_pos(s_running_status_label, 8, 188);
    lv_obj_set_style_text_align(s_running_status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(s_running_status_label, &mochi_boom_28, 0);
    lv_obj_set_style_text_color(s_running_status_label, lv_color_hex(0xFFFFFFu), 0);
    lv_obj_set_style_text_opa(s_running_status_label, LV_OPA_COVER, 0);
    lv_label_set_long_mode(s_running_status_label, LV_LABEL_LONG_WRAP);
    lv_obj_add_flag(s_running_status_label, LV_OBJ_FLAG_HIDDEN);

    lv_obj_move_foreground(s_title_label);
    lv_obj_move_foreground(s_status_label);
    lv_obj_move_foreground(s_running_status_label);

    if (s_cfg.supports_value) {
        s_control_card = lv_obj_create(s_screen);
        lv_obj_set_size(s_control_card, 212, 118);
        lv_obj_set_pos(s_control_card, 14, 188);
        lv_obj_set_style_radius(s_control_card, 24, 0);
        lv_obj_set_style_bg_color(s_control_card, lv_color_hex(0x101728u), 0);
        lv_obj_set_style_bg_grad_color(s_control_card, lv_color_hex(0x17213Au), 0);
        lv_obj_set_style_bg_grad_dir(s_control_card, LV_GRAD_DIR_VER, 0);
        lv_obj_set_style_border_width(s_control_card, 1, 0);
        lv_obj_set_style_border_color(s_control_card, lv_color_hex(0x24324Du), 0);
        lv_obj_set_style_pad_all(s_control_card, 0, 0);
        lv_obj_clear_flag(s_control_card, LV_OBJ_FLAG_SCROLLABLE);

        s_minus_button = create_button(s_control_card, 18, 16, 42, 38, "-");
        s_plus_button = create_button(s_control_card, 152, 16, 42, 38, "+");
        set_button_palette(s_minus_button, blend_hex(s_cfg.accent_color, 0xFFFFFFu, 96u));
        set_button_palette(s_plus_button, blend_hex(s_cfg.accent_color, 0xFFFFFFu, 96u));
        lv_obj_add_event_cb(s_minus_button, minus_button_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_add_event_cb(s_plus_button, plus_button_cb, LV_EVENT_CLICKED, NULL);

        s_value_label = lv_label_create(s_control_card);
        lv_obj_set_width(s_value_label, 88);
        lv_obj_set_pos(s_value_label, 62, 20);
        lv_obj_set_style_text_align(s_value_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(s_value_label, LV_FONT_DEFAULT, 0);
        lv_obj_set_style_text_color(s_value_label, lv_color_hex(0xF8FAFCu), 0);

        s_submit_button = create_button(s_control_card, 64, 68, 84, 34, "Submit");
        set_button_palette(s_submit_button, s_cfg.accent_color);
        lv_obj_add_event_cb(s_submit_button, submit_button_cb, LV_EVENT_CLICKED, NULL);
        s_secondary_button = NULL;
    } else if (s_cfg.supports_dual_action) {
        s_control_card = lv_obj_create(s_screen);
        lv_obj_set_size(s_control_card, 212, 96);
        lv_obj_set_pos(s_control_card, 14, 204);
        lv_obj_set_style_radius(s_control_card, 24, 0);
        lv_obj_set_style_bg_color(s_control_card, lv_color_hex(0x101728u), 0);
        lv_obj_set_style_bg_grad_color(s_control_card, lv_color_hex(0x17213Au), 0);
        lv_obj_set_style_bg_grad_dir(s_control_card, LV_GRAD_DIR_VER, 0);
        lv_obj_set_style_border_width(s_control_card, 1, 0);
        lv_obj_set_style_border_color(s_control_card, lv_color_hex(0x24324Du), 0);
        lv_obj_set_style_pad_all(s_control_card, 0, 0);
        lv_obj_clear_flag(s_control_card, LV_OBJ_FLAG_SCROLLABLE);

        const char *primary_label = (s_cfg.primary_action_label != NULL) ? s_cfg.primary_action_label : "Execute";
        const char *secondary_label = (s_cfg.secondary_action_label != NULL) ? s_cfg.secondary_action_label : "Skip";
        s_submit_button = create_button(s_control_card, 12, 18, 92, 56, primary_label);
        s_secondary_button = create_button(s_control_card, 108, 18, 92, 56, secondary_label);
        set_button_palette(s_submit_button, s_cfg.accent_color);
        set_button_palette(s_secondary_button, blend_hex(s_cfg.accent_color, 0xFFFFFFu, 96u));
        lv_obj_add_event_cb(s_submit_button, primary_action_button_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_add_event_cb(s_secondary_button, secondary_action_button_cb, LV_EVENT_CLICKED, NULL);

        s_value_label = NULL;
        s_minus_button = NULL;
        s_plus_button = NULL;
    } else {
        s_control_card = NULL;
        s_value_label = NULL;
        s_minus_button = NULL;
        s_plus_button = NULL;
        s_submit_button = NULL;
        s_secondary_button = NULL;
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
