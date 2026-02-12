import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'dart:io';
import 'dart:async';
import 'dart:convert';
import '../config/app_config.dart';
import '../utils/navigation.dart';
import '../models/block_telemetry.dart';
import '../services/telemetry_parser.dart';
import '../models/block_configuration.dart';
import '../models/configuration_rules.dart';
import '../services/block_config_parser.dart';
import '../services/configuration_validator.dart';
import 'welcome_screen.dart';
import 'about_screen.dart';
import 'block_config_screen.dart';
import 'settings_screen.dart';
import 'help_screen.dart';
import 'tutorial_screen.dart';

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

  // Telemetry parsing
  final TelemetryParser _telemetryParser = TelemetryParser();
  List<BlockTelemetry> _receivedTelemetry = [];

  // Block configuration
  final BlockConfigParser _configParser = BlockConfigParser();
  final ConfigurationValidator _configValidator = ConfigurationValidator();
  BlockConfiguration? _currentConfiguration;
  List<RuleViolation> _configViolations = [];

  // Heartbeat mechanism
  Timer? _heartbeatTimer;
  DateTime? _lastHeartbeatTime;
  bool _isReconnecting = false;
  int _reconnectionAttempts = 0;

  // Stress testing
  bool _isStressTesting = false;
  Timer? _stressTestTimer;
  int _stressTestMessagesSent = 0;
  int _stressTestMessagesReceived = 0;
  int _stressTestErrors = 0;
  DateTime? _stressTestStartTime;
  int _stressTestMessageRate = AppConfig.defaultStressTestMessageRate;

  late AnimationController _menuAnimationController;
  late Animation<double> _menuAnimation;

  @override
  void initState() {
    super.initState();
    _menuAnimationController = AnimationController(
      vsync: this,
      duration: AppConfig.menuAnimationDuration,
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
          connectionStatus = 'Server already running on port ${AppConfig.tcpPort}';
        });
        return;
      }

      setState(() {
        connectionStatus = 'Setting up TCP server...';
      });

      // Start TCP server
      tcpServer = await ServerSocket.bind(InternetAddress.anyIPv4, AppConfig.tcpPort);

      setState(() {
        connectionStatus = 'Server listening on port ${AppConfig.tcpPort} and address ${tcpServer!.address.address}';
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
          // Keep only last N telemetry entries
          if (_receivedTelemetry.length > AppConfig.maxTelemetryEntries) {
            _receivedTelemetry = _receivedTelemetry.sublist(_receivedTelemetry.length - AppConfig.maxTelemetryEntries);
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
      client.write(msg + '\n');
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

    _heartbeatTimer = Timer.periodic(AppConfig.heartbeatInterval, (timer) {
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
      _clientSocket!.write(heartbeat + '\n');
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
    if (timeSinceLastHeartbeat > AppConfig.heartbeatTimeout) {
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

    if (_reconnectionAttempts >= AppConfig.maxReconnectionAttempts) {
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
      connectionStatus = 'Reconnecting in ${delaySeconds}s (attempt $_reconnectionAttempts/${AppConfig.maxReconnectionAttempts})...';
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

    final testDuration = duration ?? AppConfig.defaultStressTestDuration;
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
            duration: AppConfig.screenTransitionDuration,
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
      duration: AppConfig.menuAnimationDuration,
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
          duration: AppConfig.menuAnimationDuration,
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
      key: ValueKey('menu_${isConnected}'),
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
