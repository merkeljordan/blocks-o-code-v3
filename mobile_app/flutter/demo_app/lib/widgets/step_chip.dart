import 'package:flutter/material.dart';

class StepChip extends StatelessWidget {
  final int index;
  final String label;
  final bool isDone;
  final Color color;

  const StepChip({
    super.key,
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
