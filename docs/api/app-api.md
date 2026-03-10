# Flutter App API Reference

This document provides API reference for the Blocks o' Code Flutter application.

## App -> Firmware Protocol

### `config_validation` event

After parsing and validating each incoming `block_config`, the app emits a
newline-delimited JSON event to the Brain Block:

```json
{
  "type": "config_validation",
  "is_valid": false,
  "error_count": 2
}
```

**Rules**:
- Emit immediately after each validation result update.
- Re-emit the latest known validation state when the TCP connection is re-established.
- `is_valid` is true only when validation has no error-severity violations.
- `error_count` is the number of error-severity violations.

## Providers

### ConnectionProvider

Manages TCP server and connection state.

#### Properties

```dart
bool isServerRunning        // TCP server status
bool isConnected           // Client connection status
String connectionStatus     // Status message
DateTime? lastHeartbeatTime // Last heartbeat timestamp
bool isReconnecting        // Reconnection in progress
int reconnectionAttempts   // Current attempt count
String? serverAddress      // Server address (IP:port)
Stream<String>? messageStream // Incoming message stream
```

#### Methods

##### `startServer()`

Start the TCP server.

**Signature**:
```dart
Future<void> startServer()
```

**Throws**:
- `Exception` if server is already running or fails to start

**Example**:
```dart
final provider = context.read<ConnectionProvider>();
try {
  await provider.startServer();
} catch (e) {
  print('Failed to start server: $e');
}
```

##### `stopServer()`

Stop the TCP server.

**Signature**:
```dart
Future<void> stopServer()
```

**Example**:
```dart
await provider.stopServer();
```

##### `sendMessage(String message)`

Send a message to the connected client.

**Signature**:
```dart
void sendMessage(String message)
```

**Throws**:
- `Exception` if no client connected

**Example**:
```dart
provider.sendMessage('{"type":"command","data":{}}');
```

##### `updateConnectionStatus(String status)`

Update connection status message.

**Signature**:
```dart
void updateConnectionStatus(String status)
```

**Example**:
```dart
provider.updateConnectionStatus('Connecting...');
```

### BlockConfigProvider

Manages block configuration and telemetry state.

#### Properties

```dart
BlockConfiguration? currentConfiguration // Current configuration
List<RuleViolation> configViolations     // Validation violations
List<BlockTelemetry> receivedTelemetry    // Telemetry data
```

#### Methods

##### `listenToMessages(Stream<String>? messageStream)`

Listen to message stream for processing.

**Signature**:
```dart
void listenToMessages(Stream<String>? messageStream)
```

**Example**:
```dart
final connectionProvider = context.read<ConnectionProvider>();
final blockConfigProvider = context.read<BlockConfigProvider>();
blockConfigProvider.listenToMessages(connectionProvider.messageStream);
```

##### `processMessage(String message)`

Process an incoming message directly.

**Signature**:
```dart
void processMessage(String message)
```

**Example**:
```dart
blockConfigProvider.processMessage(jsonString);
```

##### `clear()`

Clear all configuration and telemetry data.

**Signature**:
```dart
void clear()
```

## Services

### TcpServerService

Manages TCP server lifecycle.

#### Properties

```dart
bool isServerRunning  // Server status
bool isConnected      // Client connection status
Socket? clientSocket  // Connected client socket
Stream<String>? messageStream // Incoming message stream
String? serverAddress // Server address
```

#### Methods

##### `startServer()`

Start the TCP server.

**Signature**:
```dart
Future<void> startServer()
```

**Throws**:
- `Exception` if server is already running

##### `stopServer()`

Stop the TCP server.

**Signature**:
```dart
Future<void> stopServer()
```

##### `sendMessage(String message)`

Send message to connected client.

**Signature**:
```dart
void sendMessage(String message)
```

**Throws**:
- `Exception` if no client connected

### ConnectionManager

Manages connection state, heartbeat, and reconnection.

#### Streams

```dart
Stream<String> connectionStatusStream        // Status messages
Stream<bool> isConnectedStream               // Connection state
Stream<DateTime?> lastHeartbeatStream        // Heartbeat timestamps
Stream<Map<String, dynamic>> reconnectionStateStream // Reconnection state
```

#### Methods

##### `startHeartbeat()`

Start heartbeat mechanism.

**Signature**:
```dart
void startHeartbeat()
```

##### `stopHeartbeat()`

Stop heartbeat mechanism.

**Signature**:
```dart
void stopHeartbeat()
```

##### `updateConnectionState(bool connected)`

Update connection state.

**Signature**:
```dart
void updateConnectionState(bool connected)
```

##### `updateConnectionStatus(String status)`

Update connection status message.

**Signature**:
```dart
void updateConnectionStatus(String status)
```

## Parsers

### BlockConfigParser

Parses block configuration JSON.

#### Methods

##### `parseConfig(String jsonString)`

Parse configuration JSON string.

**Signature**:
```dart
BlockConfiguration? parseConfig(String jsonString)
```

**Returns**:
- `BlockConfiguration` on success
- `null` on parse error

**Example**:
```dart
final parser = BlockConfigParser();
final config = parser.parseConfig(jsonString);
if (config != null) {
  print('Total blocks: ${config.totalBlocks}');
}
```

### TelemetryParser

Parses telemetry JSON.

#### Methods

##### `parse(String jsonString)`

Parse telemetry JSON string.

**Signature**:
```dart
List<BlockTelemetry> parse(String jsonString)
```

**Returns**:
- List of `BlockTelemetry` objects
- Empty list on parse error

### ConfigurationValidator

Validates block configurations against rules.

#### Methods

##### `validate(BlockConfiguration config)`

Validate configuration against rules.

**Signature**:
```dart
List<RuleViolation> validate(BlockConfiguration config)
```

**Returns**:
- List of `RuleViolation` objects (empty if valid)

**Example**:
```dart
final validator = ConfigurationValidator();
final violations = validator.validate(config);
for (final violation in violations) {
  print('${violation.severity}: ${violation.message}');
}
```

## Models

### BlockConfiguration

Block configuration data structure.

```dart
class BlockConfiguration {
  final int totalBlocks;
  final List<BlockInfo> blocks;
  final List<ConfigError> errors;
  final DateTime timestamp;
}
```

### BlockInfo

Individual block information.

```dart
class BlockInfo {
  final int index;
  final int i2cAddress;
  final WhoAmiData whoami;
  final int connectionOrder;
  BlockType? get blockType; // Computed from whoami
}
```

### BlockTelemetry

Telemetry data from a block.

```dart
class BlockTelemetry {
  final String? blockId;
  final DateTime timestamp;
  final Map<String, dynamic> data;
}
```

### RuleViolation

Configuration validation violation.

```dart
class RuleViolation {
  final Severity severity; // error or warning
  final String message;
  final int? blockIndex;
}
```

## Configuration

### AppConfig

Application-wide configuration constants.

```dart
class AppConfig {
  static const int tcpPort = 41233;
  static const Duration heartbeatInterval = Duration(seconds: 30);
  static const Duration heartbeatTimeout = Duration(seconds: 60);
  static const int maxReconnectionAttempts = 5;
  static const int defaultStressTestMessageRate = 10;
  static const Duration defaultStressTestDuration = Duration(minutes: 5);
  static const int maxTelemetryEntries = 100;
  static const Duration menuAnimationDuration = Duration(milliseconds: 300);
  static const Duration screenTransitionDuration = Duration(milliseconds: 400);
}
```

## Usage Examples

### Setting Up Providers

```dart
MultiProvider(
  providers: [
    ChangeNotifierProvider(create: (_) => ConnectionProvider()),
    ChangeNotifierProvider(create: (_) => BlockConfigProvider()),
  ],
  child: MyApp(),
)
```

### Accessing Providers

```dart
// Watch for changes (rebuilds on change)
final connectionProvider = context.watch<ConnectionProvider>();

// Read once (no rebuild)
final blockConfigProvider = context.read<BlockConfigProvider>();
```

### Listening to Messages

```dart
@override
void initState() {
  super.initState();
  WidgetsBinding.instance.addPostFrameCallback((_) {
    final connectionProvider = context.read<ConnectionProvider>();
    final blockConfigProvider = context.read<BlockConfigProvider>();
    blockConfigProvider.listenToMessages(connectionProvider.messageStream);
  });
}
```

## Resources

- [Provider Package](https://pub.dev/packages/provider)
- [Flutter Documentation](https://flutter.dev/docs)
- [Dart Language Tour](https://dart.dev/guides/language/language-tour)
