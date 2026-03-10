# Firmware Architecture

## Overview

The ESP32 Brain Block firmware is built using ESP-IDF and serves as the central coordinator for the Blocks o' Code system. It acts as an I2C master, scanning for child blocks and managing their configuration.

## Project Structure

```
firmware_blocks/brain_block/
├── main/
│   ├── main.c                    # Entry point
│   ├── app.c                     # TCP client, Wi-Fi
│   ├── block_config_manager.c    # Configuration management
│   ├── block_config_manager.h
│   ├── brain_event_handler.c     # Validation gate + executor
│   ├── brain_event_handler.h
│   ├── i2c_comm.c                # I2C master communication
│   ├── i2c_protocol.h            # Protocol definitions
│   └── CMakeLists.txt
├── components/                   # Custom components
├── sdkconfig                     # ESP-IDF configuration
└── README.md
```

## Core Components

### 1. Main Entry Point (`main.c`)

**Purpose**: Initialize system and create tasks

**Responsibilities**:
- Initialize I2C master
- Create TCP client task
- Create configuration update task

### 2. TCP Client (`app.c`)

**Purpose**: Manage Wi-Fi and TCP connection

**Responsibilities**:
- Connect to Wi-Fi network
- Establish TCP connection to Flutter app
- Send configuration updates
- Handle connection errors and reconnection

**Key Functions**:
- `wifi_init()`: Initialize Wi-Fi
- `tcp_client_task()`: TCP client task
- `send_config()`: Send configuration JSON

### 3. Block Configuration Manager (`block_config_manager.c`)

**Purpose**: Manage block topology and configuration

**Responsibilities**:
- Track connected blocks via I2C
- Detect configuration changes
- Generate JSON representation
- Maintain device registry

**Key Functions**:
- `block_config_manager_init()`: Initialize manager
- `block_config_manager_update()`: Update from device registry
- `block_config_manager_generate_json()`: Generate JSON
- `block_config_manager_has_changed()`: Check for changes
- `block_config_manager_get_event_map()`: Get derived IF/LOOP/input/output metadata

**Data Structures**:
- Device registry: Map of I2C addresses to block info
- Configuration state: Current block configuration
- Change detection: Tracks additions/removals

### 4. I2C Communication (`i2c_comm.c`)

**Purpose**: I2C master communication

**Responsibilities**:
- Initialize I2C master
- Scan I2C bus for devices
- Read WHOAMI registers
- Communicate with child blocks

**Key Functions**:
- `i2c_master_init()`: Initialize I2C
- `i2c_safe_scan()`: Scan for devices
- `i2c_read_whoami()`: Read block identification

### 5. Event Handler + Executor (`brain_event_handler.c`)

**Purpose**: Enforce execution validity and interpret block sequence flow.

**Responsibilities**:
- Track app-driven validation state (`config_validation` events)
- Expose execution gate (`can_start_execution`)
- Hold scan-derived event map metadata
- Run tick-based executor state machine (`IDLE/RUNNING/WAIT_INPUT/WAIT_DELAY/...`)

**Key Functions**:
- `brain_event_handler_set_config_validation()`
- `brain_event_handler_can_start_execution()`
- `brain_executor_start()`
- `brain_executor_stop()`
- `brain_executor_tick()`

## Task Architecture

### FreeRTOS Tasks

1. **TCP Client Task**
   - Priority: Medium
   - Stack: 4096 bytes
   - Periodically checks for config changes
   - Sends updates via TCP

2. **Executor Tick Task**
   - Priority: Medium
   - Periodic non-blocking execution of interpreter logic
   - Advances control-flow program counter and handles waits

3. **Configuration Update Task**
   - Priority: Low
   - Stack: 2048 bytes
   - Periodically scans I2C bus
   - Updates device registry

## I2C Protocol

### Master Configuration

- **Speed**: 100 kHz (Standard Mode)
- **SDA**: GPIO 21
- **SCL**: GPIO 22
- **Pull-ups**: Internal pull-ups enabled

### Address Range

- **0x08 - 0x15**: Child block addresses
- **0x08**: Brain Block (if used as child)
- **0x09+**: Other child blocks

### WHOAMI Register

Each block has a WHOAMI register that identifies:
- Block ID (e.g., "brain_block", "led_matrix")
- Block type (e.g., "brain_block", "output")
- Version information

## Configuration Detection

### Change Detection Algorithm

1. Scan I2C bus for devices
2. Read WHOAMI from each device
3. Compare with previous scan
4. Detect additions/removals/changes
5. Generate JSON if changes detected

### JSON Generation

The configuration manager generates JSON in this format:

```json
{
  "type": "block_config",
  "timestamp": <unix_timestamp>,
  "config": {
    "total_blocks": <count>,
    "blocks": [
      {
        "index": <position>,
        "i2c_address": <hex_address>,
        "whoami": {
          "block_id": "<id>",
          "block_type": "<type>"
        },
        "connection_order": <order>
      }
    ],
    "errors": [
      {
        "type": "error|warning",
        "message": "<description>"
      }
    ]
  }
}
```

## Error Handling

### I2C Errors

- **Device not responding**: Mark as error, continue scanning
- **Bus error**: Retry with backoff
- **Timeout**: Log and continue

### TCP Errors

- **Connection lost**: Attempt reconnection
- **Send failure**: Retry with backoff
- **Wi-Fi disconnect**: Reconnect Wi-Fi, then TCP
- **Validation state**: Reset to invalid on reconnect until app re-sends `config_validation`

## Memory Management

- **Device Registry**: Static array (max 16 devices)
- **JSON Buffer**: Dynamic allocation (freed after send)
- **Task Stacks**: Configured per task requirements

## Configuration

### Wi-Fi Configuration

Set in `menuconfig` or `sdkconfig`:
- SSID
- Password
- IP address (if static)

### TCP Configuration

Set in `app.c`:
- Server IP address
- Server port (default: 41233)

### I2C Configuration

Set in `i2c_comm.c`:
- GPIO pins
- Clock speed
- Pull-up configuration

## Build System

### ESP-IDF Build

```bash
idf.py set-target esp32
idf.py menuconfig    # Configure
idf.py build         # Build
idf.py flash monitor # Flash and monitor
```

### CMakeLists.txt

- Defines source files
- Links components
- Sets compiler flags

## Debugging

### Serial Output

- **Baud Rate**: 115200
- **Output**: Configuration updates, errors, connection status

### Common Issues

- **Wi-Fi won't connect**: Check credentials
- **TCP won't connect**: Verify server IP and port
- **No blocks detected**: Check I2C wiring and addresses

## Next Steps

- **[Firmware API](../api/firmware-api.md)** - API reference
- **[System Overview](./system-overview.md)** - System architecture
- **[Firmware Setup](../getting-started/firmware-setup.md)** - Setup guide
