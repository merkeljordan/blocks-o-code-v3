## Note Block LVGL PC Preview (SDL)

This runs the **Note Block** LVGL UI in a desktop window (no ESP32 flash needed).

### Prereqs (Windows)

You need SDL2 installed in a way CMake can find it.

- **vcpkg** (recommended if you already use CMake + MSVC):
  - Install `sdl2`
  - Then pass `-DCMAKE_TOOLCHAIN_FILE=.../vcpkg.cmake` when configuring

Or:

- **MSYS2**:
  - Install the SDL2 dev package for your toolchain (mingw-w64)

### Build + run

From repo root:

```bash
cmake -S pc_sim/note_block -B build/note_block_sim
cmake --build build/note_block_sim
./build/note_block_sim/note_block_sim
```

### Notes

- The simulator uses `NOTE_UI_SIMULATOR=1` to compile `tft_ui.c` without ESP-IDF hardware code.
- The window is **240x320** so layouts match the physical TFT 1:1.

