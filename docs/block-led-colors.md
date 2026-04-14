# Block LED colors (identity contract)

Firmware maps each logical block type to a single **identity** RGB triple. Values are 8-bit per channel `(R, G, B)` in the order used by `led_contract_identity_color()` — the same numbers the Brain sends for matrix fill when showing identity.

**Source of truth:** `firmware_blocks/include/led_contract.h` (`led_contract_identity_color`, `led_contract_status_color`, `led_contract_status_brightness`).

## Identity color by block type

| Block role | `block_type_t` | RGB `(R, G, B)` | Hex (approx.) |
|------------|----------------|-----------------|----------------|
| Brain | `BLOCK_TYPE_BRAIN` | 255, 0, 0 | `#FF0000` |
| If / Then / End if | `BLOCK_TYPE_IF`, `BLOCK_TYPE_THEN`, `BLOCK_TYPE_END_IF` | 0, 180, 60 | `#00B43C` |
| Loop / End loop | `BLOCK_TYPE_LOOP`, `BLOCK_TYPE_END_LOOP` | 0, 40, 255 | `#0028FF` |
| Delay | `BLOCK_TYPE_DELAY` | 255, 60, 0 | `#FF3C00` |
| Button (input) | `BLOCK_TYPE_BUTTON` | 255, 20, 147 | `#FF1493` |
| Note | `BLOCK_TYPE_NOTE` | 255, 220, 0 | `#FFDC00` |
| Music sequence | `BLOCK_TYPE_MUSIC_SEQ` | 0, 210, 170 | `#00D2AA` |
| LED color flash | `BLOCK_TYPE_LED_FLASH` | 180, 70, 255 | `#B446FF` |
| Unknown / other | `default` in switch (e.g. `BLOCK_TYPE_UNKNOWN`) | 32, 32, 32 | `#202020` |

Multiple physical SKUs (e.g. `note_block`, `note_block_2`, `note_block_3`) share one `block_type_t`, so they use the **same** identity row as that type. Address-to-type mapping is in `block_infer_type_from_child_i2c_address()` in `firmware_blocks/include/i2c_protocol.h`.

## Status flags (`led_contract_*` helpers)

On blocks that call `led_contract_status_color()` / `led_contract_status_brightness()` with the I2C `STATUS_*` flags:

| Status mask | Color | Brightness (0–255 scale) |
|-------------|-------|---------------------------|
| `STATUS_ERROR` | Red **255, 0, 0** | **96** |
| `STATUS_BUSY` | Identity | **128** |
| Any other (e.g. ready, `STATUS_DATA_READY`) | Identity | **64** |

Brain **runtime broadcast** on the status strip uses a different mapping (`status_strip_runtime_color()` in `status_strip.c`): for example `BRAIN_RUNTIME_DONE` is green and `BRAIN_RUNTIME_ERROR` / `STOP` is red, independent of block identity.

For matrix/strip capabilities and mirroring, see [architecture/led-contract.md](architecture/led-contract.md).

## WS2812 component order

On boards that define `LED_STRIP_COLOR_COMPONENT_FMT_GRB`, the driver remaps channels for the strip; the **contract** numbers above remain **R, G, B** in code and in this table.
