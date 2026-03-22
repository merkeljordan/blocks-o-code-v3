import 'dart:math' as math;

import 'package:flutter/foundation.dart';
import 'package:flutter/gestures.dart';
import 'package:flutter/material.dart';

import '../models/block_configuration.dart';
import '../models/block_type.dart';

/// Windows-only pseudo-3D visualizer for block configurations.
///
/// Uses a perspective-ish transform and gradients/shadows to suggest depth.
/// On non-Windows platforms this should generally be hidden by callers.
class Block3DVisualizer extends StatefulWidget {
  final BlockConfiguration configuration;

  const Block3DVisualizer({
    super.key,
    required this.configuration,
  });

  @override
  State<Block3DVisualizer> createState() => _Block3DVisualizerState();
}

class _Block3DVisualizerState extends State<Block3DVisualizer>
    with SingleTickerProviderStateMixin {
  // Camera angles in radians.
  double _yaw = 0.4; // left/right
  double _pitch = 0.3; // up/down

  // For drag interaction.
  Offset? _lastDragPosition;

  // Brief glow animation for snap/joint changes.
  late final AnimationController _glowController;
  Set<int> _glowBlockIndices = <int>{};

  bool get _isWindows =>
      defaultTargetPlatform == TargetPlatform.windows && !kIsWeb;

  @override
  void initState() {
    super.initState();
    _glowController = AnimationController(
      vsync: this,
      duration: const Duration(milliseconds: 550),
    )..addListener(() {
        // Drive repaints for the glow fade-out.
        if (mounted) setState(() {});
      });
  }

  @override
  void dispose() {
    _glowController.dispose();
    super.dispose();
  }

  @override
  void didUpdateWidget(covariant Block3DVisualizer oldWidget) {
    super.didUpdateWidget(oldWidget);

    final oldBlocks = oldWidget.configuration.blocks;
    final newBlocks = widget.configuration.blocks;

    bool same = oldBlocks.length == newBlocks.length;
    if (same) {
      for (var i = 0; i < oldBlocks.length; i++) {
        if (oldBlocks[i].i2cAddress != newBlocks[i].i2cAddress) {
          same = false;
          break;
        }
      }
    }
    if (same) return;

    final oldIds = oldBlocks.map((b) => b.i2cAddress).toList(growable: false);
    final newIds = newBlocks.map((b) => b.i2cAddress).toList(growable: false);
    final minLen = math.min(oldIds.length, newIds.length);

    int prefix = 0;
    while (prefix < minLen && oldIds[prefix] == newIds[prefix]) {
      prefix++;
    }

    int suffix = 0;
    while (suffix < minLen - prefix &&
        oldIds[oldIds.length - 1 - suffix] ==
            newIds[newIds.length - 1 - suffix]) {
      suffix++;
    }

    final start = prefix;
    final endExclusive = newIds.length - suffix;
    final indices = <int>{};
    for (var i = start; i < endExclusive; i++) {
      indices.add(i);
    }

    setState(() {
      _glowBlockIndices = indices;
    });
    _glowController.forward(from: 0.0);
  }

  @override
  Widget build(BuildContext context) {
    if (!_isWindows) {
      // Keep it safe: don't render heavy visuals on non-Windows targets.
      return const SizedBox.shrink();
    }

    final theme = Theme.of(context);
    final colorScheme = theme.colorScheme;

    return GestureDetector(
        onPanStart: (details) {
          _lastDragPosition = details.localPosition;
        },
        onPanUpdate: (details) {
          if (_lastDragPosition == null) return;
          final delta = details.localPosition - _lastDragPosition!;
          _lastDragPosition = details.localPosition;

          setState(() {
            // Horizontal drag: adjust yaw.
            _yaw += delta.dx * 0.01;
            // Vertical drag: adjust pitch, clamped to avoid flipping.
            _pitch = (_pitch - delta.dy * 0.01).clamp(-1.0, 1.0);
          });
        },
        onPanEnd: (_) => _lastDragPosition = null,
        child: Container(
          height: 260,
          padding: const EdgeInsets.all(16),
          decoration: BoxDecoration(
            gradient: LinearGradient(
              begin: Alignment.topLeft,
              end: Alignment.bottomRight,
              colors: [
                colorScheme.surfaceContainerHighest.withOpacity(0.6),
                colorScheme.surface.withOpacity(0.9),
              ],
            ),
            borderRadius: BorderRadius.circular(20),
            border: Border.all(
              color: colorScheme.primary.withOpacity(0.3),
              width: 1.5,
            ),
            boxShadow: [
              BoxShadow(
                color: Colors.black.withOpacity(0.4),
                blurRadius: 18,
                offset: const Offset(0, 10),
              ),
            ],
          ),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Row(
                children: [
                  Icon(Icons.view_in_ar, color: colorScheme.primary),
                  const SizedBox(width: 8),
                  Text(
                    '3D Block Visualization',
                    style: theme.textTheme.titleMedium?.copyWith(
                      fontWeight: FontWeight.w600,
                    ),
                  ),
                  const Spacer(),
                  Icon(Icons.mouse, size: 16, color: colorScheme.onSurface.withOpacity(0.7)),
                  const SizedBox(width: 4),
                  Text(
                    'Drag to rotate',
                    style: theme.textTheme.bodySmall?.copyWith(
                      color: colorScheme.onSurface.withOpacity(0.7),
                    ),
                  ),
                ],
              ),
              const SizedBox(height: 12),
              Expanded(
                child: LayoutBuilder(
                  builder: (context, constraints) {
                    final blocks = widget.configuration.blocks;
                    if (blocks.isEmpty) {
                      return Center(
                        child: Text(
                          'No blocks to visualize yet.',
                          style: theme.textTheme.bodyMedium?.copyWith(
                            color: colorScheme.onSurface.withOpacity(0.7),
                          ),
                        ),
                      );
                    }

                    final centerX = constraints.maxWidth / 2;
                    final centerY = constraints.maxHeight / 2 + 10;
                    const double cubeHalf = 0.55;
                    const double cubeScale = 70;
                    const double cameraDistance = 3.2;

                    const double localZ = 0.0;
                    const double localY = 0.0;
                    final double interBlock = cubeHalf * 2; // face-to-face

                    final projected = <_ProjectedBlock>[];
                    for (var i = 0; i < blocks.length; i++) {
                      final block = blocks[i];
                      final localX = (i - (blocks.length - 1) / 2) * interBlock;

                      final rotated = _rotatePoint(localX, localY, localZ, _yaw, _pitch);

                      final factor = cameraDistance / (cameraDistance + rotated.z);
                      final screenX = centerX + rotated.x * factor * cubeScale;
                      final screenY = centerY + rotated.y * factor * cubeScale;
                      final depth = rotated.y;

                      projected.add(
                        _ProjectedBlock(
                          index: i,
                          block: block,
                          modelCenter: _Point3(localX, localY, localZ),
                          screenPosition: Offset(screenX, screenY),
                          depth: depth,
                        ),
                      );
                    }

                    projected.sort((a, b) => a.depth.compareTo(b.depth));

                    final glowAmount = _glowController.value;
                    return Stack(
                      clipBehavior: Clip.none,
                      children: [
                        // Ground plane.
                        Positioned.fill(
                          child: CustomPaint(
                            painter: _GroundPlanePainter(
                              yaw: _yaw,
                              pitch: _pitch,
                              color: colorScheme.primary.withOpacity(0.15),
                            ),
                          ),
                        ),
                        // Projected blocks.
                        ...projected.map(
                          (pb) => _buildBlockCard(
                            theme,
                            colorScheme,
                            pb.block,
                            pb.screenPosition,
                            modelCenter: pb.modelCenter,
                            depth: pb.depth,
                            glowAmount: _glowBlockIndices.contains(pb.index)
                                ? glowAmount
                                : 0.0,
                          ),
                        ),
                      ],
                    );
                  },
                ),
              ),
            ],
          ),
        ),
      );
  }

  // Rotate a point around Y (yaw) and X (pitch).
  _Point3 _rotatePoint(double x, double y, double z, double yaw, double pitch) {
    final cosYaw = math.cos(yaw);
    final sinYaw = math.sin(yaw);
    final x1 = x * cosYaw + z * sinYaw;
    final z1 = -x * sinYaw + z * cosYaw;

    final cosPitch = math.cos(pitch);
    final sinPitch = math.sin(pitch);
    final y2 = y * cosPitch - z1 * sinPitch;
    final z2 = y * sinPitch + z1 * cosPitch;

    return _Point3(x1, y2, z2);
  }

  Widget _buildBlockCard(
    ThemeData theme,
    ColorScheme colorScheme,
    BlockInfo block,
    Offset position, {
    required _Point3 modelCenter,
    required double depth,
    required double glowAmount,
  }) {
    final blockType = block.blockType;
    final baseColor = _getBlockTypeColor(colorScheme, blockType);

    const double cardSize = 160;
    const double widgetCenterX = cardSize / 2; // 80
    const double widgetCenterY = cardSize / 2 + 8; // matches `_CubePainter`

    return Positioned(
      left: position.dx - widgetCenterX,
      top: position.dy - widgetCenterY,
      child: SizedBox(
        width: cardSize,
        height: cardSize,
        child: CustomPaint(
          painter: _CubePainter(
            baseColor: baseColor,
            depth: depth,
            cubeCenter: modelCenter,
            glowAmount: glowAmount,
            yaw: _yaw,
            pitch: _pitch,
            label: blockType?.displayName ?? block.whoami.blockType ?? 'Unknown',
            textStyle: theme.textTheme.bodySmall ??
                const TextStyle(
                  color: Colors.white,
                  fontSize: 11,
                ),
          ),
        ),
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
}

class _Point3 {
  final double x;
  final double y;
  final double z;

  const _Point3(this.x, this.y, this.z);
}

class _ProjectedBlock {
  final int index;
  final BlockInfo block;
  final _Point3 modelCenter;
  final Offset screenPosition;
  final double depth;

  const _ProjectedBlock({
    required this.index,
    required this.block,
    required this.modelCenter,
    required this.screenPosition,
    required this.depth,
  });
}

/// Painter that draws a single perspective-ish cube with labels.
class _CubePainter extends CustomPainter {
  final Color baseColor;
  final double depth;
  final _Point3 cubeCenter;
  final double glowAmount;
  final double yaw;
  final double pitch;
  final String label;
  final TextStyle textStyle;

  _CubePainter({
    required this.baseColor,
    required this.depth,
    required this.cubeCenter,
    required this.glowAmount,
    required this.yaw,
    required this.pitch,
    required this.label,
    required this.textStyle,
  });

  @override
  void paint(Canvas canvas, Size size) {
    final center = Offset(size.width / 2, size.height / 2 + 8);

    // True cube: rotate 8 vertices in 3D, then perspective-project.
    const double half = 0.55; // cube half-edge in model units
    const double cameraDistance = 3.2; // camera distance in model units
    const double scale = 70; // pixels per model unit (after projection)

    final vertices = <_Vec3>[
      const _Vec3(-half, -half, -half), // 0
      const _Vec3(half, -half, -half), // 1
      const _Vec3(half, half, -half), // 2
      const _Vec3(-half, half, -half), // 3
      const _Vec3(-half, -half, half), // 4
      const _Vec3(half, -half, half), // 5
      const _Vec3(half, half, half), // 6
      const _Vec3(-half, half, half), // 7
    ];

    _Vec3 rotate(_Vec3 p) {
      final cy = math.cos(yaw);
      final sy = math.sin(yaw);
      final x1 = p.x * cy + p.z * sy;
      final z1 = -p.x * sy + p.z * cy;

      final cx = math.cos(pitch);
      final sx = math.sin(pitch);
      final y2 = p.y * cx - z1 * sx;
      final z2 = p.y * sx + z1 * cx;

      return _Vec3(x1, y2, z2);
    }

    Offset projectRelative(_Vec3 p) {
      final factor = cameraDistance / (cameraDistance + p.z);
      return Offset(p.x * factor * scale, p.y * factor * scale);
    }

    final rotatedCenter =
        rotate(_Vec3(cubeCenter.x, cubeCenter.y, cubeCenter.z));
    final rotatedOffsets = vertices.map(rotate).toList(growable: false);

    final projectedCenter = projectRelative(rotatedCenter);
    final projected = <Offset>[];
    for (final ro in rotatedOffsets) {
      final globalRotatedVertex = rotatedCenter + ro;
      final projectedGlobal = projectRelative(globalRotatedVertex);
      projected.add(center + (projectedGlobal - projectedCenter));
    }

    // Faces (4 indices) in consistent CCW order when viewed from outside.
    const faces = <List<int>>[
      [4, 5, 6, 7], // +Z (front)
      [0, 3, 2, 1], // -Z (back)
      [0, 4, 7, 3], // -X (left)
      [1, 2, 6, 5], // +X (right)
      [3, 7, 6, 2], // +Y (top)
      [0, 1, 5, 4], // -Y (bottom)
    ];

    _Vec3 normalFor(List<int> f) {
      final a = rotatedOffsets[f[0]];
      final b = rotatedOffsets[f[1]];
      final c = rotatedOffsets[f[2]];
      final ab = b - a;
      final ac = c - a;
      return ab.cross(ac).normalized();
    }

    double avgZFor(List<int> f) =>
        (rotatedOffsets[f[0]].z +
                rotatedOffsets[f[1]].z +
                rotatedOffsets[f[2]].z +
                rotatedOffsets[f[3]].z) /
        4.0;

    Path pathFor(List<int> f) {
      final p = Path()..moveTo(projected[f[0]].dx, projected[f[0]].dy);
      for (var i = 1; i < f.length; i++) {
        p.lineTo(projected[f[i]].dx, projected[f[i]].dy);
      }
      p.close();
      return p;
    }

    // Draw all faces sorted far-to-near for stability (no popping).
    final drawFaces = <_FaceDraw>[
      for (final f in faces) _FaceDraw(indices: f, normal: normalFor(f), avgZ: avgZFor(f)),
    ]..sort((a, b) => b.avgZ.compareTo(a.avgZ));

    // Depth-based dim so distant blocks read slightly darker.
    final normalizedDepth = ((depth + 200.0) / 400.0).clamp(0.0, 1.0);
    final depthDim = 0.85 - normalizedDepth * 0.25; // 0.85..0.60

    final lightDir = const _Vec3(-0.35, -0.6, -1.0).normalized();

    Color shade(_Vec3 n) {
      // Two-sided lighting so faces don't "flip" harshly; back faces are dimmer.
      final ndotl = (-n.dot(lightDir)).clamp(-1.0, 1.0);
      final facing = (-n.z).clamp(0.0, 1.0); // 1 when facing camera
      final lit = (0.35 + ((ndotl.abs()) * 0.65)) * depthDim;
      final base = Color.lerp(Colors.black, baseColor, lit.clamp(0.0, 1.0))!;
      return Color.lerp(base, Colors.black, (1.0 - facing) * 0.55)!;
    }

    // Soft ground shadow.
    final maxY = projected.map((o) => o.dy).reduce(math.max);
    canvas.drawOval(
      Rect.fromCenter(
        center: Offset(center.dx, maxY + 10),
        width: 110,
        height: 26,
      ),
      Paint()
        ..color = baseColor.withOpacity(0.25)
        ..maskFilter = const MaskFilter.blur(BlurStyle.normal, 10),
    );

    // Fill faces (back faces darker, but still drawn so cube stays rigid).
    for (final fd in drawFaces) {
      canvas.drawPath(pathFor(fd.indices), Paint()..color = shade(fd.normal));
    }

    // Draw the 12 cube edges explicitly (stable wireframe).
    const edges = <List<int>>[
      [0, 1],
      [1, 2],
      [2, 3],
      [3, 0],
      [4, 5],
      [5, 6],
      [6, 7],
      [7, 4],
      [0, 4],
      [1, 5],
      [2, 6],
      [3, 7],
    ];

    final edgePaint = Paint()
      ..color = Colors.white.withOpacity(0.85)
      ..style = PaintingStyle.stroke
      ..strokeWidth = 2.2
      ..strokeJoin = StrokeJoin.miter
      ..strokeCap = StrokeCap.square;

    for (final e in edges) {
      final a = projected[e[0]];
      final b = projected[e[1]];
      canvas.drawLine(a, b, edgePaint);
    }

    if (glowAmount > 0.001) {
      final glowPaint = Paint()
        ..color = baseColor.withOpacity(0.2 + 0.75 * glowAmount)
        ..style = PaintingStyle.stroke
        ..strokeWidth = 2.2 + glowAmount * 4.0
        ..strokeJoin = StrokeJoin.miter
        ..strokeCap = StrokeCap.square
        ..maskFilter =
            MaskFilter.blur(BlurStyle.normal, 6 + glowAmount * 14);

      for (final e in edges) {
        final a = projected[e[0]];
        final b = projected[e[1]];
        canvas.drawLine(a, b, glowPaint);
      }
    }

    // Label on the most front-facing visible face (largest -normal.z).
    _FaceDraw? labelFace;
    for (final fd in drawFaces) {
      final current = labelFace;
      if (current == null || (-fd.normal.z) > (-current.normal.z)) {
        labelFace = fd;
      }
    }

    final lf = labelFace;
    if (lf != null) {
      final f = lf.indices;
      final centroid = Offset(
        (projected[f[0]].dx + projected[f[1]].dx + projected[f[2]].dx + projected[f[3]].dx) /
            4.0,
        (projected[f[0]].dy + projected[f[1]].dy + projected[f[2]].dy + projected[f[3]].dy) /
            4.0,
      );

      final tp = TextPainter(
        text: TextSpan(
          text: label,
          style: textStyle.copyWith(
            fontWeight: FontWeight.w800,
            color: Colors.white,
            fontSize: 14,
          ),
        ),
        textAlign: TextAlign.center,
        textDirection: TextDirection.ltr,
        maxLines: 1,
        ellipsis: '…',
      )..layout(maxWidth: 84);

      canvas.drawRRect(
        RRect.fromRectAndRadius(
          Rect.fromCenter(
            center: centroid,
            width: tp.width + 18,
            height: tp.height + 10,
          ),
          const Radius.circular(10),
        ),
        Paint()..color = Colors.black.withOpacity(0.22),
      );

      tp.paint(canvas, Offset(centroid.dx - tp.width / 2, centroid.dy - tp.height / 2));
    }
  }

  @override
  bool shouldRepaint(covariant _CubePainter oldDelegate) {
    return oldDelegate.baseColor != baseColor ||
        oldDelegate.depth != depth ||
        oldDelegate.cubeCenter.x != cubeCenter.x ||
        oldDelegate.cubeCenter.y != cubeCenter.y ||
        oldDelegate.cubeCenter.z != cubeCenter.z ||
        oldDelegate.glowAmount != glowAmount ||
        oldDelegate.yaw != yaw ||
        oldDelegate.pitch != pitch ||
        oldDelegate.label != label;
  }
}

class _Vec3 {
  final double x;
  final double y;
  final double z;

  const _Vec3(this.x, this.y, this.z);

  _Vec3 operator +(_Vec3 other) => _Vec3(x + other.x, y + other.y, z + other.z);

  _Vec3 operator -(_Vec3 other) => _Vec3(x - other.x, y - other.y, z - other.z);

  double dot(_Vec3 other) => x * other.x + y * other.y + z * other.z;

  _Vec3 cross(_Vec3 o) => _Vec3(
        y * o.z - z * o.y,
        z * o.x - x * o.z,
        x * o.y - y * o.x,
      );

  double get length => math.sqrt(x * x + y * y + z * z);

  _Vec3 normalized() {
    final len = length;
    if (len == 0) return const _Vec3(0, 0, 0);
    return _Vec3(x / len, y / len, z / len);
  }
}

class _FaceDraw {
  final List<int> indices;
  final _Vec3 normal;
  final double avgZ;

  const _FaceDraw({
    required this.indices,
    required this.normal,
    required this.avgZ,
  });
}

class _GroundPlanePainter extends CustomPainter {
  final double yaw;
  final double pitch;
  final Color color;

  _GroundPlanePainter({
    required this.yaw,
    required this.pitch,
    required this.color,
  });

  @override
  void paint(Canvas canvas, Size size) {
    final paint = Paint()
      ..shader = LinearGradient(
        begin: Alignment.topCenter,
        end: Alignment.bottomCenter,
        colors: [
          color.withOpacity(0.0),
          color,
        ],
      ).createShader(Rect.fromLTWH(0, 0, size.width, size.height));

    final path = Path();
    final h = size.height * 0.6;

    final tilt = (pitch * 0.5).clamp(-0.4, 0.4);
    final offsetY = size.height * 0.5 + tilt * size.height * 0.2;

    path.moveTo(-size.width * 0.2, offsetY);
    path.lineTo(size.width * 1.2, offsetY);
    path.lineTo(size.width * 0.9, offsetY + h);
    path.lineTo(size.width * 0.1, offsetY + h);
    path.close();

    canvas.drawPath(path, paint);

    final gridPaint = Paint()
      ..color = color.withOpacity(0.4)
      ..strokeWidth = 1;

    const int gridLines = 6;
    for (var i = 1; i <= gridLines; i++) {
      final t = i / (gridLines + 1);
      final y = offsetY + t * h;
      canvas.drawLine(
        Offset(size.width * 0.15, y),
        Offset(size.width * 0.85, y),
        gridPaint,
      );
    }
  }

  @override
  bool shouldRepaint(covariant _GroundPlanePainter oldDelegate) {
    return oldDelegate.yaw != yaw ||
        oldDelegate.pitch != pitch ||
        oldDelegate.color != color;
  }
}

