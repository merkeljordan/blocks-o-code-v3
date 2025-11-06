import 'package:flutter/material.dart';
import 'dart:io';
import 'dart:async';

void main() => runApp(const BlocksOfCodeApp());

class BlocksOfCodeApp extends StatelessWidget {
  const BlocksOfCodeApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'Blocks of Code (v3)',
      theme: ThemeData(
        useMaterial3: true,
        colorSchemeSeed: Colors.indigo,
      ),
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
  bool connected = false;
  String status = 'Not connected';
  WebSocket? socket;

  final ipController = TextEditingController(text: "192.168.4.1"); // change to your ESP IP

  Future<void> connectToESP() async {
    final ip = ipController.text.trim();
    final url = 'ws://$ip:81/';

    setState(() => status = 'Connecting to $url ...');

    try {
      socket = await WebSocket.connect(url);
      setState(() {
        connected = true;
        status = '✅ Connected to $ip';
      });

      socket!.listen((message) {
        setState(() {
          status = '📩 Message from ESP: $message';
        });
      }, onDone: () {
        setState(() {
          connected = false;
          status = 'Connection closed';
        });
      });
    } catch (e) {
      setState(() => status = '❌ Failed: $e');
    }
  }

  void sendMessage(String msg) {
    if (socket != null && connected) {
      socket!.add(msg);
      setState(() => status = '📤 Sent: $msg');
    }
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: const Color.fromARGB(255, 206, 113, 221),
      body: Center(
        child: connected ? _buildConnected() : _buildLanding(),
      ),
    );
  }

  Widget _buildLanding() {
  return Center(
    child: Container(
      padding: const EdgeInsets.all(24),
      constraints: const BoxConstraints(maxWidth: 550),
      decoration: BoxDecoration(
        gradient: LinearGradient(
          colors: [Colors.purple.shade200, Colors.pink.shade200],
          begin: Alignment.topLeft,
          end: Alignment.bottomRight,
        ),
        borderRadius: BorderRadius.circular(24),
        boxShadow: [
          BoxShadow(
            color: Colors.black26,
            blurRadius: 12,
            offset: const Offset(4, 6),
          ),
        ],
      ),
      child: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          const Text(
            '🧩 Blocks of Code (v3)',
            textAlign: TextAlign.center,
            style: TextStyle(
              fontSize: 48,
              fontFamily: 'Modak',
              color: Colors.white,
            ),
          ),
          const SizedBox(height: 30),
          TextField(
            controller: ipController,
            textAlign: TextAlign.center,
            decoration: InputDecoration(
              labelText: 'ESP32 IP Address',
              labelStyle: const TextStyle(fontWeight: FontWeight.bold),
              filled: true,
              fillColor: Colors.white.withOpacity(0.9),
              border: OutlineInputBorder(
                borderRadius: BorderRadius.circular(16),
                borderSide: BorderSide.none,
              ),
              contentPadding: const EdgeInsets.symmetric(
                vertical: 16,
                horizontal: 20,
              ),
            ),
            style: const TextStyle(
              fontSize: 18,
              fontWeight: FontWeight.w500,
            ),
          ),
          const SizedBox(height: 24),
          MouseRegion(
            cursor: SystemMouseCursors.click,
            child: ElevatedButton(
              onPressed: connectToESP,
              style: ElevatedButton.styleFrom(
                backgroundColor: Colors.purple.shade400,
                padding: const EdgeInsets.symmetric(
                    horizontal: 48, vertical: 18),
                shape: RoundedRectangleBorder(
                  borderRadius: BorderRadius.circular(20),
                ),
                elevation: 8,
              ),
              child: const Text(
                'Start',
                style: TextStyle(
                  fontSize: 22,
                  fontFamily: 'Modak',
                  color: Colors.white,
                ),
              ),
            ),
          ),
          const SizedBox(height: 20),
          AnimatedOpacity(
            opacity: status.isNotEmpty ? 1.0 : 0.0,
            duration: const Duration(milliseconds: 400),
            child: Text(
              status,
              textAlign: TextAlign.center,
              style: const TextStyle(
                fontSize: 16,
                color: Colors.white,
                fontWeight: FontWeight.w500,
              ),
            ),
          ),
        ],
      ),
    ),
  );
}

Widget _buildConnected() {
  return Center(
    child: Container(
      padding: const EdgeInsets.all(24),
      constraints: const BoxConstraints(maxWidth: 400),
      decoration: BoxDecoration(
        gradient: LinearGradient(
          colors: [Colors.green.shade200, Colors.teal.shade200],
          begin: Alignment.topLeft,
          end: Alignment.bottomRight,
        ),
        borderRadius: BorderRadius.circular(24),
        boxShadow: [
          BoxShadow(
            color: Colors.black26,
            blurRadius: 12,
            offset: const Offset(4, 6),
          ),
        ],
      ),
      child: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          const Icon(Icons.cloud_done, size: 80, color: Colors.white),
          const SizedBox(height: 20),
          AnimatedOpacity(
            opacity: 1.0,
            duration: const Duration(milliseconds: 500),
            child: Text(
              status,
              textAlign: TextAlign.center,
              style: const TextStyle(
                fontSize: 18,
                fontFamily: 'Modak',
                color: Colors.white,
              ),
            ),
          ),
          const SizedBox(height: 20),
          ElevatedButton(
            onPressed: () => sendMessage("Hello from Flutter!"),
            style: ElevatedButton.styleFrom(
              backgroundColor: Colors.teal.shade400,
              padding: const EdgeInsets.symmetric(
                  horizontal: 40, vertical: 16),
              shape: RoundedRectangleBorder(
                borderRadius: BorderRadius.circular(16),
              ),
              elevation: 6,
            ),
            child: const Text(
              "Send Message",
              style: TextStyle(
                fontSize: 20,
                fontFamily: 'Modak',
                color: Colors.white,
              ),
            ),
          ),
        ],
      ),
    ),
  );
}
}