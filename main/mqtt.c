/**
 * MQTT Client Implementation
 */
#include <string.h>
#include <time.h>
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_netif.h"
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
#include "push.h"
#include "onboarding.h"

#define MQTT_CONNECT_BIT BIT(2)
#define MQTT_DISCONNECT_BIT BIT(3)
#define MQTT_PUBLISHED_BIT BIT(4)
#define MQTT_PUBLISHED_TIMEOUT_MS (20000)

#define TAG "-->MQTT"

typedef struct mdMqtt {
    EventGroupHandle_t eventGroup;
    mqttAttr_t mqtt;
    void *client;
    bool isConnected;
    SemaphoreHandle_t mutex;
    void *sendBuf;
    size_t sendBufSize;
    esp_mqtt_client_config_t cfg;
    bool useOnboardingDefault;
} mdMqtt_t;

static mdMqtt_t g_MQ = {0};
static esp_timer_handle_t s_restart_timer;

static void mqtt_restart_timer_cb(void *arg)
{
    (void)arg;
    if (sleep_is_entering_deep_sleep()) {
        ESP_LOGI(TAG, "System is entering deep sleep, skipping restart");
        return;
    }
    mqtt_restart();
}

static esp_err_t mqtt_event_handler_cb(esp_mqtt_event_handle_t event, void *handler_args)
{
    mdMqtt_t *mqtt = (mdMqtt_t *)handler_args;

    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
            if (mqtt->useOnboardingDefault) {
                char topic[ONBOARDING_TOPIC_MAX_LEN] = {0};
                if (onboarding_get_default_topic(topic, sizeof(topic)) == ESP_OK) {
                    ESP_LOGI(TAG, "Subscribing to default topic: %s", topic);
                    int msg_id = esp_mqtt_client_subscribe(mqtt->client, topic, 0);
                    if (msg_id < 0) {
                        ESP_LOGE(TAG, "Failed to subscribe to default topic");
                    } else {
                        ESP_LOGI(TAG, "Default topic subscription msg_id=%d", msg_id);
                    }
                }
            } else {
                ESP_LOGI(TAG, "Custom broker, skip onboarding topic subscribe");
                storage_upload_start();
                push_ready();
            }
            mqtt->isConnected = true;
            xEventGroupClearBits(mqtt->eventGroup, MQTT_DISCONNECT_BIT);
            xEventGroupSetBits(mqtt->eventGroup, MQTT_CONNECT_BIT);
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
            xEventGroupClearBits(mqtt->eventGroup, MQTT_CONNECT_BIT);
            xEventGroupSetBits(mqtt->eventGroup, MQTT_DISCONNECT_BIT);
            mqtt->isConnected = false;
            storage_upload_stop();
            push_exit();
            break;
        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
            storage_upload_start();
            push_ready();
            break;
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            xEventGroupSetBits(mqtt->eventGroup, MQTT_PUBLISHED_BIT);
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
            push_exit();
            break;
        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "MQTT_EVENT_DATA, topic=%.*s", event->topic_len, event->topic);
            if (onboarding_is_default_topic(event->topic, event->topic_len)) {
                ESP_LOGI(TAG, "Received config from default topic (%d bytes)", event->data_len);
                bool mqtt_changed = false;
                if (onboarding_handle_config(event->data, event->data_len, &mqtt_changed) == ESP_OK) {
                    if (mqtt_changed) {
                        ESP_LOGI(TAG, "MQTT config changed, restarting with new broker");
                        esp_timer_start_once(s_restart_timer, 1);
                    } else {
                        ESP_LOGI(TAG, "Config applied, no MQTT change needed");
                    }
                }
            }
            break;
        default:
            break;
    }
    return ESP_OK;
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    mqtt_event_handler_cb(event_data, handler_args);
}

char *push_build_json_payload(queueNode_t *node)
{
    esp_err_t res = ESP_OK;
    size_t picSize;
    deviceInfo_t device;
    char *snapType = NULL;
    char time_str[32];
    char upload_time_str[32];
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
    time_t upload_t = time(NULL);
    strftime(upload_time_str, sizeof(upload_time_str), "%Y-%m-%d %H:%M:%S", localtime(&upload_t));

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
    cJSON_AddStringToObject(subJson, "uploadtime", upload_time_str);
    cJSON_AddNumberToObject(subJson, "imageSize", picSize + strlen(header));
    cJSON_AddStringToObject(subJson, "image", (char *)g_MQ.sendBuf);
    cJSON_AddNumberToObject(json, "ts", node->pts);
    cJSON_AddItemToObject(json, "values", subJson);

    char *str = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    return str;
}

esp_err_t mqtt_publish_node(queueNode_t *node)
{
    mqttAttr_t mqtt;

    if (!g_MQ.isConnected) {
        return ESP_FAIL;
    }

    cfg_get_mqtt_attr(&mqtt);
    if (mqtt.topic[0] == '\0') {
        ESP_LOGE(TAG, "MQTT topic is empty");
        return ESP_FAIL;
    }

    char *json_str = push_build_json_payload(node);
    if (json_str == NULL) {
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Publishing to topic: %s, qos: %d", mqtt.topic, mqtt.qos);
    int res = esp_mqtt_client_publish(g_MQ.client, mqtt.topic, json_str, 0, mqtt.qos, 0);
    if (mqtt.qos == 0) {
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    cJSON_free(json_str);

    if (res < 0) {
        return ESP_FAIL;
    }

    if (mqtt.qos == 0) {
        return ESP_OK;
    }

    EventBits_t uxBits = xEventGroupWaitBits(g_MQ.eventGroup, MQTT_PUBLISHED_BIT, true, true,
                                              pdMS_TO_TICKS(MQTT_PUBLISHED_TIMEOUT_MS));
    return (uxBits & MQTT_PUBLISHED_BIT) ? ESP_OK : ESP_FAIL;
}

static void mqtt_esp_config(mdMqtt_t *m)
{
    esp_mqtt_client_config_t *c = &m->cfg;

    memset(c, 0, sizeof(esp_mqtt_client_config_t));
    cfg_get_mqtt_attr(&m->mqtt);
    /* If no user-defined host, fall back to onboarding defaults */
    m->useOnboardingDefault = false;
    if (m->mqtt.host[0] == '\0') {
        snprintf(m->mqtt.host, sizeof(m->mqtt.host), "%s", onboarding_get_default_host());
        m->mqtt.port = onboarding_get_default_port();
        m->useOnboardingDefault = true;
        ESP_LOGI(TAG, "Using default onboarding broker: %s:%lu",
                 m->mqtt.host, (unsigned long)m->mqtt.port);
    }

    strip_mqtt_scheme(m->mqtt.host);
    c->broker.address.hostname = m->mqtt.host;
    c->broker.address.port = m->mqtt.port;
    c->broker.address.transport = m->mqtt.tlsEnable ? MQTT_TRANSPORT_OVER_SSL : MQTT_TRANSPORT_OVER_TCP;
    c->credentials.username = m->mqtt.user;
    c->credentials.client_id = m->mqtt.clientId;
    c->task.stack_size = 12 * 1024;
    c->network.disable_auto_reconnect = true;
    if (strlen(m->mqtt.password)) {
        c->credentials.authentication.password = m->mqtt.password;
    }
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

static void mqtt_free_tls_certs(mdMqtt_t *m)
{
    esp_mqtt_client_config_t *c = &m->cfg;

    if (c->broker.verification.certificate) {
        free((void *)c->broker.verification.certificate);
        c->broker.verification.certificate = NULL;
    }
    if (c->credentials.authentication.certificate) {
        free((void *)c->credentials.authentication.certificate);
        c->credentials.authentication.certificate = NULL;
    }
    if (c->credentials.authentication.key) {
        free((void *)c->credentials.authentication.key);
        c->credentials.authentication.key = NULL;
    }
}

static int8_t mqtt_esp_start(mdMqtt_t *m)
{
    mqtt_esp_config(m);
    m->client = esp_mqtt_client_init(&m->cfg);
    if (!m->client) {
        return -1;
    }
    esp_mqtt_client_register_event(m->client, ESP_EVENT_ANY_ID, mqtt_event_handler, m);
    return esp_mqtt_client_start(m->client);
}

static int8_t mqtt_esp_stop(mdMqtt_t *m)
{
    if (!m->client) {
        return -1;
    }
    esp_mqtt_client_disconnect(m->client);
    esp_mqtt_client_stop(m->client);
    esp_mqtt_client_destroy(m->client);
    mqtt_free_tls_certs(m);
    m->client = NULL;
    m->isConnected = false;
    return 0;
}

static int do_sendrate_cmd(int argc, char **argv)
{
    (void) argc;
    (void) argv;
    return ESP_OK;
}

static esp_console_cmd_t g_cmd[] = {
    ESP_CONSOLE_CMD_INIT("sendrate", "mqtt send success rate", NULL, do_sendrate_cmd, NULL),
};

void mqtt_open(void)
{
    memset(&g_MQ, 0, sizeof(mdMqtt_t));
    const esp_timer_create_args_t restart_timer_args = {
        .callback = mqtt_restart_timer_cb,
        .name = "mqtt_restart",
    };
    ESP_ERROR_CHECK(esp_timer_create(&restart_timer_args, &s_restart_timer));
    g_MQ.eventGroup = xEventGroupCreate();
    g_MQ.mutex = xSemaphoreCreateMutex();
    g_MQ.sendBuf = malloc(PUSH_SEND_BUFFER_SIZE);
    assert(g_MQ.sendBuf);
    g_MQ.sendBufSize = PUSH_SEND_BUFFER_SIZE;
    debug_cmd_add(g_cmd, sizeof(g_cmd) / sizeof(esp_console_cmd_t));
}

void mqtt_start(void)
{
    xSemaphoreTake(g_MQ.mutex, portMAX_DELAY);
    if (g_MQ.client) {
        xSemaphoreGive(g_MQ.mutex);
        return;
    }
    ESP_LOGI(TAG, "mqtt esp start");
    mqtt_esp_start(&g_MQ);
    xSemaphoreGive(g_MQ.mutex);
}

void mqtt_stop(void)
{
    if (s_restart_timer) {
        esp_timer_stop(s_restart_timer);
    }
    xSemaphoreTake(g_MQ.mutex, portMAX_DELAY);
    if (!g_MQ.client) {
        xSemaphoreGive(g_MQ.mutex);
        return;
    }
    mqtt_esp_stop(&g_MQ);
    xSemaphoreGive(g_MQ.mutex);
    ESP_LOGI(TAG, "esp_mqtt_client_stop");
}

void mqtt_restart(void)
{
    xSemaphoreTake(g_MQ.mutex, portMAX_DELAY);
    if (g_MQ.client) {
        mqtt_esp_stop(&g_MQ);
    }
    ESP_LOGI(TAG, "mqtt esp restart");
    mqtt_esp_start(&g_MQ);
    xSemaphoreGive(g_MQ.mutex);
}

void mqtt_close(void)
{
    if (s_restart_timer) {
        esp_timer_stop(s_restart_timer);
        esp_timer_delete(s_restart_timer);
        s_restart_timer = NULL;
    }
    xSemaphoreTake(g_MQ.mutex, portMAX_DELAY);
    if (g_MQ.client) {
        mqtt_esp_stop(&g_MQ);
    }
    xSemaphoreGive(g_MQ.mutex);
    if (g_MQ.sendBuf) {
        free(g_MQ.sendBuf);
        g_MQ.sendBuf = NULL;
    }
}

bool mqtt_is_connected(void)
{
    return g_MQ.isConnected;
}
