import 'package:flutter/material.dart';
import 'hero_cube_painter.dart';

class HeroCube extends StatelessWidget {
  final Color color;

  const HeroCube({super.key, required this.color});

  @override
  Widget build(BuildContext context) {
    return SizedBox(
      width: 110,
      height: 110,
      child: CustomPaint(
        painter: HeroCubePainter(color: color),
      ),
    );
  }
}
