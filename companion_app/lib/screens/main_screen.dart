import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:provider/provider.dart';
import 'dart:async';
import 'dart:convert';
import '../config/app_config.dart';
import '../utils/navigation.dart';
import '../providers/connection_provider.dart';
import '../providers/block_config_provider.dart';
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

    // Set up message listener for block config provider
    WidgetsBinding.instance.addPostFrameCallback((_) {
      final connectionProvider = context.read<ConnectionProvider>();
      final blockConfigProvider = context.read<BlockConfigProvider>();
      blockConfigProvider.listenToMessages(connectionProvider.messageStream);
    });
  }

  @override
  void dispose() {
    _menuAnimationController.dispose();
    _stressTestTimer?.cancel();
    super.dispose();
  }

  void _handleGetStarted() {
    final connectionProvider = context.read<ConnectionProvider>();
    // Start server if needed; otherwise just navigate without interrupting it.
    if (!connectionProvider.isServerRunning) {
      connectionProvider.startServer();
    }
    _navigateToScreen(connectionProvider.isConnected ? ScreenType.blockConfig : ScreenType.welcome);
  }

  /// Load fake block configuration from assets for testing
  Future<void> _loadFakeConfiguration(String assetPath) async {
    try {
      final jsonString = await rootBundle.loadString(assetPath);
      final blockConfigProvider = context.read<BlockConfigProvider>();
      final connectionProvider = context.read<ConnectionProvider>();
      // Process it the same way as a real TCP message
      blockConfigProvider.processMessage(jsonString);
      connectionProvider.updateConnectionStatus('Loaded fake config from $assetPath');
    } catch (e) {
      final connectionProvider = context.read<ConnectionProvider>();
      connectionProvider.updateConnectionStatus('Failed to load fake config: $e');
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

    final connectionProvider = context.read<ConnectionProvider>();
    final blockConfigProvider = context.read<BlockConfigProvider>();
    
    final testDuration = duration ?? AppConfig.defaultStressTestDuration;
    final interval = Duration(milliseconds: 1000 ~/ _stressTestMessageRate);

    _stressTestTimer = Timer.periodic(interval, (timer) {
      if (!_isStressTesting || !connectionProvider.isConnected) {
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
        connectionProvider.sendMessage(testMessage);
        setState(() {
          _stressTestMessagesSent++;
        });
      } catch (e) {
        setState(() {
          _stressTestErrors++;
        });
      }
    });

    // Track received messages
    blockConfigProvider.addListener(() {
      if (_isStressTesting) {
        setState(() {
          _stressTestMessagesReceived = blockConfigProvider.receivedTelemetry.length;
        });
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
    final connectionProvider = context.watch<ConnectionProvider>();
    final blockConfigProvider = context.watch<BlockConfigProvider>();

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
            child: _buildCurrentScreen(theme, colorScheme, connectionProvider, blockConfigProvider),
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
                      child: _buildMenu(colorScheme, theme, connectionProvider),
                    ),
                  ),
                ),
              ],
            ),
        ],
      ),
    );
  }

  Widget _buildCurrentScreen(
    ThemeData theme,
    ColorScheme colorScheme,
    ConnectionProvider connectionProvider,
    BlockConfigProvider blockConfigProvider,
  ) {
    Widget screen;
    switch (currentScreen) {
      case ScreenType.welcome:
        screen = WelcomeScreen(
          key: const ValueKey('welcome'),
          isConnected: connectionProvider.isConnected,
          connectionStatus: connectionProvider.connectionStatus,
          isServerRunning: connectionProvider.isServerRunning,
          onStopServer: () => connectionProvider.stopServer(),
          hasConfiguration: blockConfigProvider.currentConfiguration != null,
          onGetStarted: _handleGetStarted,
        );
        break;
      case ScreenType.about:
        screen = const AboutScreen(key: ValueKey('about'));
        break;
      case ScreenType.blockConfig:
        screen = BlockConfigScreen(
          key: const ValueKey('blockConfig'),
          isConnected: connectionProvider.isConnected,
          connectionStatus: connectionProvider.connectionStatus,
          lastHeartbeatTime: connectionProvider.lastHeartbeatTime,
          isReconnecting: connectionProvider.isReconnecting,
          reconnectionAttempts: connectionProvider.reconnectionAttempts,
          isStressTesting: _isStressTesting,
          stressTestStats: _getStressTestStats(),
          onStartStressTest: () => _startStressTest(),
          onStopStressTest: _stopStressTest,
          receivedTelemetry: blockConfigProvider.receivedTelemetry,
          currentConfiguration: blockConfigProvider.currentConfiguration,
          configViolations: blockConfigProvider.configViolations,
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

  Widget _buildMenu(ColorScheme colorScheme, ThemeData theme, ConnectionProvider connectionProvider) {
    return Container(
      key: ValueKey('menu_${connectionProvider.isConnected}'),
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
