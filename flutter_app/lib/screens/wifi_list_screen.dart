import 'package:flutter/material.dart';
import '../models/wifi_network.dart';
import '../services/ble_provision_service.dart';
import 'wifi_password_screen.dart';
import 'connection_status_screen.dart';
import 'hidden_wifi_screen.dart';

/// WiFi 列表页面 - 显示设备扫描到的 WiFi 网络
class WifiListScreen extends StatefulWidget {
  final BleProvisionService bleService;

  const WifiListScreen({super.key, required this.bleService});

  @override
  State<WifiListScreen> createState() => _WifiListScreenState();
}

class _WifiListScreenState extends State<WifiListScreen> {
  @override
  void initState() {
    super.initState();
    widget.bleService.addListener(_onServiceUpdate);
    // 连接成功后自动发起 WiFi 扫描
    _requestScan();
  }

  @override
  void dispose() {
    widget.bleService.removeListener(_onServiceUpdate);
    super.dispose();
  }

  void _onServiceUpdate() {
    if (mounted) setState(() {});
  }

  Future<void> _requestScan() async {
    await widget.bleService.requestWifiScan();
  }

  /// 选择 WiFi
  void _onWifiSelected(WifiNetwork network) {
    if (network.requiresPassword) {
      // 需要密码 → 跳转密码输入页
      Navigator.push(
        context,
        MaterialPageRoute(
          builder: (context) => WifiPasswordScreen(
            bleService: widget.bleService,
            network: network,
          ),
        ),
      );
    } else {
      // OPEN 网络 → 直接发送配置
      _connectToOpenNetwork(network);
    }
  }

  /// 连接开放网络 (无需密码)
  Future<void> _connectToOpenNetwork(WifiNetwork network) async {
    try {
      await widget.bleService.sendWifiConfig(
        ssid: network.ssid,
        password: '',
        security: network.security,
        hidden: false,
      );

      if (!mounted) return;

      Navigator.push(
        context,
        MaterialPageRoute(
          builder: (context) => ConnectionStatusScreen(
            bleService: widget.bleService,
            ssid: network.ssid,
          ),
        ),
      );
    } catch (e) {
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text('发送配置失败: $e')),
        );
      }
    }
  }

  @override
  Widget build(BuildContext context) {
    final networks = widget.bleService.wifiNetworks;
    final isScanning = widget.bleService.isScanning;

    return Scaffold(
      appBar: AppBar(
        title: const Text('选择WiFi'),
        centerTitle: true,
        actions: [
          if (isScanning)
            const Padding(
              padding: EdgeInsets.only(right: 16),
              child: SizedBox(
                width: 20,
                height: 20,
                child: CircularProgressIndicator(
                  strokeWidth: 2,
                  color: Colors.white,
                ),
              ),
            ),
        ],
      ),
      body: Column(
        children: [
          // 扫描中提示
          if (isScanning && networks.isEmpty)
            const Expanded(
              child: Center(
                child: Column(
                  mainAxisAlignment: MainAxisAlignment.center,
                  children: [
                    CircularProgressIndicator(),
                    SizedBox(height: 16),
                    Text(
                      '正在扫描WiFi...',
                      style: TextStyle(fontSize: 16, color: Colors.grey),
                    ),
                    SizedBox(height: 8),
                    Text(
                      '这可能需要几秒钟',
                      style: TextStyle(color: Colors.grey),
                    ),
                  ],
                ),
              ),
            ),

          // 空状态
          if (!isScanning && networks.isEmpty)
            Expanded(
              child: Center(
                child: Column(
                  mainAxisAlignment: MainAxisAlignment.center,
                  children: [
                    const Icon(Icons.wifi_off, size: 80, color: Colors.grey),
                    const SizedBox(height: 16),
                    const Text(
                      '未发现WiFi网络',
                      style: TextStyle(fontSize: 18, color: Colors.grey),
                    ),
                    const SizedBox(height: 24),
                    ElevatedButton.icon(
                      onPressed: _requestScan,
                      icon: const Icon(Icons.refresh),
                      label: const Text('重新扫描'),
                    ),
                  ],
                ),
              ),
            ),

          // WiFi 列表
          if (networks.isNotEmpty)
            Expanded(
              child: RefreshIndicator(
                onRefresh: _requestScan,
                child: ListView.builder(
                  itemCount: networks.length + 1, // +1 for hidden network button
                  itemBuilder: (context, index) {
                    if (index < networks.length) {
                      return _buildWifiItem(networks[index]);
                    }
                    // 最后一项：连接隐藏网络
                    return _buildHiddenNetworkButton();
                  },
                ),
              ),
            ),
        ],
      ),
      floatingActionButton: networks.isNotEmpty
          ? FloatingActionButton(
              onPressed: isScanning ? null : _requestScan,
              child: const Icon(Icons.refresh),
            )
          : null,
    );
  }

  Widget _buildWifiItem(WifiNetwork network) {
    return Card(
      margin: const EdgeInsets.symmetric(horizontal: 16, vertical: 4),
      child: ListTile(
        leading: _buildSignalIcon(network),
        title: Text(
          network.ssid,
          style: const TextStyle(fontWeight: FontWeight.bold),
        ),
        subtitle: Text(
          '${network.security} · 信号: ${network.rssi} dBm',
        ),
        trailing: network.requiresPassword
            ? const Icon(Icons.lock, size: 20, color: Colors.grey)
            : const Icon(Icons.lock_open, size: 20, color: Colors.green),
        onTap: () => _onWifiSelected(network),
      ),
    );
  }

  Widget _buildHiddenNetworkButton() {
    return Card(
      margin: const EdgeInsets.symmetric(horizontal: 16, vertical: 4),
      child: ListTile(
        leading: const CircleAvatar(
          backgroundColor: Color(0xFFE8EAF6),
          child: Icon(Icons.wifi_find, color: Colors.indigo),
        ),
        title: const Text(
          '连接隐藏网络',
          style: TextStyle(fontWeight: FontWeight.bold),
        ),
        subtitle: const Text('手动输入SSID和密码'),
        trailing: const Icon(Icons.arrow_forward_ios, size: 16),
        onTap: () {
          Navigator.push(
            context,
            MaterialPageRoute(
              builder: (context) => HiddenWifiScreen(
                bleService: widget.bleService,
              ),
            ),
          );
        },
      ),
    );
  }

  Widget _buildSignalIcon(WifiNetwork network) {
    IconData icon;
    Color color;

    switch (network.signalLevel) {
      case 3:
        icon = Icons.wifi;
        color = Colors.green;
        break;
      case 2:
        icon = Icons.wifi_2_bar;
        color = Colors.orange;
        break;
      case 1:
        icon = Icons.wifi_1_bar;
        color = Colors.red;
        break;
      default:
        icon = Icons.wifi_1_bar;
        color = Colors.red;
    }

    return CircleAvatar(
      backgroundColor: color.withOpacity(0.1),
      child: Icon(icon, color: color),
    );
  }
}
