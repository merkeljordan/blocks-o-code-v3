import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'app/blocks_of_code_app.dart';

void main() {
  WidgetsFlutterBinding.ensureInitialized();
  // Enable fullscreen mode
  SystemChrome.setEnabledSystemUIMode(SystemUiMode.immersiveSticky);
  runApp(const BlocksOfCodeApp());
}
