#pragma once

/*
 * TFT UI API for the Music Sequence block.
 *
 * The UI exposes user decisions via a FreeRTOS queue (take_action), and accepts
 * optional playback/status updates from non-UI tasks.
 */

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

/* Called by: app_main()
 * Starts LVGL, display/touch drivers, shared queues/mutex, and worker tasks.
 */
esp_err_t tft_ui_start(void);

/* Called by: execution_task() in main.c
 * Reads next queued UI action (song changed/selected). Returns false on timeout.
 */
bool tft_ui_take_action(music_ui_action_t *out_action, uint32_t timeout_ms);

/* Called by: optional non-UI tasks to reflect playback activity. */
void tft_ui_set_playback_state(const music_playback_state_t *state);

/* Called by: optional non-UI tasks to push status text to UI. */
void tft_ui_set_status_message(const char *msg);

#ifdef __cplusplus
}
#endif
