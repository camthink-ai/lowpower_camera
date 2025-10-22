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

// Event bit definitions for MQTT state tracking
#define MQTT_START_BIT BIT(0)          // Client started
#define MQTT_STOP_BIT BIT(1)           // Client stopped  
#define MQTT_CONNECT_BIT BIT(2)        // Connected to broker
#define MQTT_DISCONNECT_BIT BIT(3)     // Disconnected from broker
#define MQTT_PUBLISHED_BIT BIT(4)      // Message published
#define MQTT_TASK_STOP_BIT BIT(5)      // Task should stop

// Timeout constants (milliseconds)
#define MQTT_START_TIMEOUT_MS (1000)           // Start timeout
#define MQTT_CONNECT_TIMEOUT_MS (30000)        // Connection timeout
#define MQTT_DISCONNECT_TIMEOUT_MS (2000)      // Disconnect timeout
#define MQTT_STOP_TIMEOUT_MS (1000)            // Stop timeout
#define MQTT_PUBLISHED_TIMEOUT_MS (20000)      // Publish timeout

// Buffer sizes
#define MQTT_SEND_BUFFER_SIZE  (512000)  // Send buffer size
#define MQTT_RECV_BUFFER_SIZE 8192       // Receive buffer size

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
    QueueHandle_t in;                  // Input queue for messages to send
    QueueHandle_t out;                 // Output queue for failed messages
    bool isConnected;                  // Connection status
    SemaphoreHandle_t mutex;           // Mutex for thread safety
    void *sendBuf;                     // Send buffer
    void *recvBuf;                     // Receive buffer
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
 * Send message as JSON payload
 * @param mqtt MQTT state
 * @param node Queue node containing message data
 * @return ESP_OK on success, ESP_FAIL on error
 */
static esp_err_t mqtt_send_by_json(mdMqtt_t *mqtt, queueNode_t *node)
{
    esp_err_t res = ESP_OK;
    size_t picSize;
    deviceInfo_t device;
    char *snapType = NULL;
    char *str = NULL;
    char time[32];
    char header[] = "data:image/jpeg;base64,";

    switch (node->type) {
        case SNAP_ALARMIN:
            snapType = "Alarm in";
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
    memcpy((char *)mqtt->sendBuf, header, strlen(header));
    res = esp_crypto_base64_encode(mqtt->sendBuf + strlen(header), mqtt->sendBufSize +
                                   strlen(header), &picSize, node->data, node->len);
    // res = esp_crypto_base64_encode(mqtt->sendBuf, mqtt->sendBufSize, &picSize, node->data, node->len);
    if (res < 0) {
        ESP_LOGE(TAG, "esp_crypto_base64_encode failed");
        return ESP_FAIL;
    }
    cfg_get_device_info(&device);
    time_t t = node->pts / 1000;
    strftime(time, sizeof(time), "%Y-%m-%d %H:%M:%S", localtime(&t));
    /* create Student JSON object */
    cJSON *json = cJSON_CreateObject();
    cJSON *subJson = cJSON_CreateObject();
    /* serialize data to JSON object. */
    cJSON_AddStringToObject(subJson, "devName", device.name);
    cJSON_AddStringToObject(subJson, "devMac", device.mac);
    cJSON_AddStringToObject(subJson, "devSn", device.sn);
    cJSON_AddStringToObject(subJson, "hwVersion", device.hardVersion);
    cJSON_AddStringToObject(subJson, "fwVersion", device.softVersion);
    cJSON_AddNumberToObject(subJson, "battery", misc_get_battery_voltage_rate());
    cJSON_AddNumberToObject(subJson, "batteryVoltage", misc_get_battery_voltage());
    cJSON_AddStringToObject(subJson, "snapType", snapType);
    cJSON_AddStringToObject(subJson, "localtime", time);
    cJSON_AddNumberToObject(subJson, "imageSize", picSize + strlen(header));
    cJSON_AddStringToObject(subJson, "image", mqtt->sendBuf);
    cJSON_AddNumberToObject(json, "ts", node->pts);
    cJSON_AddItemToObject(json, "values", subJson);
    str = cJSON_PrintUnformatted(json);
    // ESP_LOGI(TAG, "mqtt_send_by_json: topic=%s, qos=%d", mqtt->mqtt.topic, mqtt->mqtt.qos);
    if (iot_mip_dm_is_enable()) {
        res = iot_mip_dm_uplink_picture(str);
    } else {
        res = esp_mqtt_client_publish(mqtt->client, mqtt->mqtt.topic, str, 0, mqtt->mqtt.qos, 0);
        if (mqtt->mqtt.qos == 0) {
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }
    cJSON_Delete(json); // delete the cJSON object with all its sub-objects(sub-json)
    cJSON_free(str);
    return res;
}

/**
 * Publish message to MQTT broker
 * @param mqtt MQTT state
 * @param node Queue node containing message data
 * @return ESP_OK on success, ESP_FAIL on error
 */
static esp_err_t mqtt_publish(mdMqtt_t *mqtt, queueNode_t *node)
{
    EventBits_t uxBits;
    if (mqtt->isConnected) {
        if (mqtt_send_by_json(mqtt, node) < 0) {
            return ESP_FAIL;
        }
        if (mqtt->mqtt.qos == 0 || mqtt->mip != NULL) { // mqtt qos 0 or mip http上传到云平台无需异步等待；
            return ESP_OK;
        }
    } else {
        return ESP_FAIL;
    }
    uxBits = xEventGroupWaitBits(g_MQ.eventGroup, MQTT_PUBLISHED_BIT, true, true, pdMS_TO_TICKS(MQTT_PUBLISHED_TIMEOUT_MS));
    if (uxBits & MQTT_PUBLISHED_BIT) {
        return ESP_OK;
    } else {
        return ESP_FAIL;
    }
}

/**
 * MQTT task that processes messages from input queue
 * @param self Pointer to MQTT state
 */
static void task(mdMqtt_t *self)
{
    xEventGroupWaitBits(self->eventGroup, MQTT_CONNECT_BIT | MQTT_TASK_STOP_BIT, true, false,
                        pdMS_TO_TICKS(MQTT_CONNECT_TIMEOUT_MS));

    ESP_LOGI(TAG, "queue receive task running");
    while (true) {
        queueNode_t *node;
        if (xQueueReceive(self->in, &node, portMAX_DELAY)) {
            time_t now;
            struct tm timeinfo;
            time(&now);
            if (node->from == FROM_CAMERA && node->ntp_sync_flag == 0) {
                //If the timestamp of the image is greater than the next system wake-up time, it means that the time error is too large and the image is discarded.
                if (node->type == SNAP_TIMER &&
                    ((node->pts / 1000) > (now + calc_next_snapshot_time() + CAPTURE_ERROR_THRESHOLD_S))) {
                    localtime_r(&now, &timeinfo);
                    ESP_LOGW(TAG, "Discard the currently captured image at: %s", asctime(&timeinfo));
                    node->free_handler(node, EVENT_OK);
                    break;
                } else { //Correct the timestamp of the captured image according to the actual time.
                    node->pts = node->pts + (system_get_time_delta() * 1000);
                }
                node->ntp_sync_flag = system_get_ntp_sync_flag();
            }
            
            // Check upload configuration and system mode to decide upload behavior
            uploadAttr_t upload;
            cfg_get_upload_attr(&upload);
            modeSel_e currentMode = system_get_mode();
            
            if (upload.uploadMode == 0 || currentMode == MODE_UPLOAD) { //
                // Instant upload mode, or upload mode - attempt immediate upload
                ESP_LOGI(TAG, "PUSH ... (mode: %d, uploadMode: %d)", currentMode, upload.uploadMode);
                if (mqtt_publish(self, node) != ESP_OK) {
                    if (self->out) {
                        ESP_LOGI(TAG, "PUSH FAIL, Save to flash");
                        xQueueSend(self->out, &node, portMAX_DELAY);
                    } else {
                        // No storage queue available, free the node  
                        ESP_LOGW(TAG, "PUSH FAIL, No storage queue available");
                        node->free_handler(node, EVENT_FAIL);
                    }
                } else {
                    ESP_LOGI(TAG, "PUSH SUCCESS");
                    node->free_handler(node, EVENT_OK);
                    g_sned_success += 1;
                }
            } else { // Scheduled upload mode
                ESP_LOGI(TAG, "PUSH SKIP (mode: %d, uploadMode: %d)", currentMode, upload.uploadMode);
                if (self->out) {
                    xQueueSend(self->out, &node, portMAX_DELAY);
                } else {
                    // No storage queue available, free the node
                    ESP_LOGW(TAG, "No storage queue available for scheduled upload");
                    node->free_handler(node, EVENT_FAIL);
                }
            }
            g_sned_total += 1;
        }
    }
    ESP_LOGI(TAG, "Stop");
    vTaskDelete(NULL);
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
    {"sendrate", "mqtt send success rate", NULL, do_sendrate_cmd, NULL},
};

void mqtt_open(QueueHandle_t in, QueueHandle_t out)
{
    memset(&g_MQ, 0, sizeof(mdMqtt_t));
    g_MQ.in = in;
    g_MQ.out = out;
    g_MQ.eventGroup = xEventGroupCreate();
    g_MQ.mutex = xSemaphoreCreateMutex();
    g_MQ.sendBuf = malloc(MQTT_SEND_BUFFER_SIZE);
    assert(g_MQ.sendBuf);
    g_MQ.sendBufSize = MQTT_SEND_BUFFER_SIZE;
    xEventGroupClearBits(g_MQ.eventGroup, MQTT_TASK_STOP_BIT);
    xTaskCreatePinnedToCore((TaskFunction_t)task, TAG, 8 * 1024, &g_MQ, 4, NULL, 1);
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
    xEventGroupSetBits(g_MQ.eventGroup, MQTT_TASK_STOP_BIT);
    xSemaphoreTake(g_MQ.mutex, portMAX_DELAY);
    if (iot_mip_dm_is_enable()) {
        iot_mip_dm_stop();
    } else {
        mqtt_esp_stop(&g_MQ);
    }
    g_MQ.isConnected = false;
    xSemaphoreGive(g_MQ.mutex);
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
    if (g_MQ.eventGroup) {
        vEventGroupDelete(g_MQ.eventGroup);
        g_MQ.eventGroup = NULL;
    }
    if (g_MQ.mutex) {
        vSemaphoreDelete(g_MQ.mutex);
        g_MQ.mutex = NULL;
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
        //带了协议头部
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
