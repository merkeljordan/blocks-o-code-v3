#pragma once
#ifndef MUSIC_SEQUENCE_TYPES_H
#define MUSIC_SEQUENCE_TYPES_H

/*
 * Shared data types for the Music Sequence block.
 *
 * These types are used by:
 * - UI (`tft_ui.c`)
 * - playback logic (`speaker_music.c`)
 * - command/I2C payload handling (`main.c`)
 */

#include <stdbool.h>
#include <stdint.h>

/* Reserved for future compose-mode UI (fixed small footprint for child UX). */
#define MUSIC_COMPOSE_MAX_STEPS 8

/* -------------------------------------------------------------------------- */
/* Note IDs and their corresponding frequencies (Hz)                          */
/* -------------------------------------------------------------------------- */
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

/* Static lookup table used by speaker_play_note(). */
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

/* -------------------------------------------------------------------------- */
/* Sequence step and song metadata                                             */
/* -------------------------------------------------------------------------- */
typedef struct {
    note_id_t note;
    uint16_t duration_ms;
    uint16_t gap_ms;
} music_step_t;

typedef enum {
    MUSIC_AGE_RANGE_ALL = 0,
    MUSIC_AGE_RANGE_2_TO_4,
    MUSIC_AGE_RANGE_5_TO_7,
    MUSIC_AGE_RANGE_8_PLUS,
    MUSIC_AGE_RANGE_COUNT
} music_age_range_t;

typedef struct {
    uint8_t song_id;
    const char *name;
    music_age_range_t age_range;
} song_info_t;

/* -------------------------------------------------------------------------- */
/* UI-selected config and runtime playback state                               */
/* -------------------------------------------------------------------------- */
typedef struct {
    uint8_t selected_song_index;
    bool config_valid;
} music_seq_config_t;

typedef struct {
    bool is_playing;
    uint8_t active_song_index;
} music_playback_state_t;

/* -------------------------------------------------------------------------- */
/* Wire payload used by CMD_GET_DATA                                           */
/* -------------------------------------------------------------------------- */
typedef struct {
    uint8_t song_id;
} music_seq_payload_v1_t;

/* -------------------------------------------------------------------------- */
/* Legacy preset identifiers/types (kept for compatibility)                    */
/* -------------------------------------------------------------------------- */
typedef enum {
    MUSIC_PRESET_TWINKLE      = 0,
    MUSIC_PRESET_JINGLE_BELLS = 1,
    MUSIC_PRESET_CUSTOM_1     = 0x80,
} music_preset_id_t;

typedef struct {
    uint8_t preset_id;
    const char *name;
    const music_step_t *steps;
    uint8_t step_count;
    uint8_t default_tempo_pct;
} music_preset_t;

#endif /* MUSIC_SEQUENCE_TYPES_H */
