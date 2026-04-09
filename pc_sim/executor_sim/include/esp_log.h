#pragma once
#include <stdio.h>

// Forward declaration for the tee logger in mock_esp_framework.c
void sim_log_write(const char *fmt, ...);

#define ESP_LOGI(tag, fmt, ...) sim_log_write("I (%s): " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) sim_log_write("W (%s): " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGE(tag, fmt, ...) sim_log_write("E (%s): " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGD(tag, fmt, ...) sim_log_write("D (%s): " fmt "\n", tag, ##__VA_ARGS__)
