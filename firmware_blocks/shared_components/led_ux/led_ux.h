#pragma once

#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void led_ux_show_startup(void);
void led_ux_show_running(void);
void led_ux_show_ok(void);
void led_ux_show_error(void);

#ifdef __cplusplus
}
#endif

