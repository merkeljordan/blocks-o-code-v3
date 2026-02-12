import 'package:flutter/material.dart';
import '../widgets/hero_blocks_strip.dart';
import '../widgets/onboarding_steps_row.dart';
import '../widgets/logic_background_painter.dart';
import 'dart:math' as math;

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
          painter: LogicBackgroundPainter(
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
                const HeroBlocksStrip(),

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
                OnboardingStepsRow(
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
