import 'block_type.dart';

/// Main configuration container for block topology
class BlockConfiguration {
  final int totalBlocks;
  final List<BlockInfo> blocks;
  final List<ConfigurationError> errors;
  final DateTime timestamp;

  BlockConfiguration({
    required this.totalBlocks,
    required this.blocks,
    this.errors = const [],
    DateTime? timestamp,
  }) : timestamp = timestamp ?? DateTime.now();

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

    // If the firmware reports zero blocks, treat this as a "brain-only" configuration
    // and inject a synthetic Brain Block so the app can still visualize it.
    List<BlockInfo> finalBlocks = blocks;
    int reportedTotalBlocks = config['total_blocks'] as int? ?? blocks.length;

    if (blocks.isEmpty && reportedTotalBlocks == 0) {
      final brainWhoami = WhoAmIData(
        blockType: BlockType.brainBlock.identifier,
        blockId: 'BRAIN',
        firmwareVersion: null,
        capabilities: const [],
      );

      final brainBlock = BlockInfo(
        index: 0,
        i2cAddress: 0,
        whoami: brainWhoami,
        connectionOrder: 0,
        blockType: BlockType.brainBlock,
      );

      finalBlocks = [brainBlock];
      reportedTotalBlocks = 1;
    }

    return BlockConfiguration(
      totalBlocks: reportedTotalBlocks,
      blocks: finalBlocks,
      errors: errors,
      timestamp: timestamp,
    );
  }

  /// Convert to JSON map
  Map<String, dynamic> toJson() {
    return {
      'type': 'block_config',
      'timestamp': timestamp.millisecondsSinceEpoch,
      'config': {
        'total_blocks': totalBlocks,
        'blocks': blocks.map((b) => b.toJson()).toList(),
        'errors': errors.map((e) => e.toJson()).toList(),
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
