import 'package:flutter/material.dart';
import '../models/block_telemetry.dart';
import '../models/block_configuration.dart';
import '../models/configuration_rules.dart';
import '../models/block_type.dart';
import '../widgets/block_3d_visualizer.dart';

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
