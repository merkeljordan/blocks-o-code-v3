# Flutter App Architecture

## Overview

The Blocks o' Code Flutter app provides a rich UI for visualizing and validating block configurations. It uses Provider for state management and follows a clean architecture pattern.

## Project Structure

```
companion_app/lib/
├── main.dart                    # Entry point
├── app/
│   └── blocks_of_code_app.dart  # App widget with providers
├── screens/
│   ├── main_screen.dart         # Main navigation screen
│   ├── welcome_screen.dart      # Welcome/landing screen
│   ├── block_config_screen.dart # Configuration visualization
│   ├── about_screen.dart        # About screen
│   ├── settings_screen.dart     # Settings screen
│   ├── help_screen.dart         # Help screen
│   └── tutorial_screen.dart     # Tutorial screen
├── widgets/
│   ├── block_3d_visualizer.dart # 3D block visualization
│   ├── hero_blocks_strip.dart   # Hero animation
│   └── ...                      # Other reusable widgets
├── models/
│   ├── block_configuration.dart # Configuration model
│   ├── block_telemetry.dart     # Telemetry model
│   ├── block_type.dart          # Block type enum
│   └── configuration_rules.dart # Validation rules
├── services/
│   ├── tcp_server_service.dart  # TCP server management
│   ├── connection_manager.dart  # Connection state management
│   ├── block_config_parser.dart # JSON parsing
│   ├── telemetry_parser.dart    # Telemetry parsing
│   └── configuration_validator.dart # Rule validation
├── providers/
│   ├── connection_provider.dart # Connection state provider
│   └── block_config_provider.dart # Config state provider
├── config/
│   └── app_config.dart          # App constants
└── utils/
    └── navigation.dart          # Navigation utilities
```

## Architecture Layers

### 1. Presentation Layer (Screens & Widgets)

**Purpose**: UI components and user interaction

**Components**:
- **Screens**: Full-page UI components
- **Widgets**: Reusable UI components

**Responsibilities**:
- Display data from providers
- Handle user input
- Navigate between screens
- Animate UI transitions

### 2. State Management Layer (Providers)

**Purpose**: Manage application state

**Components**:
- **ConnectionProvider**: TCP server and connection state
- **BlockConfigProvider**: Configuration and telemetry state

**Responsibilities**:
- Hold application state
- Notify listeners of changes
- Coordinate between services

### 3. Business Logic Layer (Services)

**Purpose**: Core business logic and external communication

**Components**:
- **TcpServerService**: TCP server lifecycle
- **ConnectionManager**: Connection state, heartbeat, reconnection
- **BlockConfigParser**: JSON parsing
- **TelemetryParser**: Telemetry parsing
- **ConfigurationValidator**: Rule validation

**Responsibilities**:
- Manage TCP connections
- Parse incoming messages
- Validate configurations
- Handle errors and retries

### 4. Data Layer (Models)

**Purpose**: Data structures and domain models

**Components**:
- **BlockConfiguration**: Configuration data structure
- **BlockTelemetry**: Telemetry data structure
- **BlockType**: Block type enumeration
- **ConfigurationRules**: Validation rule definitions

**Responsibilities**:
- Define data structures
- Enforce type safety
- Provide domain logic

## State Management Flow

```
User Action
    ↓
Screen Widget
    ↓
Provider (via context.read/watch)
    ↓
Service Layer
    ↓
External System (TCP/ESP32)
    ↓
Service Layer (processes response)
    ↓
Provider (updates state)
    ↓
Screen Widget (rebuilds)
```

## Provider Architecture

### ConnectionProvider

**State**:
- `isServerRunning`: TCP server status
- `isConnected`: Client connection status
- `connectionStatus`: Status message
- `lastHeartbeatTime`: Last heartbeat timestamp
- `isReconnecting`: Reconnection in progress
- `reconnectionAttempts`: Current attempt count

**Methods**:
- `startServer()`: Start TCP server
- `stopServer()`: Stop TCP server
- `sendMessage()`: Send message to client
- `updateConnectionStatus()`: Update status message

**Dependencies**:
- `TcpServerService`: TCP server management
- `ConnectionManager`: Connection state management

### BlockConfigProvider

**State**:
- `currentConfiguration`: Current block configuration
- `configViolations`: Validation violations
- `receivedTelemetry`: Telemetry data list

**Methods**:
- `listenToMessages()`: Listen to message stream
- `processMessage()`: Process incoming message
- `clear()`: Clear all data

**Dependencies**:
- `BlockConfigParser`: JSON parsing
- `ConfigurationValidator`: Rule validation
- `TelemetryParser`: Telemetry parsing

## Service Architecture

### TcpServerService

**Responsibilities**:
- Bind TCP server socket
- Accept client connections
- Stream incoming messages
- Send messages to client

**Streams**:
- `messageStream`: Stream of incoming messages

### ConnectionManager

**Responsibilities**:
- Manage heartbeat mechanism
- Handle reconnection logic
- Track connection state
- Emit state change events

**Streams**:
- `connectionStatusStream`: Status messages
- `isConnectedStream`: Connection state
- `lastHeartbeatStream`: Heartbeat timestamps
- `reconnectionStateStream`: Reconnection state

## Screen Navigation

### Navigation Flow

```
Welcome Screen
    ↓
Get Started → Block Config Screen
    ↓
Menu → About/Settings/Help/Tutorial
```

### Screen Types

- **Welcome**: Landing page with onboarding
- **Block Config**: Main configuration visualization
- **About**: Project information
- **Settings**: App settings (placeholder)
- **Help**: Help and support
- **Tutorial**: Getting started tutorial

## Data Flow Examples

### Configuration Update Flow

```
ESP32 sends JSON
    ↓
TcpServerService.messageStream
    ↓
BlockConfigProvider.listenToMessages()
    ↓
BlockConfigParser.parseConfig()
    ↓
ConfigurationValidator.validate()
    ↓
BlockConfigProvider.notifyListeners()
    ↓
BlockConfigScreen rebuilds
```

### Connection State Flow

```
User clicks "Get Started"
    ↓
ConnectionProvider.startServer()
    ↓
TcpServerService.startServer()
    ↓
Client connects
    ↓
ConnectionManager.updateConnectionState(true)
    ↓
ConnectionProvider.notifyListeners()
    ↓
All screens rebuild with new state
```

## Error Handling

### Connection Errors

- **Server start failure**: Show error message
- **Client disconnect**: Automatic reconnection
- **Heartbeat timeout**: Trigger reconnection

### Parsing Errors

- **Invalid JSON**: Log and ignore
- **Missing fields**: Use defaults or show error
- **Type mismatch**: Log and skip

### Validation Errors

- **Errors**: Critical issues, shown prominently
- **Warnings**: Non-critical issues, shown as warnings

## Performance Considerations

### State Updates

- **Throttling**: Limit update frequency
- **Debouncing**: Debounce rapid changes
- **Selective Rebuilds**: Use `context.watch()` selectively

### Memory Management

- **Telemetry Limit**: Keep last 100 entries
- **Stream Cleanup**: Cancel subscriptions on dispose
- **Provider Disposal**: Properly dispose providers

## Testing Strategy

### Unit Tests

- **Services**: Test business logic
- **Parsers**: Test JSON parsing
- **Validators**: Test rule validation

### Widget Tests

- **Screens**: Test UI rendering
- **Widgets**: Test reusable components

### Integration Tests

- **End-to-end**: Test full flows
- **Provider Integration**: Test state management

## Next Steps

- **[App API](../api/app-api.md)** - API reference
- **[System Overview](./system-overview.md)** - System architecture
- **[App Setup](../getting-started/app-setup.md)** - Setup guide
- **[Contributing](../development/contributing.md)** - Contribution guidelines
