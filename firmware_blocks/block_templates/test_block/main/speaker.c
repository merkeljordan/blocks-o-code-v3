#include "speaker.h"

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "SPEAKER";

// PWM audio into the amplifier (net ESP32_DAC / speaker drive).
#define SPEAKER_GPIO            25

// Amplifier enable / shutdown. Set SPEAKER_AMP_ENABLE_ACTIVE_HIGH to 0 if low = enabled.
#define SPEAKER_AMP_ENABLE_GPIO         5
#define SPEAKER_AMP_ENABLE_ACTIVE_HIGH  1

#define SPEAKER_LEDC_MODE       LEDC_LOW_SPEED_MODE
#define SPEAKER_LEDC_TIMER      LEDC_TIMER_0
#define SPEAKER_LEDC_CHANNEL    LEDC_CHANNEL_0
#define SPEAKER_LEDC_DUTY_RES   LEDC_TIMER_10_BIT

#define SPEAKER_DEFAULT_VOLUME  50  // percent

static bool s_inited = false;
static uint8_t s_volume = SPEAKER_DEFAULT_VOLUME;

static void speaker_amp_set_enabled(bool on)
{
    int level_on = SPEAKER_AMP_ENABLE_ACTIVE_HIGH ? 1 : 0;
    int level = on ? level_on : (1 - level_on);
    gpio_set_level(SPEAKER_AMP_ENABLE_GPIO, level);
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
    gpio_config_t amp_io = {
        .pin_bit_mask = (1ULL << SPEAKER_AMP_ENABLE_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&amp_io);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Amp enable GPIO config failed: %s", esp_err_to_name(err));
        return err;
    }
    speaker_amp_set_enabled(true);

    ledc_timer_config_t timer_config = {
        .speed_mode = SPEAKER_LEDC_MODE,
        .timer_num = SPEAKER_LEDC_TIMER,
        .duty_resolution = SPEAKER_LEDC_DUTY_RES,
        .freq_hz = 1000,
        .clk_cfg = LEDC_AUTO_CLK,
    };

    err = ledc_timer_config(&timer_config);
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
    ESP_LOGI(TAG, "Speaker PWM GPIO%d, amp enable GPIO%d", SPEAKER_GPIO,
             SPEAKER_AMP_ENABLE_GPIO);
    return ESP_OK;
}

void speaker_deinit(void) {
    if (!s_inited) {
        return;
    }
    ledc_stop(SPEAKER_LEDC_MODE, SPEAKER_LEDC_CHANNEL, 0);
    speaker_amp_set_enabled(false);
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

    uint32_t duty = speaker_get_duty(s_volume);
    ledc_set_duty(SPEAKER_LEDC_MODE, SPEAKER_LEDC_CHANNEL, duty);
    ledc_update_duty(SPEAKER_LEDC_MODE, SPEAKER_LEDC_CHANNEL);

    vTaskDelay(pdMS_TO_TICKS(duration_ms));

    ledc_set_duty(SPEAKER_LEDC_MODE, SPEAKER_LEDC_CHANNEL, 0);
    ledc_update_duty(SPEAKER_LEDC_MODE, SPEAKER_LEDC_CHANNEL);
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

void speaker_play_boot_sound(void)
{
    // "do do do" boot sequence on speaker PWM driver.
    // Keep it short so LED loop + TFT UI start quickly.
    (void)speaker_play_tone(440, 100);
    vTaskDelay(pdMS_TO_TICKS(40));
    (void)speaker_play_tone(660, 100);
    vTaskDelay(pdMS_TO_TICKS(40));
    (void)speaker_play_tone(880, 130);
    vTaskDelay(pdMS_TO_TICKS(40));
}

