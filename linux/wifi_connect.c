/**
 * wifi_connect.c - WiFi connection module
 *
 * Uses wpa_cli network management commands to dynamically add/switch networks.
 * No config file modification or wpa_supplicant restart needed.
 *
 * Flow:
 *   wpa_cli add_network -> set_network (ssid/psk/key_mgmt/scan_ssid) ->
 *   select_network -> poll wpa_state=COMPLETED -> dhclient -> get IP
 *
 * Compatible with any running wpa_supplicant (systemd / NetworkManager / manual).
 * For Orange Pi Zero2 (Debian Linux).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "wifi_connect.h"

#define MAX_RETRY   20
#define RETRY_DELAY  2  /* seconds */

/* ================================================================
 * wpa_cli helpers
 * ================================================================ */

/**
 * Run wpa_cli -i wlan0 <args>, write first line of output to out.
 * @return 0 on success, -1 on popen failure
 */
static int wpa_cli_run(const char *args, char *out, size_t out_size)
{
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "wpa_cli -i wlan0 %s 2>/dev/null", args);

    FILE *fp = popen(cmd, "r");
    if (!fp)
        return -1;

    if (out && out_size > 0) {
        out[0] = '\0';
        fgets(out, (int)out_size, fp);
        out[strcspn(out, "\r\n")] = '\0';
    }
    pclose(fp);
    return 0;
}

/**
 * Run wpa_cli command and verify output is "OK".
 * @return 0 on success, -1 on failure
 */
static int wpa_cli_ok(const char *args)
{
    char out[64];
    if (wpa_cli_run(args, out, sizeof(out)) != 0)
        return -1;

    if (strcmp(out, "OK") != 0) {
        fprintf(stderr, "[WiFi] wpa_cli %s => '%s'\n", args, out);
        return -1;
    }
    return 0;
}

/* ================================================================
 * Connection status check
 * ================================================================ */

/**
 * Check if wlan0 is connected to the specified SSID.
 * @return 1 if connected, 0 otherwise
 */
static int check_wpa_connected(const char *ssid)
{
    FILE *fp = popen("wpa_cli -i wlan0 status 2>/dev/null", "r");
    if (!fp)
        return 0;

    char line[256];
    int state_ok = 0, ssid_ok = 0;

    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (strcmp(line, "wpa_state=COMPLETED") == 0)
            state_ok = 1;
        if (strncmp(line, "ssid=", 5) == 0 &&
            strcmp(line + 5, ssid) == 0)
            ssid_ok = 1;
    }
    pclose(fp);
    return (state_ok && ssid_ok) ? 1 : 0;
}

/**
 * Get the IPv4 address of wlan0.
 * Uses awk instead of grep -oP for portability.
 * Retries up to 10 times (1s apart) to wait for DHCP assignment.
 * @return 0 on success (IP written to out_ip), -1 on failure
 */
static int get_wlan_ip(char *out_ip, size_t ip_size)
{
    for (int i = 0; i < 10; i++) {
        FILE *fp = popen(
            "ip -4 addr show wlan0 2>/dev/null | "
            "awk '/inet /{gsub(/\\/[0-9]+$/, \"\", $2); print $2; exit}'",
            "r");
        if (!fp)
            return -1;

        out_ip[0] = '\0';
        fgets(out_ip, (int)ip_size, fp);
        out_ip[strcspn(out_ip, "\r\n")] = '\0';
        pclose(fp);

        if (out_ip[0] != '\0') {
            printf("[WiFi] Got IP on attempt %d: %s\n", i + 1, out_ip);
            return 0;
        }

        printf("[WiFi] Waiting for DHCP IP... (%d/10)\n", i + 1);
        sleep(1);
    }
    return -1;
}

/* ================================================================
 * WiFi connect
 * ================================================================ */

int wifi_connect(const char *ssid, const char *password, const char *security,
                 int hidden, char *out_ip, size_t ip_size)
{
    printf("[WiFi] Connecting to '%s' (security: %s, hidden: %d)\n",
           ssid, security, hidden);

    char cmd[512];
    char out[64];

    if (out_ip && ip_size > 0)
        out_ip[0] = '\0';

    /* 1. Remove previously added temporary networks (id 0~9) */
    for (int i = 0; i < 10; i++) {
        snprintf(cmd, sizeof(cmd), "remove_network %d", i);
        wpa_cli_run(cmd, out, sizeof(out));
    }

    /* 2. Add new network, get assigned network id */
    if (wpa_cli_run("add_network", out, sizeof(out)) != 0 ||
        out[0] == '\0' || out[0] < '0' || out[0] > '9') {
        fprintf(stderr, "[WiFi] add_network failed: '%s'\n", out);
        return WIFI_STATUS_FAILED;
    }
    int net_id = atoi(out);
    printf("[WiFi] Network id: %d\n", net_id);

    /* 3. Set SSID (wpa_cli requires SSID in double quotes) */
    snprintf(cmd, sizeof(cmd), "set_network %d ssid '\"%.100s\"'", net_id, ssid);
    if (wpa_cli_ok(cmd) != 0)
        return WIFI_STATUS_FAILED;

    /* 4. For hidden SSIDs, enable scan_ssid=1 so wpa_supplicant
     *    sends directed probe requests */
    if (hidden) {
        snprintf(cmd, sizeof(cmd), "set_network %d scan_ssid 1", net_id);
        if (wpa_cli_ok(cmd) != 0)
            return WIFI_STATUS_FAILED;
        printf("[WiFi] Hidden SSID mode enabled (scan_ssid=1)\n");
    }

    /* 5. Set authentication parameters based on security type */
    if (strcmp(security, "OPEN") == 0) {
        snprintf(cmd, sizeof(cmd), "set_network %d key_mgmt NONE", net_id);
        if (wpa_cli_ok(cmd) != 0)
            return WIFI_STATUS_FAILED;

    } else if (strcmp(security, "WEP") == 0) {
        snprintf(cmd, sizeof(cmd), "set_network %d key_mgmt NONE", net_id);
        wpa_cli_ok(cmd);
        snprintf(cmd, sizeof(cmd),
                 "set_network %d wep_key0 '\"%.64s\"'", net_id, password);
        if (wpa_cli_ok(cmd) != 0)
            return WIFI_STATUS_FAILED;
        snprintf(cmd, sizeof(cmd), "set_network %d wep_tx_keyidx 0", net_id);
        wpa_cli_ok(cmd);

    } else if (strcmp(security, "WPA3") == 0) {
        /* WPA3 routers usually also support WPA2-PSK (mixed mode).
         * Many embedded WiFi chips don't support SAE, so use WPA-PSK
         * as the default strategy for compatibility. */
        printf("[WiFi] WPA3 detected, using WPA-PSK (compatible with mixed mode)\n");
        snprintf(cmd, sizeof(cmd), "set_network %d key_mgmt WPA-PSK", net_id);
        if (wpa_cli_ok(cmd) != 0)
            return WIFI_STATUS_FAILED;
        snprintf(cmd, sizeof(cmd),
                 "set_network %d psk '\"%.64s\"'", net_id, password);
        if (wpa_cli_ok(cmd) != 0)
            return WIFI_STATUS_FAILED;
        /* Optional: allow MFP for WPA3 transition mode */
        snprintf(cmd, sizeof(cmd), "set_network %d ieee80211w 1", net_id);
        wpa_cli_ok(cmd);

    } else {
        /* WPA / WPA2-PSK (default) */
        snprintf(cmd, sizeof(cmd), "set_network %d key_mgmt WPA-PSK", net_id);
        if (wpa_cli_ok(cmd) != 0)
            return WIFI_STATUS_FAILED;
        snprintf(cmd, sizeof(cmd),
                 "set_network %d psk '\"%.64s\"'", net_id, password);
        if (wpa_cli_ok(cmd) != 0)
            return WIFI_STATUS_FAILED;
    }

    /* 6. select_network: enable this network and disconnect all others */
    snprintf(cmd, sizeof(cmd), "select_network %d", net_id);
    if (wpa_cli_ok(cmd) != 0)
        return WIFI_STATUS_FAILED;

    /* 7. Poll until connected to target SSID */
    for (int i = 0; i < MAX_RETRY; i++) {
        sleep(RETRY_DELAY);
        if (check_wpa_connected(ssid)) {
            printf("[WiFi] Connected to '%s'\n", ssid);

            /* 8. Trigger DHCP renewal - try common clients */
            if (system("which dhclient >/dev/null 2>&1") == 0) {
                system("dhclient wlan0 2>/dev/null");
            } else if (system("which dhcpcd >/dev/null 2>&1") == 0) {
                system("dhcpcd wlan0 2>/dev/null &");
                sleep(2);
            }
            /* If NetworkManager is running, it handles DHCP automatically */

            /* 9. Retrieve assigned IP */
            if (out_ip && ip_size > 0) {
                if (get_wlan_ip(out_ip, ip_size) == 0) {
                    printf("[WiFi] IP address: %s\n", out_ip);
                } else {
                    snprintf(out_ip, ip_size, "unknown");
                }
            }

            /* 10. Save config so it persists across reboots */
            wpa_cli_ok("save_config");

            return WIFI_STATUS_CONNECTED;
        }
        printf("[WiFi] Waiting for connection... (%d/%d)\n", i + 1, MAX_RETRY);
    }

    fprintf(stderr, "[WiFi] Connection to '%s' timed out\n", ssid);

    /* Print diagnostic info */
    fprintf(stderr, "[WiFi] --- Diagnostic info ---\n");
    FILE *diag = popen("wpa_cli -i wlan0 status 2>/dev/null", "r");
    if (diag) {
        char line[256];
        while (fgets(line, sizeof(line), diag))
            fprintf(stderr, "  %s", line);
        pclose(diag);
    }
    fprintf(stderr, "[WiFi] --- End diagnostic ---\n");

    return WIFI_STATUS_FAILED;
}

int wifi_is_connected(void)
{
    int ret = system("ping -c 1 -W 2 8.8.8.8 > /dev/null 2>&1");
    return (ret == 0) ? 1 : 0;
}
