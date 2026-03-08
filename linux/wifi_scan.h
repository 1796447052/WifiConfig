#ifndef WIFI_SCAN_H
#define WIFI_SCAN_H

/**
 * WiFi 网络信息结构体
 */
typedef struct {
    char ssid[128];
    int  rssi;
    char security[64];
} wifi_network_t;

/**
 * 扫描周围的 WiFi 网络
 *
 * @param networks  输出数组，调用方分配空间
 * @param max_count 数组最大容量
 * @return          实际扫描到的网络数量，失败返回 -1
 */
int wifi_scan(wifi_network_t *networks, int max_count);

/**
 * 将扫描到的 WiFi 网络列表序列化为 JSON 字符串
 * 调用方需要 free() 返回的指针
 *
 * @param networks 网络数组
 * @param count    数组元素数量
 * @return         JSON 字符串 (堆内存)，失败返回 NULL
 */
char *wifi_scan_to_json(const wifi_network_t *networks, int count);

#endif /* WIFI_SCAN_H */
