/**
 * Music block extensions for the audio component.
 * Implements song catalog and note/sequence playback.
 */

#include "speaker.h"
#include "music_sequence_types.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern const uint8_t babyshark_wav_start[] asm("_binary_babyshark_wav_start");
extern const uint8_t babyshark_wav_end[]   asm("_binary_babyshark_wav_end");

static void delay_ms(uint32_t ms) {
    if (ms) vTaskDelay(pdMS_TO_TICKS(ms));
}

size_t speaker_get_song_count(void) {
    return 1;
}

const char *speaker_get_song_name(size_t index) {
    return (index == 0) ? "Baby Shark" : "???";
}

esp_err_t speaker_play_song(size_t index) {
    if (index != 0) return ESP_ERR_INVALID_ARG;
    size_t len = (size_t)(babyshark_wav_end - babyshark_wav_start);
    return speaker_play_wav(babyshark_wav_start, len);
}

esp_err_t speaker_play_note(note_id_t note, uint32_t duration_ms) {
    if ((uint32_t)note >= NOTE_COUNT) return ESP_ERR_INVALID_ARG;
    return speaker_play_tone(note_freq_hz[note], duration_ms);
}

esp_err_t speaker_play_step(const music_step_t *step, uint8_t tempo_pct) {
    if (!step) return ESP_ERR_INVALID_ARG;
    uint32_t dur = (step->duration_ms * 100U) / (tempo_pct ? tempo_pct : 100);
    uint32_t gap = (step->gap_ms * 100U) / (tempo_pct ? tempo_pct : 100);
    esp_err_t err = speaker_play_note(step->note, dur);
    if (err == ESP_OK && gap) delay_ms(gap);
    return err;
}

esp_err_t speaker_play_sequence(const music_step_t *steps, size_t step_count, uint8_t tempo_pct) {
    if (!steps && step_count > 0) return ESP_ERR_INVALID_ARG;
    uint8_t t = tempo_pct ? tempo_pct : 100;
    for (size_t i = 0; i < step_count; i++) {
        esp_err_t err = speaker_play_step(&steps[i], t);
        if (err != ESP_OK) return err;
    }
    return ESP_OK;
}

size_t speaker_get_preset_count(void) {
    return 0;
}

const music_preset_t *speaker_get_preset_by_index(size_t index) {
    (void)index;
    return NULL;
}

const music_preset_t *speaker_get_preset_by_id(uint8_t preset_id) {
    (void)preset_id;
    return NULL;
}

esp_err_t speaker_play_preset(uint8_t preset_id, uint8_t tempo_pct) {
    (void)preset_id;
    (void)tempo_pct;
    return ESP_ERR_NOT_FOUND;
}
