import 'package:flutter/material.dart';

class HeroCubePainter extends CustomPainter {
  final Color color;

  HeroCubePainter({required this.color});

  @override
  void paint(Canvas canvas, Size size) {
    final cx = size.width / 2;
    final cy = size.height / 2 + 4;

    const double edge = 64;
    const double depth = 22;

    final frontTopLeft = Offset(cx - edge / 2, cy - edge / 2);
    final frontTopRight = Offset(cx + edge / 2, cy - edge / 2);
    final frontBottomLeft = Offset(cx - edge / 2, cy + edge / 2);
    final frontBottomRight = Offset(cx + edge / 2, cy + edge / 2);

    final depthOffset = Offset(depth * -0.7, depth * -0.6);

    final backTopLeft = frontTopLeft + depthOffset;
    final backTopRight = frontTopRight + depthOffset;
    final backBottomLeft = frontBottomLeft + depthOffset;
    final backBottomRight = frontBottomRight + depthOffset;

    final frontRect = Path()
      ..moveTo(frontTopLeft.dx, frontTopLeft.dy)
      ..lineTo(frontTopRight.dx, frontTopRight.dy)
      ..lineTo(frontBottomRight.dx, frontBottomRight.dy)
      ..lineTo(frontBottomLeft.dx, frontBottomLeft.dy)
      ..close();

    final topFace = Path()
      ..moveTo(backTopLeft.dx, backTopLeft.dy)
      ..lineTo(backTopRight.dx, backTopRight.dy)
      ..lineTo(frontTopRight.dx, frontTopRight.dy)
      ..lineTo(frontTopLeft.dx, frontTopLeft.dy)
      ..close();

    final sideFace = Path()
      ..moveTo(frontTopRight.dx, frontTopRight.dy)
      ..lineTo(backTopRight.dx, backTopRight.dy)
      ..lineTo(backBottomRight.dx, backBottomRight.dy)
      ..lineTo(frontBottomRight.dx, frontBottomRight.dy)
      ..close();

    final leftFace = Path()
      ..moveTo(backTopLeft.dx, backTopLeft.dy)
      ..lineTo(frontTopLeft.dx, frontTopLeft.dy)
      ..lineTo(frontBottomLeft.dx, frontBottomLeft.dy)
      ..lineTo(backBottomLeft.dx, backBottomLeft.dy)
      ..close();

    final hsl = HSLColor.fromColor(color);
    final topColor = hsl.withLightness((hsl.lightness + 0.18).clamp(0.0, 1.0)).toColor();
    final sideColor =
        hsl.withLightness((hsl.lightness - 0.12).clamp(0.0, 1.0)).toColor();
    final leftColor =
        hsl.withLightness((hsl.lightness - 0.18).clamp(0.0, 1.0)).toColor();

    canvas.drawPath(
      topFace,
      Paint()..color = topColor.withOpacity(0.95),
    );

    canvas.drawPath(
      sideFace,
      Paint()..color = sideColor.withOpacity(0.95),
    );

    canvas.drawPath(
      leftFace,
      Paint()..color = leftColor.withOpacity(0.95),
    );

    canvas.drawPath(
      frontRect,
      Paint()
        ..shader = LinearGradient(
          begin: Alignment.topLeft,
          end: Alignment.bottomRight,
          colors: [
            color.withOpacity(0.95),
            color.withOpacity(0.75),
          ],
        ).createShader(
          Rect.fromPoints(frontTopLeft, frontBottomRight),
        ),
    );

    final edgePaint = Paint()
      ..color = Colors.white.withOpacity(0.9)
      ..style = PaintingStyle.stroke
      ..strokeWidth = 2
      ..strokeJoin = StrokeJoin.miter;

    // Draw all cube edges for a rigid outline.
    final edges = <List<Offset>>[
      [frontTopLeft, frontTopRight],
      [frontTopRight, frontBottomRight],
      [frontBottomRight, frontBottomLeft],
      [frontBottomLeft, frontTopLeft],
      [backTopLeft, backTopRight],
      [backTopRight, backBottomRight],
      [backBottomRight, backBottomLeft],
      [backBottomLeft, backTopLeft],
      [frontTopLeft, backTopLeft],
      [frontTopRight, backTopRight],
      [frontBottomRight, backBottomRight],
      [frontBottomLeft, backBottomLeft],
    ];

    for (final e in edges) {
      canvas.drawLine(e[0], e[1], edgePaint);
    }
  }

  @override
  bool shouldRepaint(covariant HeroCubePainter oldDelegate) {
    return oldDelegate.color != color;
  }
}
