#include "speaker.h"

#include "driver/ledc.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "SPEAKER";

#define SPEAKER_GPIO            25
#define AMP_ENABLE_GPIO         33   /* Active-high: HIGH = amp on, LOW = amp off */

#define SPEAKER_LEDC_MODE       LEDC_HIGH_SPEED_MODE
#define SPEAKER_LEDC_TIMER      LEDC_TIMER_0
#define SPEAKER_LEDC_CHANNEL    LEDC_CHANNEL_0
#define SPEAKER_LEDC_DUTY_RES   LEDC_TIMER_10_BIT

#define SPEAKER_DEFAULT_VOLUME  100
#define SPEAKER_FADE_STEPS      4
#define SPEAKER_FADE_STEP_MS    3

static bool s_inited = false;
static uint8_t s_volume = SPEAKER_DEFAULT_VOLUME;

static void amp_enable(void) {
    gpio_set_level(AMP_ENABLE_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(5));
}

static void amp_disable(void) {
    gpio_set_level(AMP_ENABLE_GPIO, 0);
}

static uint32_t speaker_get_duty(uint8_t volume_percent) {
    if (volume_percent > 100) {
        volume_percent = 100;
    }
    uint32_t max_duty = (1 << SPEAKER_LEDC_DUTY_RES) - 1;
    // 50% duty at 100% volume, scale down from there.
    return (max_duty * volume_percent) / 200;
}

esp_err_t speaker_init(void) {
    gpio_reset_pin(AMP_ENABLE_GPIO);
    gpio_set_direction(AMP_ENABLE_GPIO, GPIO_MODE_OUTPUT);
    amp_disable();

    ledc_timer_config_t timer_config = {
        .speed_mode = SPEAKER_LEDC_MODE,
        .timer_num = SPEAKER_LEDC_TIMER,
        .duty_resolution = SPEAKER_LEDC_DUTY_RES,
        .freq_hz = 1000,
        .clk_cfg = LEDC_AUTO_CLK,
    };

    esp_err_t err = ledc_timer_config(&timer_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LEDC timer init failed: %s", esp_err_to_name(err));
        return err;
    }

    ledc_channel_config_t channel_config = {
        .gpio_num = SPEAKER_GPIO,
        .speed_mode = SPEAKER_LEDC_MODE,
        .channel = SPEAKER_LEDC_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = SPEAKER_LEDC_TIMER,
        .duty = 0,
        .hpoint = 0,
    };

    err = ledc_channel_config(&channel_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LEDC channel init failed: %s", esp_err_to_name(err));
        return err;
    }

    s_inited = true;
    ESP_LOGI(TAG, "Speaker initialized on GPIO%d, amp enable on GPIO%d",
             SPEAKER_GPIO, AMP_ENABLE_GPIO);
    return ESP_OK;
}

void speaker_deinit(void) {
    if (!s_inited) {
        return;
    }
    ledc_stop(SPEAKER_LEDC_MODE, SPEAKER_LEDC_CHANNEL, 0);
    amp_disable();
    s_inited = false;
}

void speaker_set_volume(uint8_t volume_percent) {
    if (volume_percent > 100) {
        volume_percent = 100;
    }
    s_volume = volume_percent;
}

esp_err_t speaker_play_tone(uint32_t freq_hz, uint32_t duration_ms) {
    if (!s_inited) {
        return ESP_ERR_INVALID_STATE;
    }
    if (freq_hz == 0 || duration_ms == 0) {
        return ESP_OK;
    }

    esp_err_t err = ledc_set_freq(SPEAKER_LEDC_MODE, SPEAKER_LEDC_TIMER, freq_hz);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LEDC set freq failed: %s", esp_err_to_name(err));
        return err;
    }

    amp_enable();

    uint32_t target_duty = speaker_get_duty(s_volume);
    uint32_t fade_total_ms = SPEAKER_FADE_STEPS * SPEAKER_FADE_STEP_MS * 2;

    if (duration_ms <= fade_total_ms) {
        ledc_set_duty(SPEAKER_LEDC_MODE, SPEAKER_LEDC_CHANNEL, target_duty);
        ledc_update_duty(SPEAKER_LEDC_MODE, SPEAKER_LEDC_CHANNEL);
        vTaskDelay(pdMS_TO_TICKS(duration_ms));
        ledc_set_duty(SPEAKER_LEDC_MODE, SPEAKER_LEDC_CHANNEL, 0);
        ledc_update_duty(SPEAKER_LEDC_MODE, SPEAKER_LEDC_CHANNEL);
        amp_disable();
        return ESP_OK;
    }

    for (uint32_t i = 1; i <= SPEAKER_FADE_STEPS; i++) {
        uint32_t step_duty = (target_duty * i) / SPEAKER_FADE_STEPS;
        ledc_set_duty(SPEAKER_LEDC_MODE, SPEAKER_LEDC_CHANNEL, step_duty);
        ledc_update_duty(SPEAKER_LEDC_MODE, SPEAKER_LEDC_CHANNEL);
        vTaskDelay(pdMS_TO_TICKS(SPEAKER_FADE_STEP_MS));
    }

    vTaskDelay(pdMS_TO_TICKS(duration_ms - fade_total_ms));

    for (int i = SPEAKER_FADE_STEPS - 1; i >= 0; i--) {
        uint32_t step_duty = (target_duty * (uint32_t)i) / SPEAKER_FADE_STEPS;
        ledc_set_duty(SPEAKER_LEDC_MODE, SPEAKER_LEDC_CHANNEL, step_duty);
        ledc_update_duty(SPEAKER_LEDC_MODE, SPEAKER_LEDC_CHANNEL);
        vTaskDelay(pdMS_TO_TICKS(SPEAKER_FADE_STEP_MS));
    }

    amp_disable();
    return ESP_OK;
}

void speaker_beep_ok(void) {
    speaker_play_tone(1200, 80);
    vTaskDelay(pdMS_TO_TICKS(40));
    speaker_play_tone(1600, 80);
}

void speaker_beep_error(void) {
    speaker_play_tone(220, 200);
}
