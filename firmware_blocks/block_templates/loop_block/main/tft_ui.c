#include "tft_ui.h"

#include <stdint.h>

#include "control_flow_tft_ui.h"

#if defined(ESP_PLATFORM)
#include "esp_log.h"
#include "control_flow_tft_hw.h"

static const char *TAG = "LOOP_TFT_UI";
#endif

extern void loop_block_set_loop_count_from_ui(uint8_t loop_count);

static bool submit_loop_value(uint32_t value)
{
    loop_block_set_loop_count_from_ui((uint8_t)value);
    return true;
}

void tft_ui_start(void)
{
    static const control_flow_ui_config_t k_cfg = {
        .title = "LOOP",
        .accent_color = 0x60A5FAu,
        .supports_value = true,
        .min_value = 1,
        .max_value = 99,
        .step = 1,
        .default_value = 1,
        .value_suffix = "x",
        .submit_cb = submit_loop_value,
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
