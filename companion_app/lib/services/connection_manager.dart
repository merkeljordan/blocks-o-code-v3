import 'dart:async';
import 'dart:convert';
import '../config/app_config.dart';
import 'tcp_server_service.dart';

/// Manages connection state, heartbeat, and reconnection logic
class ConnectionManager {
  final TcpServerService _tcpService;
  
  Timer? _heartbeatTimer;
  DateTime? _lastHeartbeatTime;
  bool _isReconnecting = false;
  int _reconnectionAttempts = 0;
  bool _isConnected = false;
  
  // Stream controllers for state changes
  final _connectionStatusController = StreamController<String>.broadcast();
  final _isConnectedController = StreamController<bool>.broadcast();
  final _lastHeartbeatController = StreamController<DateTime?>.broadcast();
  final _reconnectionStateController = StreamController<Map<String, dynamic>>.broadcast();

  ConnectionManager(this._tcpService) {
    // Listen to TCP service messages for heartbeat acks
    _tcpService.messageStream?.listen((message) {
      _handleMessage(message);
      // Also check if connection was just established
      if (_tcpService.isConnected && !_isConnected) {
        updateConnectionState(true);
        updateConnectionStatus('Client connected: ${_tcpService.serverAddress}');
      }
    }, onError: (error) {
      updateConnectionState(false);
      updateConnectionStatus('Connection error: $error');
      _attemptReconnection();
    });
  }

  // Streams
  Stream<String> get connectionStatusStream => _connectionStatusController.stream;
  Stream<bool> get isConnectedStream => _isConnectedController.stream;
  Stream<DateTime?> get lastHeartbeatStream => _lastHeartbeatController.stream;
  Stream<Map<String, dynamic>> get reconnectionStateStream => _reconnectionStateController.stream;

  // Getters
  bool get isConnected => _isConnected;
  DateTime? get lastHeartbeatTime => _lastHeartbeatTime;
  bool get isReconnecting => _isReconnecting;
  int get reconnectionAttempts => _reconnectionAttempts;

  /// Handle incoming messages (for heartbeat acks)
  void _handleMessage(String message) {
    try {
      final json = jsonDecode(message) as Map<String, dynamic>;
      if (json.containsKey('type') && json['type'] == 'heartbeat_ack') {
        _lastHeartbeatTime = DateTime.now();
        _lastHeartbeatController.add(_lastHeartbeatTime);
        _connectionStatusController.add('Heartbeat received');
      }
    } catch (e) {
      // Not a heartbeat ack, ignore
    }
  }

  /// Start heartbeat mechanism
  void startHeartbeat() {
    stopHeartbeat();
    _lastHeartbeatTime = DateTime.now();
    _lastHeartbeatController.add(_lastHeartbeatTime);

    _heartbeatTimer = Timer.periodic(AppConfig.heartbeatInterval, (timer) {
      _sendHeartbeat();
      _checkHeartbeat();
    });
  }

  /// Stop heartbeat mechanism
  void stopHeartbeat() {
    _heartbeatTimer?.cancel();
    _heartbeatTimer = null;
  }

  /// Send heartbeat to client
  void _sendHeartbeat() {
    if (!isConnected) {
      return;
    }

    try {
      final heartbeat = jsonEncode({
        'type': 'heartbeat',
        'timestamp': DateTime.now().millisecondsSinceEpoch,
      });
      _tcpService.sendMessage(heartbeat);
    } catch (e) {
      _connectionStatusController.add('Heartbeat send failed: $e');
      _isConnectedController.add(false);
      _attemptReconnection();
    }
  }

  /// Check if heartbeat timeout occurred
  void _checkHeartbeat() {
    if (!isConnected || _lastHeartbeatTime == null) {
      return;
    }

    final timeSinceLastHeartbeat = DateTime.now().difference(_lastHeartbeatTime!);
    if (timeSinceLastHeartbeat > AppConfig.heartbeatTimeout) {
      _connectionStatusController.add('Heartbeat timeout - connection lost');
      _isConnectedController.add(false);
      stopHeartbeat();
      _attemptReconnection();
    }
  }

  /// Attempt reconnection with exponential backoff
  Future<void> _attemptReconnection() async {
    if (_isReconnecting) {
      return; // Already attempting reconnection
    }

    if (_reconnectionAttempts >= AppConfig.maxReconnectionAttempts) {
      _connectionStatusController.add('Max reconnection attempts reached. Please restart server.');
      _isReconnecting = false;
      _reconnectionAttempts = 0;
      _reconnectionStateController.add({
        'isReconnecting': false,
        'attempts': 0,
      });
      return;
    }

    _isReconnecting = true;
    _reconnectionAttempts++;
    _reconnectionStateController.add({
      'isReconnecting': true,
      'attempts': _reconnectionAttempts,
    });

    // Exponential backoff: 1s, 2s, 4s, 8s, 16s (capped at 30s)
    final delaySeconds = (1 << (_reconnectionAttempts - 1)).clamp(1, 30);

    _connectionStatusController.add(
      'Reconnecting in ${delaySeconds}s (attempt $_reconnectionAttempts/${AppConfig.maxReconnectionAttempts})...',
    );

    await Future.delayed(Duration(seconds: delaySeconds));

    try {
      // Wait for connection to be established
      await Future.delayed(const Duration(seconds: 1));
      
      if (isConnected) {
        _isReconnecting = false;
        _reconnectionAttempts = 0;
        _reconnectionStateController.add({
          'isReconnecting': false,
          'attempts': 0,
        });
      } else {
        _attemptReconnection();
      }
    } catch (e) {
      _connectionStatusController.add('Reconnection failed: $e');
      _attemptReconnection();
    }
  }

  /// Update connection status
  void updateConnectionStatus(String status) {
    _connectionStatusController.add(status);
  }

  /// Update connection state
  void updateConnectionState(bool connected) {
    _isConnected = connected;
    _isConnectedController.add(connected);
    if (connected) {
      _lastHeartbeatTime = DateTime.now();
      _lastHeartbeatController.add(_lastHeartbeatTime);
      startHeartbeat();
    } else {
      stopHeartbeat();
    }
  }

  /// Dispose resources
  void dispose() {
    stopHeartbeat();
    _connectionStatusController.close();
    _isConnectedController.close();
    _lastHeartbeatController.close();
    _reconnectionStateController.close();
  }
}
