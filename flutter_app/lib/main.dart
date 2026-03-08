import 'package:flutter/material.dart';
import 'screens/device_scan_screen.dart';

void main() {
  WidgetsFlutterBinding.ensureInitialized();
  runApp(const WifiProvisionApp());
}

class WifiProvisionApp extends StatelessWidget {
  const WifiProvisionApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'WiFi 配网',
      debugShowCheckedModeBanner: false,
      theme: ThemeData(
        colorSchemeSeed: Colors.blue,
        useMaterial3: true,
        appBarTheme: const AppBarTheme(
          backgroundColor: Colors.blue,
          foregroundColor: Colors.white,
          elevation: 2,
        ),
      ),
      home: const DeviceScanScreen(),
    );
  }
}
