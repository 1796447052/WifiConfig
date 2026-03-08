/**
 * ble_server.c - BLE GATT Server 实现
 *
 * 基于 BlueZ D-Bus API 实现完整的 GATT Server。
 * 使用 GDBus (GLib/GIO) 与 BlueZ 通信。
 *
 * 实现的 D-Bus 接口:
 *   org.bluez.GattService1
 *   org.bluez.GattCharacteristic1
 *   org.bluez.LEAdvertisement1
 *   org.freedesktop.DBus.ObjectManager
 *
 * 适用于 Orange Pi Zero2 (Debian Linux + BlueZ 5.x)。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <gio/gio.h>

#include "ble_server.h"
#include "wifi_scan.h"
#include "wifi_connect.h"

/* ================================================================
 * 内部数据结构
 * ================================================================ */

#define APP_OBJECT_PATH       "/org/bluez/provision"
#define SERVICE_PATH          APP_OBJECT_PATH "/service0"
#define CHAR_SCAN_PATH        SERVICE_PATH "/char0"
#define CHAR_LIST_PATH        SERVICE_PATH "/char1"
#define CHAR_CONFIG_PATH      SERVICE_PATH "/char2"
#define CHAR_STATUS_PATH      SERVICE_PATH "/char3"

#define ADV_PATH              "/org/bluez/provision/advertisement0"

/* BLE 数据分包大小 (典型 MTU 为 20 字节, 协商后可达 512) */
#define BLE_CHUNK_SIZE        512

/* 全局 DBus 连接 */
static GDBusConnection *g_conn = NULL;

/* 主循环引用（用于异步注册失败时退出）
 * 注意：不能命名为 g_main_loop_ref，与 GLib 内置函数同名会引发编译错误 */
static GMainLoop *g_app_main_loop = NULL;

void ble_server_set_main_loop(GMainLoop *loop)
{
    g_app_main_loop = loop;
}

/* 前向声明：异步回调定义在文件后半部分，但 ble_server_init 需要引用它们 */
static void on_gatt_app_registered(GObject *source, GAsyncResult *res, gpointer user_data);
static void on_adv_registered(GObject *source, GAsyncResult *res, gpointer user_data);

/* WiFi 列表 Notify 相关 */
static gboolean wifi_list_notifying = FALSE;
static char    *pending_wifi_data   = NULL;
static int      pending_wifi_len    = 0;

/* Status Notify 相关 */
static gboolean status_notifying = FALSE;
static int      current_status   = WIFI_STATUS_IDLE;

/* 已注册的 object ID 列表 (用于清理) */
static guint registered_ids[32];
static int  registered_count = 0;

/* ================================================================
 * 辅助函数
 * ================================================================ */

static GVariant *build_byte_array(const char *data, int len)
{
    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE("ay"));
    for (int i = 0; i < len; i++) {
        g_variant_builder_add(&builder, "y", (guchar)data[i]);
    }
    return g_variant_builder_end(&builder);
}

/* ================================================================
 * WiFi 扫描线程
 * ================================================================ */

static void *wifi_scan_thread(void *arg)
{
    (void)arg;

    printf("[BLE] WiFi scan started\n");
    ble_notify_status(WIFI_STATUS_SCANNING);

    wifi_network_t networks[64];
    int count = wifi_scan(networks, 64);

    if (count < 0) {
        printf("[BLE] WiFi scan failed\n");
        ble_notify_status(WIFI_STATUS_IDLE);
        return NULL;
    }

    printf("[BLE] Found %d WiFi networks\n", count);

    char *json = wifi_scan_to_json(networks, count);
    if (json) {
        ble_notify_wifi_list(json, (int)strlen(json));
        free(json);
    }

    ble_notify_status(WIFI_STATUS_IDLE);
    return NULL;
}

/* ================================================================
 * WiFi 连接线程
 * ================================================================ */

typedef struct {
    char ssid[128];
    char password[128];
    char security[64];
    int  hidden;
} wifi_config_t;

static void *wifi_connect_thread(void *arg)
{
    wifi_config_t *cfg = (wifi_config_t *)arg;

    printf("[BLE] Connecting to WiFi: %s (hidden=%d)\n", cfg->ssid, cfg->hidden);
    ble_notify_status(WIFI_STATUS_CONNECTING);

    char ip_buf[64] = {0};
    int result = wifi_connect(cfg->ssid, cfg->password, cfg->security,
                              cfg->hidden, ip_buf, sizeof(ip_buf));

    if (result == WIFI_STATUS_CONNECTED && ip_buf[0] != '\0') {
        ble_notify_status_with_ip(result, ip_buf);
    } else {
        ble_notify_status(result);
    }

    free(cfg);
    return NULL;
}

/* ================================================================
 * JSON 解析 (轻量级，不依赖 cJSON)
 * ================================================================ */

static int parse_json_string(const char *json, const char *key,
                             char *out, size_t out_size)
{
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\":\"", key);
    const char *p = strstr(json, pattern);
    if (!p)
        return -1;
    p += strlen(pattern);
    const char *end = strchr(p, '"');
    if (!end)
        return -1;
    size_t len = (size_t)(end - p);
    if (len >= out_size)
        len = out_size - 1;
    memcpy(out, p, len);
    out[len] = '\0';
    return 0;
}

static int parse_json_bool(const char *json, const char *key, int default_val)
{
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    const char *p = strstr(json, pattern);
    if (!p)
        return default_val;
    p += strlen(pattern);
    while (*p == ' ') p++;
    if (strncmp(p, "true", 4) == 0)
        return 1;
    if (strncmp(p, "false", 5) == 0)
        return 0;
    return default_val;
}

/* ================================================================
 * GattCharacteristic1 接口实现 - Scan WiFi (Write)
 * ================================================================ */

static void handle_scan_write(GDBusConnection *conn,
                              const gchar *sender,
                              const gchar *object_path,
                              const gchar *interface_name,
                              const gchar *method_name,
                              GVariant *parameters,
                              GDBusMethodInvocation *invocation,
                              gpointer user_data)
{
    (void)conn; (void)sender; (void)object_path;
    (void)interface_name; (void)user_data;

    if (g_strcmp0(method_name, "WriteValue") == 0) {
        GVariant *value_variant;
        GVariant *options;
        g_variant_get(parameters, "(@ay@a{sv})", &value_variant, &options);

        gsize n_elements;
        const guchar *data = g_variant_get_fixed_array(value_variant,
                                                        &n_elements,
                                                        sizeof(guchar));
        char buf[256] = {0};
        if (n_elements > 0 && n_elements < sizeof(buf)) {
            memcpy(buf, data, n_elements);
        }

        printf("[BLE] Scan Char WriteValue: '%s'\n", buf);

        if (strncmp(buf, "scan", 4) == 0) {
            pthread_t tid;
            pthread_create(&tid, NULL, wifi_scan_thread, NULL);
            pthread_detach(tid);
        }

        g_variant_unref(value_variant);
        g_variant_unref(options);
        g_dbus_method_invocation_return_value(invocation, NULL);
    } else if (g_strcmp0(method_name, "ReadValue") == 0) {
        g_dbus_method_invocation_return_value(
            invocation,
            g_variant_new("(ay)", build_byte_array("", 0)));
    } else {
        g_dbus_method_invocation_return_dbus_error(invocation,
            "org.bluez.Error.NotSupported", "Method not supported");
    }
}

/* ================================================================
 * GattCharacteristic1 接口实现 - WiFi List (Notify)
 * ================================================================ */

static void handle_list_method(GDBusConnection *conn,
                               const gchar *sender,
                               const gchar *object_path,
                               const gchar *interface_name,
                               const gchar *method_name,
                               GVariant *parameters,
                               GDBusMethodInvocation *invocation,
                               gpointer user_data)
{
    (void)conn; (void)sender; (void)object_path;
    (void)interface_name; (void)parameters; (void)user_data;

    if (g_strcmp0(method_name, "StartNotify") == 0) {
        wifi_list_notifying = TRUE;
        printf("[BLE] WiFi List: StartNotify\n");
        g_dbus_method_invocation_return_value(invocation, NULL);
    } else if (g_strcmp0(method_name, "StopNotify") == 0) {
        wifi_list_notifying = FALSE;
        printf("[BLE] WiFi List: StopNotify\n");
        g_dbus_method_invocation_return_value(invocation, NULL);
    } else if (g_strcmp0(method_name, "ReadValue") == 0) {
        g_dbus_method_invocation_return_value(
            invocation,
            g_variant_new("(ay)", build_byte_array("", 0)));
    } else {
        g_dbus_method_invocation_return_dbus_error(invocation,
            "org.bluez.Error.NotSupported", "Method not supported");
    }
}

/* ================================================================
 * GattCharacteristic1 接口实现 - WiFi Config (Write)
 * ================================================================ */

static void handle_config_write(GDBusConnection *conn,
                                const gchar *sender,
                                const gchar *object_path,
                                const gchar *interface_name,
                                const gchar *method_name,
                                GVariant *parameters,
                                GDBusMethodInvocation *invocation,
                                gpointer user_data)
{
    (void)conn; (void)sender; (void)object_path;
    (void)interface_name; (void)user_data;

    if (g_strcmp0(method_name, "WriteValue") == 0) {
        GVariant *value_variant;
        GVariant *options;
        g_variant_get(parameters, "(@ay@a{sv})", &value_variant, &options);

        gsize n_elements;
        const guchar *data = g_variant_get_fixed_array(value_variant,
                                                        &n_elements,
                                                        sizeof(guchar));
        char buf[1024] = {0};
        if (n_elements > 0 && n_elements < sizeof(buf)) {
            memcpy(buf, data, n_elements);
        }

        printf("[BLE] Config WriteValue: '%s'\n", buf);

        /* 解析 JSON: {"ssid":"xxx","password":"xxx","security":"xxx"} */
        wifi_config_t *cfg = calloc(1, sizeof(wifi_config_t));
        if (cfg) {
            parse_json_string(buf, "ssid", cfg->ssid, sizeof(cfg->ssid));
            parse_json_string(buf, "password", cfg->password, sizeof(cfg->password));
            parse_json_string(buf, "security", cfg->security, sizeof(cfg->security));
            cfg->hidden = parse_json_bool(buf, "hidden", 0);

            if (cfg->ssid[0] != '\0') {
                pthread_t tid;
                pthread_create(&tid, NULL, wifi_connect_thread, cfg);
                pthread_detach(tid);
            } else {
                free(cfg);
                fprintf(stderr, "[BLE] Invalid WiFi config: empty SSID\n");
            }
        }

        g_variant_unref(value_variant);
        g_variant_unref(options);
        g_dbus_method_invocation_return_value(invocation, NULL);
    } else {
        g_dbus_method_invocation_return_dbus_error(invocation,
            "org.bluez.Error.NotSupported", "Method not supported");
    }
}

/* ================================================================
 * GattCharacteristic1 接口实现 - Status (Notify)
 * ================================================================ */

static void handle_status_method(GDBusConnection *conn,
                                 const gchar *sender,
                                 const gchar *object_path,
                                 const gchar *interface_name,
                                 const gchar *method_name,
                                 GVariant *parameters,
                                 GDBusMethodInvocation *invocation,
                                 gpointer user_data)
{
    (void)conn; (void)sender; (void)object_path;
    (void)interface_name; (void)parameters; (void)user_data;

    if (g_strcmp0(method_name, "StartNotify") == 0) {
        status_notifying = TRUE;
        printf("[BLE] Status: StartNotify\n");
        g_dbus_method_invocation_return_value(invocation, NULL);
    } else if (g_strcmp0(method_name, "StopNotify") == 0) {
        status_notifying = FALSE;
        printf("[BLE] Status: StopNotify\n");
        g_dbus_method_invocation_return_value(invocation, NULL);
    } else if (g_strcmp0(method_name, "ReadValue") == 0) {
        char status_byte = (char)current_status;
        g_dbus_method_invocation_return_value(
            invocation,
            g_variant_new("(ay)", build_byte_array(&status_byte, 1)));
    } else {
        g_dbus_method_invocation_return_dbus_error(invocation,
            "org.bluez.Error.NotSupported", "Method not supported");
    }
}

/* ================================================================
 * GetAll / Get 属性处理
 * ================================================================ */

typedef struct {
    const char *uuid;
    const char *path;
    const char *service_path;
    const char **flags;
    int         flag_count;
} char_info_t;

static const char *flags_write[]  = {"write"};
static const char *flags_notify[] = {"notify", "read"};

static GVariant *char_get_property(GDBusConnection *connection,
                                   const gchar *sender,
                                   const gchar *object_path,
                                   const gchar *interface_name,
                                   const gchar *property_name,
                                   GError **error,
                                   gpointer user_data)
{
    (void)connection; (void)sender; (void)interface_name; (void)error;
    char_info_t *info = (char_info_t *)user_data;
    (void)object_path;

    if (g_strcmp0(property_name, "UUID") == 0) {
        return g_variant_new_string(info->uuid);
    }
    if (g_strcmp0(property_name, "Service") == 0) {
        return g_variant_new_object_path(info->service_path);
    }
    if (g_strcmp0(property_name, "Flags") == 0) {
        GVariantBuilder builder;
        g_variant_builder_init(&builder, G_VARIANT_TYPE("as"));
        for (int i = 0; i < info->flag_count; i++) {
            g_variant_builder_add(&builder, "s", info->flags[i]);
        }
        return g_variant_builder_end(&builder);
    }
    if (g_strcmp0(property_name, "Notifying") == 0) {
        if (g_strcmp0(info->uuid, BLE_CHAR_LIST_UUID) == 0)
            return g_variant_new_boolean(wifi_list_notifying);
        if (g_strcmp0(info->uuid, BLE_CHAR_STATUS_UUID) == 0)
            return g_variant_new_boolean(status_notifying);
        return g_variant_new_boolean(FALSE);
    }
    if (g_strcmp0(property_name, "Value") == 0) {
        return build_byte_array("", 0);
    }
    return NULL;
}

/* 服务属性 */
static GVariant *service_get_property(GDBusConnection *connection,
                                      const gchar *sender,
                                      const gchar *object_path,
                                      const gchar *interface_name,
                                      const gchar *property_name,
                                      GError **error,
                                      gpointer user_data)
{
    (void)connection; (void)sender; (void)object_path;
    (void)interface_name; (void)error; (void)user_data;

    if (g_strcmp0(property_name, "UUID") == 0)
        return g_variant_new_string(BLE_SERVICE_UUID);
    if (g_strcmp0(property_name, "Primary") == 0)
        return g_variant_new_boolean(TRUE);
    return NULL;
}

/* ================================================================
 * D-Bus Introspection XML 定义
 * ================================================================ */

static const gchar service_introspection_xml[] =
    "<node>"
    "  <interface name='org.bluez.GattService1'>"
    "    <property name='UUID' type='s' access='read'/>"
    "    <property name='Primary' type='b' access='read'/>"
    "  </interface>"
    "</node>";

static const gchar char_write_introspection_xml[] =
    "<node>"
    "  <interface name='org.bluez.GattCharacteristic1'>"
    "    <method name='ReadValue'>"
    "      <arg name='options' type='a{sv}' direction='in'/>"
    "      <arg name='value' type='ay' direction='out'/>"
    "    </method>"
    "    <method name='WriteValue'>"
    "      <arg name='value' type='ay' direction='in'/>"
    "      <arg name='options' type='a{sv}' direction='in'/>"
    "    </method>"
    "    <property name='UUID' type='s' access='read'/>"
    "    <property name='Service' type='o' access='read'/>"
    "    <property name='Flags' type='as' access='read'/>"
    "    <property name='Value' type='ay' access='read'/>"
    "  </interface>"
    "</node>";

static const gchar char_notify_introspection_xml[] =
    "<node>"
    "  <interface name='org.bluez.GattCharacteristic1'>"
    "    <method name='ReadValue'>"
    "      <arg name='options' type='a{sv}' direction='in'/>"
    "      <arg name='value' type='ay' direction='out'/>"
    "    </method>"
    "    <method name='StartNotify'/>"
    "    <method name='StopNotify'/>"
    "    <property name='UUID' type='s' access='read'/>"
    "    <property name='Service' type='o' access='read'/>"
    "    <property name='Flags' type='as' access='read'/>"
    "    <property name='Notifying' type='b' access='read'/>"
    "    <property name='Value' type='ay' access='read'/>"
    "  </interface>"
    "</node>";

/* ObjectManager introspection */
static const gchar om_introspection_xml[] =
    "<node>"
    "  <interface name='org.freedesktop.DBus.ObjectManager'>"
    "    <method name='GetManagedObjects'>"
    "      <arg name='objects' type='a{oa{sa{sv}}}' direction='out'/>"
    "    </method>"
    "  </interface>"
    "</node>";

/* Advertisement introspection */
static const gchar adv_introspection_xml[] =
    "<node>"
    "  <interface name='org.bluez.LEAdvertisement1'>"
    "    <method name='Release'/>"
    "    <property name='Type' type='s' access='read'/>"
    "    <property name='ServiceUUIDs' type='as' access='read'/>"
    "    <property name='LocalName' type='s' access='read'/>"
    "    <property name='Includes' type='as' access='read'/>"
    "  </interface>"
    "</node>";

/* ================================================================
 * Characteristic 注册信息 (静态分配，生命周期与程序同)
 * ================================================================ */

static char_info_t scan_info = {
    .uuid = BLE_CHAR_SCAN_UUID,
    .path = CHAR_SCAN_PATH,
    .service_path = SERVICE_PATH,
    .flags = flags_write,
    .flag_count = 1
};

static char_info_t list_info = {
    .uuid = BLE_CHAR_LIST_UUID,
    .path = CHAR_LIST_PATH,
    .service_path = SERVICE_PATH,
    .flags = flags_notify,
    .flag_count = 2
};

static char_info_t config_info = {
    .uuid = BLE_CHAR_CONFIG_UUID,
    .path = CHAR_CONFIG_PATH,
    .service_path = SERVICE_PATH,
    .flags = flags_write,
    .flag_count = 1
};

static char_info_t status_info = {
    .uuid = BLE_CHAR_STATUS_UUID,
    .path = CHAR_STATUS_PATH,
    .service_path = SERVICE_PATH,
    .flags = flags_notify,
    .flag_count = 2
};

/* ================================================================
 * ObjectManager 实现 - GetManagedObjects
 * ================================================================ */

static void build_char_properties(GVariantBuilder *iface_builder,
                                  const char_info_t *info)
{
    GVariantBuilder props;
    g_variant_builder_init(&props, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&props, "{sv}", "UUID",
                          g_variant_new_string(info->uuid));
    g_variant_builder_add(&props, "{sv}", "Service",
                          g_variant_new_object_path(info->service_path));

    /* Flags */
    GVariantBuilder flags_builder;
    g_variant_builder_init(&flags_builder, G_VARIANT_TYPE("as"));
    for (int i = 0; i < info->flag_count; i++) {
        g_variant_builder_add(&flags_builder, "s", info->flags[i]);
    }
    g_variant_builder_add(&props, "{sv}", "Flags",
                          g_variant_builder_end(&flags_builder));

    g_variant_builder_add(iface_builder, "{sa{sv}}",
                          "org.bluez.GattCharacteristic1",
                          &props);
}

static void handle_get_managed_objects(GDBusConnection *conn,
                                       const gchar *sender,
                                       const gchar *object_path,
                                       const gchar *interface_name,
                                       const gchar *method_name,
                                       GVariant *parameters,
                                       GDBusMethodInvocation *invocation,
                                       gpointer user_data)
{
    (void)conn; (void)sender; (void)object_path;
    (void)interface_name; (void)method_name;
    (void)parameters; (void)user_data;

    GVariantBuilder objects;
    g_variant_builder_init(&objects, G_VARIANT_TYPE("a{oa{sa{sv}}}"));

    /* Service */
    {
        GVariantBuilder ifaces;
        g_variant_builder_init(&ifaces, G_VARIANT_TYPE("a{sa{sv}}"));

        GVariantBuilder props;
        g_variant_builder_init(&props, G_VARIANT_TYPE("a{sv}"));
        g_variant_builder_add(&props, "{sv}", "UUID",
                              g_variant_new_string(BLE_SERVICE_UUID));
        g_variant_builder_add(&props, "{sv}", "Primary",
                              g_variant_new_boolean(TRUE));
        g_variant_builder_add(&ifaces, "{sa{sv}}",
                              "org.bluez.GattService1", &props);

        g_variant_builder_add(&objects, "{oa{sa{sv}}}",
                              SERVICE_PATH, &ifaces);
    }

    /* Characteristics */
    const char_info_t *chars[] = {&scan_info, &list_info,
                                   &config_info, &status_info};
    const char *paths[] = {CHAR_SCAN_PATH, CHAR_LIST_PATH,
                           CHAR_CONFIG_PATH, CHAR_STATUS_PATH};

    for (int i = 0; i < 4; i++) {
        GVariantBuilder ifaces;
        g_variant_builder_init(&ifaces, G_VARIANT_TYPE("a{sa{sv}}"));
        build_char_properties(&ifaces, chars[i]);
        g_variant_builder_add(&objects, "{oa{sa{sv}}}",
                              paths[i], &ifaces);
    }

    g_dbus_method_invocation_return_value(
        invocation,
        g_variant_new("(a{oa{sa{sv}}})", &objects));
}

/* ================================================================
 * Advertisement 属性处理
 * ================================================================ */

static GVariant *adv_get_property(GDBusConnection *connection,
                                  const gchar *sender,
                                  const gchar *object_path,
                                  const gchar *interface_name,
                                  const gchar *property_name,
                                  GError **error,
                                  gpointer user_data)
{
    (void)connection; (void)sender; (void)object_path;
    (void)interface_name; (void)error; (void)user_data;

    if (g_strcmp0(property_name, "Type") == 0)
        return g_variant_new_string("peripheral");

    if (g_strcmp0(property_name, "ServiceUUIDs") == 0) {
        GVariantBuilder builder;
        g_variant_builder_init(&builder, G_VARIANT_TYPE("as"));
        g_variant_builder_add(&builder, "s", BLE_SERVICE_UUID);
        return g_variant_builder_end(&builder);
    }

    if (g_strcmp0(property_name, "LocalName") == 0)
        return g_variant_new_string(BLE_DEVICE_NAME);

    if (g_strcmp0(property_name, "Includes") == 0) {
        GVariantBuilder builder;
        g_variant_builder_init(&builder, G_VARIANT_TYPE("as"));
        g_variant_builder_add(&builder, "s", "tx-power");
        return g_variant_builder_end(&builder);
    }

    return NULL;
}

static void handle_adv_method(GDBusConnection *conn,
                              const gchar *sender,
                              const gchar *object_path,
                              const gchar *interface_name,
                              const gchar *method_name,
                              GVariant *parameters,
                              GDBusMethodInvocation *invocation,
                              gpointer user_data)
{
    (void)conn; (void)sender; (void)object_path;
    (void)interface_name; (void)parameters; (void)user_data;

    if (g_strcmp0(method_name, "Release") == 0) {
        printf("[BLE] Advertisement released\n");
        g_dbus_method_invocation_return_value(invocation, NULL);
    } else {
        g_dbus_method_invocation_return_dbus_error(invocation,
            "org.bluez.Error.NotSupported", "Method not supported");
    }
}

/* ================================================================
 * D-Bus 对象注册
 * ================================================================ */

/*
 * 每个对象必须拥有独立的静态 vtable 实例。
 * 若使用同一个 static 局部变量，每次调用都会覆盖之前注册对象
 * 的回调指针，导致 BlueZ 调用 GetManagedObjects 时超时。
 */
static const GDBusInterfaceVTable om_vtable     = { handle_get_managed_objects, NULL,                 NULL };
static const GDBusInterfaceVTable svc_vtable    = { NULL,                       service_get_property, NULL };
static const GDBusInterfaceVTable scan_vtable   = { handle_scan_write,          char_get_property,    NULL };
static const GDBusInterfaceVTable list_vtable   = { handle_list_method,         char_get_property,    NULL };
static const GDBusInterfaceVTable cfg_vtable    = { handle_config_write,        char_get_property,    NULL };
static const GDBusInterfaceVTable status_vtable = { handle_status_method,       char_get_property,    NULL };
static const GDBusInterfaceVTable adv_vtable    = { handle_adv_method,          adv_get_property,     NULL };

static guint register_object(GDBusConnection *conn,
                             const gchar *path,
                             const gchar *introspection_xml,
                             const gchar *interface_name,
                             const GDBusInterfaceVTable *vtable,
                             gpointer user_data)
{
    GError *error = NULL;

    GDBusNodeInfo *node_info = g_dbus_node_info_new_for_xml(
        introspection_xml, &error);
    if (!node_info) {
        fprintf(stderr, "[BLE] Failed to parse introspection XML for %s: %s\n",
                path, error->message);
        g_error_free(error);
        return 0;
    }

    GDBusInterfaceInfo *iface_info = g_dbus_node_info_lookup_interface(
        node_info, interface_name);
    if (!iface_info) {
        fprintf(stderr, "[BLE] Interface %s not found\n", interface_name);
        g_dbus_node_info_unref(node_info);
        return 0;
    }

    guint id = g_dbus_connection_register_object(
        conn, path, iface_info, vtable, user_data, NULL, &error);

    if (id == 0) {
        fprintf(stderr, "[BLE] Failed to register %s: %s\n",
                path, error->message);
        g_error_free(error);
    } else {
        if (registered_count < 32) {
            registered_ids[registered_count++] = id;
        }
    }

    /* 注意：node_info 引用需要保持到程序结束，这里简化处理不释放 */
    return id;
}

/* ================================================================
 * Notify 发送实现
 * ================================================================ */

void ble_notify_wifi_list(const char *data, int len)
{
    if (!g_conn || !wifi_list_notifying) {
        printf("[BLE] WiFi List notify not active, data length=%d\n", len);
        return;
    }

    /* 通过 PropertiesChanged 信号发送 Notify */
    GVariantBuilder changed_properties;
    g_variant_builder_init(&changed_properties, G_VARIANT_TYPE("a{sv}"));

    /* 分包发送：如果数据超过 BLE_CHUNK_SIZE，分多次发送 */
    int offset = 0;
    while (offset < len) {
        int chunk = (len - offset > BLE_CHUNK_SIZE)
                    ? BLE_CHUNK_SIZE : (len - offset);

        GVariant *value = build_byte_array(data + offset, chunk);

        g_variant_builder_init(&changed_properties, G_VARIANT_TYPE("a{sv}"));
        g_variant_builder_add(&changed_properties, "{sv}", "Value", value);

        GError *error = NULL;
        g_dbus_connection_emit_signal(
            g_conn, NULL, CHAR_LIST_PATH,
            "org.freedesktop.DBus.Properties",
            "PropertiesChanged",
            g_variant_new("(sa{sv}as)",
                          "org.bluez.GattCharacteristic1",
                          &changed_properties,
                          NULL),
            &error);

        if (error) {
            fprintf(stderr, "[BLE] Notify error: %s\n", error->message);
            g_error_free(error);
        }

        offset += chunk;

        /* 分包间隔 */
        if (offset < len)
            usleep(50000); /* 50ms */
    }

    printf("[BLE] Sent WiFi list notify, total %d bytes\n", len);
}

void ble_notify_status(int status)
{
    current_status = status;

    if (!g_conn || !status_notifying) {
        printf("[BLE] Status changed to %d (notify inactive)\n", status);
        return;
    }

    char status_byte = (char)status;
    GVariant *value = build_byte_array(&status_byte, 1);

    GVariantBuilder changed_properties;
    g_variant_builder_init(&changed_properties, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&changed_properties, "{sv}", "Value", value);

    GError *error = NULL;
    g_dbus_connection_emit_signal(
        g_conn, NULL, CHAR_STATUS_PATH,
        "org.freedesktop.DBus.Properties",
        "PropertiesChanged",
        g_variant_new("(sa{sv}as)",
                      "org.bluez.GattCharacteristic1",
                      &changed_properties,
                      NULL),
        &error);

    if (error) {
        fprintf(stderr, "[BLE] Status notify error: %s\n", error->message);
        g_error_free(error);
    } else {
        printf("[BLE] Status notify sent: %d\n", status);
    }
}

void ble_notify_status_with_ip(int status, const char *ip)
{
    current_status = status;

    if (!g_conn || !status_notifying) {
        printf("[BLE] Status changed to %d, ip=%s (notify inactive)\n",
               status, ip ? ip : "");
        return;
    }

    /* Payload: [status_byte][ip_string_bytes] */
    int ip_len = ip ? (int)strlen(ip) : 0;
    int total = 1 + ip_len;
    char buf[128];
    if (total > (int)sizeof(buf)) total = (int)sizeof(buf);
    buf[0] = (char)status;
    if (ip_len > 0)
        memcpy(buf + 1, ip, total - 1);

    GVariant *value = build_byte_array(buf, total);

    GVariantBuilder changed_properties;
    g_variant_builder_init(&changed_properties, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&changed_properties, "{sv}", "Value", value);

    GError *error = NULL;
    g_dbus_connection_emit_signal(
        g_conn, NULL, CHAR_STATUS_PATH,
        "org.freedesktop.DBus.Properties",
        "PropertiesChanged",
        g_variant_new("(sa{sv}as)",
                      "org.bluez.GattCharacteristic1",
                      &changed_properties,
                      NULL),
        &error);

    if (error) {
        fprintf(stderr, "[BLE] Status+IP notify error: %s\n", error->message);
        g_error_free(error);
    } else {
        printf("[BLE] Status notify sent: %d, IP: %s\n", status, ip);
    }
}

/* ================================================================
 * BLE GATT Server 初始化
 * ================================================================ */

int ble_server_init(GDBusConnection *connection)
{
    g_conn = connection;

    printf("[BLE] Registering GATT service and characteristics...\n");

    /* 1. 注册 ObjectManager */
    register_object(connection, APP_OBJECT_PATH,
                    om_introspection_xml,
                    "org.freedesktop.DBus.ObjectManager",
                    &om_vtable, NULL);

    /* 2. 注册 GATT Service */
    register_object(connection, SERVICE_PATH,
                    service_introspection_xml,
                    "org.bluez.GattService1",
                    &svc_vtable, NULL);

    /* 3. 注册 Scan WiFi Characteristic (Write) */
    register_object(connection, CHAR_SCAN_PATH,
                    char_write_introspection_xml,
                    "org.bluez.GattCharacteristic1",
                    &scan_vtable, &scan_info);

    /* 4. 注册 WiFi List Characteristic (Notify) */
    register_object(connection, CHAR_LIST_PATH,
                    char_notify_introspection_xml,
                    "org.bluez.GattCharacteristic1",
                    &list_vtable, &list_info);

    /* 5. 注册 WiFi Config Characteristic (Write) */
    register_object(connection, CHAR_CONFIG_PATH,
                    char_write_introspection_xml,
                    "org.bluez.GattCharacteristic1",
                    &cfg_vtable, &config_info);

    /* 6. 注册 Status Characteristic (Notify) */
    register_object(connection, CHAR_STATUS_PATH,
                    char_notify_introspection_xml,
                    "org.bluez.GattCharacteristic1",
                    &status_vtable, &status_info);

    /* 7. 异步向 BlueZ 注册 GATT Application
     * 必须异步：BlueZ 会回调我们的 GetManagedObjects，
     * 该回调需要 GLib 主循环 dispatch，而主循环在本函数返回后才启动。
     * 若使用 _sync 版本会造成死锁导致 "Timeout was reached"。 */
    g_dbus_connection_call(
        connection,
        "org.bluez",
        "/org/bluez/hci0",
        "org.bluez.GattManager1",
        "RegisterApplication",
        g_variant_new("(oa{sv})", APP_OBJECT_PATH, NULL),
        NULL,
        G_DBUS_CALL_FLAGS_NONE,
        -1, NULL,
        on_gatt_app_registered, NULL);

    printf("[BLE] RegisterApplication sent (async, completes after main loop starts)\n");
    return 0;
}

/* ================================================================
 * 异步注册回调
 * ================================================================ */

/*
 * 这两个回调在 GLib 主循环运行期间被触发。
 *
 * 正确的 BlueZ GATT 注册流程如下：
 *   1. g_dbus_connection_register_object() — 在本进程注册 D-Bus 对象（纯本地，无网络往返）
 *   2. g_dbus_connection_call()           — 异步向 BlueZ 发送 RegisterApplication
 *   3. g_main_loop_run()                 — 启动主循环
 *   4. BlueZ 回调我们的 GetManagedObjects  — 由主循环 dispatch，立即响应
 *   5. BlueZ 发回 RegisterApplication 成功 — 触发 on_gatt_app_registered
 *
 * 若使用 _sync 版本（步骤 2 阻塞）：主循环尚未运行 → GetManagedObjects
 * 永远无法被 dispatch → BlueZ 等待超时 → Timeout was reached
 */

static void on_gatt_app_registered(GObject *source,
                                   GAsyncResult *res,
                                   gpointer user_data)
{
    (void)user_data;
    GError *error = NULL;
    GVariant *result = g_dbus_connection_call_finish(
        G_DBUS_CONNECTION(source), res, &error);

    if (error) {
        fprintf(stderr, "[BLE] RegisterApplication failed: %s\n",
                error->message);
        g_error_free(error);
        if (g_app_main_loop)
            g_main_loop_quit(g_app_main_loop);
        return;
    }

    if (result)
        g_variant_unref(result);

    printf("[BLE] GATT Application registered successfully\n");
}

static void on_adv_registered(GObject *source,
                              GAsyncResult *res,
                              gpointer user_data)
{
    (void)user_data;
    GError *error = NULL;
    GVariant *result = g_dbus_connection_call_finish(
        G_DBUS_CONNECTION(source), res, &error);

    if (error) {
        fprintf(stderr, "[BLE] RegisterAdvertisement failed: %s\n",
                error->message);
        g_error_free(error);
        if (g_app_main_loop)
            g_main_loop_quit(g_app_main_loop);
        return;
    }

    if (result)
        g_variant_unref(result);

    printf("[BLE] Advertisement started as '%s'\n", BLE_DEVICE_NAME);
}

/* ================================================================
 * BLE LE 广播
 * ================================================================ */

int ble_advertising_start(GDBusConnection *connection)
{
    printf("[BLE] Setting up LE Advertisement...\n");

    /* 1. 设置设备名称 */
    GError *error = NULL;
    g_dbus_connection_call_sync(
        connection, "org.bluez", "/org/bluez/hci0",
        "org.freedesktop.DBus.Properties", "Set",
        g_variant_new("(ssv)", "org.bluez.Adapter1", "Alias",
                      g_variant_new_string(BLE_DEVICE_NAME)),
        NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
    if (error) {
        fprintf(stderr, "[BLE] Set Alias warning: %s\n", error->message);
        g_clear_error(&error);
    }

    /* 2. 确保适配器已开启 */
    g_dbus_connection_call_sync(
        connection, "org.bluez", "/org/bluez/hci0",
        "org.freedesktop.DBus.Properties", "Set",
        g_variant_new("(ssv)", "org.bluez.Adapter1", "Powered",
                      g_variant_new_boolean(TRUE)),
        NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
    if (error) {
        fprintf(stderr, "[BLE] Power on warning: %s\n", error->message);
        g_clear_error(&error);
    }

    /* 3. 注册 Advertisement 对象 */
    register_object(connection, ADV_PATH,
                    adv_introspection_xml,
                    "org.bluez.LEAdvertisement1",
                    &adv_vtable, NULL);

    /* 4. 异步注册广播到 BlueZ（同样不能用 _sync，原因同 RegisterApplication）*/
    g_dbus_connection_call(
        connection, "org.bluez", "/org/bluez/hci0",
        "org.bluez.LEAdvertisingManager1",
        "RegisterAdvertisement",
        g_variant_new("(oa{sv})", ADV_PATH, NULL),
        NULL, G_DBUS_CALL_FLAGS_NONE,
        -1, NULL,
        on_adv_registered, NULL);

    printf("[BLE] RegisterAdvertisement sent (async, completes after main loop starts)\n");
    return 0;
}

/* ================================================================
 * 清理
 * ================================================================ */

void ble_server_cleanup(void)
{
    if (!g_conn)
        return;

    /* 注销广播 */
    GError *error = NULL;
    g_dbus_connection_call_sync(
        g_conn, "org.bluez", "/org/bluez/hci0",
        "org.bluez.LEAdvertisingManager1",
        "UnregisterAdvertisement",
        g_variant_new("(o)", ADV_PATH),
        NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
    g_clear_error(&error);

    /* 注销 GATT Application */
    g_dbus_connection_call_sync(
        g_conn, "org.bluez", "/org/bluez/hci0",
        "org.bluez.GattManager1",
        "UnregisterApplication",
        g_variant_new("(o)", APP_OBJECT_PATH),
        NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
    g_clear_error(&error);

    /* 注销已注册的 DBus 对象 */
    for (int i = 0; i < registered_count; i++) {
        g_dbus_connection_unregister_object(g_conn, registered_ids[i]);
    }
    registered_count = 0;

    if (pending_wifi_data) {
        free(pending_wifi_data);
        pending_wifi_data = NULL;
    }

    printf("[BLE] Server cleanup done\n");
}
