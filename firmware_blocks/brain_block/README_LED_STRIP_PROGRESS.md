# LED Strip Behavior Progress

## Branch Status
- Current branch: `led-strip-behavior`
- Scope of this pass: Brain-driven idle type-color mapping + execution mirroring, rendered by a child-side status strip module
- Task state: implemented in code
- Validation state: ESP-IDF build and hardware verification still needed

## What Was Implemented
- Added Brain-side `i2c_matrix_show()` helper alongside existing matrix fill/clear helpers
- Added Brain-side `block_type -> RGB` mapping in the Brain executor path
- Added Brain-side refresh logic that:
  - shows per-type colors when idle
  - keeps the same per-type color while using brightness to show executor state
  - uses executor `pc`, with wait states mapped to the previous step
  - only re-sends when scan timestamp, executor state, `pc`, or block count changes
- Added a shared child-side status strip driver at `firmware_blocks/shared_components/status_strip/`
- Reused the existing `CMD_MATRIX_FILL`, `CMD_MATRIX_BRIGHTNESS`, `CMD_MATRIX_SHOW`, and `CMD_MATRIX_CLEAR` command path so no new I2C protocol was added
- Routed child-side matrix commands to the dedicated status strip renderer for supported blocks, leaving local matrix logic available for block-specific visuals
- Brought `music_sequence_block` onto the same strip command path

## Files Touched
- `firmware_blocks/brain_block/main/brain_block.h`
- `firmware_blocks/brain_block/main/i2c_comm.c`
- `firmware_blocks/brain_block/main/main.c`
- `firmware_blocks/shared_components/status_strip/status_strip.h`
- `firmware_blocks/shared_components/status_strip/status_strip.c`
- `firmware_blocks/block_templates/if_block/main/CMakeLists.txt`
- `firmware_blocks/block_templates/if_block/main/command_handler.c`
- `firmware_blocks/block_templates/then_block/main/CMakeLists.txt`
- `firmware_blocks/block_templates/then_block/main/command_handler.c`
- `firmware_blocks/block_templates/end_if_block/main/CMakeLists.txt`
- `firmware_blocks/block_templates/end_if_block/main/command_handler.c`
- `firmware_blocks/block_templates/loop_block/main/CMakeLists.txt`
- `firmware_blocks/block_templates/loop_block/main/command_handler.c`
- `firmware_blocks/block_templates/end_loop_block/main/CMakeLists.txt`
- `firmware_blocks/block_templates/end_loop_block/main/command_handler.c`
- `firmware_blocks/block_templates/delay_block/main/CMakeLists.txt`
- `firmware_blocks/block_templates/delay_block/main/command_handler.c`
- `firmware_blocks/block_templates/buttonpress_block/main/CMakeLists.txt`
- `firmware_blocks/block_templates/buttonpress_block/main/command_handler.c`
- `firmware_blocks/block_templates/buttonpress_block/main/i2c_comm.c`
- `firmware_blocks/block_templates/note_block/main/CMakeLists.txt`
- `firmware_blocks/block_templates/note_block/main/command_handler.c`
- `firmware_blocks/block_templates/led_color_flash_block/main/CMakeLists.txt`
- `firmware_blocks/block_templates/led_color_flash_block/main/command_handler.c`
- `firmware_blocks/block_templates/music_sequence_block/main/CMakeLists.txt`
- `firmware_blocks/block_templates/music_sequence_block/main/main.c`

## Current Code State
- `brain_block.h`
  - includes `esp_err_t i2c_matrix_show(uint8_t address);`
- `i2c_comm.c`
  - `i2c_matrix_show()` sends `CMD_MATRIX_SHOW`
- `main.c`
  - `block_type_supports_led_mirroring()` now includes `BLOCK_TYPE_MUSIC_SEQ`
  - `block_type_led_color()` now includes a music-sequence color
  - `brain_led_idle_brightness()` returns `96`
  - `brain_led_active_brightness()` returns `255`
  - `brain_led_inactive_running_brightness()` returns `48`
  - `brain_led_highlight_index()` maps wait states to the previous logical step
  - `brain_led_refresh_child_blocks()` pushes fill/brightness/show to child blocks
- `status_strip.h` / `status_strip.c`
  - adds a reusable child-side WS2812 status strip layer
  - stores raw RGB values and applies brightness on `show()`, so Brain `fill -> brightness -> show` works correctly
  - exposes `status_strip_handle_matrix_command()` so child blocks can reuse the existing command path with minimal per-block code
- Child command handlers
  - define a per-block `kStatusStripConfig`
  - call `status_strip_handle_matrix_command(...)` before their normal switch
  - clear the strip on `CMD_RESET`
- `music_sequence_block/main/main.c`
  - now handles the same strip commands through `status_strip_handle_matrix_command(...)`

## Supported Block Types / Colors
- `IF` -> `(40, 100, 255)`
- `THEN` -> `(0, 170, 110)`
- `END_IF` -> `(0, 210, 170)`
- `LOOP` -> `(0, 180, 60)`
- `END_LOOP` -> `(120, 220, 80)`
- `DELAY` -> `(255, 170, 0)`
- `BUTTON` -> `(255, 80, 130)`
- `NOTE` -> `(255, 220, 0)`
- `MUSIC_SEQ` -> `(255, 80, 0)`
- `LED_FLASH` -> `(180, 70, 255)`

## Important Behavior Notes
- The original task note referenced `firmware_blocks/shared_components/led_ux/led_ux.c`, but that path was not present in this repo state
- Final implementation is still Brain-controlled, but rendering is now child-side through the shared status strip module
- The Brain still uses the existing matrix command opcodes; the child blocks reinterpret those commands as status-strip updates for task 6
- Current per-block strip defaults are set in child source as compile-time macros
  - most blocks default to `GPIO_NUM_13` / `16` LEDs
  - `led_color_flash_block` defaults to `GPIO_NUM_13` / `30` LEDs
- The status strip GPIO is now aligned with the confirmed strip pin: `GPIO 13`
- Matrix-vs-strip hardware should still be checked on-device during verification
- Local matrix behavior remains in each block firmware for block-specific visuals

## Risks / Caveats
- No ESP-IDF build was run from this environment because build tools were unavailable here
- Hardware behavior is still unverified
- The separate matrix pin is not part of this task-6 strip path and should be validated independently if needed
- `led_color_flash_block` now has separate protocol routing for the status strip, but actual hardware should be checked to ensure the chosen strip GPIO does not overlap with its local effect path
- This is a working implementation pass, not final UX polish

## Suggested Next Steps
1. Build Brain firmware on the local ESP-IDF machine
2. Build and flash the child blocks that should mirror status
3. Verify idle colors appear after scan completes
4. Verify active highlight follows executor `pc`
5. Check wait-state highlighting during delay/input pauses
6. Confirm stop/done returns all child strips to idle brightness

## Hardware Test Notes
- Use Brain plus any flashed child blocks from the supported set above
- Confirm each child strip shows its type color at medium brightness while idle
- Run a multi-step program and verify only the active block goes to full brightness while the others dim
- Include a `music_sequence_block` in one run to verify it now mirrors correctly too
- Test `led_color_flash_block` separately to make sure its local effect engine and status-strip behavior both match the expected wiring

## Assumptions
- This is a private engineer handoff note, not public-facing docs
- The summary covers only task 6 LED strip work, not the earlier Start/Stop TFT task
- The note lives under `firmware_blocks/brain_block/` because the orchestration logic still starts from the Brain side
