# LED Color Flash Block Architecture

This document explains how firmware modules in `led_color_flash_block` work together, and how this block fits into the Brain-controlled system architecture.

## Role In System

- This block is an **I2C child/slave**.
- The **Brain block** is the global controller/orchestrator.
- This block handles local UI and local hardware execution (TFT, LED matrix, speaker), but Brain remains the authority for multi-block program flow.

## File Map

- `main/main.c`
  - Boot sequence and task creation.
  - Initializes peripherals and modules in dependency order.
- `main/i2c_comm.c`
  - I2C slave transport layer.
  - Routes incoming bytes to register-read path or command path.
- `main/command_handler.c`
  - Command parsing and action execution queue.
  - Owns block runtime status (`READY/BUSY/...`).
- `main/command_handler.h`
  - Public interface between UI/I2C and command/action worker.
- `main/tft_ui.c`
  - LVGL UI flow (intro -> numpad).
  - Converts button taps into async queue requests (preview/execute).

## Boot And Runtime Flow

1. `app_main()` starts in `main.c`.
2. Speaker and LED matrix initialize.
3. I2C slave initializes (`i2c_slave_init()`).
4. Command handler initializes (`command_handler_init()`):
   - Creates action queue.
   - Starts action worker task.
5. TFT UI initializes (`tft_ui_start()`), creating LVGL task.
6. I2C and status tasks start.

## Core/Task Split

Current design intentionally separates UI from timing-heavy LED execution:

- **Core 1**
  - `gui_task` in `tft_ui.c` (LVGL render/input loop)
- **Core 0**
  - `i2c_task` in `i2c_comm.c`
  - `command_action_task` in `command_handler.c`
  - `led_status_task` in `command_handler.c`

Why this split:

- Keeps touch/UI responsive.
- Prevents LED animation delays from blocking UI callbacks.
- Keeps Brain/I2C comms active while local effects run.

## I2C Packet Handling Model

`i2c_task()` handles two packet types:

- **Register read** (`len == 1 && reg < 0x10`)
  - Brain requests a register byte.
  - `REG_STATUS` is dynamic and comes from `command_handler_get_status()`.
- **Command packet**
  - Forwarded to `handle_command(buffer, len)`.
  - `CMD_GET_DATA` returns payload from `get_data_payload()`.

## UI Event Model

In `tft_ui.c`:

- Digit tap:
  - Updates local selected digit.
  - Queues preview action via `command_handler_enqueue_preview()`.
- Submit tap:
  - Queues execute action via `command_handler_enqueue_execute_digit()`.

UI does not directly run blocking LED timing. It only queues requests.

## Action Queue Model

In `command_handler.c`:

- Queue item type: preview digit / execute digit / execute current color_id.
- Worker task (`command_action_task`) consumes queue and performs LED timing logic.
- `current_status` moves to `STATUS_BUSY` while action runs, then back to `STATUS_READY`.

## Brain-Orchestrated Intent

This block is designed to fit Brain authority:

- Child can provide local feedback (preview/UI text).
- Brain can poll status and send execute commands (`CMD_EXECUTE`).
- Child executes deterministically and reports status through register/data paths.

## Practical Debug Checklist

If behavior seems wrong:

1. Confirm I2C address and wiring (`MY_ADDRESS`, SDA/SCL, common ground).
2. Confirm Brain reads `REG_WHOAMI` and `REG_STATUS` correctly.
3. Watch queue-related logs in `command_handler.c`.
4. Verify UI stays responsive while actions run (Core split working).
5. Verify `CMD_EXECUTE` path enqueues and runs action.
