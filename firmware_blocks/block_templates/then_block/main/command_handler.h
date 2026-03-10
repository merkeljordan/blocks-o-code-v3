#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void handle_command(uint8_t *buffer, int len);
void led_status_task(void *arg);

#ifdef __cplusplus
}
#endif
