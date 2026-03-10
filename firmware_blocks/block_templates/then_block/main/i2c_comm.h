#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t i2c_slave_init(void);
void      i2c_task(void *arg);

#ifdef __cplusplus
}
#endif
