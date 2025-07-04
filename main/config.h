#ifndef __CONFIG_H__
#define __CONFIG_H__

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_LEN_3 3
#define MAX_LEN_8 8
#define MAX_LEN_16 16
#define MAX_LEN_32 32
#define MAX_LEN_64 64
#define MAX_LEN_128 128
#define MAX_LEN_256 256
#define MAX_LEN_512 512
#define MAX_LEN_1024 1024

#define KEY_DEVICE_NAME     "dev:name"
#define KEY_DEVICE_MAC      "dev:mac"
#define KEY_DEVICE_SN       "dev:sn"
#define KEY_DEVICE_HVER     "dev:hardVer"
#define KEY_DEVICE_MODEL    "dev:model"
#define KEY_DEVICE_COUNTRY  "dev:country"
#define KEY_DEVICE_NETMOD   "dev:netmod"
#define KEY_DEVICE_SECRETKEY "dev:secretKey"
#define KEY_IMG_BRIGHTNESS  "img:br"
#define KEY_IMG_CONTRAST    "img:cst"
#define KEY_IMG_SATURATION  "img:sat"
#define KEY_IMG_AELEVEL     "img:ae"
#define KEY_IMG_AGC         "img:bAgc"
#define KEY_IMG_GAIN        "img:gain"
#define KEY_IMG_GAINCEILING "img:gceiling"
#define KEY_IMG_HOR         "img:bHor"
#define KEY_IMG_VER         "img:bVer"
#define KEY_LIGHT_MODE      "light:mode"
#define KEY_LIGHT_THRESHOLD "light:thr"
#define KEY_LIGHT_STIME     "light:stime"
#define KEY_LIGHT_ETINE     "light:etime"
#define KEY_LIGHT_DUTY      "light:duty"
#define KEY_CAP_SCHE        "cap:bSche"
#define KEY_CAP_ALARMIN     "cap:bAlarm"
#define KEY_CAP_BUTTON      "cap:bBtn"
#define KEY_CAP_MODE        "cap:sMode"
#define KEY_CAP_TIME_COUNT  "cap:tCount"
#define KEY_CAP_INTERVAL_V  "cap:iValue"
#define KEY_CAP_INTERVAL_U  "cap:iUnit"
#define KEY_PLATFORM_TYPE   "plat:type"
#define KEY_SNS_HTTP_PORT   "sns:httpPort"
#define KEY_MQTT_ENABLE     "mqtt:enable"
#define KEY_MQTT_HOST       "mqtt:host"
#define KEY_MQTT_PORT       "mqtt:port"
#define KEY_MQTT_TOPIC      "mqtt:topic"
#define KEY_MQTT_CLIENT_ID  "mqtt:clientId"
#define KEY_MQTT_QOS        "mqtt:qos"
#define KEY_MQTT_USER       "mqtt:user"
#define KEY_MQTT_PASSWORD   "mqtt:password"
#define KEY_WIFI_SSID       "wifi:ssid"
#define KEY_WIFI_PASSWORD   "wifi:password"
#define KEY_IOT_AUTOP       "iot:autop"
#define KEY_IOT_DM          "iot:dm"
#define KEY_IOT_AUTOP_DONE  "iot:autoPdone"
#define KEY_IOT_DM_DONE     "iot:dmdone"
#define KEY_IOT_RPS_URL     "iot:rpsUrl"
#define KEY_SYS_CRC32       "sys:crc32"
#define KEY_SYS_SCHE_TIME   "sys:scheTime"
#define KEY_SYS_TIME_ZONE   "sys:tz"
#define KEY_SYS_TIME_ERR_RATE "sys:errRate"
#define KEY_CFG_CRC32       "cfg:crc32"
#define KEY_CAT1_IMEI       "cat1:imei"
#define KEY_CAT1_APN        "cat1:apn"
#define KEY_CAT1_USER       "cat1:user"
#define KEY_CAT1_PASSWORD   "cat1:password"
#define KEY_CAT1_PIN        "cat1:pin"
#define KEY_CAT1_AUTH_TYPE  "cat1:authType"
#define KEY_CAT1_BAUD_RATE  "cat1:baudRate"


/**
 * Device information structure
 */
typedef struct deviceInfo {
    char name[MAX_LEN_32];
    char mac[MAX_LEN_32];
    char sn[MAX_LEN_16];
    char hardVersion[MAX_LEN_16];
    char softVersion[MAX_LEN_16];
    char model[MAX_LEN_16];
    char secretKey[MAX_LEN_16];
    char countryCode[MAX_LEN_3];
    char netmod[MAX_LEN_8];
    char camera[MAX_LEN_8];
} deviceInfo_t;

/**
 * Light control attributes structure
 */
typedef struct lightAttr {
    uint8_t lightMode; // 0:atuo 1:customize 2:on 3:off
    uint8_t threshold; // 0-100 use for auto mode
    uint8_t value;  // realtime light sensor value
    char startTime[MAX_LEN_32];
    char endTime[MAX_LEN_32];
    uint8_t duty;
} lightAttr_t;

/**
 * Image processing attributes structure
 */
typedef struct imgAttr {
    int8_t brightness; // -2 ~ 2
    int8_t contrast; // -2 ~ 2
    int8_t saturation; // -2 ~ 2
    int8_t aeLevel; // -2 ~ 2
    uint8_t bAgc; // 0，1
    uint8_t gain; // 0 ~ 30 if agc = 0
    uint8_t gainCeiling; // 0 ~ 6 (2x-128x) if agc = 1
    uint8_t bHorizonetal; // 0
    uint8_t bVertical; // 0
} imgAttr_t;

/**
 * Timed capture node structure
 */
typedef struct timedCapNode {
    uint8_t day; //0:sunday 1: monday, 2: tuesday, 3: wednesday ... 7: everyday,
    char time[MAX_LEN_32]; // xx:xx:xx
} timedCapNode_t;

/**
 * Capture attributes structure
 */
typedef struct capAttr {
    uint8_t bScheCap;
    uint8_t bAlarmInCap;
    uint8_t bButtonCap;
    uint8_t scheCapMode; // 0: timed 1: interval
    uint8_t timedCount; //use for timed mode
    timedCapNode_t timedNodes[8]; //use for timed mode
    uint32_t intervalValue; // use for interval mode
    uint8_t  intervalUnit; // use for interval mode. 0: minutes, 1: hours, 2:day
} capAttr_t;

/**
 * MQTT connection attributes structure
 */
typedef struct mqttAttr {
    char host[MAX_LEN_128];
    char topic[MAX_LEN_128];
    char user[MAX_LEN_64];
    char password[MAX_LEN_64];
    char clientId[MAX_LEN_128];
    uint32_t port;
    uint8_t qos;
    uint32_t httpPort;
} mqttAttr_t;

/**
 * WiFi connection attributes structure
 */
typedef struct wifiAttr {
    char ssid[MAX_LEN_32];
    char password[MAX_LEN_64];
    uint8_t isConnected; // 0: disconnet 1: connected
} wifiAttr_t;

/**
 * Battery status attributes structure
 */
typedef struct batteryAttr {
    uint8_t bBattery;
    uint8_t freePercent;    // 0-100%
} batteryAttr_t;

/**
 * Platform type enumeration
 */
typedef enum {
    PLATFORM_TYPE_SENSING = 0,
    PLATFORM_TYPE_MQTT,
    PLATFORM_TYPE_MAX
} platformType_e;

/**
 * Sensing platform attributes structure
 */
typedef struct sensingPlatformAttr {
    // 感知平台固定值
    uint8_t platformType;
    char platformName[MAX_LEN_32];
    // 可配置参数
    char host[MAX_LEN_128];
    uint32_t mqttPort;
    uint32_t httpPort;
    // 不可配置参数
    char topic[MAX_LEN_128];
    char username[MAX_LEN_64];
    char password[MAX_LEN_64];
    char clientId[MAX_LEN_128];
    uint8_t qos;
} sensingPlatformAttr_t;

/**
 * MQTT platform attributes structure
 */
typedef struct mqttPlatformAttr {
    // MQTT平台固定值
    uint8_t platformType;
    char platformName[MAX_LEN_32];
    // 可配置参数
    char host[MAX_LEN_128];
    uint32_t mqttPort;
    char topic[MAX_LEN_128];
    char clientId[MAX_LEN_128];
    uint8_t qos;
    char username[MAX_LEN_64];
    char password[MAX_LEN_64];
    uint8_t isConnected;
} mqttPlatformAttr_t;

/**
 * Platform parameters structure
 */
typedef struct platformParamAttr {
    uint8_t currentPlatformType;
    sensingPlatformAttr_t sensingPlatform;
    mqttPlatformAttr_t mqttPlatform;
} platformParamAttr_t;

/**
 * IoT service attributes structure
 */
typedef struct IoTAttr {
    uint8_t autop_enable; // 用于开启和关闭Auto-P（RPS）服务
    uint8_t dm_enable; // 用于开启和关闭与开发者平台的远程管理服务
    uint8_t autop_done; // 用于标记Auto-P（RPS）服务Profile是否已经下载完成
    uint8_t dm_done; // 用于标记与开发者平台的远程管理服务Profile是否已经下载完成
} IoTAttr_t;

/**
 * Cellular authentication type enumeration
 */
typedef enum  {
    CELLULAR_AUTH_TYPE_NONE = 0,
    CELLULAR_AUTH_TYPE_PAP,
    CELLULAR_AUTH_TYPE_CHAP,
    CELLULAR_AUTH_TYPE_PAP_OR_CHAP,
    CELLULAR_AUTH_TYPE_MAX
} cellularAuthType_e;

/**
 * Cellular parameters structure
 */
typedef struct cellularParamAttr {
    // 不可配置参数
    char imei[MAX_LEN_32];
    // 可配置参数
    char apn[MAX_LEN_32];
    char user[MAX_LEN_64];
    char password[MAX_LEN_64];
    char pin[MAX_LEN_32];
    uint8_t authentication;
} cellularParamAttr_t;

esp_err_t cfg_init(void);
esp_err_t cfg_deinit();
void cfg_dump();
void cfg_set_u8(const char *key, uint8_t value);
void cfg_set_i8(const char *key, int8_t value);
void cfg_set_u32(const char *key, uint32_t value);
void cfg_set_str(const char *key, const char *value);
void cfg_get_u8(const char *key, uint8_t *value, uint8_t def);
void cfg_get_i8(const char *key, int8_t *value, int8_t def);
void cfg_get_u32(const char *key, uint32_t *value, uint32_t def);
void cfg_get_str(const char *key, char *value, size_t length, const char *def);
void cfg_erase_key(const char *key);

esp_err_t cfg_import(char *data, size_t len);
esp_err_t cfg_user_erase_all();
esp_err_t cfg_set_firmware_crc32(uint32_t crc);
uint32_t cfg_get_firmware_crc32();
esp_err_t cfg_set_config_crc32(uint32_t crc);
uint32_t cfg_get_config_crc32();
esp_err_t cfg_set_schedule_time(char *time);
esp_err_t cfg_get_schedule_time(char *time);
esp_err_t cfg_set_timezone(char *tz);
esp_err_t cfg_get_timezone(char *tz);
esp_err_t cfg_set_time_err_rate(int32_t err_rate);
esp_err_t cfg_get_time_err_rate(int32_t *err_rate);
esp_err_t cfg_get_device_info(deviceInfo_t *device);
esp_err_t cfg_set_device_info(deviceInfo_t *device);
esp_err_t cfg_get_image_attr(imgAttr_t *image);
esp_err_t cfg_set_image_attr(imgAttr_t *image);
esp_err_t cfg_get_light_attr(lightAttr_t *light);
esp_err_t cfg_set_light_attr(lightAttr_t *light);
esp_err_t cfg_get_cap_attr(capAttr_t *capture);
esp_err_t cfg_set_cap_attr(capAttr_t *capture);
esp_err_t cfg_get_mqtt_attr(mqttAttr_t *mqtt);
esp_err_t cfg_set_mqtt_attr(mqttAttr_t *mqtt);
esp_err_t cfg_get_wifi_attr(wifiAttr_t *wifi);
esp_err_t cfg_set_wifi_attr(wifiAttr_t *wifi);
esp_err_t cfg_get_iot_attr(IoTAttr_t *iot);
esp_err_t cfg_set_iot_attr(IoTAttr_t *iot);
esp_err_t cfg_get_platform_param_attr(platformParamAttr_t *platformParam);
esp_err_t cfg_set_platform_param_attr(platformParamAttr_t *platformParam);
esp_err_t cfg_get_cellular_param_attr(cellularParamAttr_t *cellularParam);
esp_err_t cfg_set_cellular_param_attr(cellularParamAttr_t *cellularParam);
esp_err_t cfg_get_cellular_baud_rate(uint32_t *baudRate);
esp_err_t cfg_set_cellular_baud_rate(uint32_t baudRate);
bool cfg_is_undefined(char *value);

#ifdef __cplusplus
}
#endif


#endif /* __CONFIG_H__ */
