#include "battery_monitor.h"

#include <stdbool.h>

#include "driver/adc.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define BATTERY_VOLTAGE_EMPTY_V 7.0f
#define BATTERY_VOLTAGE_FULL_V  8.4f
#define BATTERY_PERCENT_STUB    100U
#define BATTERY_MONITOR_GPIO_CHANNEL ADC1_CHANNEL_6
#define BATTERY_MONITOR_ATTEN       ADC_ATTEN_DB_12
#define BATTERY_MONITOR_MAX_RAW     4095.0f
/*
 * Battery sense is routed to GPIO34 through a divider. The pack itself is a
 * 2S source (7.0V empty to 8.4V full), so the ADC-side node needs to support
 * roughly half the pack voltage near full charge.
 */
#define BATTERY_MONITOR_ADC_FULL_V  4.2f
#define BATTERY_MONITOR_DIVIDER     3.0f
#define BATTERY_MONITOR_SAMPLES     16
#define BATTERY_MONITOR_PERIOD_MS   3000U
#define BATTERY_MONITOR_STACK_SIZE  2048
#define BATTERY_MONITOR_PRIORITY    2

static float s_last_battery_voltage_v = -1.0f;
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

void battery_monitor_update_voltage(float volts)
{
    s_last_battery_voltage_v = volts;
}

uint8_t battery_monitor_get_percent(void)
{
    float percent = 0.0f;

    if (s_last_battery_voltage_v <= 0.0f) {
        return BATTERY_PERCENT_STUB;
    }

    percent = ((s_last_battery_voltage_v - BATTERY_VOLTAGE_EMPTY_V) * 100.0f) /
              (BATTERY_VOLTAGE_FULL_V - BATTERY_VOLTAGE_EMPTY_V);
    percent = clampf(percent, 0.0f, 100.0f);
    return (uint8_t)(percent + 0.5f);
}

static float battery_monitor_read_voltage(void)
{
    uint32_t raw_total = 0;

    for (uint32_t i = 0; i < BATTERY_MONITOR_SAMPLES; i++) {
        int raw = adc1_get_raw(BATTERY_MONITOR_GPIO_CHANNEL);
        if (raw < 0) {
            return -1.0f;
        }
        raw_total += (uint32_t)raw;
    }

    {
        const float raw_average = (float)raw_total / (float)BATTERY_MONITOR_SAMPLES;
        const float adc_voltage = (raw_average / BATTERY_MONITOR_MAX_RAW) * BATTERY_MONITOR_ADC_FULL_V;
        return adc_voltage * BATTERY_MONITOR_DIVIDER;
    }
}

static void battery_monitor_task(void *arg)
{
    (void)arg;

    while (true) {
        float volts = battery_monitor_read_voltage();
        if (volts > 0.0f) {
            battery_monitor_update_voltage(volts);
            ESP_LOGD(TAG, "battery voltage=%.2fV percent=%u", (double)volts,
                     (unsigned)battery_monitor_get_percent());
        }

        vTaskDelay(pdMS_TO_TICKS(BATTERY_MONITOR_PERIOD_MS));
    }
}

esp_err_t battery_monitor_start(void)
{
    BaseType_t task_ok;

    if (s_monitor_started) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(adc1_config_width(ADC_WIDTH_BIT_12), TAG,
                        "adc1_config_width failed");
    ESP_RETURN_ON_ERROR(adc1_config_channel_atten(BATTERY_MONITOR_GPIO_CHANNEL,
                                                  BATTERY_MONITOR_ATTEN),
                        TAG, "adc1_config_channel_atten failed");

    task_ok = xTaskCreate(battery_monitor_task, "battery_monitor",
                          BATTERY_MONITOR_STACK_SIZE, NULL,
                          BATTERY_MONITOR_PRIORITY, NULL);
    if (task_ok != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    s_monitor_started = true;
    return ESP_OK;
}
