#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void battery_monitor_update_voltage(float volts);
uint8_t battery_monitor_get_percent(void);

#ifdef __cplusplus
}
#endif
