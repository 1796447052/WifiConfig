/**
 * wifi_scan.c - WiFi 扫描模块
 *
 * 使用 iw 命令扫描周围 WiFi 网络，解析 SSID、RSSI、Security 信息。
 * 适用于 Orange Pi Zero2 (Debian Linux)。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "wifi_scan.h"

/* 去除字符串首尾空白 */
static void trim(char *str)
{
    char *start = str;
    char *end;

    while (isspace((unsigned char)*start))
        start++;

    if (*start == '\0') {
        str[0] = '\0';
        return;
    }

    end = start + strlen(start) - 1;
    while (end > start && isspace((unsigned char)*end))
        end--;
    *(end + 1) = '\0';

    if (start != str)
        memmove(str, start, strlen(start) + 1);
}

/**
 * 从 nmcli 的 SECURITY 字段判断认证类型
 * 样例: "WPA2" "WPA1 WPA2" "WPA3" "WPA2 802.1X" "--" (OPEN)
 */
static void parse_security(const char *flags, char *security, size_t len)
{
    if (strstr(flags, "SAE") || strstr(flags, "WPA3")) {
        snprintf(security, len, "WPA3");
    } else if (strstr(flags, "WPA2") || strstr(flags, "RSN")) {
        snprintf(security, len, "WPA2");
    } else if (strstr(flags, "WPA")) {
        snprintf(security, len, "WPA");
    } else if (strstr(flags, "WEP")) {
        snprintf(security, len, "WEP");
    } else {
        snprintf(security, len, "OPEN");
    }
}

int wifi_scan(wifi_network_t *networks, int max_count)
{
    FILE *fp;
    char line[512];
    int count = 0;

    /* NetworkManager manages wpa_supplicant on this system.
     * Using wpa_cli scan directly causes 'SCAN command timed out' because
     * NM controls the supplicant state machine.
     * Use nmcli instead, which goes through NM's D-Bus API. */
    system("nmcli device wifi rescan ifname wlan0 2>/dev/null");
    sleep(4);

    /* nmcli -t -e yes output format (one line per AP):
     *   SSID:SIGNAL:SECURITY
     *   HomeWifi:80:WPA2
     *   Some\:Wifi:65:WPA1 WPA2
     *   OpenNet:45:--
     * -e yes: colons inside field values are escaped as \:
     */
    fp = popen(
        "nmcli --colors no -t -e yes -f SSID,SIGNAL,SECURITY "
        "device wifi list 2>/dev/null",
        "r");
    if (fp == NULL) {
        perror("popen nmcli");
        return -1;
    }

    while (fgets(line, sizeof(line), fp) != NULL && count < max_count) {
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] == '\0') continue;

        /* --- parse SSID field (read until unescaped ':') --- */
        char ssid[256] = {0};
        char *src = line;
        char *dst = ssid;

        while (*src && (dst - ssid) < 254) {
            if (*src == '\\' && *(src + 1) == ':') {
                *dst++ = ':';
                src += 2;
            } else if (*src == ':') {
                src++;          /* skip field separator */
                break;
            } else {
                *dst++ = *src++;
            }
        }
        *dst = '\0';
        trim(ssid);
        if (ssid[0] == '\0') continue;   /* skip hidden / empty SSID */

        /* --- parse SIGNAL field --- */
        char sig_str[16] = {0};
        dst = sig_str;
        while (*src && *src != ':' && (dst - sig_str) < 15)
            *dst++ = *src++;
        *dst = '\0';
        if (*src == ':') src++;
        int signal_pct = atoi(sig_str);   /* 0-100 percentage */

        /* --- parse SECURITY field (rest of line) --- */
        char sec_raw[128] = {0};
        strncpy(sec_raw, src, sizeof(sec_raw) - 1);
        trim(sec_raw);

        wifi_network_t net;
        memset(&net, 0, sizeof(net));
        snprintf(net.ssid, sizeof(net.ssid), "%s", ssid);

        /* Convert nmcli signal percentage (0-100) to approximate dBm.
         * Linear mapping: 0% -> -100 dBm, 100% -> -50 dBm */
        net.rssi = -100 + (signal_pct * 50 / 100);

        /* Map nmcli security string to our canonical labels */
        parse_security(sec_raw, net.security, sizeof(net.security));

        networks[count++] = net;
    }

    pclose(fp);

    /* 去重：相同 SSID 只保留信号最强的 */
    for (int i = 0; i < count; i++) {
        for (int j = i + 1; j < count; j++) {
            if (strcmp(networks[i].ssid, networks[j].ssid) == 0) {
                if (networks[j].rssi > networks[i].rssi) {
                    networks[i] = networks[j];
                }
                /* 移除重复项 */
                memmove(&networks[j], &networks[j + 1],
                        (count - j - 1) * sizeof(wifi_network_t));
                count--;
                j--;
            }
        }
    }

    return count;
}

char *wifi_scan_to_json(const wifi_network_t *networks, int count)
{
    /* 预分配足够大的缓冲区 */
    size_t buf_size = (size_t)count * 256 + 64;
    char *buf = malloc(buf_size);
    if (!buf)
        return NULL;

    strcpy(buf, "[");

    for (int i = 0; i < count; i++) {
        char item[256];
        /* 对 SSID 进行 JSON 转义（处理双引号和反斜杠） */
        char escaped_ssid[256];
        const char *src = networks[i].ssid;
        char *dst = escaped_ssid;
        while (*src && (dst - escaped_ssid) < 250) {
            if (*src == '"' || *src == '\\') {
                *dst++ = '\\';
            }
            *dst++ = *src++;
        }
        *dst = '\0';

        snprintf(item, sizeof(item),
                 "%s{\"ssid\":\"%s\",\"rssi\":%d,\"security\":\"%s\"}",
                 (i > 0) ? "," : "",
                 escaped_ssid, networks[i].rssi, networks[i].security);

        if (strlen(buf) + strlen(item) < buf_size - 2) {
            strcat(buf, item);
        }
    }

    strcat(buf, "]");
    return buf;
}
