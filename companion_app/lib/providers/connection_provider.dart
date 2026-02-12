import 'package:flutter/foundation.dart';
import 'dart:async';
import '../services/tcp_server_service.dart';
import '../services/connection_manager.dart';
import '../config/app_config.dart';

/// Provider for managing TCP server and connection state
class ConnectionProvider extends ChangeNotifier {
  final TcpServerService _tcpService = TcpServerService();
  ConnectionManager? _connectionManager;
  
  String _connectionStatus = 'Server not started';
  bool _isConnected = false;
  DateTime? _lastHeartbeatTime;
  bool _isReconnecting = false;
  int _reconnectionAttempts = 0;
  
  StreamSubscription<String>? _statusSubscription;
  StreamSubscription<bool>? _connectedSubscription;
  StreamSubscription<DateTime?>? _heartbeatSubscription;
  StreamSubscription<Map<String, dynamic>>? _reconnectionSubscription;

  ConnectionProvider() {
    _setupSubscriptions();
  }

  void _setupSubscriptions() {
    _connectionManager = ConnectionManager(_tcpService);
    
    _statusSubscription = _connectionManager!.connectionStatusStream.listen((status) {
      _connectionStatus = status;
      notifyListeners();
    });
    
    _connectedSubscription = _connectionManager!.isConnectedStream.listen((connected) {
      _isConnected = connected;
      notifyListeners();
    });
    
    _heartbeatSubscription = _connectionManager!.lastHeartbeatStream.listen((time) {
      _lastHeartbeatTime = time;
      notifyListeners();
    });
    
    _reconnectionSubscription = _connectionManager!.reconnectionStateStream.listen((state) {
      _isReconnecting = state['isReconnecting'] as bool;
      _reconnectionAttempts = state['attempts'] as int;
      notifyListeners();
    });
  }

  // Getters
  bool get isServerRunning => _tcpService.isServerRunning;
  bool get isConnected => _isConnected;
  String get connectionStatus => _connectionStatus;
  DateTime? get lastHeartbeatTime => _lastHeartbeatTime;
  bool get isReconnecting => _isReconnecting;
  int get reconnectionAttempts => _reconnectionAttempts;
  String? get serverAddress => _tcpService.serverAddress;
  Stream<String>? get messageStream => _tcpService.messageStream;

  /// Start the TCP server
  Future<void> startServer() async {
    try {
      await _tcpService.startServer();
      _connectionStatus = 'Server listening on port ${AppConfig.tcpPort}';
      
      // Connection will be established when client connects
      // The ConnectionManager will handle state updates via streams
      
      notifyListeners();
    } catch (e) {
      _connectionStatus = 'Failed to start server: $e';
      notifyListeners();
      rethrow;
    }
  }

  /// Stop the TCP server
  Future<void> stopServer() async {
    try {
      _connectionManager?.stopHeartbeat();
      await _tcpService.stopServer();
      _isConnected = false;
      _connectionStatus = 'Server stopped';
      _lastHeartbeatTime = null;
      _isReconnecting = false;
      _reconnectionAttempts = 0;
      notifyListeners();
    } catch (e) {
      _connectionStatus = 'Failed to stop server: $e';
      notifyListeners();
    }
  }

  /// Send a message to the connected client
  void sendMessage(String message) {
    try {
      _tcpService.sendMessage(message);
      _connectionStatus = 'Sent: $message';
      notifyListeners();
    } catch (e) {
      _connectionStatus = 'Send failed: $e';
      notifyListeners();
      _connectionManager?.updateConnectionState(false);
    }
  }

  /// Update connection status (for external updates)
  void updateConnectionStatus(String status) {
    _connectionStatus = status;
    notifyListeners();
  }

  @override
  void dispose() {
    _statusSubscription?.cancel();
    _connectedSubscription?.cancel();
    _heartbeatSubscription?.cancel();
    _reconnectionSubscription?.cancel();
    _connectionManager?.dispose();
    _tcpService.stopServer();
    super.dispose();
  }
}
