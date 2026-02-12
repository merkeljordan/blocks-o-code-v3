import 'package:flutter/material.dart';
import 'dart:math' as math;

class LogicBackgroundPainter extends CustomPainter {
  final ColorScheme colorScheme;
  final double t; // 0..1 looping animation phase

  LogicBackgroundPainter({
    required this.colorScheme,
    required this.t,
  });

  @override
  void paint(Canvas canvas, Size size) {
    final pathPaint = Paint()
      ..style = PaintingStyle.stroke
      ..strokeWidth = 2
      ..color = colorScheme.primary.withOpacity(0.25);

    final nodePaint = Paint()..color = colorScheme.secondary.withOpacity(0.6);

    const rows = 4;
    for (var r = 0; r < rows; r++) {
      final tRow = r / (rows - 1);
      final phase = t * 2.4 * math.pi + tRow * 1.6;
      final wobble = math.sin(phase) * 22;
      final baseY = size.height * 0.5 + (tRow - 0.5) * 34 + wobble;

      final path = Path();
      path.moveTo(0, baseY);
      path.cubicTo(
        size.width * 0.18,
        baseY - 26 - 8 * tRow,
        size.width * 0.45,
        baseY + 32 + 12 * (1 - tRow),
        size.width * 0.7,
        baseY - 18,
      );
      path.quadraticBezierTo(
        size.width * 0.9,
        baseY + 10,
        size.width,
        baseY - 6,
      );
      canvas.drawPath(path, pathPaint);
    }

    // Logic "nodes" that follow a flowing, bouncing trajectory
    for (var i = 0; i < 8; i++) {
      final u = i / 7;
      final phase = t * 3.2 * math.pi + u * 2.8;
      final cx = size.width * (0.08 + 0.84 * u) + math.sin(phase) * 36;
      final cy =
          size.height * 0.55 + math.sin(phase * 1.7 + math.pi / 3) * 40;
      canvas.drawCircle(Offset(cx, cy), 3.5, nodePaint);
    }
  }

  @override
  bool shouldRepaint(covariant LogicBackgroundPainter oldDelegate) {
    return oldDelegate.colorScheme != colorScheme || oldDelegate.t != t;
  }
}
