# System Overview

## What is Blocks o' Code?

Blocks o' Code is an end-to-end system for **configuring, validating, and visualizing modular I2C "blocks"** that enables physical programming through tangible hardware blocks.

## System Components

The system consists of two main components:

### 1. ESP32 Brain Block (Firmware)
- Scans I2C bus for connected blocks
- Maintains device registry and block configuration
- Generates newline-delimited JSON messages
- Connects as TCP client to the Flutter app
- Located in: `firmware_blocks/brain_block/`

### 2. Flutter Companion App
- Runs TCP server (default port `41233`)
- Parses configuration and telemetry JSON
- Validates configurations using rule logic
- Provides rich real-time UI visualization
- Located in: `companion_app/`

## High-Level Architecture

```
┌─────────────────┐         TCP (Port 41233)         ┌──────────────────┐
│   ESP32 Brain   │ ────────────────────────────────> │  Flutter App     │
│      Block      │                                   │  (TCP Server)    │
│                 │                                   │                  │
│  - I2C Scanner  │                                   │  - JSON Parser   │
│  - Config Mgr   │                                   │  - Validator     │
│  - JSON Gen     │                                   │  - UI Display    │
└─────────────────┘                                   └──────────────────┘
         │
    I2C Bus
```

## Key Features

- **Real-time Block Detection**: Automatically detects blocks connected via I2C
- **Configuration Validation**: Validates block sequences against rules
- **Visual Feedback**: Rich UI showing block topology, errors, and telemetry
- **Connection Management**: Heartbeat monitoring and automatic reconnection
- **Stress Testing**: Built-in tools for testing TCP connection reliability

## Typical Usage Flow

1. **Start the companion app**
   - Navigate to "Get Started" screen
   - TCP server starts listening on port `41233`

2. **Connect Brain Block (ESP32)**
   - Power on the ESP32 Brain Block
   - Ensure Wi-Fi credentials match your network
   - Brain Block connects to companion app automatically

3. **Observe Configuration**
   - App displays detected blocks in real-time
   - Shows validation results (errors/warnings)
   - Displays telemetry data

4. **Experiment**
   - Add/remove/rearrange blocks on I2C chain
   - Watch configuration update live
   - See validation feedback immediately

## Next Steps

- **[Firmware Setup](./firmware-setup.md)** - Set up ESP-IDF and build firmware
- **[App Setup](./app-setup.md)** - Set up Flutter and run the app
- **[System Architecture](../architecture/system-overview.md)** - Learn about system design
