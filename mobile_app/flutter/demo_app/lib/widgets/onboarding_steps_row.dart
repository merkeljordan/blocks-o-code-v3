import 'package:flutter/material.dart';
import 'step_chip.dart';

class OnboardingStepsRow extends StatelessWidget {
  final bool isServerRunning;
  final bool isConnected;
  final bool hasConfiguration;

  const OnboardingStepsRow({
    super.key,
    required this.isServerRunning,
    required this.isConnected,
    required this.hasConfiguration,
  });

  @override
  Widget build(BuildContext context) {
    final colorScheme = Theme.of(context).colorScheme;

    return Row(
      children: [
        StepChip(
          index: 1,
          label: 'Start server',
          isDone: isServerRunning,
          color: colorScheme.primary,
        ),
        const SizedBox(width: 8),
        StepChip(
          index: 2,
          label: 'Connect Brain Block',
          isDone: isConnected,
          color: colorScheme.secondary,
        ),
        const SizedBox(width: 8),
        StepChip(
          index: 3,
          label: 'Detect blocks',
          isDone: hasConfiguration,
          color: colorScheme.tertiary,
        ),
      ],
    );
  }
}
