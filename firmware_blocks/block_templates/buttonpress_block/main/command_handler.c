// Legacy command handler retained for compatibility with older templates.
// The button block uses `command_handle()` in `main.c` for CMD_GET_DATA semantics.

#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "CMD_HANDLER";

void handle_command(uint8_t *buffer, int len)
{
    (void)buffer;
    (void)len;
    ESP_LOGW(TAG, "handle_command() is deprecated for this block");
}

void led_status_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "LED status task started (button block)");
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
