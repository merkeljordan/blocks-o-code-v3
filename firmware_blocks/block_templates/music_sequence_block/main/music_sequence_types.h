#pragma once
#ifndef MUSIC_SEQUENCE_TYPES_H
#define MUSIC_SEQUENCE_TYPES_H

#include <stdint.h>
#include <stdbool.h>

#define MUSIC_COMPOSE_MAX_STEPS 8

// -----------------------------------------------------------------------------
// Notes (kept for sine-wave tone generation / beeps)
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
// Song steps (kept for sine-wave tone sequences)
// -----------------------------------------------------------------------------
typedef struct {
    note_id_t note;
    uint16_t duration_ms;
    uint16_t gap_ms;
} music_step_t;

// -----------------------------------------------------------------------------
// Song catalog entry (MP3-backed songs)
// -----------------------------------------------------------------------------
typedef struct {
    uint8_t     song_id;
    const char *name;
} song_info_t;

// -----------------------------------------------------------------------------
// TFT/UI config (simplified — just song selection)
// -----------------------------------------------------------------------------
typedef struct {
    uint8_t selected_song_index;
    bool    config_valid;
} music_seq_config_t;

// -----------------------------------------------------------------------------
// Runtime playback state
// -----------------------------------------------------------------------------
typedef struct {
    bool    is_playing;
    uint8_t active_song_index;
} music_playback_state_t;

// -----------------------------------------------------------------------------
// I2C payload (wire format)
// -----------------------------------------------------------------------------
typedef struct {
    uint8_t song_id;
} music_seq_payload_v1_t;

// Legacy identifiers kept for I2C protocol compatibility
typedef enum {
    MUSIC_PRESET_TWINKLE     = 0,
    MUSIC_PRESET_JINGLE_BELLS = 1,
    MUSIC_PRESET_CUSTOM_1    = 0x80,
} music_preset_id_t;

// Legacy preset struct (kept so speaker.h compiles; not used by new UI)
typedef struct {
    uint8_t preset_id;
    const char *name;
    const music_step_t *steps;
    uint8_t step_count;
    uint8_t default_tempo_pct;
} music_preset_t;

#endif // MUSIC_SEQUENCE_TYPES_H
