#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

/*
 * Battery monitor assumptions:
 * - 1-cell Li-ion battery (about 7.0V empty to 8.4V full)
 * - Battery is connected to ADC through a resistor divider
 * - Example divider: 2k (top) / 1k (bottom) -> divide by 2
 */
#define BATTERY_VOLTAGE_EMPTY_MV 7000.0f
#define BATTERY_VOLTAGE_FULL_MV 8400.0f

#define ADC_UNIT_USED          ADC_UNIT_1
#define ADC_CHANNEL_BATTERY    ADC_CHANNEL_6   // GPIO34 on many ESP32 boards
#define ADC_ATTEN_USED         ADC_ATTEN_DB_12
#define ADC_BITWIDTH_USED      ADC_BITWIDTH_DEFAULT

#define DIVIDER_R_TOP_OHMS     2000.0f
#define DIVIDER_R_BOTTOM_OHMS  1000.0f
 
static adc_oneshot_unit_handle_t adc_handle;
static adc_cali_handle_t adc_cali_handle = NULL;

static float clampf(float value, float min, float max)
{
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

static int voltage_to_percentage(float battery_mv)
{
    float percent = ((battery_mv - BATTERY_VOLTAGE_EMPTY_MV) * 100.0f) /
                    (BATTERY_VOLTAGE_FULL_MV - BATTERY_VOLTAGE_EMPTY_MV);
    percent = clampf(percent, 0.0f, 100.0f);
    return (int)(percent + 0.5f); // round to nearest int
}

static float read_battery_voltage_mv(void)
{
    int raw = 0;
    adc_oneshot_read(adc_handle, ADC_CHANNEL_BATTERY, &raw);

    int adc_pin_mv = 0;

    if (adc_cali_handle) {
        if (adc_cali_raw_to_voltage(adc_cali_handle, raw, &adc_pin_mv) != ESP_OK) {
            return -1.0f;
        }
    } else {
        const float adc_reference_mv = 3000.0f;
        const int adc_max_count = 4095; // 12-bit
        adc_pin_mv = (int)(((float)raw / (float)adc_max_count) * adc_reference_mv);
    }

    // Undo resistor divider: Vbat = Vadc * (Rtop + Rbottom) / Rbottom
    float battery_mv = adc_pin_mv *
                       ((DIVIDER_R_TOP_OHMS + DIVIDER_R_BOTTOM_OHMS) / DIVIDER_R_BOTTOM_OHMS);

    return battery_mv;
}

void app_main(void)
{
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT_USED,
    };
    adc_oneshot_new_unit(&init_config, &adc_handle);

    adc_oneshot_chan_cfg_t chan_config = {
        .bitwidth = ADC_BITWIDTH_USED,
        .atten = ADC_ATTEN_USED,
    };
    adc_oneshot_config_channel(adc_handle, ADC_CHANNEL_BATTERY, &chan_config);

    adc_cali_line_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT_USED,
        .atten = ADC_ATTEN_USED,
        .bitwidth = ADC_BITWIDTH_USED,
    };
    if (adc_cali_create_scheme_line_fitting(&cali_config, &adc_cali_handle) != ESP_OK) {
        adc_cali_handle = NULL;
    }

    while (1) {
        float battery_mv = read_battery_voltage_mv();
        int battery_percent = voltage_to_percentage(battery_mv);

        printf("Battery: %.0f mV (%d%%)\n", battery_mv, battery_percent);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
