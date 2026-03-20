#include "tft_ui.h"

#include "control_flow_tft_ui.h"

#if defined(ESP_PLATFORM)
#include "esp_log.h"
#include "control_flow_tft_hw.h"

static const char *TAG = "END_IF_TFT_UI";
#endif

void tft_ui_start(void)
{
    static const control_flow_ui_config_t k_cfg = {
        .title = "END IF",
        .accent_color = 0x34D399u,
        .supports_value = false,
        .min_value = 0,
        .max_value = 0,
        .step = 0,
        .default_value = 0,
        .value_suffix = "",
        .submit_cb = NULL,
    };

#if defined(ESP_PLATFORM)
    esp_err_t err = control_flow_tft_hw_start(&k_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "control_flow_tft_hw_start failed: %s", esp_err_to_name(err));
    }
#else
    control_flow_tft_ui_start(&k_cfg);
#endif
}

void tft_ui_trigger_execute(void)
{
    control_flow_tft_ui_trigger_execute();
}

void tft_ui_set_idle(void)
{
    control_flow_tft_ui_set_idle();
}
