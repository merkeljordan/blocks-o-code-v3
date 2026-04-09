#pragma once
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_TIMEOUT -2
#define ESP_ERR_INVALID_STATE -3
#define ESP_ERR_INVALID_ARG -4
#define ESP_ERR_NOT_FOUND -5
typedef int esp_err_t;
const char* esp_err_to_name(esp_err_t code);
