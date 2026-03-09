# THEN Block

Block type: `BLOCK_TYPE_THEN`  
Payload: none (no `CMD_GET_DATA` bytes; this block is a control-flow marker).

## Build & flash

This folder is a minimal ESP-IDF project for the THEN block.

### Prerequisites
- ESP-IDF installed and `IDF_PATH` set.
- Toolchain in `PATH` (e.g. `idf.ps1` / `export.sh` run in this shell).

### Configure and build

From this directory:

```bash
idf.py set-target esp32s3   # or your MCU
idf.py menuconfig           # optional: set serial port, flash size, etc.
idf.py build
```

### Flash and monitor

```bash
idf.py -p COMx flash monitor   # replace COMx with your serial port
```

You should see boot logs like:

- `THEN BLOCK BOOT`
- `I2C slave initialized`
- `Block ready and waiting for commands!`

## Behavior & protocol

- `REG_WHOAMI` returns `BLOCK_TYPE_THEN`.
- `REG_STATUS` reflects the current `STATUS_*` bits.
- `CMD_PING` plays a short OK beep.
- `CMD_GET_STATUS` returns one status byte.
- `CMD_GET_DATA` returns zero bytes (no payload).
- `CMD_EXECUTE` briefly flashes the LED matrix green and beeps.
- `CMD_RESET` clears local state and matrix and returns to `STATUS_READY`.

See `firmware_blocks/FRAMEWORK.md` for full UX rules and contract details and for the common block contract.
