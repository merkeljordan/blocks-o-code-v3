# Music Sequence Block Audio Path

This module now uses a **WAV + I2S DAC** audio path (not the older PWM-only path).

## Current Audio Backend

- Output pipeline: `SampleSource` -> `DACOutput` -> ESP32 internal DAC (I2S DAC mode)
- Default data pin path: DAC right channel (GPIO25 on classic ESP32)
- API surface: `components/audio/audio_speaker.h`

## Core Files

- `components/audio/speaker.cpp`
- `components/audio/DACOutput.cpp`
- `components/audio/WAVFileReader.cpp`
- `components/audio/SinWaveGenerator.cpp`
- `main/speaker_music.c`
- `speaker.h`

## Playback Modes

1. **Song playback**
   - `speaker_play_song(index)` resolves song asset and calls `speaker_play_wav(...)`.
2. **Tone playback (utility)**
   - `speaker_play_tone(freq, ms)` for beeps and note-based sequences.
3. **Sequence playback (legacy-compatible APIs)**
   - `speaker_play_step(...)`
   - `speaker_play_sequence(...)`

## Timing Model

Playback helpers are currently synchronous/blocking at call site. To keep UI responsive:

- `main/tft_ui.c` uses a dedicated `preview_task` for song preview playback.
- LVGL task only posts preview requests.

## Song Catalog

The current 4 MB hardware catalog is:

- `Espresso` -> backed by `main/audio/espresso.wav`, `Ages 8+`
- `Birds of a Feather` -> backed by `main/audio/birds_of_a_feather.wav`, `Ages 8+`
- `Beat It` -> backed by `main/audio/beat_it.wav`, `Ages 8+`
- `Hakuna Matata` -> backed by `main/audio/hakuna_matata.wav`, `Ages 5-7`
- `Baby Shark` -> backed by `main/audio/babyshark.wav`, `Ages 2-4`

The UI can filter the catalog by age range:

- `All Ages`
- `Ages 2-4`
- `Ages 5-7`
- `Ages 8+`

To change songs:

1. Add WAV assets under `main/audio/`.
2. Embed via `main/CMakeLists.txt` `EMBED_FILES`.
3. Update the shared catalog in `main/speaker_music.c` and the matching simulator catalog in `pc_sim/music_sequence_block/main.c`.

## Brain Integration Notes

When Brain sends `CMD_EXECUTE`, music playback happens only if:

- speaker initialized successfully
- a song has been confirmed via UI (`Select`)

So the intended child flow is:

1. Kid previews with `Play`
2. Kid confirms with `Select`
3. Brain triggers with `CMD_EXECUTE`

## Status Semantics

- `STATUS_DATA_READY` means a confirmed song selection is available
- `STATUS_BUSY` means playback is in progress
- `STATUS_READY` means the block is idle
- `STATUS_ERROR` is reserved for init/playback failures

After a successful `CMD_EXECUTE`, the block returns to ready and keeps the current selection valid for the next execute.
