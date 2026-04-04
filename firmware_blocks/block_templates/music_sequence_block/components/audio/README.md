# Audio Component (`components/audio`)

Reusable audio component for ESP32 block templates.

## Purpose

Provides a simple C API for:

- speaker init/deinit
- boot sound playback
- success/error beeps
- arbitrary WAV playback from embedded memory
- generated sine-tone playback

## Public API

Header: `audio_speaker.h`

Main functions:

- `speaker_init()`
- `speaker_play_boot_sound()`
- `speaker_play_wav(data, len)`
- `speaker_play_tone(freq_hz, duration_ms)`
- `speaker_beep_ok()`
- `speaker_beep_error()`

## Internal Design

- `SampleSource.h`
  - polymorphic source interface (`sampleRate`, `getFrames`)
- `DACOutput.cpp/.h`
  - I2S writer task that continuously asks the active sample source for frames
- `WAVFileReader.cpp/.h`
  - in-memory WAV parser + frame provider
- `SinWaveGenerator.cpp/.h`
  - generated tone source
- `speaker.cpp`
  - C wrapper that switches active source and implements timing helpers

## Data Flow

1. `speaker_init()` starts DACOutput with silence source.
2. Playback call creates/chooses a source object.
3. Source pointer is installed via `setSampleSource(...)`.
4. Writer task streams frames to I2S DAC.
5. Source is returned to silence after playback.

## I2S Settings

| Setting | Value |
|---------|-------|
| Mode | I2S master TX, built-in DAC |
| Default sample rate (silence/tones) | 44100 Hz (`SPEAKER_DEFAULT_SAMPLE_RATE_HZ`) |
| Music WAV sample rate | 11025 Hz (read from WAV header, applied dynamically) |
| Bit depth | 16-bit (`I2S_BITS_PER_SAMPLE_16BIT`) |
| DAC output pin | GPIO25 (right DAC channel) |

Sample rate is set at `DACOutput::start()` from the initial silence source and updated via
`i2s_set_sample_rates()` in `DACOutput::setSampleSource()` whenever a new source is installed.



- ESP-IDF driver component
- Arduino compatibility layer (`espressif__arduino-esp32`)

## Integration In Another Block

1. Copy this `components/audio/` folder into target block template.
2. Add `audio` to `REQUIRES` in target `main/CMakeLists.txt`.
3. Embed any WAV assets (`EMBED_FILES`) in the component using them.
4. Include `audio_speaker.h` and call the API from app code.
