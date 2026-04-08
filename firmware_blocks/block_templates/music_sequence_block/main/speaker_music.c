/**
 * ============================================================================
 * speaker_music.c
 * ============================================================================
 * Music-block-specific layer on top of generic audio APIs.
 *
 * Why this file exists:
 * - Shared `audio` component (`shared_components/audio`) handles wav/tone output.
 * - This file maps UI concepts (song index) to actual song assets.
 * - This file keeps legacy note/sequence/preset APIs available.
 *
 * Call flow:
 * - tft_ui.c / main.c -> speaker_play_song(index)
 * - speaker_play_song -> speaker_play_wav(...)
 * - speaker_play_wav -> DACOutput writer pipeline
 */

#include "speaker.h"
#include "music_sequence_types.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Debug/demo switch:
// 1 = output generated sine wave (easy to inspect on oscilloscope)
// 0 = output embedded WAV song(s)
#define USE_SCOPE_TONE   0
#define SCOPE_TONE_HZ    1000
#define SCOPE_TONE_MS    5000

// Linker symbols from main/CMakeLists.txt EMBED_FILES.
extern const uint8_t baby_shark_wav_start[] asm("_binary_baby_shark_wav_start");
extern const uint8_t baby_shark_wav_end[]   asm("_binary_baby_shark_wav_end");
extern const uint8_t espresso_wav_start[] asm("_binary_espresso_wav_start");
extern const uint8_t espresso_wav_end[]   asm("_binary_espresso_wav_end");
extern const uint8_t no_wav_start[] asm("_binary_no_wav_start");
extern const uint8_t no_wav_end[]   asm("_binary_no_wav_end");
extern const uint8_t sofia_the_first_wav_start[] asm("_binary_sofia_the_first_wav_start");
extern const uint8_t sofia_the_first_wav_end[]   asm("_binary_sofia_the_first_wav_end");

typedef struct {
    const char *name;
    music_age_range_t age_range;
    const uint8_t *wav_start;
    const uint8_t *wav_end;
    bool embedded;
} embedded_song_t;

static const embedded_song_t k_song_catalog[] = {
    {
        .name = "Baby Shark",
        .age_range = MUSIC_AGE_RANGE_2_TO_4,
        .wav_start = baby_shark_wav_start,
        .wav_end = baby_shark_wav_end,
        .embedded = true,
    },
    {
        .name = "Espresso",
        .age_range = MUSIC_AGE_RANGE_8_PLUS,
        .wav_start = espresso_wav_start,
        .wav_end = espresso_wav_end,
        .embedded = true,
    },
    {
        .name = "No - Meghan Trainor",
        .age_range = MUSIC_AGE_RANGE_8_PLUS,
        .wav_start = no_wav_start,
        .wav_end = no_wav_end,
        .embedded = true,
    },
    {
        .name = "Sofia the First",
        .age_range = MUSIC_AGE_RANGE_5_TO_7,
        .wav_start = sofia_the_first_wav_start,
        .wav_end = sofia_the_first_wav_end,
        .embedded = true,
    },
};

// --------------------------------------------------------------------------
// delay_ms
// --------------------------------------------------------------------------
// Called by: speaker_play_step
// Purpose: keep simple blocking timing behavior for sequence playback.
static void delay_ms(uint32_t ms)
{
    if (ms) {
        vTaskDelay(pdMS_TO_TICKS(ms));
    }
}

// --------------------------------------------------------------------------
// speaker_get_song_count
// --------------------------------------------------------------------------
// Called by: UI code (`tft_ui.c`) to show pagination/count.
size_t speaker_get_song_count(void)
{
    return sizeof(k_song_catalog) / sizeof(k_song_catalog[0]);
}

// --------------------------------------------------------------------------
// speaker_get_song_name
// --------------------------------------------------------------------------
// Called by: UI code (`tft_ui.c`) to display current song name.
const char *speaker_get_song_name(size_t index)
{
    if (index >= speaker_get_song_count()) {
        return "Unknown Song";
    }
    return k_song_catalog[index].name;
}

music_age_range_t speaker_get_song_age_range(size_t index)
{
    if (index >= speaker_get_song_count()) {
        return MUSIC_AGE_RANGE_ALL;
    }
    return k_song_catalog[index].age_range;
}

const char *speaker_get_age_range_label(music_age_range_t age_range)
{
    switch (age_range) {
        case MUSIC_AGE_RANGE_ALL:
            return "All Ages";
        case MUSIC_AGE_RANGE_2_TO_4:
            return "Ages 2-4";
        case MUSIC_AGE_RANGE_5_TO_7:
            return "Ages 5-7";
        case MUSIC_AGE_RANGE_8_PLUS:
            return "Ages 8+";
        default:
            return "Ages ?";
    }
}

// --------------------------------------------------------------------------
// speaker_play_song
// --------------------------------------------------------------------------
// Called by: preview worker and execute path.
// Calls: speaker_play_wav
esp_err_t speaker_play_song(size_t index)
{
#if USE_SCOPE_TONE
    (void)index;
    return speaker_play_tone(SCOPE_TONE_HZ, SCOPE_TONE_MS);
#else
    const embedded_song_t *song = NULL;
    size_t len = 0;

    if (index >= speaker_get_song_count()) {
        return ESP_ERR_INVALID_ARG;
    }

    song = &k_song_catalog[index];
    if (!song->embedded || song->wav_start == NULL || song->wav_end == NULL) {
        return ESP_ERR_NOT_FOUND;
    }

    len = (size_t)(song->wav_end - song->wav_start);
    if (len == 0U) {
        return ESP_ERR_NOT_FOUND;
    }

    return speaker_play_wav(song->wav_start, len);
#endif
}


// --------------------------------------------------------------------------
// speaker_play_note
// --------------------------------------------------------------------------
// Called by: speaker_play_step / legacy note callers.
// Calls: speaker_play_tone
esp_err_t speaker_play_note(note_id_t note, uint32_t duration_ms)
{
    // Validate enum before indexing note_freq_hz table.
    if ((uint32_t)note >= NOTE_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    return speaker_play_tone(note_freq_hz[note], duration_ms);
}

// --------------------------------------------------------------------------
// speaker_play_step
// --------------------------------------------------------------------------
// Called by: speaker_play_sequence
// Calls: speaker_play_note + delay_ms
esp_err_t speaker_play_step(const music_step_t *step, uint8_t tempo_pct)
{
    if (!step) {
        return ESP_ERR_INVALID_ARG;
    }

    // tempo_pct=100 means unchanged duration; lower=faster, higher=slower by this formula.
    uint8_t tempo = tempo_pct ? tempo_pct : 100;
    uint32_t dur = (step->duration_ms * 100U) / tempo;
    uint32_t gap = (step->gap_ms * 100U) / tempo;

    // Play note first.
    esp_err_t err = speaker_play_note(step->note, dur);

    // Only apply post-note gap if note playback succeeded.
    if (err == ESP_OK && gap) {
        delay_ms(gap);
    }

    return err;
}

// --------------------------------------------------------------------------
// speaker_play_sequence
// --------------------------------------------------------------------------
// Called by: legacy preset/custom sequence paths.
// Calls: speaker_play_step in a loop.
esp_err_t speaker_play_sequence(const music_step_t *steps, size_t step_count, uint8_t tempo_pct)
{
    // Null pointer is allowed only for empty sequence.
    if (!steps && step_count > 0) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t tempo = tempo_pct ? tempo_pct : 100;

    // Walk every step in order.
    for (size_t i = 0; i < step_count; i++) {
        esp_err_t err = speaker_play_step(&steps[i], tempo);
        if (err != ESP_OK) {
            // Stop immediately if any step fails.
            return err;
        }
    }

    return ESP_OK;
}

// --------------------------------------------------------------------------
// Preset placeholders (kept for API compatibility)
// --------------------------------------------------------------------------
size_t speaker_get_preset_count(void)
{
    return 0;
}

const music_preset_t *speaker_get_preset_by_index(size_t index)
{
    (void)index;
    return NULL;
}

const music_preset_t *speaker_get_preset_by_id(uint8_t preset_id)
{
    (void)preset_id;
    return NULL;
}

esp_err_t speaker_play_preset(uint8_t preset_id, uint8_t tempo_pct)
{
    (void)preset_id;
    (void)tempo_pct;
    return ESP_ERR_NOT_FOUND;
}
