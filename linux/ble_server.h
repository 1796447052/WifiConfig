#ifndef BLE_SERVER_H
#define BLE_SERVER_H

#include <gio/gio.h>

/* BLE GATT UUIDs */
#define BLE_SERVICE_UUID      "12345678-1234-1234-1234-123456789abc"
#define BLE_CHAR_SCAN_UUID    "12345678-1234-1234-1234-123456789abd"
#define BLE_CHAR_LIST_UUID    "12345678-1234-1234-1234-123456789abe"
#define BLE_CHAR_CONFIG_UUID  "12345678-1234-1234-1234-123456789abf"
#define BLE_CHAR_STATUS_UUID  "12345678-1234-1234-1234-123456789ac0"

#define BLE_DEVICE_NAME       "OrangePi-Setup"
#define BLE_OBJECT_PATH       "/org/bluez/provision"

/**
 * 初始化 BLE GATT Server 并开始广播
 * 此函数会向 DBus 注册 GATT 服务和特征值
 *
 * @param connection 已建立的 System DBus 连接
 * @return 0 成功, -1 失败
 */
int ble_server_init(GDBusConnection *connection);

/**
 * 启动 BLE LE 广播
 *
 * @param connection 已建立的 System DBus 连接
 * @return 0 成功, -1 失败
 */
int ble_advertising_start(GDBusConnection *connection);

/**
 * 通过 WiFi List Characteristic 发送 Notify 数据
 * 支持自动分包（BLE MTU 限制）
 *
 * @param data  要发送的数据
 * @param len   数据长度
 */
void ble_notify_wifi_list(const char *data, int len);

/**
 * 通过 Status Characteristic 发送状态 Notify
 *
 * @param status 状态码 (0-4)
 */
void ble_notify_status(int status);

/**
 * 通过 Status Characteristic 发送状态 + IP 地址
 * 数据格式: [status_byte][ip_string_bytes]
 *
 * @param status 状态码 (0-4)
 * @param ip     IP 地址字符串
 */
void ble_notify_status_with_ip(int status, const char *ip);

/**
 * 清理 BLE 资源
 */
void ble_server_cleanup(void);

/**
 * 设置主循环引用（必须在 ble_server_init 之前调用）
 * 用于异步注册完成时的错误处理
 *
 * @param loop GLib 主循环
 */
void ble_server_set_main_loop(GMainLoop *loop);

#endif /* BLE_SERVER_H */
