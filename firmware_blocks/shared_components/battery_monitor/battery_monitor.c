#include "battery_monitor.h"

#include <stdbool.h>

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/*
 * Battery monitor assumptions:
 * - 1-cell Li-ion battery (about 7.0V empty to 8.4V full)
 * - Battery is connected to ADC through a resistor divider
 * - Example divider: 2k (top) / 1k (bottom) -> divide by 2
 */
#define BATTERY_VOLTAGE_EMPTY_MV 7000.0f
#define BATTERY_VOLTAGE_FULL_MV  8400.0f

#define ADC_UNIT_USED          ADC_UNIT_1
#define ADC_CHANNEL_BATTERY    ADC_CHANNEL_6
#define ADC_ATTEN_USED         ADC_ATTEN_DB_12
#define ADC_BITWIDTH_USED      ADC_BITWIDTH_DEFAULT

#define DIVIDER_R_TOP_OHMS     2000.0f
#define DIVIDER_R_BOTTOM_OHMS  1000.0f

#define BATTERY_PERCENT_STUB      100U
#define BATTERY_MONITOR_PERIOD_MS 2000U
#define BATTERY_MONITOR_STACK_SIZE 2048
#define BATTERY_MONITOR_PRIORITY   2

static adc_oneshot_unit_handle_t s_adc_handle;
static adc_cali_handle_t s_adc_cali_handle = NULL;
static float s_last_battery_voltage_mv = -1.0f;
static bool s_monitor_started = false;

static const char *TAG = "battery_monitor";

static float clampf(float value, float min, float max)
{
    if (value < min) {
        return min;
    }
    if (value > max) {
        return max;
    }
    return value;
}

static uint8_t voltage_to_percentage(float battery_mv)
{
    float percent = ((battery_mv - BATTERY_VOLTAGE_EMPTY_MV) * 100.0f) /
                    (BATTERY_VOLTAGE_FULL_MV - BATTERY_VOLTAGE_EMPTY_MV);
    percent = clampf(percent, 0.0f, 100.0f);
    return (uint8_t)(percent + 0.5f);
}

void battery_monitor_update_voltage(float volts)
{
    s_last_battery_voltage_mv = volts * 1000.0f;
}

uint8_t battery_monitor_get_percent(void)
{
    if (s_last_battery_voltage_mv <= 0.0f) {
        return BATTERY_PERCENT_STUB;
    }

    return voltage_to_percentage(s_last_battery_voltage_mv);
}

static float battery_monitor_read_voltage_mv(void)
{
    int raw = 0;
    int adc_pin_mv = 0;
    esp_err_t err;

    err = adc_oneshot_read(s_adc_handle, ADC_CHANNEL_BATTERY, &raw);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "adc_oneshot_read failed: %s", esp_err_to_name(err));
        return -1.0f;
    }

    if (s_adc_cali_handle != NULL) {
        if (adc_cali_raw_to_voltage(s_adc_cali_handle, raw, &adc_pin_mv) != ESP_OK) {
            return -1.0f;
        }
    } else {
        const float adc_reference_mv = 3000.0f;
        const int adc_max_count = 4095;
        adc_pin_mv = (int)(((float)raw / (float)adc_max_count) * adc_reference_mv);
    }

    return adc_pin_mv *
           ((DIVIDER_R_TOP_OHMS + DIVIDER_R_BOTTOM_OHMS) / DIVIDER_R_BOTTOM_OHMS);
}

static void battery_monitor_task(void *arg)
{
    (void)arg;

    while (true) {
        const float battery_mv = battery_monitor_read_voltage_mv();
        if (battery_mv > 0.0f) {
            s_last_battery_voltage_mv = battery_mv;
            ESP_LOGD(TAG, "battery voltage=%.0fmV percent=%u", (double)battery_mv,
                     (unsigned)battery_monitor_get_percent());
        }

        vTaskDelay(pdMS_TO_TICKS(BATTERY_MONITOR_PERIOD_MS));
    }
}

esp_err_t battery_monitor_start(void)
{
    BaseType_t task_ok;
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT_USED,
    };
    adc_oneshot_chan_cfg_t chan_config = {
        .bitwidth = ADC_BITWIDTH_USED,
        .atten = ADC_ATTEN_USED,
    };
    adc_cali_line_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT_USED,
        .atten = ADC_ATTEN_USED,
        .bitwidth = ADC_BITWIDTH_USED,
    };

    if (s_monitor_started) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(adc_oneshot_new_unit(&init_config, &s_adc_handle), TAG,
                        "adc_oneshot_new_unit failed");
    ESP_RETURN_ON_ERROR(adc_oneshot_config_channel(s_adc_handle, ADC_CHANNEL_BATTERY, &chan_config),
                        TAG, "adc_oneshot_config_channel failed");

    if (adc_cali_create_scheme_line_fitting(&cali_config, &s_adc_cali_handle) != ESP_OK) {
        s_adc_cali_handle = NULL;
    }

    task_ok = xTaskCreate(battery_monitor_task, "battery_monitor",
                          BATTERY_MONITOR_STACK_SIZE, NULL,
                          BATTERY_MONITOR_PRIORITY, NULL);
    if (task_ok != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    s_monitor_started = true;
    return ESP_OK;
}
