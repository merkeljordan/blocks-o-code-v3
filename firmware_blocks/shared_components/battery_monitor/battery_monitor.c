#include "battery_monitor.h"

#include <stdbool.h>

#include "driver/adc.h"
#include "esp_adc_cal.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* Include block-specific calibration when provided by the block project. */
#ifdef BATTERY_MONITOR_HAS_CAL_HEADER
#include "battery_calibration.h"
#endif

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
 * Block-specific defaults are defined in battery_calibration.h (included above).
 * Use fallback defaults only if those macros are not defined.
 */
#ifndef BATTERY_CAL_SCALE
#define BATTERY_CAL_SCALE      0.995f
#endif
#ifndef BATTERY_CAL_OFFSET_MV
#define BATTERY_CAL_OFFSET_MV  0.0f
#endif

#define DIVIDER_R_TOP_OHMS     2000.0f
#define DIVIDER_R_BOTTOM_OHMS  1000.0f

#define BATTERY_PERCENT_STUB      100U
#define BATTERY_MONITOR_PERIOD_MS 2000U
#define BATTERY_MONITOR_STACK_SIZE 2048
#define BATTERY_MONITOR_PRIORITY   2
#define BATTERY_FILTER_ALPHA       0.25f
#define BATTERY_FULL_CONFIRM_MV    8420.0f
#define BATTERY_FULL_CONFIRM_SAMPLES 15U
#define CHARGE_TREND_RISE_MV       4.0f
#define CHARGE_TREND_FALL_MV      -4.0f
#define CHARGE_UNPLUG_DROP_MV     -12.0f
#define CHARGE_SCORE_SET_THRESHOLD 6
#define CHARGE_SCORE_CLEAR_THRESHOLD -2
#define CHARGE_SCORE_MAX           20

static float s_last_battery_voltage_mv = -1.0f;
static float s_filtered_battery_voltage_mv = -1.0f;
static float s_prev_filtered_battery_voltage_mv = -1.0f;
static bool s_monitor_started = false;
static bool s_adc_cal_ready = false;
static esp_adc_cal_characteristics_t s_adc_chars;
static uint32_t s_full_confirm_count = 0;
static uint8_t s_display_percent = BATTERY_PERCENT_STUB;
static int s_charge_trend_score = 0;
static bool s_is_charging = false;

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
    s_filtered_battery_voltage_mv = s_last_battery_voltage_mv;
    s_display_percent = voltage_to_percentage(s_filtered_battery_voltage_mv);
}

uint8_t battery_monitor_get_percent(void)
{
    if (!s_monitor_started && s_last_battery_voltage_mv <= 0.0f) {
        return BATTERY_PERCENT_STUB;
    }

    return s_display_percent;
}

bool battery_monitor_is_charging(void)
{
    return s_is_charging;
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
            uint8_t percent = 0;
            float delta_mv = 0.0f;
            if (s_filtered_battery_voltage_mv <= 0.0f) {
                s_filtered_battery_voltage_mv = battery_mv;
            } else {
                s_filtered_battery_voltage_mv =
                    ((1.0f - BATTERY_FILTER_ALPHA) * s_filtered_battery_voltage_mv) +
                    (BATTERY_FILTER_ALPHA * battery_mv);
            }

            if (s_prev_filtered_battery_voltage_mv > 0.0f) {
                delta_mv = s_filtered_battery_voltage_mv - s_prev_filtered_battery_voltage_mv;
            }
            s_prev_filtered_battery_voltage_mv = s_filtered_battery_voltage_mv;

            if (delta_mv > CHARGE_TREND_RISE_MV) {
                s_charge_trend_score += 2;
            } else if (delta_mv < CHARGE_TREND_FALL_MV) {
                s_charge_trend_score -= 2;
            } else if (s_charge_trend_score > 0) {
                s_charge_trend_score -= 1;
            } else if (s_charge_trend_score < 0) {
                s_charge_trend_score += 1;
            }
            if (s_charge_trend_score > CHARGE_SCORE_MAX) {
                s_charge_trend_score = CHARGE_SCORE_MAX;
            } else if (s_charge_trend_score < -CHARGE_SCORE_MAX) {
                s_charge_trend_score = -CHARGE_SCORE_MAX;
            }

            if (s_charge_trend_score >= CHARGE_SCORE_SET_THRESHOLD) {
                s_is_charging = true;
            } else if (s_charge_trend_score <= CHARGE_SCORE_CLEAR_THRESHOLD) {
                s_is_charging = false;
            }

            // Fast clear on unplug-like voltage drop so the lightning icon
            // doesn't linger for many polling cycles.
            if (s_is_charging && delta_mv <= CHARGE_UNPLUG_DROP_MV) {
                s_is_charging = false;
                s_charge_trend_score = CHARGE_SCORE_CLEAR_THRESHOLD;
            }

            if (s_filtered_battery_voltage_mv >= BATTERY_FULL_CONFIRM_MV) {
                if (s_full_confirm_count < BATTERY_FULL_CONFIRM_SAMPLES) {
                    s_full_confirm_count++;
                }
            } else {
                s_full_confirm_count = 0;
            }

            percent = voltage_to_percentage(s_filtered_battery_voltage_mv);
            if (percent >= 100U && s_full_confirm_count < BATTERY_FULL_CONFIRM_SAMPLES) {
                // Prevent an immediate 100% jump from charger-in voltage boost.
                percent = 99U;
            }

            s_last_battery_voltage_mv = battery_mv;
            s_display_percent = percent;
            ESP_LOGD(
                TAG,
                "raw=%d adc_pin=%.0fmV inst=%.0fmV filt=%.0fmV d=%.1fmV pct=%u chg=%d score=%d full_hold=%lu/%u",
                raw,
                (double)adc_pin_mv,
                (double)battery_mv,
                (double)s_filtered_battery_voltage_mv,
                (double)delta_mv,
                (unsigned)s_display_percent,
                (int)s_is_charging,
                s_charge_trend_score,
                (unsigned long)s_full_confirm_count,
                (unsigned)BATTERY_FULL_CONFIRM_SAMPLES
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
