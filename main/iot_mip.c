#include "config.h"
#include "debug.h"
#include "system.h"
#include "storage.h"
#include "utils.h"
#include "system.h"
#include "mbedtls/md5.h"
#include "iot_mip.h"
#include "pthread.h"
#include "esp_timer.h"

/* Logging tag for MIP module */
#define TAG "-->IOT_MIP"

/* Event group bits for MIP operations */
#define MIP_AUTOP_START_BIT BIT(0)    // Auto-provisioning start flag
#define MIP_DM_START_BIT BIT(1)       // Device management start flag  
#define MIP_API_TOKEN_BIT BIT(2)      // API token received flag

/* Queue node structure for async operations */
typedef struct qNode {
    int8_t (*cb)(void *param);    // Callback function
    void *param;                  // Callback parameter
    mip_after_cb after_cb;        // Completion callback
} qNode_t;

/* Configuration remapping structure */
typedef struct remap {
    char *upload_name;            // Name used in uploads
    char *local_name;             // Local configuration key
    void (*apply_cb)(char *local_name, cJSON *root);  // Apply config callback
    void (*fetch_cb)(char *local_name, cJSON *root, char *defval); // Fetch config callback
    char *fetch_defval;           // Default value for fetch
} remap_t;

/* MIP module attributes structure */
typedef struct iot_mip_attr {
    QueueHandle_t taskQueue;      // Queue for async tasks
    EventGroupHandle_t eventGroup; // Event group for signaling
    pthread_mutex_t mutex;        // General mutex
    pthread_mutex_t time_mutex;   // Time-related mutex
    esp_timer_handle_t timer;     // Timer handle
    int8_t timeout_sec;           // Timeout in seconds
    bool autop_enable;            // Auto-provisioning enabled flag
    bool dm_enable;               // Device management enabled flag
    bool autop_started;           // Auto-provisioning started flag
    bool dm_started;              // Device management started flag
    bool autop_done;              // Auto-provisioning completed flag
    bool dm_done;                 // Device management completed flag
    char sn[32];                  // Device serial number
    char endpoint[64];            // API endpoint
    char accessToken[64];         // API access token
    char rps_url[128];            // RPS (Remote Provisioning Service) URL
} iot_mip_attr_t;

static iot_mip_attr_t g_iot_mip_attr = {0};

//Use the mbedtls library to calculate the md5 value, input and ilen, open up output space and return output and olen
static int8_t get_md5sum(const unsigned char *input, size_t ilen, const unsigned char *key, size_t klen,
                         unsigned char **output, size_t *olen)
{
    if (md5_calc(input, ilen, output)) {
        return -1;
    }
    if (olen) {
        *olen = 32;
    }
    return 0;
}

static void apply_i8_value(char *local_name, cJSON *root)
{
    int8_t value = 0;
    cJSON *item = root;
    if (item) {
        value = item->valueint;
        cfg_set_i8(local_name, value);
        ESP_LOGI(TAG, "%s: %d", local_name, value);
    }
}

static void apply_u8_value(char *local_name, cJSON *root)
{
    uint8_t value = 0;
    cJSON *item = root;
    if (item) {
        value = item->valueint;
        cfg_set_u8(local_name, value);
        ESP_LOGI(TAG, "%s: %d", local_name, value);
    }
}

static void apply_u32_value(char *local_name, cJSON *root)
{
    uint32_t value = 0;
    cJSON *item = root;
    if (item) {
        value = item->valueint;
        cfg_set_u32(local_name, value);
        ESP_LOGI(TAG, "%s: %lu", local_name, value);
    }
}

static void apply_str_value(char *local_name, cJSON *root)
{
    char *value = NULL;
    cJSON *item = root;
    if (item) {
        value = item->valuestring;
        cfg_set_str(local_name, value);
        ESP_LOGI(TAG, "%s: %s", local_name, value);
    }
}

static void apply_timed_value(char *local_name, cJSON *root)
{
    int array_size = cJSON_GetArraySize(root);
    char key[32];
    int8_t hour, minute;
    char time_str[12] = {0};

    for (int i = 0; i < array_size; i++) {
        cJSON *value_array = cJSON_GetArrayItem(root, i);
        for (int j = 0; j < cJSON_GetArraySize(value_array); j++) {
            cJSON *sub_array = cJSON_GetArrayItem(value_array, j);
            cJSON *sub_key = cJSON_GetObjectItem(sub_array, "key");
            cJSON *sub_value = cJSON_GetObjectItem(sub_array, "value");
            if (sub_key && sub_value) {
                if (strcmp(sub_key->valuestring, "timed_day") == 0) {
                    sprintf(key, "cap:t%d.day", i);
                    cfg_set_u8(key, sub_value->valueint);
                    ESP_LOGI(TAG, "%s: %d", key, sub_value->valueint);
                } else if (strcmp(sub_key->valuestring, "timed_time") == 0) {
                    hour = sub_value->valueint / 3600;
                    minute = (sub_value->valueint % 3600) / 60;
                    snprintf(time_str, sizeof(time_str), "%02d:%02d:00", hour, minute);
                    sprintf(key, "cap:t%d.time", i);
                    cfg_set_str(key, time_str);
                    ESP_LOGI(TAG, "%s: %s", key, time_str);
                }
            }
        }
    }
    cfg_set_u8(KEY_CAP_TIME_COUNT, array_size);
    ESP_LOGI(TAG, "timed count: %d", array_size);
}

static void apply_time2str_value(char *local_name, cJSON *root)
{
    cJSON *item = root;
    //Convert seconds to a string, 82800 -> "23:00"
    if (item) {
        int value = item->valueint;
        int8_t hour = value / 3600;
        int8_t minute = (value % 3600) / 60;
        char time_str[10] = {0};
        snprintf(time_str, sizeof(time_str), "%02d:%02d", hour, minute);
        cfg_set_str(local_name, time_str);
        ESP_LOGI(TAG, "%s: %s", local_name, time_str);
    }
}

static void apply_tz_value(char *local_name, cJSON *root)
{
    const char *tz[] = {"UTC12", "UTC11", "UTC10", "UTC9:30", "UTC9",
                        "UTC8", "UTC7", "UTC6", "UTC5", "UTC4",
                        "UTC3:30", "UTC3", "UTC2", "UTC1", "UTC0",
                        "UTC-1", "UTC-2", "UTC-3", "UTC-3:30", "UTC-4",
                        "UTC-4:30", "UTC-5", "UTC-5:30", "UTC-5:45", "UTC-6",
                        "UTC-6:30", "UTC-7", "UTC-8", "UTC-8:45", "UTC-9",
                        "UTC-9:30", "UTC-10", "UTC-10:30", "UTC-11", "UTC-12",
                        "UTC-12:45", "UTC-13", "UTC-14"
                       };
    cJSON *item = root;
    if (item) {
        int value = item->valueint;
        system_set_timezone(tz[value]);
        cfg_set_str(local_name, tz[value]);
        ESP_LOGI(TAG, "timezone: %s", tz[value]);
    }
}

static void fetch_i8_value(char *local_name, cJSON *root, char *defval)
{
    int8_t value = 0;
    int8_t def = atoi(defval);

    if (root) {
        cfg_get_i8(local_name, &value, def);
        cJSON_AddNumberToObject(root, "value", value);
    }
}

static void fetch_u8_value(char *local_name, cJSON *root, char *defval)
{
    uint8_t value = 0;
    uint8_t def = atoi(defval);

    if (root) {
        cfg_get_u8(local_name, &value, def);
        cJSON_AddNumberToObject(root, "value", value);
    }
}

static void fetch_u32_value(char *local_name, cJSON *root, char *defval)
{
    uint32_t value = 0;
    uint32_t def = atoi(defval);

    if (root) {
        cfg_get_u32(local_name, &value, def);
        cJSON_AddNumberToObject(root, "value", value);
    }
}

static void fetch_str_value(char *local_name, cJSON *root, char *defval)
{
    char value[64] = {0};

    if (root) {
        cfg_get_str(local_name, value, sizeof(value), defval);
        cJSON_AddStringToObject(root, "value", value);
    }
}

static void fetch_str2time_value(char *local_name, cJSON *root, char *defval)
{
    char value[10] = {0};
    char *p = NULL;
    int hour, minute;

    if (root) {
        cfg_get_str(local_name, value, sizeof(value), defval);
        p = strtok(value, ":");
        if (p) {
            hour = atoi(p);
            p = strtok(NULL, ":");
            if (p) {
                minute = atoi(p);
                cJSON_AddNumberToObject(root, "value", hour * 3600 + minute * 60);
            }
        }
    }
}

static void fecth_tz_value(char *local_name, cJSON *root, char *defval)
{
    const char *tz[] = {"UTC12", "UTC11", "UTC10", "UTC9:30", "UTC9",
                        "UTC8", "UTC7", "UTC6", "UTC5", "UTC4",
                        "UTC3:30", "UTC3", "UTC2", "UTC1", "UTC0",
                        "UTC-1", "UTC-2", "UTC-3", "UTC-3:30", "UTC-4",
                        "UTC-4:30", "UTC-5", "UTC-5:30", "UTC-5:45", "UTC-6",
                        "UTC-6:30", "UTC-7", "UTC-8", "UTC-8:45", "UTC-9",
                        "UTC-9:30", "UTC-10", "UTC-10:30", "UTC-11", "UTC-12",
                        "UTC-12:45", "UTC-13", "UTC-14"
                       };
    char value[32] = {0};

    if (root) {
        cfg_get_str(local_name, value, sizeof(value), defval);
        for (int i = 0; i < sizeof(tz) / sizeof(char *); i++) {
            if (strcmp(value, tz[i]) == 0) {
                cJSON_AddNumberToObject(root, "value", i);
                break;
            }
        }
    }
}

static void fetch_timed_value(char *local_name, cJSON *root, char *defval)
{
    uint8_t count = 0;
    uint8_t value = 0;
    cJSON *array = NULL;
    cJSON *sub_arry = NULL;
    cJSON *item = NULL;
    char key[32];
    char time_str[12] = {0};
    int8_t hour, minute;

    if (root) {
        cfg_get_u8(KEY_CAP_TIME_COUNT, &count, 0);
        array = cJSON_CreateArray();
        for (int i = 0; i < count; i++) {
            sub_arry = cJSON_CreateArray();
            item = cJSON_CreateObject();
            sprintf(key, "cap:t%d.day", i);
            cfg_get_u8(key, &value, 0);
            cJSON_AddItemToObject(item, "key", cJSON_CreateString("timed_day"));
            cJSON_AddNumberToObject(item, "value", value);
            cJSON_AddItemToArray(sub_arry, item);
            item = cJSON_CreateObject();
            sprintf(key, "cap:t%d.time", i);
            cfg_get_str(key, time_str, sizeof(time_str), defval);
            hour = atoi(time_str);
            minute = atoi(time_str + 3);
            cJSON_AddItemToObject(item, "key", cJSON_CreateString("timed_time"));
            cJSON_AddNumberToObject(item, "value", hour * 3600 + minute * 60);
            cJSON_AddItemToArray(sub_arry, item);
            cJSON_AddItemToArray(array, sub_arry);
        }
        cJSON_AddItemToObject(root, "value", array);
    }

}

remap_t g_remap[] = {
    {"dm_enable", KEY_IOT_DM, apply_u8_value, fetch_u8_value, "1"},
    {"autop_enable", KEY_IOT_AUTOP, apply_u8_value, fetch_u8_value, "1"},
    {"light_mode", KEY_LIGHT_MODE, apply_u8_value, fetch_u8_value, "0"},
    {"light_start_time", KEY_LIGHT_STIME, apply_time2str_value, fetch_str2time_value, "23:00"},
    {"light_end_time", KEY_LIGHT_ETINE, apply_time2str_value, fetch_str2time_value, "07:00"},
    {"light_threshold", KEY_LIGHT_THRESHOLD, apply_u8_value, fetch_u8_value, "55"},
    {"image_brightness", KEY_IMG_BRIGHTNESS, apply_i8_value, fetch_i8_value, "0"},
    {"image_contrast", KEY_IMG_CONTRAST, apply_i8_value, fetch_i8_value, "0"},
    {"image_saturation", KEY_IMG_SATURATION, apply_i8_value, fetch_i8_value, "0"},
    {"image_flip_horizontal", KEY_IMG_HOR, apply_u8_value, fetch_u8_value, "0"},
    {"image_flip_vertical", KEY_IMG_VER, apply_u8_value, fetch_u8_value, "0"},
    {"capture_enable_schedule", KEY_CAP_SCHE, apply_u8_value, fetch_u8_value, "0"},
    {"capture_mode", KEY_CAP_MODE, apply_u8_value, fetch_u8_value, "0"},
    {"capture_interval_time", KEY_CAP_INTERVAL_V, apply_u32_value, fetch_u32_value, "8"},
    {"capture_interval_unit", KEY_CAP_INTERVAL_U, apply_u8_value, fetch_u8_value, "1"},
    {"capture_timed", NULL, apply_timed_value, fetch_timed_value, "00:00"},
    {"capture_enable_alarmin", KEY_CAP_ALARMIN, apply_u8_value, fetch_u8_value, "1"},
    {"capture_enable_button", KEY_CAP_BUTTON, apply_u8_value, fetch_u8_value, "1"},
    {"platform_type", KEY_PLATFORM_TYPE, apply_u8_value, fetch_u8_value, "0"},
    {"platfrom_mqtt_host", KEY_MQTT_HOST, apply_str_value, fetch_str_value, ""},
    {"platfrom_mqtt_port", KEY_MQTT_PORT, apply_u32_value, fetch_u32_value, "1883"},
    {"platfrom_sns_httpport", KEY_SNS_HTTP_PORT, apply_u32_value, fetch_u32_value, "5220"},
    {"platfrom_mqtt_topic", KEY_MQTT_TOPIC, apply_str_value, fetch_str_value, "v1/devices/me/telemetry"},
    {"platfrom_mqtt_clientid", KEY_MQTT_CLIENT_ID, apply_str_value, fetch_str_value, ""},
    {"platfrom_mqtt_qos", KEY_MQTT_QOS, apply_u8_value, fetch_u8_value, "1"},
    {"platfrom_mqtt_user", KEY_MQTT_USER, apply_str_value, fetch_str_value, ""},
    {"platfrom_mqtt_password", KEY_MQTT_PASSWORD, apply_str_value, fetch_str_value, ""},
    {"device_name", KEY_DEVICE_NAME, apply_str_value, fetch_str_value, "NE101 Sensing Camera"},
    {"device_timezone", KEY_SYS_TIME_ZONE, apply_tz_value, fecth_tz_value, "UTC"},
    {"cat1_user", KEY_CAT1_USER, apply_str_value, fetch_str_value, ""},
    {"cat1_password", KEY_CAT1_PASSWORD, apply_str_value, fetch_str_value, ""},
    {"cat1_apn", KEY_CAT1_APN, apply_str_value, fetch_str_value, ""},
    {"cat1_pin", KEY_CAT1_PIN, apply_str_value, fetch_str_value, ""},
    {"cat1_auth_type", KEY_CAT1_AUTH_TYPE, apply_u8_value, fetch_u8_value, "0"},
};

// --------------------iot mip--------------------
static void timer_cb(void *arg)
{
    iot_mip_attr_t *attr = (iot_mip_attr_t *)arg;

    pthread_mutex_lock(&attr->time_mutex);
    if (attr->timeout_sec > 0) {
        attr->timeout_sec--;
        if (attr->timeout_sec == 0) {
            ESP_LOGI(TAG, "mip timer timeout");
            sleep_set_event_bits(SLEEP_MIP_DONE_BIT);
        }
    }
    pthread_mutex_unlock(&attr->time_mutex);
}

static void mip_timer_pause()
{
    pthread_mutex_lock(&g_iot_mip_attr.time_mutex);
    g_iot_mip_attr.timeout_sec = -1;
    sleep_clear_event_bits(SLEEP_MIP_DONE_BIT);
    pthread_mutex_unlock(&g_iot_mip_attr.time_mutex);
}

static void mip_timer_resume(int8_t sec)
{
    pthread_mutex_lock(&g_iot_mip_attr.time_mutex);
    g_iot_mip_attr.timeout_sec = sec;
    pthread_mutex_unlock(&g_iot_mip_attr.time_mutex);
}

static void mip_timer_start()
{
    static bool timer_started = false;
    if (timer_started) {
        return;
    }
    ESP_LOGI(TAG, "mip_timer_start");
    const esp_timer_create_args_t timer_args = {
        timer_cb,
        &g_iot_mip_attr,
        ESP_TIMER_TASK,
        "wifi_timer",
        true,
    };
    g_iot_mip_attr.timeout_sec = 3;
    sleep_clear_event_bits(SLEEP_MIP_DONE_BIT);
    esp_timer_create(&timer_args, &g_iot_mip_attr.timer);
    esp_timer_start_periodic(g_iot_mip_attr.timer, 1000 * 1000); //1s
    timer_started = true;
}

static void mip_task(void *param)
{
    ESP_LOGI(TAG, "mip_task");
    while (1) {
        qNode_t *node = NULL;
        if (xQueueReceive(g_iot_mip_attr.taskQueue, &node, portMAX_DELAY) == pdTRUE) {
            if (node->cb) {
                node->cb(node->param);
            }
            if (node->after_cb) {
                node->after_cb();
            }
            free(node);
        }
    }
    vTaskDelete(NULL);
}

int8_t iot_mip_autop_async_start(mip_after_cb after_cb)
{
    qNode_t *node = (qNode_t *)malloc(sizeof(qNode_t));
    if (node == NULL) {
        return -1;
    }
    node->cb = iot_mip_autop_start;
    node->param = NULL;
    node->after_cb = after_cb;
    xQueueSend(g_iot_mip_attr.taskQueue, &node, portMAX_DELAY);
    return 0;
}

int8_t iot_mip_dm_async_start(mip_after_cb after_cb)
{
    qNode_t *node = (qNode_t *)malloc(sizeof(qNode_t));
    if (node == NULL) {
        return -1;
    }
    node->cb = iot_mip_dm_start;
    node->param = NULL;
    node->after_cb = after_cb;
    xQueueSend(g_iot_mip_attr.taskQueue, &node, portMAX_DELAY);
    return 0;
}

int8_t iot_mip_dm_async_stop(mip_after_cb after_cb)
{
    qNode_t *node = (qNode_t *)malloc(sizeof(qNode_t));
    if (node == NULL) {
        return -1;
    }
    node->cb = iot_mip_dm_stop;
    node->param = NULL;
    node->after_cb = after_cb;
    xQueueSend(g_iot_mip_attr.taskQueue, &node, portMAX_DELAY);
    return 0;
}

int8_t iot_mip_autop_async_stop(mip_after_cb after_cb)
{
    qNode_t *node = (qNode_t *)malloc(sizeof(qNode_t));
    if (node == NULL) {
        return -1;
    }
    node->cb = iot_mip_autop_stop;
    node->param = NULL;
    node->after_cb = after_cb;
    xQueueSend(g_iot_mip_attr.taskQueue, &node, portMAX_DELAY);
    return 0;
}

int8_t iot_mip_init()
{
    IoTAttr_t iot;

    memset(&g_iot_mip_attr, 0, sizeof(g_iot_mip_attr));

    g_iot_mip_attr.taskQueue = xQueueCreate(4, sizeof(qNode_t *));
    g_iot_mip_attr.eventGroup = xEventGroupCreate();
    g_iot_mip_attr.mutex = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
    g_iot_mip_attr.time_mutex = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
    cfg_get_iot_attr(&iot);

    g_iot_mip_attr.autop_enable = iot.autop_enable;
    g_iot_mip_attr.dm_enable = iot.dm_enable;
    g_iot_mip_attr.autop_done = iot.autop_done;
    g_iot_mip_attr.dm_done = iot.dm_done;
    cfg_get_str(KEY_IOT_RPS_URL, g_iot_mip_attr.rps_url, sizeof(g_iot_mip_attr.rps_url), RPS_HTTP_URL);
    ESP_LOGI(TAG, "rps url: %s", g_iot_mip_attr.rps_url);
    iot_mip_autop_init();
    iot_mip_dm_init();
    sleep_set_event_bits(SLEEP_MIP_DONE_BIT);
    xTaskCreatePinnedToCore(mip_task, "mip_task", 1024 * 10, NULL, 5, NULL, 1);
    return 0;
}

int8_t iot_mip_deinit()
{
    iot_mip_autop_deinit();
    iot_mip_dm_deinit();
    vEventGroupDelete(g_iot_mip_attr.eventGroup);
    vQueueDelete(g_iot_mip_attr.taskQueue);
    return 0;
}

// --------------------autop--------------------
static bool autop_is_done()
{
    return g_iot_mip_attr.autop_done;
}

static void autop_resp_got(char *resp)
{
    ESP_LOGI(TAG, "autop_resp_got: %s", resp);
}

static void profile_apply(char *profile)
{
    cJSON *root = cJSON_Parse(profile);
    cJSON *item = cJSON_GetObjectItem(root, "values");
    if (item) {
        for (int i = 0; i < cJSON_GetArraySize(item); i++) {
            cJSON *subitem = cJSON_GetArrayItem(item, i);
            cJSON *key = cJSON_GetObjectItem(subitem, "key");
            cJSON *value = cJSON_GetObjectItem(subitem, "value");
            ESP_LOGI(TAG, "----------->key: %s", key->valuestring);
            if (key && value) {
                for (int j = 0; j < sizeof(g_remap) / sizeof(remap_t); j++) {
                    if (g_remap[j].upload_name && strcmp(key->valuestring, g_remap[j].upload_name) == 0) {
                        g_remap[j].apply_cb(g_remap[j].local_name, value);
                    }
                }
            }
        }
    }
    cJSON_Delete(root);
}

static void profile_fetch(char **profile)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "version", "v1.0");
    cJSON *values = cJSON_AddArrayToObject(root, "values");
    for (int i = 0; i < sizeof(g_remap) / sizeof(remap_t); i++) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "key", g_remap[i].upload_name);
        g_remap[i].fetch_cb(g_remap[i].local_name, item, g_remap[i].fetch_defval);
        cJSON_AddItemToArray(values, item);
    }
    *profile = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
}

static int8_t autop_pofile_downloaded()
{
    ESP_LOGI(TAG, "autop profile downloaded");
    IoTAttr_t iot;
    char *profile = filesystem_read(MIP_AUTOP_PROFILE_PATH);
    if (profile == NULL) {
        ESP_LOGE(TAG, "autop profile is NULL");
        return -1;
    }
    pthread_mutex_lock(&g_iot_mip_attr.mutex);
    profile_apply(profile);
    free(profile);
    cfg_get_iot_attr(&iot);
    iot.autop_done = 1;
    cfg_set_iot_attr(&iot);
    pthread_mutex_unlock(&g_iot_mip_attr.mutex);
    return 0;
}

int8_t iot_mip_autop_init()
{
    header_sign_t sign = {0};
    http_cb_t httpcbs = {0};
    deviceInfo_t dev;

    cfg_get_device_info(&dev);
    memset(&sign, 0, sizeof(sign));

    strncpy(sign.sn, dev.sn, sizeof(sign.sn) - 1);
    if (!cfg_is_undefined(dev.secretKey)) {
        strncpy(sign.sec_key, dev.secretKey, sizeof(sign.sec_key) - 1);
    }
    strncpy(sign.type, "MD5", sizeof(sign.type) - 1);

    // sign.get_timestamp_cb = get_timestamp;
    sign.get_signature_cb = get_md5sum;

    httpcbs.http_send_req = http_client_send_req;
    httpcbs.http_download_file = http_client_download_file;
    httpcbs.http_upload_file = http_client_upload_file;

    if (mip_init(&sign, &httpcbs) != 0) {
        ESP_LOGE(TAG, "mip_init failed");
        return ESP_FAIL;
    }
    return ESP_OK;
}

int8_t iot_mip_autop_deinit()
{
    return ESP_OK;
}

int8_t iot_mip_autop_enable(bool enable)
{
    if (g_iot_mip_attr.autop_enable == enable) {
        return ESP_OK;
    }
    if (enable) {
        iot_mip_autop_async_start(NULL);
    } else {
        iot_mip_autop_stop();
    }
    g_iot_mip_attr.autop_enable = enable;
    return ESP_OK;
}

int8_t iot_mip_autop_start()
{
    if (g_iot_mip_attr.autop_started == true) {
        return ESP_OK;
    }
    profile_cb_t cbs = {0};
    rps_resp_t profile_resp = {0};

    if (autop_is_done()) {
        ESP_LOGI(TAG, "autop profile get from local");
    } else {
        ESP_LOGI(TAG, "autop profile get from server");
        cbs.got_resp = autop_resp_got;
        cbs.downloaded = autop_pofile_downloaded;
        if (mip_get_device_profile(g_iot_mip_attr.rps_url, MIP_AUTOP_PROFILE_PATH, &cbs, &profile_resp)) {
            ESP_LOGE(TAG, "mip get profile failed");
            return ESP_FAIL;
        }
    }
    g_iot_mip_attr.autop_started = true;
    ESP_LOGI(TAG, "mip autop start");
    return ESP_OK;
}

int8_t iot_mip_autop_stop()
{
    if (g_iot_mip_attr.autop_started == false) {
        return ESP_OK;
    }
    // filesystem_delete(MIP_AUTOP_PROFILE_PATH);
    g_iot_mip_attr.autop_started = false;
    ESP_LOGI(TAG, "mip autop stop");
    return ESP_OK;
}

bool iot_mip_autop_is_enable()
{
    return g_iot_mip_attr.autop_enable;
}

//--------------------dm--------------------
static void dm_timestamp(dm_downlink_header_t dh, cJSON *ddata, dm_downlink_result_t *dres, char **udata)
{
    timeAttr_t tAttr = {0};
    ESP_LOGI(TAG, "dm_timestamp");
    cJSON *values = NULL;
    values = cJSON_GetObjectItem(ddata, "seconds");
    if (values) {
        system_get_time(&tAttr);
        tAttr.ts = values->valueint;
        system_set_time(&tAttr);
    } else {
        ESP_LOGI(TAG, "dm_timestamp failed to use ntp");
        system_ntp_time(false);
    }
    snprintf(dres->status, sizeof(dres->status), DM_DOWNLINK_RES_SUCCESS);
}

static void dm_upgrade(dm_downlink_header_t dh, cJSON *ddata, dm_downlink_result_t *dres, char **udata)
{
    ESP_LOGI(TAG, "dm_upgrade");

    snprintf(dres->status, sizeof(dres->status), DM_DOWNLINK_RES_SUCCESS);
}

static void dm_profile_update(dm_downlink_header_t dh, cJSON *ddata, dm_downlink_result_t *dres, char **udata)
{
    ESP_LOGI(TAG, "dm_profile_update");
    profile_t profile = {0};
    int retry_cnt = 0;
    char *content = NULL;
    int ret = 0;
    char *path = "/littlefs/profile_update.json";

    mip_timer_pause();
    pthread_mutex_lock(&g_iot_mip_attr.mutex);
    memset(&profile, 0, sizeof(profile_t));
    cJSON *values = cJSON_GetObjectItem(ddata, "url");
    if (values) {
        strncpy(profile.url, values->valuestring, sizeof(profile.url) - 1);
    }
    values = cJSON_GetObjectItem(ddata, "md5");
    if (values) {
        strncpy(profile.md5, values->valuestring, sizeof(profile.md5) - 1);
    }
    values = cJSON_GetObjectItem(ddata, "crc32");
    if (values) {
        strncpy(profile.crc32, values->valuestring, sizeof(profile.crc32) - 1);
    }
    values = cJSON_GetObjectItem(ddata, "filesize");
    if (values) {
        profile.filesize = values->valueint;
    }

    if (!strlen(profile.url)) {
        dres->err_code = ERR_NULL_URL;
        snprintf(dres->status, sizeof(dres->status), DM_DOWNLINK_RES_FAILED);
        snprintf(dres->err_msg, sizeof(dres->err_msg), "%s", mip_get_err_msg(ERR_NULL_URL));
        ESP_LOGI(TAG, "profile_update_cb: url is null");
        pthread_mutex_unlock(&g_iot_mip_attr.mutex);
        mip_timer_resume(3);
        return;
    }
    for (int i = 0 ; i < strlen(profile.md5) ; i++) {
        profile.md5[i] = tolower(profile.md5[i]);
    }
    do {
        ret = http_client_download_file(profile.url, path, 60, profile.filesize, profile.md5, profile.crc32);
        if (ret) {
            ESP_LOGW(TAG, "profile_update_cb: profile file download fail, retry_cnt: %d", retry_cnt);
            sleep(2);
        }
    } while (ret && retry_cnt++ < 3);
    if (ret) {
        dres->err_code = ERR_RESOURCE_DOWNLOAD_FAILED;
        snprintf(dres->status, sizeof(dres->status), DM_DOWNLINK_RES_FAILED);
        snprintf(dres->err_msg, sizeof(dres->err_msg), "%s", mip_get_err_msg(ERR_RESOURCE_DOWNLOAD_FAILED));
        ESP_LOGE(TAG, "profile_update_cb: profile file download fail");
        pthread_mutex_unlock(&g_iot_mip_attr.mutex);
        mip_timer_resume(3);
        return;
    }
    content = filesystem_read(path);
    if (content) {
        ESP_LOGD(TAG, "profile content:\n %s", content);
        snprintf(dres->status, sizeof(dres->status), DM_DOWNLINK_RES_SUCCESS);
        profile_apply(content);
        free(content);
    }
    pthread_mutex_unlock(&g_iot_mip_attr.mutex);
    unlink(path);
    mip_timer_resume(3);
}

static void dm_profile_get(dm_downlink_header_t dh, cJSON *ddata, dm_downlink_result_t *dres, char **udata)
{
    ESP_LOGI(TAG, "dm_profile_get");
    char *buf = NULL;
    mip_timer_pause();
    pthread_mutex_lock(&g_iot_mip_attr.mutex);
    profile_fetch(&buf);
    pthread_mutex_unlock(&g_iot_mip_attr.mutex);

    *udata = buf;
    snprintf(dres->status, sizeof(dres->status), DM_DOWNLINK_RES_SUCCESS);
    mip_timer_resume(3);
}

static void dm_api_token(dm_downlink_header_t dh, cJSON *ddata, dm_downlink_result_t *dres, char **udata)
{
    cJSON *values = NULL;

    ESP_LOGI(TAG, "dm_api_token");
    values = cJSON_GetObjectItem(ddata, "accessToken");
    if (values) {
        strncpy(g_iot_mip_attr.accessToken, values->valuestring, sizeof(g_iot_mip_attr.accessToken) - 1);
    }
    values = cJSON_GetObjectItem(ddata, "endpoint");
    if (values) {
        strncpy(g_iot_mip_attr.endpoint, values->valuestring, sizeof(g_iot_mip_attr.endpoint) - 1);
    }
    snprintf(dres->status, sizeof(dres->status), DM_DOWNLINK_RES_SUCCESS);
    ESP_LOGI(TAG, "accessToken: %s, endpoint: %s", g_iot_mip_attr.accessToken, g_iot_mip_attr.endpoint);
    xEventGroupSetBits(g_iot_mip_attr.eventGroup, MIP_API_TOKEN_BIT);
}

static void dm_wake_up(dm_downlink_header_t dh, cJSON *ddata, dm_downlink_result_t *dres, char **udata)
{
    ESP_LOGI(TAG, "dm_wake_up");
    iot_mip_dm_response_wake_up();
    snprintf(dres->status, sizeof(dres->status), DM_DOWNLINK_RES_SUCCESS);
}

static void dm_connecet_status(int status)
{
    ESP_LOGI(TAG, "dm_connecet_status: %d", status);
    if (status == true) {
        // iot_mip_dm_request_timestamp();
        iot_mip_dm_request_api_token();
        iot_mip_dm_response_wake_up();
    }
    iot_mip_dm_done();
}

static bool dm_is_done()
{
    return g_iot_mip_attr.dm_done;
}

static void dm_resp_got(char *resp)
{
    if (resp == NULL) {
        ESP_LOGE(TAG, "dm_resp is NULL");
        return;
    }

    filesystem_write(MIP_DM_RESP_PATH, resp, strlen(resp));
    ESP_LOGI(TAG, "got_dm_profile_resp: %s", resp);
}

static int8_t dm_downloaded()
{
    IoTAttr_t iot;
    cfg_get_iot_attr(&iot);
    iot.dm_done = 1;
    cfg_set_iot_attr(&iot);
    ESP_LOGI(TAG, "dm_profile_downloaded");
    return 0;
}

int8_t iot_mip_dm_init()
{
    dm_cb_t dmcbs = {0};
    mqtt_cb_t mqttcbs = {0};

    dmcbs.timestamp = dm_timestamp;
    dmcbs.upgrade = dm_upgrade;
    dmcbs.profile_update  = dm_profile_update;
    dmcbs.api_token = dm_api_token;
    dmcbs.wake_up = dm_wake_up;
    dmcbs.profile_get = dm_profile_get;
    dmcbs.mip_dm_update_con_status = dm_connecet_status;

    mqttcbs.mqtt_start = mqtt_mip_start;
    mqttcbs.mqtt_stop = mqtt_mip_stop;
    mqttcbs.mqtt_is_connected = mqtt_mip_is_connected;
    mqttcbs.mqtt_publish = mqtt_mip_publish;
    mqttcbs.mqtt_get_timestamp = get_timestamp;

    return mip_dm_init(&dmcbs, &mqttcbs);
}

int8_t iot_mip_dm_deinit()
{
    return ESP_OK;
}

int8_t iot_mip_dm_enable(bool enable)
{
    if (g_iot_mip_attr.dm_enable == enable) {
        return ESP_OK;
    }

    g_iot_mip_attr.dm_enable = enable;

    if (!enable) {
        IoTAttr_t iot;
        g_iot_mip_attr.dm_done = 0;
        cfg_get_iot_attr(&iot);
        iot.dm_done = 0;
        g_iot_mip_attr.dm_started = false;
        cfg_set_iot_attr(&iot);
    }

    return ESP_OK;
}

int8_t iot_mip_dm_start()
{
    if (g_iot_mip_attr.dm_started == true) {
        ESP_LOGI(TAG, "dm has started");
        return ESP_OK;
    }
    profile_cb_t cbs = {0};
    rps_resp_t profile_resp = {0};
    dm_resp_t dm_resp = {0};
    char *dm_json = NULL;
    dm_profile_path_t dm_path = {
        .mqtt_ca_cert_path = MIP_MQTT_CA_CERT_PATH,
        .mqtt_cert_path = MIP_MQTT_CERT_PATH,
        .mqtt_prikey_path = MIP_MQTT_KEY_PATH,
    };

    if (dm_is_done()) {
        dm_json = filesystem_read(MIP_DM_RESP_PATH);
        if (j2s_dm_resp(dm_json, &dm_resp) == 0) {
            free(dm_json);
            ESP_LOGI(TAG, "dm resp get from local");
        }
    } else {
        ESP_LOGI(TAG, "dm resp get from server");
        if (mip_get_source_profile(g_iot_mip_attr.rps_url, &cbs, &profile_resp)) {
            ESP_LOGE(TAG, "mip get source profile failed");
            return ESP_FAIL;
        }

        cbs.got_resp = dm_resp_got;
        cbs.downloaded = dm_downloaded;
        if (mip_get_dm_profile(profile_resp.data.source.host, profile_resp.data.source.type, &dm_path, &cbs, &dm_resp)) {
            ESP_LOGE(TAG, "mip get dm profile failed");
            return ESP_FAIL;
        }
    }

    if (mip_dm_start(&dm_resp, &dm_path)) {
        ESP_LOGE(TAG, "mip dm start failed");
        return ESP_FAIL;
    }
    g_iot_mip_attr.dm_started = true;
    ESP_LOGI(TAG, "mip dm start");
    return ESP_OK;

}

int8_t iot_mip_dm_stop()
{
    if (g_iot_mip_attr.dm_started == false) {
        return ESP_OK;
    }
    iot_mip_dm_request_sleep();
    if (mip_dm_stop()) {
        ESP_LOGE(TAG, "mip dm stop failed");
        return ESP_FAIL;
    }
    g_iot_mip_attr.dm_started = false;
    ESP_LOGI(TAG, "mip dm stop");
    return ESP_OK;
}

void iot_mip_dm_done(void)
{
    xEventGroupSetBits(g_iot_mip_attr.eventGroup, MIP_DM_START_BIT);
}

int8_t iot_mip_dm_pending(int32_t timeout_ms)
{
    return xEventGroupWaitBits(g_iot_mip_attr.eventGroup, MIP_DM_START_BIT, pdTRUE, pdTRUE, pdMS_TO_TICKS(timeout_ms));
}

bool iot_mip_dm_is_enable()
{
    return g_iot_mip_attr.dm_enable;
}

int8_t iot_mip_dm_request_timestamp()
{
    mip_dm_uplink(NULL, NULL, "request_timestamp", "");
    return 0;
}

int8_t iot_mip_dm_request_profile()
{
    mip_dm_uplink(NULL, NULL, "request_profile", "");
    return 0;
}

int8_t iot_mip_dm_request_api_token()
{
    mip_dm_uplink(NULL, NULL, "request_api_token", "");
    return 0;
}

int8_t iot_mip_dm_request_sleep()
{
    mip_dm_uplink(NULL, NULL, "sleep", "");
    return 0;
}

int8_t iot_mip_dm_response_wake_up()
{
    // topic: iot/v1/device/{SN}/uplink/wake_up
    mip_timer_start();
    mip_dm_uplink(NULL, NULL, "wake_up", "");
    return 0;
}

int8_t iot_mip_dm_uplink_picture(const char *msg)
{
    // mip_dm_uplink(NULL, NULL, "property", msg);
    // return 0;
    xEventGroupWaitBits(g_iot_mip_attr.eventGroup, MIP_API_TOKEN_BIT, pdFALSE, pdTRUE, pdMS_TO_TICKS(10000));
    const char *url = g_iot_mip_attr.endpoint;
    const char *token = g_iot_mip_attr.accessToken;

    if (strlen(url) == 0 || strlen(token) == 0) {
        ESP_LOGE(TAG, "url or token is invalid");
        return -1;
    }
    return mip_dm_uplink_http(url, token, msg);
}
