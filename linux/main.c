/**
 * main.c - BLE WiFi 配网服务入口
 *
 * 程序启动流程:
 * 1. 检查设备是否已连接互联网
 * 2. 如果未连接，启动 BLE GATT Server 广播
 * 3. 等待手机连接并配置 WiFi
 * 4. 配网成功后可选择关闭 BLE
 *
 * 编译: make
 * 运行: sudo ./ble_provision
 */

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <gio/gio.h>

#include "ble_server.h"
#include "wifi_connect.h"

/* 开机后允许配网的最大秒数 */
#define BOOT_WINDOW_SECONDS  300

static GMainLoop *main_loop = NULL;

/**
 * 读取系统开机时间（秒）
 * @return 开机秒数，失败返回 -1
 */
static double get_uptime(void)
{
    FILE *fp = fopen("/proc/uptime", "r");
    if (!fp) return -1;
    double uptime = 0;
    if (fscanf(fp, "%lf", &uptime) != 1)
        uptime = -1;
    fclose(fp);
    return uptime;
}

/* 是否以 --force 强制启动（跳过开机时间检查） */
static int g_force_mode = 0;

/**
 * 定时回调：检查是否已超过开机 5 分钟窗口
 * --force 模式下不检查
 */
static gboolean check_boot_window(gpointer user_data)
{
    (void)user_data;
    if (g_force_mode) return G_SOURCE_CONTINUE;

    double uptime = get_uptime();
    if (uptime < 0) return G_SOURCE_CONTINUE;

    if (uptime >= BOOT_WINDOW_SECONDS) {
        printf("[Main] Boot window (%d seconds) exceeded (uptime=%.0f s). "
               "Shutting down BLE provisioning.\n",
               BOOT_WINDOW_SECONDS, uptime);
        ble_server_cleanup();
        if (main_loop)
            g_main_loop_quit(main_loop);
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

/**
 * 信号处理 - 优雅退出
 */
static void signal_handler(int signum)
{
    printf("\n[Main] Received signal %d, shutting down...\n", signum);
    ble_server_cleanup();
    if (main_loop)
        g_main_loop_quit(main_loop);
}

/**
 * 定时检查网络连接状态
 * 如果配网成功（已连接互联网），可以选择停止 BLE 广播
 */
static gboolean check_network_status(gpointer user_data)
{
    (void)user_data;

    if (wifi_is_connected()) {
        printf("[Main] Device is now connected to the internet.\n");
        printf("[Main] BLE provisioning service remains active.\n");
        /* 如果需要配网成功后自动关闭 BLE，取消下面注释: */
        /* ble_server_cleanup(); */
        /* g_main_loop_quit(main_loop); */
        /* return G_SOURCE_REMOVE; */
    }

    return G_SOURCE_CONTINUE; /* 继续定时检查 */
}

int main(int argc, char *argv[])
{
    /* 解析命令行参数 */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--force") == 0) {
            g_force_mode = 1;
        }
    }

    printf("==========================================\n");
    printf("  BLE WiFi Provisioning Service\n");
    printf("  Device: Orange Pi Zero2\n");
    printf("  BLE Name: %s\n", BLE_DEVICE_NAME);
    if (g_force_mode)
        printf("  Mode: FORCE (boot window disabled)\n");
    printf("==========================================\n\n");

    /* 检查开机时间是否已超过配网窗口（--force 时跳过） */
    double uptime = get_uptime();
    if (uptime >= 0) {
        printf("[Main] System uptime: %.0f seconds\n", uptime);
        if (!g_force_mode && uptime >= BOOT_WINDOW_SECONDS) {
            printf("[Main] Boot window (%d s) already passed. "
                   "Provisioning not allowed.\n"
                   "[Main] Use --force to override.\n",
                   BOOT_WINDOW_SECONDS);
            return EXIT_SUCCESS;
        }
        if (!g_force_mode)
            printf("[Main] Boot window remaining: %.0f seconds\n",
                   BOOT_WINDOW_SECONDS - uptime);
    }

    /* 注册信号处理 */
    signal(SIGINT,  signal_handler);
    signal(SIGTERM, signal_handler);

    /* 检查是否以 root 权限运行 */
    if (getuid() != 0) {
        fprintf(stderr, "[Main] Warning: This program should run as root "
                        "for Bluetooth and WiFi management.\n");
    }

    /* 1. 检查网络连接状态 */
    printf("[Main] Checking internet connectivity...\n");
    if (wifi_is_connected()) {
        printf("[Main] Device already connected to the internet.\n");
        printf("[Main] Starting BLE provisioning anyway "
               "(allows re-configuration).\n\n");
    } else {
        printf("[Main] No internet connection detected.\n");
        printf("[Main] Starting BLE provisioning service...\n\n");
    }

    /* 2. 连接 System D-Bus */
    GError *error = NULL;
    GDBusConnection *connection = g_bus_get_sync(G_BUS_TYPE_SYSTEM,
                                                  NULL, &error);
    if (!connection) {
        fprintf(stderr, "[Main] Failed to connect to D-Bus: %s\n",
                error->message);
        g_error_free(error);
        return EXIT_FAILURE;
    }
    printf("[Main] Connected to System D-Bus\n");

    /* 3. 提前创建主循环 — BLE 异步注册回调需要它
     *    必须在 ble_server_init 之前建立，否则注册失败时无法 quit */
    main_loop = g_main_loop_new(NULL, FALSE);
    ble_server_set_main_loop(main_loop);

    /* 4. 初始化 BLE GATT Server（注册 D-Bus 对象 + 发出异步 RegisterApplication）*/
    if (ble_server_init(connection) != 0) {
        fprintf(stderr, "[Main] Failed to initialize BLE server\n");
        g_main_loop_unref(main_loop);
        g_object_unref(connection);
        return EXIT_FAILURE;
    }

    /* 5. 启动 BLE 广播（注册 Advertisement 对象 + 发出异步 RegisterAdvertisement）*/
    if (ble_advertising_start(connection) != 0) {
        fprintf(stderr, "[Main] Failed to start BLE advertising\n");
        ble_server_cleanup();
        g_main_loop_unref(main_loop);
        g_object_unref(connection);
        return EXIT_FAILURE;
    }

    printf("\n[Main] BLE provisioning service is running.\n");
    printf("[Main] Waiting for mobile device connection...\n");
    printf("[Main] (BLE registration completes once main loop starts)\n\n");

    /* 6. 每 30 秒检查一次网络状态 */
    g_timeout_add_seconds(30, check_network_status, NULL);

    /* 7. 每 10 秒检查开机窗口，超过 5 分钟自动退出 */
    g_timeout_add_seconds(10, check_boot_window, NULL);

    /* 8. 运行主循环 — 在此处 BlueZ 与我们完成 GATT/Advertisement 握手 */
    g_main_loop_run(main_loop);

    /* 9. 清理 */
    printf("[Main] Cleaning up...\n");
    ble_server_cleanup();
    g_main_loop_unref(main_loop);
    g_object_unref(connection);

    printf("[Main] Service stopped.\n");
    return EXIT_SUCCESS;
}
