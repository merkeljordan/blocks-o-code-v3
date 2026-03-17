#pragma once
#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t speaker_init(void);
void      speaker_deinit(void);
void      speaker_set_volume(uint8_t volume_percent);
esp_err_t speaker_stop(void);

// Shared “do-do-do” PWM boot sound
esp_err_t speaker_play_boot_sound(void);

// Simple tone + UX beeps
esp_err_t speaker_play_tone(uint32_t hz, uint32_t ms);
void      speaker_beep_ok(void);
void      speaker_beep_error(void);

#ifdef __cplusplus
}
#endif