#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "music_sequence_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MUSIC_UI_ACTION_NONE = 0,
    MUSIC_UI_ACTION_SONG_CHANGED,
    MUSIC_UI_ACTION_SONG_SELECTED,
} music_ui_action_type_t;

typedef struct {
    music_ui_action_type_t type;
    uint8_t song_index;
} music_ui_action_t;

esp_err_t tft_ui_start(void);

bool tft_ui_take_action(music_ui_action_t *out_action, uint32_t timeout_ms);

void tft_ui_set_playback_state(const music_playback_state_t *state);
void tft_ui_set_status_message(const char *msg);

#ifdef __cplusplus
}
#endif
