#pragma once

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "main/music_sequence_types.h"

// Simple audio driver for the music sequence block (ESP32 uses I2S + internal DAC).
esp_err_t speaker_init(void);
void speaker_deinit(void);
void speaker_set_volume(uint8_t volume_percent);
uint8_t speaker_get_volume(void);
esp_err_t speaker_stop(void);
esp_err_t speaker_play_tone(uint32_t freq_hz, uint32_t duration_ms);

// Note + melody helpers for music sequence block.
esp_err_t speaker_play_note(note_id_t note, uint32_t duration_ms);
esp_err_t speaker_play_step(const music_step_t *step, uint8_t tempo_pct);
esp_err_t speaker_play_sequence(const music_step_t *steps, size_t step_count, uint8_t tempo_pct);

// Built-in preset catalog for TFT arrow browsing (Preset Mode).
size_t speaker_get_preset_count(void);
const music_preset_t *speaker_get_preset_by_index(size_t index);
const music_preset_t *speaker_get_preset_by_id(uint8_t preset_id);
esp_err_t speaker_play_preset(uint8_t preset_id, uint8_t tempo_pct);

// Common UX beeps
void speaker_beep_ok(void);
void speaker_beep_error(void);
