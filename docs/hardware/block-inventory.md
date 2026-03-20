# Block Inventory

This document provides a comprehensive list of all blocks in the Blocks o' Code v3 system.

## System Overview

The Blocks o' Code system consists of **15 total blocks**:
- **1 Brain Block** (system coordinator)
- **14 Child Blocks** (control flow, input, and output blocks)

All blocks communicate via I2C bus at 100 kHz (Standard Mode) using GPIO 21 (SDA) and GPIO 22 (SCL).

## Block Types

### System Block

#### Brain Block (`BLOCK_TYPE_BRAIN` = 0x00)
- **Role**: I2C Master, system coordinator
- **MCU**: ESP32-WROOM
- **Features**:
  - Discovers blocks on I2C bus
  - Maintains device registry
  - Generates JSON configuration
  - Connects to Flutter app via TCP (port 41233)
  - Touch TFT display
  - Wi-Fi connectivity
- **I2C Address**: N/A (acts as master)
- **Location**: `firmware_blocks/brain_block/`

### Control Flow Blocks

#### If Block (`BLOCK_TYPE_IF` = 0x10)
- **Purpose**: Conditional logic start marker
- **Configuration**: None (marker only)
- **Template**: `firmware_blocks/block_templates/if_block/`

#### Then Block (`BLOCK_TYPE_THEN` = 0x11)
- **Purpose**: Conditional logic branch marker
- **Configuration**: None (marker only)
- **Template**: `firmware_blocks/block_templates/then_block/`

#### End If Block (`BLOCK_TYPE_END_IF` = 0x12)
- **Purpose**: Conditional logic end marker
- **Configuration**: None (marker only)
- **Template**: `firmware_blocks/block_templates/end_if_block/`

#### Loop Block (`BLOCK_TYPE_LOOP` = 0x13)
- **Purpose**: Loop start marker with iteration count
- **Configuration**: `loop_count` (uint8, 1-99 typical)
- **Input**: Numpad for loop count
- **Template**: `firmware_blocks/block_templates/loop_block/`

#### End Loop Block (`BLOCK_TYPE_END_LOOP` = 0x14)
- **Purpose**: Loop end marker
- **Configuration**: None (marker only)
- **Template**: `firmware_blocks/block_templates/end_loop_block/`

#### Delay Block (`BLOCK_TYPE_DELAY` = 0x15)
- **Purpose**: Program delay/pause
- **Configuration**: `delay_ms` (uint16 or uint32, milliseconds)
- **Input**: Numpad for delay input (ms or seconds)
- **Template**: `firmware_blocks/block_templates/delay_block/`

### Input Blocks

#### Button Press Block (`BLOCK_TYPE_BUTTON` = 0x20)
- **Purpose**: Button input detection
- **Configuration**: `button_id` (uint8, 0-9; only one button enabled)
- **Input**: Numpad with single active button
- **Features**: Debounce and preview on press
- **Template**: `firmware_blocks/block_templates/buttonpress_block/`

### Output Blocks

#### Note Block (`BLOCK_TYPE_NOTE` = 0x30)
- **Purpose**: Play musical note
- **Configuration**: `note_id` (uint8, A-G mapped 0-6)
- **Input**: Numpad maps to notes A-G
- **Features**: Preview plays selected note immediately
- **Template**: `firmware_blocks/block_templates/note_block/`

#### Music Sequence Block (`BLOCK_TYPE_MUSIC_SEQ` = 0x31)
- **Purpose**: Play pre-made musical sequence
- **Configuration**: `sequence_id` (uint8, pre-made sequence index)
- **Input**: Numpad selects sequence index
- **Features**: Preview plays short clip of chosen sequence
- **Template**: `firmware_blocks/block_templates/music_sequence_block/`

#### LED Color Flash Block (`BLOCK_TYPE_LED_FLASH` = 0x32)
- **Purpose**: Flash LED matrix with selected color
- **Configuration**: `color_id` (uint8, map numpad to color)
- **Input**: Numpad selects color
- **Features**: Preview flashes selected color on LED matrix + addressable LEDs
- **Template**: `firmware_blocks/block_templates/led_color_flash_block/`

#### Disco Mode Block (`BLOCK_TYPE_DISCO` = 0x33)
- **Purpose**: Rhythm and LED tempo mode
- **Configuration**: `mode_id` (uint8, rhythm + LED tempo mode)
- **Input**: Numpad chooses mode
- **Features**: Preview plays short pattern and LED tempo
- **Template**: `firmware_blocks/block_templates/disco_mode_block/`

## Shared Peripherals

All child blocks include:
- **LED Matrix**: Used for disco color flashes and status display
- **Addressable LEDs**: Used for block type color coding and status
- **Speaker**: Used for click feedback and sound preview

## I2C Address Range

- **0x08 - 0x15**: Reserved for child blocks

### Default I2C addresses per template

These are the default slave addresses used by the firmware templates (see each
block's `i2c_comm.c` for the authoritative value):

| Block Type        | Address |
|-------------------|---------|
| LED_FLASH         | 0x08    |
| THEN              | 0x09    |
| END_IF            | 0x0A    |
| LOOP              | 0x0B    |
| END_LOOP          | 0x0C    |
| DELAY             | 0x0D    |
| BUTTON            | 0x0E    |
| NOTE              | 0x0F    |
| IF                | 0x10    |
| MUSIC_SEQ         | 0x12    |

## Block Templates

Templates for creating new blocks are available in `firmware_blocks/block_templates/`. Each template includes:
- Basic I2C slave implementation
- Block type definition
- Minimal main.c structure

See `firmware_blocks/FRAMEWORK.md` for detailed requirements and implementation guidelines.

## Configuration Payloads

Each block returns a fixed-length payload on `CMD_GET_DATA`:

| Block Type | Payload | Size | Notes |
|------------|---------|------|-------|
| IF | none | 0 | Marker only |
| THEN | none | 0 | Marker only |
| END_IF | none | 0 | Marker only |
| LOOP | `loop_count` | 1 byte | 1-99 typical |
| END_LOOP | none | 0 | Marker only |
| DELAY | `delay_ms` | 2-4 bytes | milliseconds |
| BUTTON | `button_id` | 1 byte | 0-9 |
| DISCO | `mode_id` | 1 byte | rhythm + LED tempo mode |
| NOTE | `note_id` | 1 byte | A-G mapped 0-6 |
| MUSIC_SEQ | `sequence_id` | 1 byte | pre-made sequence index |
| LED_FLASH | `color_id` | 1 byte | map numpad to color |

## Common UX Rules

All blocks follow these UX conventions:
- **On boot**: Short LED matrix flash + beep to indicate readiness
- **While configuring**: Show current selection on LED matrix
- **On confirm**: Green flash + short positive beep
- **On invalid input**: Red flash + short error beep
- **When executing**: Show "running" pattern on LED matrix

## Related Documentation

- **[Firmware Framework](../firmware_blocks/FRAMEWORK.md)**: Detailed block contract and requirements
- **[I2C Protocol](../../firmware_blocks/include/i2c_protocol.h)**: Protocol definitions and register map
- **[Brain Block README](../../firmware_blocks/brain_block/README.md)**: Brain Block implementation details
