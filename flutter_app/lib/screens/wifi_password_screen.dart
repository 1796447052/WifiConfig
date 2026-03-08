import 'package:flutter/material.dart';
import '../models/wifi_network.dart';
import '../services/ble_provision_service.dart';
import 'connection_status_screen.dart';

/// WiFi 密码输入页面
class WifiPasswordScreen extends StatefulWidget {
  final BleProvisionService bleService;
  final WifiNetwork network;
  final bool isHidden;

  const WifiPasswordScreen({
    super.key,
    required this.bleService,
    required this.network,
    this.isHidden = false,
  });

  @override
  State<WifiPasswordScreen> createState() => _WifiPasswordScreenState();
}

class _WifiPasswordScreenState extends State<WifiPasswordScreen> {
  final TextEditingController _passwordController = TextEditingController();
  bool _obscurePassword = true;
  bool _isSending = false;

  @override
  void dispose() {
    _passwordController.dispose();
    super.dispose();
  }

  /// 发送 WiFi 配置
  Future<void> _submitConfig() async {
    String password = _passwordController.text.trim();

    if (password.isEmpty) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text('请输入WiFi密码')),
      );
      return;
    }

    // 密码长度校验
    if (password.length < 8 &&
        widget.network.security != 'WEP') {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text('WiFi密码长度至少8位')),
      );
      return;
    }

    setState(() => _isSending = true);

    try {
      await widget.bleService.sendWifiConfig(
        ssid: widget.network.ssid,
        password: password,
        security: widget.network.security,
        hidden: widget.isHidden,
      );

      if (!mounted) return;

      // 跳转到连接状态页面
      Navigator.push(
        context,
        MaterialPageRoute(
          builder: (context) => ConnectionStatusScreen(
            bleService: widget.bleService,
            ssid: widget.network.ssid,
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
        title: const Text('输入密码'),
        centerTitle: true,
      ),
      body: SingleChildScrollView(
        padding: const EdgeInsets.all(24),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            // WiFi 信息卡片
            Card(
              child: Padding(
                padding: const EdgeInsets.all(16),
                child: Row(
                  children: [
                    const Icon(Icons.wifi, size: 40, color: Colors.blue),
                    const SizedBox(width: 16),
                    Expanded(
                      child: Column(
                        crossAxisAlignment: CrossAxisAlignment.start,
                        children: [
                          Text(
                            widget.network.ssid,
                            style: const TextStyle(
                              fontSize: 18,
                              fontWeight: FontWeight.bold,
                            ),
                          ),
                          const SizedBox(height: 4),
                          Text(
                            '安全性: ${widget.network.security}',
                            style: const TextStyle(color: Colors.grey),
                          ),
                        ],
                      ),
                    ),
                  ],
                ),
              ),
            ),

            const SizedBox(height: 32),

            // 密码输入框
            TextField(
              controller: _passwordController,
              obscureText: _obscurePassword,
              autofocus: true,
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

            const SizedBox(height: 16),

            // 提示信息
            const Text(
              '提示: 密码将通过 BLE 发送到设备，设备将尝试连接此WiFi网络。',
              style: TextStyle(fontSize: 12, color: Colors.grey),
              textAlign: TextAlign.center,
            ),
          ],
        ),
      ),
    );
  }
}
