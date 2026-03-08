import 'dart:async';
import 'dart:io';
import 'package:flutter/material.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import 'package:permission_handler/permission_handler.dart';
import '../services/ble_provision_service.dart';
import 'wifi_list_screen.dart';

/// 设备扫描页面 - 扫描并连接 BLE IoT 设备
class DeviceScanScreen extends StatefulWidget {
  const DeviceScanScreen({super.key});

  @override
  State<DeviceScanScreen> createState() => _DeviceScanScreenState();
}

class _DeviceScanScreenState extends State<DeviceScanScreen> {
  final BleProvisionService _bleService = BleProvisionService();
  List<ScanResult> _scanResults = [];
  bool _isScanning = false;
  bool _isConnecting = false;
  String? _errorMessage;
  StreamSubscription? _scanSubscription;
  StreamSubscription? _scanningSubscription;

  @override
  void initState() {
    super.initState();
    _requestPermissions();
  }

  @override
  void dispose() {
    _scanSubscription?.cancel();
    _scanningSubscription?.cancel();
    _bleService.dispose();
    super.dispose();
  }

  /// 请求蓝牙和定位权限
  Future<void> _requestPermissions() async {
    if (Platform.isAndroid) {
      Map<Permission, PermissionStatus> statuses = await [
        Permission.bluetoothScan,
        Permission.bluetoothConnect,
        Permission.location,
      ].request();

      bool allGranted = statuses.values.every(
        (s) => s == PermissionStatus.granted,
      );

      if (!allGranted && mounted) {
        setState(() {
          _errorMessage = '需要蓝牙和定位权限才能扫描设备';
        });
        return;
      }
    }

    // 检查蓝牙是否已开启
    BluetoothAdapterState adapterState =
        await FlutterBluePlus.adapterState.first;
    if (adapterState != BluetoothAdapterState.on) {
      if (mounted) {
        setState(() {
          _errorMessage = '请先开启蓝牙';
        });
      }
      return;
    }

    _startScan();
  }

  /// 开始扫描 BLE 设备
  void _startScan() {
    setState(() {
      _isScanning = true;
      _scanResults = [];
      _errorMessage = null;
    });

    _scanSubscription?.cancel();
    _scanSubscription = _bleService.scanForDevices().listen(
      (results) {
        if (mounted) {
          setState(() {
            _scanResults = results;
          });
        }
      },
      onError: (e) {
        if (mounted) {
          setState(() {
            _errorMessage = '扫描出错: $e';
            _isScanning = false;
          });
        }
      },
    );

    // 监听扫描状态
    _scanningSubscription?.cancel();
    _scanningSubscription = FlutterBluePlus.isScanning.listen((scanning) {
      if (mounted) {
        setState(() {
          _isScanning = scanning;
        });
      }
    });
  }

  /// 连接到设备
  Future<void> _connectToDevice(BluetoothDevice device) async {
    setState(() {
      _isConnecting = true;
      _errorMessage = null;
    });

    // 停止扫描
    await _bleService.stopScan();

    bool success = await _bleService.connectToDevice(device);

    if (!mounted) return;

    setState(() {
      _isConnecting = false;
    });

    if (success) {
      Navigator.push(
        context,
        MaterialPageRoute(
          builder: (context) => WifiListScreen(bleService: _bleService),
        ),
      ).then((_) {
        // 从 WiFi 列表页面返回时断开连接
        _bleService.disconnect();
      });
    } else {
      setState(() {
        _errorMessage = '连接设备失败，请重试';
      });
    }
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('搜索设备'),
        centerTitle: true,
        actions: [
          if (_isScanning)
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
          // 错误信息
          if (_errorMessage != null)
            Container(
              width: double.infinity,
              padding: const EdgeInsets.all(12),
              color: Colors.red.shade50,
              child: Row(
                children: [
                  Icon(Icons.error_outline, color: Colors.red.shade700),
                  const SizedBox(width: 8),
                  Expanded(
                    child: Text(
                      _errorMessage!,
                      style: TextStyle(color: Colors.red.shade700),
                    ),
                  ),
                ],
              ),
            ),

          // 连接中遮罩
          if (_isConnecting)
            Container(
              width: double.infinity,
              padding: const EdgeInsets.all(16),
              color: Colors.blue.shade50,
              child: const Row(
                mainAxisAlignment: MainAxisAlignment.center,
                children: [
                  SizedBox(
                    width: 20,
                    height: 20,
                    child: CircularProgressIndicator(strokeWidth: 2),
                  ),
                  SizedBox(width: 12),
                  Text('正在连接设备...'),
                ],
              ),
            ),

          // 提示信息
          if (_scanResults.isEmpty && !_isScanning && _errorMessage == null)
            const Expanded(
              child: Center(
                child: Column(
                  mainAxisAlignment: MainAxisAlignment.center,
                  children: [
                    Icon(Icons.bluetooth_searching,
                        size: 80, color: Colors.grey),
                    SizedBox(height: 16),
                    Text(
                      '未发现设备',
                      style: TextStyle(fontSize: 18, color: Colors.grey),
                    ),
                    SizedBox(height: 8),
                    Text(
                      '请确保设备已开启并在附近',
                      style: TextStyle(color: Colors.grey),
                    ),
                  ],
                ),
              ),
            ),

          if (_scanResults.isEmpty && _isScanning)
            const Expanded(
              child: Center(
                child: Column(
                  mainAxisAlignment: MainAxisAlignment.center,
                  children: [
                    Icon(Icons.bluetooth_searching,
                        size: 80, color: Colors.blue),
                    SizedBox(height: 16),
                    Text(
                      '正在搜索设备...',
                      style: TextStyle(fontSize: 18, color: Colors.blue),
                    ),
                  ],
                ),
              ),
            ),

          // 设备列表
          if (_scanResults.isNotEmpty)
            Expanded(
              child: ListView.builder(
                itemCount: _scanResults.length,
                itemBuilder: (context, index) {
                  ScanResult result = _scanResults[index];
                  return _buildDeviceItem(result);
                },
              ),
            ),
        ],
      ),
      floatingActionButton: FloatingActionButton(
        onPressed: _isScanning ? null : _startScan,
        child: Icon(_isScanning ? Icons.bluetooth_searching : Icons.refresh),
      ),
    );
  }

  Widget _buildDeviceItem(ScanResult result) {
    String deviceName =
        result.device.platformName.isNotEmpty
            ? result.device.platformName
            : '未知设备';
    int rssi = result.rssi;

    return Card(
      margin: const EdgeInsets.symmetric(horizontal: 16, vertical: 4),
      child: ListTile(
        leading: const CircleAvatar(
          backgroundColor: Colors.blue,
          child: Icon(Icons.developer_board, color: Colors.white),
        ),
        title: Text(
          deviceName,
          style: const TextStyle(fontWeight: FontWeight.bold),
        ),
        subtitle: Text(
          'ID: ${result.device.remoteId}\n'
          '信号: $rssi dBm',
        ),
        trailing: const Icon(Icons.chevron_right),
        isThreeLine: true,
        onTap: _isConnecting ? null : () => _connectToDevice(result.device),
      ),
    );
  }
}
