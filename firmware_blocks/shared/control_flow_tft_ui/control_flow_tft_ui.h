#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef bool (*control_flow_ui_submit_cb_t)(uint32_t value);
typedef bool (*control_flow_ui_action_cb_t)(void);

typedef struct {
    const char *title;
    const char *center_icon_text;
    uint32_t accent_color;
    bool supports_value;
    uint32_t min_value;
    uint32_t max_value;
    uint32_t step;
    uint32_t default_value;
    const char *value_suffix;
    control_flow_ui_submit_cb_t submit_cb;
    bool supports_dual_action;
    const char *primary_action_label;
    const char *secondary_action_label;
    control_flow_ui_action_cb_t primary_action_cb;
    control_flow_ui_action_cb_t secondary_action_cb;
} control_flow_ui_config_t;

void control_flow_tft_ui_start(const control_flow_ui_config_t *cfg);
void control_flow_tft_ui_trigger_execute(void);
void control_flow_tft_ui_set_idle(void);
void control_flow_tft_ui_set_value(uint32_t value);

#ifdef __cplusplus
}
#endif
