# Firmware Block Framework

## Purpose
This document defines the minimum contract and implementation checklist for every block firmware so new blocks can be built consistently and quickly. It is intentionally simple and aligned with the current I2C protocol in `firmware/esp32/include/i2c_protocol.h`.

## System Model (Short)
- The Brain Block is I2C master and discovers blocks by reading `REG_WHOAMI`.
- Each block exposes a local configuration (via TFT or numpad).
- When the Brain starts a program, it pulls each block's config and then sends execute commands in order.
- Every block has: LED matrix, addressable LEDs, and a speaker.

## Common Block Contract (All Child Blocks)
### I2C Identity + Status (Required)
- `REG_WHOAMI` returns the correct `block_type_t`.
- `REG_STATUS` returns status flags (`STATUS_READY`, `STATUS_BUSY`, `STATUS_ERROR`, `STATUS_DATA_READY`).
- `REG_FW_MAJOR`, `REG_FW_MINOR` are recommended.
- `REG_CAPS` is optional for future capability discovery (can return `0x00` for now).

### Required Commands (Handle at Minimum)
- `CMD_PING` - respond OK (no side effects).
- `CMD_GET_STATUS` - return status byte.
- `CMD_GET_DATA` - return the block's current configuration payload.
- `CMD_EXECUTE` - run the configured behavior.
- `CMD_RESET` - return to idle and clear configuration.

### Shared Peripherals (Every Block)
- **LED matrix**: used for disco color flashes
- **Addressable LEDs**: used for block type color coding and status.
- **Speaker**: used for click feedback and sound preview.

### Common UX Rules
- On boot: short LED matrix flash + beep to indicate readiness.
- While configuring: show the current selection on the LED matrix.
- On confirm: green flash + short positive beep.
- On invalid input: red flash + short error beep.
- When executing: show a "running" pattern on the LED matrix.

## Suggested Module Layout (Per Block)
Keep it minimal and consistent with existing projects.
```
main/
  main.c              // init only, create tasks
  i2c_comm.c          // I2C slave, RX/TX buffers
  command_handler.c   // parse commands, call actions
  led_matrix.c        // shared LED matrix driver
  addr_leds.c         // addressable LED strip driver
  speaker.c           // tone / simple playback
  input.c             // numpad or TFT driver (if needed)
  config.c            // store current configuration
```

## Configuration Payloads (Simple and Fixed-Length)
Each block returns a small payload on `CMD_GET_DATA`. Keep it fixed-length per block type.

| Block Type | Payload | Notes |
|---|---|---|
| IF | none | Marker only |
| THEN | none | Marker only |
| END_IF | none | Marker only |
| LOOP | `loop_count` (uint8) | 1-99 typical |
| END_LOOP | none | Marker only |
| DELAY | `delay_ms` (uint16 or uint32) | milliseconds |
| BUTTON | `button_id` (uint8) | 0-9; only one button enabled |
| DISCO | `mode_id` (uint8) | rhythm + LED tempo mode |
| NOTE | `note_id` (uint8) | A-G mapped 0-6 |
| MUSIC_SEQ | `sequence_id` (uint8) | pre-made sequence index |
| LED_FLASH | `color_id` (uint8) | map numpad to color |

## Block-Specific Requirements

### Brain Block (already started to implement)
- Discovers blocks, builds JSON configuration, handles events.
- Touch TFT, wireless link to app, program orchestration.

### If / Then / EndIf Blocks (Control Flow)
- Non-touch TFT.
- Provide simple UI prompts and confirmation flashes.
- No config payload required.

### Loop Block
- Numpad for loop count.
- `CMD_SET_LOOP` may be used to set remotely (optional).
- Return `loop_count` on `CMD_GET_DATA`.

### EndLoop Block
- TFT for display only.
- No config payload required.

### Delay Block
- Numpad for delay input (ms or seconds).
- Return `delay_ms` on `CMD_GET_DATA`.
- `CMD_SET_DELAY` may be used to set remotely (optional).

### ButtonPress Block
- Numpad input with only one active button.
- Return `button_id` on `CMD_GET_DATA`.
- Should debounce and preview on press.

### Disco Mode Block
- Numpad chooses mode.
- Preview by playing a short pattern and LED tempo.
- Return `mode_id` on `CMD_GET_DATA`.

### Note Block
- Numpad maps to notes A-G.
- Preview: play the selected note immediately.
- Return `note_id` on `CMD_GET_DATA`.

### Music Sequence Block
- Numpad selects a sequence index.
- Preview: short clip of the chosen sequence.
- Return `sequence_id` on `CMD_GET_DATA`.

### LED Color Flash Block
- Numpad selects color.
- Preview: flash selected color on LED matrix + addressable LEDs.
- Return `color_id` on `CMD_GET_DATA`.

## Execution Rules (Simple)
- A block executes its **configured action** when `CMD_EXECUTE` is received.
- If a block is not configured, return `STATUS_ERROR` and flash red.
- Execution should be non-blocking where possible (use a task or timer).

## Notes for the Firmware Developer
- Keep each block simple: input -> config -> execute.
- Use the shared `i2c_protocol.h` enums and status flags.
- If a behavior is unclear, default to: "configure locally, execute on CMD_EXECUTE".
