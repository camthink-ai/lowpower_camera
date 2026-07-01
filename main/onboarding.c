/**
 * @file onboarding.c
 * @brief HaLowLink device onboarding implementation
 *
 * Default MQTT broker: provisioncamthink.qa.halow.link:1883
 * Default topic:      camera/{cameramac}/default
 *
 * Every MQTT connect subscribes to the default topic.
 * Cloud can push updated config at any time.
 * MQTT settings trigger a reconnect only when they changed.
 * All settings are written to NVS only when values differ from the current config.
 */
#include "onboarding.h"
#include "config.h"
#include "utils.h"
#include "camera.h"
#include "mmregdb.h"
#include "morse.h"
#include "net_module.h"
#include "pir.h"
#include "push.h"
#include "storage.h"
#include "esp_log.h"
#include "cJSON.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>

#define TAG "-->ONBOARDING"

bool onboarding_has_user_config(void)
{
    mqttAttr_t mqtt = {0};
    cfg_get_mqtt_attr(&mqtt);
    return (mqtt.host[0] != '\0');
}

const char* onboarding_get_default_host(void)
{
    return ONBOARDING_DEFAULT_HOST;
}

uint32_t onboarding_get_default_port(void)
{
    return ONBOARDING_DEFAULT_PORT;
}

esp_err_t onboarding_get_default_topic(char *topic, size_t len)
{
    if (!topic || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    deviceInfo_t device;
    esp_err_t err = cfg_get_device_info(&device);
    if (err != ESP_OK || device.mac[0] == '\0') {
        ESP_LOGE(TAG, "Failed to get device MAC");
        return ESP_FAIL;
    }

    /* Convert MAC from xx:xx:xx:xx:xx:xx to lowercase without colons */
    char mac_no_colon[18] = {0};
    size_t j = 0;
    for (size_t i = 0; device.mac[i] != '\0' && j < sizeof(mac_no_colon) - 1; i++) {
        char c = device.mac[i];
        if (c == ':') {
            continue;
        }
        if (c >= 'A' && c <= 'Z') {
            c = (char)(c + ('a' - 'A'));
        }
        mac_no_colon[j++] = c;
    }
    mac_no_colon[j] = '\0';

    int ret = snprintf(topic, len, "%s%s%s", ONBOARDING_TOPIC_PREFIX, mac_no_colon, ONBOARDING_TOPIC_SUFFIX);
    if (ret < 0 || (size_t)ret >= len) {
        ESP_LOGE(TAG, "Topic buffer too small");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Default onboarding topic: %s", topic);
    return ESP_OK;
}

bool onboarding_is_default_topic(const char *topic, size_t topic_len)
{
    char expected[ONBOARDING_TOPIC_MAX_LEN] = {0};
    if (onboarding_get_default_topic(expected, sizeof(expected)) != ESP_OK) {
        return false;
    }
    size_t expected_len = strlen(expected);
    if (topic_len != expected_len) {
        return false;
    }
    return (strncmp(topic, expected, topic_len) == 0);
}

/* ------------------------------------------------------------------ */
/* Helpers for parsing config                                           */

static cJSON *json_item_any(cJSON *root, const char *key, const char *alt_key)
{
    cJSON *item = cJSON_GetObjectItem(root, key);
    if (item != NULL) {
        return item;
    }
    if (alt_key != NULL) {
        return cJSON_GetObjectItem(root, alt_key);
    }
    return NULL;
}

static bool json_read_u8(cJSON *item, uint8_t *out)
{
    if (item == NULL || out == NULL) {
        return false;
    }
    if (cJSON_IsNumber(item)) {
        *out = (uint8_t)item->valueint;
        return true;
    }
    if (cJSON_IsString(item) && item->valuestring != NULL) {
        *out = (uint8_t)atoi(item->valuestring);
        return true;
    }
    if (cJSON_IsBool(item)) {
        *out = cJSON_IsTrue(item) ? 1 : 0;
        return true;
    }
    return false;
}

static bool json_read_i8(cJSON *item, int8_t *out)
{
    if (item == NULL || out == NULL) {
        return false;
    }
    if (cJSON_IsNumber(item)) {
        *out = (int8_t)item->valueint;
        return true;
    }
    if (cJSON_IsString(item) && item->valuestring != NULL) {
        *out = (int8_t)atoi(item->valuestring);
        return true;
    }
    if (cJSON_IsBool(item)) {
        *out = cJSON_IsTrue(item) ? 1 : 0;
        return true;
    }
    return false;
}

static bool json_read_u32(cJSON *item, uint32_t *out)
{
    if (item == NULL || out == NULL) {
        return false;
    }
    if (cJSON_IsNumber(item)) {
        *out = (uint32_t)item->valueint;
        return true;
    }
    if (cJSON_IsString(item) && item->valuestring != NULL) {
        *out = (uint32_t)atoi(item->valuestring);
        return true;
    }
    if (cJSON_IsBool(item)) {
        *out = cJSON_IsTrue(item) ? 1U : 0U;
        return true;
    }
    return false;
}

static bool json_read_float(cJSON *item, float *out)
{
    if (item == NULL || out == NULL) {
        return false;
    }
    if (cJSON_IsNumber(item)) {
        *out = (float)item->valuedouble;
        return true;
    }
    if (cJSON_IsString(item) && item->valuestring != NULL) {
        *out = (float)atof(item->valuestring);
        return true;
    }
    return false;
}

static uint8_t clamp_u8(uint8_t val, uint8_t min_val, uint8_t max_val)
{
    if (val < min_val) {
        return min_val;
    }
    if (val > max_val) {
        return max_val;
    }
    return val;
}

static int8_t clamp_i8(int8_t val, int8_t min_val, int8_t max_val)
{
    if (val < min_val) {
        return min_val;
    }
    if (val > max_val) {
        return max_val;
    }
    return val;
}

static uint32_t clamp_u32(uint32_t val, uint32_t min_val, uint32_t max_val)
{
    if (val < min_val) {
        return min_val;
    }
    if (val > max_val) {
        return max_val;
    }
    return val;
}

/* PIR display values (Web UI semantics) -> hardware register values */
static uint8_t pir_blind_display_to_reg(float display_sec)
{
    if (display_sec < 0.5f) {
        display_sec = 0.5f;
    } else if (display_sec > 8.0f) {
        display_sec = 8.0f;
    }
    int reg = (int)lroundf((display_sec - 0.5f) * 2.0f);
    return clamp_u8((uint8_t)reg, 0, 15);
}

static uint8_t pir_pulse_display_to_reg(int pulse_count)
{
    if (pulse_count < 1) {
        pulse_count = 1;
    } else if (pulse_count > 4) {
        pulse_count = 4;
    }
    return (uint8_t)(pulse_count - 1);
}

static uint8_t pir_window_display_to_reg(float display_sec)
{
    if (display_sec < 2.0f) {
        display_sec = 2.0f;
    } else if (display_sec > 8.0f) {
        display_sec = 8.0f;
    }
    int reg = (int)lroundf((display_sec - 2.0f) / 2.0f);
    return clamp_u8((uint8_t)reg, 0, 3);
}

static bool frame_size_is_allowed(uint8_t frame_size)
{
    static const uint8_t allowed[] = {5, 8, 9, 10, 11, 12, 13, 14, 17, 21};

    for (size_t i = 0; i < sizeof(allowed) / sizeof(allowed[0]); i++) {
        if (allowed[i] == frame_size) {
            return true;
        }
    }
    return false;
}

static bool json_apply_string(cJSON *root, const char *key, const char *alt_key,
                              char *dest, size_t len)
{
    cJSON *item = json_item_any(root, key, alt_key);
    if (item != NULL && cJSON_IsString(item) && item->valuestring != NULL) {
        strncpy(dest, item->valuestring, len - 1);
        dest[len - 1] = '\0';
        return true;
    }
    return false;
}

static bool json_apply_u8(cJSON *root, const char *key, const char *alt_key, uint8_t *dest)
{
    return json_read_u8(json_item_any(root, key, alt_key), dest);
}

static bool json_apply_i8(cJSON *root, const char *key, const char *alt_key, int8_t *dest)
{
    return json_read_i8(json_item_any(root, key, alt_key), dest);
}

static bool json_apply_u32(cJSON *root, const char *key, const char *alt_key, uint32_t *dest)
{
    return json_read_u32(json_item_any(root, key, alt_key), dest);
}

static esp_err_t parse_resolution(const char *res_str, uint8_t *frame_size)
{
    static const struct {
        const char *str;
        uint8_t     fs;
    } map[] = {
        {"2560x1920", 21},
        {"2048x1536", 17},
        {"1920x1080", 14},
        {"1600x1200", 13},
        {"1280x1024", 12},
        {"1280x720",  11},
        {"1024x768",  10},
        {"800x600",    9},
        {"640x480",    8},
        {"320x240",    5},
    };
    for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
        if (strcmp(res_str, map[i].str) == 0) {
            *frame_size = map[i].fs;
            return ESP_OK;
        }
    }
    ESP_LOGW(TAG, "Unknown resolution '%s', keeping current", res_str);
    return ESP_FAIL;
}

static bool parse_light_mode(const char *mode, uint8_t *out)
{
    if (mode == NULL) {
        return false;
    }
    if (strcmp(mode, "auto") == 0) {
        *out = 0;
        return true;
    }
    if (strcmp(mode, "customize") == 0 || strcmp(mode, "custom") == 0) {
        *out = 1;
        return true;
    }
    if (strcmp(mode, "on") == 0) {
        *out = 2;
        return true;
    }
    if (strcmp(mode, "off") == 0) {
        *out = 3;
        return true;
    }
    return false;
}

#define CAP_INTERVAL_MAX 999

static bool is_valid_time_hhmm(const char *time_str)
{
    if (time_str == NULL || strlen(time_str) != 5 ||
        time_str[2] != ':' ||
        time_str[0] < '0' || time_str[0] > '9' ||
        time_str[1] < '0' || time_str[1] > '9' ||
        time_str[3] < '0' || time_str[3] > '9' ||
        time_str[4] < '0' || time_str[4] > '9') {
        return false;
    }

    int hour = (time_str[0] - '0') * 10 + (time_str[1] - '0');
    int minute = (time_str[3] - '0') * 10 + (time_str[4] - '0');
    return (hour >= 0 && hour <= 23 && minute >= 0 && minute <= 59);
}

static bool is_valid_time_hhmmss(const char *time_str)
{
    if (time_str == NULL || strlen(time_str) != 8 ||
        time_str[2] != ':' || time_str[5] != ':' ||
        time_str[0] < '0' || time_str[0] > '9' ||
        time_str[1] < '0' || time_str[1] > '9' ||
        time_str[3] < '0' || time_str[3] > '9' ||
        time_str[4] < '0' || time_str[4] > '9' ||
        time_str[6] < '0' || time_str[6] > '9' ||
        time_str[7] < '0' || time_str[7] > '9') {
        return false;
    }

    int hour = (time_str[0] - '0') * 10 + (time_str[1] - '0');
    int minute = (time_str[3] - '0') * 10 + (time_str[4] - '0');
    int second = (time_str[6] - '0') * 10 + (time_str[7] - '0');
    return (hour >= 0 && hour <= 23 &&
            minute >= 0 && minute <= 59 &&
            second >= 0 && second <= 59);
}

static void sanitize_timed_nodes(timedNode_t *nodes, uint8_t *count, size_t max_nodes)
{
    if (nodes == NULL || count == NULL) {
        return;
    }

    uint8_t valid = 0;
    for (uint8_t i = 0; i < *count && valid < max_nodes; i++) {
        if (!is_valid_time_hhmmss(nodes[i].time)) {
            ESP_LOGW(TAG, "Invalid timed node time '%s', skipping", nodes[i].time);
            continue;
        }
        if (valid != i) {
            nodes[valid] = nodes[i];
        }
        valid++;
    }
    *count = valid;
}

static void parse_capture_interval(const char *in_str, uint32_t *value, uint8_t *unit)
{
    *value = 1;
    *unit  = 0; /* minutes */

    if (!in_str || in_str[0] == '\0') {
        return;
    }

    char buf[64] = {0};
    strncpy(buf, in_str, sizeof(buf) - 1);

    char *token = strtok(buf, " ");
    if (token) {
        *value = (uint32_t)atoi(token);
    }
    token = strtok(NULL, " ");
    if (token) {
        if (strstr(token, "hour") || strstr(token, "Hour")) {
            *unit = 1;
        } else if (strstr(token, "day") || strstr(token, "Day")) {
            *unit = 2;
        }
    }

    *value = clamp_u32(*value, 1, CAP_INTERVAL_MAX);
}

static void parse_timed_nodes(cJSON *arr, timedNode_t *nodes, uint8_t *count, size_t max_nodes)
{
    if (arr == NULL || !cJSON_IsArray(arr) || nodes == NULL || count == NULL) {
        return;
    }

    memset(nodes, 0, max_nodes * sizeof(timedNode_t));

    size_t n = cJSON_GetArraySize(arr);
    if (n > max_nodes) {
        n = max_nodes;
    }

    uint8_t parsed = 0;
    for (size_t i = 0; i < n && parsed < max_nodes; i++) {
        cJSON *item = cJSON_GetArrayItem(arr, i);
        if (item == NULL || !cJSON_IsObject(item)) {
            continue;
        }
        cJSON *day = cJSON_GetObjectItem(item, "day");
        cJSON *time = cJSON_GetObjectItem(item, "time");
        if (time == NULL || !cJSON_IsString(time) || time->valuestring == NULL) {
            ESP_LOGW(TAG, "Timed node missing valid time field, skipping");
            continue;
        }
        if (!is_valid_time_hhmmss(time->valuestring)) {
            ESP_LOGW(TAG, "Invalid timed node time '%s', skipping", time->valuestring);
            continue;
        }

        if (day != NULL) {
            uint8_t day_val;
            if (json_read_u8(day, &day_val)) {
                nodes[parsed].day = clamp_u8(day_val, 0, 7);
            }
        }
        strncpy(nodes[parsed].time, time->valuestring, sizeof(nodes[parsed].time) - 1);
        nodes[parsed].time[sizeof(nodes[parsed].time) - 1] = '\0';
        parsed++;
    }
    *count = parsed;
}

static bool timed_nodes_differs(const timedNode_t *a, const timedNode_t *b, uint8_t count)
{
    for (uint8_t i = 0; i < count; i++) {
        if (a[i].day != b[i].day || strcmp(a[i].time, b[i].time) != 0) {
            return true;
        }
    }
    return false;
}

static bool mqtt_attr_differs(const mqttAttr_t *a, const mqttAttr_t *b)
{
    return (strcmp(a->host, b->host) != 0) ||
           (a->port != b->port) ||
           (strcmp(a->topic, b->topic) != 0) ||
           (strcmp(a->clientId, b->clientId) != 0) ||
           (strcmp(a->user, b->user) != 0) ||
           (strcmp(a->password, b->password) != 0) ||
           (a->qos != b->qos) ||
           (a->tlsEnable != b->tlsEnable);
}

static bool img_attr_differs(const imgAttr_t *a, const imgAttr_t *b)
{
    return (a->frameSize != b->frameSize) ||
           (a->quality != b->quality) ||
           (a->brightness != b->brightness) ||
           (a->contrast != b->contrast) ||
           (a->saturation != b->saturation) ||
           (a->aeLevel != b->aeLevel) ||
           (a->bAgc != b->bAgc) ||
           (a->gain != b->gain) ||
           (a->gainCeiling != b->gainCeiling) ||
           (a->bHorizonetal != b->bHorizonetal) ||
           (a->bVertical != b->bVertical) ||
           (a->hdrEnable != b->hdrEnable);
}

static bool light_attr_differs(const lightAttr_t *a, const lightAttr_t *b)
{
    return (a->lightMode != b->lightMode) ||
           (a->threshold != b->threshold) ||
           (a->duty != b->duty) ||
           (strcmp(a->startTime, b->startTime) != 0) ||
           (strcmp(a->endTime, b->endTime) != 0);
}

static bool cap_attr_differs(const capAttr_t *a, const capAttr_t *b)
{
    if (a->bScheCap != b->bScheCap ||
        a->bAlarmInCap != b->bAlarmInCap ||
        a->bButtonCap != b->bButtonCap ||
        a->scheCapMode != b->scheCapMode ||
        a->intervalValue != b->intervalValue ||
        a->intervalUnit != b->intervalUnit ||
        a->camWarmupMs != b->camWarmupMs ||
        a->timedCount != b->timedCount ||
        strcmp(a->intervalAnchorTime, b->intervalAnchorTime) != 0) {
        return true;
    }
    return timed_nodes_differs(a->timedNodes, b->timedNodes, a->timedCount);
}

static bool upload_attr_differs(const uploadAttr_t *a, const uploadAttr_t *b)
{
    if (a->uploadMode != b->uploadMode ||
        a->retryCount != b->retryCount ||
        a->timedCount != b->timedCount) {
        return true;
    }
    return timed_nodes_differs(a->timedNodes, b->timedNodes, a->timedCount);
}

static bool webhook_attr_differs(const webhookAttr_t *a, const webhookAttr_t *b)
{
    return (strcmp(a->url, b->url) != 0) ||
           (strcmp(a->header, b->header) != 0);
}

static bool pir_attr_differs(const pirAttr_t *a, const pirAttr_t *b)
{
    return (a->sens != b->sens) ||
           (a->blind != b->blind) ||
           (a->pulse != b->pulse) ||
           (a->window != b->window);
}

static void sanitize_mqtt_attr(mqttAttr_t *mqtt)
{
    strip_mqtt_scheme(mqtt->host);
    mqtt->port = clamp_u32(mqtt->port, 1, 65535);
    mqtt->qos = clamp_u8(mqtt->qos, 0, 2);
    mqtt->tlsEnable = mqtt->tlsEnable ? 1 : 0;
}

static void apply_mqtt_config(cJSON *root, mqttAttr_t *mqtt)
{
    (void)json_apply_string(root, "mqtt_host", "host", mqtt->host, sizeof(mqtt->host));
    (void)json_apply_string(root, "topic", NULL, mqtt->topic, sizeof(mqtt->topic));
    (void)json_apply_string(root, "client_id", "clientId", mqtt->clientId, sizeof(mqtt->clientId));
    (void)json_apply_string(root, "mqtt_user", "user", mqtt->user, sizeof(mqtt->user));
    (void)json_apply_string(root, "mqtt_password", "password", mqtt->password, sizeof(mqtt->password));
    (void)json_apply_u8(root, "mqtt_qos", "qos", &mqtt->qos);
    (void)json_apply_u8(root, "mqtt_tls_enable", "tlsEnable", &mqtt->tlsEnable);

    cJSON *j_port = json_item_any(root, "mqtt_port", "port");
    if (j_port != NULL) {
        (void)json_read_u32(j_port, &mqtt->port);
    }

    sanitize_mqtt_attr(mqtt);
}

static void sanitize_image_attr(imgAttr_t *img, const imgAttr_t *fallback)
{
    if (!frame_size_is_allowed(img->frameSize)) {
        img->frameSize = fallback->frameSize;
    }

    img->quality = clamp_u8(img->quality, 0, 63);
    img->brightness = clamp_i8(img->brightness, -2, 2);
    img->contrast = clamp_i8(img->contrast, -2, 2);
    img->saturation = clamp_i8(img->saturation, -2, 2);
    img->aeLevel = clamp_i8(img->aeLevel, -2, 2);
    img->bAgc = img->bAgc ? 1 : 0;
    if (img->gain != 64) {
        img->gain = clamp_u8(img->gain, 0, 30);
    }
    img->gainCeiling = clamp_u8(img->gainCeiling, 0, 6);
    img->bHorizonetal = img->bHorizonetal ? 1 : 0;
    img->bVertical = img->bVertical ? 1 : 0;
    img->hdrEnable = img->hdrEnable ? 1 : 0;

    if (img->frameSize < FRAMESIZE_INVALID) {
        camera_apply_jpeg_quality_limit((framesize_t)img->frameSize, &img->quality);
    }
}

static void apply_image_config(cJSON *root, imgAttr_t *img, const imgAttr_t *fallback)
{
    cJSON *j_resolution = json_item_any(root, "resolution", NULL);
    if (j_resolution != NULL && cJSON_IsString(j_resolution) && j_resolution->valuestring != NULL) {
        uint8_t fs;
        if (parse_resolution(j_resolution->valuestring, &fs) == ESP_OK) {
            img->frameSize = fs;
        }
    }

    (void)json_apply_u8(root, "image_quality", "quality", &img->quality);
    (void)json_apply_u8(root, "frameSize", NULL, &img->frameSize);
    (void)json_apply_i8(root, "brightness", NULL, &img->brightness);
    (void)json_apply_i8(root, "contrast", NULL, &img->contrast);
    (void)json_apply_i8(root, "saturation", NULL, &img->saturation);
    (void)json_apply_i8(root, "aeLevel", NULL, &img->aeLevel);
    (void)json_apply_u8(root, "bAgc", NULL, &img->bAgc);
    (void)json_apply_u8(root, "gain", NULL, &img->gain);
    (void)json_apply_u8(root, "gainCeiling", NULL, &img->gainCeiling);
    (void)json_apply_u8(root, "bHorizonetal", "horizontal_mirror", &img->bHorizonetal);
    (void)json_apply_u8(root, "bVertical", "vertical_flip", &img->bVertical);
    (void)json_apply_u8(root, "hdr_enable", "hdrEnable", &img->hdrEnable);

    sanitize_image_attr(img, fallback);
}

static void sanitize_light_attr(lightAttr_t *light)
{
    light->lightMode = clamp_u8(light->lightMode, 0, 3);
    light->threshold = clamp_u8(light->threshold, 0, 100);
    light->duty = clamp_u8(light->duty, 0, 100);
    if (light->startTime[0] != '\0' && !is_valid_time_hhmm(light->startTime)) {
        ESP_LOGW(TAG, "Invalid light_start_time '%s', resetting to 00:00", light->startTime);
        strncpy(light->startTime, "00:00", sizeof(light->startTime) - 1);
        light->startTime[sizeof(light->startTime) - 1] = '\0';
    }
    if (light->endTime[0] != '\0' && !is_valid_time_hhmm(light->endTime)) {
        ESP_LOGW(TAG, "Invalid light_end_time '%s', resetting to 00:00", light->endTime);
        strncpy(light->endTime, "00:00", sizeof(light->endTime) - 1);
        light->endTime[sizeof(light->endTime) - 1] = '\0';
    }
}

static void apply_light_config(cJSON *root, lightAttr_t *light)
{
    cJSON *j_mode = json_item_any(root, "light_mode", "lightMode");
    if (j_mode != NULL) {
        if (cJSON_IsString(j_mode) && j_mode->valuestring != NULL) {
            uint8_t mode;
            if (parse_light_mode(j_mode->valuestring, &mode)) {
                light->lightMode = mode;
            }
        } else {
            (void)json_read_u8(j_mode, &light->lightMode);
        }
    }

    (void)json_apply_u8(root, "light_threshold", "threshold", &light->threshold);
    (void)json_apply_u8(root, "light_brightness", "duty", &light->duty);
    (void)json_apply_string(root, "light_start_time", "startTime", light->startTime, sizeof(light->startTime));
    (void)json_apply_string(root, "light_end_time", "endTime", light->endTime, sizeof(light->endTime));

    sanitize_light_attr(light);
}

static void sanitize_capture_attr(capAttr_t *cap)
{
    cap->bScheCap = cap->bScheCap ? 1 : 0;
    cap->bAlarmInCap = cap->bAlarmInCap ? 1 : 0;
    cap->bButtonCap = cap->bButtonCap ? 1 : 0;
    cap->scheCapMode = cap->scheCapMode ? 1 : 0;
    cap->intervalUnit = clamp_u8(cap->intervalUnit, 0, 2);
    cap->intervalValue = clamp_u32(cap->intervalValue, 1, CAP_INTERVAL_MAX);
    if (cap->intervalAnchorTime[0] != '\0' &&
        !is_valid_time_hhmm(cap->intervalAnchorTime)) {
        ESP_LOGW(TAG, "Invalid intervalAnchorTime '%s', resetting to 00:00",
                 cap->intervalAnchorTime);
        strncpy(cap->intervalAnchorTime, "00:00", sizeof(cap->intervalAnchorTime) - 1);
        cap->intervalAnchorTime[sizeof(cap->intervalAnchorTime) - 1] = '\0';
    }
    cap->timedCount = clamp_u8(cap->timedCount, 0, 8);
    sanitize_timed_nodes(cap->timedNodes, &cap->timedCount,
                         sizeof(cap->timedNodes) / sizeof(cap->timedNodes[0]));
}

static void apply_capture_config(cJSON *root, capAttr_t *cap)
{
    cJSON *j_cap_mode = json_item_any(root, "capture_mode", NULL);
    if (j_cap_mode != NULL && cJSON_IsString(j_cap_mode) && j_cap_mode->valuestring != NULL) {
        const char *mode = j_cap_mode->valuestring;
        if (strcmp(mode, "off") == 0 || strcmp(mode, "disabled") == 0) {
            cap->bScheCap = 0;
        } else {
            cap->bScheCap = 1;
            cap->scheCapMode = (strcmp(mode, "interval") == 0) ? 1 : 0;
        }
    }

    cJSON *j_cap_interval = json_item_any(root, "capture_interval", NULL);
    if (j_cap_interval != NULL) {
        if (cJSON_IsString(j_cap_interval) && j_cap_interval->valuestring != NULL) {
            uint32_t ivalue;
            uint8_t iunit;
            parse_capture_interval(j_cap_interval->valuestring, &ivalue, &iunit);
            cap->intervalValue = ivalue;
            cap->intervalUnit = iunit;
        } else if (cJSON_IsNumber(j_cap_interval)) {
            cap->intervalValue = clamp_u32((uint32_t)j_cap_interval->valueint, 1, CAP_INTERVAL_MAX);
        }
    }

    (void)json_apply_u8(root, "bScheCap", NULL, &cap->bScheCap);
    (void)json_apply_u8(root, "bAlarmInCap", NULL, &cap->bAlarmInCap);
    (void)json_apply_u8(root, "bButtonCap", NULL, &cap->bButtonCap);
    (void)json_apply_u8(root, "scheCapMode", NULL, &cap->scheCapMode);
    (void)json_apply_u32(root, "intervalValue", NULL, &cap->intervalValue);
    (void)json_apply_u8(root, "intervalUnit", NULL, &cap->intervalUnit);
    (void)json_apply_u32(root, "camWarmupMs", NULL, &cap->camWarmupMs);
    (void)json_apply_string(root, "intervalAnchorTime", NULL, cap->intervalAnchorTime,
                            sizeof(cap->intervalAnchorTime));

    cJSON *j_timed = json_item_any(root, "capture_timed_nodes", "timedNodes");
    if (j_timed != NULL) {
        parse_timed_nodes(j_timed, cap->timedNodes, &cap->timedCount,
                          sizeof(cap->timedNodes) / sizeof(cap->timedNodes[0]));
    } else {
        (void)json_apply_u8(root, "timedCount", NULL, &cap->timedCount);
    }

    sanitize_capture_attr(cap);
}

static void sanitize_upload_attr(uploadAttr_t *upload)
{
    upload->uploadMode = clamp_u8(upload->uploadMode, 0, 1);
    upload->timedCount = clamp_u8(upload->timedCount, 0, 10);
    sanitize_timed_nodes(upload->timedNodes, &upload->timedCount,
                         sizeof(upload->timedNodes) / sizeof(upload->timedNodes[0]));
}

static void apply_upload_config(cJSON *root, uploadAttr_t *upload)
{
    cJSON *j_mode = json_item_any(root, "upload_mode", "uploadMode");
    if (j_mode != NULL) {
        if (cJSON_IsString(j_mode) && j_mode->valuestring != NULL) {
            if (strcmp(j_mode->valuestring, "scheduled") == 0 ||
                strcmp(j_mode->valuestring, "schedule") == 0) {
                upload->uploadMode = 1;
            } else {
                upload->uploadMode = 0;
            }
        } else {
            (void)json_read_u8(j_mode, &upload->uploadMode);
        }
    }

    (void)json_apply_u8(root, "upload_retry", "retryCount", &upload->retryCount);

    cJSON *j_timed = json_item_any(root, "upload_timed_nodes", "uploadTimedNodes");
    if (j_timed != NULL) {
        parse_timed_nodes(j_timed, upload->timedNodes, &upload->timedCount,
                          sizeof(upload->timedNodes) / sizeof(upload->timedNodes[0]));
    } else {
        (void)json_apply_u8(root, "upload_timed_count", NULL, &upload->timedCount);
    }

    sanitize_upload_attr(upload);
}

static void sanitize_trigger_config(uint8_t *trigger_mode, pirAttr_t *pir)
{
    if (*trigger_mode > TRIGGER_MODE_PIR) {
        *trigger_mode = TRIGGER_MODE_DISABLED;
    }
    pir->sens = clamp_u8(pir->sens, 0, 255);
    pir->blind = clamp_u8(pir->blind & 0x0F, 0, 15);
    pir->pulse = clamp_u8(pir->pulse & 0x03, 0, 3);
    pir->window = clamp_u8(pir->window & 0x03, 0, 3);
}

static void apply_trigger_config(cJSON *root, uint8_t *trigger_mode, pirAttr_t *pir)
{
    cJSON *j_mode = json_item_any(root, "trigger_mode", NULL);
    if (j_mode != NULL) {
        uint8_t mode;
        if (json_read_u8(j_mode, &mode)) {
            if (mode > TRIGGER_MODE_PIR) {
                mode = TRIGGER_MODE_DISABLED;
            }
            *trigger_mode = mode;
        }
    }

    cJSON *j_sens = cJSON_GetObjectItem(root, "sens");
    if (j_sens != NULL) {
        uint8_t val;
        if (json_read_u8(j_sens, &val)) {
            pir->sens = val;
        }
    }
    cJSON *j_blind = cJSON_GetObjectItem(root, "blind");
    if (j_blind != NULL) {
        float display_sec;
        if (json_read_float(j_blind, &display_sec)) {
            pir->blind = pir_blind_display_to_reg(display_sec);
        }
    }
    cJSON *j_pulse = cJSON_GetObjectItem(root, "pulse");
    if (j_pulse != NULL) {
        float display_count;
        if (json_read_float(j_pulse, &display_count)) {
            pir->pulse = pir_pulse_display_to_reg((int)lroundf(display_count));
        }
    }
    cJSON *j_window = cJSON_GetObjectItem(root, "window");
    if (j_window != NULL) {
        float display_sec;
        if (json_read_float(j_window, &display_sec)) {
            pir->window = pir_window_display_to_reg(display_sec);
        }
    }

    sanitize_trigger_config(trigger_mode, pir);
}

/* ------------------------------------------------------------------ */
esp_err_t onboarding_handle_config(const char *data, size_t len, bool *mqtt_changed)
{
    if (!data || len == 0) {
        ESP_LOGE(TAG, "Empty config data");
        return ESP_ERR_INVALID_ARG;
    }
    if (!mqtt_changed) {
        ESP_LOGE(TAG, "mqtt_changed output param is required");
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_ParseWithLength(data, len);
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse JSON payload");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "onboarding_handle_config: root: %s", cJSON_Print(root));
    *mqtt_changed = false;
    bool push_mode_changed = false;
    bool upload_mode_changed = false;
    uint8_t new_upload_mode = 0;

    /* --- MQTT --- */
    mqttAttr_t old_mqtt;
    mqttAttr_t new_mqtt;
    cfg_get_mqtt_attr(&old_mqtt);
    memcpy(&new_mqtt, &old_mqtt, sizeof(new_mqtt));
    apply_mqtt_config(root, &new_mqtt);
    *mqtt_changed = mqtt_attr_differs(&old_mqtt, &new_mqtt);
    if (*mqtt_changed) {
        cfg_set_mqtt_attr(&new_mqtt);
    }

    /* --- Image --- */
    imgAttr_t old_img;
    imgAttr_t img;
    cfg_get_image_attr(&old_img);
    memcpy(&img, &old_img, sizeof(img));
    apply_image_config(root, &img, &old_img);
    if (img_attr_differs(&old_img, &img)) {
        if (camera_set_image(&img) == ESP_OK) {
            cfg_set_image_attr(&img);
        } else {
            ESP_LOGW(TAG, "Failed to apply image settings to camera");
        }
    }

    /* --- Light --- */
    lightAttr_t old_light;
    lightAttr_t light;
    cfg_get_light_attr(&old_light);
    memcpy(&light, &old_light, sizeof(light));
    apply_light_config(root, &light);
    if (light_attr_differs(&old_light, &light)) {
        cfg_set_light_attr(&light);
    }

    /* --- Capture --- */
    capAttr_t old_cap;
    capAttr_t cap;
    cfg_get_cap_attr(&old_cap);
    memcpy(&cap, &old_cap, sizeof(cap));
    apply_capture_config(root, &cap);
    if (cap_attr_differs(&old_cap, &cap)) {
        cfg_set_cap_attr(&cap);
    }

    /* --- Upload --- */
    uploadAttr_t old_upload;
    uploadAttr_t upload;
    cfg_get_upload_attr(&old_upload);
    memcpy(&upload, &old_upload, sizeof(upload));
    apply_upload_config(root, &upload);
    if (upload_attr_differs(&old_upload, &upload)) {
        new_upload_mode = upload.uploadMode;
        upload_mode_changed = true;
        cfg_set_upload_attr(&upload);
    }

    /* --- Trigger / PIR --- */
    uint8_t old_trigger_mode = TRIGGER_MODE_DISABLED;
    uint8_t trigger_mode = TRIGGER_MODE_DISABLED;
    pirAttr_t old_pir;
    pirAttr_t pir;
    cfg_get_trigger_mode(&old_trigger_mode);
    cfg_get_pir_attr(&old_pir);
    trigger_mode = old_trigger_mode;
    memcpy(&pir, &old_pir, sizeof(pir));
    apply_trigger_config(root, &trigger_mode, &pir);

    /* Keep trigger_mode consistent with bAlarmInCap when that field is present */
    if (json_item_any(root, "bAlarmInCap", NULL) != NULL) {
        if (!cap.bAlarmInCap) {
            trigger_mode = TRIGGER_MODE_DISABLED;
        } else if (json_item_any(root, "trigger_mode", NULL) == NULL &&
                   trigger_mode == TRIGGER_MODE_DISABLED) {
            trigger_mode = TRIGGER_MODE_ALARM;
        }
        sanitize_trigger_config(&trigger_mode, &pir);
    }

    /* PIR parameter fields imply PIR mode when trigger_mode is omitted */
    if (json_item_any(root, "trigger_mode", NULL) == NULL && cap.bAlarmInCap &&
        (cJSON_GetObjectItem(root, "sens") != NULL ||
         cJSON_GetObjectItem(root, "blind") != NULL ||
         cJSON_GetObjectItem(root, "pulse") != NULL ||
         cJSON_GetObjectItem(root, "window") != NULL)) {
        trigger_mode = TRIGGER_MODE_PIR;
        sanitize_trigger_config(&trigger_mode, &pir);
    }

    if (trigger_mode != old_trigger_mode) {
        cfg_set_trigger_mode(trigger_mode);
    }
    if (pir_attr_differs(&old_pir, &pir)) {
        cfg_set_pir_attr(&pir);
    }
    if (trigger_mode == TRIGGER_MODE_PIR &&
        (trigger_mode != old_trigger_mode || pir_attr_differs(&old_pir, &pir))) {
        pir_update_config();
    }

    /* --- Webhook --- */
    webhookAttr_t old_webhook;
    webhookAttr_t webhook;
    cfg_get_webhook_attr(&old_webhook);
    memcpy(&webhook, &old_webhook, sizeof(webhook));
    (void)json_apply_string(root, "webhook_url", "url", webhook.url, sizeof(webhook.url));
    (void)json_apply_string(root, "webhook_header", "header", webhook.header, sizeof(webhook.header));
    if (webhook_attr_differs(&old_webhook, &webhook)) {
        cfg_set_webhook_attr(&webhook);
    }

    /* --- Push mode --- */
    uint8_t old_push_mode = 0;
    uint8_t push_mode = 0;
    cfg_get_u8(KEY_PUSH_MODE, &old_push_mode, 0);
    push_mode = old_push_mode;
    cJSON *j_push = json_item_any(root, "push_mode", NULL);
    if (j_push != NULL && json_read_u8(j_push, &push_mode)) {
        push_mode = clamp_u8(push_mode, 0, 1);
        if (push_mode != old_push_mode) {
            cfg_set_u8(KEY_PUSH_MODE, push_mode);
            push_mode_changed = true;
        }
    }

    /* --- Device info --- */
    deviceInfo_t old_device;
    deviceInfo_t device;
    cfg_get_device_info(&old_device);
    memcpy(&device, &old_device, sizeof(device));
    (void)json_apply_string(root, "device_name", "name", device.name, sizeof(device.name));
    if (json_apply_string(root, "country_code", "countryCode", device.countryCode,
                          sizeof(device.countryCode))) {
        if (strcmp(device.countryCode, "AU") == 0) {
            strncpy(device.countryCode, "AU-2020", sizeof(device.countryCode));
        }
        if (mmregdb_lookup_domain(device.countryCode) == NULL) {
            ESP_LOGW(TAG, "Invalid country_code '%s', keeping current value", device.countryCode);
            strncpy(device.countryCode, old_device.countryCode, sizeof(device.countryCode));
            device.countryCode[sizeof(device.countryCode) - 1] = '\0';
        }
    }
    if (strcmp(device.name, old_device.name) != 0 ||
        strcmp(device.countryCode, old_device.countryCode) != 0) {
        cfg_set_device_info(&device);
        if (strcmp(device.countryCode, old_device.countryCode) != 0 &&
            mmregdb_lookup_domain(device.countryCode) != NULL &&
            netModule_is_mmwifi()) {
            mm_wifi_set_country_code(device.countryCode);
        }
    }

    /* --- WiFi credentials --- */
    wifiAttr_t old_wifi;
    wifiAttr_t wifi;
    cfg_get_wifi_attr(&old_wifi);
    memcpy(&wifi, &old_wifi, sizeof(wifi));
    (void)json_apply_string(root, "wifi_ssid", "ssid", wifi.ssid, sizeof(wifi.ssid));
    (void)json_apply_string(root, "wifi_password", NULL, wifi.password, sizeof(wifi.password));
    if (strcmp(wifi.ssid, old_wifi.ssid) != 0 ||
        strcmp(wifi.password, old_wifi.password) != 0) {
        cfg_set_wifi_attr(&wifi);
    }

    /* --- NTP sync --- */
    uint8_t old_ntp = 0;
    uint8_t ntp_sync = 0;
    cfg_get_ntp_sync(&old_ntp);
    ntp_sync = old_ntp;
    if (json_apply_u8(root, "ntp_sync", "ntpSync", &ntp_sync)) {
        ntp_sync = ntp_sync ? 1 : 0;
        if (ntp_sync != old_ntp) {
            cfg_set_ntp_sync(ntp_sync);
        }
    }

    if (upload_mode_changed) {
        if (new_upload_mode == 0) {
            storage_upload_start();
        } else {
            storage_upload_stop();
        }
    }
    if (push_mode_changed) {
        push_restart();
    }

    cJSON_Delete(root);
    ESP_LOGI(TAG, "Config handled. MQTT changed: %s", *mqtt_changed ? "yes" : "no");
    return ESP_OK;
}
