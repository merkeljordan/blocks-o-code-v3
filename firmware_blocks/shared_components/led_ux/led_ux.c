#include "led_ux.h"
#include "led_matrix.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void led_ux_show_startup(void)
{
    led_matrix_startup_animation();
}

void led_ux_show_running(void)
{
    matrix_fill(0, 64, 0);
    matrix_show();
    vTaskDelay(pdMS_TO_TICKS(120));
    matrix_clear();
    matrix_show();
}

void led_ux_show_ok(void)
{
    matrix_fill(0, 255, 0);
    matrix_show();
    vTaskDelay(pdMS_TO_TICKS(80));
    matrix_clear();
    matrix_show();
}

void led_ux_show_error(void)
{
    matrix_fill(255, 0, 0);
    matrix_show();
    vTaskDelay(pdMS_TO_TICKS(120));
    matrix_clear();
    matrix_show();
}

