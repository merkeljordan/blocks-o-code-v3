#include "battery_monitor.h"

#include <stdbool.h>

#include "driver/adc.h"
#include "esp_adc_cal.h"
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

#define ADC_CHANNEL_BATTERY    ADC1_CHANNEL_6
#define ADC_ATTEN_USED         ADC_ATTEN_DB_12
#define ADC_REFERENCE_MV       3300.0f
#define ADC_SAMPLE_COUNT       8

/*
 * Calibration knobs:
 * - BATTERY_CAL_SCALE: multiplicative correction for resistor tolerances/ADC gain.
 * - BATTERY_CAL_OFFSET_MV: additive correction if a fixed offset is observed.
 */
#define BATTERY_CAL_SCALE      1.00f
#define BATTERY_CAL_OFFSET_MV  0.0f

#define DIVIDER_R_TOP_OHMS     2000.0f
#define DIVIDER_R_BOTTOM_OHMS  1000.0f

#define BATTERY_PERCENT_STUB      100U
#define BATTERY_MONITOR_PERIOD_MS 2000U
#define BATTERY_MONITOR_STACK_SIZE 2048
#define BATTERY_MONITOR_PRIORITY   2

static float s_last_battery_voltage_mv = -1.0f;
static bool s_monitor_started = false;
static bool s_adc_cal_ready = false;
static esp_adc_cal_characteristics_t s_adc_chars;

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

static float battery_monitor_read_voltage_mv(int *raw_out, float *adc_pin_mv_out)
{
    int raw = 0;
    int raw_sum = 0;
    float adc_pin_mv = 0.0f;
    float battery_mv = 0.0f;

    for (int i = 0; i < ADC_SAMPLE_COUNT; i++) {
        raw = adc1_get_raw(ADC_CHANNEL_BATTERY);
        if (raw < 0) {
            ESP_LOGE(TAG, "adc1_get_raw failed: %d", raw);
            return -1.0f;
        }
        raw_sum += raw;
    }
    raw = raw_sum / ADC_SAMPLE_COUNT;

    if (s_adc_cal_ready) {
        adc_pin_mv = (float)esp_adc_cal_raw_to_voltage(raw, &s_adc_chars);
    } else {
        adc_pin_mv = ((float)raw / 4095.0f) * ADC_REFERENCE_MV;
    }

    battery_mv = adc_pin_mv *
                 ((DIVIDER_R_TOP_OHMS + DIVIDER_R_BOTTOM_OHMS) / DIVIDER_R_BOTTOM_OHMS);
    battery_mv = (battery_mv * BATTERY_CAL_SCALE) + BATTERY_CAL_OFFSET_MV;

    if (raw_out != NULL) {
        *raw_out = raw;
    }
    if (adc_pin_mv_out != NULL) {
        *adc_pin_mv_out = adc_pin_mv;
    }
    return battery_mv;
}

static void battery_monitor_task(void *arg)
{
    (void)arg;

    while (true) {
        int raw = 0;
        float adc_pin_mv = 0.0f;
        const float battery_mv = battery_monitor_read_voltage_mv(&raw, &adc_pin_mv);
        if (battery_mv > 0.0f) {
            s_last_battery_voltage_mv = battery_mv;
            ESP_LOGD(
                TAG,
                "raw=%d adc_pin=%.0fmV battery=%.0fmV percent=%u",
                raw,
                (double)adc_pin_mv,
                (double)battery_mv,
                (unsigned)battery_monitor_get_percent()
            );
        }

        vTaskDelay(pdMS_TO_TICKS(BATTERY_MONITOR_PERIOD_MS));
    }
}

esp_err_t battery_monitor_start(void)
{
    BaseType_t task_ok;
    esp_err_t err;

    if (s_monitor_started) {
        return ESP_OK;
    }

    err = adc1_config_width(ADC_WIDTH_BIT_12);
    ESP_RETURN_ON_ERROR(err, TAG, "adc1_config_width failed");
    err = adc1_config_channel_atten(ADC_CHANNEL_BATTERY, ADC_ATTEN_USED);
    ESP_RETURN_ON_ERROR(err, TAG, "adc1_config_channel_atten failed");

    // Legacy ADC calibration for improved mV estimation.
    s_adc_cal_ready = true;
    esp_adc_cal_value_t cal_type = esp_adc_cal_characterize(
        ADC_UNIT_1,
        ADC_ATTEN_USED,
        ADC_WIDTH_BIT_12,
        1100,
        &s_adc_chars
    );
    if (cal_type == ESP_ADC_CAL_VAL_EFUSE_TP) {
        ESP_LOGI(TAG, "ADC calibration: eFuse Two Point");
    } else if (cal_type == ESP_ADC_CAL_VAL_EFUSE_VREF) {
        ESP_LOGI(TAG, "ADC calibration: eFuse Vref");
    } else {
        ESP_LOGW(TAG, "ADC calibration: default Vref");
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
