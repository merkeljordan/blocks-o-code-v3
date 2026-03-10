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

### Brain ↔ Child I²C + Status/Error Contract (Current v3)
- **Discovery & identity**
  - The Brain scans I²C addresses \(0x08–0x15\) and reads `REG_WHOAMI` to learn each block's `block_type_t`.
  - Blocks that do not answer `REG_WHOAMI` or `CMD_PING` are treated as **absent** and excluded from the configuration.
- **Configuration snapshots**
  - The Brain periodically builds an in‑memory configuration (`block_config_state_t`) and JSON snapshot from the discovered blocks.
  - Any add/remove or identity change invalidates the last config and forces the app to re‑validate.
- **Execution gate (validation first)**
  - The Flutter app sends a `config_validation` JSON message (`is_valid`, `error_count`, `timestamp`) over TCP.
  - Until at least one `config_validation` is received and `is_valid == true`, the Brain will **refuse** to start execution (`START` returns `NAK:NEED_VALIDATION` / `NAK:INVALID_CONFIG`).
  - A topology or identity change (scan detects a different set of blocks) resets the validation state to **invalid**.
- **START/STOP + executor**
  - The app sends newline‑terminated `"START"` / `"STOP"` commands over TCP.
  - On accepted `"START"`, the Brain:
    - Reloads the current block sequence into an internal program array of `block_type_t`.
    - Enters a tick‑based executor loop that walks the sequence and interprets control‑flow (IF/THEN/END_IF, LOOP/END_LOOP, DELAY) and output blocks (LED, NOTE, MUSIC_SEQ, etc.).
  - `"STOP"` always transitions the executor to a stopped state; no further `CMD_EXECUTE` calls are made until a new `"START"`.
- **Child expectations during execution**
  - **Configured vs not configured**:
    - If a block is not locally configured when it receives `CMD_EXECUTE`, it should:
      - Set `STATUS_ERROR` in `REG_STATUS`.
      - Optionally surface a red/error UX locally.
  - **Normal execution**:
    - On `CMD_EXECUTE`, a block runs its configured action and uses `REG_STATUS` flags to report state:
      - Set `STATUS_BUSY` while a non‑trivial action is in progress (e.g. long music sequence).
      - Clear `STATUS_BUSY` when the action completes.
      - Optionally set `STATUS_DATA_READY` when new result data is available for `CMD_GET_DATA` (e.g. BUTTON press).
  - **Status polling from Brain**:
    - For simple actions (NOTE, LED_FLASH, short DELAY), the Brain may not poll `REG_STATUS` and instead just advances.
    - For longer‑running actions (e.g. MUSIC_SEQ), the Brain may:
      - Send `CMD_EXECUTE`.
      - Poll `REG_STATUS` until `STATUS_BUSY` is observed or a timeout elapses (used for latency/debug metrics).
  - **Reset behavior**:
    - On `CMD_RESET`, blocks must return to a known idle state:
      - Clear any internal state machines.
      - Clear `STATUS_BUSY` / `STATUS_ERROR` / `STATUS_DATA_READY` as appropriate.

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
| IF | none | Marker only (control-flow start) |
| THEN | none | Marker only (action branch) |
| END_IF | none | Marker only (control-flow end) |
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

## Canonical Child-Block Pattern (from THEN reference)

Every child block should follow the same high-level structure used by the `then_block` reference implementation:

1. **main.c**
   - Define `BLOCK_TYPE` and fixed I²C address for this block instance.
   - Own the high-level lifecycle:
     - Initialize peripherals (LED matrix, speaker, inputs).
     - Initialize I²C slave (`i2c_slave_init`) and start the I²C task.
     - Show boot UX (matrix animation / boot sound).
   - Provide:
     - A `config_reset()`-style function/logic to reset local configuration.
     - A `peripherals_show_running()`-style function for execution feedback.
2. **i2c_comm.c**
   - Configure I²C in **slave mode** at the chosen address.
   - Maintain a small register array (at least `REG_WHOAMI`, `REG_STATUS`, firmware version).
   - Implement `i2c_task` to:
     - Distinguish simple register reads \(REG_WHOAMI/REG_STATUS/etc.\).
     - Forward non-register requests to a shared `command_handle(...)` function.
     - Refresh dynamic registers (e.g., `REG_STATUS`) before serving reads.
3. **command_handler.c**
   - Implement a `command_handle(i2c_command_t cmd, ...)` switch that:
     - Handles the **required protocol**:
       - `CMD_PING` (keep `STATUS_READY`, optional positive UX).
       - `CMD_GET_STATUS` \(\(1 byte\)\).
       - `CMD_GET_DATA` \(0–N bytes of payload depending on block type\).
       - `CMD_EXECUTE` \(perform configured action; update status/UX\).
       - `CMD_RESET` \(clear config + status\).
     - Owns a small config struct \(e.g., `loop_count`, `note_id`, `color_id`\) and a `config_is_valid()` check.
   - Track a single `current_status` byte backed by `STATUS_*` flags:
     - `STATUS_READY` when idle and configured.
     - `STATUS_ERROR` on invalid config or internal failure.
     - `STATUS_BUSY` while executing long-running actions.
     - `STATUS_DATA_READY` when block-originated data is available.
4. **Peripherals (e.g., led_matrix.c, speaker.c, input.c)**
   - Wrap hardware details behind a tiny API:
     - LED: `matrix_fill`, `matrix_clear`, `matrix_show`, `matrix_set_brightness`.
     - Speaker: `speaker_init`, short boot/OK/error sounds, previews.
     - Input: numpad/button helpers as needed.
   - Keep UX consistent with the rules in this framework.

When creating a new block, start from the `then_block` directory as a template, rename symbols appropriately, and ensure the above structure and contract are preserved.

## Template roadmap (beyond this sprint)

- **IF / THEN / END_IF**
  - **Behavior**: pure control-flow markers; no payload; provide clear UI prompts and confirmation flashes.
  - **Peripherals**: TFT for IF/END_IF; LED matrix and speaker for feedback only.
  - **Complexity**: **S** (THEN is implemented as reference; IF/END_IF mirror the same pattern without payload).
- **LOOP / END_LOOP**
  - **Behavior**: LOOP captures `loop_count` via numpad or UI; END_LOOP is a marker. Brain uses these to wrap sequences.
  - **Config**: `loop_count` (uint8) in `CMD_GET_DATA` payload; END_LOOP has none.
  - **Peripherals**: Numpad/TFT for LOOP entry; LED matrix previews remaining loops when executing.
  - **Complexity**: **M** (simple state machine + numeric input UX).
- **DELAY**
  - **Behavior**: waits for configured delay on `CMD_EXECUTE` without blocking I²C (use task or timer).
  - **Config**: `delay_ms` (uint16/uint32) from numpad or remote `CMD_SET_DELAY`.
  - **Peripherals**: LED matrix countdown/progress bar; speaker tick at completion.
  - **Complexity**: **M** (timer-based execution + UX).
- **BUTTON**
  - **Behavior**: waits for a single button press while Brain executor is in `WAIT_INPUT`; sends back event via `CMD_GET_DATA` / `STATUS_DATA_READY`.
  - **Config**: `button_id` (uint8) for the active input; debounced and previewed on press.
  - **Peripherals**: Numpad, LED matrix highlight for selected button, speaker click.
  - **Complexity**: **M/L** (input handling, debouncing, event reporting).
  - **Implementation notes**:
    - Reuse the canonical pattern: `main.c` + `i2c_comm.c` + `command_handler.c`.
    - Implement a small state machine that tracks when a press has been detected and sets `STATUS_DATA_READY` until the Brain reads and clears it.
- **NOTE**
  - **Behavior**: plays the selected note once on `CMD_EXECUTE` and on local preview.
  - **Config**: `note_id` (uint8) A–G mapped 0–6 in payload.
  - **Peripherals**: Speaker playback, LED matrix music-note pattern.
  - **Complexity**: **M** (audio tone mapping + simple UI).
  - **Implementation notes**:
    - Model closely after `then_block` with an added `note_id` config field returned via `CMD_GET_DATA`.
    - Provide a preview mode that plays the note locally without involving the Brain.
- **MUSIC_SEQ**
  - **Behavior**: selects and plays a pre-baked song; already has a richer reference implementation.
  - **Config**: `sequence_id` / `song_id` (uint8) in payload.
  - **Peripherals**: TFT UI, speaker, optional LED choreography.
  - **Complexity**: **L** (streaming audio, UI, and execution task).
  - **Implementation notes**:
    - Use the existing audio component under `then_block/components/audio` as a shared library.
    - Ensure long-running playback correctly reports `STATUS_BUSY` while audio is in progress.
- **LED_FLASH**
  - **Behavior**: flashes the selected color pattern on `CMD_EXECUTE` and sets `STATUS_DATA_READY` when a color is confirmed.
  - **Config**: `color_id` (uint8) mapped from numpad.
  - **Peripherals**: LED matrix + strip patterns; simple color picker UI.
  - **Complexity**: **M** (pattern generation + simple state).
  - **Implementation notes**:
    - Reuse the LED matrix helpers from `then_block`.
    - Keep patterns short and non-blocking; use a small state machine if animations span multiple frames.

## Execution Rules (Simple)
- A block executes its **configured action** when `CMD_EXECUTE` is received.
- If a block is not configured, return `STATUS_ERROR` and flash red.
- Execution should be non-blocking where possible (use a task or timer).

## Notes for the Firmware Developer
- Keep each block simple: input -> config -> execute.
- Use the shared `i2c_protocol.h` enums and status flags.
- If a behavior is unclear, default to: "configure locally, execute on CMD_EXECUTE".
