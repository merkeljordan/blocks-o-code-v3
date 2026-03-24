#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint8_t command_handler_get_status_flags(void);
void led_status_task(void *arg);

#ifdef __cplusplus
}
#endif
