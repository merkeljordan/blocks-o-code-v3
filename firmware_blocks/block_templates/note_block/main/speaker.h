#pragma once

#include "esp_err.h"
#include <stdint.h>

// Simple PWM speaker driver for LM386 input.
esp_err_t speaker_init(void);
void speaker_deinit(void);
void speaker_set_volume(uint8_t volume_percent);
esp_err_t speaker_play_tone(uint32_t freq_hz, uint32_t duration_ms);

// Common UX beeps
void speaker_beep_ok(void);
void speaker_beep_error(void);

// Boot UX sound: 440 Hz -> 660 Hz -> 880 Hz.
void speaker_play_boot_sound(void);
