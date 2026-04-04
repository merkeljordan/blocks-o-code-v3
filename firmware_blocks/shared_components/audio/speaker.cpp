// ============================================================================
// speaker.cpp
// ============================================================================
// High-level audio wrapper for this block.
//
// Why this file exists:
// - DACOutput/SampleSource are C++ internals.
// - Most firmware code in this project is C.
// - This file provides a clean C API bridge (`audio_speaker.h`).
//
// Call flow (typical):
// 1) app_main/main code calls speaker_init()
// 2) UI/execution code calls speaker_play_wav() or speaker_play_tone()
// 3) This file swaps active SampleSource on DACOutput
// 4) DACOutput writer task continuously pulls frames from that source
//
// Note:
// - This implementation is intentionally synchronous/blocking at API level.
// - Background streaming still happens in DACOutput's writer task.

#include "audio_speaker.h"
#include "DACOutput.h"
#include "SinWaveGenerator.h"
#include "WAVFileReader.h"

#include <Arduino.h>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern "C" {

static const char *TAG = "AUDIO";
#define SPEAKER_AMP_ENABLE_GPIO 5
#define SPEAKER_AMP_ENABLE_ACTIVE_HIGH 0
#define SPEAKER_PWM_GPIO 25
#define SPEAKER_BOOT_LEDC_MODE LEDC_LOW_SPEED_MODE
#define SPEAKER_BOOT_LEDC_TIMER LEDC_TIMER_0
#define SPEAKER_BOOT_LEDC_CHANNEL LEDC_CHANNEL_0
#define SPEAKER_BOOT_LEDC_DUTY_RES LEDC_TIMER_10_BIT
#define SPEAKER_BOOT_VOLUME_PERCENT 30

static void speaker_amp_set_enabled(bool on) {
    int level_on = SPEAKER_AMP_ENABLE_ACTIVE_HIGH ? 1 : 0;
    int level = on ? level_on : (1 - level_on);
    gpio_set_level((gpio_num_t)SPEAKER_AMP_ENABLE_GPIO, level);
}

// Set true after successful speaker_init().
static bool s_inited = false;

// Global DAC bridge used by all playback APIs in this file.
static DACOutput *s_dac = NULL;

// User-facing volume control (0..100%). Default matches Brain / music blocks.
static uint8_t s_volume_percent = 30;

// Map UI percent to linear gain scalar.
static float volume_to_gain(uint8_t pct)
{
    return ((float)pct) / 100.0f;
}

// --------------------------------------------------------------------------
// SilenceSource
// --------------------------------------------------------------------------
// This source outputs constant center-biased values.
// It is used when idle/stopped so DACOutput always has safe data to stream.
class SilenceSource : public SampleSource {
public:
    // Called by DACOutput::start() and other sample-rate consumers.
    int sampleRate() override { return 44100; }

    // Called repeatedly by DACOutput writer task.
    void getFrames(Frame_t *frames, int number_frames) override {
        // Fill every frame with "silence" values.
        for (int i = 0; i < number_frames; i++) {
            frames[i].left = 32768;
            frames[i].right = 32768;
        }
    }
};

static SilenceSource s_silence;

static void delay_ms(uint32_t ms)
{
    if (ms) {
        vTaskDelay(pdMS_TO_TICKS(ms));
    }
}

static uint32_t boot_pwm_get_duty(uint8_t volume_percent)
{
    if (volume_percent > 100U) {
        volume_percent = 100U;
    }

    uint32_t max_duty = (1U << SPEAKER_BOOT_LEDC_DUTY_RES) - 1U;
    return (max_duty * volume_percent) / 200U;
}

static esp_err_t ensure_dac_ready(void)
{
    if (s_dac != NULL) {
        return ESP_OK;
    }

    s_dac = new DACOutput();
    if (s_dac == NULL) {
        return ESP_ERR_NO_MEM;
    }

    s_dac->start(&s_silence);
    return ESP_OK;
}

static esp_err_t boot_pwm_play_tone(uint32_t hz, uint32_t ms)
{
    if (hz == 0U || ms == 0U) {
        return ESP_OK;
    }

    ledc_timer_config_t timer_config = {
        .speed_mode = SPEAKER_BOOT_LEDC_MODE,
        .duty_resolution = SPEAKER_BOOT_LEDC_DUTY_RES,
        .timer_num = SPEAKER_BOOT_LEDC_TIMER,
        .freq_hz = 1000,
        .clk_cfg = LEDC_AUTO_CLK,
        .deconfigure = false,
    };
    esp_err_t err = ledc_timer_config(&timer_config);
    if (err != ESP_OK) {
        return err;
    }

    ledc_channel_config_t channel_config = {
        .gpio_num = SPEAKER_PWM_GPIO,
        .speed_mode = SPEAKER_BOOT_LEDC_MODE,
        .channel = SPEAKER_BOOT_LEDC_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = SPEAKER_BOOT_LEDC_TIMER,
        .duty = 0,
        .hpoint = 0,
        .sleep_mode = LEDC_SLEEP_MODE_NO_ALIVE_NO_PD,
        .flags = {.output_invert = 0},
    };
    err = ledc_channel_config(&channel_config);
    if (err != ESP_OK) {
        return err;
    }

    err = ledc_set_freq(SPEAKER_BOOT_LEDC_MODE, SPEAKER_BOOT_LEDC_TIMER, hz);
    if (err != ESP_OK) {
        return err;
    }

    uint32_t duty = boot_pwm_get_duty(SPEAKER_BOOT_VOLUME_PERCENT);
    ledc_set_duty(SPEAKER_BOOT_LEDC_MODE, SPEAKER_BOOT_LEDC_CHANNEL, duty);
    ledc_update_duty(SPEAKER_BOOT_LEDC_MODE, SPEAKER_BOOT_LEDC_CHANNEL);
    delay_ms(ms);
    ledc_set_duty(SPEAKER_BOOT_LEDC_MODE, SPEAKER_BOOT_LEDC_CHANNEL, 0);
    ledc_update_duty(SPEAKER_BOOT_LEDC_MODE, SPEAKER_BOOT_LEDC_CHANNEL);
    return ESP_OK;
}

// --------------------------------------------------------------------------
// speaker_init
// --------------------------------------------------------------------------
// Called by: block startup (`main/main.c`)
// Calls: DACOutput::start
esp_err_t speaker_init(void)
{
    // If already initialized, keep behavior idempotent.
    if (s_inited) {
        return ESP_OK;
    }

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

    s_inited = true;
    ESP_LOGI(TAG, "Speaker amp ready on GPIO%d", SPEAKER_PWM_GPIO);
    return ESP_OK;
}

// --------------------------------------------------------------------------
// speaker_deinit
// --------------------------------------------------------------------------
// Called by: optional shutdown paths (not heavily used right now)
void speaker_deinit(void)
{
    speaker_amp_set_enabled(false);
    s_inited = false;
}

// --------------------------------------------------------------------------
// speaker_set_volume / speaker_get_volume
// --------------------------------------------------------------------------
// Called by: app code that expects a volume API.
void speaker_set_volume(uint8_t pct)
{
    // Clamp to documented range so callers can pass raw values safely.
    s_volume_percent = (pct > 100U) ? 100U : pct;
}

uint8_t speaker_get_volume(void)
{
    return s_volume_percent;
}

// --------------------------------------------------------------------------
// speaker_stop
// --------------------------------------------------------------------------
// Called by: code that wants immediate silence.
// Calls: DACOutput::setSampleSource
esp_err_t speaker_stop(void)
{
    if (s_dac) {
        s_dac->setSampleSource(&s_silence);
    }
    return ESP_OK;
}

// --------------------------------------------------------------------------
// speaker_play_boot_sound
// --------------------------------------------------------------------------
// Called by: startup flow in main.
// Calls: speaker_play_tone + delay_ms
esp_err_t speaker_play_boot_sound(void)
{
    if (!s_inited) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_dac != NULL) {
        s_dac->setSampleSource(&s_silence);
    }

    ESP_LOGI(TAG, "Playing Note-style boot PWM sequence");
    (void)boot_pwm_play_tone(440, 100);
    delay_ms(40);
    (void)boot_pwm_play_tone(660, 100);
    delay_ms(40);
    (void)boot_pwm_play_tone(880, 130);

    if (s_dac != NULL) {
        s_dac->setSampleSource(&s_silence);
    }

    ESP_LOGI(TAG, "Boot tone sequence finished");
    return ESP_OK;
}

// --------------------------------------------------------------------------
// speaker_play_wav
// --------------------------------------------------------------------------
// Called by: speaker_music.c (song playback), optional direct callers.
// Calls: WAVFileReader + DACOutput::setSampleSource + delay_ms.
esp_err_t speaker_play_wav(const uint8_t *data, size_t len)
{
    // Validate arguments and init state.
    if (!s_inited || !data || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ensure_dac_ready();
    if (err != ESP_OK) {
        return err;
    }

    WAVFileReader reader(data, data + len);
    reader.setGain(volume_to_gain(s_volume_percent));

    int data_bytes = reader.getDataBytes();
    int bytes_per_sec = reader.bytesPerSecond();
    uint32_t duration_ms = (uint32_t)((uint64_t)data_bytes * 1000 /
                                      (bytes_per_sec ? bytes_per_sec : 1));
    /* Short tail so UI/speaker isn't "busy" long after PCM has finished. */
    duration_ms += 80;

    s_dac->setSampleSource(&reader);
    delay_ms(duration_ms);
    s_dac->setSampleSource(&s_silence);
    delay_ms(50);
    return ESP_OK;
}

// --------------------------------------------------------------------------
// speaker_play_tone
// --------------------------------------------------------------------------
// Called by: beep helpers, speaker_music.c note APIs.
// Calls: SinWaveGenerator + DACOutput::setSampleSource + delay_ms.
esp_err_t speaker_play_tone(uint32_t hz, uint32_t ms)
{
    if (!s_inited) {
        return ESP_ERR_INVALID_STATE;
    }

    // Treat invalid/empty tone as no-op.
    if (hz == 0 || ms == 0) {
        return ESP_OK;
    }

    esp_err_t err = ensure_dac_ready();
    if (err != ESP_OK) {
        return err;
    }

    // Keep requested frequency, but lower magnitude to avoid clipping.
    float tone_magnitude = 0.1f * volume_to_gain(s_volume_percent);
    SinWaveGenerator tone(44100, hz, tone_magnitude);

    s_dac->setSampleSource(&tone);
    delay_ms(ms);
    s_dac->setSampleSource(&s_silence);
    delay_ms(50);
    return ESP_OK;
}

// --------------------------------------------------------------------------
// speaker_beep_ok
// --------------------------------------------------------------------------
// Called by: success feedback paths.
// Calls: speaker_play_tone twice.
void speaker_beep_ok(void)
{
    // Upward two-note chirp.
    speaker_play_tone(1200, 80);
    delay_ms(40);
    speaker_play_tone(1600, 80);
}

// --------------------------------------------------------------------------
// speaker_beep_error
// --------------------------------------------------------------------------
// Called by: error feedback paths.
void speaker_beep_error(void)
{
    // Lower, longer tone to communicate error state.
    speaker_play_tone(220, 200);
}

} // extern "C"
