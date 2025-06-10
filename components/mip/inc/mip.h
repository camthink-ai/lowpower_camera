#ifndef __MIP_H__
#define __MIP_H__

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef LOG_PRINTF
#define LOG_PRINTF(fmt, args...)    printf("%s:%d: "fmt, __FUNCTION__, __LINE__, ## args);fflush(stdout)
#endif

#ifndef MIP_SLEEP
#define MIP_SLEEP(s)                sleep(s)
#endif

#ifndef MIP_USLEEP
#define MIP_USLEEP(us)              usleep(us)
#endif

#ifndef MIP_RANDOM
#define MIP_RANDOM()                random()
#endif

#define RPS_HTTP_URL                "https://provision.milesight.com"
#define RPS_TEST_HTTP_URL           "https://test-provision.milesight.com"
#define RPS_DEBUG_HTTP_URL          "https://provision-device-debug-us.milesight.com"
#define RPS_DEV_HTTP_URL            "https://dev-provision.milesight.com"

#define RPS_PROFILE_MIP_PATH        "/api/v1/profiles"
#define RPS_MIP_PATH                "/api/v1/profiles/source-url"

#define MIP_LNS_PATH                "/api/v1/devices/certificate/lns"
#define MIP_DM_PATH                 "/api/v1/devices/certificate/mqtt"

#define DH_LNS_PATH                 "/devicehub/api/v1/open/device/certificate/lns"
#define DH_DM_PATH                  "/devicehub/api/v1/open/device/certificate/mqtt"

#define FILE_PATH_SIZE              64
#define URL_SIZE                    128
#define MSG_SIZE                    128

#define DM_DOWNLINK_RES_PENDING        "pending"
#define DM_DOWNLINK_RES_SUCCESS        "success"
#define DM_DOWNLINK_RES_FAILED         "failed"

#define DM_MQTT_VERISON                 "1"

enum {
    ERR_UNSUPPORT_TOPIC = 1001,
    ERR_RESOURCE_DOWNLOAD_FAILED = 1002,
    ERR_MD5_VALIDATION_FAILED = 1003,
    ERR_FIRMWARE_VERSION_IS_INCONSISTENT = 1004,
    ERR_NULL_URL        = 1006,
    ERR_RESOURCE_VERIFY_FAILED = 1007,
    ERR_RESOURCE_FORMAT = 1008,
    ERR_PRE_TASK_RUNNING     = 1009,
    ERR_UPGRADE_FAILED  = 1010,
};

#define mip_get_err_msg(err_code) #err_code

/***************    up level    ***************/

void *mip_malloc(size_t size);
void  mip_free(void **p);

typedef struct header_sign_s {
    char sn[17];
    char sec_key[10];
    char type[16];
    int8_t (*get_timestamp_cb)(char *timestamp, int len);
    int8_t (*get_signature_cb)(const unsigned char *input, size_t ilen, const unsigned char *key, size_t klen,
                               unsigned char **output, size_t *olen);
} header_sign_t;

typedef struct http_header_s {
    char *key;
    char *value;
} http_header_t;

typedef struct http_s {
    char *url;
    char *method;
    char *body;
    int timeout;
    http_header_t *headers;
    int header_cnt;
    char **resp;
} http_t;

typedef struct http_cb_s {
    int8_t (*http_send_req)(http_t *http);
    int8_t (*http_download_file)(const char *url, const char *filename, int timeout, int filesize, const char *md5,
                                 const char *crc32);
    int8_t (*http_upload_file)(const char *url, const char *filename, int timeout);
} http_cb_t;

/**
 * @brief 初始化mip功能
 *
 * @param sign http签名所需的参数和函数以及dm会使用到的sn和时间戳功能
 * @return int 0:成功 -1:失败
 */
int mip_init(header_sign_t *sign, http_cb_t *http_cbs);

typedef struct profile_s {
    char url[URL_SIZE];
    char md5[33];
    char crc32[9];
    int filesize;
} profile_t;

typedef struct source_s {
    char type[10];
    char host[URL_SIZE];
} source_t;

typedef struct resp_header_s {
    int status;
    char err_code[64];
    char err_msg[MSG_SIZE];
    char detail_msg[MSG_SIZE];
    char request_id[33];
} resp_header_t;

typedef struct rps_resp_s {
    resp_header_t header;
    struct resp_data_s {
        profile_t *profile;
        int profile_cnt;
        source_t source;
    } data;
} rps_resp_t;

typedef enum lns_type {
    LNW_TYPE_NONE = 0,
    LNS_TYPE_SEMTECH = 1,
    LNS_TYPE_BASICSTATION,
    LNS_TYPE_CHIRPSTACK,
} lns_type_e;

typedef struct lns_resp_s {
    resp_header_t header;
    struct lns_resp_data_s {
        lns_type_e type;
        union {
            struct lns_semtech_s {
                char addr[URL_SIZE];
                int up_port;
                int down_port;
            } semtech;
            struct lns_basicstation_s {
                char cups_uri[URL_SIZE];
                char cups_trust_url[URL_SIZE];
                char cups_key_url[URL_SIZE];
                char cups_cert_url[URL_SIZE];
                char lns_uri[URL_SIZE];
                char lns_trust_url[URL_SIZE];
                char lns_key_url[URL_SIZE];
                char lns_cert_url[URL_SIZE];
            } basicstation;
            struct lns_chirpstack_s {
                char addr[URL_SIZE];
                int port;
                char user[64];
                char pass[64];
                char cert_url[URL_SIZE];
                char prikey_url[URL_SIZE];
                char ca_cert_url[URL_SIZE];
            } chirpstack;
        };
    } data;
} lns_resp_t;

typedef struct dm_resp_s {
    resp_header_t header;
    struct dm_resp_data_s {
        char addr[URL_SIZE];
        int port;
        char user[64];
        char pass[64];
        char cert_url[URL_SIZE];
        char prikey_url[URL_SIZE];
        char ca_cert_url[URL_SIZE];
    } data;
} dm_resp_t;

typedef struct profile_cb_s {
    void (*got_resp)(char *resp);
    int8_t (*downloaded)();
} profile_cb_t;

typedef struct lns_profile_path_s {
    char cups_trust_path[FILE_PATH_SIZE];
    char cups_key_path[FILE_PATH_SIZE];
    char cups_cert_path[FILE_PATH_SIZE];
    char lns_trust_path[FILE_PATH_SIZE];
    char lns_key_path[FILE_PATH_SIZE];
    char lns_cert_path[FILE_PATH_SIZE];
    char mqtt_cert_path[FILE_PATH_SIZE];
    char mqtt_prikey_path[FILE_PATH_SIZE];
    char mqtt_ca_cert_path[FILE_PATH_SIZE];
} lns_profile_path_t;

typedef struct dm_profile_path_s {
    char mqtt_cert_path[FILE_PATH_SIZE];
    char mqtt_prikey_path[FILE_PATH_SIZE];
    char mqtt_ca_cert_path[FILE_PATH_SIZE];
} dm_profile_path_t;

int j2s_rps_resp(const char *j, void *s);

int mip_get_device_profile(const char *url, const char *pfpath, profile_cb_t *cbs, rps_resp_t *resp);

int mip_get_source_profile(const char *url, profile_cb_t *cbs, rps_resp_t *resp);

int j2s_lns_resp(const char *j, void *s);

int mip_get_lns_profile(const char *url, const char *type, const lns_profile_path_t *pfpath, profile_cb_t *cbs,
                        lns_resp_t *resp);

int j2s_dm_resp(const char *j, void *s);

int mip_get_dm_profile(const char *url, const char *type, const dm_profile_path_t *pfpath, profile_cb_t *cbs,
                       dm_resp_t *resp);

int j2s_http_resp(const char *j, void *s);

typedef struct dm_downlink_header_s {
    char ts[11];
    char msg_id[128];
    char event[32];
    char ver[16];
    char task_id[128]; //for devicehub
} dm_downlink_header_t;

typedef struct dm_downlink_result_s {
    char status[16];
    int err_code;
    char err_msg[64];
} dm_downlink_result_t;

enum {
    MIP_DM_CONN_STATUS_CONNECTING    = 0,
    MIP_DM_CONN_STATUS_CONNECTED     = 1,
    MIP_DM_CONN_STATUS_DISCONNECTED  = 2,
};

typedef struct dm_cb_s {
    void (*reboot)(dm_downlink_header_t dh, cJSON *ddata, dm_downlink_result_t *dres, char **udata);
    void (*upgrade)(dm_downlink_header_t dh, cJSON *ddata, dm_downlink_result_t *dres, char **udata);
    void (*profile_update)(dm_downlink_header_t dh, cJSON *ddata, dm_downlink_result_t *dres, char **udata);
    void (*profile_get)(dm_downlink_header_t dh, cJSON *ddata, dm_downlink_result_t *dres, char **udata);
    void (*history_get)(dm_downlink_header_t dh, cJSON *ddata, dm_downlink_result_t *dres, char **udata);
    void (*rule_update)(dm_downlink_header_t dh, cJSON *ddata, dm_downlink_result_t *dres, char **udata);
    void (*modbus_update)(dm_downlink_header_t dh, cJSON *ddata, dm_downlink_result_t *dres, char **udata);
    void (*wake_up)(dm_downlink_header_t dh, cJSON *ddata, dm_downlink_result_t *dres, char **udata);
    void (*service)(dm_downlink_header_t dh, cJSON *ddata, dm_downlink_result_t *dres, char **udata);
    void (*property)(dm_downlink_header_t dh, cJSON *ddata, dm_downlink_result_t *dres, char **udata);
    void (*api_token)(dm_downlink_header_t dh, cJSON *ddata, dm_downlink_result_t *dres, char **udata);
    void (*timestamp)(dm_downlink_header_t dh, cJSON *ddata, dm_downlink_result_t *dres, char **udata);
    void (*after_profile_update)(dm_downlink_result_t dres, char *udata);
    void (*after_reboot)(dm_downlink_result_t dres, char *udata);
    void (*after_upgrade)(dm_downlink_result_t dres, char *udata);
    void (*mip_dm_update_con_status)(int status);
} dm_cb_t;

typedef struct mqtt_s {
    char *host;
    int port;
    char *user;
    char *pass;
    char *ca_cert_path;
    char *cert_path;
    char *key_path;
    char **topics;
    int topic_cnt;
    char *client_id;
} mqtt_t;

typedef int(*sub_notify_cb)(char *topic, char *msg);
typedef int(*connect_status_cb)(int status);

typedef struct mqtt_cb_s {
    int8_t (*mqtt_start)(mqtt_t *mqtt, sub_notify_cb cb, connect_status_cb status_cb);
    int8_t (*mqtt_stop)();
    int8_t (*mqtt_is_connected)();
    int8_t (*mqtt_publish)(const char *topic, const char *msg, int timeout);
    int8_t (*mqtt_get_timestamp)(char *timestamp, int len);
} mqtt_cb_t;

int mip_dm_init(dm_cb_t *cbs, mqtt_cb_t *mqtt_cbs);

int mip_dm_start(dm_resp_t *cfg, const dm_profile_path_t *pfpath);

int mip_dm_stop(void);

int mip_dm_uplink(dm_downlink_header_t *dh, dm_downlink_result_t *dres, const char *topic, const char *msg);
int mip_dm_uplink_property(const char *msg);
int mip_dm_uplink_response(dm_downlink_header_t *dh, dm_downlink_result_t *dres, const char *msg);
int mip_dm_uplink_http(const char *url, const char *token, const char *msg);

int mip_dm_deinit(void);

#ifdef __cplusplus
}
#endif

#endif
