#include "battery_monitor.h"

#define BATTERY_VOLTAGE_EMPTY_V 7.0f
#define BATTERY_VOLTAGE_FULL_V  8.4f
#define BATTERY_PERCENT_STUB    100U

static float s_last_battery_voltage_v = -1.0f;

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
