import 'dart:async';
import 'dart:convert';
import 'package:flutter/foundation.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import '../models/wifi_network.dart';

/// BLE 配网服务 - 管理与 IoT 设备的 BLE GATT 通信
class BleProvisionService extends ChangeNotifier {
  // ============================================================
  // BLE GATT UUIDs (与设备端保持一致)
  // ============================================================
  static const String serviceUuid = '12345678-1234-1234-1234-123456789abc';
  static const String charScanUuid = '12345678-1234-1234-1234-123456789abd';
  static const String charListUuid = '12345678-1234-1234-1234-123456789abe';
  static const String charConfigUuid = '12345678-1234-1234-1234-123456789abf';
  static const String charStatusUuid = '12345678-1234-1234-1234-123456789ac0';

  // ============================================================
  // 状态
  // ============================================================
  BluetoothDevice? _device;
  BluetoothCharacteristic? _scanChar;
  BluetoothCharacteristic? _listChar;
  BluetoothCharacteristic? _configChar;
  BluetoothCharacteristic? _statusChar;

  bool _isConnected = false;
  bool _isScanning = false;
  DeviceStatus _deviceStatus = DeviceStatus.idle;
  List<WifiNetwork> _wifiNetworks = [];
  String? _connectedIp;

  // 用于收集分包数据
  final List<int> _wifiListBuffer = [];
  Timer? _bufferTimer;

  // 订阅管理
  StreamSubscription? _listSubscription;
  StreamSubscription? _statusSubscription;
  StreamSubscription? _connectionSubscription;

  // ============================================================
  // Getters
  // ============================================================
  bool get isConnected => _isConnected;
  bool get isScanning => _isScanning;
  DeviceStatus get deviceStatus => _deviceStatus;
  List<WifiNetwork> get wifiNetworks => List.unmodifiable(_wifiNetworks);
  BluetoothDevice? get device => _device;
  String? get connectedIp => _connectedIp;

  // ============================================================
  // BLE 设备扫描
  // ============================================================

  /// 扫描 BLE 设备，只返回名称为 OrangePi-Setup 的设备
  Stream<List<ScanResult>> scanForDevices() {
    // 开始扫描
    FlutterBluePlus.startScan(
      timeout: const Duration(seconds: 10),
      withNames: ['OrangePi-Setup'],
    );

    return FlutterBluePlus.scanResults;
  }

  /// 停止 BLE 扫描
  Future<void> stopScan() async {
    await FlutterBluePlus.stopScan();
  }

  // ============================================================
  // BLE 连接
  // ============================================================

  /// 连接到指定的 BLE 设备并发现 GATT 服务
  Future<bool> connectToDevice(BluetoothDevice device) async {
    try {
      _device = device;

      // 连接设备
      await device.connect(
        timeout: const Duration(seconds: 15),
        autoConnect: false,
      );

      // 监听连接状态
      _connectionSubscription = device.connectionState.listen((state) {
        if (state == BluetoothConnectionState.disconnected) {
          _onDisconnected();
        }
      });

      // 请求更大的 MTU (用于传输 WiFi 列表 JSON)
      await device.requestMtu(512);

      // 发现服务
      List<BluetoothService> services = await device.discoverServices();

      // 查找目标 Service
      for (var service in services) {
        if (service.uuid.toString().toLowerCase() == serviceUuid) {
          for (var char in service.characteristics) {
            String uuid = char.uuid.toString().toLowerCase();
            if (uuid == charScanUuid) _scanChar = char;
            if (uuid == charListUuid) _listChar = char;
            if (uuid == charConfigUuid) _configChar = char;
            if (uuid == charStatusUuid) _statusChar = char;
          }
        }
      }

      // 验证所有特征值都已找到
      if (_scanChar == null ||
          _listChar == null ||
          _configChar == null ||
          _statusChar == null) {
        debugPrint('[BLE] Required characteristics not found');
        await disconnect();
        return false;
      }

      // 订阅 Notify 特征值
      await _subscribeNotifications();

      _isConnected = true;
      notifyListeners();
      return true;
    } catch (e) {
      debugPrint('[BLE] Connection error: $e');
      await disconnect();
      return false;
    }
  }

  /// 订阅 WiFi List 和 Status 的 Notify
  Future<void> _subscribeNotifications() async {
    // 订阅 WiFi List Notify
    if (_listChar != null) {
      await _listChar!.setNotifyValue(true);
      _listSubscription = _listChar!.onValueReceived.listen(_onWifiListData);
    }

    // 订阅 Status Notify
    if (_statusChar != null) {
      await _statusChar!.setNotifyValue(true);
      _statusSubscription =
          _statusChar!.onValueReceived.listen(_onStatusData);
    }
  }

  /// 断开 BLE 连接
  Future<void> disconnect() async {
    _listSubscription?.cancel();
    _statusSubscription?.cancel();
    _connectionSubscription?.cancel();
    _bufferTimer?.cancel();

    _listSubscription = null;
    _statusSubscription = null;
    _connectionSubscription = null;

    try {
      await _device?.disconnect();
    } catch (_) {}

    _onDisconnected();
  }

  void _onDisconnected() {
    _device = null;
    _scanChar = null;
    _listChar = null;
    _configChar = null;
    _statusChar = null;
    _isConnected = false;
    _isScanning = false;
    _deviceStatus = DeviceStatus.idle;
    _wifiNetworks = [];
    _connectedIp = null;
    _wifiListBuffer.clear();
    notifyListeners();
  }

  // ============================================================
  // WiFi 扫描
  // ============================================================

  /// 请求设备扫描 WiFi
  Future<void> requestWifiScan() async {
    if (_scanChar == null) return;

    _isScanning = true;
    _wifiNetworks = [];
    _wifiListBuffer.clear();
    notifyListeners();

    try {
      await _scanChar!.write(utf8.encode('scan'), withoutResponse: false);
      debugPrint('[BLE] Sent WiFi scan request');
    } catch (e) {
      debugPrint('[BLE] Failed to send scan request: $e');
      _isScanning = false;
      notifyListeners();
    }
  }

  /// 处理 WiFi List Notify 数据 (支持分包)
  void _onWifiListData(List<int> value) {
    if (value.isEmpty) return;

    debugPrint('[BLE] Received WiFi list data: ${value.length} bytes');

    // 追加到缓冲区
    _wifiListBuffer.addAll(value);

    // 重置定时器 - 等待所有分包到齐
    _bufferTimer?.cancel();
    _bufferTimer = Timer(const Duration(milliseconds: 200), () {
      _parseWifiListBuffer();
    });
  }

  /// 解析完整的 WiFi 列表 JSON
  void _parseWifiListBuffer() {
    try {
      String jsonStr = utf8.decode(_wifiListBuffer);
      debugPrint('[BLE] WiFi list JSON: $jsonStr');

      List<dynamic> jsonList = jsonDecode(jsonStr) as List<dynamic>;
      _wifiNetworks = jsonList
          .map((item) => WifiNetwork.fromJson(item as Map<String, dynamic>))
          .where((n) => n.ssid.isNotEmpty)
          .toList();

      // 按信号强度排序
      _wifiNetworks.sort((a, b) => b.rssi.compareTo(a.rssi));

      _isScanning = false;
      notifyListeners();
    } catch (e) {
      debugPrint('[BLE] Failed to parse WiFi list: $e');
      // 数据可能还未完整，等下一个分包
    } finally {
      _wifiListBuffer.clear();
    }
  }

  // ============================================================
  // WiFi 配置
  // ============================================================

  /// 发送 WiFi 配置到设备
  Future<void> sendWifiConfig({
    required String ssid,
    required String password,
    required String security,
    bool hidden = false,
  }) async {
    if (_configChar == null) return;

    _connectedIp = null;

    Map<String, dynamic> config = {
      'ssid': ssid,
      'password': password,
      'security': security,
      'hidden': hidden,
    };

    String jsonStr = jsonEncode(config);
    debugPrint('[BLE] Sending WiFi config: $jsonStr');

    try {
      await _configChar!.write(
        utf8.encode(jsonStr),
        withoutResponse: false,
      );
    } catch (e) {
      debugPrint('[BLE] Failed to send WiFi config: $e');
      rethrow;
    }
  }

  // ============================================================
  // 状态监听
  // ============================================================

  /// 处理 Status Notify 数据
  /// 数据格式: [status_byte] 或 [status_byte][ip_string_bytes]
  void _onStatusData(List<int> value) {
    if (value.isEmpty) return;

    int statusCode = value[0];
    _deviceStatus = DeviceStatus.fromCode(statusCode);
    debugPrint('[BLE] Device status: ${_deviceStatus.label} ($statusCode)');

    // 如果是连接成功且有额外数据，解析 IP 地址
    if (_deviceStatus == DeviceStatus.connected && value.length > 1) {
      try {
        _connectedIp = utf8.decode(value.sublist(1));
        debugPrint('[BLE] Connected IP: $_connectedIp');
      } catch (e) {
        debugPrint('[BLE] Failed to parse IP: $e');
        _connectedIp = null;
      }
    }

    if (_deviceStatus == DeviceStatus.scanning) {
      _isScanning = true;
    } else if (_deviceStatus != DeviceStatus.scanning) {
      _isScanning = false;
    }

    notifyListeners();
  }

  /// 获取设备状态变化的 Stream
  Stream<DeviceStatus> get statusStream {
    if (_statusChar == null) return const Stream.empty();
    return _statusChar!.onValueReceived.map((value) {
      if (value.isEmpty) return DeviceStatus.idle;
      return DeviceStatus.fromCode(value[0]);
    });
  }

  // ============================================================
  // 生命周期
  // ============================================================
  @override
  void dispose() {
    disconnect();
    super.dispose();
  }
}
