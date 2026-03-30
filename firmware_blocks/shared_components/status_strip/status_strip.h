#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "esp_err.h"
#include "i2c_protocol.h"

typedef struct {
    gpio_num_t gpio_num;
    uint16_t led_count;
} status_strip_config_t;

esp_err_t status_strip_ensure_ready(const status_strip_config_t *cfg);
void status_strip_fill(uint8_t r, uint8_t g, uint8_t b);
void status_strip_set_pixel(uint16_t idx, uint8_t r, uint8_t g, uint8_t b);
void status_strip_clear(void);
void status_strip_set_brightness(uint8_t brightness);
uint8_t status_strip_get_brightness(void);
uint16_t status_strip_get_led_count(void);
esp_err_t status_strip_show(void);
esp_err_t status_strip_reset(const status_strip_config_t *cfg);
bool status_strip_handle_matrix_command(const char *tag,
                                        const status_strip_config_t *cfg,
                                        i2c_command_t cmd,
                                        const uint8_t *payload,
                                        size_t payload_len);
