/**
 * @file ble_prov.c
 * @brief BLE provisioning using raw NimBLE GATT server.
 *
 * Creates a custom GATT service (0xFFFF) with a single characteristic (0xFF53)
 * for receiving JSON config (WiFi + MQTT). Bypasses wifi_prov_mgr entirely
 * for maximum control and simplicity.
 *
 * Flow:
 *   1. nimble_port_init() + ble_hs_cfg
 *   2. Create GATT service 0xFFFF with char 0xFF53 (write+read)
 *   3. Start advertising as "NE101_XXXXXX"
 *   4. Client writes JSON to 0xFF53 → handler parses & saves config → {"status":0}
 */

#include <string.h>
#include "esp_log.h"
#include "esp_mac.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "ble_prov.h"
#include "config.h"

static const char *TAG = "BLE_PROV";
static bool s_active = false;
static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static EventGroupHandle_t s_prov_eg = NULL;
#define PROV_DONE_BIT  BIT0
#define BLE_CONNECTED_BIT BIT1
#define SLEEP_NOW_BIT  BIT2

// Forward declarations
void ble_advertise(void);

// GATT characteristic value handle
static uint16_t s_char_val_handle;

// Response buffer (static so it persists across read callback)
static char s_resp_buf[256];
static uint16_t s_resp_len;

// ---------------------------------------------------------------------------
// GATT service table
// ---------------------------------------------------------------------------

/* Service UUID: 0000fffe-0000-1000-8000-00805f9b34fb (different from wifi_prov_mgr to avoid CoreBluetooth cache) */
static const ble_uuid128_t gatt_svr_svc_uuid = {
    .u = { .type = BLE_UUID_TYPE_128 },
    .value = { 0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80,
               0x00, 0x10, 0x00, 0x00, 0xFE, 0xFF, 0x00, 0x00 }
};

/* Characteristic UUID: 0000ff53-0000-1000-8000-00805f9b34fb */
static const ble_uuid128_t gatt_svr_chr_uuid = {
    .u = { .type = BLE_UUID_TYPE_128 },
    .value = { 0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80,
               0x00, 0x10, 0x00, 0x00, 0x53, 0xFF, 0x00, 0x00 }
};

static int gatt_svr_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                                struct ble_gatt_access_ctxt *ctxt, void *arg);

static const struct ble_gatt_svc_def gatt_svr_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &gatt_svr_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &gatt_svr_chr_uuid.u,
                .access_cb = gatt_svr_chr_access,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE,
                .val_handle = &s_char_val_handle,
            },
            { 0 } /* terminator */
        },
    },
    { 0 } /* terminator */
};

// ---------------------------------------------------------------------------
// JSON config handler
// ---------------------------------------------------------------------------

static void handle_config_json(const uint8_t *data, size_t len)
{
    char *json_str = malloc(len + 1);
    if (!json_str) {
        s_resp_len = snprintf(s_resp_buf, sizeof(s_resp_buf), "{\"status\":1,\"error\":\"OOM\"}");
        return;
    }
    memcpy(json_str, data, len);
    json_str[len] = '\0';

    ESP_LOGI(TAG, "Received config JSON (%d bytes): %s", (int)len, json_str);

    cJSON *root = cJSON_Parse(json_str);
    free(json_str);

    if (!root) {
        ESP_LOGE(TAG, "Failed to parse JSON");
        s_resp_len = snprintf(s_resp_buf, sizeof(s_resp_buf), "{\"status\":1,\"error\":\"bad JSON\"}");
        return;
    }

    /* Device name */
    const cJSON *dev_name = cJSON_GetObjectItem(root, "device_name");
    if (dev_name && cJSON_IsString(dev_name)) {
        deviceInfo_t device;
        cfg_get_device_info(&device);
        snprintf(device.name, sizeof(device.name), "%s", dev_name->valuestring);
        cfg_set_device_info(&device);
        ESP_LOGI(TAG, "Device name saved: %s", device.name);
    }

    /* WiFi config */
    const cJSON *ssid = cJSON_GetObjectItem(root, "ssid");
    const cJSON *wifi_pass = cJSON_GetObjectItem(root, "wifi_password");

    if (ssid && cJSON_IsString(ssid)) {
        wifiAttr_t wifi;
        memset(&wifi, 0, sizeof(wifi));
        cfg_get_wifi_attr(&wifi);
        snprintf(wifi.ssid, sizeof(wifi.ssid), "%s", ssid->valuestring);
        if (wifi_pass && cJSON_IsString(wifi_pass))
            snprintf(wifi.password, sizeof(wifi.password), "%s", wifi_pass->valuestring);
        cfg_set_wifi_attr(&wifi);
        ESP_LOGI(TAG, "WiFi config saved: SSID=%s", wifi.ssid);
    }

    /* MQTT config — use platformParam to ensure PLATFORM_TYPE_MQTT is set,
     * so cfg_get_mqtt_attr() reads our values instead of the Sensing defaults. */
    platformParamAttr_t platform;
    cfg_get_platform_param_attr(&platform);
    platform.currentPlatformType = PLATFORM_TYPE_MQTT;

    const cJSON *host = cJSON_GetObjectItem(root, "host");
    if (host && cJSON_IsString(host))
        snprintf(platform.mqttPlatform.host, sizeof(platform.mqttPlatform.host), "%s", host->valuestring);

    const cJSON *port = cJSON_GetObjectItem(root, "port");
    if (port && cJSON_IsNumber(port))
        platform.mqttPlatform.mqttPort = (uint32_t)port->valuedouble;

    const cJSON *username = cJSON_GetObjectItem(root, "username");
    if (username && cJSON_IsString(username))
        snprintf(platform.mqttPlatform.username, sizeof(platform.mqttPlatform.username), "%s", username->valuestring);

    const cJSON *password = cJSON_GetObjectItem(root, "password");
    if (password && cJSON_IsString(password))
        snprintf(platform.mqttPlatform.password, sizeof(platform.mqttPlatform.password), "%s", password->valuestring);

    const cJSON *topic_prefix = cJSON_GetObjectItem(root, "topic_prefix");
    if (topic_prefix && cJSON_IsString(topic_prefix))
        snprintf(platform.mqttPlatform.topic, sizeof(platform.mqttPlatform.topic), "%s/uplink", topic_prefix->valuestring);

    const cJSON *client_id = cJSON_GetObjectItem(root, "client_id");
    if (client_id && cJSON_IsString(client_id))
        snprintf(platform.mqttPlatform.clientId, sizeof(platform.mqttPlatform.clientId), "%s", client_id->valuestring);

    cfg_set_u8(KEY_MQTT_ENABLE, 1);
    cfg_set_platform_param_attr(&platform);

    ESP_LOGI(TAG, "MQTT config saved: host=%s port=%lu user=%s topic=%s clientId=%s",
             platform.mqttPlatform.host, (unsigned long)platform.mqttPlatform.mqttPort,
             platform.mqttPlatform.username, platform.mqttPlatform.topic, platform.mqttPlatform.clientId);

    const cJSON *sleep_req = cJSON_GetObjectItem(root, "sleep");
    cJSON_Delete(root);

    s_resp_len = snprintf(s_resp_buf, sizeof(s_resp_buf), "{\"status\":0}");
    ESP_LOGI(TAG, "Config saved successfully");

    if (s_prov_eg) {
        xEventGroupSetBits(s_prov_eg, PROV_DONE_BIT);
        if (sleep_req && cJSON_IsTrue(sleep_req)) {
            ESP_LOGI(TAG, "Sleep requested by client");
            xEventGroupSetBits(s_prov_eg, SLEEP_NOW_BIT);
        }
    }
}

// ---------------------------------------------------------------------------
// GATT access callback
// ---------------------------------------------------------------------------

static int gatt_svr_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                                struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)arg;

    int rc;

    switch (ctxt->op) {
    case BLE_GATT_ACCESS_OP_READ_CHR:
        /* If no write yet, return current device info; otherwise return last response */
        if (s_resp_len == 0) {
            deviceInfo_t dev;
            wifiAttr_t wifi;
            cfg_get_device_info(&dev);
            cfg_get_wifi_attr(&wifi);
            const char *sn = (dev.sn[0] && strcmp(dev.sn, "undefined") != 0) ? dev.sn : "";
            s_resp_len = snprintf(s_resp_buf, sizeof(s_resp_buf),
                "{\"device_name\":\"%s\",\"ssid\":\"%s\",\"mac\":\"%s\",\"sn\":\"%s\",\"model\":\"%s\",\"netmod\":\"%s\"}",
                dev.name, wifi.ssid, dev.mac, sn, dev.model, dev.netmod);
        }
        {
            rc = os_mbuf_append(ctxt->om, s_resp_buf, s_resp_len);
        }
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;

    case BLE_GATT_ACCESS_OP_WRITE_CHR:
        /* Accumulate write data */
        {
            uint16_t om_len = OS_MBUF_PKTLEN(ctxt->om);
            uint8_t *buf = malloc(om_len + 1);
            if (!buf) return BLE_ATT_ERR_INSUFFICIENT_RES;

            rc = ble_hs_mbuf_to_flat(ctxt->om, buf, om_len, NULL);
            if (rc != 0) {
                free(buf);
                return BLE_ATT_ERR_UNLIKELY;
            }
            buf[om_len] = '\0';

            handle_config_json(buf, om_len);
            free(buf);
        }
        return 0;

    default:
        return BLE_ATT_ERR_UNLIKELY;
    }
}

// ---------------------------------------------------------------------------
// BLE host task & GAP events
// ---------------------------------------------------------------------------

static void ble_on_sync(void)
{
    ESP_LOGI(TAG, "BLE sync — starting advertising");
    ble_advertise();
}

static void ble_on_reset(int reason)
{
    ESP_LOGI(TAG, "BLE reset, reason=%d", reason);
}

static void ble_host_task(void *param)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static int gap_event_handler(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        ESP_LOGI(TAG, "BLE %s; status=%d",
                 event->connect.status == 0 ? "connected" : "connect failed",
                 event->connect.status);
        if (event->connect.status == 0) {
            s_conn_handle = event->connect.conn_handle;
            if (s_prov_eg) xEventGroupSetBits(s_prov_eg, BLE_CONNECTED_BIT);
        } else {
            /* Connection failed; resume advertising */
            ble_advertise();
        }
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "BLE disconnect; reason=%d", event->disconnect.reason);
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        /* Resume advertising */
        ble_advertise();
        break;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        ble_advertise();
        break;

    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "MTU update: conn=%d mtu=%d",
                 event->mtu.conn_handle, event->mtu.value);
        break;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Advertising
// ---------------------------------------------------------------------------

void ble_advertise(void)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_BT);
    char name[24];
    snprintf(name, sizeof(name), "NE101_%02X%02X%02X", mac[3], mac[4], mac[5]);

    struct ble_hs_adv_fields fields = {0};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = (uint8_t *)name;
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;
    ble_gap_adv_set_fields(&fields);

    struct ble_gap_adv_params adv_params = {0};
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    adv_params.itvl_min = 0x100;  /* 160ms */
    adv_params.itvl_max = 0x100;

    int rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                                &adv_params, gap_event_handler, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv_start failed: rc=%d", rc);
    } else {
        ESP_LOGI(TAG, "Advertising as '%s'", name);
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void ble_prov_init(void)
{
    /* Nothing to do — nimble_port_init() is called in ble_prov_start() */
}

void ble_prov_start(void)
{
    if (s_active) return;

    if (!s_prov_eg) s_prov_eg = xEventGroupCreate();

    /* Reset response */
    s_resp_len = 0;
    s_resp_buf[0] = '\0';

    ESP_ERROR_CHECK(nimble_port_init());

    /* Configure BLE host */
    ble_hs_cfg.sync_cb = ble_on_sync;
    ble_hs_cfg.reset_cb = ble_on_reset;

    /* Initialize GAP and GATT services */
    ble_svc_gap_init();
    ble_svc_gatt_init();

    /* Register our custom GATT service table */
    ESP_ERROR_CHECK(ble_gatts_count_cfg(gatt_svr_svcs));
    ESP_ERROR_CHECK(ble_gatts_add_svcs(gatt_svr_svcs));

    /* Set device name */
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_BT);
    char name[24];
    snprintf(name, sizeof(name), "NE101_%02X%02X%02X", mac[3], mac[4], mac[5]);
    ble_svc_gap_device_name_set(name);

    /* Start BLE host task */
    nimble_port_freertos_init(ble_host_task);

    s_active = true;
    ESP_LOGI(TAG, "BLE provisioning started as '%s'", name);
}

void ble_prov_stop(void)
{
    if (!s_active) return;
    s_active = false;

    if (s_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        ble_gap_terminate(s_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }
    ble_gap_adv_stop();
    nimble_port_stop();
    nimble_port_deinit();
    ESP_LOGI(TAG, "BLE stopped");
}

bool ble_prov_is_active(void) { return s_active; }
bool ble_prov_is_connected(void) { return s_conn_handle != BLE_HS_CONN_HANDLE_NONE; }

bool ble_prov_wait_connect(uint32_t timeout_ms)
{
    if (!s_prov_eg) return false;

    ESP_LOGI(TAG, "Waiting for BLE config (%lus)...", (unsigned long)(timeout_ms / 1000));
    EventBits_t bits = xEventGroupWaitBits(
        s_prov_eg, PROV_DONE_BIT, pdTRUE, pdFALSE, pdMS_TO_TICKS(timeout_ms));

    if (!(bits & PROV_DONE_BIT)) {
        ESP_LOGI(TAG, "BLE timeout");
        return false;
    }

    ESP_LOGI(TAG, "BLE config received successfully");
    return true;
}

bool ble_prov_wait_connected(uint32_t timeout_ms)
{
    if (!s_prov_eg) return false;

    ESP_LOGI(TAG, "Waiting for BLE connection (%lus)...", (unsigned long)(timeout_ms / 1000));
    EventBits_t bits = xEventGroupWaitBits(
        s_prov_eg, BLE_CONNECTED_BIT, pdTRUE, pdFALSE, pdMS_TO_TICKS(timeout_ms));

    if (!(bits & BLE_CONNECTED_BIT)) {
        ESP_LOGI(TAG, "No BLE connection within timeout");
        return false;
    }

    ESP_LOGI(TAG, "BLE client connected");
    return true;
}

bool ble_prov_sleep_requested(void)
{
    if (!s_prov_eg) return false;
    EventBits_t bits = xEventGroupGetBits(s_prov_eg);
    return (bits & SLEEP_NOW_BIT) != 0;
}
