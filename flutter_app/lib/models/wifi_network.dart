/// WiFi 网络信息模型
class WifiNetwork {
  final String ssid;
  final int rssi;
  final String security;

  WifiNetwork({
    required this.ssid,
    required this.rssi,
    required this.security,
  });

  factory WifiNetwork.fromJson(Map<String, dynamic> json) {
    return WifiNetwork(
      ssid: json['ssid'] as String? ?? '',
      rssi: json['rssi'] as int? ?? 0,
      security: json['security'] as String? ?? 'OPEN',
    );
  }

  /// 是否需要密码
  bool get requiresPassword => security != 'OPEN';

  /// 信号强度图标级别 (0-3)
  int get signalLevel {
    if (rssi >= -50) return 3;
    if (rssi >= -70) return 2;
    if (rssi >= -85) return 1;
    return 0;
  }
}

/// 设备连接状态
enum DeviceStatus {
  idle(0, '空闲'),
  scanning(1, '正在扫描WiFi'),
  connecting(2, '正在连接WiFi'),
  connected(3, '连接成功'),
  failed(4, '连接失败');

  const DeviceStatus(this.code, this.label);
  final int code;
  final String label;

  static DeviceStatus fromCode(int code) {
    return DeviceStatus.values.firstWhere(
      (s) => s.code == code,
      orElse: () => DeviceStatus.idle,
    );
  }
}
