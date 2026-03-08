#ifndef WIFI_CONNECT_H
#define WIFI_CONNECT_H

/* WiFi 连接状态码，与 BLE Status Characteristic 对齐 */
#define WIFI_STATUS_IDLE        0
#define WIFI_STATUS_SCANNING    1
#define WIFI_STATUS_CONNECTING  2
#define WIFI_STATUS_CONNECTED   3
#define WIFI_STATUS_FAILED      4

/**
 * 连接指定的 WiFi 网络
 *
 * @param ssid     网络名称
 * @param password 密码（OPEN 网络传 NULL 或空字符串）
 * @param security 认证方式 ("OPEN","WEP","WPA","WPA2","WPA3")
 * @param hidden   是否为隐藏 SSID (1=隐藏, 0=可见)
 * @param out_ip   输出缓冲区，连接成功后写入获取到的 IP 地址
 * @param ip_size  out_ip 缓冲区大小
 * @return         WIFI_STATUS_CONNECTED 或 WIFI_STATUS_FAILED
 */
int wifi_connect(const char *ssid, const char *password, const char *security,
                 int hidden, char *out_ip, size_t ip_size);

/**
 * 检查当前是否已经连接到互联网
 *
 * @return 1 已连接, 0 未连接
 */
int wifi_is_connected(void);

#endif /* WIFI_CONNECT_H */
