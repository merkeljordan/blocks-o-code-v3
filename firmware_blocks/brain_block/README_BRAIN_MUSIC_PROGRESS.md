# Brain <-> Music Block Progress (Today)

## What We Were Trying To Prove

We wanted to show this path works:

`Companion App -> Brain Block -> Music Sequence Child Block -> Child executes sound`

We also wanted to measure the Brain-to-child execution latency and compare it to the demo target (`< 50 ms`).

## What Works Now (Big Wins)

### 1. The app talks to the Brain
- The Brain connects to Wi-Fi and the Flutter app TCP server.
- The app sends `config_validation` JSON.
- The Brain receives it and applies it to the executor validation state.

You can see logs like:
- `Applied config_validation from app: valid=true ...`
- `Received heartbeat, sending acknowledgment`

### 2. The Brain can scan the music child block
- The music block is at fixed I2C address `0x10`
- It reports `REG_WHOAMI = 0x31` (`MUSIC_SEQ`) when reads succeed

You can see logs like:
- `Device at 0x10: type=0x31 (MUSIC_SEQ)`

### 3. Brain executor can dispatch to the music child
- `brain_event_handler` now sends a real `i2c_execute()` to the music block
- The music child receives the execute command (now opcode `0x86`, not `0x06`)

You can see logs like:
- Brain: `ACTION music_sequence sequence_id=0 -> addr=0x10`
- Child: `Received 1 bytes: [0]=0x86`

### 4. Music child executes in the correct task (good architecture)
- I2C parsing stays in `i2c_task` (fast path)
- Actual playback runs in `execution_task` (slow/blocking path)

This keeps the child responsive during I2C traffic.

### 5. We measured latency and got a pass
- We added a Brain-side latency measurement (`dispatch -> child STATUS_BUSY`)
- We observed passing samples, including:
  - `45 ms` (PASS)
  - `4 ms` (PASS)

This demonstrates the `< 50 ms` target is achievable in the current setup.

## Important Architecture Improvements Made

### Child-side (`music_sequence_block`)
- Added a real `main/i2c_comm.c` (like the other child block templates)
- Fixed address + type:
  - Address `0x10`
  - Type `BLOCK_TYPE_MUSIC_SEQ`
- `REG_STATUS` now reflects runtime status (`READY/BUSY/ERROR`)
- Coalesced register reads now reply with **1 byte only** (latest register request wins)
  - This reduces stale bytes in the I2C slave TX buffer

### Brain-side
- `device_registry` now owns the WHOAMI read (single WHOAMI read per scan cycle)
- `block_config_manager` reuses the registry WHOAMI result
  - no second WHOAMI read in the same scan cycle
- App `config_validation` JSON is now wired into the Brain event handler
- Command opcodes were moved out of the register range (`<0x10`) to avoid command/register confusion
  - Example: `CMD_EXECUTE` is now `0x86`
- False "second Brain block" prevention:
  - `BLOCK_TYPE_BRAIN` is rejected for child addresses (`0x08-0x15`)

## What Is Still Flaky (Main Remaining Issue)

### I2C scan reliability / response-byte alignment under load

Symptoms we still see:
- Music block sometimes appears as:
  - `music_sequence_block`
  - `unknown`
  - `missing`
- WHOAMI can occasionally be:
  - timeout (`err=263`)
  - fail (`err=-1`)
  - invalid byte (`0x23`, etc.)

What this means:
- The app UI is usually showing the latest Brain JSON correctly
- The unstable part is the Brain <-> child I2C scan/read path, not the app UI logic

## Why the App Sometimes Flickers

The app displays whatever `block_config` JSON the Brain most recently sends.

If a scan cycle gets:
- no response from `0x10` -> app shows `missing`
- bad WHOAMI -> app shows `unknown`
- good WHOAMI (`0x31`) -> app shows `music_sequence_block`

So the flicker is a scan accuracy issue, not an app rendering bug.

## Current Demo Status (What We Can Honestly Demonstrate)

We can demonstrate:
- App connects to Brain
- App sends validation
- Brain applies validation
- Brain detects music child (`0x10`) when scan succeeds
- Brain sends execute command to child
- Child receives execute command (`0x86`)
- Child starts execution / reports busy
- Brain logs latency and can pass `< 50 ms`

We cannot yet claim:
- 100% stable scan/topology detection every cycle

## Next Software Steps (Recommended Order)

### 1. Add a WHOAMI retry in `device_registry` (highest priority)
- If WHOAMI read fails, retry once after a tiny delay (`1-2 ms`)
- Goal: reduce transient `unknown/missing` flips

### 2. Harden child parser for mixed coalesced I2C packets
- Detect mixed register/non-register coalesced packets
- Log and ignore instead of misclassifying
- Goal: avoid weird parser edge cases under bus load

### 3. Add scan-result smoothing for app JSON (demo stability)
- Require 2 consecutive bad scans before marking a known block `missing/unknown`
- Goal: reduce UI flicker from one bad scan

### 4. (Optional demo mode) Skip FW/CAPS metadata reads during scans
- Reduce I2C traffic and bogus FW values caused by noisy reads
- Goal: improve scan stability while demoing

### 5. Add scan reliability counters
- Track:
  - ping success
  - WHOAMI success
  - WHOAMI fail
  - invalid WHOAMI
- Goal: measure progress objectively across changes

## Suggested Team Split (Software)

### One person
- `device_registry` retry + scan counters
- `block_config_manager` 2-scan hysteresis

### Other person
- Child `i2c_comm.c` mixed-packet guard
- log cleanup / demo-mode flags

Then test together and compare:
- app stability
- scan logs
- latency logs

## Key Files Touched Today

### Brain
- `firmware_blocks/brain_block/main/app.c`
- `firmware_blocks/brain_block/main/main.c`
- `firmware_blocks/brain_block/main/device_registry.c`
- `firmware_blocks/brain_block/main/block_config_manager.c`
- `firmware_blocks/brain_block/main/brain_event_handler.c`

### Music child
- `firmware_blocks/block_templates/music_sequence_block/main/i2c_comm.c`
- `firmware_blocks/block_templates/music_sequence_block/main/main.c`
- `firmware_blocks/block_templates/music_sequence_block/speaker.c`

### Shared protocol
- `firmware_blocks/include/i2c_protocol.h`

## Quick Proof Log Checklist (For Demo)

### Brain logs
- `Successfully connected to server`
- `Applied config_validation from app...`
- `Device at 0x10: type=0x31 (MUSIC_SEQ)` (at least once)
- `ACTION music_sequence ... -> addr=0x10`
- `MUSIC_SEQ latency dispatch->BUSY = ... ms`

### Child logs
- `Register 0x00 -> 0x31` (WHOAMI response)
- `Received ... [0]=0x86` (execute command)
- `SPEAKER: Playing preset 'Twinkle' ...`

