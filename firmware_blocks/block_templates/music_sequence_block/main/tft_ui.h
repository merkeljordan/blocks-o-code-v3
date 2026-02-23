#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "music_sequence_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Dual-core usage model for the Music Sequence block:
 * - UI/LVGL task(s): pin to core 1 (inside tft_ui_start on dual-core ESP32)
 * - I2C + execution tasks: pin to core 0 (from app_main)
 */

typedef enum {
    MUSIC_UI_ACTION_NONE = 0,
    MUSIC_UI_ACTION_CONFIG_UPDATED,
    MUSIC_UI_ACTION_CONFIG_COMMITTED,
} music_ui_action_type_t;

typedef struct {
    music_ui_action_type_t type;
    uint8_t mode;          // music_ui_mode_t as byte
    uint8_t sequence_id;   // selected preset/custom ID
    uint8_t tempo_pct;
    uint8_t config_valid;  // bool serialized as byte
} music_ui_action_t;

// Starts TFT/LVGL and creates UI + preview tasks.
esp_err_t tft_ui_start(void);

// Thread-safe snapshot for core0 I2C/execution logic.
bool tft_ui_get_config_snapshot(music_seq_config_t *out_config);

// Non-LVGL API for core0 to pull UI-originated config events.
bool tft_ui_take_action(music_ui_action_t *out_action, uint32_t timeout_ms);

// Thread-safe playback state/status updates from core0 execution task.
void tft_ui_set_playback_state(const music_playback_state_t *state);
void tft_ui_set_status_message(const char *msg);

#ifdef __cplusplus
}
#endif

