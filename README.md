## Blocks o' Code v3

An end‑to‑end system for **configuring, validating, and visualizing modular I2C “blocks”** using:

- **ESP32 firmware (Brain Block)** that discovers blocks on an I2C bus and streams configuration / telemetry data.
- **Flutter app** that runs a TCP server, validates block sequences against rules, and provides rich real‑time UI.

This repository contains everything needed for the v3 prototype: firmware, desktop/mobile app, and supporting docs.

---

## Repository Structure

- **`firmware_blocks/`**: ESP32 firmware blocks (ESP‑IDF projects) including:
  - **`brain_block/`**: Main Brain Block firmware that:
    - Scans the I2C bus for connected blocks
    - Maintains a device registry and block configuration
    - Generates newline‑delimited JSON messages
    - Connects as a TCP client to the companion app
  - **`child_block_1/`**, **`child_block_2/`**: Example child block implementations
  - **`block_templates/`**: Templates for creating new blocks
- **`companion_app/`**: Flutter companion application that:
  - Listens for TCP connections from the Brain Block (default port `41233`)
  - Parses configuration and telemetry JSON
  - Validates configurations using rule logic
  - Visualizes blocks, errors, warnings, and telemetry
- **`docs/`**: Comprehensive documentation (see [docs/README.md](docs/README.md))
  - `getting-started/`: Setup guides for firmware and app
  - `architecture/`: System and component architecture
  - `development/`: Contributing, code style, and testing guides
  - `hardware/`: Block inventory and hardware specifications
  - `api/`: API reference for firmware and app
- **`scripts/`** and **`tools/`**: Reserved for helper scripts and utilities (currently minimal/placeholder).

For detailed documentation, see the [Documentation Index](docs/README.md).

---

## System Architecture (High‑Level)

The core architecture is:

```text
ESP32 Brain Block (firmware)  ──TCP (port 41233)──▶  Flutter App (TCP server + UI)
           │                                           │
        I2C Bus                                     Desktop
```

- **Firmware (ESP32 Brain Block)**:
  - Periodically scans I2C addresses for blocks.
  - Builds an in‑memory configuration of all detected blocks.
  - Detects configuration changes and sends JSON over TCP.

- **Flutter App**:
  - Runs a TCP server (default IPv4 on port `41233`).
  - Parses messages into strongly‑typed models (`BlockConfiguration`, `BlockInfo`, etc.).
  - Validates configurations with `ConfigurationRules` (e.g., Brain Block at index 0, valid If/Loop sequences).
  - Displays configuration, validation errors/warnings, telemetry, and stress‑test tools.

Details of the JSON formats, rule set, and data flow are documented in `docs/getting-started/firmware-setup.md` and `docs/api/firmware-api.md`.

---

## Getting Started

### Prerequisites

- **General**
  - Git
  - A supported OS (Windows, macOS, or Linux)

- **Firmware / ESP32**
  - ESP‑IDF installed and configured (see [Firmware Setup Guide](docs/getting-started/firmware-setup.md) for guidance).
  - Supported ESP32 development board for the Brain Block.
  - USB cable and basic hardware setup for the block chain (I2C bus).

- **Flutter App**
  - Flutter SDK installed (`flutter doctor` passes).
  - Dart SDK (bundled with Flutter).
  - A target device:
    - Desktop: Windows/macOS/Linux, or
    - Mobile: Android / iOS (if you want to run on phones/tablets).

---

## Firmware: ESP32 Brain Block

The Brain Block firmware is located under `firmware_blocks/brain_block/` and is built with ESP‑IDF.

### Key Responsibilities

- Scan I2C addresses and maintain a registry of connected blocks.
- Manage block configuration via `block_config_manager.c`:
  - `block_config_manager_init()`
  - `block_config_manager_update()`
  - `block_config_manager_generate_json()`
- Maintain a TCP client connection to the Flutter app:
  - Default **TCP port**: `41233`
  - Sends JSON messages when configuration changes and on initial connection.

### Building and Flashing (Conceptual)

Exact commands depend on your local ESP‑IDF setup, but the typical flow is:

```bash
cd firmware_blocks/brain_block
idf.py set-target esp32
idf.py menuconfig      # configure Wi‑Fi credentials and other options
idf.py build
idf.py flash monitor
```

Refer to the [Firmware Architecture](docs/architecture/firmware-architecture.md) and [Firmware API](docs/api/firmware-api.md) documentation for detailed information.

---

## Flutter App: Block Configuration & Telemetry Viewer

The Flutter app lives at `companion_app/`.

### Features

- **TCP Server**
  - Listens on port `41233` (see `serverPort` in `lib/main.dart`).
  - Accepts a single active Brain Block connection at a time.

- **Block Configuration Visualization**
  - Shows total block count and a visual list of blocks.
  - Displays per‑block details:
    - Position/index
    - I2C address
    - Block ID and type
    - Connection order
  - Color‑codes blocks by category (control/system, control flow, input, output).

- **Validation Rules**
  - Brain Block must exist and be at index 0.
  - If‑Block sequences: `If → Input → Then → [Output+] → End If`.
  - Loop sequences: `Loop → [Output+] → End Loop`.
  - Sequence isolation: avoids interleaved If/Loop sequences.
  - Violations are surfaced as **errors** or **warnings** with clear messages and affected indices.

- **Additional Tools**
  - Heartbeat and reconnection tracking.
  - Telemetry display and counts.
  - Connection stress‑test utilities.

### Running the App

From the repository root:

```bash
cd companion_app
flutter pub get
```

Then, to run:

- **Desktop (example: Windows)**:

```bash
flutter run -d windows
```

- **Android (with a device/emulator connected)**:

```bash
flutter run -d android
```

Adjust the target (`-d`) as needed for your environment.

### Typical Usage Flow

1. **Start the Flutter app.**
   - From the main menu, navigate to the **Get Started** / configuration screen.
   - Confirm the TCP server is listening on port `41233`.
2. **Power and connect the Brain Block (ESP32).**
   - Ensure Wi‑Fi credentials and server IP in `app.c` match your Flutter host machine.
3. **Observe configuration in real time.**
   - When the ESP32 connects, the app displays:
     - Block topology
     - Configuration validation results
     - Any hardware/communication errors
4. **Experiment with different physical block arrangements.**
   - Move/add/remove blocks on the I2C chain.
   - Watch the configuration view and rule violations update live.

---

## Data Model & JSON Messages

The Brain Block sends newline‑delimited JSON messages. The main configuration message has:

- `type: "block_config"`
- `timestamp`: capture time
- `config`:
  - `total_blocks`
  - `blocks[]` (each with index, I2C address, WHOAMI data, connection order)
  - `errors[]` (hardware/communication issues)

The Flutter app maps firmware string identifiers (e.g., `brain_block`, `if_block`, `loop_block`) to enum values in `block_type.dart` and validates sequences using `configuration_rules.dart`.

For full JSON examples and API reference, see the [API Documentation](docs/api/).

---

## Development Notes

- **Firmware**
  - Keep `block_config_manager.c` and the device registry logic in sync with any new block types.
  - When changing JSON formats, also update:
    - `companion_app/lib/services/block_config_parser.dart`
    - `companion_app/lib/models/block_configuration.dart`

- **Companion App**
  - New block types should be added to:
    - `companion_app/lib/models/block_type.dart`
    - Validation logic in `companion_app/lib/models/configuration_rules.dart` (if they participate in sequences).
  - UI changes for configuration/telemetry live in `companion_app/lib/screens/` and `companion_app/lib/widgets/`.

---

## Contributing

Pull requests and issue reports are welcome. If you are working with a specific hardware set of blocks:

- Document new block types in `docs/hardware/block-inventory.md`.
- Update firmware WHOAMI handling and Flutter models/rules accordingly.

---

## License

License information has not been specified yet. Add your chosen license (e.g., MIT, Apache‑2.0) here and include the corresponding `LICENSE` file in the repository root.
