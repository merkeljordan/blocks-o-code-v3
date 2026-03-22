import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'dart:io';
import 'dart:async';
import 'dart:convert';
import 'dart:math' as math;
import 'models/block_telemetry.dart';
import 'services/telemetry_parser.dart';
import 'models/block_configuration.dart';
import 'models/configuration_rules.dart';
import 'services/block_config_parser.dart';
import 'services/configuration_validator.dart';
import 'models/block_type.dart';
import 'widgets/block_3d_visualizer.dart';

const bool kOfflineMode =
    bool.fromEnvironment('COMPANION_OFFLINE_MODE', defaultValue: false);

void main() {
  WidgetsFlutterBinding.ensureInitialized();
  // Enable fullscreen mode
  SystemChrome.setEnabledSystemUIMode(SystemUiMode.immersiveSticky);
  runApp(const BlocksOfCodeApp());
}

class BlocksOfCodeApp extends StatelessWidget {
  const BlocksOfCodeApp({super.key});

  @override
  Widget build(BuildContext context) {
    // Custom vibrant color scheme with pinks, blues, and purples
    final colorScheme = ColorScheme.fromSeed(
      seedColor: const Color(0xFF9C27B0), // Purple base
      brightness: Brightness.dark,
      primary: const Color(0xFFE91E63), // Pink
      secondary: const Color(0xFF2196F3), // Blue
      tertiary: const Color(0xFF9C27B0), // Purple
      surface: const Color(0xFF1A1A2E),
      onSurface: Colors.white,
      primaryContainer: const Color(0xFFE91E63).withOpacity(0.2),
      secondaryContainer: const Color(0xFF2196F3).withOpacity(0.2),
      tertiaryContainer: const Color(0xFF9C27B0).withOpacity(0.2),
    );

    return MaterialApp(
      title: 'Blocks of Code (v3)',
      theme: ThemeData(
        useMaterial3: true,
        colorScheme: colorScheme,
        brightness: Brightness.dark,
      ),
      home: const MainScreen(),
      debugShowCheckedModeBanner: false,
    );
  }
}

// Screen types for navigation
enum ScreenType { welcome, about, blockConfig, settings, help, tutorial }

class MainScreen extends StatefulWidget {
  const MainScreen({super.key});

  @override
  State<MainScreen> createState() => _MainScreenState();
}

class _MainScreenState extends State<MainScreen> with TickerProviderStateMixin {
  ScreenType currentScreen = ScreenType.welcome;
  bool isMenuOpen = false;
  bool isConnected = false;
  ServerSocket? tcpServer;
  Socket? _clientSocket;
  String connectionStatus = 'Server not started';
  final int serverPort = 41233;
  
  // Telemetry parsing
  final TelemetryParser _telemetryParser = TelemetryParser();
  List<BlockTelemetry> _receivedTelemetry = [];
  
  // Block configuration
  final BlockConfigParser _configParser = BlockConfigParser();
  final ConfigurationValidator _configValidator = ConfigurationValidator();
  BlockConfiguration? _currentConfiguration;
  BlockConfiguration? _offlineBaseConfiguration;
  List<RuleViolation> _configViolations = [];
  
  // Heartbeat mechanism
  Timer? _heartbeatTimer;
  DateTime? _lastHeartbeatTime;
  static const Duration _heartbeatInterval = Duration(seconds: 30);
  static const Duration _heartbeatTimeout = Duration(seconds: 60); // 2x interval
  bool _isReconnecting = false;
  int _reconnectionAttempts = 0;
  static const int _maxReconnectionAttempts = 5;
  
  // Stress testing
  bool _isStressTesting = false;
  Timer? _stressTestTimer;
  int _stressTestMessagesSent = 0;
  int _stressTestMessagesReceived = 0;
  int _stressTestErrors = 0;
  DateTime? _stressTestStartTime;
  int _stressTestMessageRate = 10; // messages per second
  
  late AnimationController _menuAnimationController;
  late Animation<double> _menuAnimation;

  @override
  void initState() {
    super.initState();
    _menuAnimationController = AnimationController(
      vsync: this,
      duration: const Duration(milliseconds: 300),
    );
    _menuAnimation = CurvedAnimation(
      parent: _menuAnimationController,
      curve: Curves.easeOutCubic,
    );
  }

  @override
  void dispose() {
    _menuAnimationController.dispose();
    _heartbeatTimer?.cancel();
    _stressTestTimer?.cancel();
    _clientSocket?.destroy();
    _clientSocket = null;
    tcpServer?.close();
    tcpServer = null;
    super.dispose();
  }

  void _handleGetStarted() {
    if (kOfflineMode) {
      setState(() {
        isConnected = false;
        _isReconnecting = false;
        _reconnectionAttempts = 0;
        connectionStatus = 'Offline mode: using fake config (no WiFi/TCP)';
      });
      _navigateToScreen(ScreenType.blockConfig);
      unawaited(_loadOfflineBaseConfiguration('assets/sample_block_config.json'));
      return;
    }

    // Start server if needed; otherwise just navigate without interrupting it.
    if (!_isServerRunning) {
      _setupTCPServerAndConnect();
    }
    _navigateToScreen(isConnected ? ScreenType.blockConfig : ScreenType.welcome);
  }

  /// Load fake block configuration from assets for testing
  Future<void> _loadFakeConfiguration(String assetPath) async {
    try {
      final jsonString = await rootBundle.loadString(assetPath);
      // Process it the same way as a real TCP message
      _processMessage(jsonString);
      setState(() {
        connectionStatus = 'Loaded fake config from $assetPath';
      });
    } catch (e) {
      setState(() {
        connectionStatus = 'Failed to load fake config: $e';
      });
    }
  }

  Future<void> _loadOfflineBaseConfiguration(String assetPath) async {
    try {
      final jsonString = await rootBundle.loadString(assetPath);
      final config = _configParser.parseConfig(jsonString);
      if (config == null) {
        throw StateError('Failed to parse block configuration from $assetPath');
      }

      final violations = _configValidator.validate(config);

      setState(() {
        _currentConfiguration = config;
        _offlineBaseConfiguration = config;
        _configViolations = violations;
        connectionStatus = 'Offline mode: loaded fake config from $assetPath';
        _lastHeartbeatTime = DateTime.now();
      });
    } catch (e) {
      setState(() {
        connectionStatus = 'Offline mode: failed to load fake config: $e';
      });
    }
  }

  void _applyLocalBlockConfiguration(
    BlockConfiguration config, {
    required String statusText,
  }) {
    final violations = _configValidator.validate(config);
    setState(() {
      _currentConfiguration = config;
      _configViolations = violations;
      connectionStatus = statusText;
      _lastHeartbeatTime = DateTime.now();
    });
  }

  void _offlineAddBlock() {
    final current = _currentConfiguration;
    if (current == null) return;
    final blocks = List<BlockInfo>.from(current.blocks);
    final last = blocks.isNotEmpty ? blocks.last : null;

    final type = last?.blockType ?? BlockType.brainBlock;
    final nextAddr = (last?.i2cAddress ?? 0) + 1;
    final nextIndex = blocks.length;

    final blockId =
        'BLOCK_${nextAddr.toRadixString(16).toUpperCase().padLeft(2, '0')}';

    final whoami = WhoAmIData(
      blockType: type.identifier,
      blockId: blockId,
      firmwareVersion: '1.0.0',
      capabilities: const [],
    );

    blocks.add(
      BlockInfo(
        index: nextIndex,
        i2cAddress: nextAddr,
        whoami: whoami,
        connectionOrder: nextIndex,
        blockType: type,
      ),
    );

    // Reindex sequentially so validation and rendering stay consistent.
    final reindexed = <BlockInfo>[];
    for (var i = 0; i < blocks.length; i++) {
      final b = blocks[i];
      reindexed.add(
        BlockInfo(
          index: i,
          i2cAddress: b.i2cAddress,
          whoami: b.whoami,
          connectionOrder: i,
          blockType: b.blockType ?? type,
        ),
      );
    }

    _applyLocalBlockConfiguration(
      BlockConfiguration(
        totalBlocks: reindexed.length,
        blocks: reindexed,
        errors: current.errors,
        timestamp: DateTime.now(),
        originalFirmwareBlockCount: reindexed.length,
        hasSyntheticBrainBlock: current.hasSyntheticBrainBlock,
      ),
      statusText: 'Offline mode: added block (${reindexed.length})',
    );
  }

  void _offlineRemoveBlock() {
    final current = _currentConfiguration;
    if (current == null) return;
    if (current.blocks.length <= 1) return;

    final blocks = List<BlockInfo>.from(current.blocks);
    blocks.removeLast();

    final type = blocks.last.blockType ?? BlockType.brainBlock;

    // Reindex sequentially after removal.
    final reindexed = <BlockInfo>[];
    for (var i = 0; i < blocks.length; i++) {
      final b = blocks[i];
      reindexed.add(
        BlockInfo(
          index: i,
          i2cAddress: b.i2cAddress,
          whoami: b.whoami,
          connectionOrder: i,
          blockType: b.blockType ?? type,
        ),
      );
    }

    _applyLocalBlockConfiguration(
      BlockConfiguration(
        totalBlocks: reindexed.length,
        blocks: reindexed,
        errors: current.errors,
        timestamp: DateTime.now(),
        originalFirmwareBlockCount: reindexed.length,
        hasSyntheticBrainBlock: current.hasSyntheticBrainBlock,
      ),
      statusText: 'Offline mode: removed block (${reindexed.length})',
    );
  }

  void _offlineResetBlocks() {
    final base = _offlineBaseConfiguration;
    if (base == null) return;
    _applyLocalBlockConfiguration(
      BlockConfiguration(
        totalBlocks: base.totalBlocks,
        blocks: List<BlockInfo>.from(base.blocks),
        errors: base.errors,
        timestamp: DateTime.now(),
        originalFirmwareBlockCount: base.originalFirmwareBlockCount,
        hasSyntheticBrainBlock: base.hasSyntheticBrainBlock,
      ),
      statusText: 'Offline mode: reset config',
    );
  }

  void _navigateToScreen(ScreenType screen) {
    setState(() {
      currentScreen = screen;
    });
  }

  void _toggleMenu() {
    setState(() {
      isMenuOpen = !isMenuOpen;
    });
    if (isMenuOpen) {
      _menuAnimationController.forward();
    } else {
      _menuAnimationController.reverse();
    }
  }

  bool get _isServerRunning => tcpServer != null;

  Future<void> _stopTCPServer() async {
    try {
      _stopHeartbeat();

      // Disconnect any active client
      _clientSocket?.destroy();
      _clientSocket = null;

      // Close the server
      await tcpServer?.close();
      tcpServer = null;

      setState(() {
        isConnected = false;
        _currentConfiguration = null;
        _configViolations = [];
        connectionStatus = 'Server stopped';
      });
    } catch (e) {
      setState(() {
        connectionStatus = 'Failed to stop server: $e';
      });
    }
  }

  Future<void> _setupTCPServerAndConnect() async {
    try {
      // If already running, don't restart/close it (clicking Get Started twice should be safe)
      if (_isServerRunning) {
        setState(() {
          connectionStatus = 'Server already running on port $serverPort';
        });
        return;
      }
      
      setState(() {
        connectionStatus = 'Setting up TCP server...';
      });
      
      // Start TCP server
      tcpServer = await ServerSocket.bind(InternetAddress.anyIPv4, serverPort);
      
      setState(() {
        connectionStatus = 'Server listening on port $serverPort and address ${tcpServer!.address.address}';
      });
      
      // Set up client connection handler
      tcpServer!.listen(_handleClient, onError: (e) {
        setState(() {
          connectionStatus = 'Server error: $e';
        });
      }, onDone: () {
        setState(() {
          connectionStatus = 'Server closed';
          isConnected = false;
        });
      });
      
    } catch (e) {
      setState(() {
        connectionStatus = 'Failed to start server: $e';
        isConnected = false;
      });
    }
  }

  void _handleClient(Socket client) {
    // Close previous client if exists
    _clientSocket?.destroy();
    _clientSocket = client;
    
    setState(() {
      connectionStatus = 'Client connected: ${client.remoteAddress.address}:${client.remotePort}';
      isConnected = true;
      _isReconnecting = false;
      _reconnectionAttempts = 0;
      _lastHeartbeatTime = DateTime.now();
    });
    
    // Start heartbeat mechanism
    _startHeartbeat();
    
    // Navigate to block configuration screen when client connects
    _navigateToScreen(ScreenType.blockConfig);
    
    // Set up message listener with JSON parsing
    String buffer = '';
    client.listen((data) {
      final msg = String.fromCharCodes(data);
      buffer += msg;
      
      // Process complete messages (assuming newline-delimited)
      final lines = buffer.split('\n');
      buffer = lines.removeLast(); // Keep incomplete line in buffer
      
      for (final line in lines) {
        if (line.trim().isEmpty) continue;
        _processMessage(line.trim());
      }
    }, onDone: () {
      setState(() {
        connectionStatus = 'Client disconnected: ${client.remoteAddress.address}';
        isConnected = false;
      });
      _stopHeartbeat();
      if (_clientSocket == client) {
        _clientSocket = null;
      }
      // Attempt reconnection
      _attemptReconnection();
    }, onError: (e) {
      setState(() {
        connectionStatus = 'Client error: $e';
        isConnected = false;
      });
      _stopHeartbeat();
      if (_clientSocket == client) {
        _clientSocket = null;
      }
      // Attempt reconnection
      _attemptReconnection();
    });
  }

  void _processMessage(String message) {
    try {
      // Try to parse as JSON
      final json = jsonDecode(message) as Map<String, dynamic>;
      
      // Check if it's a heartbeat acknowledgment
      if (json.containsKey('type') && json['type'] == 'heartbeat_ack') {
        setState(() {
          _lastHeartbeatTime = DateTime.now();
          connectionStatus = 'Heartbeat received';
        });
        return;
      }
      
      // Check if it's a block configuration message
      if (json.containsKey('type') && json['type'] == 'block_config') {
        final config = _configParser.parseConfig(message);
        if (config != null) {
          // Validate configuration
          final violations = _configValidator.validate(config);
          
          setState(() {
            _currentConfiguration = config;
            _configViolations = violations;
            connectionStatus = 'Block config: ${config.totalBlocks} block(s), ${violations.where((v) => v.severity == Severity.error).length} error(s)';
            _lastHeartbeatTime = DateTime.now();
          });
        } else {
          setState(() {
            connectionStatus = 'Failed to parse block configuration';
          });
        }
        return;
      }
      
      // Try to parse as telemetry
      final telemetryList = _telemetryParser.parse(message);
      if (telemetryList.isNotEmpty) {
        setState(() {
          _receivedTelemetry.addAll(telemetryList);
          // Keep only last 100 telemetry entries
          if (_receivedTelemetry.length > 100) {
            _receivedTelemetry = _receivedTelemetry.sublist(_receivedTelemetry.length - 100);
          }
          connectionStatus = 'Received telemetry: ${telemetryList.length} block(s)';
          _lastHeartbeatTime = DateTime.now(); // Update on any message
        });
        
        // Update stress test stats
        if (_isStressTesting) {
          _stressTestMessagesReceived++;
        }
      } else {
        // Not valid telemetry, just log it
        setState(() {
          connectionStatus = 'Received: $message';
        });
      }
    } catch (e) {
      // Not JSON or parsing failed, treat as plain text
      setState(() {
        connectionStatus = 'Received: $message';
      });
      
      // Update stress test stats
      if (_isStressTesting) {
        _stressTestMessagesReceived++;
      }
    }
  }

  void _sendMessageToESP(String msg) {
    final client = _clientSocket;
    if (client == null) {
      setState(() {
        connectionStatus = 'No ESP32 connected';
      });
      return;
    }
    try {
      client.write('$msg\n');
      setState(() {
        connectionStatus = 'Connected to ESP32 and sent: $msg';
      });
    } catch (e) {
      setState(() {
        connectionStatus = 'Send failed: $e';
      });
      // Connection might be lost
      _attemptReconnection();
    }
  }

  // Heartbeat mechanism
  void _startHeartbeat() {
    _stopHeartbeat();
    _lastHeartbeatTime = DateTime.now();
    
    _heartbeatTimer = Timer.periodic(_heartbeatInterval, (timer) {
      _sendHeartbeat();
      _checkHeartbeat();
    });
  }

  void _stopHeartbeat() {
    _heartbeatTimer?.cancel();
    _heartbeatTimer = null;
  }

  void _sendHeartbeat() {
    if (!isConnected || _clientSocket == null) {
      return;
    }
    
    try {
      final heartbeat = jsonEncode({
        'type': 'heartbeat',
        'timestamp': DateTime.now().millisecondsSinceEpoch,
      });
      _clientSocket!.write('$heartbeat\n');
    } catch (e) {
      // Connection lost
      setState(() {
        connectionStatus = 'Heartbeat send failed: $e';
        isConnected = false;
      });
      _attemptReconnection();
    }
  }

  void _checkHeartbeat() {
    if (!isConnected || _lastHeartbeatTime == null) {
      return;
    }
    
    final timeSinceLastHeartbeat = DateTime.now().difference(_lastHeartbeatTime!);
    if (timeSinceLastHeartbeat > _heartbeatTimeout) {
      setState(() {
        connectionStatus = 'Heartbeat timeout - connection lost';
        isConnected = false;
      });
      _stopHeartbeat();
      _clientSocket?.destroy();
      _clientSocket = null;
      _attemptReconnection();
    }
  }

  // Reconnection logic with exponential backoff
  Future<void> _attemptReconnection() async {
    if (_isReconnecting) {
      return; // Already attempting reconnection
    }
    
    if (_reconnectionAttempts >= _maxReconnectionAttempts) {
      setState(() {
        connectionStatus = 'Max reconnection attempts reached. Please restart server.';
        _isReconnecting = false;
        _reconnectionAttempts = 0;
      });
      return;
    }
    
    setState(() {
      _isReconnecting = true;
      _reconnectionAttempts++;
    });
    
    // Exponential backoff: 1s, 2s, 4s, 8s, 16s (capped at 30s)
    final delaySeconds = (1 << (_reconnectionAttempts - 1)).clamp(1, 30);
    
    setState(() {
      connectionStatus = 'Reconnecting in ${delaySeconds}s (attempt $_reconnectionAttempts/$_maxReconnectionAttempts)...';
    });
    
    await Future.delayed(Duration(seconds: delaySeconds));
    
    if (mounted) {
      try {
        // Restart TCP server
      await _setupTCPServerAndConnect();
        
        // Reset reconnection state if successful
        if (isConnected) {
          setState(() {
            _isReconnecting = false;
            _reconnectionAttempts = 0;
          });
        } else {
          // Try again
          _attemptReconnection();
        }
      } catch (e) {
        setState(() {
          connectionStatus = 'Reconnection failed: $e';
        });
        // Try again
        _attemptReconnection();
      }
    }
  }

  // Stress testing functionality
  void _startStressTest({int? messageRate, Duration? duration}) {
    if (_isStressTesting) {
      _stopStressTest();
    }
    
    setState(() {
      _isStressTesting = true;
      _stressTestMessagesSent = 0;
      _stressTestMessagesReceived = 0;
      _stressTestErrors = 0;
      _stressTestStartTime = DateTime.now();
      if (messageRate != null) {
        _stressTestMessageRate = messageRate;
      }
    });
    
    final testDuration = duration ?? const Duration(minutes: 5);
    final interval = Duration(milliseconds: 1000 ~/ _stressTestMessageRate);
    
    _stressTestTimer = Timer.periodic(interval, (timer) {
      if (!_isStressTesting || !isConnected) {
        _stopStressTest();
        return;
      }
      
      // Check if duration exceeded
      if (_stressTestStartTime != null) {
        final elapsed = DateTime.now().difference(_stressTestStartTime!);
        if (elapsed >= testDuration) {
          _stopStressTest();
          return;
        }
      }
      
      // Send test message
      try {
        final testMessage = jsonEncode({
          'type': 'stress_test',
          'timestamp': DateTime.now().millisecondsSinceEpoch,
          'sequence': _stressTestMessagesSent,
          'payload': 'A' * 100, // 100 character payload
        });
        _sendMessageToESP(testMessage);
        _stressTestMessagesSent++;
      } catch (e) {
        _stressTestErrors++;
      }
    });
    
    // Auto-stop after duration
    Timer(testDuration, () {
      if (_isStressTesting) {
        _stopStressTest();
      }
    });
  }

  void _stopStressTest() {
    setState(() {
      _isStressTesting = false;
    });
    _stressTestTimer?.cancel();
    _stressTestTimer = null;
  }

  String _getStressTestStats() {
    if (_stressTestStartTime == null) {
      return 'No test running';
    }
    
    final elapsed = DateTime.now().difference(_stressTestStartTime!);
    final elapsedSeconds = elapsed.inSeconds;
    final sentPerSec = elapsedSeconds > 0 ? (_stressTestMessagesSent / elapsedSeconds).toStringAsFixed(1) : '0';
    final receivedPerSec = elapsedSeconds > 0 ? (_stressTestMessagesReceived / elapsedSeconds).toStringAsFixed(1) : '0';
    final lossRate = _stressTestMessagesSent > 0 
        ? ((_stressTestMessagesSent - _stressTestMessagesReceived) / _stressTestMessagesSent * 100).toStringAsFixed(1)
        : '0';
    
    return 'Duration: ${elapsed.inMinutes}m ${elapsed.inSeconds % 60}s\n'
           'Sent: $_stressTestMessagesSent ($sentPerSec/s)\n'
           'Received: $_stressTestMessagesReceived ($receivedPerSec/s)\n'
           'Errors: $_stressTestErrors\n'
           'Loss: $lossRate%';
  }

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final colorScheme = theme.colorScheme;

    return Scaffold(
      backgroundColor: colorScheme.surface,
      body: Stack(
        children: [
          // Main content with fade transition
          AnimatedSwitcher(
            duration: const Duration(milliseconds: 400),
            transitionBuilder: (Widget child, Animation<double> animation) {
              return FadeTransition(
                opacity: animation,
                child: child,
              );
            },
            child: _buildCurrentScreen(theme, colorScheme),
          ),
          
          // Persistent Menu Button
          Positioned(
            top: 16,
            right: 16,
            child: _buildMenuButton(colorScheme),
          ),
          
          // Menu Overlay
          if (isMenuOpen)
            Stack(
              children: [
                // Background overlay that closes menu on tap
                GestureDetector(
                  onTap: _toggleMenu,
                  child: Container(
                    color: Colors.black.withOpacity(0.3),
                  ),
                ),
                // Menu content
                Align(
                  alignment: Alignment.topRight,
                  child: SlideTransition(
                    position: Tween<Offset>(
                      begin: const Offset(1.0, 0.0),
                      end: Offset.zero,
                    ).animate(_menuAnimation),
                    child: GestureDetector(
                      onTap: () {}, // Consume taps on menu to prevent closing
                      child: _buildMenu(colorScheme, theme),
                    ),
                  ),
                ),
              ],
            ),
        ],
      ),
    );
  }

  Widget _buildCurrentScreen(ThemeData theme, ColorScheme colorScheme) {
    Widget screen;
    switch (currentScreen) {
      case ScreenType.welcome:
        screen = WelcomeScreen(
          key: const ValueKey('welcome'),
          isConnected: isConnected,
          connectionStatus: connectionStatus,
          isServerRunning: _isServerRunning,
          onStopServer: _stopTCPServer,
          hasConfiguration: _currentConfiguration != null,
          onGetStarted: _handleGetStarted,
        );
        break;
      case ScreenType.about:
        screen = const AboutScreen(key: ValueKey('about'));
        break;
      case ScreenType.blockConfig:
        screen = BlockConfigScreen(
          key: const ValueKey('blockConfig'),
          isConnected: isConnected,
          connectionStatus: connectionStatus,
          lastHeartbeatTime: _lastHeartbeatTime,
          isReconnecting: _isReconnecting,
          reconnectionAttempts: _reconnectionAttempts,
          isStressTesting: _isStressTesting,
          stressTestStats: _getStressTestStats(),
          onStartStressTest: () => _startStressTest(),
          onStopStressTest: _stopStressTest,
          receivedTelemetry: _receivedTelemetry,
          currentConfiguration: _currentConfiguration,
          configViolations: _configViolations,
          onLoadFakeConfig: (path) => _loadFakeConfiguration(path),
          onOfflineAddBlock: kOfflineMode ? _offlineAddBlock : null,
          onOfflineRemoveBlock: kOfflineMode ? _offlineRemoveBlock : null,
          onOfflineResetBlocks: kOfflineMode ? _offlineResetBlocks : null,
        );
        break;
      case ScreenType.settings:
        screen = const SettingsScreen(key: ValueKey('settings'));
        break;
      case ScreenType.help:
        screen = const HelpScreen(key: ValueKey('help'));
        break;
      case ScreenType.tutorial:
        screen = const TutorialScreen(key: ValueKey('tutorial'));
        break;
    }
    return screen;
  }

  Widget _buildMenuButton(ColorScheme colorScheme) {
    return AnimatedContainer(
      duration: const Duration(milliseconds: 300),
      decoration: BoxDecoration(
        shape: BoxShape.circle,
        gradient: LinearGradient(
          colors: [
            colorScheme.primary,
            colorScheme.secondary,
            colorScheme.tertiary,
          ],
          begin: Alignment.topLeft,
          end: Alignment.bottomRight,
        ),
        boxShadow: [
          BoxShadow(
            color: colorScheme.primary.withOpacity(0.5),
            blurRadius: isMenuOpen ? 20 : 10,
            spreadRadius: isMenuOpen ? 2 : 0,
          ),
        ],
      ),
      child: FloatingActionButton(
        onPressed: _toggleMenu,
        backgroundColor: Colors.transparent,
        elevation: 0,
        child: AnimatedRotation(
          turns: isMenuOpen ? 0.125 : 0,
          duration: const Duration(milliseconds: 300),
          child: Icon(
            isMenuOpen ? Icons.close : Icons.menu,
            color: Colors.white,
          ),
        ),
      ),
    );
  }

  Widget _buildMenu(ColorScheme colorScheme, ThemeData theme) {
    return Container(
      key: ValueKey('menu_$isConnected'),
      width: 280,
      margin: const EdgeInsets.only(top: 80, right: 16),
      decoration: BoxDecoration(
        gradient: LinearGradient(
          begin: Alignment.topLeft,
          end: Alignment.bottomRight,
          colors: [
            colorScheme.surface.withOpacity(0.95),
            colorScheme.surfaceContainerHighest.withOpacity(0.8),
          ],
        ),
        borderRadius: BorderRadius.circular(24),
        border: Border.all(
          color: colorScheme.primary.withOpacity(0.3),
          width: 1.5,
        ),
        boxShadow: [
          BoxShadow(
            color: colorScheme.primary.withOpacity(0.3),
            blurRadius: 30,
            spreadRadius: 5,
            offset: const Offset(0, 10),
          ),
          BoxShadow(
            color: Colors.black.withOpacity(0.3),
            blurRadius: 20,
            offset: const Offset(0, 10),
          ),
        ],
      ),
      child: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          _buildMenuItem(
            icon: Icons.play_arrow_rounded,
            title: 'Get Started',
            color: colorScheme.primary,
            onTap: () {
              _toggleMenu();
              _handleGetStarted();
            },
            colorScheme: colorScheme,
            theme: theme,
          ),
          _buildDivider(colorScheme),
          _buildMenuItem(
            icon: Icons.info_outline_rounded,
            title: 'About',
            color: colorScheme.secondary,
            onTap: () {
              _toggleMenu();
              _navigateToScreen(ScreenType.about);
            },
            colorScheme: colorScheme,
            theme: theme,
          ),
          _buildDivider(colorScheme),
          _buildMenuItem(
            icon: Icons.school_rounded,
            title: 'Tutorial',
            color: colorScheme.tertiary,
            onTap: () {
              _toggleMenu();
              _navigateToScreen(ScreenType.tutorial);
            },
            colorScheme: colorScheme,
            theme: theme,
          ),
          _buildDivider(colorScheme),
          _buildMenuItem(
            icon: Icons.help_outline_rounded,
            title: 'Help',
            color: colorScheme.secondary,
            onTap: () {
              _toggleMenu();
              _navigateToScreen(ScreenType.help);
            },
            colorScheme: colorScheme,
            theme: theme,
          ),
          _buildDivider(colorScheme),
          _buildMenuItem(
            icon: Icons.settings_rounded,
            title: 'Settings',
            color: colorScheme.primary,
            onTap: () {
              _toggleMenu();
              _navigateToScreen(ScreenType.settings);
            },
            colorScheme: colorScheme,
            theme: theme,
          ),
          _buildDivider(colorScheme),
          _buildMenuItem(
            icon: Icons.home_rounded,
            title: 'Welcome',
            color: colorScheme.tertiary,
            onTap: () {
              _toggleMenu();
              _navigateToScreen(ScreenType.welcome);
            },
            colorScheme: colorScheme,
            theme: theme,
          ),
        ],
      ),
    );
  }

  Widget _buildDivider(ColorScheme colorScheme) {
    return Container(
      margin: const EdgeInsets.symmetric(horizontal: 16),
      height: 1,
      decoration: BoxDecoration(
        gradient: LinearGradient(
          colors: [
            Colors.transparent,
            colorScheme.primary.withOpacity(0.3),
            Colors.transparent,
          ],
        ),
      ),
    );
  }

  Widget _buildMenuItem({
    required IconData icon,
    required String title,
    required Color color,
    required VoidCallback onTap,
    required ColorScheme colorScheme,
    required ThemeData theme,
  }) {
    return Material(
      color: Colors.transparent,
      child: InkWell(
        onTap: onTap,
        borderRadius: BorderRadius.circular(16),
        child: Container(
          padding: const EdgeInsets.symmetric(horizontal: 20, vertical: 16),
          child: Row(
            children: [
              Container(
                padding: const EdgeInsets.all(8),
                decoration: BoxDecoration(
                  gradient: LinearGradient(
                    colors: [
                      color.withOpacity(0.3),
                      color.withOpacity(0.1),
                    ],
                  ),
                  borderRadius: BorderRadius.circular(12),
                  border: Border.all(
                    color: color.withOpacity(0.5),
                    width: 1,
                  ),
                ),
                child: Icon(icon, color: color, size: 22),
              ),
              const SizedBox(width: 16),
              Expanded(
                child: Text(
                  title,
                  style: theme.textTheme.bodyLarge?.copyWith(
                    fontWeight: FontWeight.w600,
                    color: Colors.white,
                  ),
                ),
              ),
              Icon(
                Icons.chevron_right_rounded,
                color: color.withOpacity(0.5),
                size: 20,
              ),
            ],
          ),
        ),
      ),
    );
  }
}

// Welcome Screen with fade-in animations
class WelcomeScreen extends StatefulWidget {
  final bool isConnected;
  final String connectionStatus;
  final bool isServerRunning;
  final VoidCallback onStopServer;
  final bool hasConfiguration;
  final VoidCallback onGetStarted;

  const WelcomeScreen({
    super.key,
    required this.isConnected,
    required this.connectionStatus,
    required this.isServerRunning,
    required this.onStopServer,
    required this.hasConfiguration,
    required this.onGetStarted,
  });

  @override
  State<WelcomeScreen> createState() => _WelcomeScreenState();
}

class _WelcomeScreenState extends State<WelcomeScreen>
    with TickerProviderStateMixin {
  late AnimationController _fadeController;
  late Animation<double> _fadeAnimation1;
  late Animation<double> _fadeAnimation2;
  late AnimationController _bgController;

  @override
  void initState() {
    super.initState();
    _fadeController = AnimationController(
      vsync: this,
      duration: const Duration(milliseconds: 1500),
    );

    _fadeAnimation1 = Tween<double>(begin: 0.0, end: 1.0).animate(
      CurvedAnimation(
        parent: _fadeController,
        curve: const Interval(0.0, 0.5, curve: Curves.easeOut),
      ),
    );

    _fadeAnimation2 = Tween<double>(begin: 0.0, end: 1.0).animate(
      CurvedAnimation(
        parent: _fadeController,
        curve: const Interval(0.3, 1.0, curve: Curves.easeOut),
      ),
    );

    _fadeController.forward();

    _bgController = AnimationController(
      vsync: this,
      // Slightly faster loop for more energetic flow
      duration: const Duration(seconds: 8),
    )..repeat();
  }

  @override
  void dispose() {
    _fadeController.dispose();
    _bgController.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final colorScheme = theme.colorScheme;

    return Container(
      decoration: BoxDecoration(
        gradient: LinearGradient(
          begin: Alignment.topLeft,
          end: Alignment.bottomRight,
          colors: [
            colorScheme.surface,
            colorScheme.surfaceContainerHighest.withOpacity(0.4),
          ],
        ),
      ),
      child: AnimatedBuilder(
        animation: _bgController,
        builder: (context, child) => CustomPaint(
          painter: _LogicBackgroundPainter(
            colorScheme: colorScheme,
            t: _bgController.value,
          ),
          child: child,
        ),
        child: SafeArea(
          child: Padding(
            padding: const EdgeInsets.all(32.0),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                // Welcome text with fade-in
                FadeTransition(
                  opacity: _fadeAnimation1,
                  child: SlideTransition(
                    position: Tween<Offset>(
                      begin: const Offset(-0.2, 0),
                      end: Offset.zero,
                    ).animate(_fadeAnimation1),
                    child: Text(
                      'welcome to',
                      style: theme.textTheme.headlineMedium?.copyWith(
                        color: colorScheme.onSurface.withOpacity(0.7),
                        fontWeight: FontWeight.w300,
                        fontSize: 48,
                        letterSpacing: 2,
                      ),
                    ),
                  ),
                ),
                const SizedBox(height: 8),
                FadeTransition(
                  opacity: _fadeAnimation2,
                  child: SlideTransition(
                    position: Tween<Offset>(
                      begin: const Offset(-0.2, 0),
                      end: Offset.zero,
                    ).animate(_fadeAnimation2),
                    child: Text(
                      "blocks o' code (v3)",
                      style: theme.textTheme.displayMedium?.copyWith(
                        fontFamily: 'Modak',
                        color: colorScheme.primary,
                        fontWeight: FontWeight.normal,
                        fontSize: 80,
                        height: 1.2,
                      ),
                    ),
                  ),
                ),

                const SizedBox(height: 32),

                // Hero 3D-style block strip
                const _HeroBlocksStrip(),

                const Spacer(),

                // Connection status capsule (only when not idle)
                if (widget.connectionStatus != 'Server not started')
                  FadeTransition(
                    opacity: _fadeAnimation2,
                    child: Container(
                      padding: const EdgeInsets.all(20),
                      decoration: BoxDecoration(
                        color: colorScheme.primaryContainer,
                        borderRadius: BorderRadius.circular(18),
                      ),
                      child: Row(
                        children: [
                          SizedBox(
                            width: 20,
                            height: 20,
                            child: CircularProgressIndicator(
                              strokeWidth: 2,
                              valueColor: AlwaysStoppedAnimation<Color>(
                                colorScheme.primary,
                              ),
                            ),
                          ),
                          const SizedBox(width: 16),
                          Expanded(
                            child: Text(
                              widget.connectionStatus,
                              style: theme.textTheme.bodyLarge?.copyWith(
                                color: colorScheme.onPrimaryContainer,
                              ),
                            ),
                          ),
                        ],
                      ),
                    ),
                  ),

                const SizedBox(height: 16),

                // Onboarding steps
                _OnboardingStepsRow(
                  isServerRunning: widget.isServerRunning,
                  isConnected: widget.isConnected,
                  hasConfiguration: widget.hasConfiguration,
                ),

                const SizedBox(height: 16),

                // Primary Get Started CTA
                SizedBox(
                  width: double.infinity,
                  child: ElevatedButton.icon(
                    onPressed: widget.onGetStarted,
                    icon: const Icon(Icons.play_arrow_rounded),
                    label: const Text('Get Started'),
                    style: ElevatedButton.styleFrom(
                      backgroundColor: colorScheme.primary,
                      foregroundColor: Colors.white,
                      padding: const EdgeInsets.symmetric(vertical: 16),
                      textStyle: theme.textTheme.titleMedium?.copyWith(
                        fontWeight: FontWeight.w600,
                      ),
                    ),
                  ),
                ),

                const SizedBox(height: 12),

                // Stop Server control (only when running)
                if (widget.isServerRunning)
                  SizedBox(
                    width: double.infinity,
                    child: OutlinedButton.icon(
                      onPressed: widget.onStopServer,
                      icon: const Icon(Icons.stop_circle_rounded),
                      label: const Text('Stop Server'),
                      style: OutlinedButton.styleFrom(
                        foregroundColor: Colors.redAccent,
                        side: BorderSide(color: Colors.redAccent.withOpacity(0.7)),
                        padding: const EdgeInsets.symmetric(vertical: 14),
                      ),
                    ),
                  ),
              ],
            ),
          ),
        ),
      ),
    );
  }
}

class _HeroBlocksStrip extends StatelessWidget {
  const _HeroBlocksStrip();

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final colorScheme = theme.colorScheme;

    return LayoutBuilder(
      builder: (context, constraints) {
        final blockCount = 5;
        final width = constraints.maxWidth;
        final spacing = width / (blockCount + 1);

        return SizedBox(
          height: 170,
          child: Stack(
            children: List.generate(blockCount, (i) {
              final denom = blockCount - 1;
              final t = denom == 0 ? 0.0 : i / denom;
              final x = spacing * (i + 1);
              final baseColor = [
                colorScheme.primary,
                colorScheme.secondary,
                Colors.orange,
                colorScheme.tertiary,
                colorScheme.secondary,
              ][i % 5];

              return Positioned(
                left: x - 55,
                top: 40 + (1 - t) * 10,
                child: _HeroCube(color: baseColor),
              );
            }),
          ),
        );
      },
    );
  }
}

class _HeroCube extends StatelessWidget {
  final Color color;

  const _HeroCube({required this.color});

  @override
  Widget build(BuildContext context) {
    return SizedBox(
      width: 110,
      height: 110,
      child: CustomPaint(
        painter: _HeroCubePainter(color: color),
      ),
    );
  }
}

class _HeroCubePainter extends CustomPainter {
  final Color color;

  _HeroCubePainter({required this.color});

  @override
  void paint(Canvas canvas, Size size) {
    final cx = size.width / 2;
    final cy = size.height / 2 + 4;

    const double edge = 64;
    const double depth = 22;

    final frontTopLeft = Offset(cx - edge / 2, cy - edge / 2);
    final frontTopRight = Offset(cx + edge / 2, cy - edge / 2);
    final frontBottomLeft = Offset(cx - edge / 2, cy + edge / 2);
    final frontBottomRight = Offset(cx + edge / 2, cy + edge / 2);

    final depthOffset = Offset(depth * -0.7, depth * -0.6);

    final backTopLeft = frontTopLeft + depthOffset;
    final backTopRight = frontTopRight + depthOffset;
    final backBottomLeft = frontBottomLeft + depthOffset;
    final backBottomRight = frontBottomRight + depthOffset;

    final frontRect = Path()
      ..moveTo(frontTopLeft.dx, frontTopLeft.dy)
      ..lineTo(frontTopRight.dx, frontTopRight.dy)
      ..lineTo(frontBottomRight.dx, frontBottomRight.dy)
      ..lineTo(frontBottomLeft.dx, frontBottomLeft.dy)
      ..close();

    final topFace = Path()
      ..moveTo(backTopLeft.dx, backTopLeft.dy)
      ..lineTo(backTopRight.dx, backTopRight.dy)
      ..lineTo(frontTopRight.dx, frontTopRight.dy)
      ..lineTo(frontTopLeft.dx, frontTopLeft.dy)
      ..close();

    final sideFace = Path()
      ..moveTo(frontTopRight.dx, frontTopRight.dy)
      ..lineTo(backTopRight.dx, backTopRight.dy)
      ..lineTo(backBottomRight.dx, backBottomRight.dy)
      ..lineTo(frontBottomRight.dx, frontBottomRight.dy)
      ..close();

    final leftFace = Path()
      ..moveTo(backTopLeft.dx, backTopLeft.dy)
      ..lineTo(frontTopLeft.dx, frontTopLeft.dy)
      ..lineTo(frontBottomLeft.dx, frontBottomLeft.dy)
      ..lineTo(backBottomLeft.dx, backBottomLeft.dy)
      ..close();

    final hsl = HSLColor.fromColor(color);
    final topColor = hsl.withLightness((hsl.lightness + 0.18).clamp(0.0, 1.0)).toColor();
    final sideColor =
        hsl.withLightness((hsl.lightness - 0.12).clamp(0.0, 1.0)).toColor();
    final leftColor =
        hsl.withLightness((hsl.lightness - 0.18).clamp(0.0, 1.0)).toColor();

    canvas.drawPath(
      topFace,
      Paint()..color = topColor.withOpacity(0.95),
    );

    canvas.drawPath(
      sideFace,
      Paint()..color = sideColor.withOpacity(0.95),
    );

    canvas.drawPath(
      leftFace,
      Paint()..color = leftColor.withOpacity(0.95),
    );

    canvas.drawPath(
      frontRect,
      Paint()
        ..shader = LinearGradient(
          begin: Alignment.topLeft,
          end: Alignment.bottomRight,
          colors: [
            color.withOpacity(0.95),
            color.withOpacity(0.75),
          ],
        ).createShader(
          Rect.fromPoints(frontTopLeft, frontBottomRight),
        ),
    );

    final edgePaint = Paint()
      ..color = Colors.white.withOpacity(0.9)
      ..style = PaintingStyle.stroke
      ..strokeWidth = 2
      ..strokeJoin = StrokeJoin.miter;

    // Draw all cube edges for a rigid outline.
    final edges = <List<Offset>>[
      [frontTopLeft, frontTopRight],
      [frontTopRight, frontBottomRight],
      [frontBottomRight, frontBottomLeft],
      [frontBottomLeft, frontTopLeft],
      [backTopLeft, backTopRight],
      [backTopRight, backBottomRight],
      [backBottomRight, backBottomLeft],
      [backBottomLeft, backTopLeft],
      [frontTopLeft, backTopLeft],
      [frontTopRight, backTopRight],
      [frontBottomRight, backBottomRight],
      [frontBottomLeft, backBottomLeft],
    ];

    for (final e in edges) {
      canvas.drawLine(e[0], e[1], edgePaint);
    }
  }

  @override
  bool shouldRepaint(covariant _HeroCubePainter oldDelegate) {
    return oldDelegate.color != color;
  }
}

class _OnboardingStepsRow extends StatelessWidget {
  final bool isServerRunning;
  final bool isConnected;
  final bool hasConfiguration;

  const _OnboardingStepsRow({
    required this.isServerRunning,
    required this.isConnected,
    required this.hasConfiguration,
  });

  @override
  Widget build(BuildContext context) {
    final colorScheme = Theme.of(context).colorScheme;

    return Row(
      children: [
        _StepChip(
          index: 1,
          label: 'Start server',
          isDone: isServerRunning,
          color: colorScheme.primary,
        ),
        const SizedBox(width: 8),
        _StepChip(
          index: 2,
          label: 'Connect Brain Block',
          isDone: isConnected,
          color: colorScheme.secondary,
        ),
        const SizedBox(width: 8),
        _StepChip(
          index: 3,
          label: 'Detect blocks',
          isDone: hasConfiguration,
          color: colorScheme.tertiary,
        ),
      ],
    );
  }
}

class _StepChip extends StatelessWidget {
  final int index;
  final String label;
  final bool isDone;
  final Color color;

  const _StepChip({
    required this.index,
    required this.label,
    required this.isDone,
    required this.color,
  });

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);

    return Expanded(
      child: AnimatedContainer(
        duration: const Duration(milliseconds: 250),
        padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 10),
        decoration: BoxDecoration(
          borderRadius: BorderRadius.circular(20),
          gradient: LinearGradient(
            colors: isDone
                ? [color.withOpacity(0.9), color.withOpacity(0.7)]
                : [color.withOpacity(0.35), color.withOpacity(0.15)],
          ),
          border: Border.all(
            color: isDone ? Colors.white : color.withOpacity(0.6),
            width: 1.4,
          ),
        ),
        child: Row(
          mainAxisAlignment: MainAxisAlignment.center,
          children: [
            CircleAvatar(
              radius: 10,
              backgroundColor: Colors.white.withOpacity(isDone ? 1 : 0.7),
              child: isDone
                  ? Icon(
                      Icons.check,
                      size: 14,
                      color: color,
                    )
                  : Text(
                      '$index',
                      style: theme.textTheme.labelSmall?.copyWith(
                        fontWeight: FontWeight.w700,
                        color: color.withOpacity(0.9),
                      ),
                    ),
            ),
            const SizedBox(width: 8),
            Flexible(
              child: Text(
                label,
                maxLines: 1,
                overflow: TextOverflow.ellipsis,
                style: theme.textTheme.labelMedium?.copyWith(
                  color: Colors.white,
                  fontWeight: FontWeight.w600,
                ),
              ),
            ),
          ],
        ),
      ),
    );
  }
}

class _LogicBackgroundPainter extends CustomPainter {
  final ColorScheme colorScheme;
  final double t; // 0..1 looping animation phase

  _LogicBackgroundPainter({
    required this.colorScheme,
    required this.t,
  });

  @override
  void paint(Canvas canvas, Size size) {
    final pathPaint = Paint()
      ..style = PaintingStyle.stroke
      ..strokeWidth = 2
      ..color = colorScheme.primary.withOpacity(0.25);

    final nodePaint = Paint()..color = colorScheme.secondary.withOpacity(0.6);

    const rows = 4;
    for (var r = 0; r < rows; r++) {
      final tRow = r / (rows - 1);
      final phase = t * 2.4 * math.pi + tRow * 1.6;
      final wobble = math.sin(phase) * 22;
      final baseY = size.height * 0.5 + (tRow - 0.5) * 34 + wobble;

      final path = Path();
      path.moveTo(0, baseY);
      path.cubicTo(
        size.width * 0.18,
        baseY - 26 - 8 * tRow,
        size.width * 0.45,
        baseY + 32 + 12 * (1 - tRow),
        size.width * 0.7,
        baseY - 18,
      );
      path.quadraticBezierTo(
        size.width * 0.9,
        baseY + 10,
        size.width,
        baseY - 6,
      );
      canvas.drawPath(path, pathPaint);
    }

    // Logic "nodes" that follow a flowing, bouncing trajectory
    for (var i = 0; i < 8; i++) {
      final u = i / 7;
      final phase = t * 3.2 * math.pi + u * 2.8;
      final cx = size.width * (0.08 + 0.84 * u) + math.sin(phase) * 36;
      final cy =
          size.height * 0.55 + math.sin(phase * 1.7 + math.pi / 3) * 40;
      canvas.drawCircle(Offset(cx, cy), 3.5, nodePaint);
    }
  }

  @override
  bool shouldRepaint(covariant _LogicBackgroundPainter oldDelegate) {
    return oldDelegate.colorScheme != colorScheme || oldDelegate.t != t;
  }
}

// About Screen
class AboutScreen extends StatelessWidget {
  const AboutScreen({super.key});

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final colorScheme = theme.colorScheme;

    return Container(
      decoration: BoxDecoration(
        gradient: LinearGradient(
          begin: Alignment.topLeft,
          end: Alignment.bottomRight,
          colors: [
            colorScheme.surface,
            colorScheme.surfaceContainerHighest.withOpacity(0.3),
          ],
        ),
      ),
      child: SafeArea(
        child: SingleChildScrollView(
          padding: const EdgeInsets.all(32),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text(
                'About',
                style: theme.textTheme.displaySmall?.copyWith(
                  fontFamily: 'Modak',
                  color: colorScheme.primary,
                  fontWeight: FontWeight.normal,
                ),
              ),
              const SizedBox(height: 32),
              _buildSection(
                theme,
                colorScheme,
                Icons.code,
                'Blocks o\' Code: Reimagine the Building Blocks of Programming',
                'Understanding the logic behind software is a fundamental literacy in the modern world. However, many learners find screens and syntax-heavy coding languages intimidating. Blocks o\' Code is a tangible hardware platform that bridges the gap between physical play and digital logic. By turning abstract concepts like loops and logic gates into physical, magnetic blocks, we allow students to feel the flow of a program. Our goal is to move beyond the screen and into the hands of the next generation of creators.',
              ),
              const SizedBox(height: 24),
              _buildSection(
                theme,
                colorScheme,
                Icons.sensors,
                'How it Works',
                'The system uses a central Brain Block and a series of Child Blocks. These blocks connect via magnetic pogo-pin connectors to form a chain of logic that the Brain interprets in real time, then sends that data to this app.',
              ),
              const SizedBox(height: 24),
              _buildSection(
                theme,
                colorScheme,
                Icons.rocket_launch,
                'Getting Started',
                'Click "Get Started" in the menu to set up the TCP server and connect to your block configuration. Once connected, you\'ll be able to see your block visualization in real time.',
              ),
              const SizedBox(height: 24),
              Container(
                padding: const EdgeInsets.all(20),
                decoration: BoxDecoration(
                  color: colorScheme.primaryContainer.withOpacity(0.5),
                  borderRadius: BorderRadius.circular(16),
                  border: Border.all(
                    color: colorScheme.primary.withOpacity(0.3),
                    width: 2,
                  ),
                ),
                child: Row(
                  children: [
                    Icon(
                      Icons.info_outline,
                      color: colorScheme.primary,
                      size: 32,
                    ),
                    const SizedBox(width: 16),
                    Expanded(
                      child: Text(
                        'Version 3.0.0',
                        style: theme.textTheme.bodyLarge?.copyWith(
                          color: colorScheme.onPrimaryContainer,
                          fontWeight: FontWeight.w500,
                        ),
                      ),
                    ),
                  ],
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }

  Widget _buildSection(
    ThemeData theme,
    ColorScheme colorScheme,
    IconData icon,
    String title,
    String content,
  ) {
    return Container(
      padding: const EdgeInsets.all(20),
      decoration: BoxDecoration(
        color: colorScheme.surfaceContainerHighest.withOpacity(0.5),
        borderRadius: BorderRadius.circular(16),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              Icon(icon, color: colorScheme.primary, size: 28),
              const SizedBox(width: 12),
              Text(
                title,
                style: theme.textTheme.titleLarge?.copyWith(
                  fontWeight: FontWeight.bold,
                  color: colorScheme.onSurface,
                ),
              ),
            ],
          ),
          const SizedBox(height: 12),
          Text(
            content,
            style: theme.textTheme.bodyMedium?.copyWith(
              color: colorScheme.onSurface.withOpacity(0.8),
              height: 1.6,
            ),
          ),
        ],
      ),
    );
  }
}

// Settings Screen
class SettingsScreen extends StatelessWidget {
  const SettingsScreen({super.key});

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final colorScheme = theme.colorScheme;

    return Container(
      decoration: BoxDecoration(
        gradient: LinearGradient(
          begin: Alignment.topLeft,
          end: Alignment.bottomRight,
          colors: [
            colorScheme.surface,
            colorScheme.surfaceContainerHighest.withOpacity(0.3),
          ],
        ),
      ),
      child: SafeArea(
        child: SingleChildScrollView(
          padding: const EdgeInsets.all(32),
          child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(
                    'Settings',
                    style: theme.textTheme.displaySmall?.copyWith(
                      fontFamily: 'Modak',
                      color: colorScheme.primary,
                      fontWeight: FontWeight.normal,
                    ),
                  ),
                  const SizedBox(height: 32),
                  _buildSettingCard(
                    theme,
                    colorScheme,
                    Icons.palette_rounded,
                    'Theme',
                    'Customize app appearance',
                    colorScheme.primary,
                  ),
                  const SizedBox(height: 16),
                  _buildSettingCard(
                    theme,
                    colorScheme,
                    Icons.notifications_rounded,
                    'Notifications',
                    'Manage notification preferences',
                    colorScheme.secondary,
                  ),
                  const SizedBox(height: 16),
                  _buildSettingCard(
                    theme,
                    colorScheme,
                    Icons.wifi_rounded,
                    'Connection',
                    'TCP server and ESP32 settings',
                    colorScheme.tertiary,
                  ),
                  const SizedBox(height: 16),
                  _buildSettingCard(
                    theme,
                    colorScheme,
                    Icons.storage_rounded,
                    'Storage',
                    'Manage saved projects',
                    colorScheme.primary,
                  ),
                ],
              ),
            ),
          ),
        );
  }

  Widget _buildSettingCard(
    ThemeData theme,
    ColorScheme colorScheme,
    IconData icon,
    String title,
    String subtitle,
    Color color,
  ) {
    return Container(
      padding: const EdgeInsets.all(20),
      decoration: BoxDecoration(
        gradient: LinearGradient(
          begin: Alignment.topLeft,
          end: Alignment.bottomRight,
          colors: [
            color.withOpacity(0.2),
            color.withOpacity(0.05),
          ],
        ),
        borderRadius: BorderRadius.circular(20),
        border: Border.all(
          color: color.withOpacity(0.3),
          width: 1.5,
        ),
        boxShadow: [
          BoxShadow(
            color: color.withOpacity(0.2),
            blurRadius: 15,
            offset: const Offset(0, 5),
          ),
        ],
      ),
      child: Row(
        children: [
          Container(
            padding: const EdgeInsets.all(12),
            decoration: BoxDecoration(
              gradient: LinearGradient(
                colors: [color, color.withOpacity(0.7)],
              ),
              borderRadius: BorderRadius.circular(12),
            ),
            child: Icon(icon, color: Colors.white, size: 28),
          ),
          const SizedBox(width: 16),
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(
                  title,
                  style: theme.textTheme.titleLarge?.copyWith(
                    fontWeight: FontWeight.normal,
                    color: Colors.white,
                  ),
                ),
                const SizedBox(height: 4),
                Text(
                  subtitle,
                  style: theme.textTheme.bodyMedium?.copyWith(
                    color: Colors.white70,
                  ),
                ),
              ],
            ),
          ),
          Icon(
            Icons.chevron_right_rounded,
            color: color.withOpacity(0.7),
          ),
        ],
      ),
    );
  }
}

// Help Screen
class HelpScreen extends StatelessWidget {
  const HelpScreen({super.key});

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final colorScheme = theme.colorScheme;

    return Container(
      decoration: BoxDecoration(
        gradient: LinearGradient(
          begin: Alignment.topLeft,
          end: Alignment.bottomRight,
          colors: [
            colorScheme.surface,
            colorScheme.surfaceContainerHighest.withOpacity(0.3),
          ],
        ),
      ),
      child: SafeArea(
        child: SingleChildScrollView(
          padding: const EdgeInsets.all(32),
          child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(
                    'Help & Support',
                    style: theme.textTheme.displaySmall?.copyWith(
                      fontFamily: 'Modak',
                      color: colorScheme.primary,
                      fontWeight: FontWeight.normal,
                    ),
                  ),
                  const SizedBox(height: 32),
                  _buildHelpItem(
                    theme,
                    colorScheme,
                    Icons.question_answer_rounded,
                    'FAQ',
                    'Frequently asked questions',
                    colorScheme.primary,
                  ),
                  const SizedBox(height: 16),
                  _buildHelpItem(
                    theme,
                    colorScheme,
                    Icons.bug_report_rounded,
                    'Report Issue',
                    'Found a bug? Let us know!',
                    colorScheme.secondary,
                  ),
                  const SizedBox(height: 16),
                  _buildHelpItem(
                    theme,
                    colorScheme,
                    Icons.contact_support_rounded,
                    'Contact Us',
                    'Get in touch with our team',
                    colorScheme.tertiary,
                  ),
                  const SizedBox(height: 16),
                  _buildHelpItem(
                    theme,
                    colorScheme,
                    Icons.update_rounded,
                    'Updates',
                    'Check for app updates',
                    colorScheme.primary,
                  ),
                ],
              ),
            ),
          ),
        );
  }

  Widget _buildHelpItem(
    ThemeData theme,
    ColorScheme colorScheme,
    IconData icon,
    String title,
    String subtitle,
    Color color,
  ) {
    return Container(
      padding: const EdgeInsets.all(20),
      decoration: BoxDecoration(
        gradient: LinearGradient(
          begin: Alignment.topLeft,
          end: Alignment.bottomRight,
          colors: [
            color.withOpacity(0.2),
            color.withOpacity(0.05),
          ],
        ),
        borderRadius: BorderRadius.circular(20),
        border: Border.all(
          color: color.withOpacity(0.3),
          width: 1.5,
        ),
      ),
      child: Row(
        children: [
          Icon(icon, color: color, size: 32),
          const SizedBox(width: 16),
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(
                  title,
                  style: theme.textTheme.titleLarge?.copyWith(
                    fontWeight: FontWeight.bold,
                    color: Colors.white,
                  ),
                ),
                const SizedBox(height: 4),
                Text(
                  subtitle,
                  style: theme.textTheme.bodyMedium?.copyWith(
                    color: Colors.white70,
                  ),
                ),
              ],
            ),
          ),
        ],
      ),
    );
  }
}

// Tutorial Screen
class TutorialScreen extends StatelessWidget {
  const TutorialScreen({super.key});

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final colorScheme = theme.colorScheme;

    return Container(
      decoration: BoxDecoration(
        gradient: LinearGradient(
          begin: Alignment.topLeft,
          end: Alignment.bottomRight,
          colors: [
            colorScheme.surface,
            colorScheme.surfaceContainerHighest.withOpacity(0.3),
          ],
        ),
      ),
      child: SafeArea(
        child: SingleChildScrollView(
          padding: const EdgeInsets.all(32),
          child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(
                    'Tutorial',
                    style: theme.textTheme.displaySmall?.copyWith(
                      fontFamily: 'Modak',
                      color: colorScheme.primary,
                      fontWeight: FontWeight.bold,
                    ),
                  ),
                  const SizedBox(height: 32),
                  _buildTutorialStep(
                    theme,
                    colorScheme,
                    '1',
                    'Get Started',
                    'Click "Get Started" in the menu to begin',
                    colorScheme.primary,
                  ),
                  const SizedBox(height: 16),
                  _buildTutorialStep(
                    theme,
                    colorScheme,
                    '2',
                    'Connect ESP32',
                    'The app will set up a TCP server and connect to your ESP32',
                    colorScheme.secondary,
                  ),
                  const SizedBox(height: 16),
                  _buildTutorialStep(
                    theme,
                    colorScheme,
                    '3',
                    'Configure Blocks',
                    'Drag and drop blocks to create your program',
                    colorScheme.tertiary,
                  ),
                  const SizedBox(height: 16),
                  _buildTutorialStep(
                    theme,
                    colorScheme,
                    '4',
                    'Run & Test',
                    'Execute your program and see it in action!',
                    colorScheme.primary,
                  ),
                ],
              ),
            ),
          ),
        );
  }

  Widget _buildTutorialStep(
    ThemeData theme,
    ColorScheme colorScheme,
    String step,
    String title,
    String description,
    Color color,
  ) {
    return Container(
      padding: const EdgeInsets.all(20),
      decoration: BoxDecoration(
        gradient: LinearGradient(
          begin: Alignment.topLeft,
          end: Alignment.bottomRight,
          colors: [
            color.withOpacity(0.2),
            color.withOpacity(0.05),
          ],
        ),
        borderRadius: BorderRadius.circular(20),
        border: Border.all(
          color: color.withOpacity(0.3),
          width: 1.5,
        ),
      ),
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Container(
            width: 50,
            height: 50,
            decoration: BoxDecoration(
              gradient: LinearGradient(
                colors: [color, color.withOpacity(0.7)],
              ),
              shape: BoxShape.circle,
              boxShadow: [
                BoxShadow(
                  color: color.withOpacity(0.5),
                  blurRadius: 10,
                  spreadRadius: 2,
                ),
              ],
            ),
            child: Center(
              child: Text(
                step,
                style: theme.textTheme.titleLarge?.copyWith(
                  color: Colors.white,
                  fontWeight: FontWeight.bold,
                ),
              ),
            ),
          ),
          const SizedBox(width: 16),
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(
                  title,
                  style: theme.textTheme.titleLarge?.copyWith(
                    fontWeight: FontWeight.bold,
                    color: Colors.white,
                  ),
                ),
                const SizedBox(height: 8),
                Text(
                  description,
                  style: theme.textTheme.bodyMedium?.copyWith(
                    color: Colors.white70,
                    height: 1.5,
                  ),
                ),
              ],
            ),
          ),
        ],
      ),
    );
  }
}

// Block Configuration Screen
class BlockConfigScreen extends StatelessWidget {
  final bool isConnected;
  final String connectionStatus;
  final DateTime? lastHeartbeatTime;
  final bool isReconnecting;
  final int reconnectionAttempts;
  final bool isStressTesting;
  final String stressTestStats;
  final VoidCallback onStartStressTest;
  final VoidCallback onStopStressTest;
  final List<BlockTelemetry> receivedTelemetry;
  final BlockConfiguration? currentConfiguration;
  final List<RuleViolation> configViolations;
  final Function(String)? onLoadFakeConfig;
  final VoidCallback? onOfflineAddBlock;
  final VoidCallback? onOfflineRemoveBlock;
  final VoidCallback? onOfflineResetBlocks;

  const BlockConfigScreen({
    super.key,
    required this.isConnected,
    required this.connectionStatus,
    this.lastHeartbeatTime,
    this.isReconnecting = false,
    this.reconnectionAttempts = 0,
    this.isStressTesting = false,
    this.stressTestStats = '',
    required this.onStartStressTest,
    required this.onStopStressTest,
    this.receivedTelemetry = const [],
    this.currentConfiguration,
    this.configViolations = const [],
    this.onLoadFakeConfig,
    this.onOfflineAddBlock,
    this.onOfflineRemoveBlock,
    this.onOfflineResetBlocks,
  });

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final colorScheme = theme.colorScheme;

    return Container(
      decoration: BoxDecoration(
        gradient: LinearGradient(
          begin: Alignment.topLeft,
          end: Alignment.bottomRight,
          colors: [
            colorScheme.surface,
            colorScheme.surfaceContainerHighest.withOpacity(0.3),
          ],
        ),
      ),
      child: SafeArea(
        child: Column(
          children: [
            // Connection Status Bar
            Container(
              width: double.infinity,
              padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 12),
              decoration: BoxDecoration(
                gradient: LinearGradient(
                  colors: isConnected
                      ? [
                          colorScheme.primary,
                          colorScheme.secondary,
                          colorScheme.tertiary,
                        ]
                      : [
                          Colors.grey.shade700,
                          Colors.grey.shade600,
                        ],
                ),
                boxShadow: [
                  BoxShadow(
                    color: (isConnected ? colorScheme.primary : Colors.grey)
                        .withOpacity(0.5),
                    blurRadius: 15,
                    spreadRadius: 2,
                  ),
                ],
              ),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Row(
                    children: [
                      Container(
                        width: 12,
                        height: 12,
                        decoration: BoxDecoration(
                          color: isConnected ? Colors.green : Colors.red,
                          shape: BoxShape.circle,
                        ),
                      ),
                      const SizedBox(width: 12),
                      Expanded(
                        child: Text(
                          isConnected
                              ? 'Brain Block Connected'
                              : isReconnecting
                                  ? 'Reconnecting...'
                                  : 'Not Connected',
                          style: const TextStyle(
                            color: Colors.white,
                            fontWeight: FontWeight.bold,
                            fontSize: 16,
                          ),
                        ),
                      ),
                    ],
                  ),
                          if (isConnected && lastHeartbeatTime != null) ...[
                    const SizedBox(height: 4),
                    Text(
                      'Last heartbeat: ${_formatTimeSince(lastHeartbeatTime!)}',
                      style: TextStyle(
                        color: Colors.white.withOpacity(0.8),
                        fontSize: 12,
                      ),
                    ),
                  ],
                  if (isReconnecting) ...[
                    const SizedBox(height: 4),
                    Text(
                      'Attempt $reconnectionAttempts/5',
                      style: TextStyle(
                        color: Colors.white.withOpacity(0.8),
                        fontSize: 12,
                      ),
                    ),
                  ],
                ],
              ),
            ),
            
            // Main Content
            Expanded(
              child: SingleChildScrollView(
                padding: const EdgeInsets.all(16),
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.stretch,
                  children: [
                    // Fake Config Loader (for testing)
                    if (onLoadFakeConfig != null) ...[
                      Container(
                        padding: const EdgeInsets.all(16),
                        decoration: BoxDecoration(
                          color: Colors.amber.withOpacity(0.2),
                          borderRadius: BorderRadius.circular(16),
                          border: Border.all(
                            color: Colors.amber.withOpacity(0.5),
                            width: 1.5,
                          ),
                        ),
                        child: Column(
                          crossAxisAlignment: CrossAxisAlignment.start,
                          children: [
                            Row(
                              children: [
                                Icon(
                                  Icons.bug_report,
                                  color: Colors.amber.shade300,
                                ),
                                const SizedBox(width: 8),
                                Text(
                                  'Test Mode - Load Fake Config',
                                  style: theme.textTheme.titleMedium?.copyWith(
                                    fontWeight: FontWeight.bold,
                                    color: Colors.amber.shade300,
                                  ),
                                ),
                              ],
                            ),
                            const SizedBox(height: 12),
                            Row(
                              children: [
                                Expanded(
                                  child: ElevatedButton.icon(
                                    onPressed: () => onLoadFakeConfig!(
                                      'assets/sample_block_config.json',
                                    ),
                                    icon: const Icon(Icons.check_circle),
                                    label: const Text('Load Valid Config'),
                                    style: ElevatedButton.styleFrom(
                                      backgroundColor: Colors.green,
                                      foregroundColor: Colors.white,
                                    ),
                                  ),
                                ),
                                const SizedBox(width: 8),
                                Expanded(
                                  child: ElevatedButton.icon(
                                    onPressed: () => onLoadFakeConfig!(
                                      'assets/sample_block_config_invalid.json',
                                    ),
                                    icon: const Icon(Icons.error),
                                    label: const Text('Load Invalid Config'),
                                    style: ElevatedButton.styleFrom(
                                      backgroundColor: Colors.red,
                                      foregroundColor: Colors.white,
                                    ),
                                  ),
                                ),
                              ],
                            ),
                          ],
                        ),
                      ),
                      const SizedBox(height: 16),
                    ],

                    // Offline Block Controls (dev bypass mode, no WiFi/TCP).
                    if (kOfflineMode) ...[
                      Container(
                        padding: const EdgeInsets.all(16),
                        decoration: BoxDecoration(
                          color: colorScheme.primaryContainer.withOpacity(0.25),
                          borderRadius: BorderRadius.circular(16),
                          border: Border.all(
                            color: colorScheme.primary.withOpacity(0.3),
                            width: 1.5,
                          ),
                        ),
                        child: Column(
                          crossAxisAlignment: CrossAxisAlignment.start,
                          children: [
                            Row(
                              children: [
                                Icon(Icons.extension,
                                    color: colorScheme.primary),
                                const SizedBox(width: 8),
                                Text(
                                  'Offline Block Controls',
                                  style: theme.textTheme.titleMedium?.copyWith(
                                    fontWeight: FontWeight.bold,
                                    color: colorScheme.onPrimaryContainer,
                                  ),
                                ),
                              ],
                            ),
                            const SizedBox(height: 12),
                            Row(
                              children: [
                                Expanded(
                                  child: ElevatedButton.icon(
                                    onPressed: currentConfiguration != null
                                        ? onOfflineAddBlock
                                        : null,
                                    icon: const Icon(Icons.add),
                                    label: const Text('Add Block'),
                                  ),
                                ),
                                const SizedBox(width: 8),
                                Expanded(
                                  child: ElevatedButton.icon(
                                    onPressed: (currentConfiguration != null &&
                                                currentConfiguration!.blocks.length >
                                                    1)
                                            ? onOfflineRemoveBlock
                                            : null,
                                    icon: const Icon(Icons.remove),
                                    label: const Text('Remove Block'),
                                  ),
                                ),
                              ],
                            ),
                            const SizedBox(height: 8),
                            SizedBox(
                              width: double.infinity,
                              child: OutlinedButton.icon(
                                onPressed: onOfflineResetBlocks,
                                icon: const Icon(Icons.refresh),
                                label: const Text('Reset'),
                              ),
                            ),
                          ],
                        ),
                      ),
                      const SizedBox(height: 16),
                    ],

                    // Block Configuration Display
                    if (currentConfiguration != null) ...[
                      // 3D visualizer (Windows only) as an enhanced view.
                      Block3DVisualizer(configuration: currentConfiguration!),
                      const SizedBox(height: 16),
                      // Existing 2D list as a reliable, readable fallback.
                      _buildBlockConfigurationSection(
                        theme,
                        colorScheme,
                        currentConfiguration!,
                      ),
                      const SizedBox(height: 16),
                    ],

                    // Configuration Validation Section
                    if (configViolations.isNotEmpty) ...[
                      _buildValidationSection(theme, colorScheme, configViolations),
                      const SizedBox(height: 16),
                    ],

                    // Telemetry Info
                    if (receivedTelemetry.isNotEmpty) ...[
                      Container(
                        padding: const EdgeInsets.all(16),
                        decoration: BoxDecoration(
                          color: colorScheme.primaryContainer.withOpacity(0.5),
                          borderRadius: BorderRadius.circular(16),
                        ),
                        child: Column(
                          crossAxisAlignment: CrossAxisAlignment.start,
                          children: [
                            Row(
                              children: [
                                Icon(
                                  Icons.sensors,
                                  color: colorScheme.primary,
                                ),
                                const SizedBox(width: 8),
                                Text(
                                  'Telemetry Data',
                                  style: theme.textTheme.titleLarge?.copyWith(
                                    fontWeight: FontWeight.bold,
                                    color: colorScheme.onPrimaryContainer,
                                  ),
                                ),
                              ],
                            ),
                            const SizedBox(height: 8),
                            Text(
                              'Received: ${receivedTelemetry.length} messages',
                              style: theme.textTheme.bodyMedium?.copyWith(
                                color: colorScheme.onPrimaryContainer,
                              ),
                            ),
                            if (receivedTelemetry.isNotEmpty) ...[
                              const SizedBox(height: 4),
                              Text(
                                'Latest: ${receivedTelemetry.last.blockId ?? "Unknown"} - ${receivedTelemetry.last.timestamp.toString().substring(11, 19)}',
                                style: theme.textTheme.bodySmall?.copyWith(
                                  color: colorScheme.onPrimaryContainer.withOpacity(0.7),
                                ),
                              ),
                            ],
                          ],
                        ),
                      ),
                      const SizedBox(height: 16),
                    ],

                    // Stress Test Section
                    Container(
                      padding: const EdgeInsets.all(16),
                      decoration: BoxDecoration(
                        color: colorScheme.secondaryContainer.withOpacity(0.5),
                        borderRadius: BorderRadius.circular(16),
                        border: Border.all(
                          color: colorScheme.secondary.withOpacity(0.3),
                          width: 1.5,
                        ),
                      ),
                      child: Column(
                        crossAxisAlignment: CrossAxisAlignment.start,
                        children: [
                          Row(
                            children: [
                              Icon(
                                Icons.speed,
                                color: colorScheme.secondary,
                              ),
                              const SizedBox(width: 8),
                              Text(
                                'Stress Test',
                                style: theme.textTheme.titleLarge?.copyWith(
                                  fontWeight: FontWeight.bold,
                                  color: colorScheme.onSecondaryContainer,
                                ),
                              ),
                            ],
                          ),
                          const SizedBox(height: 12),
                          if (isStressTesting) ...[
                            Container(
                              padding: const EdgeInsets.all(12),
                              decoration: BoxDecoration(
                                color: Colors.orange.withOpacity(0.2),
                                borderRadius: BorderRadius.circular(8),
                              ),
                              child: Text(
                                stressTestStats,
                                style: theme.textTheme.bodyMedium?.copyWith(
                                  color: colorScheme.onSecondaryContainer,
                                  fontFamily: 'monospace',
                                ),
                              ),
                            ),
                            const SizedBox(height: 12),
                            SizedBox(
                              width: double.infinity,
                              child: ElevatedButton.icon(
                                onPressed: isConnected ? onStopStressTest : null,
                                icon: const Icon(Icons.stop),
                                label: const Text('Stop Stress Test'),
                                style: ElevatedButton.styleFrom(
                                  backgroundColor: Colors.red,
                                  foregroundColor: Colors.white,
                                  padding: const EdgeInsets.symmetric(vertical: 12),
                                ),
                              ),
                            ),
                          ] else ...[
                            Text(
                              'Test the TCP connection with high-frequency messages',
                              style: theme.textTheme.bodyMedium?.copyWith(
                                color: colorScheme.onSecondaryContainer.withOpacity(0.8),
                              ),
                            ),
                            const SizedBox(height: 12),
                            SizedBox(
                              width: double.infinity,
                              child: ElevatedButton.icon(
                                onPressed: isConnected ? onStartStressTest : null,
                                icon: const Icon(Icons.play_arrow),
                                label: const Text('Start Stress Test'),
                                style: ElevatedButton.styleFrom(
                                  backgroundColor: colorScheme.secondary,
                                  foregroundColor: Colors.white,
                                  padding: const EdgeInsets.symmetric(vertical: 12),
                                ),
                              ),
                            ),
                          ],
                        ],
                      ),
                    ),
                    const SizedBox(height: 16),

                    // Connection Status Details
                    Container(
                      padding: const EdgeInsets.all(16),
                      decoration: BoxDecoration(
                        color: colorScheme.surfaceContainerHighest.withOpacity(0.5),
                        borderRadius: BorderRadius.circular(16),
                      ),
                      child: Column(
                        crossAxisAlignment: CrossAxisAlignment.start,
                        children: [
                          Row(
                            children: [
                              Icon(
                                Icons.info_outline,
                                color: colorScheme.tertiary,
                              ),
                              const SizedBox(width: 8),
                              Text(
                                'Connection Status',
                                style: theme.textTheme.titleMedium?.copyWith(
                                  fontWeight: FontWeight.bold,
                                  color: colorScheme.onSurface,
                                ),
                              ),
                            ],
                          ),
                          const SizedBox(height: 8),
                          Builder(
                            builder: (context) {
                              final errorCount = configViolations
                                  .where((v) => v.severity == Severity.error)
                                  .length;
                              final statusText = currentConfiguration != null
                                  ? 'Block config: ${currentConfiguration!.totalBlocks} block(s), $errorCount error(s)'
                                  : connectionStatus;
                              return Text(
                                statusText,
                                style: theme.textTheme.bodyMedium?.copyWith(
                                  color: colorScheme.onSurface.withOpacity(0.8),
                                ),
                              );
                            },
                          ),
                        ],
                      ),
                    ),
                  ],
                ),
              ),
            ),
          ],
        ),
      ),
    );
  }

  static String _formatTimeSince(DateTime time) {
    final difference = DateTime.now().difference(time);
    if (difference.inSeconds < 60) {
      return '${difference.inSeconds}s ago';
    } else if (difference.inMinutes < 60) {
      return '${difference.inMinutes}m ${difference.inSeconds % 60}s ago';
    } else {
      return '${difference.inHours}h ${difference.inMinutes % 60}m ago';
    }
  }

  Widget _buildBlockConfigurationSection(
    ThemeData theme,
    ColorScheme colorScheme,
    BlockConfiguration config,
  ) {
    final errors = config.errors.where((e) => e.type == 'error' || e.type == 'communication').toList();
    
    return Container(
      padding: const EdgeInsets.all(16),
      decoration: BoxDecoration(
        color: colorScheme.tertiaryContainer.withOpacity(0.5),
        borderRadius: BorderRadius.circular(16),
        border: Border.all(
          color: colorScheme.tertiary.withOpacity(0.3),
          width: 1.5,
        ),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              Icon(
                Icons.view_module,
                color: colorScheme.tertiary,
              ),
              const SizedBox(width: 8),
              Text(
                'Block Configuration',
                style: theme.textTheme.titleLarge?.copyWith(
                  fontWeight: FontWeight.bold,
                  color: colorScheme.onTertiaryContainer,
                ),
              ),
            ],
          ),
          const SizedBox(height: 12),
          Text(
            'Total Blocks: ${config.totalBlocks}',
            style: theme.textTheme.bodyLarge?.copyWith(
              color: colorScheme.onTertiaryContainer,
              fontWeight: FontWeight.w600,
            ),
          ),
          const SizedBox(height: 12),
          // Block list
          ...config.blocks.asMap().entries.map((entry) {
            final index = entry.key;
            final block = entry.value;
            return _buildBlockItem(theme, colorScheme, block, index);
          }),
          // Hardware errors
          if (errors.isNotEmpty) ...[
            const SizedBox(height: 12),
            const Divider(),
            const SizedBox(height: 8),
            Text(
              'Hardware Errors:',
              style: theme.textTheme.titleSmall?.copyWith(
                color: Colors.red,
                fontWeight: FontWeight.bold,
              ),
            ),
            const SizedBox(height: 4),
            ...errors.map((error) => Padding(
              padding: const EdgeInsets.only(left: 8, top: 4),
              child: Text(
                '• ${error.message}',
                style: theme.textTheme.bodySmall?.copyWith(
                  color: Colors.red.shade300,
                ),
              ),
            )),
          ],
        ],
      ),
    );
  }

  Widget _buildBlockItem(
    ThemeData theme,
    ColorScheme colorScheme,
    BlockInfo block,
    int index,
  ) {
    final blockType = block.blockType;
    final blockColor = _getBlockTypeColor(colorScheme, blockType);
    
    return Container(
      margin: const EdgeInsets.only(bottom: 8),
      padding: const EdgeInsets.all(12),
      decoration: BoxDecoration(
        color: blockColor.withOpacity(0.2),
        borderRadius: BorderRadius.circular(12),
        border: Border.all(
          color: blockColor.withOpacity(0.5),
          width: 1,
        ),
      ),
      child: Row(
        children: [
          // Block index/position
          Container(
            width: 32,
            height: 32,
            decoration: BoxDecoration(
              color: blockColor,
              shape: BoxShape.circle,
            ),
            child: Center(
              child: Text(
                '$index',
                style: theme.textTheme.bodySmall?.copyWith(
                  color: Colors.white,
                  fontWeight: FontWeight.bold,
                ),
              ),
            ),
          ),
          const SizedBox(width: 12),
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(
                  blockType?.displayName ?? block.whoami.blockType ?? 'Unknown Block',
                  style: theme.textTheme.bodyLarge?.copyWith(
                    fontWeight: FontWeight.w600,
                    color: colorScheme.onTertiaryContainer,
                  ),
                ),
                const SizedBox(height: 4),
                Text(
                  'I2C: 0x${block.i2cAddress.toRadixString(16).toUpperCase().padLeft(2, '0')} | ID: ${block.whoami.blockId ?? "N/A"}',
                  style: theme.textTheme.bodySmall?.copyWith(
                    color: colorScheme.onTertiaryContainer.withOpacity(0.7),
                    fontFamily: 'monospace',
                  ),
                ),
              ],
            ),
          ),
        ],
      ),
    );
  }

  Color _getBlockTypeColor(ColorScheme colorScheme, BlockType? blockType) {
    if (blockType == null) return Colors.grey;
    
    switch (blockType.category) {
      case BlockCategory.controlSystem:
        return colorScheme.primary;
      case BlockCategory.controlFlow:
        return colorScheme.secondary;
      case BlockCategory.input:
        return Colors.orange;
      case BlockCategory.output:
        return colorScheme.tertiary;
    }
  }

  Widget _buildValidationSection(
    ThemeData theme,
    ColorScheme colorScheme,
    List<RuleViolation> violations,
  ) {
    final errors = violations.where((v) => v.severity == Severity.error).toList();
    final warnings = violations.where((v) => v.severity == Severity.warning).toList();
    
    return Container(
      padding: const EdgeInsets.all(16),
      decoration: BoxDecoration(
        color: errors.isNotEmpty 
            ? Colors.red.withOpacity(0.1)
            : Colors.orange.withOpacity(0.1),
        borderRadius: BorderRadius.circular(16),
        border: Border.all(
          color: errors.isNotEmpty 
              ? Colors.red.withOpacity(0.5)
              : Colors.orange.withOpacity(0.5),
          width: 1.5,
        ),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              Icon(
                errors.isNotEmpty ? Icons.error : Icons.warning,
                color: errors.isNotEmpty ? Colors.red : Colors.orange,
              ),
              const SizedBox(width: 8),
              Text(
                'Configuration Validation',
                style: theme.textTheme.titleLarge?.copyWith(
                  fontWeight: FontWeight.bold,
                  color: errors.isNotEmpty ? Colors.red : Colors.orange,
                ),
              ),
            ],
          ),
          const SizedBox(height: 12),
          if (errors.isNotEmpty) ...[
            Text(
              'Errors (${errors.length}):',
              style: theme.textTheme.titleMedium?.copyWith(
                color: Colors.red,
                fontWeight: FontWeight.bold,
              ),
            ),
            const SizedBox(height: 8),
            ...errors.map((violation) => _buildViolationItem(theme, colorScheme, violation, true)),
            if (warnings.isNotEmpty) const SizedBox(height: 12),
          ],
          if (warnings.isNotEmpty) ...[
            Text(
              'Warnings (${warnings.length}):',
              style: theme.textTheme.titleMedium?.copyWith(
                color: Colors.orange,
                fontWeight: FontWeight.bold,
              ),
            ),
            const SizedBox(height: 8),
            ...warnings.map((violation) => _buildViolationItem(theme, colorScheme, violation, false)),
          ],
        ],
      ),
    );
  }

  Widget _buildViolationItem(
    ThemeData theme,
    ColorScheme colorScheme,
    RuleViolation violation,
    bool isError,
  ) {
    return Container(
      margin: const EdgeInsets.only(bottom: 8),
      padding: const EdgeInsets.all(12),
      decoration: BoxDecoration(
        color: (isError ? Colors.red : Colors.orange).withOpacity(0.1),
        borderRadius: BorderRadius.circular(8),
        border: Border.all(
          color: (isError ? Colors.red : Colors.orange).withOpacity(0.3),
          width: 1,
        ),
      ),
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Icon(
            isError ? Icons.error_outline : Icons.warning_amber_rounded,
            color: isError ? Colors.red : Colors.orange,
            size: 20,
          ),
          const SizedBox(width: 8),
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(
                  violation.message,
                  style: theme.textTheme.bodyMedium?.copyWith(
                    color: isError ? Colors.red.shade200 : Colors.orange.shade200,
                  ),
                ),
                if (violation.blockIndex != null) ...[
                  const SizedBox(height: 4),
                  Text(
                    'Block Index: ${violation.blockIndex}',
                    style: theme.textTheme.bodySmall?.copyWith(
                      color: (isError ? Colors.red : Colors.orange).withOpacity(0.7),
                      fontFamily: 'monospace',
                    ),
                  ),
                ],
              ],
            ),
          ),
        ],
      ),
    );
  }
}
