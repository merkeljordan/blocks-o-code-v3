import 'package:flutter/material.dart';

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
