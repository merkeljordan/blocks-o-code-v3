import 'dart:io';
import 'dart:async';
import 'dart:convert';
import '../config/app_config.dart';

/// Service for managing TCP server connections
class TcpServerService {
  ServerSocket? _serverSocket;
  Socket? _clientSocket;
  StreamController<String>? _messageController;
  
  bool get isServerRunning => _serverSocket != null;
  bool get isConnected => _clientSocket != null;
  Socket? get clientSocket => _clientSocket;
  
  /// Stream of incoming messages
  Stream<String>? get messageStream => _messageController?.stream;

  /// Start the TCP server
  Future<void> startServer() async {
    if (isServerRunning) {
      throw Exception('Server is already running');
    }

    try {
      _serverSocket = await ServerSocket.bind(
        InternetAddress.anyIPv4,
        AppConfig.tcpPort,
      );

      _messageController = StreamController<String>.broadcast();

      // Listen for client connections
      _serverSocket!.listen(
        _handleClient,
        onError: (error) {
          _messageController?.addError('Server error: $error');
        },
        onDone: () {
          _messageController?.addError('Server closed');
        },
      );
    } catch (e) {
      throw Exception('Failed to start server: $e');
    }
  }

  /// Stop the TCP server
  Future<void> stopServer() async {
    _clientSocket?.destroy();
    _clientSocket = null;
    await _serverSocket?.close();
    _serverSocket = null;
    await _messageController?.close();
    _messageController = null;
  }

  /// Handle client connection
  void _handleClient(Socket client) {
    // Close previous client if exists
    _clientSocket?.destroy();
    _clientSocket = client;

    // Set up message listener with JSON parsing
    String buffer = '';
    client.listen(
      (data) {
        final msg = String.fromCharCodes(data);
        buffer += msg;

        // Process complete messages (assuming newline-delimited)
        final lines = buffer.split('\n');
        buffer = lines.removeLast(); // Keep incomplete line in buffer

        for (final line in lines) {
          if (line.trim().isEmpty) continue;
          _messageController?.add(line.trim());
        }
      },
      onDone: () {
        _clientSocket = null;
        _messageController?.addError('Client disconnected');
      },
      onError: (e) {
        _clientSocket = null;
        _messageController?.addError('Client error: $e');
      },
    );
  }

  /// Send a message to the connected client
  void sendMessage(String message) {
    if (_clientSocket == null) {
      throw Exception('No client connected');
    }
    try {
      _clientSocket!.write(message + '\n');
    } catch (e) {
      throw Exception('Failed to send message: $e');
    }
  }

  /// Get server address
  String? get serverAddress {
    if (_serverSocket == null) return null;
    return '${_serverSocket!.address.address}:${AppConfig.tcpPort}';
  }
}
