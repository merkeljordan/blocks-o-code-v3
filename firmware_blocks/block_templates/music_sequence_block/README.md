# Music Sequence Block (`BLOCK_TYPE_MUSIC_SEQ`)

This folder contains the full firmware template for the **Music Sequence child block**.

## What This Block Does

At runtime, this block lets a child:

1. Open a touchscreen UI.
2. Browse songs.
3. Tap `Play` to hear a local preview.
4. Tap `Select` to mark a song as the active configuration.
5. Wait for the Brain Block to issue `CMD_EXECUTE`.
6. Play the selected song when execute is received.

## Runtime Architecture

Main runtime files:

- `main/main.c`
- `main/tft_ui.c`
- `main/speaker_music.c`
- `components/audio/*`

Execution flow:

1. `app_main()` initializes Arduino compatibility, speaker/audio, and LVGL UI.
2. `tft_ui_start()` creates:
   - `s_action_queue` (UI -> execution task messages)
   - `s_preview_queue` (UI -> preview audio worker)
3. The UI thread handles touch/buttons and pushes song actions.
4. `execution_task` consumes UI actions and updates block state:
   - `MUSIC_UI_ACTION_SONG_CHANGED` -> updates index, marks config invalid
   - `MUSIC_UI_ACTION_SONG_SELECTED` -> marks config valid and sets `STATUS_DATA_READY`
5. On `CMD_EXECUTE`, the block plays the selected song if config is valid.

## Current I2C Status (Important)

`main/main.c` currently contains an **I2C stub** (`i2c_slave_init()` and `i2c_task()` are placeholders).

That means:

- UI and local preview playback work.
- The command handler logic exists.
- But the block is not yet wired to an active I2C slave driver in this template, so Brain->Child command transport is incomplete until the I2C stub is replaced.

## Brain Block Integration: When Kids Tap Play In The App

For full classroom flow (Companion App `Play` -> Brain -> Music Block):

1. In Brain firmware, ensure Play starts the executor (`brain_executor_start()`).
2. In `brain_event_handler.c`, replace `dispatch_output_action()` logging with real I2C dispatch:
   - Find child address(es) where `block_type == BLOCK_TYPE_MUSIC_SEQ`
   - Send `CMD_EXECUTE` (`i2c_execute(address)`)
3. In this music block, replace the I2C stub with real slave RX handling so `CMD_EXECUTE` reaches `command_handle()`.
4. Keep `CMD_GET_DATA` returning `music_seq_payload_v1_t { song_id }` so Brain/app can inspect selected song.
5. Optionally add a `CMD_SET_*` command for Brain-driven song assignment if you want remote overrides.

## Suggested Integration Wiring (Brain Side)

Inside Brain `dispatch_output_action(BLOCK_TYPE_MUSIC_SEQ)`:

1. Query latest scanned block list (`block_config_manager_get_state()`).
2. For each `block_type == BLOCK_TYPE_MUSIC_SEQ`, call `i2c_execute(i2c_address)`.
3. Check return codes and log failures per address.
4. Continue executor even if one child fails (classroom resilience).

## Build Notes

- Main component links LVGL + TFT/touch drivers.
- Audio component (`components/audio`) embeds `bootupsound.wav`.
- Main embeds song assets from `main/audio/`.

## File Ownership

- `managed_components/` is third-party/vendor code.
- This template’s project-owned logic is in `main/`, `components/audio/`, and top-level config/docs files.
