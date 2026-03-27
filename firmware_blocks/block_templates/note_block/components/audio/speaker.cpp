// Audio component for blocks: boot sound + beeps + WAV playback.
// Uses atomic14's DACOutput (event-driven I2S DAC on GPIO25).

#include "audio_speaker.h"
#include "WAVFileReader.h"
#include "SinWaveGenerator.h"
#include "DACOutput.h"

#include <Arduino.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern const uint8_t bootupsound_wav_start[] asm("_binary_bootupsound_wav_start");
extern const uint8_t bootupsound_wav_end[]   asm("_binary_bootupsound_wav_end");

extern "C" {

static const char *TAG = "AUDIO";
#define SPEAKER_AMP_ENABLE_GPIO 5
#define SPEAKER_AMP_ENABLE_ACTIVE_HIGH 0
static bool s_inited = false;
static DACOutput *s_dac = NULL;

static void speaker_amp_set_enabled(bool on) {
    int level_on = SPEAKER_AMP_ENABLE_ACTIVE_HIGH ? 1 : 0;
    int level = on ? level_on : (1 - level_on);
    gpio_set_level((gpio_num_t)SPEAKER_AMP_ENABLE_GPIO, level);
}

class SilenceSource : public SampleSource {
public:
    int sampleRate() override { return 44100; }
    void getFrames(Frame_t *frames, int number_frames) override {
        for (int i = 0; i < number_frames; i++) {
            frames[i].left = 32768;
            frames[i].right = 32768;
        }
    }
};

static SilenceSource s_silence;

static void delay_ms(uint32_t ms) {
    if (ms) vTaskDelay(pdMS_TO_TICKS(ms));
}

esp_err_t speaker_init(void) {
    if (s_inited) return ESP_OK;

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
    if (!s_dac) return ESP_ERR_NO_MEM;

    s_dac->start(&s_silence);
    s_inited = true;
    ESP_LOGI(TAG, "Speaker ready (I2S DAC on GPIO25)");
    return ESP_OK;
}

void speaker_deinit(void) {
    speaker_amp_set_enabled(false);
    s_inited = false;
}

void speaker_set_volume(uint8_t pct) { (void)pct; }
uint8_t speaker_get_volume(void) { return 100; }
esp_err_t speaker_stop(void) {
    if (s_dac) s_dac->setSampleSource(&s_silence);
    return ESP_OK;
}

esp_err_t speaker_play_boot_sound(void) {
    if (!s_inited || !s_dac) return ESP_ERR_INVALID_STATE;

    ESP_LOGI(TAG, "Playing boot PWM tone sequence");
    (void)speaker_play_tone(440, 120);
    delay_ms(40);
    (void)speaker_play_tone(660, 120);
    delay_ms(40);
    (void)speaker_play_tone(880, 160);
    ESP_LOGI(TAG, "Boot tone sequence finished");
    return ESP_OK;
}

esp_err_t speaker_play_wav(const uint8_t *data, size_t len) {
    if (!s_inited || !s_dac || !data || len == 0) return ESP_ERR_INVALID_ARG;

    WAVFileReader reader(data, data + len);

    int data_bytes = reader.getDataBytes();
    int bytes_per_sec = reader.sampleRate() * 4;
    uint32_t duration_ms = (uint32_t)((uint64_t)data_bytes * 1000 / (bytes_per_sec ? bytes_per_sec : 1));
    duration_ms += 300;

    s_dac->setSampleSource(&reader);
    delay_ms(duration_ms);
    s_dac->setSampleSource(&s_silence);
    delay_ms(50);
    return ESP_OK;
}

esp_err_t speaker_play_tone(uint32_t hz, uint32_t ms) {
    if (!s_inited || !s_dac) return ESP_ERR_INVALID_STATE;
    if (hz == 0 || ms == 0) return ESP_OK;

    SinWaveGenerator tone(44100, hz, 0.75);
    s_dac->setSampleSource(&tone);
    delay_ms(ms);
    s_dac->setSampleSource(&s_silence);
    delay_ms(50);
    return ESP_OK;
}

void speaker_beep_ok(void) {
    speaker_play_tone(1200, 80);
    delay_ms(40);
    speaker_play_tone(1600, 80);
}

void speaker_beep_error(void) {
    speaker_play_tone(220, 200);
}

} // extern "C"
