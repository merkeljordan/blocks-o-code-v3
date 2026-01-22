import 'dart:convert';
import '../models/block_configuration.dart';

/// Service for parsing block configuration JSON messages
class BlockConfigParser {
  /// Parse a block configuration message from JSON string
  /// Returns null if parsing fails
  BlockConfiguration? parseConfig(String jsonString) {
    try {
      final trimmed = jsonString.trim();
      if (trimmed.isEmpty) {
        return null;
      }

      final json = jsonDecode(trimmed) as Map<String, dynamic>;

      // Check if it's a block_config message
      if (json.containsKey('type') && json['type'] == 'block_config') {
        return BlockConfiguration.fromJson(json);
      }

      // Also try parsing if it's just the config object
      if (json.containsKey('config') || json.containsKey('blocks')) {
        return BlockConfiguration.fromJson(json);
      }

      return null;
    } catch (e) {
      // Handle JSON parsing errors gracefully
      return null;
    }
  }

  /// Validate that a JSON string has the correct structure for block_config
  bool isValidConfigMessage(String jsonString) {
    try {
      final json = jsonDecode(jsonString.trim()) as Map<String, dynamic>;
      
      // Must have type field
      if (!json.containsKey('type')) {
        return false;
      }

      // Type must be 'block_config'
      if (json['type'] != 'block_config') {
        return false;
      }

      // Must have config or blocks field
      if (!json.containsKey('config') && !json.containsKey('blocks')) {
        return false;
      }

      return true;
    } catch (e) {
      return false;
    }
  }
}
