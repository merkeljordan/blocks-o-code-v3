# Child Block Firmware Pattern

This summarizes the common pattern used by the `then_block` template so other block templates can follow the same structure.

## File layout

- `main/main.c`: block-specific state, config, and `command_handle(...)`.
- `main/i2c_comm.c`: I²C slave transport that:
  - Exposes `REG_WHOAMI`, `REG_STATUS`, and optional FW version registers.
  - Distinguishes register index bytes (`< 0x10`) from command packets (`CMD_*`).
  - Forwards commands to `command_handle(...)` with optional RX/TX payloads.
- `main/led_matrix.c`: LED matrix driver + helpers (`matrix_fill`, `matrix_clear`, `matrix_show`, brightness).
- `main/speaker.c` + `components/audio`: common speaker backend and UX beeps.

## Required pieces per block

1. **Identity & I²C wiring**
   - Set `MY_ADDRESS` and `MY_BLOCK_TYPE` in `i2c_comm.c`.
   - Initialize the register map with at least:
     - `REG_WHOAMI = MY_BLOCK_TYPE`
     - `REG_STATUS = STATUS_READY`
     - `REG_FW_MAJOR`, `REG_FW_MINOR` (optional but recommended).
   - Implement `i2c_slave_init()` and `i2c_task()` like the THEN or MUSIC_SEQ templates:
     - Use `I2C_PORT_NUM`, `I2C_SDA_PIN`, `I2C_SCL_PIN` from `i2c_protocol.h`.
     - In `i2c_task`, treat pure register-index buffers specially and otherwise call `command_handle(...)`.

2. **Config & status**
   - Define a `block_config_t` in `main.c` that holds any local configuration (may be empty for marker blocks like THEN).
   - Track:
     - `static block_config_t g_config;`
     - `static bool g_config_valid;`
     - `static uint8_t g_status_flags;  // STATUS_* bitfield`
   - Implement:
     - `static void config_reset(void)` that:
       - Resets `g_config` to defaults.
       - Sets `g_config_valid` appropriately.
       - Sets `g_status_flags = STATUS_READY;`
     - Optional helpers:
       - `static bool config_is_valid(void);`
       - `static size_t config_get_payload(uint8_t *out, size_t max_len);`
     - A small accessor used by `i2c_comm.c`:
       - `uint8_t <block>_get_status_flags(void)` to keep `REG_STATUS` in sync.

3. **Peripherals / UX helpers**
   - Provide per-block helpers that encapsulate UX behavior:
     - `peripherals_init()` – initialize speaker, matrix, inputs, etc.
     - `peripherals_boot_feedback()` – startup animation + boot sound.
     - `peripherals_error_feedback()` / `peripherals_ok_feedback()` – UX beeps.
     - `peripherals_show_running()` – what the block displays while executing.
   - Reuse the shared UX rules from `FRAMEWORK.md` (boot flash, configure state, confirm/error flashes).

4. **Command handler**

Implement in `main.c`:

```c
void command_handle(i2c_command_t cmd,
                    const uint8_t *rx, size_t rx_len,
                    uint8_t *tx, size_t *tx_len);
```

- **Always**:
  - Initialize `*tx_len = 0` when `tx_len` is non-NULL.
  - Never block for long periods; keep any animations short or offload to a task.
- **Handle at minimum**:
  - `CMD_PING` – leave block in READY state, optionally play OK beep.
  - `CMD_GET_STATUS` – return one status byte (`g_status_flags`).
  - `CMD_GET_DATA` – return the fixed-length payload for this block (may be zero bytes).
  - `CMD_EXECUTE` – perform the configured action:
    - If config is invalid, set `STATUS_ERROR` and play error feedback.
    - If config is valid, clear `STATUS_ERROR`, optionally set `STATUS_BUSY` while running, and perform the action.
  - `CMD_RESET` – call `config_reset()`, clear transient state/queues, and reset LEDs/peripherals.

5. **Tasks**

- `app_main()` should:
  - Log a clear boot banner (`BLOCK_NAME`).
  - Call `initArduino()` when needed by the board.
  - Initialize peripherals (`peripherals_init()`, `peripherals_boot_feedback()`).
  - Initialize I²C (`i2c_slave_init()`).
  - Create:
    - The I²C receive task (`i2c_task`).
    - Any block-specific worker tasks (e.g., UI or execution tasks).

## Implementation checklist for new blocks

For each new block template (IF, LOOP, DELAY, BUTTON, NOTE, MUSIC_SEQ, LED_FLASH, etc.):

1. **Clone skeleton**
   - Copy `main.c` and `i2c_comm.c` from `then_block` or `music_sequence_block`.
   - Update `BLOCK_NAME`, `MY_ADDRESS`, and `MY_BLOCK_TYPE`.

2. **Define config schema**
   - Fill in `block_config_t` with the fields listed in `FRAMEWORK.md` (e.g., `loop_count`, `delay_ms`, `button_id`, etc.).
   - Implement `config_reset()` and `config_get_payload()` to match the fixed-length payload.

3. **Implement behavior**
   - Fill out `peripherals_*` helpers for this block’s UX.
   - Implement `command_handle()` for:
     - `CMD_GET_DATA` – serialize your config into a flat payload.
     - `CMD_EXECUTE` – run the action (non-blocking when practical).

4. **Wire and test**
   - Confirm `REG_WHOAMI` and `REG_STATUS` via an I²C scanner or the Brain Block.
   - Verify:
     - `CMD_PING` works.
     - `CMD_GET_STATUS` returns expected flags.
     - `CMD_GET_DATA` payload layout matches the doc.
     - `CMD_EXECUTE` and `CMD_RESET` drive the peripherals as expected.

