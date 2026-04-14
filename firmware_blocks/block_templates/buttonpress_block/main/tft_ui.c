#include "tft_ui.h"

#include <stdint.h>

#include "control_flow_tft_ui.h"

#if defined(ESP_PLATFORM)
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "control_flow_tft_hw.h"

static const char *TAG = "BUTTON_TFT_UI";
#define TFT_BOOT_START_DELAY_MS 500
#endif

extern void button_block_execute_from_ui(void);
extern void button_block_pass_from_ui(void);

static bool on_execute_action(void)
{
    button_block_execute_from_ui();
    control_flow_tft_ui_set_press_now_visible(false);
    return true;
}

static bool on_skip_action(void)
{
    button_block_pass_from_ui();
    control_flow_tft_ui_set_press_now_visible(false);
    return true;
}

void tft_ui_start(void)
{
    static const control_flow_ui_config_t k_cfg = {
        .title = "BUTTON",
        .accent_color = 0x60A5FAu,
        .supports_value = false,
        .min_value = 0,
        .max_value = 0,
        .step = 0,
        .default_value = 0,
        .value_suffix = "",
        .submit_cb = NULL,
        .supports_dual_action = true,
        .primary_action_label = "Execute",
        .secondary_action_label = "Skip",
        .primary_action_cb = on_execute_action,
        .secondary_action_cb = on_skip_action,
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

void tft_ui_set_press_now_visible(bool visible)
{
    control_flow_tft_ui_set_press_now_visible(visible);
}
