#pragma once

#include "esp_err.h"

#include "control_flow_tft_ui.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t control_flow_tft_hw_start(const control_flow_ui_config_t *cfg);

#ifdef __cplusplus
}
#endif
