# Blocks o' Code v3

An end‑to‑end system for **configuring, validating, and visualizing modular I2C “blocks”** using:

- **ESP32 firmware (Brain Block)** that discovers child blocks on an I2C bus and streams configuration / telemetry data
- **14 child blocks** (control flow, input, and output blocks) that connect via I2C and implement programmable behaviors
- **Flutter app** that runs a TCP server, validates block sequences against rules, and provides rich real‑time UI

The system consists of **15 total blocks**: 1 Brain Block (system coordinator) and 14 child blocks that can be arranged in various sequences to create programs. Child blocks include control flow blocks (If, Then, Loop, etc.), input blocks (Button Press), and output blocks (Note, Music Sequence, LED effects, etc.).

This repository contains everything needed for the v3 prototype: firmware for all blocks, desktop/mobile app, and supporting documentation.

---

## 🚀 Quick Start

1. **Set up the Flutter app** (see [App Setup Guide](docs/getting-started/app-setup.md)):
   ```bash
   cd companion_app
   flutter pub get
   flutter run -d windows  # or your target platform
   ```

2. **Set up ESP-IDF and build the Brain Block firmware** (see [Firmware Setup Guide](docs/getting-started/firmware-setup.md)):
   ```bash
   cd firmware_blocks/brain_block
   idf.py set-target esp32
   idf.py menuconfig  # configure Wi‑Fi credentials
   idf.py build
   idf.py flash monitor
   ```

3. **Connect child blocks**: Connect your child blocks to the I2C bus (addresses 0x08-0x15)

4. **Connect and observe**: The ESP32 will connect to your Flutter app and stream block configuration data in real time. The app will validate your block sequence and display any errors or warnings.

For detailed setup instructions, see the [Getting Started Guide](docs/getting-started/overview.md).

---

## 📁 Repository Structure

```
blocks-o-code-v3/
├── firmware_blocks/          # ESP32 firmware blocks (ESP‑IDF projects)
│   ├── brain_block/          # Main Brain Block firmware
│   ├── child_block_1/        # Example child block implementations
│   ├── child_block_2/
│   ├── block_templates/      # Templates for creating new blocks
│   └── FRAMEWORK.md          # Firmware block contract and requirements
├── companion_app/            # Flutter companion application
│   ├── lib/                  # Dart source code
│   └── README.md             # App-specific documentation
├── docs/                     # Comprehensive documentation
│   ├── getting-started/      # Setup guides
│   ├── architecture/         # System and component architecture
│   ├── hardware/             # Block inventory and specifications
│   └── api/                  # API reference
└── README.md                 # This file
```

### Key Components

- **`firmware_blocks/brain_block/`**: Main Brain Block firmware that:
  - Scans the I2C bus (addresses 0x08-0x15) for connected child blocks
  - Maintains a device registry and block configuration
  - Generates newline‑delimited JSON messages
  - Connects as a TCP client to the companion app (port `41233`)
  - Detects configuration changes and streams updates

- **`firmware_blocks/block_templates/`**: Templates for creating child blocks:
  - Control flow blocks (If, Then, End If, Loop, End Loop, Delay)
  - Input blocks (Button Press)
  - Output blocks (Note, Music Sequence, LED Color Flash, Disco Mode)
  - Each template includes I2C slave implementation and required modules

- **`firmware_blocks/child_block_1/`** and **`child_block_2/`**: Example child block implementations

- **`companion_app/`**: Flutter companion application that:
  - Runs a TCP server (default port `41233`)
  - Parses configuration and telemetry JSON messages
  - Validates block configurations using rule logic
  - Visualizes blocks, errors, warnings, and telemetry in real time
  - Supports desktop (Windows/macOS/Linux) and mobile (Android/iOS) platforms

- **`docs/`**: Comprehensive documentation (see [Documentation Index](docs/README.md))
  - `getting-started/`: Setup guides for firmware and app
  - `architecture/`: System and component architecture
  - `hardware/`: Block inventory and hardware specifications
  - `api/`: API reference for firmware and app

---

## 🏗️ System Architecture

The core architecture follows a client-server model with modular I2C blocks:

```text
                    ┌─────────────────┐
                    │  Child Blocks   │
                    │  (I2C Slaves)   │
                    │  ┌───────────┐  │
                    │  │ If, Loop, │  │
                    │  │ Button,   │  │
                    │  │ Output... │  │
                    │  └───────────┘  │
                    └────────┬────────┘
                             │ I2C Bus
                             │ (0x08-0x15)
                    ┌────────▼────────┐
                    │ ESP32 Brain     │
                    │ Block (Master)  │
                    └────────┬────────┘
                             │ TCP (port 41233)
                    ┌────────▼────────┐
                    │  Flutter App    │
                    │  (TCP Server)   │
                    └─────────────────┘
```

### Firmware Components

**ESP32 Brain Block (I2C Master)**:
- Periodically scans I2C addresses (0x08-0x15) for connected child blocks
- Builds an in‑memory configuration of all detected blocks
- Detects configuration changes and sends JSON over TCP
- Maintains device registry and block topology
- Streams telemetry data from connected blocks
- Orchestrates program execution by sending commands to child blocks

**Child Blocks (I2C Slaves)**:
- 14 different block types organized into categories:
  - **Control Flow**: If, Then, End If, Loop, End Loop, Delay
  - **Input**: Button Press
  - **Output**: Note, Music Sequence, LED Color Flash, Disco Mode
- Each block implements the common I2C protocol
- Blocks can be physically arranged in any order on the I2C bus
- Configuration is validated by the Flutter app based on block sequence rules

### Flutter App
- Runs a TCP server (default IPv4 on port `41233`)
- Parses messages into strongly‑typed models (`BlockConfiguration`, `BlockInfo`, etc.)
- Validates configurations with `ConfigurationRules`:
  - Brain Block must be at index 0
  - Valid If/Loop block sequences
  - Sequence isolation rules
- Displays configuration, validation errors/warnings, telemetry, and stress‑test tools
- Real-time visualization with color-coded block categories

For detailed architecture information, see:
- [System Overview](docs/architecture/system-overview.md)
- [Firmware Architecture](docs/architecture/firmware-architecture.md)
- [App Architecture](docs/architecture/app-architecture.md)

---

## 📋 Prerequisites

### General
- Git
- A supported OS (Windows, macOS, or Linux)

### Firmware / ESP32
- ESP‑IDF installed and configured (see [Firmware Setup Guide](docs/getting-started/firmware-setup.md) for guidance)
- Supported ESP32 development board for the Brain Block
- USB cable and basic hardware setup for the block chain (I2C bus)
- Wi‑Fi network access for TCP communication

### Flutter App
- Flutter SDK installed (`flutter doctor` passes)
- Dart SDK (bundled with Flutter)
- A target device:
  - Desktop: Windows/macOS/Linux, or
  - Mobile: Android / iOS (if you want to run on phones/tablets)

---

## 🔧 Firmware: ESP32 Brain Block

The Brain Block firmware is located under `firmware_blocks/brain_block/` and is built with ESP‑IDF.

### Key Responsibilities

- Scan I2C addresses (0x08-0x15) and maintain a registry of connected blocks
- Manage block configuration via `block_config_manager.c`:
  - `block_config_manager_init()` - Initialize the configuration manager
  - `block_config_manager_update()` - Update configuration from device registry
  - `block_config_manager_generate_json()` - Generate JSON representation
  - `block_config_manager_has_changed()` - Check for configuration changes
- Maintain a TCP client connection to the Flutter app:
  - Default **TCP port**: `41233`
  - Sends JSON messages when configuration changes and on initial connection
  - Handles reconnection automatically

### Building and Flashing

Exact commands depend on your local ESP‑IDF setup, but the typical flow is:

```bash
cd firmware_blocks/brain_block
idf.py set-target esp32
idf.py menuconfig      # configure Wi‑Fi credentials and other options
idf.py build
idf.py flash monitor
```

**Important**: Configure Wi‑Fi credentials in `menuconfig` or `app.c` to match your network, and set the server IP address to your Flutter app host machine.

For detailed information, see:
- [Firmware Setup Guide](docs/getting-started/firmware-setup.md)
- [Firmware Architecture](docs/architecture/firmware-architecture.md)
- [Firmware API](docs/api/firmware-api.md)

---

## 🧩 Child Blocks: Control Flow, Input & Output

The Blocks o' Code system includes **14 child blocks** that connect to the Brain Block via I2C bus (addresses 0x08-0x15). Each block implements a common I2C protocol and follows the framework defined in [`FRAMEWORK.md`](firmware_blocks/FRAMEWORK.md).

### Block Categories

#### Control Flow Blocks
These blocks define program structure and flow control:

- **If Block** (`if_block`) - Conditional logic start marker
  - No configuration payload (marker only)
  - Non-touch TFT display
  - Part of: `If → Input → Then → [Output+] → End If` sequence

- **Then Block** (`then_block`) - Conditional logic branch marker
  - No configuration payload (marker only)
  - Non-touch TFT display
  - Separates condition from action in If sequences

- **End If Block** (`end_if_block`) - Conditional logic end marker
  - No configuration payload (marker only)
  - TFT display only
  - Closes If block sequences

- **Loop Block** (`loop_block`) - Loop start with iteration count
  - Configuration: `loop_count` (uint8, 1-99 typical)
  - Numpad input for loop count
  - Part of: `Loop → [Output+] → End Loop` sequence

- **End Loop Block** (`end_loop_block`) - Loop end marker
  - No configuration payload (marker only)
  - TFT display only
  - Closes Loop sequences

- **Delay Block** (`delay_block`) - Program delay/pause
  - Configuration: `delay_ms` (uint16/uint32, milliseconds)
  - Numpad input for delay duration
  - Adds timing control to programs

#### Input Blocks

- **Button Press Block** (`button_press`) - Button input detection
  - Configuration: `button_id` (uint8, 0-9; only one button enabled)
  - Numpad with single active button
  - Features: Debounce and preview on press
  - Used in If sequences to trigger conditional logic

#### Output Blocks
These blocks produce visual or audio output:

- **Note Block** (`note_block`) - Play musical note
  - Configuration: `note_id` (uint8, A-G mapped 0-6)
  - Numpad maps to notes A-G
  - Preview: Plays selected note immediately

- **Music Sequence Block** (`music_sequence_block`) - Play pre-made musical sequence
  - Configuration: `sequence_id` (uint8, pre-made sequence index)
  - Numpad selects sequence index
  - Preview: Plays short clip of chosen sequence

- **LED Color Flash Block** (`led_color_flash_block`) - Flash LED matrix with color
  - Configuration: `color_id` (uint8, map numpad to color)
  - Numpad selects color
  - Preview: Flashes selected color on LED matrix + addressable LEDs

- **Disco Mode Block** (`disco_mode_block`) - Rhythm and LED tempo mode
  - Configuration: `mode_id` (uint8, rhythm + LED tempo mode)
  - Numpad chooses mode
  - Preview: Plays short pattern and LED tempo

### Common Features

All child blocks share:

- **I2C Communication**: Slave mode, responds to standard commands
  - `CMD_PING` - Health check
  - `CMD_GET_STATUS` - Status flags (READY, BUSY, ERROR, DATA_READY)
  - `CMD_GET_DATA` - Return configuration payload
  - `CMD_EXECUTE` - Run configured behavior
  - `CMD_RESET` - Return to idle and clear configuration

- **Shared Peripherals**:
  - **LED Matrix**: Used for disco color flashes and status display
  - **Addressable LEDs**: Used for block type color coding and status
  - **Speaker**: Used for click feedback and sound preview

- **Common UX Rules**:
  - On boot: Short LED matrix flash + beep to indicate readiness
  - While configuring: Show current selection on LED matrix
  - On confirm: Green flash + short positive beep
  - On invalid input: Red flash + short error beep
  - When executing: Show "running" pattern on LED matrix

### Block Templates

Templates for creating new blocks are available in `firmware_blocks/block_templates/`. Each template includes:
- Basic I2C slave implementation
- Block type definition
- Minimal main.c structure
- Required module layout (i2c_comm.c, command_handler.c, led_matrix.c, etc.)

### Example Implementations

- **`child_block_1/`**: Example implementation with LED Matrix
- **`child_block_2/`**: Example implementation with OLED Display

### Building Child Blocks

Child blocks are built similarly to the Brain Block:

```bash
cd firmware_blocks/child_block_1  # or any child block
idf.py set-target esp32
idf.py build
idf.py flash monitor
```

**Important**: Each child block must have a unique I2C address (0x08-0x15) configured in its firmware.

For detailed information about child blocks, see:
- [Block Inventory](docs/hardware/block-inventory.md) - Complete list and specifications
- [Firmware Framework](firmware_blocks/FRAMEWORK.md) - Block contract and requirements
- [Firmware API](docs/api/firmware-api.md) - I2C protocol details

---

## 📱 Flutter App: Block Configuration & Telemetry Viewer

The Flutter app lives at `companion_app/`.

### Features

#### TCP Server
- Listens on port `41233` (configurable in `lib/main.dart`)
- Accepts a single active Brain Block connection at a time
- Handles connection lifecycle and reconnection

#### Block Configuration Visualization
- Shows total block count and a visual list of blocks
- Displays per‑block details:
  - Position/index
  - I2C address (hex format)
  - Block ID and type
  - Connection order
- Color‑codes blocks by category:
  - **Control/System**: Primary color (pink) - Brain Block
  - **Control Flow**: Secondary color (blue) - If, Then, End If, Loop, End Loop
  - **Input**: Orange - Button Press
  - **Output**: Tertiary color (purple) - Disco Mode, Note, Music Sequence, LED Color Flash

#### Validation Rules
The app validates block configurations against these rules:

1. **Brain Block Requirement**: Brain Block must exist and be at index 0
2. **If Block Sequence**: `If → Input → Then → [Output+] → End If`
   - Must have exactly one Input Block and one Then Block
   - Must have at least one Output Block
   - Must end with End If Block
3. **Loop Sequence**: `Loop → [Output+] → End Loop`
   - Must have at least one Output Block
   - Must end with End Loop Block
4. **Sequence Isolation**: If/Loop sequences should not interleave (warning)

Violations are surfaced as **errors** (red) or **warnings** (orange) with clear messages and affected block indices.

#### Additional Tools
- Heartbeat and reconnection tracking
- Telemetry display and counts
- Connection stress‑test utilities
- Hardware error detection and reporting

### Running the App

From the repository root:

```bash
cd companion_app
flutter pub get
```

Then, to run:

- **Desktop (Windows)**:
  ```bash
  flutter run -d windows
  ```

- **Desktop (macOS/Linux)**:
  ```bash
  flutter run -d macos  # or linux
  ```

- **Mobile (Android/iOS)**:
  ```bash
  flutter run -d android  # or ios
  ```

Adjust the target (`-d`) as needed for your environment.

### Typical Usage Flow

1. **Start the Flutter app**
   - From the main menu, navigate to the **Get Started** / configuration screen
   - Confirm the TCP server is listening on port `41233`
   - Note your machine's IP address (displayed in the app)

2. **Set up your block chain**
   - Connect child blocks to the I2C bus (addresses 0x08-0x15)
   - Arrange blocks physically in your desired sequence
   - Ensure each block has a unique I2C address

3. **Power and connect the Brain Block (ESP32)**
   - Ensure Wi‑Fi credentials and server IP in firmware match your Flutter host machine
   - The ESP32 will automatically connect to the app
   - The Brain Block will scan and discover all connected child blocks

4. **Observe configuration in real time**
   - When the ESP32 connects, the app displays:
     - Block topology with visual representation (all detected blocks)
     - Configuration validation results (errors/warnings)
     - Hardware/communication errors (if any)
     - Telemetry data

5. **Experiment with different physical block arrangements**
   - Move/add/remove child blocks on the I2C chain
   - Watch the configuration view and rule violations update live
   - Observe how validation errors change as you modify the block sequence
   - Try different combinations: If sequences, Loop sequences, or mixed arrangements

For detailed information, see:
- [App Setup Guide](docs/getting-started/app-setup.md)
- [App Architecture](docs/architecture/app-architecture.md)
- [App API](docs/api/app-api.md)

---

## 📡 Data Model & JSON Messages

The Brain Block sends newline‑delimited JSON messages over TCP. The main configuration message format:

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

### Supported Block Types

The Flutter app maps firmware string identifiers to enum values:

| Firmware String | Flutter Enum | Category |
|----------------|--------------|----------|
| `brain_block` | `brainBlock` | Control/System |
| `if_block` | `ifBlock` | Control Flow |
| `then_block` | `thenBlock` | Control Flow |
| `end_if_block` | `endIfBlock` | Control Flow |
| `loop_block` | `loopBlock` | Control Flow |
| `end_loop_block` | `endLoopBlock` | Control Flow |
| `button_press` | `buttonPress` | Input |
| `note_block` | `noteBlock` | Output |
| `music_sequence_block` | `musicSequenceBlock` | Output |
| `led_color_flash_block` | `ledColorFlashBlock` | Output |
| `disco_mode_block` | `discoModeBlock` | Output |

For full JSON examples and API reference, see:
- [Firmware API](docs/api/firmware-api.md)
- [App API](docs/api/app-api.md)

---

## 🛠️ Development Notes

### Adding New Block Types

When adding a new block type to the system:

**Firmware**:
- Update `block_config_manager.c` and the device registry logic
- Ensure the block implements the required I2C protocol (see [FRAMEWORK.md](firmware_blocks/FRAMEWORK.md))
- Add WHOAMI register handling for the new block type

**Flutter App**:
- Add the block type to `companion_app/lib/models/block_type.dart`
- Update `companion_app/lib/services/block_config_parser.dart` to parse the new type
- If the block participates in sequences, update validation logic in `companion_app/lib/models/configuration_rules.dart`
- Update UI components in `companion_app/lib/screens/` and `companion_app/lib/widgets/` if needed

**Documentation**:
- Document the new block in `docs/hardware/block-inventory.md`
- Update API documentation if needed

### JSON Format Changes

When changing JSON message formats:
- Update `block_config_manager.c` in firmware
- Update `companion_app/lib/services/block_config_parser.dart`
- Update `companion_app/lib/models/block_configuration.dart`
- Update API documentation in `docs/api/`

### Code Organization

- **Firmware**: Follow ESP-IDF conventions and the framework defined in `FRAMEWORK.md`
- **Flutter App**: Follow Dart/Flutter best practices, organize by feature when possible
- **Documentation**: Keep docs in sync with code changes

---

## 📚 Documentation

Comprehensive documentation is available in the `docs/` directory:

- **[Getting Started](docs/getting-started/)** - Setup guides and overview
- **[Architecture](docs/architecture/)** - System and component design
- **[Hardware](docs/hardware/)** - Block inventory and specifications
- **[API Reference](docs/api/)** - Firmware and app APIs

See the [Documentation Index](docs/README.md) for a complete guide.

---

## 🤝 Contributing

Pull requests and issue reports are welcome! If you are working with a specific hardware set of blocks:

- Document new block types in `docs/hardware/block-inventory.md`
- Update firmware WHOAMI handling and Flutter models/rules accordingly
- Follow the firmware framework defined in `firmware_blocks/FRAMEWORK.md`
- Keep documentation in sync with code changes

---

## 📄 License

License information has not been specified yet. Add your chosen license (e.g., MIT, Apache‑2.0) here and include the corresponding `LICENSE` file in the repository root.
