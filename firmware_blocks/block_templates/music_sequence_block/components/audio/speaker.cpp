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
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern "C" {

static const char *TAG = "AUDIO";

// Match the speaker amplifier-enable wiring used by the test block.
#define SPEAKER_AMP_ENABLE_GPIO 5
#define SPEAKER_AMP_ENABLE_ACTIVE_HIGH 0

// Default I2S sample rate used for silence and generated tones.
// Music WAV assets are encoded at 11025 Hz and the I2S rate is updated
// dynamically in DACOutput::setSampleSource() to match each WAV file's
// embedded sample rate.
#define SPEAKER_DEFAULT_SAMPLE_RATE_HZ 44100

// Set true after successful speaker_init().
static bool s_inited = false;

// Global DAC bridge used by all playback APIs in this file.
static DACOutput *s_dac = NULL;

// User-facing volume control (0..100%).
static uint8_t s_volume_percent = 30;

static void speaker_amp_set_enabled(bool on)
{
    int level_on = SPEAKER_AMP_ENABLE_ACTIVE_HIGH ? 1 : 0;
    int level = on ? level_on : (1 - level_on);
    gpio_set_level((gpio_num_t)SPEAKER_AMP_ENABLE_GPIO, level);
}

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
    int sampleRate() override { return SPEAKER_DEFAULT_SAMPLE_RATE_HZ; }

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

// --------------------------------------------------------------------------
// delay_ms
// --------------------------------------------------------------------------
// Internal helper used by blocking playback APIs.
// Called by: speaker_play_* functions, beep helpers.
static void delay_ms(uint32_t ms)
{
    if (ms) {
        vTaskDelay(pdMS_TO_TICKS(ms));
    }
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

    s_dac = new DACOutput();
    if (!s_dac) {
        return ESP_ERR_NO_MEM;
    }

    // Start DAC writer against silence first to avoid startup pops/noise.
    s_dac->start(&s_silence);
    s_inited = true;
    ESP_LOGI(TAG, "Speaker ready (I2S DAC on GPIO25)");
    return ESP_OK;
}

// --------------------------------------------------------------------------
// speaker_deinit
// --------------------------------------------------------------------------
// Called by: optional shutdown paths (not heavily used right now)
void speaker_deinit(void)
{
    // Lightweight deinit for now (task teardown not implemented yet).
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
// Calls: WAVFileReader + DACOutput::setSampleSource + delay_ms.
esp_err_t speaker_play_boot_sound(void)
{
    if (!s_inited || !s_dac) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Playing boot PWM tone sequence");
    (void)speaker_play_tone(440, 100);
    delay_ms(40);
    (void)speaker_play_tone(660, 100);
    delay_ms(40);
    (void)speaker_play_tone(880, 130);
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
    if (!s_inited || !s_dac || !data || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    WAVFileReader reader(data, data + len);
    reader.setGain(volume_to_gain(s_volume_percent));

    int data_bytes = reader.getDataBytes();
    int channels = reader.getNumChannels();
    int bits_per_sample = reader.getBitsPerSample();
    int bytes_per_sample = bits_per_sample / 8;
    int bytes_per_sec = reader.sampleRate() * channels * bytes_per_sample;
    uint32_t duration_ms = (uint32_t)((uint64_t)data_bytes * 1000 /
                                      (bytes_per_sec ? bytes_per_sec : 1));
    duration_ms += 300;

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
    if (!s_inited || !s_dac) {
        return ESP_ERR_INVALID_STATE;
    }

    // Treat invalid/empty tone as no-op.
    if (hz == 0 || ms == 0) {
        return ESP_OK;
    }

    // Keep requested frequency, but lower magnitude to avoid clipping.
    float tone_magnitude = 0.1f * volume_to_gain(s_volume_percent);
    SinWaveGenerator tone(SPEAKER_DEFAULT_SAMPLE_RATE_HZ, (int)hz, tone_magnitude);

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
