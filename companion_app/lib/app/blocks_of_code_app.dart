import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import '../screens/main_screen.dart';
import '../providers/connection_provider.dart';
import '../providers/block_config_provider.dart';

class BlocksOfCodeApp extends StatelessWidget {
  const BlocksOfCodeApp({super.key});

  @override
  Widget build(BuildContext context) {
    // Custom vibrant color scheme with pinks, blues, and purples
    final colorScheme = ColorScheme.fromSeed(
      seedColor: const Color(0xFF9C27B0), // Purple base
      brightness: Brightness.dark,
      primary: const Color(0xFFE91E63), // Pink
      secondary: const Color(0xFF2196F3), // Blue
      tertiary: const Color(0xFF9C27B0), // Purple
      surface: const Color(0xFF1A1A2E),
      onSurface: Colors.white,
      primaryContainer: const Color(0xFFE91E63).withOpacity(0.2),
      secondaryContainer: const Color(0xFF2196F3).withOpacity(0.2),
      tertiaryContainer: const Color(0xFF9C27B0).withOpacity(0.2),
    );

    return MultiProvider(
      providers: [
        ChangeNotifierProvider(create: (_) => ConnectionProvider()),
        ChangeNotifierProvider(create: (_) => BlockConfigProvider()),
      ],
      child: MaterialApp(
        title: 'Blocks of Code (v3)',
        theme: ThemeData(
          useMaterial3: true,
          colorScheme: colorScheme,
          brightness: Brightness.dark,
        ),
        home: const MainScreen(),
        debugShowCheckedModeBanner: false,
      ),
    );
  }
}
