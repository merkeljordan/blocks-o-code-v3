/// Application-wide configuration constants
class AppConfig {
  // TCP Server Configuration
  static const int tcpPort = 41233;
  
  // Heartbeat Configuration
  static const Duration heartbeatInterval = Duration(seconds: 30);
  static const Duration heartbeatTimeout = Duration(seconds: 60); // 2x interval
  
  // Reconnection Configuration
  static const int maxReconnectionAttempts = 5;
  
  // Stress Test Configuration
  static const int defaultStressTestMessageRate = 10; // messages per second
  static const Duration defaultStressTestDuration = Duration(minutes: 5);
  
  // Telemetry Configuration
  static const int maxTelemetryEntries = 100;
  
  // UI Configuration
  static const Duration menuAnimationDuration = Duration(milliseconds: 300);
  static const Duration screenTransitionDuration = Duration(milliseconds: 400);
}
