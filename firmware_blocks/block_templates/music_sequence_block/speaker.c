#include "speaker.h"

#include <stddef.h>
#include <stdbool.h>
#include <math.h>

#include "driver/i2s.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "SPEAKER";

// Internal DAC via I2S (GPIO25 / DAC1) for the current LM386 analog-input board.
#define SPEAKER_I2S_PORT                  I2S_NUM_0
#define SPEAKER_I2S_SAMPLE_RATE_HZ        44100
#define SPEAKER_I2S_DMA_BUF_COUNT         8
#define SPEAKER_I2S_DMA_BUF_LEN           256
#define SPEAKER_I2S_CHUNK_FRAMES          128U

#define SPEAKER_DAC_CHANNEL               I2S_DAC_CHANNEL_RIGHT_EN  // GPIO25 on ESP32
#define SPEAKER_DAC_MIDPOINT_U8           128U
#define SPEAKER_DAC_MAX_AMPLITUDE         120U  // Peak swing around midpoint at 100% volume (experiment branch)
#define SPEAKER_ENVELOPE_MS               4U    // Small fade in/out to reduce clicks

#define SPEAKER_DEFAULT_VOLUME            30    // percent
#define SPEAKER_DEFAULT_TEMPO_PCT         100
#define SPEAKER_NOTE_GAP_MS_DEFAULT       40U

#define NOTE_Q 250U  // quarter note base duration (ms)
#define NOTE_H 500U  // half note base duration (ms)
#define NOTE_E 125U  // eighth note base duration (ms)

#define STEP(note_, dur_)       { .note = (note_), .duration_ms = (dur_), .gap_ms = SPEAKER_NOTE_GAP_MS_DEFAULT }
#define STEP_G(note_, dur_, g_) { .note = (note_), .duration_ms = (dur_), .gap_ms = (g_) }

#define SPEAKER_PI_F 3.14159265358979323846f

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

static uint8_t speaker_clamp_tempo(uint8_t tempo_pct)
{
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

static uint32_t speaker_scale_duration_ms(uint32_t base_ms, uint8_t tempo_pct)
{
    tempo_pct = speaker_clamp_tempo(tempo_pct);
    // Higher tempo => shorter duration.
    uint32_t scaled = (base_ms * 100U) / tempo_pct;
    return (scaled == 0U) ? 1U : scaled;
}

static void speaker_delay_ms(uint32_t duration_ms)
{
    if (duration_ms == 0U) {
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
}

static uint8_t speaker_get_peak_amplitude(uint8_t volume_percent)
{
    if (volume_percent > 100U) {
        volume_percent = 100U;
    }
    uint32_t amp = ((uint32_t)SPEAKER_DAC_MAX_AMPLITUDE * (uint32_t)volume_percent) / 100U;
    if (amp > 127U) {
        amp = 127U;
    }
    return (uint8_t)amp;
}

static inline uint16_t speaker_pack_dac_u8(uint8_t v)
{
    // Duplicate the DAC byte into both halves of the 16-bit word.
    // The internal DAC nominally uses the upper 8 bits, but mirroring helps avoid
    // byte-order/format quirks in the legacy I2S->DAC path during experiments.
    return (uint16_t)(((uint16_t)v << 8) | (uint16_t)v);
}

static esp_err_t speaker_i2s_write_samples(const uint16_t *samples, size_t sample_count)
{
    if (samples == NULL || sample_count == 0U) {
        return ESP_OK;
    }

    size_t bytes_to_write = sample_count * sizeof(uint16_t);
    size_t bytes_written = 0;
    esp_err_t err = i2s_write(SPEAKER_I2S_PORT,
                              (const char *)samples,
                              bytes_to_write,
                              &bytes_written,
                              portMAX_DELAY);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_write failed: %s", esp_err_to_name(err));
        return err;
    }
    if (bytes_written != bytes_to_write) {
        ESP_LOGW(TAG, "i2s_write short write: %u/%u bytes",
                 (unsigned)bytes_written, (unsigned)bytes_to_write);
    }
    return ESP_OK;
}

static esp_err_t speaker_write_midpoint_frames(uint32_t frame_count)
{
    uint16_t buf[SPEAKER_I2S_CHUNK_FRAMES * 2];
    uint16_t mid = speaker_pack_dac_u8(SPEAKER_DAC_MIDPOINT_U8);

    for (size_t i = 0; i < (sizeof(buf) / sizeof(buf[0])); i++) {
        buf[i] = mid;
    }

    while (frame_count > 0U) {
        uint32_t chunk_frames = (frame_count > SPEAKER_I2S_CHUNK_FRAMES) ? SPEAKER_I2S_CHUNK_FRAMES : frame_count;
        esp_err_t err = speaker_i2s_write_samples(buf, (size_t)chunk_frames * 2U);
        if (err != ESP_OK) {
            return err;
        }
        frame_count -= chunk_frames;
    }

    return ESP_OK;
}

esp_err_t speaker_init(void)
{
    if (s_inited) {
        return ESP_OK;
    }

    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_DAC_BUILT_IN),
        .sample_rate = SPEAKER_I2S_SAMPLE_RATE_HZ,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = (i2s_comm_format_t)I2S_COMM_FORMAT_STAND_MSB,
        .intr_alloc_flags = 0,
        .dma_buf_count = SPEAKER_I2S_DMA_BUF_COUNT,
        .dma_buf_len = SPEAKER_I2S_DMA_BUF_LEN,
        .use_apll = false,
        .tx_desc_auto_clear = true,
        .fixed_mclk = 0,
    };

    esp_err_t err = i2s_driver_install(SPEAKER_I2S_PORT, &i2s_config, 0, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2S driver init failed: %s", esp_err_to_name(err));
        return err;
    }

    err = i2s_set_pin(SPEAKER_I2S_PORT, NULL); // NULL for built-in DAC mode
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2S set pin (internal DAC) failed: %s", esp_err_to_name(err));
        (void)i2s_driver_uninstall(SPEAKER_I2S_PORT);
        return err;
    }

    err = i2s_set_dac_mode(SPEAKER_DAC_CHANNEL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2S set DAC mode failed: %s", esp_err_to_name(err));
        (void)i2s_driver_uninstall(SPEAKER_I2S_PORT);
        return err;
    }

    // Re-apply the clock after enabling built-in DAC mode so divider issues fail loudly.
    err = i2s_set_clk(SPEAKER_I2S_PORT,
                      SPEAKER_I2S_SAMPLE_RATE_HZ,
                      I2S_BITS_PER_SAMPLE_16BIT,
                      I2S_CHANNEL_STEREO);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2S set clock failed: %s", esp_err_to_name(err));
        (void)i2s_driver_uninstall(SPEAKER_I2S_PORT);
        return err;
    }

    (void)i2s_zero_dma_buffer(SPEAKER_I2S_PORT);
    (void)speaker_write_midpoint_frames(32U);

    s_inited = true;
    ESP_LOGI(TAG, "Speaker initialized via I2S internal DAC on GPIO25 (LM386 input)");
    return ESP_OK;
}

void speaker_deinit(void)
{
    if (!s_inited) {
        return;
    }

    (void)speaker_stop();
    (void)i2s_driver_uninstall(SPEAKER_I2S_PORT);
    s_inited = false;
}

void speaker_set_volume(uint8_t volume_percent)
{
    if (volume_percent > 100) {
        volume_percent = 100;
    }
    s_volume = volume_percent;
}

uint8_t speaker_get_volume(void)
{
    return s_volume;
}

esp_err_t speaker_stop(void)
{
    if (!s_inited) {
        return ESP_ERR_INVALID_STATE;
    }

    // Drive the DAC back to midscale to reduce pops and DC shift into the LM386 path.
    return speaker_write_midpoint_frames(24U);
}

esp_err_t speaker_play_tone(uint32_t freq_hz, uint32_t duration_ms)
{
    if (!s_inited) {
        return ESP_ERR_INVALID_STATE;
    }
    if (duration_ms == 0U) {
        return ESP_OK;
    }
    if (freq_hz == 0U) {
        (void)speaker_stop();
        speaker_delay_ms(duration_ms);
        return ESP_OK;
    }

    uint64_t total_frames_u64 = ((uint64_t)SPEAKER_I2S_SAMPLE_RATE_HZ * (uint64_t)duration_ms + 999ULL) / 1000ULL;
    uint32_t total_frames = (total_frames_u64 == 0ULL) ? 1U : (uint32_t)total_frames_u64;
    uint32_t env_frames = ((uint32_t)SPEAKER_I2S_SAMPLE_RATE_HZ * SPEAKER_ENVELOPE_MS) / 1000U;
    if (env_frames == 0U) {
        env_frames = 1U;
    }
    if (env_frames * 2U > total_frames) {
        env_frames = total_frames / 2U;
    }

    uint8_t peak = speaker_get_peak_amplitude(s_volume);
    if (peak == 0U) {
        (void)speaker_stop();
        speaker_delay_ms(duration_ms);
        return ESP_OK;
    }

    const float two_pi = 2.0f * SPEAKER_PI_F;
    const float phase_inc = two_pi * ((float)freq_hz / (float)SPEAKER_I2S_SAMPLE_RATE_HZ);
    float phase = 0.0f;

    uint16_t buf[SPEAKER_I2S_CHUNK_FRAMES * 2];
    uint32_t frames_done = 0U;

    while (frames_done < total_frames) {
        uint32_t chunk_frames = total_frames - frames_done;
        if (chunk_frames > SPEAKER_I2S_CHUNK_FRAMES) {
            chunk_frames = SPEAKER_I2S_CHUNK_FRAMES;
        }

        for (uint32_t i = 0; i < chunk_frames; i++) {
            uint32_t idx = frames_done + i;
            float env = 1.0f;

            if (env_frames > 0U && idx < env_frames) {
                float attack = (float)(idx + 1U) / (float)env_frames;
                if (attack < env) {
                    env = attack;
                }
            }
            if (env_frames > 0U) {
                uint32_t frames_left = total_frames - idx;
                if (frames_left <= env_frames) {
                    float release = (float)frames_left / (float)env_frames;
                    if (release < env) {
                        env = release;
                    }
                }
            }

            float sample_f = sinf(phase);
            phase += phase_inc;
            if (phase >= two_pi) {
                phase -= two_pi;
            }

            float scaled = sample_f * ((float)peak * env);
            int32_t offset = (scaled >= 0.0f) ? (int32_t)(scaled + 0.5f) : (int32_t)(scaled - 0.5f);
            int32_t u8 = (int32_t)SPEAKER_DAC_MIDPOINT_U8 + offset;
            if (u8 < 0) {
                u8 = 0;
            } else if (u8 > 255) {
                u8 = 255;
            }

            uint16_t packed = speaker_pack_dac_u8((uint8_t)u8);
            // Fill both slots; only the selected internal DAC channel is enabled.
            buf[(size_t)i * 2U] = packed;
            buf[(size_t)i * 2U + 1U] = packed;
        }

        esp_err_t err = speaker_i2s_write_samples(buf, (size_t)chunk_frames * 2U);
        if (err != ESP_OK) {
            return err;
        }

        frames_done += chunk_frames;
    }

    return speaker_stop();
}

esp_err_t speaker_play_note(note_id_t note, uint32_t duration_ms)
{
    if ((uint32_t)note >= NOTE_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    return speaker_play_tone(note_freq_hz[note], duration_ms);
}

esp_err_t speaker_play_step(const music_step_t *step, uint8_t tempo_pct)
{
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

esp_err_t speaker_play_sequence(const music_step_t *steps, size_t step_count, uint8_t tempo_pct)
{
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

size_t speaker_get_preset_count(void)
{
    return sizeof(s_presets) / sizeof(s_presets[0]);
}

const music_preset_t *speaker_get_preset_by_index(size_t index)
{
    if (index >= speaker_get_preset_count()) {
        return NULL;
    }
    return &s_presets[index];
}

const music_preset_t *speaker_get_preset_by_id(uint8_t preset_id)
{
    for (size_t i = 0; i < speaker_get_preset_count(); i++) {
        if (s_presets[i].preset_id == preset_id) {
            return &s_presets[i];
        }
    }
    return NULL;
}

esp_err_t speaker_play_preset(uint8_t preset_id, uint8_t tempo_pct)
{
    const music_preset_t *preset = speaker_get_preset_by_id(preset_id);
    if (preset == NULL) {
        return ESP_ERR_NOT_FOUND;
    }

    uint8_t effective_tempo = (tempo_pct == 0U) ? preset->default_tempo_pct : tempo_pct;
    ESP_LOGI(TAG, "Playing preset '%s' (id=0x%02X, tempo=%u%%)",
             preset->name, preset->preset_id, (unsigned)effective_tempo);
    return speaker_play_sequence(preset->steps, preset->step_count, effective_tempo);
}

void speaker_beep_ok(void)
{
    (void)speaker_play_tone(1200, 80);
    speaker_delay_ms(40);
    (void)speaker_play_tone(1600, 80);
}

void speaker_beep_error(void)
{
    (void)speaker_play_tone(220, 200);
}
