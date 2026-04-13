# Session Notes — 4/13/26

## What We Were Working On
**Goal:** Get the BUTTON block working in a chain (Brain → BUTTON → NOTE).

## Fixes Applied

### 1. Brain build fix — undeclared variables (brain_event_handler.c)
- **Problem:** `s_executor_active_poll_addr` and `s_dispatch_mutex` were used at line ~1087 but declared at line ~1366.
- **Fix:** Moved both declarations above `dispatch_output_action()` (before line 1017). Left `s_event_queue` in its original location.
- **File:** `brain_block/main/brain_event_handler.c`

### 2. Frame parser added to input blocks (buttonpress + delay)
- **Problem:** Button and delay blocks had a naive `i2c_task` that treated the entire I2C slave FIFO dump as one command. When register reads, pings, and runtime broadcasts coalesced in the FIFO, the block parsed garbage (e.g., `[0]=0x05` was `REG_DATA_LEN` byte, not a command).
- **Fix:** Ported the frame-based parser from the output blocks (music_sequence_block_2). The parser steps through the buffer frame-by-frame using `command_frame_len()` to know each command's expected size, handles register reads (bytes `< 0x10`), and carries over truncated frames.
- **Also added:** `is_command_byte()` function (was missing — caused initial build error).
- **Files:**
  - `block_templates/buttonpress_block/main/i2c_comm.c`
  - `block_templates/delay_block/main/i2c_comm.c`

### 3. TX FIFO flush before every response write (ALL input + output blocks)
- **Problem:** Stale bytes accumulated in the I2C slave TX FIFO. When the brain read `REG_DATA_LEN`, it got a stale value (e.g., 10 instead of 2), then read garbage from `CMD_GET_DATA`. This caused events like `BTN_RX: event_id=0x0A choice=0xFF` (garbage) instead of `event_id=0x04 choice=0x01`.
- **Fix:** Added `i2c_reset_tx_fifo()` call before every `i2c_slave_write_buffer()` — both for register read responses and command responses.
- **Files (8 blocks total):**
  - `block_templates/buttonpress_block/main/i2c_comm.c`
  - `block_templates/delay_block/main/i2c_comm.c`
  - `block_templates/note_block/main/i2c_comm.c`
  - `block_templates/note_block_2/main/i2c_comm.c`
  - `block_templates/note_block_3/main/i2c_comm.c`
  - `block_templates/led_color_flash_block/main/i2c_comm.c`
  - `block_templates/led_color_flash_block_2/main/i2c_comm.c`
  - `block_templates/music_sequence_block/main/i2c_comm.c`
  - `block_templates/music_sequence_block_2/main/i2c_comm.c`

### 4. Debug log added to brain poll task
- **What:** Added `BTN_POLL` log in `block_event_poll_task` (main.c) that prints when button status changes.
- **File:** `brain_block/main/main.c` (~line 135)
- **Note:** This is a temporary debug log — throttled to only print on status change. Can be removed later.

## Current State / Where We're Stuck

### The button block receives CMD_EXECUTE and shows disco animation ✓
### The button block sends correct BTN_TX (event_id=0x04 choice=0x01) ✓
### The brain poll task reads status=0x01 (STATUS_READY) from button — never sees STATUS_DATA_READY ✗

**The button block's STATUS_DATA_READY flag is either:**
1. Never being set (the TFT tap isn't reaching `publish_button_press_event`)
2. Being cleared by a CMD_PING before the brain polls it

**Key suspect:** In `command_handle` (buttonpress_block/main/main.c line 132):
```c
case CMD_PING:
    set_status_flags(g_pending_event.has_event ? STATUS_DATA_READY : STATUS_READY);
    break;
```
Every CMD_PING overwrites status flags entirely. If a ping arrives after the user taps Execute but `has_event` was already consumed or cleared by a near-simultaneous CMD_GET_DATA race, the ping resets status to STATUS_READY.

**Also investigate:** Whether the user is tapping the button block's OWN TFT screen (disco animation with Execute/Skip buttons), not the brain's screen. The button press must come from the button block's local display.

## What Needs Reflashing
- Brain: yes (build fix + debug log)
- Buttonpress block: yes (frame parser + TX flush)
- Note block: yes if testing note submit (TX flush)
- Delay block: only if testing delay chains

## Other Issues From Testing Doc (Not Yet Addressed)
- **Loop count not stopping** — stash lookup may be failing, `s_loop_count_stash_by_addr` not cleared between runs
- **Block ordering race conditions** — 3 race conditions in block_config_manager.c (torn read, debounce order, live JSON reads)
- **LED matrix log spam** — brightness/fill logs flooding on button block (cosmetic)
