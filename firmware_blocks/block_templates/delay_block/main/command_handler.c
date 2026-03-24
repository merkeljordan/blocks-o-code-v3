#include <stdint.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "i2c_protocol.h"
#include "led_matrix.h"

static const char *TAG = "CMD_HANDLER";

extern uint8_t delay_block_get_status_flags(void);

uint8_t command_handler_get_status_flags(void)
{
    return delay_block_get_status_flags();
}

void led_status_task(void *arg) {
    ESP_LOGI(TAG, "LED status task started");

    while (1) {
        uint8_t status = delay_block_get_status_flags();
        ESP_LOGI(TAG, "Status: %s | Brightness: %d%%",
                 (status & STATUS_BUSY) ? "BUSY" : "READY",
                 (matrix_get_brightness() * 100) / 255);

        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
