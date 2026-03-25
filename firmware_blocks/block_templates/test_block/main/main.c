#include <stdio.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_err.h"
#include "driver/gpio.h"

#include "battery_monitor.h"
#include "speaker.h"
#include "led_dual_ws2812.h"
#include "tft_test_ui.h"

static const char *TAG = "TEST_BLOCK";
#define STARTUP_GUARD_SETTLE_MS 120
static void startup_power_guard(void)
{
    static const gpio_num_t k_quiet_pins[] = { GPIO_NUM_13, GPIO_NUM_15, GPIO_NUM_18 };
    gpio_config_t io_cfg = {
        .pin_bit_mask = 0,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    io_cfg.pin_bit_mask = (1ULL << GPIO_NUM_5);
    (void)gpio_config(&io_cfg);
    (void)gpio_set_level(GPIO_NUM_5, 1);

    for (size_t i = 0; i < (sizeof(k_quiet_pins) / sizeof(k_quiet_pins[0])); ++i) {
        io_cfg.pin_bit_mask = (1ULL << k_quiet_pins[i]);
        (void)gpio_config(&io_cfg);
        (void)gpio_set_level(k_quiet_pins[i], 0);
    }

    vTaskDelay(pdMS_TO_TICKS(STARTUP_GUARD_SETTLE_MS));
}

void app_main(void)
{
    startup_power_guard();

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "    Peripheral Test Block BOOT");
    ESP_LOGI(TAG, "========================================");

    /* Speaker PWM startup noise (required). */
    esp_err_t speaker_init_err = speaker_init();
    bool speaker_ok = (speaker_init_err == ESP_OK);
    if (speaker_ok) {
        ESP_LOGI(TAG, "Speaker init OK, playing PWM boot do-do-do");
        speaker_play_boot_sound();
    } else {
        ESP_LOGE(TAG, "Speaker init failed: %s", esp_err_to_name(speaker_init_err));
    }

    /* LEDs (matrix red + ledstrip blue). */
    esp_err_t led_init_err = led_dual_ws2812_init();
    if (led_init_err != ESP_OK) {
        ESP_LOGE(TAG, "LED init failed: %s", esp_err_to_name(led_init_err));
    }

    esp_err_t bat_err = battery_monitor_start();
    if (bat_err != ESP_OK) {
        ESP_LOGE(TAG, "Battery monitor start failed: %s", esp_err_to_name(bat_err));
    }

    /* TFT + touch (required). */
    esp_err_t tft_init_err = tft_test_ui_start();
    if (tft_init_err != ESP_OK) {
        ESP_LOGE(TAG, "TFT UI init failed: %s", esp_err_to_name(tft_init_err));
    }

    tft_test_ui_set_line(TFT_TEST_LINE_SPEAKER,
                          speaker_ok ? "Speaker: boot sound" : "Speaker: ERROR");

    tft_test_ui_set_line(TFT_TEST_LINE_MATRIX, "Matrix: RED (loop)");
    tft_test_ui_set_line(TFT_TEST_LINE_LEDSTRIP, "Ledstrip: BLUE (loop)");
    tft_test_ui_set_line(TFT_TEST_LINE_TOUCH, "Touch: tap to confirm");

    ESP_LOGI(TAG, "Peripheral test running in LED loop...");

    /* Infinite bring-up loop:
     * - Flash matrix red
     * - Flash ledstrip blue
     * This helps visually validate wiring on new boards.
     */
    while (1) {
        led_dual_ws2812_flash_matrix_red(600);
        tft_test_ui_set_line(TFT_TEST_LINE_MATRIX, "Matrix: RED OK");

        vTaskDelay(pdMS_TO_TICKS(150));

        led_dual_ws2812_flash_ledstrip_blue(600);
        tft_test_ui_set_line(TFT_TEST_LINE_LEDSTRIP, "Ledstrip: BLUE OK");

        vTaskDelay(pdMS_TO_TICKS(1000));

        tft_test_ui_set_line(TFT_TEST_LINE_MATRIX, "Matrix: RED (loop)");
        tft_test_ui_set_line(TFT_TEST_LINE_LEDSTRIP, "Ledstrip: BLUE (loop)");
    }
}

