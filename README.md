# BLE WiFi 配网系统 (BLE WiFi Provisioning)

通过 BLE 蓝牙为 IoT 设备配置 WiFi 网络。

## 系统架构

```
┌─────────────────┐     BLE GATT      ┌──────────────────┐
│   Flutter App   │ ◄──────────────► │  Orange Pi Zero2  │
│  (Android/iOS)  │                   │  (Debian Linux)   │
└─────────────────┘                   └──────────────────┘
                                              │
                                              │ wpa_supplicant
                                              ▼
                                        ┌──────────┐
                                        │  WiFi AP │
                                        └──────────┘
```

## 配网流程

1. 设备启动，检查网络连接
2. 如果未连接互联网，开启 BLE 广播 (`OrangePi-Setup`)
3. 手机扫描并连接 BLE 设备
4. 手机请求设备扫描周围 WiFi
5. 设备返回 WiFi 列表 (SSID / RSSI / Security)
6. 用户选择 WiFi 并输入密码
7. 手机发送配置到设备
8. 设备通过 wpa_supplicant 连接 WiFi
9. 设备返回连接状态

## BLE GATT 设计

| 特征值 | UUID | 属性 | 说明 |
|--------|------|------|------|
| Scan WiFi | `...9abd` | Write | 手机写入 `scan` 触发扫描 |
| WiFi List | `...9abe` | Notify | 设备返回 WiFi 列表 JSON |
| WiFi Config | `...9abf` | Write | 手机写入 WiFi 配置 JSON |
| Status | `...9ac0` | Notify | 设备返回连接状态 (0-4) |

## 项目结构

```
├── linux/                    # 设备端 (C 语言)
│   ├── main.c               # 程序入口
│   ├── ble_server.h/c       # BLE GATT Server (BlueZ DBus)
│   ├── wifi_scan.h/c        # WiFi 扫描模块
│   ├── wifi_connect.h/c     # WiFi 连接模块
│   ├── Makefile             # 编译脚本
│   └── ble_provision.service # systemd 服务文件
│
└── flutter_app/              # 手机端 (Flutter)
    └── lib/
        ├── main.dart
        ├── models/
        │   └── wifi_network.dart
        ├── services/
        │   └── ble_provision_service.dart
        └── screens/
            ├── device_scan_screen.dart
            ├── wifi_list_screen.dart
            ├── wifi_password_screen.dart
            └── connection_status_screen.dart
```

## Linux 端编译与运行

### 依赖安装

```bash
sudo apt-get update
sudo apt-get install -y \
    libglib2.0-dev \
    libdbus-1-dev \
    bluez \
    wpasupplicant \
    wireless-tools \
    iw
```

### 编译

```bash
cd linux
make
```

### 运行

```bash
sudo ./ble_provision
```

### 安装为系统服务

```bash
sudo make install
sudo systemctl start ble_provision
sudo systemctl status ble_provision
```

### 查看日志

```bash
journalctl -u ble_provision -f
```

## Flutter 端编译与运行

### 依赖安装

```bash
cd flutter_app
flutter pub get
```

### Android 运行

```bash
flutter run -d android
```

### iOS 运行

```bash
cd ios && pod install && cd ..
flutter run -d ios
```

## 安全性说明

- BLE 默认明文传输，适用于局域网配网场景
- 如需加密，可在应用层对 Write/Notify 数据进行 AES 加密
- AES 密钥可通过设备序列号或预置方式管理

## 注意事项

1. Linux 端必须以 root 权限运行 (蓝牙和 WiFi 管理需要)
2. 确保 BlueZ 服务已启动: `sudo systemctl start bluetooth`
3. Android 需要 API 31+ 的蓝牙权限声明
4. iOS 必须在 Info.plist 中声明蓝牙使用说明
5. WiFi 扫描由设备端执行 (因为 iOS 不允许 APP 获取 WiFi 列表)
