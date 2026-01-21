import 'dart:convert';
import '../models/block_telemetry.dart';

/// Service for parsing telemetry data from JSON strings
class TelemetryParser {
  /// Parse a single telemetry message from JSON string
  /// Returns null if parsing fails
  BlockTelemetry? parseTelemetry(String jsonString) {
    try {
      // Trim whitespace and newlines
      final trimmed = jsonString.trim();
      if (trimmed.isEmpty) {
        return null;
      }

      // Parse JSON
      final json = jsonDecode(trimmed) as Map<String, dynamic>;
      
      // Validate basic structure
      if (!isValidTelemetry(json)) {
        return null;
      }

      return BlockTelemetry.fromJson(json);
    } catch (e) {
      // Handle JSON parsing errors gracefully
      return null;
    }
  }

  /// Parse a batch of telemetry messages (array of JSON objects)
  /// Returns empty list if parsing fails
  List<BlockTelemetry> parseBatch(String jsonString) {
    try {
      final trimmed = jsonString.trim();
      if (trimmed.isEmpty) {
        return [];
      }

      final json = jsonDecode(trimmed);
      
      if (json is List) {
        return json
            .whereType<Map<String, dynamic>>()
            .where((item) => isValidTelemetry(item))
            .map((item) => BlockTelemetry.fromJson(item))
            .toList();
      } else if (json is Map<String, dynamic>) {
        // Single object wrapped in batch format
        if (isValidTelemetry(json)) {
          return [BlockTelemetry.fromJson(json)];
        }
      }
      
      return [];
    } catch (e) {
      return [];
    }
  }

  /// Validate if a JSON map represents valid telemetry data
  /// Accepts any JSON object as valid (flexible validation)
  bool isValidTelemetry(Map<String, dynamic> json) {
    // Accept any non-empty map as valid telemetry
    // This allows for flexible telemetry formats
    return json.isNotEmpty;
  }

  /// Try to parse as either single message or batch
  /// Returns list with one or more telemetry objects
  List<BlockTelemetry> parse(String jsonString) {
    // First try as batch
    final batch = parseBatch(jsonString);
    if (batch.isNotEmpty) {
      return batch;
    }

    // Then try as single message
    final single = parseTelemetry(jsonString);
    if (single != null) {
      return [single];
    }

    return [];
  }
}
