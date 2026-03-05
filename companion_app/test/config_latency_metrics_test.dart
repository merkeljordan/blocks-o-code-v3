import 'dart:convert';

import 'package:flutter_test/flutter_test.dart';

import 'package:demo_app/services/block_config_parser.dart';
import 'package:demo_app/services/config_latency_metrics.dart';

void main() {
  group('Config latency metadata', () {
    test('parses detected and sent uptime from block_config payload', () {
      final parser = BlockConfigParser();
      final payload = jsonEncode({
        'type': 'block_config',
        'timestamp': 1200,
        'detected_uptime_ms': 1200,
        'sent_uptime_ms': 1234,
        'config': {
          'total_blocks': 1,
          'blocks': [
            {
              'index': 0,
              'i2c_address': 0,
              'connection_order': 0,
              'whoami': {
                'block_type': 'brain_block',
                'block_id': 'BRAIN',
                'firmware_version': '1.0.0',
                'capabilities': <String>[],
              },
            },
          ],
          'errors': <Map<String, dynamic>>[],
        },
      });

      final config = parser.parseConfig(payload);
      expect(config, isNotNull);
      expect(config!.detectedUptimeMs, 1200);
      expect(config.sentUptimeMs, 1234);
    });

    test('calculates brain and estimated render latency segments', () {
      final parser = BlockConfigParser();
      final calculator = ConfigLatencyCalculator();
      final payload = jsonEncode({
        'type': 'block_config',
        'detected_uptime_ms': 5000,
        'sent_uptime_ms': 5037,
        'config': {
          'total_blocks': 1,
          'blocks': [
            {
              'index': 0,
              'i2c_address': 0,
              'connection_order': 0,
              'whoami': {
                'block_type': 'brain_block',
                'block_id': 'BRAIN',
                'firmware_version': '1.0.0',
                'capabilities': <String>[],
              },
            },
          ],
          'errors': <Map<String, dynamic>>[],
        },
      });

      final config = parser.parseConfig(payload)!;
      final brainDetectToSend = calculator.brainDetectToSend(config);
      final estimated = calculator.estimatedDetectToRender(
        brainDetectToSendMs: brainDetectToSend,
        appReceiveToRenderMs: 42,
      );

      expect(brainDetectToSend, 37);
      expect(estimated, 79);
    });
  });
}
