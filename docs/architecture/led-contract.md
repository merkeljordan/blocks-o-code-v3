# Unified LED Contract

## Contract

All child blocks follow one LED contract:

- Identity color comes from `led_contract_identity_color()` in `firmware_blocks/include/led_contract.h`.
- Status states are interpreted consistently:
  - `READY`: identity color at idle brightness.
  - `BUSY`: identity color at full brightness.
  - `DATA_READY`: identity color at idle brightness.
  - `ERROR`: red at error brightness.
- Brain `CMD_MATRIX_*` commands are mirrored to both surfaces for matrix-capable blocks:
  - local matrix (`matrix_fill/clear/brightness/show`)
  - status strip (`status_strip_handle_matrix_command`)

## Block Capability Matrix

Source of truth: `led_contract_get_caps()` in `firmware_blocks/include/led_contract.h`.

- `IF`, `THEN`, `END_IF`, `LOOP`, `END_LOOP`, `DELAY`, `BUTTON`, `NOTE`, `LED_FLASH`
  - identity color: yes
  - status strip: yes
  - matrix: yes
  - pattern playback: yes
  - matrix-to-strip mirroring: yes
- `MUSIC_SEQ`
  - identity color: yes
  - status strip: yes
  - matrix: yes
  - pattern playback: yes (song/note playback path)
  - matrix-to-strip mirroring: yes

## Brain -> Child LED Flow

- Brain side uses:
  - `led_contract_supports_brain_mirroring()`
  - `led_contract_identity_color()`
- Render path in `firmware_blocks/brain_block/main/main.c`:
  - `i2c_matrix_fill(addr, r, g, b)`
  - `i2c_matrix_set_brightness(addr, brightness)`
  - `i2c_matrix_show(addr)`

## Validation Checklist (Hardware Regression)

Run for each block type:

1. Boot block, verify `READY` status color (identity, idle brightness).
2. Trigger `CMD_EXECUTE`, verify `BUSY` brightness and return to `READY`.
3. Produce a block-originated event (where applicable), verify `DATA_READY`.
4. Trigger an invalid execute/config path, verify `ERROR` color behavior.
5. Send Brain `CMD_MATRIX_FILL + BRIGHTNESS + SHOW`; verify matrix and strip update consistently.
6. Send Brain `CMD_MATRIX_CLEAR + SHOW`; verify both surfaces clear.
7. For NOTE: verify no stub path is present and active I2C path controls LEDs.
