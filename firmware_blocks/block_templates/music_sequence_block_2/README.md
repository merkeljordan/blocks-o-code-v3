# Music Sequence Block (`BLOCK_TYPE_MUSIC_SEQ`)

This folder contains the full firmware template for the **Music Sequence child block**.

## What This Block Does

At runtime, this block lets a child:

1. Open a touchscreen UI.
2. Choose an age range.
3. Browse songs within that age range.
4. Tap `Play` to hear a local preview.
5. Tap `Select` to confirm and submit the current song choice.
6. Wait for the Brain Block to issue `CMD_EXECUTE`.
7. Play the selected song when execute is received.

## LED Behavior

Issue `#66` adds music-linked LED feedback on the block matrix:

- Each selected song runs its own looping animation while audio is playing.
- `CMD_PLAY_NOTE` now maps each note to a unique matrix color.
- The matrix returns to a dim idle glow when playback ends.

Current note-to-color lookup used by the firmware:

| Note | Color | RGB |
| --- | --- | --- |
| A | Red | `255, 32, 32` |
| B | Orange | `255, 128, 0` |
| C | Yellow | `255, 220, 0` |
| D | Green | `32, 200, 64` |
| E | Cyan | `0, 170, 255` |
| F | Blue | `80, 96, 255` |
| G | Violet | `200, 64, 255` |

The current hardware path is monophonic, so overlapping note-color blending is not implemented yet; the latest note color wins.

## Runtime Architecture

Main runtime files:

- `main/main.c`
- `main/tft_ui.c`
- `main/speaker_music.c`
- `components/audio/*`

Execution flow:

1. `app_main()` initializes Arduino compatibility, speaker/audio, and LVGL UI.
2. `tft_ui_start()` creates the intro and song-selection screens, and in firmware builds also starts:
   - `s_action_queue` (UI -> execution task messages)
   - `s_preview_queue` (UI -> preview audio worker)
3. The UI thread handles touch/buttons and pushes song actions.
4. `execution_task` consumes UI actions and updates block state:
   - `MUSIC_UI_ACTION_SONG_CHANGED` -> updates index, marks config invalid
   - `MUSIC_UI_ACTION_SONG_SELECTED` -> marks config valid and sets `STATUS_DATA_READY`
5. On `CMD_EXECUTE`, the block plays the selected song once if config is valid, reports `STATUS_BUSY` during playback, then returns to ready.

The selector screen now includes an age-range filter so songs can be grouped for younger vs older kids without changing the wire payload. The selected config is still just `song_id`.

## Current I2C Status

This template uses an active I2C slave transport in `main/i2c_comm.c`.

- `REG_WHOAMI` and `REG_STATUS` are exposed over the child-bus register map
- `CMD_GET_DATA` returns `music_seq_payload_v1_t { song_id }`
- `CMD_EXECUTE` triggers the currently selected song when the config is valid
- `CMD_RESET` restores startup state

## Brain Block Integration: When Kids Tap Play In The App

For full classroom flow (Companion App `Play` -> Brain -> Music Block):

1. In Brain firmware, ensure Play starts the executor (`brain_executor_start()`).
2. In `brain_event_handler.c`, dispatch to music blocks by address:
   - Find child address(es) where `block_type == BLOCK_TYPE_MUSIC_SEQ`
   - Send `CMD_EXECUTE` (`i2c_execute(address)`)
3. Keep `CMD_GET_DATA` returning `music_seq_payload_v1_t { song_id }` so Brain/app can inspect selected song.
4. Optionally add a `CMD_SET_*` command for Brain-driven song assignment if you want remote overrides.

## Suggested Integration Wiring (Brain Side)

Inside Brain `dispatch_output_action(BLOCK_TYPE_MUSIC_SEQ)`:

1. Query latest scanned block list (`block_config_manager_get_state()`).
2. For each `block_type == BLOCK_TYPE_MUSIC_SEQ`, call `i2c_execute(i2c_address)`.
3. Check return codes and log failures per address.
4. Continue executor even if one child fails (classroom resilience).

## Build Notes

- Main component links LVGL + TFT/touch drivers.
- Main embeds the final 5-song hardware catalog from `main/audio/`:
  - `Espresso`
  - `Birds of a Feather`
  - `Beat It`
  - `Hakuna Matata`
  - `Baby Shark`
- Startup audio is not embedded by this block.
- The desktop preview target lives at `pc_sim/music_sequence_block/`.

## Simulator Notes

The SDL simulator builds the real `main/tft_ui.c` with `MUSIC_SEQ_UI_SIMULATOR=1`.

- `Play` previews locally in the simulator
- `Select` still acts as the submit/confirm step
- song names and selector flow match the real UI
- real firmware audio/I2C execution still lives in the ESP32 build

## File Ownership

- `managed_components/` is third-party/vendor code.
- This template’s project-owned logic is in `main/`, `components/audio/`, and top-level config/docs files.
