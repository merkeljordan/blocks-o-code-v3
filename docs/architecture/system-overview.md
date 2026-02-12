# System Architecture Overview

## High-Level Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                         Blocks o' Code v3                       │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  ┌──────────────────┐              ┌──────────────────┐       │
│  │  ESP32 Brain     │              │  Flutter App     │       │
│  │  Block           │              │  (Desktop/Mobile)│       │
│  │                  │              │                  │       │
│  │  ┌────────────┐  │              │  ┌────────────┐  │       │
│  │  │ I2C Master │  │              │  │ TCP Server │  │       │
│  │  └─────┬──────┘  │              │  └─────▲──────┘  │       │
│  │        │          │              │        │         │       │
│  │  ┌─────▼──────┐  │              │  ┌─────▼──────┐  │       │
│  │  │ Config Mgr │  │              │  │ JSON Parser│  │       │
│  │  └────────────┘  │              │  └─────┬──────┘  │       │
│  │                  │              │        │         │       │
│  │  ┌────────────┐  │              │  ┌─────▼──────┐  │       │
│  │  │ TCP Client │──┼──────────────┼─▶│ Validator │  │       │
│  │  └────────────┘  │   TCP/IP     │  └─────┬──────┘  │       │
│  │                  │   Port 41233 │        │         │       │
│  └──────────────────┘              │  ┌─────▼──────┐  │       │
│         │                           │  │ UI Display │  │       │
│         │ I2C Bus                   │  └────────────┘  │       │
│         │                           │                  │       │
│  ┌──────▼──────┐                    └──────────────────┘       │
│  │ Child Blocks│                                               │
│  │ (I2C Slaves)│                                               │
│  └─────────────┘                                               │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

## Component Responsibilities

### ESP32 Brain Block (Firmware)

**Role**: I2C Master and TCP Client

**Responsibilities**:
- Scan I2C bus for connected blocks
- Maintain device registry
- Generate block configuration JSON
- Send configuration updates via TCP
- Handle Wi-Fi connectivity
- Manage TCP connection lifecycle

**Key Components**:
- `block_config_manager.c`: Configuration management
- `i2c_comm.c`: I2C master communication
- `app.c`: TCP client and Wi-Fi setup
- `main.c`: Application entry point

### Flutter App

**Role**: TCP Server and UI

**Responsibilities**:
- Listen for TCP connections from Brain Block
- Parse incoming JSON messages
- Validate block configurations
- Display real-time visualization
- Manage connection state
- Handle reconnection logic

**Key Components**:
- `ConnectionProvider`: TCP server and connection state
- `BlockConfigProvider`: Configuration and telemetry state
- `TcpServerService`: TCP server management
- `BlockConfigParser`: JSON parsing
- `ConfigurationValidator`: Rule validation

## Data Flow

### 1. Block Detection Flow

```
I2C Scan → Device Registry → Config Manager → JSON Generation → TCP Send
```

### 2. Configuration Update Flow

```
ESP32: Config Change Detected
  ↓
ESP32: Generate JSON
  ↓
ESP32: Send via TCP
  ↓
Flutter: Receive JSON
  ↓
Flutter: Parse JSON
  ↓
Flutter: Validate Rules
  ↓
Flutter: Update UI
```

### 3. Telemetry Flow

```
Child Block: Generate Telemetry
  ↓
I2C: Send to Brain Block
  ↓
Brain Block: Forward via TCP
  ↓
Flutter: Parse and Display
```

## Communication Protocols

### I2C Protocol

- **Master**: ESP32 Brain Block
- **Slaves**: Child Blocks (addresses 0x08-0x15)
- **Speed**: 100 kHz (Standard Mode)
- **Pins**: GPIO 21 (SDA), GPIO 22 (SCL)

### TCP Protocol

- **Port**: 41233 (configurable)
- **Format**: Newline-delimited JSON
- **Direction**: ESP32 → Flutter (client → server)
- **Heartbeat**: Every 30 seconds

## Message Formats

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
          "block_id": "brain_block",
          "block_type": "brain_block"
        }
      }
    ],
    "errors": []
  }
}
```

### Telemetry Message

```json
{
  "type": "telemetry",
  "timestamp": 1234567890,
  "blocks": [
    {
      "block_id": "led_matrix",
      "data": {...}
    }
  ]
}
```

### Heartbeat Message

```json
{
  "type": "heartbeat",
  "timestamp": 1234567890
}
```

## State Management

### Flutter App State

- **ConnectionProvider**: Manages TCP server, connection state, heartbeat
- **BlockConfigProvider**: Manages configuration, telemetry, validation

### ESP32 State

- **Device Registry**: Map of I2C addresses to block info
- **Configuration State**: Current block configuration
- **TCP Connection State**: Connection status and retry logic

## Error Handling

### Connection Errors

- **Timeout**: Heartbeat timeout triggers reconnection
- **Disconnect**: Automatic reconnection with exponential backoff
- **Max Retries**: After 5 attempts, manual restart required

### Validation Errors

- **Errors**: Critical issues (e.g., missing Brain Block)
- **Warnings**: Non-critical issues (e.g., sequence warnings)

## Security Considerations

- **Local Network Only**: TCP server listens on local network
- **No Authentication**: Designed for local development/testing
- **Firewall**: Ensure firewall allows port 41233

## Performance Considerations

- **I2C Scan Rate**: Configurable (default: periodic)
- **TCP Buffer**: Handles message buffering
- **UI Updates**: Throttled to prevent UI lag
- **Telemetry Limit**: Last 100 entries kept in memory

## Next Steps

- **[Firmware Architecture](./firmware-architecture.md)** - Detailed firmware design
- **[App Architecture](./app-architecture.md)** - Detailed app design
- **[Firmware API](../api/firmware-api.md)** - Firmware API reference
- **[App API](../api/app-api.md)** - App API reference
