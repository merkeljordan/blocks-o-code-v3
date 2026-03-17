## Block UI Simulator (LVGL + SDL on Windows)

This repo can run **LVGL-based block UIs** in a desktop window using **SDL2** (no ESP32 flash needed).

### How simulators are organized in this repo

- **Each simulator lives in**: `pc_sim/<block_name>/`
- **Each simulator builds to**: `build/<block_name>_sim/`

The simulator window should match the physical TFT resolution for 1:1 layout (often **240×320**).

---

## One-time setup (Windows, PowerShell)

### Install vcpkg + SDL2

```powershell
cd $env:USERPROFILE

git clone https://github.com/microsoft/vcpkg.git
& ".\vcpkg\bootstrap-vcpkg.bat"

.\vcpkg\vcpkg install sdl2:x64-windows
```

---

## Build + run

From the repo root (`C:\Projects\spring26\blocks-o-code-v3`), replace `<block_name>` with the simulator folder name under `pc_sim/`.

### Configure (first time, or after changing CMake)

```powershell
cmake -S pc_sim\<block_name> -B build\<block_name>_sim -A x64 `
  -DCMAKE_TOOLCHAIN_FILE="$env:USERPROFILE\vcpkg\scripts\buildsystems\vcpkg.cmake"
```

### Build

```powershell
cmake --build build\<block_name>_sim --config Release
```

### Run

```powershell
& ".\build\<block_name>_sim\Release\<block_name>_sim.exe"
```

---

## When do I need to rebuild?

- If you change any `.c/.h` used by the simulator, **rebuild** (the build is incremental).
- If you change `pc_sim/<block_name>/CMakeLists.txt` or `pc_sim/<block_name>/lv_conf.h`, run **Configure** again, then **Build**.

---

## Troubleshooting

### “Manually-specified variables were not used by the project: CMAKE_TOOLCHAIN_FILE”

This usually means the build folder was configured earlier without vcpkg. Delete it and re-configure:

```powershell
Remove-Item -Recurse -Force build\<block_name>_sim
cmake -S pc_sim\<block_name> -B build\<block_name>_sim -A x64 `
  -DCMAKE_TOOLCHAIN_FILE="$env:USERPROFILE\vcpkg\scripts\buildsystems\vcpkg.cmake"
```

### PowerShell says the `.exe` “is not recognized”

Run it with the call operator:

```powershell
& ".\build\<block_name>_sim\Release\<block_name>_sim.exe"
```

---

## Adding a new block simulator (template)

Create a new folder:

- `pc_sim/<block_name>/`

Minimum files:

- `pc_sim/<block_name>/CMakeLists.txt`
  - Finds SDL2
  - Builds LVGL (from the repo’s LVGL source) as a subdirectory
  - Builds an executable named `<block_name>_sim`
  - Defines a macro like `NOTE_UI_SIMULATOR=1` (or similar) so your embedded UI file can compile without ESP-IDF/FreeRTOS includes
- `pc_sim/<block_name>/main.c`
  - `lv_init()`
  - `lv_sdl_window_create(<hor>, <ver>)`
  - create mouse/keyboard
  - call your block’s `*_ui_start()` (or a UI init function)
  - loop `lv_timer_handler()` + `lv_tick_inc(...)`
- `pc_sim/<block_name>/lv_conf.h`
  - `#define LV_USE_SDL 1`
  - enable the fonts/widgets your UI uses

Tip: keep “UI-only” code (screen creation, styles, callbacks) in files that can be compiled for both targets, and guard hardware bring-up with `#if !defined(<SIMULATOR_MACRO>)`.

