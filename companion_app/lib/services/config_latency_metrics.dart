import '../models/block_configuration.dart';

/// Latency breakdown for block configuration updates.
class ConfigLatencyMetrics {
  final int? brainDetectToSendMs;
  final int? appReceiveToRenderMs;
  final int? estimatedDetectToRenderMs;

  const ConfigLatencyMetrics({
    this.brainDetectToSendMs,
    this.appReceiveToRenderMs,
    this.estimatedDetectToRenderMs,
  });

  ConfigLatencyMetrics copyWith({
    int? brainDetectToSendMs,
    int? appReceiveToRenderMs,
    int? estimatedDetectToRenderMs,
  }) {
    return ConfigLatencyMetrics(
      brainDetectToSendMs: brainDetectToSendMs ?? this.brainDetectToSendMs,
      appReceiveToRenderMs: appReceiveToRenderMs ?? this.appReceiveToRenderMs,
      estimatedDetectToRenderMs:
          estimatedDetectToRenderMs ?? this.estimatedDetectToRenderMs,
    );
  }
}

class ConfigLatencyCalculator {
  /// Returns Brain-side latency if both firmware uptime timestamps are present.
  int? brainDetectToSend(BlockConfiguration configuration) {
    final detected = configuration.detectedUptimeMs;
    final sent = configuration.sentUptimeMs;
    if (detected == null || sent == null) return null;
    if (sent < detected) return null;
    return sent - detected;
  }

  /// Combines known segments into an estimated detect-to-render duration.
  ///
  /// Note: This estimate includes Brain detect->send and app receive->render only.
  /// It does not include network transit or cross-device clock alignment.
  int? estimatedDetectToRender({
    required int? brainDetectToSendMs,
    required int? appReceiveToRenderMs,
  }) {
    if (brainDetectToSendMs == null || appReceiveToRenderMs == null) return null;
    return brainDetectToSendMs + appReceiveToRenderMs;
  }
}
