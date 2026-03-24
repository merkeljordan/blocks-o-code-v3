#include "tft_ui.h"

#include <stdint.h>

#include "control_flow_tft_ui.h"

#if defined(ESP_PLATFORM)
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "control_flow_tft_hw.h"

static const char *TAG = "DELAY_TFT_UI";
#define TFT_BOOT_START_DELAY_MS 800
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
    // Let supply rails settle after switch-on before enabling TFT stack.
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
