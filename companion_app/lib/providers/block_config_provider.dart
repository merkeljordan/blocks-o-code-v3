import 'package:flutter/foundation.dart';
import '../models/block_configuration.dart';
import '../models/configuration_rules.dart';
import '../models/block_telemetry.dart';
import '../services/block_config_parser.dart';
import '../services/configuration_validator.dart';
import '../services/telemetry_parser.dart';
import '../config/app_config.dart';
import 'dart:async';

/// Provider for managing block configuration and telemetry
class BlockConfigProvider extends ChangeNotifier {
  final BlockConfigParser _configParser = BlockConfigParser();
  final ConfigurationValidator _configValidator = ConfigurationValidator();
  final TelemetryParser _telemetryParser = TelemetryParser();
  
  BlockConfiguration? _currentConfiguration;
  List<RuleViolation> _configViolations = [];
  List<BlockTelemetry> _receivedTelemetry = [];
  
  StreamSubscription<String>? _messageSubscription;

  // Getters
  BlockConfiguration? get currentConfiguration => _currentConfiguration;
  List<RuleViolation> get configViolations => _configViolations;
  List<BlockTelemetry> get receivedTelemetry => _receivedTelemetry;

  /// Set up message stream listener
  void listenToMessages(Stream<String>? messageStream) {
    _messageSubscription?.cancel();
    
    if (messageStream == null) return;
    
    _messageSubscription = messageStream.listen((message) {
      _processMessage(message);
    });
  }

  /// Process incoming message
  void _processMessage(String message) {
    try {
      // Try to parse as block configuration
      if (message.contains('"type":"block_config"') || message.contains("'type':'block_config'")) {
        final config = _configParser.parseConfig(message);
        if (config != null) {
          final violations = _configValidator.validate(config);
          _currentConfiguration = config;
          _configViolations = violations;
          notifyListeners();
        }
        return;
      }

      // Try to parse as telemetry
      final telemetryList = _telemetryParser.parse(message);
      if (telemetryList.isNotEmpty) {
        _receivedTelemetry.addAll(telemetryList);
        // Keep only last N telemetry entries
        if (_receivedTelemetry.length > AppConfig.maxTelemetryEntries) {
          _receivedTelemetry = _receivedTelemetry.sublist(
            _receivedTelemetry.length - AppConfig.maxTelemetryEntries,
          );
        }
        notifyListeners();
      }
    } catch (e) {
      // Message parsing failed, ignore
      debugPrint('Failed to parse message: $e');
    }
  }

  /// Process a message directly (for fake config loading)
  void processMessage(String message) {
    _processMessage(message);
  }

  /// Clear all data
  void clear() {
    _currentConfiguration = null;
    _configViolations = [];
    _receivedTelemetry = [];
    notifyListeners();
  }

  @override
  void dispose() {
    _messageSubscription?.cancel();
    super.dispose();
  }
}
