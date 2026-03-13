# LED Strip Behavior Progress

## Branch Status
- Current branch: `led-strip-behavior`
- Scope of this pass: Brain-driven idle color mapping + basic execution mirroring
- Validation state: code changes made, hardware/build verification still needed

## What Was Implemented
- Added Brain-side `i2c_matrix_show()` helper alongside existing matrix fill/clear helpers
- Added Brain-side `block_type -> RGB` mapping in the Brain executor path
- Added Brain-side refresh logic that:
  - shows per-type colors when idle
  - highlights one block when executor is active
  - uses executor `pc`, with wait states mapped to the previous step
- Refresh is change-driven: only re-sends when scan timestamp, executor state, `pc`, or block count changes

## Files Touched
- `firmware_blocks/brain_block/main/brain_block.h`
- `firmware_blocks/brain_block/main/i2c_comm.c`
- `firmware_blocks/brain_block/main/main.c`

## Current Code State
- `brain_block.h`
  - added `esp_err_t i2c_matrix_show(uint8_t address);`
- `i2c_comm.c`
  - added `i2c_matrix_show()` to send `CMD_MATRIX_SHOW`
- `main.c`
  - added `led_rgb_t`
  - added `block_type_supports_led_mirroring()`
  - added `block_type_led_color()`
  - added `highlight_led_color()`
  - added `brain_led_highlight_index()`
  - added `brain_led_refresh_child_blocks()`
  - `brain_executor_task()` now calls `brain_led_refresh_child_blocks(cfg, ctx)` after `brain_executor_tick()`

## Important Behavior Notes
- This branch does **not** currently have `firmware_blocks/shared_components/led_ux/led_ux.c`; the task note was stale relative to the repo
- Implementation is Brain-driven over existing I2C matrix commands
- Current mirroring only targets block types that appear to support matrix commands in this repo
- `music_sequence_block` is intentionally not part of this first-pass mirror path
- Highlighting is a simple brighter color, not an animation system

## Risks / Caveats
- No ESP-IDF build was run from this environment because build tools were unavailable here
- Hardware behavior is still unverified
- `led_color_flash_block` may have overlapping local LED behavior, so Brain-driven mirroring should be watched for conflicts during execution
- This is a first pass, not final UX polish

## Suggested Next Steps
1. Build Brain firmware on the local ESP-IDF machine
2. Verify idle colors appear on scanned child blocks
3. Verify active highlight follows executor `pc`
4. Check wait-state highlighting during delay/input pauses
5. Decide whether output blocks should keep Brain mirroring or own their local animations
6. If needed later, add animation/brightness-based highlighting instead of static brighten

## Hardware Test Notes
- Use Brain + any child blocks that support matrix commands
- Confirm idle colors after scan completes
- Run a multi-step program and verify the highlight moves with execution
- Confirm stop/done returns LEDs to idle type colors

## Assumptions
- The file is a private engineer handoff note, not public-facing docs
- The summary covers only the current LED strip work, not the earlier Start/Stop TFT task
- The note lives under `firmware_blocks/brain_block/` because the current implementation is Brain-side
