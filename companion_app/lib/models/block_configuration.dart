import 'block_type.dart';

/// Main configuration container for block topology
class BlockConfiguration {
  final int totalBlocks;
  final List<BlockInfo> blocks;
  final List<ConfigurationError> errors;
  final DateTime timestamp;
  final int? detectedUptimeMs;
  final int? sentUptimeMs;
  
  /// The original block count reported by firmware before any synthetic blocks were added
  final int originalFirmwareBlockCount;
  
  /// Whether a synthetic Brain Block was injected by the app (true when firmware reported 0 blocks)
  final bool hasSyntheticBrainBlock;

  BlockConfiguration({
    required this.totalBlocks,
    required this.blocks,
    this.errors = const [],
    DateTime? timestamp,
    this.detectedUptimeMs,
    this.sentUptimeMs,
    int? originalFirmwareBlockCount,
    this.hasSyntheticBrainBlock = false,
  }) : timestamp = timestamp ?? DateTime.now(),
       this.originalFirmwareBlockCount = originalFirmwareBlockCount ?? totalBlocks;

  /// Create from JSON map
  factory BlockConfiguration.fromJson(Map<String, dynamic> json) {
    final config = json['config'] as Map<String, dynamic>? ?? json;
    
    final blocksList = config['blocks'] as List<dynamic>? ?? [];
    final blocks = blocksList
        .map((b) => BlockInfo.fromJson(b as Map<String, dynamic>))
        .toList();

    final errorsList = config['errors'] as List<dynamic>? ?? [];
    final errors = errorsList
        .map((e) => ConfigurationError.fromJson(e as Map<String, dynamic>))
        .toList();

    // Extract timestamp
    DateTime? timestamp;
    if (json.containsKey('timestamp')) {
      final ts = json['timestamp'];
      if (ts is int) {
        timestamp = DateTime.fromMillisecondsSinceEpoch(ts);
      } else if (ts is String) {
        timestamp = DateTime.tryParse(ts);
      }
    }
    final detectedUptimeMs =
        _parseMillisecondsValue(json['detected_uptime_ms']) ??
        _parseMillisecondsValue(json['timestamp']);
    final sentUptimeMs = _parseMillisecondsValue(json['sent_uptime_ms']);

    // If the firmware reports zero blocks, treat this as a "brain-only" configuration
    // and inject a synthetic Brain Block so the app can still visualize it.
    List<BlockInfo> finalBlocks = blocks;
    int reportedTotalBlocks = config['total_blocks'] as int? ?? blocks.length;
    int originalFirmwareCount = reportedTotalBlocks;
    bool hasSynthetic = false;

    /// Creates a synthetic Brain Block for visualization when no blocks are detected.
    /// 
    /// The Brain Block never appears in firmware scan results because the Brain doesn't
    /// scan its own I2C address - it only scans child blocks (addresses 0x08-0x15).
    /// This synthetic block ensures the UI can always display at least the Brain Block.
    /// 
    /// Values are chosen to align with firmware conventions:
    /// - blockId: 'BLOCK_0x00' follows firmware's "BLOCK_0x{i2c_address}" format
    /// - i2cAddress: 0x00 represents the Brain (BLOCK_TYPE_BRAIN = 0x00 in firmware)
    /// - firmwareVersion: '1.0.0' matches firmware's default version string
    /// 
    /// Note: If firmware is ever modified to report the Brain Block in scan results,
    /// these values should match exactly to avoid conflicts.
    BlockInfo buildBrainBlock() {
      final brainWhoami = WhoAmIData(
        blockType: BlockType.brainBlock.identifier,
        blockId: 'BLOCK_0x00',
        firmwareVersion: '1.0.0',
        capabilities: const [],
      );

      return BlockInfo(
        index: 0,
        i2cAddress: 0x00,
        whoami: brainWhoami,
        connectionOrder: 0,
        blockType: BlockType.brainBlock,
      );
    }

    if (blocks.isEmpty) {
      finalBlocks = [buildBrainBlock()];
      reportedTotalBlocks = 1;
      hasSynthetic = true;
    }

    return BlockConfiguration(
      totalBlocks: reportedTotalBlocks,
      blocks: finalBlocks,
      errors: errors,
      timestamp: timestamp,
      detectedUptimeMs: detectedUptimeMs,
      sentUptimeMs: sentUptimeMs,
      originalFirmwareBlockCount: originalFirmwareCount,
      hasSyntheticBrainBlock: hasSynthetic,
    );
  }

  static int? _parseMillisecondsValue(dynamic value) {
    if (value is int) return value;
    if (value is double) return value.toInt();
    if (value is String) return int.tryParse(value);
    return null;
  }

  /// Convert to JSON map
  Map<String, dynamic> toJson() {
    return {
      'type': 'block_config',
      'timestamp': timestamp.millisecondsSinceEpoch,
      if (detectedUptimeMs != null) 'detected_uptime_ms': detectedUptimeMs,
      if (sentUptimeMs != null) 'sent_uptime_ms': sentUptimeMs,
      'config': {
        'total_blocks': totalBlocks,
        'blocks': blocks.map((b) => b.toJson()).toList(),
        'errors': errors.map((e) => e.toJson()).toList(),
        'original_firmware_block_count': originalFirmwareBlockCount,
        'has_synthetic_brain_block': hasSyntheticBrainBlock,
      },
    };
  }

  /// Get block at a specific index
  BlockInfo? getBlockAt(int index) {
    if (index < 0 || index >= blocks.length) return null;
    return blocks[index];
  }

  /// Get block by I2C address
  BlockInfo? getBlockByAddress(int i2cAddress) {
    try {
      return blocks.firstWhere((b) => b.i2cAddress == i2cAddress);
    } catch (e) {
      return null;
    }
  }

  /// Get all blocks of a specific type
  List<BlockInfo> getBlocksByType(BlockType type) {
    return blocks.where((b) => b.blockType == type).toList();
  }
}

/// Individual block information
class BlockInfo {
  final int index;
  final int i2cAddress;
  final WhoAmIData whoami;
  final int connectionOrder;
  final BlockType? blockType; // Parsed from whoami.blockType

  BlockInfo({
    required this.index,
    required this.i2cAddress,
    required this.whoami,
    required this.connectionOrder,
    this.blockType,
  });

  /// Create from JSON map
  factory BlockInfo.fromJson(Map<String, dynamic> json) {
    final whoamiData = json['whoami'] as Map<String, dynamic>? ?? {};
    final whoami = WhoAmIData.fromJson(whoamiData);

    // Parse block type from whoami
    BlockType? blockType;
    if (whoami.blockType != null) {
      blockType = BlockType.fromIdentifier(whoami.blockType!);
    }

    return BlockInfo(
      index: json['index'] as int? ?? 0,
      i2cAddress: _parseI2CAddress(json['i2c_address']),
      whoami: whoami,
      connectionOrder: json['connection_order'] as int? ?? json['index'] as int? ?? 0,
      blockType: blockType,
    );
  }

  /// Parse I2C address (handles hex strings like "0x08" or integers)
  static int _parseI2CAddress(dynamic address) {
    if (address is int) {
      return address;
    } else if (address is String) {
      if (address.startsWith('0x') || address.startsWith('0X')) {
        return int.parse(address.substring(2), radix: 16);
      }
      return int.tryParse(address) ?? 0;
    }
    return 0;
  }

  /// Convert to JSON map
  Map<String, dynamic> toJson() {
    return {
      'index': index,
      'i2c_address': i2cAddress,
      'whoami': whoami.toJson(),
      'connection_order': connectionOrder,
    };
  }

  @override
  String toString() {
    return 'BlockInfo(index: $index, address: 0x${i2cAddress.toRadixString(16)}, type: ${blockType?.displayName ?? whoami.blockType}, order: $connectionOrder)';
  }
}

/// WHOAMI register contents
class WhoAmIData {
  final String? blockType;
  final String? blockId;
  final String? firmwareVersion;
  final List<String> capabilities;

  WhoAmIData({
    this.blockType,
    this.blockId,
    this.firmwareVersion,
    this.capabilities = const [],
  });

  /// Create from JSON map
  factory WhoAmIData.fromJson(Map<String, dynamic> json) {
    final capabilitiesList = json['capabilities'] as List<dynamic>? ?? [];
    return WhoAmIData(
      blockType: json['block_type'] as String?,
      blockId: json['block_id'] as String?,
      firmwareVersion: json['firmware_version'] as String?,
      capabilities: capabilitiesList.map((c) => c.toString()).toList(),
    );
  }

  /// Convert to JSON map
  Map<String, dynamic> toJson() {
    return {
      if (blockType != null) 'block_type': blockType,
      if (blockId != null) 'block_id': blockId,
      if (firmwareVersion != null) 'firmware_version': firmwareVersion,
      if (capabilities.isNotEmpty) 'capabilities': capabilities,
    };
  }
}

/// Configuration error from firmware
class ConfigurationError {
  final String type;
  final String message;
  final int? blockIndex;
  final int? i2cAddress;
  final DateTime? timestamp;

  ConfigurationError({
    required this.type,
    required this.message,
    this.blockIndex,
    this.i2cAddress,
    DateTime? timestamp,
  }) : timestamp = timestamp ?? DateTime.now();

  /// Create from JSON map
  factory ConfigurationError.fromJson(Map<String, dynamic> json) {
    DateTime? timestamp;
    if (json.containsKey('timestamp')) {
      final ts = json['timestamp'];
      if (ts is int) {
        timestamp = DateTime.fromMillisecondsSinceEpoch(ts);
      } else if (ts is String) {
        timestamp = DateTime.tryParse(ts);
      }
    }

    return ConfigurationError(
      type: json['type'] as String? ?? 'unknown',
      message: json['message'] as String? ?? '',
      blockIndex: json['block_index'] as int?,
      i2cAddress: _parseI2CAddress(json['i2c_address']),
      timestamp: timestamp,
    );
  }

  /// Parse I2C address (handles hex strings like "0x08" or integers)
  static int? _parseI2CAddress(dynamic address) {
    if (address == null) return null;
    if (address is int) {
      return address;
    } else if (address is String) {
      if (address.startsWith('0x') || address.startsWith('0X')) {
        return int.tryParse(address.substring(2), radix: 16);
      }
      return int.tryParse(address);
    }
    return null;
  }

  /// Convert to JSON map
  Map<String, dynamic> toJson() {
    return {
      'type': type,
      'message': message,
      if (blockIndex != null) 'block_index': blockIndex,
      if (i2cAddress != null) 'i2c_address': i2cAddress,
      if (timestamp != null) 'timestamp': timestamp!.millisecondsSinceEpoch,
    };
  }

  @override
  String toString() {
    return 'ConfigurationError(type: $type, message: $message, blockIndex: $blockIndex, i2cAddress: $i2cAddress)';
  }
}
