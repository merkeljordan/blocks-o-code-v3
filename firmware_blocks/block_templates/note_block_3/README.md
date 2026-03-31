# NOTE Block

Block type: `BLOCK_TYPE_NOTE`  
Payload: `note_id` (uint8)

This block lets the user preview and submit either:
- a single note `A` through `G`
- a short custom note sequence

## LED Behavior

For issue `#66`, the note block now maps each explicit note to a unique matrix color during preview, execute, and `CMD_PLAY_NOTE`.

| Note | Color | RGB |
| --- | --- | --- |
| A | Red | `255, 32, 32` |
| B | Orange | `255, 128, 0` |
| C | Yellow | `255, 220, 0` |
| D | Green | `32, 200, 64` |
| E | Cyan | `0, 170, 255` |
| F | Blue | `80, 96, 255` |
| G | Violet | `200, 64, 255` |

The matrix returns to a dim idle glow between notes. Chord blending is not implemented because note playback is currently monophonic.

## Notes

Start here:
- Copy `firmware/esp32/block_templates/common_block` into this folder.
- Update `MY_BLOCK_TYPE` and `MY_ADDRESS` in `main/i2c_comm.c`.
- Add `note_id` config and preview tone on selection.

See `firmware/FRAMEWORK.md` for UX rules and contract details.
