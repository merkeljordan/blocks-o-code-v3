import 'dart:io';
import 'dart:async';
import 'package:flutter/material.dart';

void main() => runApp(const BlocksOfCodeApp());

class BlocksOfCodeApp extends StatelessWidget {
  const BlocksOfCodeApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'Blocks of Code (v3)',
      theme: ThemeData(useMaterial3: true, colorSchemeSeed: Colors.indigo),
      home: const HomePage(),
      debugShowCheckedModeBanner: false,
    );
  }
}

class HomePage extends StatefulWidget {
  const HomePage({super.key});

  @override
  State<HomePage> createState() => _HomePageState();
}

class _HomePageState extends State<HomePage> {
  ServerSocket? _server;
  Socket? _clientSocket;
  String status = 'Server not started';
  final int serverPort = 41233;

  @override
  void initState() {
    super.initState();
    _startServer();
  }

  @override
  void dispose() {
    _stopServer();
    super.dispose();
  }

  Future<void> _startServer() async {
    try {
      _server = await ServerSocket.bind(InternetAddress.anyIPv4, serverPort);
      setState(() => status = 'Server listening on port $serverPort and address ${_server!.address.address}');

      _server!.listen(_handleClient, onError: (e) {
        setState(() => status = 'Server error: $e');
      }, onDone: () {
        setState(() => status = 'Server closed');
      });
    } catch (e) {
      setState(() => status = 'Failed to start server: $e');
    }
  }

  Future<void> _stopServer() async {
    try {
      await _clientSocket?.close();
      await _server?.close();
      setState(() => status = 'Server stopped');
    } catch (e) {
      setState(() => status = 'Error stopping server: $e');
    }
  }

  void _handleClient(Socket client) {
    _clientSocket?.destroy();
    _clientSocket = client;
    setState(() => status =
        'Client connected: ${client.remoteAddress.address}:${client.remotePort}');

    client.listen((data) {
      final msg = String.fromCharCodes(data).trim();
      setState(() => status = 'Received from ESP32: $msg');
    }, onDone: () {
      setState(() =>
          status = 'Client disconnected: ${client.remoteAddress.address}');
      if (_clientSocket == client) _clientSocket = null;
    }, onError: (e) {
      setState(() => status = 'Client error: $e');
      if (_clientSocket == client) _clientSocket = null;
    });
  }

  void sendMessageToESP(String msg) {
    final client = _clientSocket;
    if (client == null) {
      setState(() => status = 'No ESP32 connected');
      return;
    }
    try {
      client.write(msg + '\n');
      setState(() => status = 'Connected to ESP32 and sent: $msg');
    } catch (e) {
      setState(() => status = 'Send failed: $e');
    }
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: Colors.purple.shade200,
      body: Center(
        child: Container(
          padding: const EdgeInsets.all(24),
          constraints: const BoxConstraints(maxWidth: 500),
          decoration: BoxDecoration(
            gradient: LinearGradient(
              colors: [Colors.purple.shade200, Colors.pink.shade200],
              begin: Alignment.topLeft,
              end: Alignment.bottomRight,
            ),
            borderRadius: BorderRadius.circular(24),
            boxShadow: const [BoxShadow(color: Colors.black26, blurRadius: 12)],
          ),
          child: Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              const Text('Blocks of Code (v3)',
                  textAlign: TextAlign.center,
                  style: TextStyle(fontSize: 48, fontFamily: 'Modak', color: Colors.white)),
              const SizedBox(height: 30),
              Text(status,
                  textAlign: TextAlign.center,
                  style: const TextStyle(fontSize: 16, color: Colors.white)),
              const SizedBox(height: 24),
              ElevatedButton(
                onPressed: () => sendMessageToESP("Hello from Flutter!"),
                style: ElevatedButton.styleFrom(
                  backgroundColor: Colors.teal.shade400,
                  padding: const EdgeInsets.symmetric(horizontal: 40, vertical: 16),
                  shape: RoundedRectangleBorder(
                      borderRadius: BorderRadius.circular(16)),
                  elevation: 6,
                ),
                child: const Text("Start",
                    style: TextStyle(fontSize: 20, fontFamily: 'Modak', color: Colors.white)),
              ),
            ],
          ),
        ),
      ),
    );
  }
}
