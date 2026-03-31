#pragma once

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t music_leds_init(void);
void music_leds_show_startup(void);
void music_leds_show_idle(void);
void music_leds_show_note_color(uint8_t note_id, uint32_t hold_ms);
void music_leds_start_song_pattern(uint8_t song_id);
void music_leds_stop_song_pattern(void);

#ifdef __cplusplus
}
#endif
