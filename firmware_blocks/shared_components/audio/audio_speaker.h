#pragma once

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t speaker_init(void);
void      speaker_deinit(void);
void      speaker_set_volume(uint8_t volume_percent);
uint8_t   speaker_get_volume(void);
esp_err_t speaker_stop(void);

esp_err_t speaker_play_boot_sound(void);
esp_err_t speaker_play_wav(const uint8_t *data, size_t len);
esp_err_t speaker_play_tone(uint32_t freq_hz, uint32_t duration_ms);
esp_err_t speaker_play_note_tone(uint32_t freq_hz, uint32_t duration_ms);

void speaker_beep_ok(void);
void speaker_beep_error(void);

#ifdef __cplusplus
}
#endif
