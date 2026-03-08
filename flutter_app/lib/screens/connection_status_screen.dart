import 'dart:async';
import 'package:flutter/material.dart';
import '../models/wifi_network.dart';
import '../services/ble_provision_service.dart';

/// 连接状态页面 - 显示 WiFi 连接进度和结果
class ConnectionStatusScreen extends StatefulWidget {
  final BleProvisionService bleService;
  final String ssid;

  const ConnectionStatusScreen({
    super.key,
    required this.bleService,
    required this.ssid,
  });

  @override
  State<ConnectionStatusScreen> createState() => _ConnectionStatusScreenState();
}

class _ConnectionStatusScreenState extends State<ConnectionStatusScreen> {
  late DeviceStatus _status;
  Timer? _timeoutTimer;

  @override
  void initState() {
    super.initState();
    _status = widget.bleService.deviceStatus;
    widget.bleService.addListener(_onServiceUpdate);

    // 超时处理：60秒后如果还未 connected/failed，标记超时
    _timeoutTimer = Timer(const Duration(seconds: 60), () {
      if (_status != DeviceStatus.connected &&
          _status != DeviceStatus.failed) {
        if (mounted) {
          setState(() {
            _status = DeviceStatus.failed;
          });
        }
      }
    });
  }

  @override
  void dispose() {
    widget.bleService.removeListener(_onServiceUpdate);
    _timeoutTimer?.cancel();
    super.dispose();
  }

  void _onServiceUpdate() {
    if (mounted) {
      setState(() {
        _status = widget.bleService.deviceStatus;
      });
    }
  }

  /// 返回到 WiFi 列表重试
  void _retry() {
    Navigator.pop(context); // 回到密码页或 WiFi 列表
  }

  /// 配网成功，回到首页
  void _done() {
    // 弹出所有页面回到设备扫描页
    Navigator.of(context).popUntil((route) => route.isFirst);
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('连接状态'),
        centerTitle: true,
        automaticallyImplyLeading: false, // 连接中禁止返回
      ),
      body: Center(
        child: Padding(
          padding: const EdgeInsets.all(32),
          child: Column(
            mainAxisAlignment: MainAxisAlignment.center,
            children: [
              // 状态图标
              _buildStatusIcon(),
              const SizedBox(height: 32),

              // WiFi 名称
              Text(
                widget.ssid,
                style: const TextStyle(
                  fontSize: 20,
                  fontWeight: FontWeight.bold,
                ),
              ),
              const SizedBox(height: 16),

              // 状态文字
              Text(
                _getStatusText(),
                style: TextStyle(
                  fontSize: 16,
                  color: _getStatusColor(),
                ),
                textAlign: TextAlign.center,
              ),

              // 连接成功后显示 IP 地址
              if (_status == DeviceStatus.connected &&
                  widget.bleService.connectedIp != null) ...[  
                const SizedBox(height: 12),
                Container(
                  padding: const EdgeInsets.symmetric(
                      horizontal: 16, vertical: 8),
                  decoration: BoxDecoration(
                    color: Colors.green.shade50,
                    borderRadius: BorderRadius.circular(8),
                    border: Border.all(color: Colors.green.shade200),
                  ),
                  child: Row(
                    mainAxisSize: MainAxisSize.min,
                    children: [
                      const Icon(Icons.lan, size: 16, color: Colors.green),
                      const SizedBox(width: 8),
                      Text(
                        'IP: ${widget.bleService.connectedIp}',
                        style: const TextStyle(
                          fontFamily: 'monospace',
                          fontSize: 14,
                          color: Colors.green,
                          fontWeight: FontWeight.bold,
                        ),
                      ),
                    ],
                  ),
                ),
              ],

              const SizedBox(height: 48),

              // 操作按钮
              if (_status == DeviceStatus.connected)
                SizedBox(
                  width: 200,
                  height: 48,
                  child: ElevatedButton(
                    onPressed: _done,
                    style: ElevatedButton.styleFrom(
                      backgroundColor: Colors.green,
                      foregroundColor: Colors.white,
                      shape: RoundedRectangleBorder(
                        borderRadius: BorderRadius.circular(8),
                      ),
                    ),
                    child: const Text('完成', style: TextStyle(fontSize: 16)),
                  ),
                ),

              if (_status == DeviceStatus.failed) ...[
                SizedBox(
                  width: 200,
                  height: 48,
                  child: ElevatedButton(
                    onPressed: _retry,
                    style: ElevatedButton.styleFrom(
                      backgroundColor: Colors.orange,
                      foregroundColor: Colors.white,
                      shape: RoundedRectangleBorder(
                        borderRadius: BorderRadius.circular(8),
                      ),
                    ),
                    child: const Text('重试', style: TextStyle(fontSize: 16)),
                  ),
                ),
                const SizedBox(height: 12),
                TextButton(
                  onPressed: _done,
                  child: const Text('返回首页'),
                ),
              ],
            ],
          ),
        ),
      ),
    );
  }

  Widget _buildStatusIcon() {
    switch (_status) {
      case DeviceStatus.idle:
      case DeviceStatus.scanning:
      case DeviceStatus.connecting:
        return const SizedBox(
          width: 100,
          height: 100,
          child: CircularProgressIndicator(
            strokeWidth: 4,
            color: Colors.blue,
          ),
        );
      case DeviceStatus.connected:
        return Container(
          width: 100,
          height: 100,
          decoration: BoxDecoration(
            color: Colors.green.shade50,
            shape: BoxShape.circle,
          ),
          child: const Icon(
            Icons.check_circle,
            size: 80,
            color: Colors.green,
          ),
        );
      case DeviceStatus.failed:
        return Container(
          width: 100,
          height: 100,
          decoration: BoxDecoration(
            color: Colors.red.shade50,
            shape: BoxShape.circle,
          ),
          child: const Icon(
            Icons.error,
            size: 80,
            color: Colors.red,
          ),
        );
    }
  }

  String _getStatusText() {
    switch (_status) {
      case DeviceStatus.idle:
        return '等待设备响应...';
      case DeviceStatus.scanning:
        return '设备正在处理...';
      case DeviceStatus.connecting:
        return '设备正在连接 WiFi...\n请稍候';
      case DeviceStatus.connected:
        return 'WiFi 连接成功！\n设备已接入网络';
      case DeviceStatus.failed:
        return 'WiFi 连接失败\n请检查密码是否正确';
    }
  }

  Color _getStatusColor() {
    switch (_status) {
      case DeviceStatus.idle:
      case DeviceStatus.scanning:
      case DeviceStatus.connecting:
        return Colors.blue;
      case DeviceStatus.connected:
        return Colors.green;
      case DeviceStatus.failed:
        return Colors.red;
    }
  }
}
