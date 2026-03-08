import 'package:flutter/material.dart';
import '../services/ble_provision_service.dart';
import 'connection_status_screen.dart';

/// 隐藏 WiFi 网络连接页面 - 手动输入 SSID 和密码
class HiddenWifiScreen extends StatefulWidget {
  final BleProvisionService bleService;

  const HiddenWifiScreen({super.key, required this.bleService});

  @override
  State<HiddenWifiScreen> createState() => _HiddenWifiScreenState();
}

class _HiddenWifiScreenState extends State<HiddenWifiScreen> {
  final TextEditingController _ssidController = TextEditingController();
  final TextEditingController _passwordController = TextEditingController();
  bool _obscurePassword = true;
  bool _isSending = false;
  String _selectedSecurity = 'WPA2';

  static const List<String> _securityOptions = [
    'OPEN',
    'WEP',
    'WPA',
    'WPA2',
    'WPA3',
  ];

  @override
  void dispose() {
    _ssidController.dispose();
    _passwordController.dispose();
    super.dispose();
  }

  bool get _needsPassword => _selectedSecurity != 'OPEN';

  Future<void> _submitConfig() async {
    String ssid = _ssidController.text.trim();
    String password = _passwordController.text.trim();

    if (ssid.isEmpty) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text('请输入WiFi名称 (SSID)')),
      );
      return;
    }

    if (_needsPassword && password.isEmpty) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text('请输入WiFi密码')),
      );
      return;
    }

    if (_needsPassword &&
        password.length < 8 &&
        _selectedSecurity != 'WEP') {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text('WiFi密码长度至少8位')),
      );
      return;
    }

    setState(() => _isSending = true);

    try {
      await widget.bleService.sendWifiConfig(
        ssid: ssid,
        password: password,
        security: _selectedSecurity,
        hidden: true,
      );

      if (!mounted) return;

      Navigator.push(
        context,
        MaterialPageRoute(
          builder: (context) => ConnectionStatusScreen(
            bleService: widget.bleService,
            ssid: ssid,
          ),
        ),
      );
    } catch (e) {
      if (mounted) {
        setState(() => _isSending = false);
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text('发送配置失败: $e')),
        );
      }
    }
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('连接隐藏网络'),
        centerTitle: true,
      ),
      body: SingleChildScrollView(
        padding: const EdgeInsets.all(24),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            // 提示
            const Card(
              child: Padding(
                padding: EdgeInsets.all(16),
                child: Row(
                  children: [
                    Icon(Icons.info_outline, color: Colors.blue),
                    SizedBox(width: 12),
                    Expanded(
                      child: Text(
                        '隐藏网络不会在扫描结果中显示，需要手动输入网络名称。',
                        style: TextStyle(color: Colors.grey),
                      ),
                    ),
                  ],
                ),
              ),
            ),

            const SizedBox(height: 24),

            // SSID 输入框
            TextField(
              controller: _ssidController,
              autofocus: true,
              enabled: !_isSending,
              decoration: const InputDecoration(
                labelText: '网络名称 (SSID)',
                hintText: '请输入WiFi名称',
                prefixIcon: Icon(Icons.wifi),
                border: OutlineInputBorder(),
              ),
            ),

            const SizedBox(height: 16),

            // 安全性选择
            DropdownButtonFormField<String>(
              value: _selectedSecurity,
              decoration: const InputDecoration(
                labelText: '安全性',
                prefixIcon: Icon(Icons.security),
                border: OutlineInputBorder(),
              ),
              items: _securityOptions.map((s) {
                return DropdownMenuItem(value: s, child: Text(s));
              }).toList(),
              onChanged: _isSending
                  ? null
                  : (value) {
                      if (value != null) {
                        setState(() => _selectedSecurity = value);
                      }
                    },
            ),

            const SizedBox(height: 16),

            // 密码输入框（仅非 OPEN 网络显示）
            if (_needsPassword)
              TextField(
                controller: _passwordController,
                obscureText: _obscurePassword,
                enabled: !_isSending,
                decoration: InputDecoration(
                  labelText: 'WiFi密码',
                  hintText: '请输入WiFi密码',
                  prefixIcon: const Icon(Icons.lock),
                  suffixIcon: IconButton(
                    icon: Icon(
                      _obscurePassword
                          ? Icons.visibility_off
                          : Icons.visibility,
                    ),
                    onPressed: () {
                      setState(() {
                        _obscurePassword = !_obscurePassword;
                      });
                    },
                  ),
                  border: const OutlineInputBorder(),
                ),
                onSubmitted: (_) => _submitConfig(),
              ),

            const SizedBox(height: 32),

            // 连接按钮
            SizedBox(
              height: 48,
              child: ElevatedButton(
                onPressed: _isSending ? null : _submitConfig,
                style: ElevatedButton.styleFrom(
                  backgroundColor: Colors.blue,
                  foregroundColor: Colors.white,
                  shape: RoundedRectangleBorder(
                    borderRadius: BorderRadius.circular(8),
                  ),
                ),
                child: _isSending
                    ? const SizedBox(
                        width: 24,
                        height: 24,
                        child: CircularProgressIndicator(
                          strokeWidth: 2,
                          color: Colors.white,
                        ),
                      )
                    : const Text(
                        '连接WiFi',
                        style: TextStyle(fontSize: 16),
                      ),
              ),
            ),
          ],
        ),
      ),
    );
  }
}
