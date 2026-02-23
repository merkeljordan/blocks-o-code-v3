#pragma once
#ifndef MUSIC_SEQUENCE_TYPES_H
#define MUSIC_SEQUENCE_TYPES_H

#include <stdint.h>
#include <stdbool.h>

#define MUSIC_COMPOSE_MAX_STEPS 8

// -----------------------------------------------------------------------------
// Notes (v1: enough range for simple kid songs like Twinkle/Jingle)
// -----------------------------------------------------------------------------
typedef enum {
    NOTE_REST = 0,
    NOTE_C4,
    NOTE_D4,
    NOTE_E4,
    NOTE_F4,
    NOTE_G4,
    NOTE_A4,
    NOTE_B4,
    NOTE_C5,
    NOTE_D5,
    NOTE_E5,
    NOTE_COUNT
} note_id_t;

static const uint16_t note_freq_hz[NOTE_COUNT] = {
    [NOTE_REST] = 0,
    [NOTE_C4]   = 262,
    [NOTE_D4]   = 294,
    [NOTE_E4]   = 330,
    [NOTE_F4]   = 349,
    [NOTE_G4]   = 392,
    [NOTE_A4]   = 440,
    [NOTE_B4]   = 494,
    [NOTE_C5]   = 523,
    [NOTE_D5]   = 587,
    [NOTE_E5]   = 659,
};

static const char *const note_labels[NOTE_COUNT] = {
    [NOTE_REST] = "REST",
    [NOTE_C4]   = "C4",
    [NOTE_D4]   = "D4",
    [NOTE_E4]   = "E4",
    [NOTE_F4]   = "F4",
    [NOTE_G4]   = "G4",
    [NOTE_A4]   = "A4",
    [NOTE_B4]   = "B4",
    [NOTE_C5]   = "C5",
    [NOTE_D5]   = "D5",
    [NOTE_E5]   = "E5",
};

// -----------------------------------------------------------------------------
// Song steps + presets (firmware playback)
// -----------------------------------------------------------------------------
typedef struct {
    note_id_t note;
    uint16_t duration_ms;   // how long to sound the note
    uint16_t gap_ms;        // silence after the note (0 if none)
} music_step_t;

typedef enum {
    MUSIC_PRESET_TWINKLE = 0,
    MUSIC_PRESET_JINGLE_BELLS = 1,
    MUSIC_PRESET_CUSTOM_1 = 0x80,
} music_preset_id_t;

typedef struct {
    uint8_t preset_id;                 // music_preset_id_t stored as byte
    const char *name;                  // "Twinkle"
    const music_step_t *steps;         // points to static song array
    uint8_t step_count;
    uint8_t default_tempo_pct;         // 100 = normal tempo
} music_preset_t;

// -----------------------------------------------------------------------------
// TFT/UI config (local block state)
// -----------------------------------------------------------------------------
typedef enum {
    MUSIC_UI_MODE_PRESET = 0,
    MUSIC_UI_MODE_COMPOSE = 1,
} music_ui_mode_t;

typedef struct {
    music_ui_mode_t mode;

    // Preset mode
    uint8_t selected_preset_id;        // music_preset_id_t

    // Compose mode (v1 = fixed note duration for all slots)
    uint8_t compose_step_count;        // 0..MUSIC_COMPOSE_MAX_STEPS
    uint8_t compose_cursor_index;      // highlighted slot on TFT
    note_id_t compose_notes[MUSIC_COMPOSE_MAX_STEPS];
    uint16_t compose_step_duration_ms; // e.g. 250 ms per note

    // Shared options
    uint8_t tempo_pct;                 // 50..200
    uint8_t loop_count;                // 0=no loop, 1..N, 0xFF=infinite
    bool config_valid;
} music_seq_config_t;

// -----------------------------------------------------------------------------
// Runtime playback state (non-blocking state machine)
// -----------------------------------------------------------------------------
typedef struct {
    bool is_playing;
    bool is_preview;
    uint8_t active_preset_id;
    uint8_t step_index;
    bool note_on_phase;                // true=playing note, false=gap/rest
    int64_t next_transition_us;        // target time (esp_timer_get_time units)
} music_playback_state_t;

// -----------------------------------------------------------------------------
// I2C payloads (use byte fields for wire-format compatibility)
// -----------------------------------------------------------------------------
typedef struct {
    uint8_t sequence_id;               // preset/custom ID (FRAMEWORK.md v1)
} music_seq_payload_v1_t;

typedef struct {
    uint8_t mode;                      // music_ui_mode_t as byte
    uint8_t sequence_id;               // preset/custom ID
    uint8_t step_count;                // compose steps used
    uint8_t tempo_pct;
    uint8_t notes[MUSIC_COMPOSE_MAX_STEPS]; // note_id_t values as bytes
} music_seq_payload_v2_t;

#endif // MUSIC_SEQUENCE_TYPES_H
