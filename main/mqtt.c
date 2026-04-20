/**
 * MQTT Client Implementation
 *
 * Handles MQTT connections, message publishing, and subscription management
 * Supports both standard MQTT and MIP (Mesh IP) protocols
 */
#include <string.h>
#include <time.h>
#include "esp_log.h"
#include "esp_system.h"
#include "esp_netif.h"
// #include "esp_tls.h"
#include "esp_tls_crypto.h"
#include "esp_crt_bundle.h"
#include "mqtt_client.h"
#include "storage.h"
#include "config.h"
#include "system.h"
#include "s2j.h"
#include "misc.h"
#include "mqtt.h"
#include "debug.h"
#include "utils.h"
#include "iot_mip.h"
#include "push.h"

// Event bit definitions for MQTT state tracking
#define MQTT_START_BIT BIT(0)          // Client started
#define MQTT_STOP_BIT BIT(1)           // Client stopped
#define MQTT_CONNECT_BIT BIT(2)        // Connected to broker
#define MQTT_DISCONNECT_BIT BIT(3)     // Disconnected from broker
#define MQTT_PUBLISHED_BIT BIT(4)      // Message published

// Timeout constants (milliseconds)
#define MQTT_START_TIMEOUT_MS (1000)           // Start timeout
#define MQTT_CONNECT_TIMEOUT_MS (90000)        // Connection timeout
#define MQTT_DISCONNECT_TIMEOUT_MS (2000)      // Disconnect timeout
#define MQTT_STOP_TIMEOUT_MS (1000)            // Stop timeout
#define MQTT_PUBLISHED_TIMEOUT_MS (20000)      // Publish timeout

// Buffer sizes
#define MQTT_RECV_BUFFER_SIZE 8192       // Receive buffer size (used by MIP)

#define TAG "-->MQTT"  // Logging tag

/**
 * Subscription information
 */
typedef struct subscribe_s {
    char **topics;          // Array of subscribed topics
    int topic_cnt;          // Number of subscribed topics
    sub_notify_cb notify_cb; // Callback for received messages
} subscribe_t;

/**
 * MQTT module state
 */
typedef struct mdMqtt {
    EventGroupHandle_t eventGroup;     // Event group for state tracking
    mqttAttr_t mqtt;                   // MQTT configuration
    void *client;                      // MQTT client handle
    bool isConnected;                  // Connection status
    SemaphoreHandle_t mutex;           // Mutex for thread safety
    void *sendBuf;                     // Send buffer
    void *recvBuf;                     // Receive buffer (used by MIP)
    size_t sendBufSize;                // Send buffer size
    int8_t cfg_set_flag;               // Configuration flag
    subscribe_t sub;                   // Subscription info
    connect_status_cb status_cb;       // Connection status callback
    mqtt_t *mip;                       // MIP configuration
    esp_mqtt_client_config_t cfg;      // ESP MQTT client config
} mdMqtt_t;

static RTC_DATA_ATTR int g_sned_total = 0;
static RTC_DATA_ATTR int g_sned_success = 0;

static mdMqtt_t g_MQ = {0};
static int buff_index = 0;
static char event_topic[128];

/**
 * MQTT event handler callback
 * @param event MQTT event data
 * @param handler_args Pointer to mdMqtt_t state
 * @return ESP_OK on success
 */
static esp_err_t mqtt_event_handler_cb(esp_mqtt_event_handle_t event, void *handler_args)
{
    mdMqtt_t *mqtt = (mdMqtt_t *)handler_args;
    int i = 0;
    int msg_id;

    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
            for (i = 0; i < mqtt->sub.topic_cnt; i++) {
                msg_id = esp_mqtt_client_subscribe(mqtt->client, mqtt->sub.topics[i], 0);
                ESP_LOGI(TAG, "sent subscribe %s successful, msg_id=%d", mqtt->sub.topics[i], msg_id);
            }
            mqtt->isConnected = true;
            if (!iot_mip_dm_is_enable()) {
                xEventGroupClearBits(mqtt->eventGroup, MQTT_DISCONNECT_BIT);
                xEventGroupSetBits(mqtt->eventGroup, MQTT_CONNECT_BIT);
                storage_upload_start();
                push_ready();
            }
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
            xEventGroupClearBits(mqtt->eventGroup, MQTT_CONNECT_BIT);
            xEventGroupSetBits(mqtt->eventGroup, MQTT_DISCONNECT_BIT);
            if (mqtt->status_cb) {
                mqtt->status_cb(false);
            }
            mqtt->isConnected = false;
            storage_upload_stop();
            break;

        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
            if (mqtt->status_cb) {
                mqtt->status_cb(true);
            }
            if (iot_mip_dm_is_enable()) {
                xEventGroupClearBits(mqtt->eventGroup, MQTT_DISCONNECT_BIT);
                xEventGroupSetBits(mqtt->eventGroup, MQTT_CONNECT_BIT);
                storage_upload_start();
                push_ready();
            }
            break;
        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            xEventGroupSetBits(mqtt->eventGroup, MQTT_PUBLISHED_BIT);
            break;
        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "MQTT_EVENT_DATA");
            ESP_LOGI(TAG, "TOPIC=%.*s", event->topic_len, event->topic);
            ESP_LOGI(TAG, "DATA=%.*s", event->data_len, event->data);
            if (event->topic_len) {
                snprintf(event_topic, sizeof(event_topic), "%.*s", event->topic_len, event->topic);
            }
            if (event->data_len) {
                snprintf(mqtt->recvBuf + buff_index, MQTT_RECV_BUFFER_SIZE - buff_index, "%.*s", event->data_len, event->data);
                buff_index += event->data_len;
            }
            if (buff_index == event->total_data_len) {
                if (mqtt->sub.notify_cb) {
                    mqtt->sub.notify_cb(event_topic, mqtt->recvBuf);
                }
                buff_index = 0;
            }
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
            break;
        default:
            ESP_LOGI(TAG, "Other event id:%d", event->event_id);
            break;
    }
    return ESP_OK;
}

/**
 * MQTT event handler wrapper
 * @param handler_args Pointer to mdMqtt_t state
 * @param base Event base
 * @param event_id Event ID
 * @param event_data Event data
 */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGI(TAG, "Event dispatched from event loop base=%s, event_id=%ld", base, event_id);
    mqtt_event_handler_cb(event_data, handler_args);
}

/**
 * Build JSON payload string from a queueNode_t.
 * Uses internal send buffer for base64 encoding.
 * Caller must free the returned string with cJSON_free()
 * @param node Queue node containing image data
 * @return Allocated JSON string, or NULL on failure
 */
char *push_build_json_payload(queueNode_t *node)
{
    esp_err_t res = ESP_OK;
    size_t picSize;
    deviceInfo_t device;
    char *snapType = NULL;
    char time_str[32];
    char header[] = "data:image/jpeg;base64,";

    switch (node->type) {
        case SNAP_ALARMIN:
            snapType = "Alarm in";
            break;
        case SNAP_PIR:
            snapType = "PIR";
            break;
        case SNAP_BUTTON:
            snapType = "Button";
            break;
        case SNAP_TIMER:
            snapType = "Timer";
            break;
        default:
            snapType = "Unknown";
            break;
    }

    memcpy((char *)g_MQ.sendBuf, header, strlen(header));
    size_t header_len = strlen(header);
    size_t available_size = g_MQ.sendBufSize - header_len;

    size_t required_size = ((node->len + 2) / 3) * 4;
    if (required_size > available_size) {
        ESP_LOGE(TAG, "Buffer too small: required=%zu, available=%zu", required_size, available_size);
        return NULL;
    }

    res = esp_crypto_base64_encode(g_MQ.sendBuf + header_len, available_size, &picSize, node->data, node->len);
    if (res < 0) {
        ESP_LOGE(TAG, "base64_encode failed");
        return NULL;
    }

    cfg_get_device_info(&device);
    time_t t = node->pts / 1000;
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", localtime(&t));

    cJSON *json = cJSON_CreateObject();
    cJSON *subJson = cJSON_CreateObject();
    cJSON_AddStringToObject(subJson, "devName", device.name);
    cJSON_AddStringToObject(subJson, "devMac", device.mac);
    cJSON_AddStringToObject(subJson, "devSn", device.sn);
    cJSON_AddStringToObject(subJson, "hwVersion", device.hardVersion);
    cJSON_AddStringToObject(subJson, "fwVersion", device.softVersion);
    cJSON_AddNumberToObject(subJson, "battery", misc_get_battery_voltage_rate());
    cJSON_AddNumberToObject(subJson, "batteryVoltage", misc_get_battery_voltage());
    cJSON_AddStringToObject(subJson, "snapType", snapType);
    cJSON_AddStringToObject(subJson, "localtime", time_str);
    cJSON_AddNumberToObject(subJson, "imageSize", picSize + strlen(header));
    cJSON_AddStringToObject(subJson, "image", (char *)g_MQ.sendBuf);
    cJSON_AddNumberToObject(json, "ts", node->pts);
    cJSON_AddItemToObject(json, "values", subJson);

    char *str = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    return str;
}

/**
 * Publish a queueNode_t as JSON via MQTT
 * @param node Queue node containing image data
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t mqtt_publish_node(queueNode_t *node)
{
    if (!g_MQ.isConnected) {
        return ESP_FAIL;
    }

    char *json_str = push_build_json_payload(node);
    if (json_str == NULL) {
        return ESP_FAIL;
    }

    esp_err_t res;
    if (iot_mip_dm_is_enable()) {
        res = iot_mip_dm_uplink_picture(json_str);
    } else {
        mqttAttr_t mqtt;
        cfg_get_mqtt_attr(&mqtt);
        res = esp_mqtt_client_publish(g_MQ.client, mqtt.topic, json_str, 0, mqtt.qos, 0);
        if (mqtt.qos == 0) {
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }
    cJSON_free(json_str);

    if (res < 0) {
        return ESP_FAIL;
    }

    // For QoS > 0 and non-MIP, wait for publish ACK
    mqttAttr_t mqtt;
    cfg_get_mqtt_attr(&mqtt);
    if (mqtt.qos == 0 || iot_mip_dm_is_enable()) {
        return ESP_OK;
    }

    EventBits_t uxBits = xEventGroupWaitBits(g_MQ.eventGroup, MQTT_PUBLISHED_BIT, true, true,
                                              pdMS_TO_TICKS(MQTT_PUBLISHED_TIMEOUT_MS));
    return (uxBits & MQTT_PUBLISHED_BIT) ? ESP_OK : ESP_FAIL;
}

/**
 * Free MQTT client configuration resources
 * @param m MQTT state
 */
static void free_mqtt_client_config(mdMqtt_t *m)
{
    int i = 0;
    esp_mqtt_client_config_t *c = &m->cfg;
    mip_free((void **)&c->credentials.client_id);
    mip_free((void **)&c->credentials.username);
    mip_free((void **)&c->credentials.authentication.password);
    mip_free((void **)&c->broker.verification.certificate);
    mip_free((void **)&c->credentials.authentication.certificate);
    mip_free((void **)&c->credentials.authentication.key);
    mip_free((void **)&c->broker.address.uri);

    for (i = 0; i < m->sub.topic_cnt; i++) {
        mip_free((void **)&m->sub.topics[i]);
    }
    m->sub.topic_cnt = 0;
    mip_free((void **)&m->sub.topics);
    memset(c, 0, sizeof(esp_mqtt_client_config_t));

    return;
}

/**
 * Configure ESP MQTT client
 * @param m MQTT state
 * @param c ESP MQTT client config to populate
 */
static void mqtt_esp_config(mdMqtt_t *m)
{
    esp_mqtt_client_config_t *c = &m->cfg;
    memset(c, 0, sizeof(esp_mqtt_client_config_t));
    cfg_get_mqtt_attr(&m->mqtt);
    c->broker.address.hostname = m->mqtt.host;
    c->broker.address.port = m->mqtt.port;
    c->broker.address.transport = m->mqtt.tlsEnable ? MQTT_TRANSPORT_OVER_SSL : MQTT_TRANSPORT_OVER_TCP;
    c->credentials.username = m->mqtt.user;
    c->credentials.client_id = m->mqtt.clientId;
    c->task.stack_size = 6 * 1024;
    c->network.disable_auto_reconnect = true;
    if (strlen(m->mqtt.password)) {
        c->credentials.authentication.password = m->mqtt.password;
    }
    // TLS:
    if (m->mqtt.tlsEnable) {
        if (strlen(m->mqtt.caName)) {
            c->broker.verification.skip_cert_common_name_check = true;
            c->broker.verification.certificate = filesystem_read(MQTT_CA_PATH);
        } else {
            c->broker.verification.crt_bundle_attach = esp_crt_bundle_attach;
        }
        if (strlen(m->mqtt.certName) && strlen(m->mqtt.keyName)) {
            c->credentials.authentication.certificate = filesystem_read(MQTT_CERT_PATH);
            c->credentials.authentication.key = filesystem_read(MQTT_KEY_PATH);
        }
        c->network.timeout_ms = 15000;
        c->broker.verification.use_global_ca_store = false;
    }
    ESP_LOGI(TAG, "HOST:%s, USER:%s PSW:%s, PORT:%ld, TLS:%d",
             m->mqtt.host, m->mqtt.user, m->mqtt.password, m->mqtt.port, m->mqtt.tlsEnable);
}

static void mqtt_free_config(mdMqtt_t *m)
{
    esp_mqtt_client_config_t *c = &m->cfg;
    if (c->broker.verification.certificate) {
        free((void *)c->broker.verification.certificate);
    }
    if (c->credentials.authentication.certificate) {
        free((void *)c->credentials.authentication.certificate);
    }
    if (c->credentials.authentication.key) {
        free((void *)c->credentials.authentication.key);
    }
}
/**
 * Start ESP MQTT client
 * @param m MQTT state
 * @return 0 on success, negative on error
 */
static int8_t mqtt_esp_start(mdMqtt_t *m)
{
    m->mip = NULL;
    m->status_cb = NULL;
    m->sub.notify_cb = NULL;
    m->sub.topic_cnt = 0;
    mqtt_esp_config(m);
    m->client = esp_mqtt_client_init(&m->cfg);
    esp_mqtt_client_register_event(g_MQ.client, ESP_EVENT_ANY_ID, mqtt_event_handler, &g_MQ);
    return esp_mqtt_client_start(g_MQ.client);
}

/**
 * Stop ESP MQTT client
 * @param m MQTT state
 * @return 0 on success, negative on error
 */
static int8_t mqtt_esp_stop(mdMqtt_t *m)
{
    if (!m->client) {
        return -1;
    }
    esp_mqtt_client_disconnect(m->client);
    esp_mqtt_client_stop(m->client);
    esp_mqtt_client_destroy(m->client);
    mqtt_free_config(m);
    m->client = NULL;
    return 0;
}

/**
 * Console command handler for showing send success rate
 * @param argc Argument count
 * @param argv Argument values
 * @return ESP_OK
 */
static int do_sendrate_cmd(int argc, char **argv)
{
    if (g_sned_total) {
        ESP_LOGI(TAG, "Send: %d/%d = %d%%", g_sned_success, g_sned_total, g_sned_success * 100 / g_sned_total);
    } else {
        ESP_LOGI(TAG, "Send: 0/0 = 0%%");
    }
    return ESP_OK;
}

static esp_console_cmd_t g_cmd[] = {
    ESP_CONSOLE_CMD_INIT("sendrate", "mqtt send success rate", NULL, do_sendrate_cmd, NULL),
};

void mqtt_open(void)
{
    memset(&g_MQ, 0, sizeof(mdMqtt_t));
    g_MQ.eventGroup = xEventGroupCreate();
    g_MQ.mutex = xSemaphoreCreateMutex();
    g_MQ.sendBuf = malloc(PUSH_SEND_BUFFER_SIZE);
    assert(g_MQ.sendBuf);
    g_MQ.sendBufSize = PUSH_SEND_BUFFER_SIZE;
    debug_cmd_add(g_cmd, sizeof(g_cmd) / sizeof(esp_console_cmd_t));
}

void mqtt_start()
{
    if (g_MQ.isConnected) {
        return;
    }
    if (iot_mip_dm_is_enable()) {
        iot_mip_dm_async_start(NULL);
    } else {
        ESP_LOGI(TAG, "mqtt esp start");
        mqtt_esp_start(&g_MQ);
    }
    ESP_LOGI(TAG, "esp_mqtt_client_start");
}

void mqtt_stop()
{
    if (iot_mip_dm_is_enable()) {
        iot_mip_dm_stop();
    } else {
        mqtt_esp_stop(&g_MQ);
    }
    g_MQ.isConnected = false;
    ESP_LOGI(TAG, "esp_mqtt_client_stop");
}

void mqtt_restart()
{
    mqtt_stop();
    mqtt_start();
}

void mqtt_close(void)
{
    if (g_MQ.sendBuf) {
        free(g_MQ.sendBuf);
        g_MQ.sendBuf = NULL;
    }
}

//---------------------------------mqtt mip---------------------------------

static void mqtt_mip_config(mdMqtt_t *m)
{
    mqtt_t *mqtt = m->mip;
    esp_mqtt_client_config_t *c = &m->cfg;
    int i = 0;
    char uri[256] = {0};

    if (!mqtt || !c) {
        ESP_LOGE(TAG, "mqtt or c is null");
        return;
    }

    memset(c, 0, sizeof(esp_mqtt_client_config_t));
    c->broker.address.port = mqtt->port;
    c->credentials.client_id = strdup(mqtt->client_id);
    if (strlen(mqtt->user) && strlen(mqtt->pass)) {
        c->credentials.username = strdup(mqtt->user);
        c->credentials.authentication.password = strdup(mqtt->pass);
    }
    ESP_LOGI(TAG, "ca:%s, cert:%s, key:%s", mqtt->ca_cert_path ? mqtt->ca_cert_path : "NULL",
             mqtt->cert_path ? mqtt->cert_path : "NULL", mqtt->key_path ? mqtt->key_path : "NULL");
    if (!strncmp(mqtt->host, "ws", 2) || !strncmp(mqtt->host, "mqtt", 4)) {
        // protocol header included
        snprintf(uri, sizeof(uri), "%s", mqtt->host);
        if (!strncmp(uri, "wss", 3) || !strncmp(uri, "mqtts", 5)) {
            if (mqtt->ca_cert_path && strlen(mqtt->ca_cert_path)) {
                c->broker.verification.skip_cert_common_name_check = true;
                c->broker.verification.certificate = filesystem_read(mqtt->ca_cert_path);
            } else {
                c->broker.verification.crt_bundle_attach = esp_crt_bundle_attach;
            }
            if (mqtt->cert_path && strlen(mqtt->cert_path) && mqtt->key_path && strlen(mqtt->key_path)) {
                c->credentials.authentication.certificate = filesystem_read(mqtt->cert_path);
                c->credentials.authentication.key = filesystem_read(mqtt->key_path);
            }
        }
    } else {
        if (mqtt->ca_cert_path && strlen(mqtt->ca_cert_path)) {
            snprintf(uri, sizeof(uri), "mqtts://%s", mqtt->host);
            c->broker.verification.skip_cert_common_name_check = true;
            c->broker.verification.certificate = filesystem_read(mqtt->ca_cert_path);
            if (mqtt->cert_path && strlen(mqtt->cert_path) && mqtt->key_path && strlen(mqtt->key_path)) {
                c->credentials.authentication.certificate = filesystem_read(mqtt->cert_path);
                c->credentials.authentication.key = filesystem_read(mqtt->key_path);
            }
        } else {
            snprintf(uri, sizeof(uri), "mqtt://%s", mqtt->host);
        }
    }
    ESP_LOGD(TAG, "uri=%s", uri);
    c->broker.address.uri = strdup(uri);
    c->task.stack_size = 7 * 1024;
    c->network.disable_auto_reconnect = true;

    m->sub.topic_cnt = mqtt->topic_cnt;
    m->sub.topics = mip_malloc(sizeof(char *) * m->sub.topic_cnt);
    ESP_LOGI(TAG, "sub.topic_cnt=%d", m->sub.topic_cnt);
    for (i = 0; i < m->sub.topic_cnt; i++) {
        ESP_LOGI(TAG, "sub.topics[%d]=%s", i, mqtt->topics[i]);
        m->sub.topics[i] = strdup(mqtt->topics[i]);
    }

    m->cfg_set_flag = 1;
}

int8_t mqtt_mip_is_connected(void)
{
    return g_MQ.isConnected;
}

int8_t mqtt_mip_publish(const char *topic, const char *msg, int timeout)
{
    ESP_LOGD(TAG, "topic=%s, msg=%s", topic, msg);
    if (!mqtt_mip_is_connected()) {
        return -1;
    }
    if (esp_mqtt_client_publish(g_MQ.client, topic, msg, strlen(msg), 0, 0) == -1) {
        ESP_LOGE(TAG, "mqtt publish %s failed", topic);
        return -2;
    }
    ESP_LOGI(TAG, "mqtt publish %s succ", topic);
    return 0;
}

int8_t mqtt_mip_start(mqtt_t *mqtt, sub_notify_cb cb, connect_status_cb status_cb)
{
    g_MQ.sub.notify_cb = cb;
    g_MQ.status_cb = status_cb;
    g_MQ.mip = mqtt;
    g_MQ.recvBuf = mip_malloc(MQTT_RECV_BUFFER_SIZE);
    mqtt_mip_config(&g_MQ);

    g_MQ.client = esp_mqtt_client_init(&g_MQ.cfg);
    if (!g_MQ.client) {
        ESP_LOGE(TAG, "esp_mqtt_client_init failed");
        return -1;
    }
    esp_mqtt_client_register_event(g_MQ.client, ESP_EVENT_ANY_ID, mqtt_event_handler, &g_MQ);
    return esp_mqtt_client_start(g_MQ.client);
}

int8_t mqtt_mip_stop(void)
{
    if (!g_MQ.client) {
        return -1;
    }
    esp_mqtt_client_disconnect(g_MQ.client);
    esp_mqtt_client_stop(g_MQ.client);
    if (g_MQ.cfg_set_flag) {
        free_mqtt_client_config(&g_MQ);
    } else {
        //init
        g_MQ.sub.topic_cnt = 0;
        g_MQ.sub.topics = NULL;
        g_MQ.sub.notify_cb = NULL;
    }
    esp_mqtt_client_destroy(g_MQ.client);
    mip_free((void **)&g_MQ.recvBuf);
    g_MQ.client = NULL;
    return 0;
}
