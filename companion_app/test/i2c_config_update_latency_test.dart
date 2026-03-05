import 'dart:convert';

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

import 'package:demo_app/main.dart';
import 'package:demo_app/models/block_configuration.dart';
import 'package:demo_app/models/configuration_rules.dart';
import 'package:demo_app/services/block_config_parser.dart';
import 'package:demo_app/services/configuration_validator.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  group('I2C config update latency', () {
    testWidgets(
      'measures snap-on and removal latency from simulated Brain detect time to visualizer config update',
      (tester) async {
        final harnessKey = GlobalKey<_LatencyHarnessState>();

        await tester.pumpWidget(
          MaterialApp(
            home: LatencyHarness(key: harnessKey),
          ),
        );

        // Baseline: Brain only.
        final baselineDetectedAt = DateTime.now().millisecondsSinceEpoch;
        harnessKey.currentState!.processMessage(
          _buildConfigMessage(
            detectionTimestampMs: baselineDetectedAt,
            blocks: _brainOnlyBlocks(),
          ),
        );
        await _waitForText(tester, 'Total Blocks: 1');

        // SNAP-ON: simulate a new child block detected on I2C.
        final snapDetectedAt = DateTime.now().millisecondsSinceEpoch;
        harnessKey.currentState!.processMessage(
          _buildConfigMessage(
            detectionTimestampMs: snapDetectedAt,
            blocks: _brainAndLedBlocks(),
          ),
        );
        await _waitForText(tester, 'Total Blocks: 2');
        await _waitForText(tester, 'LED Color Flash Block');
        final snapLatencyMs = DateTime.now().millisecondsSinceEpoch - snapDetectedAt;

        // REMOVAL: simulate child block removed from I2C.
        final removeDetectedAt = DateTime.now().millisecondsSinceEpoch;
        harnessKey.currentState!.processMessage(
          _buildConfigMessage(
            detectionTimestampMs: removeDetectedAt,
            blocks: _brainOnlyBlocks(),
          ),
        );
        await _waitForText(tester, 'Total Blocks: 1');
        await _waitForTextToDisappear(tester, 'LED Color Flash Block');
        final removeLatencyMs =
            DateTime.now().millisecondsSinceEpoch - removeDetectedAt;

        // Relaxed bound to prevent CI flakiness while still detecting regressions.
        const maxAllowedLatencyMs = 500;
        expect(
          snapLatencyMs,
          lessThanOrEqualTo(maxAllowedLatencyMs),
          reason:
              'Snap-on update should remain <= ${maxAllowedLatencyMs}ms (got ${snapLatencyMs}ms).',
        );
        expect(
          removeLatencyMs,
          lessThanOrEqualTo(maxAllowedLatencyMs),
          reason:
              'Removal update should remain <= ${maxAllowedLatencyMs}ms (got ${removeLatencyMs}ms).',
        );

        // ignore: avoid_print
        print('Snap-on latency: ${snapLatencyMs}ms');
        // ignore: avoid_print
        print('Removal latency: ${removeLatencyMs}ms');
      },
    );
  });
}

class LatencyHarness extends StatefulWidget {
  const LatencyHarness({super.key});

  @override
  State<LatencyHarness> createState() => _LatencyHarnessState();
}

class _LatencyHarnessState extends State<LatencyHarness> {
  final BlockConfigParser _parser = BlockConfigParser();
  final ConfigurationValidator _validator = ConfigurationValidator();

  BlockConfiguration? _configuration;
  List<RuleViolation> _violations = const [];

  void processMessage(String message) {
    final parsed = _parser.parseConfig(message);
    if (parsed == null) {
      return;
    }

    final violations = _validator.validate(parsed);
    setState(() {
      _configuration = parsed;
      _violations = violations;
    });
  }

  @override
  Widget build(BuildContext context) {
    return BlockConfigScreen(
      isConnected: true,
      connectionStatus: 'Test',
      onStartStressTest: () {},
      onStopStressTest: () {},
      currentConfiguration: _configuration,
      configViolations: _violations,
    );
  }
}

String _buildConfigMessage({
  required int detectionTimestampMs,
  required List<Map<String, dynamic>> blocks,
}) {
  final sentTimestampMs = detectionTimestampMs + 8;
  return jsonEncode({
    'type': 'block_config',
    'timestamp': detectionTimestampMs,
    'detected_uptime_ms': detectionTimestampMs,
    'sent_uptime_ms': sentTimestampMs,
    'config': {
      'total_blocks': blocks.length,
      'blocks': blocks,
      'errors': <Map<String, dynamic>>[],
    },
  });
}

Future<void> _waitForText(
  WidgetTester tester,
  String text, {
  Duration timeout = const Duration(seconds: 2),
}) async {
  final end = DateTime.now().add(timeout);
  while (DateTime.now().isBefore(end)) {
    await tester.pump(const Duration(milliseconds: 16));
    if (find.text(text).evaluate().isNotEmpty) return;
  }
  fail('Timed out waiting for text: "$text".');
}

Future<void> _waitForTextToDisappear(
  WidgetTester tester,
  String text, {
  Duration timeout = const Duration(seconds: 2),
}) async {
  final end = DateTime.now().add(timeout);
  while (DateTime.now().isBefore(end)) {
    await tester.pump(const Duration(milliseconds: 16));
    if (find.text(text).evaluate().isEmpty) return;
  }
  fail('Timed out waiting for text to disappear: "$text".');
}

List<Map<String, dynamic>> _brainOnlyBlocks() {
  return [
    {
      'index': 0,
      'i2c_address': '0x00',
      'connection_order': 0,
      'whoami': {
        'block_type': 'brain_block',
        'block_id': 'BLOCK_0x00',
        'firmware_version': '1.0.0',
        'capabilities': <String>[],
      },
    },
  ];
}

List<Map<String, dynamic>> _brainAndLedBlocks() {
  return [
    ..._brainOnlyBlocks(),
    {
      'index': 1,
      'i2c_address': '0x08',
      'connection_order': 1,
      'whoami': {
        'block_type': 'led_color_flash_block',
        'block_id': 'BLOCK_0x08',
        'firmware_version': '1.0.0',
        'capabilities': ['led_pattern', 'color_output'],
      },
    },
  ];
}
