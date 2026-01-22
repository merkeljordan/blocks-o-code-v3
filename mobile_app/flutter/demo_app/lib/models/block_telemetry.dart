/// Model class for block telemetry data received from the Brain Block
class BlockTelemetry {
  final DateTime timestamp;
  final String? blockId;
  final Map<String, dynamic> sensorData;
  final Map<String, dynamic> metadata;
  final Map<String, dynamic> rawData; // Store all raw fields for flexibility

  BlockTelemetry({
    required this.timestamp,
    this.blockId,
    Map<String, dynamic>? sensorData,
    Map<String, dynamic>? metadata,
    Map<String, dynamic>? rawData,
  })  : sensorData = sensorData ?? {},
        metadata = metadata ?? {},
        rawData = rawData ?? {};

  /// Create BlockTelemetry from JSON map
  factory BlockTelemetry.fromJson(Map<String, dynamic> json) {
    // Extract timestamp - try multiple formats
    DateTime timestamp;
    if (json.containsKey('timestamp')) {
      final ts = json['timestamp'];
      if (ts is int) {
        timestamp = DateTime.fromMillisecondsSinceEpoch(ts);
      } else if (ts is String) {
        timestamp = DateTime.tryParse(ts) ?? DateTime.now();
      } else {
        timestamp = DateTime.now();
      }
    } else if (json.containsKey('time')) {
      final ts = json['time'];
      if (ts is int) {
        timestamp = DateTime.fromMillisecondsSinceEpoch(ts);
      } else if (ts is String) {
        timestamp = DateTime.tryParse(ts) ?? DateTime.now();
      } else {
        timestamp = DateTime.now();
      }
    } else {
      timestamp = DateTime.now();
    }

    // Extract block ID
    String? blockId;
    if (json.containsKey('blockId')) {
      blockId = json['blockId']?.toString();
    } else if (json.containsKey('block_id')) {
      blockId = json['block_id']?.toString();
    } else if (json.containsKey('id')) {
      blockId = json['id']?.toString();
    }

    // Extract sensor data - look for common keys
    Map<String, dynamic> sensorData = {};
    final sensorKeys = ['sensorData', 'sensors', 'data', 'values'];
    for (var key in sensorKeys) {
      if (json.containsKey(key) && json[key] is Map) {
        sensorData = Map<String, dynamic>.from(json[key] as Map);
        break;
      }
    }

    // Extract metadata
    Map<String, dynamic> metadata = {};
    if (json.containsKey('metadata') && json['metadata'] is Map) {
      metadata = Map<String, dynamic>.from(json['metadata'] as Map);
    }

    return BlockTelemetry(
      timestamp: timestamp,
      blockId: blockId,
      sensorData: sensorData,
      metadata: metadata,
      rawData: json,
    );
  }

  /// Convert to JSON map
  Map<String, dynamic> toJson() {
    return {
      'timestamp': timestamp.millisecondsSinceEpoch,
      if (blockId != null) 'blockId': blockId,
      if (sensorData.isNotEmpty) 'sensorData': sensorData,
      if (metadata.isNotEmpty) 'metadata': metadata,
      ...rawData,
    };
  }

  @override
  String toString() {
    return 'BlockTelemetry(timestamp: $timestamp, blockId: $blockId, sensorData: $sensorData)';
  }
}
