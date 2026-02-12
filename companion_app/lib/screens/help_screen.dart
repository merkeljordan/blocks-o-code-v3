import 'package:flutter/material.dart';

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
