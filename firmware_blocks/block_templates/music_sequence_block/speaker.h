#pragma once

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "audio_speaker.h"
#include "main/music_sequence_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Music block extensions (audio component provides init, boot_sound, beeps, play_wav, play_tone) */

size_t      speaker_get_song_count(void);
const char *speaker_get_song_name(size_t index);
esp_err_t   speaker_play_song(size_t index);

esp_err_t speaker_play_note(note_id_t note, uint32_t duration_ms);
esp_err_t speaker_play_step(const music_step_t *step, uint8_t tempo_pct);
esp_err_t speaker_play_sequence(const music_step_t *steps, size_t step_count, uint8_t tempo_pct);

size_t speaker_get_preset_count(void);
const music_preset_t *speaker_get_preset_by_index(size_t index);
const music_preset_t *speaker_get_preset_by_id(uint8_t preset_id);
esp_err_t speaker_play_preset(uint8_t preset_id, uint8_t tempo_pct);

#ifdef __cplusplus
}
#endif
