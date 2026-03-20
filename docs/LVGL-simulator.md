# LVGL Simulator

This branch includes a desktop LVGL simulator for the music sequence block so you can preview the UI without flashing hardware.

## Prereqs

On macOS:

```bash
xcode-select --install
brew install cmake sdl2 pkg-config
```

## Music Sequence Block

From the repo root:

```bash
cmake -S pc_sim/music_sequence_block -B build/music_sequence_block_sim \
  -DCMAKE_PREFIX_PATH="$(brew --prefix)"
cmake --build build/music_sequence_block_sim
./build/music_sequence_block_sim/music_sequence_block_sim
```

The simulator:

- opens a 240x320 window to match the physical TFT
- uses the real `music_sequence_block/main/tft_ui.c`
- plays the real WAV files from `firmware_blocks/block_templates/music_sequence_block/main/audio/`
- lets you test both the UI flow and desktop preview audio before flashing hardware

## Notes

- `Play` is still local preview only.
- `Select` is the submit/confirm action that marks the current song ready for Brain-side execution.
- The simulator is for UI iteration; firmware audio and I2C behavior are still validated in the real block firmware.
