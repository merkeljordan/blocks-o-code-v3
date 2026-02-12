# Flutter App Setup Guide

This guide will help you set up the development environment for the Blocks o' Code Flutter application.

## Prerequisites

- **Operating System**: Windows, macOS, or Linux
- **Flutter SDK**: Version 3.9.2 or later
- **Dart SDK**: Bundled with Flutter
- **IDE**: VS Code, Android Studio, or IntelliJ IDEA (recommended: VS Code with Flutter extension)

## Installing Flutter

### Windows

1. Download Flutter SDK from [flutter.dev](https://flutter.dev/docs/get-started/install/windows)
2. Extract to a location (e.g., `C:\src\flutter`)
3. Add Flutter to PATH:
   - Search for "Environment Variables" in Windows
   - Add `C:\src\flutter\bin` to Path variable
4. Run `flutter doctor` to check installation

### macOS

```bash
# Download Flutter
cd ~/development
git clone https://github.com/flutter/flutter.git -b stable

# Add to PATH (add to ~/.zshrc or ~/.bash_profile)
export PATH="$PATH:$HOME/development/flutter/bin"

# Verify installation
flutter doctor
```

### Linux

```bash
# Download Flutter
cd ~/development
git clone https://github.com/flutter/flutter.git -b stable

# Add to PATH (add to ~/.bashrc)
export PATH="$PATH:$HOME/development/flutter/bin"

# Install dependencies
sudo apt-get install clang cmake ninja-build pkg-config libgtk-3-dev liblzma-dev

# Verify installation
flutter doctor
```

## Project Structure

```
companion_app/
├── lib/
│   ├── main.dart              # Entry point
│   ├── app/                   # App-level widgets
│   ├── screens/               # Screen widgets
│   ├── widgets/               # Reusable widgets
│   ├── models/                # Data models
│   ├── services/              # Business logic services
│   ├── providers/             # State management providers
│   ├── config/                # Configuration constants
│   └── utils/                 # Utility functions
├── assets/                    # Images, fonts, configs
├── pubspec.yaml              # Dependencies
└── README.md                 # App-specific docs
```

## Running the App

### 1. Install Dependencies

```bash
cd companion_app
flutter pub get
```

### 2. Check Connected Devices

```bash
flutter devices
```

### 3. Run the App

**Desktop (Windows/macOS/Linux):**
```bash
flutter run -d windows    # Windows
flutter run -d macos      # macOS
flutter run -d linux      # Linux
```

**Mobile:**
```bash
flutter run -d android    # Android
flutter run -d ios        # iOS (macOS only)
```

**Web:**
```bash
flutter run -d chrome     # Chrome browser
```

## Configuration

### TCP Server Port

Default port is `41233`. To change it, edit:
- `lib/config/app_config.dart`: Change `tcpPort` constant

### Network Configuration

The app listens on all IPv4 addresses (`0.0.0.0`) by default. Ensure:
- Your computer's firewall allows incoming connections on port `41233`
- ESP32 firmware has the correct IP address of your computer

## Development Workflow

### Hot Reload

While the app is running:
- Press `r` in terminal to hot reload
- Press `R` to hot restart
- Press `q` to quit

### Debugging

- **VS Code**: Use Flutter extension's debugger
- **Android Studio**: Use built-in debugger
- **Console**: Check terminal output for logs

### Building for Release

**Desktop:**
```bash
flutter build windows
flutter build macos
flutter build linux
```

**Mobile:**
```bash
flutter build apk          # Android APK
flutter build appbundle    # Android App Bundle
flutter build ios          # iOS (macOS only)
```

## Key Features

- **TCP Server**: Listens for ESP32 connections
- **Real-time Visualization**: Shows block configuration and telemetry
- **Validation**: Validates block sequences against rules
- **Connection Management**: Heartbeat and reconnection handling
- **State Management**: Uses Provider for state management

## Troubleshooting

### Build Errors

- **"pub get failed"**: Check internet connection, try `flutter pub cache repair`
- **"No devices found"**: Connect device or start emulator
- **"Port already in use"**: Another instance is running, kill it or change port

### Connection Issues

- **ESP32 can't connect**: 
  - Verify TCP server is running (check app UI)
  - Check firewall settings
  - Verify IP address matches ESP32 configuration

### Runtime Errors

- **"Provider not found"**: Ensure app is wrapped with `MultiProvider`
- **"Null check operator"**: Check for null values before using `!`

## Next Steps

- **[App Architecture](../architecture/app-architecture.md)** - Understand app design
- **[App API](../api/app-api.md)** - API reference
- **[Contributing](../development/contributing.md)** - Contribute to the project

## Additional Resources

- [Flutter Documentation](https://flutter.dev/docs)
- [Dart Language Tour](https://dart.dev/guides/language/language-tour)
- [Provider Package](https://pub.dev/packages/provider)
- [Flutter Cookbook](https://flutter.dev/docs/cookbook)
