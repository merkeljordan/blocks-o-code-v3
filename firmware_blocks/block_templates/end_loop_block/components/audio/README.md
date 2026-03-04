# Audio Component

Boot sound + beeps + WAV playback for blocks. Uses atomic14's event-driven I2S DAC on GPIO25.

## To use in another block

1. **Copy this folder** (`components/audio/`) into your block's `components/` directory.

2. **Add to your main component's CMakeLists.txt:**
   ```
   REQUIRES ... audio
   ```

3. **Call from your code:**
   ```c
   #include "audio_speaker.h"

   speaker_init();
   speaker_play_boot_sound();   // plays bootupsound.wav
   speaker_beep_ok();            // success beep
   speaker_beep_error();         // error beep
   speaker_play_wav(data, len); // play any 16-bit WAV from flash
   speaker_play_tone(440, 100); // 440 Hz for 100 ms
   ```

## Contents

- `audio/bootupsound.wav` - embedded boot sound (16-bit PCM)
- `speaker.cpp` / `audio_speaker.h` - high-level C API wrapping DACOutput
- `DACOutput.cpp/.h` - event-driven I2S DAC output with dedicated FreeRTOS task
- `WAVFileReader.cpp/.h` - robust chunk-walking WAV parser for embedded buffers
- `SinWaveGenerator.cpp/.h` - sine wave tone generator
- `SampleSource.h` - base class for audio sources

## Requirements

- Arduino-ESP32 (`espressif__arduino-esp32`)
- driver (ESP-IDF)
