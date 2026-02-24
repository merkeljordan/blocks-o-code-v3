#include "speaker.h"

#include <stddef.h>
#include <stdbool.h>

#include "driver/ledc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "SPEAKER";

// Simple PWM output path for LM386 input (known-good on this hardware).
#define SPEAKER_GPIO                     25

#define SPEAKER_LEDC_MODE                LEDC_LOW_SPEED_MODE
#define SPEAKER_LEDC_TIMER               LEDC_TIMER_0
#define SPEAKER_LEDC_CHANNEL             LEDC_CHANNEL_0
#define SPEAKER_LEDC_DUTY_RES            LEDC_TIMER_10_BIT

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

static uint32_t speaker_get_duty(uint8_t volume_percent) {
    if (volume_percent > 100U) {
        volume_percent = 100U;
    }
    uint32_t max_duty = (1U << SPEAKER_LEDC_DUTY_RES) - 1U;
    // 50% duty at 100% volume, scaled down from there.
    return (max_duty * volume_percent) / 200U;
}

esp_err_t speaker_init(void) {
    if (s_inited) {
        return ESP_OK;
    }

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
    ESP_LOGI(TAG, "Speaker initialized via LEDC PWM on GPIO%d", SPEAKER_GPIO);
    return ESP_OK;
}

void speaker_deinit(void) {
    if (!s_inited) {
        return;
    }
    (void)speaker_stop();
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
    return ledc_stop(SPEAKER_LEDC_MODE, SPEAKER_LEDC_CHANNEL, 0);
}

esp_err_t speaker_play_tone(uint32_t freq_hz, uint32_t duration_ms) {
    if (!s_inited) {
        return ESP_ERR_INVALID_STATE;
    }
    if (duration_ms == 0) {
        return ESP_OK;
    }
    if (freq_hz == 0) {
        (void)speaker_stop();
        speaker_delay_ms(duration_ms);
        return ESP_OK;
    }

    esp_err_t err = ledc_set_freq(SPEAKER_LEDC_MODE, SPEAKER_LEDC_TIMER, freq_hz);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LEDC set freq failed: %s", esp_err_to_name(err));
        return err;
    }

    uint32_t duty = speaker_get_duty(s_volume);
    err = ledc_set_duty(SPEAKER_LEDC_MODE, SPEAKER_LEDC_CHANNEL, duty);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LEDC set duty failed: %s", esp_err_to_name(err));
        return err;
    }

    err = ledc_update_duty(SPEAKER_LEDC_MODE, SPEAKER_LEDC_CHANNEL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LEDC update duty failed: %s", esp_err_to_name(err));
        return err;
    }

    speaker_delay_ms(duration_ms);
    return speaker_stop();
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
