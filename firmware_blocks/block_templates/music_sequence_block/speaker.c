#include "speaker.h"

#include <math.h>
#include <stddef.h>
#include <stdbool.h>

#include "driver/i2s.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "SPEAKER";

// ESP32 internal DAC audio path (DAC1 = GPIO25).
// This is much cleaner than raw PWM square-wave output for melodies.
#define SPEAKER_DAC_GPIO                 25
#define SPEAKER_I2S_PORT                 I2S_NUM_0
#define SPEAKER_I2S_SAMPLE_RATE_HZ       16000
#define SPEAKER_I2S_DMA_BUF_COUNT        6
#define SPEAKER_I2S_DMA_BUF_LEN          256
#define SPEAKER_PCM_CHUNK_FRAMES         192
#define SPEAKER_DAC_CENTER               128U
#define SPEAKER_VOLUME_MAX_AMPLITUDE     56.0f
#define SPEAKER_ENVELOPE_MS              4U
#define SPEAKER_TWO_PI_F                 6.28318530718f

#define SPEAKER_DEFAULT_VOLUME  30  // percent
#define SPEAKER_DEFAULT_TEMPO_PCT 100
#define SPEAKER_NOTE_GAP_MS_DEFAULT 40U

#define NOTE_Q 250U  // quarter note base duration (ms)
#define NOTE_H 500U  // half note base duration (ms)
#define NOTE_E 125U  // eighth note base duration (ms)

#define STEP(note_, dur_)      { .note = (note_), .duration_ms = (dur_), .gap_ms = SPEAKER_NOTE_GAP_MS_DEFAULT }
#define STEP_G(note_, dur_, g_) { .note = (note_), .duration_ms = (dur_), .gap_ms = (g_) }

// Twinkle Twinkle Little Star (C major, simplified classroom tempo)
static const music_step_t s_twinkle_steps[] = {
    STEP(NOTE_C4, NOTE_Q), STEP(NOTE_C4, NOTE_Q), STEP(NOTE_G4, NOTE_Q), STEP(NOTE_G4, NOTE_Q),
    STEP(NOTE_A4, NOTE_Q), STEP(NOTE_A4, NOTE_Q), STEP(NOTE_G4, NOTE_H),

    STEP(NOTE_F4, NOTE_Q), STEP(NOTE_F4, NOTE_Q), STEP(NOTE_E4, NOTE_Q), STEP(NOTE_E4, NOTE_Q),
    STEP(NOTE_D4, NOTE_Q), STEP(NOTE_D4, NOTE_Q), STEP(NOTE_C4, NOTE_H),

    STEP(NOTE_G4, NOTE_Q), STEP(NOTE_G4, NOTE_Q), STEP(NOTE_F4, NOTE_Q), STEP(NOTE_F4, NOTE_Q),
    STEP(NOTE_E4, NOTE_Q), STEP(NOTE_E4, NOTE_Q), STEP(NOTE_D4, NOTE_H),

    STEP(NOTE_G4, NOTE_Q), STEP(NOTE_G4, NOTE_Q), STEP(NOTE_F4, NOTE_Q), STEP(NOTE_F4, NOTE_Q),
    STEP(NOTE_E4, NOTE_Q), STEP(NOTE_E4, NOTE_Q), STEP(NOTE_D4, NOTE_H),

    STEP(NOTE_C4, NOTE_Q), STEP(NOTE_C4, NOTE_Q), STEP(NOTE_G4, NOTE_Q), STEP(NOTE_G4, NOTE_Q),
    STEP(NOTE_A4, NOTE_Q), STEP(NOTE_A4, NOTE_Q), STEP(NOTE_G4, NOTE_H),

    STEP(NOTE_F4, NOTE_Q), STEP(NOTE_F4, NOTE_Q), STEP(NOTE_E4, NOTE_Q), STEP(NOTE_E4, NOTE_Q),
    STEP(NOTE_D4, NOTE_Q), STEP(NOTE_D4, NOTE_Q), STEP_G(NOTE_C4, NOTE_H, 120),
};

// Jingle Bells (recognizable chorus phrase in C major, simplified rhythm)
static const music_step_t s_jingle_bells_steps[] = {
    STEP(NOTE_E4, NOTE_Q), STEP(NOTE_E4, NOTE_Q), STEP_G(NOTE_E4, NOTE_H, 70),
    STEP(NOTE_E4, NOTE_Q), STEP(NOTE_E4, NOTE_Q), STEP_G(NOTE_E4, NOTE_H, 70),

    STEP(NOTE_E4, NOTE_Q), STEP(NOTE_G4, NOTE_Q), STEP(NOTE_C4, NOTE_Q), STEP(NOTE_D4, NOTE_Q),
    STEP_G(NOTE_E4, NOTE_H, 70),

    STEP(NOTE_F4, NOTE_Q), STEP(NOTE_F4, NOTE_Q), STEP(NOTE_F4, NOTE_Q), STEP(NOTE_F4, NOTE_Q),
    STEP(NOTE_F4, NOTE_Q), STEP(NOTE_E4, NOTE_Q), STEP(NOTE_E4, NOTE_Q),
    STEP(NOTE_E4, NOTE_E), STEP(NOTE_E4, NOTE_E),

    STEP(NOTE_E4, NOTE_Q), STEP(NOTE_D4, NOTE_Q), STEP(NOTE_D4, NOTE_Q), STEP(NOTE_E4, NOTE_Q),
    STEP(NOTE_D4, NOTE_H), STEP_G(NOTE_G4, NOTE_H, 120),
};

static const music_preset_t s_presets[] = {
    {
        .preset_id = MUSIC_PRESET_TWINKLE,
        .name = "Twinkle",
        .steps = s_twinkle_steps,
        .step_count = (uint8_t)(sizeof(s_twinkle_steps) / sizeof(s_twinkle_steps[0])),
        .default_tempo_pct = 100,
    },
    {
        .preset_id = MUSIC_PRESET_JINGLE_BELLS,
        .name = "Jingle Bells",
        .steps = s_jingle_bells_steps,
        .step_count = (uint8_t)(sizeof(s_jingle_bells_steps) / sizeof(s_jingle_bells_steps[0])),
        .default_tempo_pct = 100,
    },
};

static bool s_inited = false;
static uint8_t s_volume = SPEAKER_DEFAULT_VOLUME;

static uint8_t speaker_clamp_tempo(uint8_t tempo_pct) {
    if (tempo_pct == 0) {
        return SPEAKER_DEFAULT_TEMPO_PCT;
    }
    if (tempo_pct < 25) {
        return 25;
    }
    if (tempo_pct > 200) {
        return 200;
    }
    return tempo_pct;
}

static uint32_t speaker_scale_duration_ms(uint32_t base_ms, uint8_t tempo_pct) {
    tempo_pct = speaker_clamp_tempo(tempo_pct);
    // Higher tempo => shorter duration.
    uint32_t scaled = (base_ms * 100U) / tempo_pct;
    return (scaled == 0U) ? 1U : scaled;
}

static void speaker_delay_ms(uint32_t duration_ms) {
    if (duration_ms == 0U) {
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
}

#if CONFIG_IDF_TARGET_ESP32
static float speaker_volume_to_amplitude(uint8_t volume_percent) {
    if (volume_percent > 100U) {
        volume_percent = 100U;
    }
    return ((float)volume_percent / 100.0f) * SPEAKER_VOLUME_MAX_AMPLITUDE;
}

static uint16_t speaker_pack_dac_sample(uint8_t dac_u8) {
    // ESP32 built-in DAC mode takes the top 8 bits from the 16-bit I2S sample.
    return (uint16_t)dac_u8 << 8;
}

static esp_err_t speaker_i2s_write_frames(const uint16_t *interleaved_lr, size_t frame_count) {
    if (frame_count == 0U) {
        return ESP_OK;
    }
    size_t bytes_written = 0;
    const size_t bytes_to_write = frame_count * 2U * sizeof(uint16_t); // stereo L/R samples
    return i2s_write(SPEAKER_I2S_PORT, interleaved_lr, bytes_to_write, &bytes_written, portMAX_DELAY);
}

static esp_err_t speaker_write_midpoint_frames(size_t frame_count) {
    if (frame_count == 0U) {
        return ESP_OK;
    }

    if (frame_count > SPEAKER_PCM_CHUNK_FRAMES) {
        frame_count = SPEAKER_PCM_CHUNK_FRAMES;
    }

    uint16_t buf[SPEAKER_PCM_CHUNK_FRAMES * 2];
    const uint16_t sample = speaker_pack_dac_sample(SPEAKER_DAC_CENTER);
    for (size_t i = 0; i < frame_count; i++) {
        buf[(2U * i) + 0U] = sample;
        buf[(2U * i) + 1U] = sample;
    }
    return speaker_i2s_write_frames(buf, frame_count);
}

static float speaker_calc_envelope(uint32_t sample_idx, uint32_t total_samples, uint32_t edge_samples) {
    if (total_samples == 0U || edge_samples == 0U) {
        return 1.0f;
    }

    float env = 1.0f;
    if (sample_idx < edge_samples) {
        env = (float)sample_idx / (float)edge_samples;
    }

    if (total_samples > edge_samples && sample_idx >= (total_samples - edge_samples)) {
        uint32_t remaining = total_samples - sample_idx;
        float release_env = (float)remaining / (float)edge_samples;
        if (release_env < env) {
            env = release_env;
        }
    }

    if (env < 0.0f) {
        env = 0.0f;
    } else if (env > 1.0f) {
        env = 1.0f;
    }
    return env;
}
#endif

esp_err_t speaker_init(void) {
#if !CONFIG_IDF_TARGET_ESP32
    ESP_LOGE(TAG, "I2S internal DAC path is only supported on ESP32 (not this target)");
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (s_inited) {
        return ESP_OK;
    }

    i2s_config_t cfg = {
        .mode = I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_DAC_BUILT_IN,
        .sample_rate = SPEAKER_I2S_SAMPLE_RATE_HZ,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = SPEAKER_I2S_DMA_BUF_COUNT,
        .dma_buf_len = SPEAKER_I2S_DMA_BUF_LEN,
        .use_apll = false,
        .tx_desc_auto_clear = true,
        .fixed_mclk = 0,
        .mclk_multiple = I2S_MCLK_MULTIPLE_256,
        .bits_per_chan = I2S_BITS_PER_CHAN_DEFAULT,
    };

    esp_err_t err = i2s_driver_install(SPEAKER_I2S_PORT, &cfg, 0, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_driver_install failed: %s", esp_err_to_name(err));
        return err;
    }

    err = i2s_set_pin(SPEAKER_I2S_PORT, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_set_pin(NULL) failed: %s", esp_err_to_name(err));
        (void)i2s_driver_uninstall(SPEAKER_I2S_PORT);
        return err;
    }

    err = i2s_set_dac_mode(I2S_DAC_CHANNEL_RIGHT_EN);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_set_dac_mode failed: %s", esp_err_to_name(err));
        (void)i2s_driver_uninstall(SPEAKER_I2S_PORT);
        return err;
    }

    (void)i2s_zero_dma_buffer(SPEAKER_I2S_PORT);
    s_inited = true;
    (void)speaker_write_midpoint_frames(SPEAKER_PCM_CHUNK_FRAMES);
    ESP_LOGI(TAG, "Speaker initialized via I2S DAC on GPIO%d (I2S%d @ %d Hz)",
             SPEAKER_DAC_GPIO, (int)SPEAKER_I2S_PORT, SPEAKER_I2S_SAMPLE_RATE_HZ);
    return ESP_OK;
#endif
}

void speaker_deinit(void) {
    if (!s_inited) {
        return;
    }
    (void)speaker_stop();
#if CONFIG_IDF_TARGET_ESP32
    (void)i2s_driver_uninstall(SPEAKER_I2S_PORT);
#endif
    s_inited = false;
}

void speaker_set_volume(uint8_t volume_percent) {
    if (volume_percent > 100) {
        volume_percent = 100;
    }
    s_volume = volume_percent;
}

uint8_t speaker_get_volume(void) {
    return s_volume;
}

esp_err_t speaker_stop(void) {
    if (!s_inited) {
        return ESP_ERR_INVALID_STATE;
    }
#if CONFIG_IDF_TARGET_ESP32
    return speaker_write_midpoint_frames(SPEAKER_PCM_CHUNK_FRAMES);
#else
    return ESP_OK;
#endif
}

esp_err_t speaker_play_tone(uint32_t freq_hz, uint32_t duration_ms) {
    if (!s_inited) {
        return ESP_ERR_INVALID_STATE;
    }
    if (duration_ms == 0) {
        return ESP_OK;
    }
    if (freq_hz == 0) {
#if CONFIG_IDF_TARGET_ESP32
        (void)speaker_stop();
#endif
        speaker_delay_ms(duration_ms);
        return ESP_OK;
    }

#if !CONFIG_IDF_TARGET_ESP32
    return ESP_ERR_NOT_SUPPORTED;
#else
    uint32_t total_frames = (SPEAKER_I2S_SAMPLE_RATE_HZ * duration_ms) / 1000U;
    if (total_frames == 0U) {
        total_frames = 1U;
    }

    uint32_t edge_frames = (SPEAKER_I2S_SAMPLE_RATE_HZ * SPEAKER_ENVELOPE_MS) / 1000U;
    if (edge_frames > (total_frames / 2U)) {
        edge_frames = total_frames / 2U;
    }

    const float amplitude = speaker_volume_to_amplitude(s_volume);
    const float phase_inc = SPEAKER_TWO_PI_F * ((float)freq_hz / (float)SPEAKER_I2S_SAMPLE_RATE_HZ);
    float phase = 0.0f;

    uint16_t buf[SPEAKER_PCM_CHUNK_FRAMES * 2];
    uint32_t frames_done = 0U;

    while (frames_done < total_frames) {
        size_t chunk_frames = (size_t)(total_frames - frames_done);
        if (chunk_frames > SPEAKER_PCM_CHUNK_FRAMES) {
            chunk_frames = SPEAKER_PCM_CHUNK_FRAMES;
        }

        for (size_t i = 0; i < chunk_frames; i++) {
            uint32_t sample_idx = frames_done + (uint32_t)i;
            float env = speaker_calc_envelope(sample_idx, total_frames, edge_frames);
            float value = (float)SPEAKER_DAC_CENTER + (amplitude * env * sinf(phase));

            if (value < 0.0f) {
                value = 0.0f;
            } else if (value > 255.0f) {
                value = 255.0f;
            }

            uint16_t packed = speaker_pack_dac_sample((uint8_t)value);
            buf[(2U * i) + 0U] = packed;
            buf[(2U * i) + 1U] = packed;

            phase += phase_inc;
            if (phase >= SPEAKER_TWO_PI_F) {
                phase -= SPEAKER_TWO_PI_F;
            }
        }

        esp_err_t err = speaker_i2s_write_frames(buf, chunk_frames);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "i2s_write failed: %s", esp_err_to_name(err));
            (void)speaker_stop();
            return err;
        }

        frames_done += (uint32_t)chunk_frames;
    }

    return speaker_stop();
#endif
}

esp_err_t speaker_play_note(note_id_t note, uint32_t duration_ms) {
    if ((uint32_t)note >= NOTE_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    return speaker_play_tone(note_freq_hz[note], duration_ms);
}

esp_err_t speaker_play_step(const music_step_t *step, uint8_t tempo_pct) {
    if (step == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint32_t duration_ms = speaker_scale_duration_ms(step->duration_ms, tempo_pct);
    uint32_t gap_ms = speaker_scale_duration_ms(step->gap_ms, tempo_pct);

    esp_err_t err = speaker_play_note(step->note, duration_ms);
    if (err != ESP_OK) {
        return err;
    }

    if (gap_ms > 0U) {
        // Ensure a clean separation between notes.
        (void)speaker_stop();
        speaker_delay_ms(gap_ms);
    }

    return ESP_OK;
}

esp_err_t speaker_play_sequence(const music_step_t *steps, size_t step_count, uint8_t tempo_pct) {
    if ((steps == NULL && step_count > 0U)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_inited) {
        return ESP_ERR_INVALID_STATE;
    }

    tempo_pct = speaker_clamp_tempo(tempo_pct);

    for (size_t i = 0; i < step_count; i++) {
        esp_err_t err = speaker_play_step(&steps[i], tempo_pct);
        if (err != ESP_OK) {
            return err;
        }
    }

    return speaker_stop();
}

size_t speaker_get_preset_count(void) {
    return sizeof(s_presets) / sizeof(s_presets[0]);
}

const music_preset_t *speaker_get_preset_by_index(size_t index) {
    if (index >= speaker_get_preset_count()) {
        return NULL;
    }
    return &s_presets[index];
}

const music_preset_t *speaker_get_preset_by_id(uint8_t preset_id) {
    for (size_t i = 0; i < speaker_get_preset_count(); i++) {
        if (s_presets[i].preset_id == preset_id) {
            return &s_presets[i];
        }
    }
    return NULL;
}

esp_err_t speaker_play_preset(uint8_t preset_id, uint8_t tempo_pct) {
    const music_preset_t *preset = speaker_get_preset_by_id(preset_id);
    if (preset == NULL) {
        return ESP_ERR_NOT_FOUND;
    }

    uint8_t effective_tempo = (tempo_pct == 0U) ? preset->default_tempo_pct : tempo_pct;
    ESP_LOGI(TAG, "Playing preset '%s' (id=0x%02X, tempo=%u%%)",
             preset->name, preset->preset_id, (unsigned)effective_tempo);
    return speaker_play_sequence(preset->steps, preset->step_count, effective_tempo);
}

void speaker_beep_ok(void) {
    (void)speaker_play_tone(1200, 80);
    speaker_delay_ms(40);
    (void)speaker_play_tone(1600, 80);
}

void speaker_beep_error(void) {
    (void)speaker_play_tone(220, 200);
}
