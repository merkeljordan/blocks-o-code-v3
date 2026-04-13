#include "tft_ui.h"

#include "control_flow_tft_ui.h"

#if defined(ESP_PLATFORM)
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "control_flow_tft_hw.h"

static const char *TAG = "THEN_TFT_UI";
#define TFT_BOOT_START_DELAY_MS 500
#endif

void tft_ui_start(void)
{
    static const control_flow_ui_config_t k_cfg = {
        .title = "THEN",
        .center_icon_text = "THEN",
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
    vTaskDelay(pdMS_TO_TICKS(TFT_BOOT_START_DELAY_MS));
    esp_err_t err = control_flow_tft_hw_start(&k_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "control_flow_tft_hw_start failed: %s", esp_err_to_name(err));
        ESP_LOGW(TAG, "Continuing boot without TFT UI.");
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
