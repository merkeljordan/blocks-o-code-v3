# Block Configuration System Documentation

## Overview

This document describes the implementation of the block configuration system that enables real-time monitoring and validation of I2C-connected blocks in the Blocks o' Code v3 system. The system consists of:

1. **Firmware (ESP32 Brain Block)**: Scans I2C bus, detects block topology, and sends JSON configuration via TCP
2. **Flutter App**: Receives, parses, validates, and displays block configuration with error detection

## Architecture

```
┌─────────────────┐         TCP (Port 41233)         ┌──────────────────┐
│   ESP32 Brain   │ ────────────────────────────────> │  Flutter App     │
│      Block      │                                   │  (TCP Server)    │
│                 │                                   │                  │
│  - I2C Scanner  │                                   │  - JSON Parser   │
│  - Config Mgr   │                                   │  - Validator     │
│  - JSON Gen     │                                   │  - UI Display    │
└─────────────────┘                                   └──────────────────┘
```

## Components

### 1. Firmware Components

#### Block Configuration Manager (`block_config_manager.h` / `block_config_manager.c`)

**Purpose**: Manages block topology detection, change tracking, and JSON generation.

**Key Functions**:
- `block_config_manager_init()`: Initialize the configuration manager
- `block_config_manager_update()`: Update configuration from device registry and detect changes
- `block_config_manager_generate_json()`: Generate JSON representation of current configuration
- `block_config_manager_has_changed()`: Check if configuration changed since last update

**Features**:
- Tracks all connected blocks via I2C
- Detects when blocks are added, removed, or changed
- Generates JSON in the format expected by Flutter app
- Maintains state between scans

#### Integration with TCP Client (`app.c`)

The TCP client task has been enhanced to:
- Periodically check for configuration changes
- Send JSON configuration when changes are detected
- Send initial configuration on connection establishment

**TCP Connection Details**:
- **Port**: 41233 (configurable in Flutter app)
- **Protocol**: TCP/IP
- **Message Format**: Newline-delimited JSON
- **Connection**: ESP32 connects as client to Flutter app server

### 2. Flutter App Components

#### Block Type Definitions (`lib/models/block_type.dart`)

Defines all 12 supported block types:

**Control/System**:
- `brainBlock` - Brain Block (required, must be first)

**Control Flow**:
- `ifBlock` - If Block
- `thenBlock` - Then Block
- `endIfBlock` - End If Block
- `loopBlock` - Loop Block
- `endLoopBlock` - End Loop Block

**Input**:
- `buttonPress` - Button Press

**Output**:
- `discoModeBlock` - Disco Mode Block
- `noteBlock` - Note Block
- `musicSequenceBlock` - Music Sequence Block
- `ledColorFlashBlock` - LED Color Flash Block

Each block type includes:
- Unique identifier (string)
- Display name
- Category classification
- Helper methods (`isOutput`, `isInput`, `isControlFlow`, etc.)

#### Block Configuration Models (`lib/models/block_configuration.dart`)

**Classes**:
- `BlockConfiguration`: Main container for block topology
  - `totalBlocks`: Total number of detected blocks
  - `blocks`: List of `BlockInfo` objects
  - `errors`: List of hardware/communication errors
  - `timestamp`: When configuration was captured

- `BlockInfo`: Individual block information
  - `index`: Position in configuration
  - `i2cAddress`: I2C address (0x08-0x15)
  - `whoami`: WHOAMI register data
  - `connectionOrder`: Order of connection
  - `blockType`: Parsed block type enum

- `WhoAmIData`: WHOAMI register contents
  - `blockType`: Block type identifier
  - `blockId`: Unique block ID
  - `firmwareVersion`: Firmware version
  - `capabilities`: List of block capabilities

- `ConfigurationError`: Hardware/communication errors
  - `type`: Error type
  - `message`: Error description
  - `blockIndex`: Affected block index
  - `i2cAddress`: Affected I2C address

#### Configuration Rules (`lib/models/configuration_rules.dart`)

Implements validation rules for block configurations:

**Rule 1: Brain Block Requirement**
- Brain Block must be at position 0 (first block)
- Brain Block is always required
- **Severity**: Error

**Rule 2: If Block Sequence**
- Pattern: `If Block → Input Block (Button Press) → Then Block → [Output Block(s)] → End If Block`
- Must have exactly one Input Block and one Then Block
- Must have at least one Output Block
- Must end with End If Block
- **Severity**: Error if sequence is broken

**Rule 3: Loop Block Sequence**
- Pattern: `Loop Block → [Output Block(s)] → End Loop Block`
- Must have at least one Output Block
- Must end with End Loop Block
- **Severity**: Error if sequence is broken

**Rule 4: Sequence Isolation**
- If/Then/End If sequences and Loop/End Loop sequences should not interleave
- **Severity**: Warning

**Classes**:
- `RuleViolation`: Represents a rule violation with severity, message, and affected blocks
- `ConfigurationRules`: Static methods to validate configurations
  - `checkBrainBlockRule()`: Validate Brain Block requirement
  - `checkIfBlockSequences()`: Validate If Block sequences
  - `checkLoopBlockSequences()`: Validate Loop Block sequences
  - `checkSequenceIsolation()`: Check for interleaved sequences
  - `validateAll()`: Run all validations

#### JSON Parser (`lib/services/block_config_parser.dart`)

**Purpose**: Parse block configuration JSON messages from firmware.

**Key Functions**:
- `parseConfig()`: Parse a block configuration message from JSON string
- `isValidConfigMessage()`: Validate JSON structure

**Supported JSON Format**:
```json
{
  "type": "block_config",
  "timestamp": 1234567890,
  "config": {
    "total_blocks": 3,
    "blocks": [
      {
        "index": 0,
        "i2c_address": 8,
        "whoami": {
          "block_type": "brain_block",
          "block_id": "BLOCK_08",
          "firmware_version": "1.0.0",
          "capabilities": []
        },
        "connection_order": 0
      }
    ],
    "errors": []
  }
}
```

#### Configuration Validator (`lib/services/configuration_validator.dart`)

**Purpose**: Validate configurations against rules and provide summaries.

**Key Functions**:
- `validate()`: Validate configuration and return all violations
- `getErrors()`: Get only error-level violations
- `getWarnings()`: Get only warning-level violations
- `isValid()`: Check if configuration is valid (no errors)
- `getSummary()`: Get validation summary with counts

**Returns**:
- `ValidationSummary`: Contains validation results, error/warning counts, and all violations

#### Main App Integration (`lib/main.dart`)

**Changes Made**:

1. **Message Processing** (`_processMessage()`):
   - Added detection for `type: "block_config"` messages
   - Parses configuration JSON using `BlockConfigParser`
   - Validates configuration using `ConfigurationValidator`
   - Updates state with current configuration and violations

2. **State Management**:
   - Added `_currentConfiguration`: Current block configuration
   - Added `_configViolations`: List of rule violations
   - Added `_configParser`: JSON parser instance
   - Added `_configValidator`: Configuration validator instance

3. **UI Updates** (`BlockConfigScreen`):
   - Added `currentConfiguration` and `configViolations` parameters
   - Enhanced UI to display:
     - Block topology with visual representation
     - Block information (type, I2C address, ID, connection order)
     - Configuration validation section (errors/warnings)
     - Hardware error display
     - Color-coded blocks by category

## Data Flow

### Configuration Update Flow

```
1. Device Registry Scan (every 1 second)
   └─> Scans I2C bus (0x08-0x15)
       └─> Reads REG_WHOAMI from each device
           └─> Updates device registry

2. Block Config Manager Update
   └─> Compares current registry with previous state
       └─> Detects changes (added/removed/changed blocks)
           └─> Sets change flag if modified

3. TCP Client Task (periodic check)
   └─> Checks if configuration changed
       └─> Generates JSON if changed
           └─> Sends JSON via TCP to Flutter app

4. Flutter App Receives JSON
   └─> Parses JSON using BlockConfigParser
       └─> Creates BlockConfiguration object
           └─> Validates using ConfigurationValidator
               └─> Updates UI with configuration and violations
```

## JSON Message Format

### Block Configuration Message

```json
{
  "type": "block_config",
  "timestamp": 1234567890,
  "config": {
    "total_blocks": 3,
    "blocks": [
      {
        "index": 0,
        "i2c_address": 8,
        "whoami": {
          "block_type": "brain_block",
          "block_id": "BLOCK_08",
          "firmware_version": "1.0.0",
          "capabilities": []
        },
        "connection_order": 0
      },
      {
        "index": 1,
        "i2c_address": 9,
        "whoami": {
          "block_type": "if_block",
          "block_id": "BLOCK_09",
          "firmware_version": "1.0.0",
          "capabilities": []
        },
        "connection_order": 1
      }
    ],
    "errors": []
  }
}
```

### Block Type Identifiers

The firmware sends block types as string identifiers that map to Flutter enum values:

| Firmware String | Flutter Enum | Display Name |
|----------------|--------------|--------------|
| `brain_block` | `brainBlock` | Brain Block |
| `if_block` | `ifBlock` | If Block |
| `then_block` | `thenBlock` | Then Block |
| `end_if_block` | `endIfBlock` | End If Block |
| `loop_block` | `loopBlock` | Loop Block |
| `end_loop_block` | `endLoopBlock` | End Loop Block |
| `button_press` | `buttonPress` | Button Press |
| `note_block` | `noteBlock` | Note Block |
| `music_sequence_block` | `musicSequenceBlock` | Music Sequence Block |
| `led_color_flash_block` | `ledColorFlashBlock` | LED Color Flash Block |
| `disco_mode_block` | `discoModeBlock` | Disco Mode Block |

## Configuration Validation Rules

### Rule 1: Brain Block Requirement
- **Requirement**: Brain Block must be at position 0 and always present
- **Violation Type**: `brainBlockMissing` or `brainBlockNotFirst`
- **Severity**: Error

### Rule 2: If Block Sequence
- **Pattern**: `If Block → Input Block → Then Block → [Output Block(s)] → End If Block`
- **Requirements**:
  - Exactly one Input Block (Button Press) after If Block
  - Exactly one Then Block after Input Block
  - At least one Output Block after Then Block
  - Must end with End If Block
- **Violation Types**: `ifSequenceIncomplete`, `ifSequenceInvalidBlock`
- **Severity**: Error

### Rule 3: Loop Block Sequence
- **Pattern**: `Loop Block → [Output Block(s)] → End Loop Block`
- **Requirements**:
  - At least one Output Block after Loop Block
  - Must end with End Loop Block
- **Violation Types**: `loopSequenceIncomplete`, `loopSequenceInvalidBlock`
- **Severity**: Error

### Rule 4: Sequence Isolation
- **Requirement**: Sequences should not interleave
- **Violation Type**: `sequenceInterleaved`
- **Severity**: Warning

## UI Features

### Block Configuration Screen

The `BlockConfigScreen` displays:

1. **Connection Status Bar**:
   - Connection indicator (green/red)
   - Connection status text
   - Last heartbeat time
   - Reconnection attempts

2. **Block Configuration Section**:
   - Total block count
   - Visual list of all blocks showing:
     - Block position/index (numbered circle)
     - Block type name
     - I2C address (hex format)
     - Block ID
     - Color-coded by category:
       - **Control/System**: Primary color (pink)
       - **Control Flow**: Secondary color (blue)
       - **Input**: Orange
       - **Output**: Tertiary color (purple)

3. **Configuration Validation Section**:
   - **Errors** (red): Critical rule violations that must be fixed
   - **Warnings** (orange): Non-critical issues
   - Each violation shows:
     - Violation message
     - Affected block index
     - Expected vs actual block type (if applicable)

4. **Hardware Errors Section**:
   - Communication failures
   - Invalid WHOAMI responses
   - Missing blocks

5. **Telemetry Data Section** (existing):
   - Received telemetry messages count
   - Latest telemetry information

6. **Stress Test Section** (existing):
   - Connection stress testing tools

## TCP Communication

### Connection Setup

**Flutter App (Server)**:
- Binds to port 41233 on all interfaces (`InternetAddress.anyIPv4`)
- Listens for incoming TCP connections
- Handles one client at a time (disconnects previous client if new one connects)

**ESP32 (Client)**:
- Connects to Flutter app's IP address on port 41233
- Maintains persistent connection with automatic reconnection
- Sends configuration JSON when changes detected
- Responds to heartbeat messages

### Message Types

1. **Block Configuration** (`type: "block_config"`):
   - Sent when configuration changes
   - Contains full block topology
   - Newline-delimited JSON

2. **Heartbeat** (`type: "heartbeat"`):
   - Sent every 30 seconds from Flutter app
   - ESP32 responds with `type: "heartbeat_ack"`

3. **Telemetry** (existing):
   - Block sensor/telemetry data
   - Parsed by `TelemetryParser`

## Error Detection

### Hardware/Communication Errors (from firmware)

Detected and reported by firmware:
- **Missing blocks**: Previously detected blocks no longer responding
- **Communication errors**: I2C read/write failures
- **Invalid WHOAMI**: Unexpected or corrupted WHOAMI data
- **Address conflicts**: Multiple blocks with same I2C address

### Configuration Rule Violations (validated in app)

Detected and reported by Flutter app:
- **Brain Block violations**: Missing or wrong position
- **If Block sequence violations**: Incomplete or invalid sequences
- **Loop Block sequence violations**: Incomplete or invalid sequences
- **Sequence isolation warnings**: Interleaved sequences

## Files Modified/Created

### Flutter App

**New Files**:
- `lib/models/block_type.dart` - Block type definitions
- `lib/models/block_configuration.dart` - Configuration data models
- `lib/models/configuration_rules.dart` - Validation rules
- `lib/services/block_config_parser.dart` - JSON parser
- `lib/services/configuration_validator.dart` - Configuration validator

**Modified Files**:
- `lib/main.dart` - Added configuration handling and UI updates

### Firmware

**New Files**:
- `firmware/esp32/brain_block/main/block_config_manager.h` - Configuration manager header
- `firmware/esp32/brain_block/main/block_config_manager.c` - Configuration manager implementation

**Modified Files** (to be done):
- `firmware/esp32/brain_block/main/CMakeLists.txt` - Add block_config_manager.c to build
- `firmware/esp32/brain_block/main/app.c` - Integrate configuration manager with TCP client
- `firmware/esp32/brain_block/main/main.c` - Initialize configuration manager

## Usage

### Flutter App

1. Start the app
2. Navigate to "Get Started" from the menu
3. App starts TCP server on port 41233
4. Wait for ESP32 to connect
5. View block configuration in real-time on Block Configuration screen
6. Monitor validation errors/warnings

### ESP32 Firmware

1. Ensure WiFi credentials are configured in `app.c`
2. Ensure server IP and port match Flutter app settings
3. Flash firmware to ESP32
4. ESP32 will:
   - Connect to WiFi
   - Connect to Flutter app TCP server
   - Scan I2C bus every 1 second
   - Send configuration JSON when changes detected

## Testing

### Valid Configurations

- Brain Block only
- Brain Block + complete If Block sequence
- Brain Block + complete Loop Block sequence
- Brain Block + multiple sequences
- Brain Block + mixed If and Loop sequences

### Invalid Configurations (for testing)

- No Brain Block
- Brain Block not at position 0
- Incomplete If Block sequence
- Incomplete Loop Block sequence
- Wrong block types in sequences
- Interleaved sequences

## Future Enhancements

Potential improvements:
- Configuration history/undo
- Export/import configurations
- Block capability detection
- Firmware version checking
- Configuration templates
- Real-time block status monitoring
- Configuration diff visualization

## Troubleshooting

### Flutter App Not Receiving Configuration

1. Check TCP server is running (port 41233)
2. Verify ESP32 is connected to same network
3. Check ESP32 logs for connection errors
4. Verify JSON format matches expected structure

### Configuration Not Updating

1. Check device registry is scanning (every 1 second)
2. Verify block_config_manager_update() is being called
3. Check for I2C communication errors
4. Verify blocks are responding on I2C bus

### Validation Errors Not Showing

1. Verify configuration is being parsed correctly
2. Check validator is being called
3. Verify block types are being parsed correctly
4. Check rule logic matches expected behavior
