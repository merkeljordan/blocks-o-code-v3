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

1. **Boot feedback**
   - `speaker_play_boot_sound()` plays embedded `bootupsound.wav`.
2. **Song playback**
   - `speaker_play_song(index)` resolves song asset and calls `speaker_play_wav(...)`.
3. **Tone playback (utility)**
   - `speaker_play_tone(freq, ms)` for beeps and note-based sequences.
4. **Sequence playback (legacy-compatible APIs)**
   - `speaker_play_step(...)`
   - `speaker_play_sequence(...)`

## Timing Model

Playback helpers are currently synchronous/blocking at call site. To keep UI responsive:

- `main/tft_ui.c` uses a dedicated `preview_task` for song preview playback.
- LVGL task only posts preview requests.

## Song Catalog

The current template ships with one example song:

- `main/audio/babyshark.wav`

To add songs:

1. Add WAV assets under `main/audio/`.
2. Embed via `main/CMakeLists.txt` `EMBED_FILES`.
3. Extend `speaker_get_song_count()`, `speaker_get_song_name()`, and `speaker_play_song()` in `main/speaker_music.c`.

## Brain Integration Notes

When Brain sends `CMD_EXECUTE`, music playback happens only if:

- speaker initialized successfully
- a song has been confirmed via UI (`Select`)

So the intended child flow is:

1. Kid previews with `Play`
2. Kid confirms with `Select`
3. Brain triggers with `CMD_EXECUTE`

## Known Gap

This template’s I2C runtime in `main/main.c` is currently a transport stub. Replace with active I2C slave RX logic to complete Brain->Music execution in hardware.
