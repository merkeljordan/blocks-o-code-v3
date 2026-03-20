#include "tft_ui.h"

#include <stdint.h>

#include "control_flow_tft_ui.h"

#if defined(ESP_PLATFORM)
#include "esp_log.h"
#include "control_flow_tft_hw.h"

static const char *TAG = "DELAY_TFT_UI";
#endif

extern void delay_block_set_delay_ms_from_ui(uint32_t delay_ms);

static bool submit_delay_value(uint32_t value)
{
    delay_block_set_delay_ms_from_ui(value);
    return true;
}

void tft_ui_start(void)
{
    static const control_flow_ui_config_t k_cfg = {
        .title = "DELAY",
        .accent_color = 0xF59E0Bu,
        .supports_value = true,
        .min_value = 100,
        .max_value = 5000,
        .step = 100,
        .default_value = 500,
        .value_suffix = "ms",
        .submit_cb = submit_delay_value,
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
