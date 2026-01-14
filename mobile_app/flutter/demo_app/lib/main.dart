import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'dart:io';
import 'dart:async';
import 'dart:ui';

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
  String connectionStatus = 'Not connected';
  
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
    tcpServer?.close();
    super.dispose();
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

  Future<void> _setupTCPServerAndConnect() async {
    try {
      // Simulate TCP server setup
      setState(() {
        connectionStatus = 'Setting up TCP server...';
      });
      
      await Future.delayed(const Duration(milliseconds: 800));
      
      // Simulate server listening
      setState(() {
        connectionStatus = 'TCP server listening on port 8080...';
      });
      
      await Future.delayed(const Duration(milliseconds: 1000));
      
      // Simulate ESP32 connection
      setState(() {
        connectionStatus = 'Searching for ESP32...';
      });
      
      await Future.delayed(const Duration(milliseconds: 1200));
      
      // Simulate successful connection
      setState(() {
        connectionStatus = 'ESP32 connected!';
        isConnected = true;
      });
      
      await Future.delayed(const Duration(milliseconds: 500));
      
      // Navigate to block configuration screen
      _navigateToScreen(ScreenType.blockConfig);
    } catch (e) {
      setState(() {
        connectionStatus = 'Connection failed: $e';
      });
    }
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
        );
        break;
      case ScreenType.about:
        screen = const AboutScreen(key: ValueKey('about'));
        break;
      case ScreenType.blockConfig:
        screen = BlockConfigScreen(
          key: const ValueKey('blockConfig'),
          isConnected: isConnected,
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
              if (!isConnected) {
                _setupTCPServerAndConnect();
              } else {
                _navigateToScreen(ScreenType.blockConfig);
              }
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

  const WelcomeScreen({
    super.key,
    required this.isConnected,
    required this.connectionStatus,
  });

  @override
  State<WelcomeScreen> createState() => _WelcomeScreenState();
}

class _WelcomeScreenState extends State<WelcomeScreen>
    with SingleTickerProviderStateMixin {
  late AnimationController _fadeController;
  late Animation<double> _fadeAnimation1;
  late Animation<double> _fadeAnimation2;

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
  }

  @override
  void dispose() {
    _fadeController.dispose();
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
            colorScheme.surfaceContainerHighest.withOpacity(0.3),
          ],
        ),
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
                  
                  const Spacer(),
                  
                  // Connection status if connecting
                  if (widget.connectionStatus != 'Not connected')
                    FadeTransition(
                      opacity: _fadeAnimation2,
                      child: Container(
                        padding: const EdgeInsets.all(20),
                        decoration: BoxDecoration(
                          color: colorScheme.primaryContainer,
                          borderRadius: BorderRadius.circular(16),
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
                  
                  const SizedBox(height: 40),
                ],
              ),
            ),
          ),
        );
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

// Block Configuration Screen (placeholder)
class BlockConfigScreen extends StatelessWidget {
  final bool isConnected;

  const BlockConfigScreen({super.key, required this.isConnected});

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
                  colors: [
                    colorScheme.primary,
                    colorScheme.secondary,
                    colorScheme.tertiary,
                  ],
                ),
                boxShadow: [
                  BoxShadow(
                    color: colorScheme.primary.withOpacity(0.5),
                    blurRadius: 15,
                    spreadRadius: 2,
                  ),
                ],
              ),
              child: Row(
                children: [
                  Container(
                    width: 12,
                    height: 12,
                    decoration: const BoxDecoration(
                      color: Colors.white,
                      shape: BoxShape.circle,
                    ),
                  ),
                  const SizedBox(width: 12),
                  Expanded(
                    child: Text(
                      isConnected ? 'ESP32 Connected' : 'Not Connected',
                      style: const TextStyle(
                        color: Colors.white,
                        fontWeight: FontWeight.bold,
                        fontSize: 16,
                      ),
                    ),
                  ),
                ],
              ),
            ),
            
            // Main Content
            Expanded(
              child: Center(
                child: Padding(
                  padding: const EdgeInsets.all(32),
                  child: Column(
                    mainAxisAlignment: MainAxisAlignment.center,
                    children: [
                      Container(
                        padding: const EdgeInsets.all(20),
                        decoration: BoxDecoration(
                          gradient: LinearGradient(
                            colors: [
                              colorScheme.primary,
                              colorScheme.secondary,
                              colorScheme.tertiary,
                            ],
                          ),
                          shape: BoxShape.circle,
                          boxShadow: [
                            BoxShadow(
                              color: colorScheme.primary.withOpacity(0.5),
                              blurRadius: 20,
                              spreadRadius: 5,
                            ),
                          ],
                        ),
                        child: Icon(
                          Icons.extension,
                          size: 60,
                          color: Colors.white,
                        ),
                      ),
                      const SizedBox(height: 24),
                      Text(
                        'Block Configuration',
                        style: theme.textTheme.headlineMedium?.copyWith(
                          fontWeight: FontWeight.bold,
                          color: colorScheme.onSurface,
                        ),
                      ),
                      const SizedBox(height: 16),
                      Text(
                        'Configure your blocks here',
                        style: theme.textTheme.bodyLarge?.copyWith(
                          color: colorScheme.onSurface.withOpacity(0.7),
                        ),
                      ),
                      const SizedBox(height: 32),
                      Container(
                        padding: const EdgeInsets.all(20),
                        decoration: BoxDecoration(
                          color: colorScheme.primaryContainer.withOpacity(0.5),
                          borderRadius: BorderRadius.circular(16),
                        ),
                        child: Text(
                          'This screen will contain the block programming interface',
                          style: theme.textTheme.bodyMedium?.copyWith(
                            color: colorScheme.onPrimaryContainer,
                          ),
                          textAlign: TextAlign.center,
                        ),
                      ),
                    ],
                  ),
                ),
              ),
            ),
          ],
        ),
      ),
    );
  }
}
