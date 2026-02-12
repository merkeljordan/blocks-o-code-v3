import 'package:flutter/material.dart';
import 'dart:math' as math;
import '../widgets/hero_cube.dart';

class HeroBlocksStrip extends StatelessWidget {
  const HeroBlocksStrip({super.key});

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
                child: HeroCube(color: baseColor),
              );
            }),
          ),
        );
      },
    );
  }
}
