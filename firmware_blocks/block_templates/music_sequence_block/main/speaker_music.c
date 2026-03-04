/**
 * ============================================================================
 * speaker_music.c
 * ============================================================================
 * Music-block-specific layer on top of generic audio APIs.
 *
 * Why this file exists:
 * - `components/audio` handles low-level playback (wav/tone output).
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
#define USE_SCOPE_TONE   1
#define SCOPE_TONE_HZ    1000
#define SCOPE_TONE_MS    5000

// Linker symbols from EMBED_FILES "audio/babyshark.wav".
extern const uint8_t babyshark_wav_start[] asm("_binary_babyshark_wav_start");
extern const uint8_t babyshark_wav_end[]   asm("_binary_babyshark_wav_end");

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
    return 1;
}

// --------------------------------------------------------------------------
// speaker_get_song_name
// --------------------------------------------------------------------------
// Called by: UI code (`tft_ui.c`) to display current song name.
const char *speaker_get_song_name(size_t index)
{
    return (index == 0) ? "Baby Shark" : "???";
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
    if (index != 0) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t len = (size_t)(babyshark_wav_end - babyshark_wav_start);
    return speaker_play_wav(babyshark_wav_start, len);
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
