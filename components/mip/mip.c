#include "mip.h"
#include "pthread.h"

#define GOTO_END(code) {\
        ret = code;\
        goto end;\
    }

typedef int (*j2s_cb)(const char *j, void *s);

enum downlink_event_e {
    EVENT_RESTART,
    EVENT_FIRMWARE_UPGRADE,
    EVENT_PROFILE_RETRIEVAL,
    EVENT_PROFILE_UPDATE,
    EVENT_HISTORY_RETRIEVAL,
    EVENT_RULES_UPDATE,
    EVENT_MODBUS_UPDATE,
    EVENT_WAKE_UP,
    EVENT_SERVICE,
    EVENT_PROPERTY,
    EVENT_API_TOKEN,
    EVENT_TIMESTAMP,
    EVENT_MAX
};
typedef struct downlink_event_s {
    char name[32];
    void (*cb)(dm_downlink_header_t dh, cJSON *ddata, dm_downlink_result_t *dres, char **udata);
    void (*after_cb)(dm_downlink_result_t dres, char *udata);
    /*
    downlink回复流程见：
        https://doc.weixin.qq.com/sheet/e3_AQAA4QbpACAa2lmFW4LTwaL08g2UN?scode=AMgAYAe8AAY5E41OeYADIAzQYDACU&tab=7kztoo

    【流程说明】
            platform--------downlink---------->Device
    情况1: 可快速回复结果的  (注：success时data里面会在一个data代表处理返回的数据，例如profile的获取)
                    <--------response---------- status=succss 或者failed

    情况2：需要比较慢才能回复结果的（例如升级、重启）
                    1、保存下dm_downlink_header_t的值
                    2、<--------response---------- status=pending
                    3、处理完之后生成 dm_downlink_result_t结果
                    4、<--------response---------- status=succss 或者failed  (调用mip_dm_uplink_response发送)

    【cb 函数说明】：
    @param:
        [in] dh: 为downlink_header, 如果是需要二次回复的，例如升级镜像和重启，需要在收到downlink先回复一个response(pending)
        [in] ddata: downlink data的JSON结构，使用完不需要释放，有些downlink没有data，这个字段可能为NULL
        [out]dres: 返回的处理结果
            status = pening,success,failed
            err_code: 请在mip.h中定义，并且在上面文件添加，因为要所有设备统一
                !!!特别注意，例如当在处理升级的下载镜像中，平台下发重启，需回复异常，这个需在cb中自行处理，然后status回复 failed
            err_msg: 错误信息
        [out]udata: 为cb函数内部处理完，需要外部上报的data
            如果不需要上报data，可以为NULL
            如果需要上报data,需在cb内部开辟空间，再框架中会在使用完自动释放
    */
} downlink_event_t;

typedef struct dm_uplink_s {
    char ts[14];
    char msg_id[25];
    char event[34];
    char ver[16];
    struct {
        dm_downlink_result_t dres;
        char *data;
    } data;
} dm_uplink_t;

//global variable define
static header_sign_t g_sign;
static http_cb_t g_http_cb;
static mqtt_cb_t g_mqtt_cb;

static int dm_msg_id = 0;

static char resp_topic[128] = {0};

downlink_event_t devents[EVENT_MAX] = {
    {"restart", NULL, NULL},
    {"firmware_upgrade", NULL, NULL},
    {"profile_retrieval", NULL, NULL},
    {"profile_update", NULL, NULL},
    {"history_retrieval", NULL, NULL},
    {"rules_update", NULL, NULL},
    {"modbus_update", NULL, NULL},
    {"wake_up", NULL, NULL},
    {"service", NULL, NULL},
    {"property", NULL, NULL},
    {"api_token", NULL, NULL},
    {"timestamp", NULL, NULL},
};

typedef void (*mip_dm_update_con_status)(int status);
mip_dm_update_con_status g_dm_update_status_cb = NULL;

static pthread_mutex_t msg_id_mutex = PTHREAD_MUTEX_INITIALIZER;

//static functions
static int get_jsonobj_string_value(cJSON *root, const char *name, char *value, int size)
{
    cJSON *obj = cJSON_GetObjectItem(root, name);
    if (!obj || obj->type != cJSON_String || !obj->valuestring) {
        return -1;
    }
    snprintf(value, size, "%s", obj->valuestring);
    return 0;
}

static int get_jsonobj_int_value(cJSON *root, const char *name, int *value)
{
    cJSON *obj = cJSON_GetObjectItem(root, name);
    if (!obj || obj->type != cJSON_Number) {
        return -1;
    }
    *value = obj->valueint;
    return 0;
}

static int get_http_upload_headers(http_header_t **headers, int *count, const char *token, const char *type)
{
    int ret = -1;
    http_header_t *h = NULL;

    *count = 2;
    h = mip_malloc(sizeof(http_header_t) * (*count));
    h[0].key = strdup("X-MIP-AUTH-TOKEN");
    h[0].value = strdup(token);
    h[1].key = strdup("X-MIP-AUTH-TYPE");
    h[1].value = strdup(type);
    *headers = h;
    ret = 0;

    return ret;
}

static int get_http_headers(http_header_t **headers, int *count, int is_devicehub)
{
    int ret = -1;
    size_t ilen = 0;
    size_t klen = 0;
    char *key = NULL;
    char *input = NULL;
    char *signature = NULL;
    char *securt_key = NULL;
    char nonce[17] = {0};
    char timestamp[14] = {0};
    http_header_t *h = NULL;

    *count = 4;
    if (g_sign.get_timestamp_cb) {
        *count = 5;
        ret = g_sign.get_timestamp_cb(timestamp, 13);
        if (ret != 0) {
            LOG_PRINTF("ERR: get timestamp failed\n");
            GOTO_END(ret);
        }
    }

    if (is_devicehub) {
        securt_key = "4rn7bKvQ";
    } else {
        securt_key = g_sign.sec_key;
    }

    for (int i = 0 ; i < 16 ; i++) {
        nonce[i] = MIP_RANDOM() % 10 + '0';
    }

    if (!strcasecmp(g_sign.type, "HmacSHA256")) {
        ilen = strlen(g_sign.sn) + strlen(nonce) + strlen(timestamp);
        input = mip_malloc(ilen + 1);
        snprintf(input, ilen + 1, "%s%s%s", g_sign.sn, nonce, timestamp);
        klen = strlen(g_sign.sn) + strlen(securt_key);
        key = mip_malloc(klen + 1);
        snprintf(key, klen + 1, "%s%s", g_sign.sn, securt_key);
    } else {
        ilen = strlen(g_sign.sn) + strlen(nonce) + strlen(securt_key) + strlen(timestamp);
        input = mip_malloc(ilen + 1);
        snprintf(input, ilen + 1, "%s%s%s%s", g_sign.sn, nonce, securt_key, timestamp);
    }

    if ((ret = g_sign.get_signature_cb((unsigned char *)input, ilen, (unsigned char *)key, klen,
                                       (unsigned char **) &signature, NULL))) {
        LOG_PRINTF("ERR: get signature failed\n");
        GOTO_END(ret);
    }

    h = mip_malloc(sizeof(http_header_t) * (*count));
    h[0].key = strdup("X-REQUEST-SN");
    h[0].value = strdup(g_sign.sn);
    h[1].key = strdup("X-REQUEST-NONCE");
    h[1].value = strdup(nonce);
    h[2].key = strdup("X-REQUEST-SIGN-TYPE");
    h[2].value = strdup(g_sign.type);
    h[3].key = strdup("X-REQUEST-SIGNATURE");
    h[3].value = strdup(signature);
    if (strlen(timestamp)) {
        h[4].key = strdup("X-REQUEST-TIMESTAMP");
        h[4].value = strdup(timestamp);
    }
    *headers = h;
    ret = 0;

end:
    mip_free((void **)&key);
    mip_free((void **)&input);
    mip_free((void **)&signature);
    return ret;
}

static void free_http_headers(http_header_t *headers, int *count)
{
    int i = 0;

    for (i = 0; i < *count; i++) {
        mip_free((void **)&headers[i].key);
        mip_free((void **)&headers[i].value);
    }
    mip_free((void **)&headers);
    *count = 0;
    return;
}

static void free_http(http_t *http)
{
    mip_free((void **)&http->url);
    mip_free((void **)&http->method);
    free_http_headers(http->headers, &http->header_cnt);
    return;
}

static int j2s_http_resp_header(cJSON *root, resp_header_t *header)
{
    char status[16] = {0};

    if (get_jsonobj_string_value(root, "status", status, sizeof(status))) {
        LOG_PRINTF("ERR: get status failed\n");
        return -1;
    }
    if (!strcasecmp(status, "Failed")) {
        header->status = 0;
    } else if (!strcasecmp(status, "Success")) {
        header->status = 1;
    } else {
        LOG_PRINTF("ERR: status(%s) is invalid\n", status);
        return -2;
    }

    get_jsonobj_string_value(root, "errCode", header->err_code, sizeof(header->err_code));
    get_jsonobj_string_value(root, "errMsg", header->err_msg, sizeof(header->err_msg));
    get_jsonobj_string_value(root, "detailMsg", header->detail_msg, sizeof(header->detail_msg));
    get_jsonobj_string_value(root, "requestId", header->request_id, sizeof(header->request_id));

    return 0;
}

static int j2s_mqtt_downlink_header(cJSON *root, dm_downlink_header_t *dh)
{
    cJSON *context = NULL;

    if (!root || !dh) {
        return -1;
    }
    memset(dh, 0, sizeof(dm_downlink_header_t));

    get_jsonobj_string_value(root, "ts", dh->ts, sizeof(dh->ts));
    get_jsonobj_string_value(root, "ver", dh->ver, sizeof(dh->ver));
    if (get_jsonobj_string_value(root, "msgId", dh->msg_id, sizeof(dh->msg_id)) ||
        get_jsonobj_string_value(root, "event", dh->event, sizeof(dh->event))) {
        LOG_PRINTF("ERR: get msgId or eventType failed\n");
        return -2;
    }
    context = cJSON_GetObjectItem(root, "context");
    if (context) {
        get_jsonobj_string_value(context, "taskId", dh->task_id, sizeof(dh->task_id));
    }
    return 0;
}

//need free after use finish
static char *s2j_dm_uplink(dm_downlink_header_t *dh, dm_uplink_t *s)
{
    char *j = NULL;
    cJSON *root = NULL;
    cJSON *data = NULL;
    cJSON *child_data = NULL;
    cJSON *context = NULL;

    if (!s) {
        LOG_PRINTF("ERR: ack is null\n");
        return NULL;
    }

    root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "ts", s->ts);
    cJSON_AddStringToObject(root, "msgId", s->msg_id);
    cJSON_AddStringToObject(root, "event", s->event);
    cJSON_AddStringToObject(root, "ver", s->ver);

    if (s->data.data && strlen(s->data.data) > 0) {
        child_data = cJSON_Parse(s->data.data);
    }

    if (dh) { //is response
        data = cJSON_CreateObject();
        cJSON_AddStringToObject(data, "msgId", dh->msg_id);
        cJSON_AddStringToObject(data, "event", dh->event);
        cJSON_AddStringToObject(data, "status", s->data.dres.status);
        if (!strcasecmp(s->data.dres.status, DM_DOWNLINK_RES_FAILED)) {
            cJSON_AddNumberToObject(data, "errCode", s->data.dres.err_code);
            cJSON_AddStringToObject(data, "errMsg", s->data.dres.err_msg);
        }
        if (child_data) {
            cJSON_AddItemToObject(data, "data", child_data);
        }
        cJSON_AddItemToObject(root, "data", data);

        //add context
        if (strlen(dh->task_id) > 0) {
            context = cJSON_CreateObject();
            cJSON_AddStringToObject(context, "taskId", dh->task_id);
            cJSON_AddItemToObject(root, "context", context);
        }
    } else { //is uplink
        if (child_data) {
            cJSON_AddItemToObject(root, "data", child_data);
        }
    }

    j = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return j;
}

static int do_http_req(char *url, char *method, int is_devicehub, j2s_cb j2s, char **json_resp, void *resp)
{
    int i = 0;
    int ret = -1;
    int retry = 1;
    int header_cnt = 0;
    http_header_t *headers = NULL;
    http_t http;

    if (!url || !method || !j2s || !resp) {
        LOG_PRINTF("ERR: url(%p) or method(%p) or j2s(%p) or resp(%p) is null\n", url, method, j2s, resp);
        GOTO_END(-1);
    }

    memset(&http, 0, sizeof(http_t));
    http.url = strdup(url);
    http.method = strdup(method);
    http.timeout = 60;
    http.resp = json_resp;

    for (i = 0; i < retry; i++) {
        if (headers) {
            free_http_headers(headers, &header_cnt);
        }
        ret = get_http_headers(&headers, &header_cnt, is_devicehub);
        if (ret != 0) {
            LOG_PRINTF("WARN: set header failed, try again\n");
            MIP_SLEEP(1);
            continue;
        }
        http.headers = headers;
        http.header_cnt = header_cnt;
        ret = g_http_cb.http_send_req(&http);
        if (ret != 0) {
            mip_free((void **)json_resp);
            LOG_PRINTF("WARN: http send req get resp failed, try again\n");
            MIP_SLEEP(1);
            continue;
        }
        break;
    }

    if (ret) {
        mip_free((void **)json_resp);
        LOG_PRINTF("get rps failed\n");
        GOTO_END(-2);
    }

    if (j2s(*json_resp, resp)) {
        mip_free((void **)json_resp);
        LOG_PRINTF("ERR: json to struct failed\n");
        GOTO_END(-3);
    }
    ret = 0;

end:
    free_http(&http);
    return ret;
}

static int do_http_download_file(char *url, const char *path)
{
    if (!strlen(url) || !strlen(path)) {
        LOG_PRINTF("ERR: url(%s) or path(%s) is null\n", url ? url : "NULL", path ? path : "NULL");
        return 0;
        //return -1;
    }
    int ret = 0;
    int retry_cnt = 0;
    do {
        ret = g_http_cb.http_download_file(url, path, 60, -1, NULL, NULL);
        if (ret) {
            LOG_PRINTF("ERR: download file failed, try again\n");
            MIP_SLEEP(2);
        }
    } while (ret && retry_cnt++ < 3);
    return ret;
}

static int do_http_upload_data(char *url, const char *token, const char *type, const char *data, j2s_cb j2s,
                               char **json_resp, void *resp)
{
    int i = 0;
    int ret = -1;
    int retry = 1;
    int header_cnt = 0;
    http_header_t *headers = NULL;
    http_t http;

    if (!url || !token || !type || !data || !j2s || !resp) {
        LOG_PRINTF("ERR: url(%p) or token(%p) or type(%p) or data(%p) or j2s(%p) or resp(%p) is null\n", url, token, type, data,
                   j2s, resp);
        GOTO_END(-1);
    }

    memset(&http, 0, sizeof(http_t));
    http.url = strdup(url);
    http.method = strdup("POST");
    http.timeout = 60;
    http.resp = json_resp;

    get_http_upload_headers(&headers, &header_cnt, token, type);
    http.headers = headers;
    http.header_cnt = header_cnt;
    http.body = strdup(data);
    for (i = 0; i < retry; i++) {
        ret = g_http_cb.http_send_req(&http);
        if (ret != 0) {
            mip_free((void **)json_resp);
            LOG_PRINTF("WARN: http send req get resp failed, try again\n");
            MIP_SLEEP(1);
            continue;
        }
        break;
    }

    if (ret) {
        mip_free((void **)json_resp);
        LOG_PRINTF("get rps failed\n");
        GOTO_END(-2);
    }

    if (j2s(*json_resp, resp)) {
        mip_free((void **)json_resp);
        LOG_PRINTF("ERR: json to struct failed\n");
        GOTO_END(-3);
    }
    ret = 0;

end:
    free_http(&http);
    return ret;
}

static int get_rps(const char *url, int need_profile, const char *pfpath, profile_cb_t *cbs, rps_resp_t *resp)
{
    int ret = -1;
    char full_url[256] = {0};
    int retry = 1;
    char *json_resp = NULL;

    if (!resp || !url) {
        LOG_PRINTF("ERR: rps resp is null\n");
        return -1;
    }

    snprintf(full_url, sizeof(full_url), "%s%s", url, need_profile ? RPS_PROFILE_MIP_PATH : RPS_MIP_PATH);
    ret = do_http_req(full_url, "GET", 0, j2s_rps_resp, &json_resp, (void *)resp);
    if (ret) {
        return ret;
    }
    if (cbs->got_resp) {
        cbs->got_resp(json_resp);
    }
    //获取成功，但是字段为失败也当做失败返回
    if (!resp->header.status) {
        LOG_PRINTF("ERR: get rps failed, err_code(%s) err_msg(%s)\n", resp->header.err_code, resp->header.err_msg);
        mip_free((void **)&json_resp);
        return -2;
    }
    mip_free((void **)&json_resp);

    if (need_profile) {
        ret = 0;
        if (pfpath && strlen(pfpath) && resp->data.profile && strlen(resp->data.profile[0].url)) {
            for (int i = 0 ; i < retry ; i++) {
                if ((ret = g_http_cb.http_download_file(resp->data.profile[0].url, pfpath, 60, resp->data.profile[0].filesize,
                                                        resp->data.profile[0].md5, resp->data.profile[0].crc32))) {
                    LOG_PRINTF("ERR: download profile failed, try again\n");
                    MIP_SLEEP(1);
                    continue;
                }
                break;
            }
            if (!ret) {
                LOG_PRINTF("INFO: download profile success\n");
                if (cbs->downloaded) {
                    cbs->downloaded();
                }
            }
        } else {
            return -3;
        }
        return ret;
    }
    return 0;
}

static int http_post(const char *url, const char *token, const char *type, const char *event, const char *msg)
{
    int ret = -1;
    char *json_resp = NULL;
    resp_header_t header;
    dm_uplink_t up;
    char *buf = NULL;

    if (!url || !token || !type || !msg || !event) {
        LOG_PRINTF("ERR: url(%p) or token(%p) or type(%p) or msg(%p) or event(%p) is null\n", url, token, type, msg, event);
        return -1;
    }
    memset(&up, 0, sizeof(up));

    pthread_mutex_lock(&msg_id_mutex);
    if (g_mqtt_cb.mqtt_get_timestamp) {
        g_mqtt_cb.mqtt_get_timestamp(up.ts, 13);
        snprintf(up.msg_id, sizeof(up.msg_id), "%s%011d", up.ts, ++dm_msg_id);
    } else {
        snprintf(up.msg_id, sizeof(up.msg_id), "%024d", ++dm_msg_id);
    }
    pthread_mutex_unlock(&msg_id_mutex);

    snprintf(up.ver, sizeof(up.ver), DM_MQTT_VERISON);
    snprintf(up.event, sizeof(up.event), "%s", event);

    up.data.data = (char *)msg;
    buf = s2j_dm_uplink(NULL, &up);

    ret = do_http_upload_data((char *)url, token, type, buf, j2s_http_resp, &json_resp, &header);
    if (ret) {
        mip_free((void **)&buf);
        return ret;
    }
    if (!header.status) {
        LOG_PRINTF("ERR: http post failed, err_code(%s) err_msg(%s)\n", header.err_code, header.err_msg);
        mip_free((void **)&json_resp);
        mip_free((void **)&buf);
        return -2;
    }
    mip_free((void **)&buf);
    mip_free((void **)&json_resp);
    return 0;

}

static int dm_downlink_cb(char *topic, char *msg)
{
    int i = 0;
    int ret = -1;
    char *buf = NULL;
    cJSON *root = NULL;
    cJSON *data = NULL;
    dm_uplink_t up;
    dm_downlink_header_t dh;

    if (!topic || !msg) {
        return -1;
    }
    // LOG_PRINTF("DEBUG: notice topic(%s) msg(%s)\n", topic, msg);

    root = cJSON_Parse(msg);
    if (!root) {
        LOG_PRINTF("ERR: msg not json format\n");
        return -1;
    }

    if (j2s_mqtt_downlink_header(root, &dh)) {
        LOG_PRINTF("ERR: notice msg json to struct failed\n");
        GOTO_END(-2);
    }
    data = cJSON_GetObjectItem(root, "data");

    memset(&up, 0, sizeof(up));

    //make sure data automic
    pthread_mutex_lock(&msg_id_mutex);
    if (g_mqtt_cb.mqtt_get_timestamp) {
        g_mqtt_cb.mqtt_get_timestamp(up.ts, 13);
        snprintf(up.msg_id, sizeof(up.msg_id), "%s%011d", up.ts, ++dm_msg_id);
    } else {
        snprintf(up.msg_id, sizeof(up.msg_id), "%024d", ++dm_msg_id);
    }
    pthread_mutex_unlock(&msg_id_mutex);

    snprintf(up.ver, sizeof(up.ver), DM_MQTT_VERISON);
    snprintf(up.event, sizeof(up.event), "response");

    for (i = 0; i < sizeof(devents) / sizeof(devents[0]); i++) {
        if (!strstr(topic, devents[i].name) || !devents[i].cb) {
            continue;
        }

        devents[i].cb(dh, data, &up.data.dres, &up.data.data);
        buf = s2j_dm_uplink(&dh, &up);
        if (!buf) {
            LOG_PRINTF("ERR: dm ack struct to json failed\n");
            GOTO_END(-3);
        }
        LOG_PRINTF("DEBUG: dm response msg(%s)\n", buf);
        if (g_mqtt_cb.mqtt_publish(resp_topic, buf, 3)) {
            LOG_PRINTF("ERR: mqtt publish failed\n");
        }
        if (devents[i].after_cb) {
            devents[i].after_cb(up.data.dres, up.data.data);
        }
        GOTO_END(0);
    }

    LOG_PRINTF("unsupport topic:%s\n", topic);
    up.data.dres.err_code = ERR_UNSUPPORT_TOPIC;
    snprintf(up.data.dres.status, sizeof(up.data.dres.status), DM_DOWNLINK_RES_FAILED);
    snprintf(up.data.dres.err_msg, sizeof(up.data.dres.err_msg), "%s", mip_get_err_msg(ERR_UNSUPPORT_TOPIC));

    buf = s2j_dm_uplink(&dh, &up);
    g_mqtt_cb.mqtt_publish(resp_topic, buf, 3);
    GOTO_END(0);

end:
    cJSON_Delete(root);
    mip_free((void **)&buf);
    mip_free((void **)&up.data.data);
    return ret;
}

static int dm_connect_status_cb(int status)
{
    if (g_dm_update_status_cb) {
        g_dm_update_status_cb(status);
    }
    return 0;
}

//extern functions
void *mip_malloc(size_t size)
{
    void *p = malloc(size);

    if (p == NULL) {
        return NULL;
    }
    memset(p, 0, size);
    return p;
}

void mip_free(void **p)
{
    if (*p) {
        free(*p);
        *p = NULL;
    }
    return;
}

int j2s_rps_resp(const char *j, void *s)
{
    int ret = -1;
    cJSON *root = NULL;
    cJSON *data = NULL;
    cJSON *source = NULL;
    cJSON *profile = NULL;
    cJSON *profile_item = NULL;
    rps_resp_t *resp = NULL;

    if (!j || !s) {
        LOG_PRINTF("ERR: buf(%p) or resp(%p) is null\n", j, s);
        GOTO_END(-1);
    }
    resp = (rps_resp_t *)s;

    root = cJSON_Parse(j);
    if (!root) {
        LOG_PRINTF("ERR: parse json failed\n");
        GOTO_END(-2);
    }

    if (j2s_http_resp_header(root, &resp->header)) {
        LOG_PRINTF("ERR: resp header json to struct failed\n");
        GOTO_END(-3);
    }

    data = cJSON_GetObjectItem(root, "data");
    if (data && data->type == cJSON_Object) {
        resp->data.profile = NULL;
        resp->data.profile_cnt = 0;
        profile = cJSON_GetObjectItem(data, "profiles");
        if (profile && profile->type == cJSON_Array) {
            resp->data.profile_cnt = cJSON_GetArraySize(profile);
            resp->data.profile = mip_malloc(sizeof(profile_t) * resp->data.profile_cnt);
            for (int i = 0 ; i < resp->data.profile_cnt ; i++) {
                profile_item = cJSON_GetArrayItem(profile, i);
                if (get_jsonobj_string_value(profile_item, "url", resp->data.profile[i].url, sizeof(resp->data.profile[i].url))) {
                    LOG_PRINTF("WARN: get profile url failed\n");
                    continue;
                    // GOTO_END(-4); 如果平台未开启auto-p则这里会为空，应该继续往下走不报错在外层判断
                }
                get_jsonobj_string_value(profile_item, "md5", resp->data.profile[i].md5, sizeof(resp->data.profile[i].md5));
                get_jsonobj_string_value(profile_item, "crc32", resp->data.profile[i].crc32, sizeof(resp->data.profile[i].crc32));
                get_jsonobj_int_value(profile_item, "fileSize", &resp->data.profile[i].filesize);
            }
        }

        source = cJSON_GetObjectItem(data, "source");
        if (!source) {
            GOTO_END(-5);
        }
        if (get_jsonobj_string_value(source, "type", resp->data.source.type, sizeof(resp->data.source.type))) {
            LOG_PRINTF("ERR: get source type failed\n");
            GOTO_END(-6);
        }
        if (get_jsonobj_string_value(source, "host", resp->data.source.host, sizeof(resp->data.source.host))) {
            LOG_PRINTF("ERR: get source host failed\n");
            GOTO_END(-7);
        }
    }
    ret = 0;

end:
    if (root) {
        cJSON_Delete(root);
    }
    return ret;
}

int j2s_lns_resp(const char *j, void *s)
{
    int ret = -1;
    char type[16] = {0};
    cJSON *root = NULL;
    cJSON *data = NULL;
    cJSON *ns = NULL;
    lns_resp_t *resp = NULL;

    if (!j || !s) {
        LOG_PRINTF("ERR: buf(%p) or resp(%p) is null\n", j, s);
        return -1;
    }
    resp = (lns_resp_t *)s;

    root = cJSON_Parse(j);
    if (!root) {
        LOG_PRINTF("ERR: parse json failed\n");
        GOTO_END(-2);
    }

    if (j2s_http_resp_header(root, &resp->header)) {
        LOG_PRINTF("ERR: resp header json to struct failed\n");
        GOTO_END(-3);
    }
    data = cJSON_GetObjectItem(root, "data");
    if (data && data->type == cJSON_Object) {
        if (get_jsonobj_string_value(data, "type", type, sizeof(type))) {
            LOG_PRINTF("ERR: get type failed\n");
            GOTO_END(-4);
        }
        if (!strcasecmp(type, "Semtech")) {
            resp->data.type = LNS_TYPE_SEMTECH;
            ns = cJSON_GetObjectItem(data, "semtech");
            if (!ns) {
                LOG_PRINTF("ERR: get semtech failed\n");
                GOTO_END(-5);
            }

            if (get_jsonobj_string_value(ns, "serverAddress", resp->data.semtech.addr, sizeof(resp->data.semtech.addr)) ||
                get_jsonobj_int_value(ns, "portUp", &resp->data.semtech.up_port) ||
                get_jsonobj_int_value(ns, "portDown", &resp->data.semtech.down_port)) {
                LOG_PRINTF("ERR: get semtech cfg item failed\n");
                GOTO_END(-6);
            }
        } else if (!strcasecmp(type, "BasicStation")) {
            resp->data.type = LNS_TYPE_BASICSTATION;
            ns = cJSON_GetObjectItem(data, "basicStation");
            if (!ns) {
                LOG_PRINTF("ERR: get basicStation failed\n");
                GOTO_END(-7);
            }
            get_jsonobj_string_value(ns, "cupsUri", resp->data.basicstation.cups_uri, sizeof(resp->data.basicstation.cups_uri));
            get_jsonobj_string_value(ns, "cupsCaTrustUrl", resp->data.basicstation.cups_trust_url,
                                     sizeof(resp->data.basicstation.cups_trust_url));
            get_jsonobj_string_value(ns, "cupsClientCertPemUrl", resp->data.basicstation.cups_cert_url,
                                     sizeof(resp->data.basicstation.cups_cert_url));
            get_jsonobj_string_value(ns, "cupsClientKeyUrl", resp->data.basicstation.cups_key_url,
                                     sizeof(resp->data.basicstation.cups_key_url));
            get_jsonobj_string_value(ns, "lnsUri", resp->data.basicstation.lns_uri, sizeof(resp->data.basicstation.lns_uri));
            get_jsonobj_string_value(ns, "lnsCaTrustUrl", resp->data.basicstation.lns_trust_url,
                                     sizeof(resp->data.basicstation.lns_trust_url));
            get_jsonobj_string_value(ns, "lnsClientCertPemUrl", resp->data.basicstation.lns_cert_url,
                                     sizeof(resp->data.basicstation.lns_cert_url));
            get_jsonobj_string_value(ns, "lnsClientKeyUrl", resp->data.basicstation.lns_key_url,
                                     sizeof(resp->data.basicstation.lns_key_url));
        } else if (!strcasecmp(type, "Chirpstack")) {
            resp->data.type = LNS_TYPE_CHIRPSTACK;
            ns = cJSON_GetObjectItem(data, "chirpstack");
            if (!ns) {
                LOG_PRINTF("ERR: get chirpstack failed\n");
                GOTO_END(-8);
            }
            if (get_jsonobj_string_value(ns, "mqttBroker", resp->data.chirpstack.addr, sizeof(resp->data.chirpstack.addr)) ||
                get_jsonobj_int_value(ns, "mqttPort", &resp->data.chirpstack.port)) {
                LOG_PRINTF("ERR: get chirpstack cfg item failed\n");
                GOTO_END(-9);
            }
            get_jsonobj_string_value(ns, "username", resp->data.chirpstack.user, sizeof(resp->data.chirpstack.user));
            get_jsonobj_string_value(ns, "password", resp->data.chirpstack.pass, sizeof(resp->data.chirpstack.pass));
            get_jsonobj_string_value(ns, "certPemUrl", resp->data.chirpstack.cert_url, sizeof(resp->data.chirpstack.cert_url));
            get_jsonobj_string_value(ns, "privateKeyUrl", resp->data.chirpstack.prikey_url,
                                     sizeof(resp->data.chirpstack.prikey_url));
            get_jsonobj_string_value(ns, "caCertPemUrl", resp->data.chirpstack.ca_cert_url,
                                     sizeof(resp->data.chirpstack.ca_cert_url));
        } else {
            LOG_PRINTF("ERR: type(%s) is invalid\n", type);
            GOTO_END(-10);
        }
    }
    ret = 0;

end:
    if (root) {
        cJSON_Delete(root);
    }
    return ret;
}

int j2s_dm_resp(const char *j, void *s)
{
    int ret = -1;
    cJSON *root = NULL;
    cJSON *data = NULL;
    dm_resp_t *resp = NULL;

    if (!j || !s) {
        LOG_PRINTF("ERR: buf(%p) or resp(%p) is null\n", j, s);
        return -1;
    }
    resp = (dm_resp_t *)s;

    root = cJSON_Parse(j);
    if (!root) {
        LOG_PRINTF("ERR: parse json failed\n");
        GOTO_END(-2);
    }
    if (j2s_http_resp_header(root, &resp->header)) {
        LOG_PRINTF("ERR: resp header json to struct failed\n");
        GOTO_END(-3);
    }
    data = cJSON_GetObjectItem(root, "data");
    if (data && data->type == cJSON_Object) {
        if (get_jsonobj_string_value(data, "mqttBroker", resp->data.addr, sizeof(resp->data.addr)) ||
            get_jsonobj_int_value(data, "mqttPort", &resp->data.port)) {
            LOG_PRINTF("ERR: get dm cfg item failed\n");
            GOTO_END(-4);
        }
        get_jsonobj_string_value(data, "username", resp->data.user, sizeof(resp->data.user));
        get_jsonobj_string_value(data, "password", resp->data.pass, sizeof(resp->data.pass));
        get_jsonobj_string_value(data, "certPemUrl", resp->data.cert_url, sizeof(resp->data.cert_url));
        get_jsonobj_string_value(data, "privateKeyUrl", resp->data.prikey_url, sizeof(resp->data.prikey_url));
        get_jsonobj_string_value(data, "caCertPemUrl", resp->data.ca_cert_url, sizeof(resp->data.ca_cert_url));
    }
    ret = 0;

end:
    if (root) {
        cJSON_Delete(root);
    }
    return ret;
}

int j2s_http_resp(const char *j, void *s)
{
    int ret = -1;
    cJSON *root = NULL;
    resp_header_t *header = NULL;

    if (!j || !s) {
        return 0;
    }
    header = (resp_header_t *)s;

    root = cJSON_Parse(j);
    if (!root) {
        LOG_PRINTF("ERR: parse json failed\n");
        GOTO_END(-2);
    }
    if (j2s_http_resp_header(root, header)) {
        LOG_PRINTF("ERR: resp header json to struct failed\n");
        GOTO_END(-3);
    }
    ret = 0;

end:
    if (root) {
        cJSON_Delete(root);
    }
    return ret;
}

int mip_init(header_sign_t *sign, http_cb_t *http_cbs)
{
    if (!sign || !http_cbs) {
        LOG_PRINTF("ERR: sign(%p) or http_cbs(%p) is null\n", sign, http_cbs);
        return -1;
    }
    if (!http_cbs->http_send_req || !http_cbs->http_download_file || !http_cbs->http_upload_file) {
        LOG_PRINTF("ERR: http_cbs is invalid\n");
        return -2;
    }
    g_sign = *sign;
    g_http_cb = *http_cbs;
    return 0;
}

int mip_get_device_profile(const char *url, const char *pfpath, profile_cb_t *cbs, rps_resp_t *resp)
{
    return get_rps(url, 1, pfpath, cbs, resp);
}

int mip_get_source_profile(const char *url, profile_cb_t *cbs, rps_resp_t *resp)
{
    return get_rps(url, 0, NULL, cbs, resp);
}

int mip_get_lns_profile(const char *url, const char *type, const lns_profile_path_t *pfpath, profile_cb_t *cbs,
                        lns_resp_t *resp)
{
    int ret = -1;
    int is_devicehub = 0;
    char full_url[256] = {0};
    char *json_resp = NULL;

    if (!url || !type || !pfpath || !cbs || !resp) {
        LOG_PRINTF("ERR: type(%p) or pfpath(%p) or cbs(%p) or resp(%p) is null\n", type, pfpath, cbs, resp);
        return -1;
    }

    if (!strcasecmp(type, "devicehub")) {
        is_devicehub = 1;
    }
    snprintf(full_url, sizeof(full_url), "%s%s", url, is_devicehub ? DH_LNS_PATH : MIP_LNS_PATH);

    ret = do_http_req(full_url, "GET", is_devicehub, j2s_lns_resp, &json_resp, (void *)resp);
    if (ret) {
        return ret;
    }
    if (cbs->got_resp) {
        cbs->got_resp(json_resp);
    }
    //获取成功，但是字段为失败也当做失败返回
    if (!resp->header.status) {
        LOG_PRINTF("ERR: get rps failed, err_code(%s) err_msg(%s)\n", resp->header.err_code, resp->header.err_msg);
        mip_free((void **)&json_resp);
        return -2;
    }
    mip_free((void **)&json_resp);

    if (resp->data.type == LNS_TYPE_BASICSTATION) {
        if (do_http_download_file(resp->data.basicstation.cups_trust_url, pfpath->cups_trust_path) ||
            do_http_download_file(resp->data.basicstation.cups_cert_url, pfpath->cups_cert_path) ||
            do_http_download_file(resp->data.basicstation.cups_key_url, pfpath->cups_key_path) ||
            do_http_download_file(resp->data.basicstation.lns_trust_url, pfpath->lns_trust_path) ||
            do_http_download_file(resp->data.basicstation.lns_cert_url, pfpath->lns_cert_path) ||
            do_http_download_file(resp->data.basicstation.lns_key_url, pfpath->lns_key_path)) {
            LOG_PRINTF("ERR: download file failed\n");
            return -3;
        }
    } else if (resp->data.type == LNS_TYPE_CHIRPSTACK) {
        if (do_http_download_file(resp->data.chirpstack.cert_url, pfpath->mqtt_cert_path) ||
            do_http_download_file(resp->data.chirpstack.prikey_url, pfpath->mqtt_prikey_path) ||
            do_http_download_file(resp->data.chirpstack.ca_cert_url, pfpath->mqtt_ca_cert_path)) {
            LOG_PRINTF("ERR: download file failed\n");
            return -3;
        }
    }
    if (cbs->downloaded) {
        cbs->downloaded();
    }

    return 0;
}

int mip_get_dm_profile(const char *url, const char *type, const dm_profile_path_t *pfpath, profile_cb_t *cbs,
                       dm_resp_t *resp)
{
    int ret = -1;
    int is_devicehub = 0;
    char full_url[256] = {0};
    char *json_resp = NULL;

    if (!url || !type || !pfpath || !cbs || !resp) {
        LOG_PRINTF("ERR: type(%p) or pfpath(%p) or cbs(%p) or resp(%p) is null\n", type, pfpath, cbs, resp);
        return -1;
    }

    if (!strcasecmp(type, "devicehub")) {
        is_devicehub = 1;
    }
    snprintf(full_url, sizeof(full_url), "%s%s", url, is_devicehub ? DH_DM_PATH : MIP_DM_PATH);

    ret = do_http_req(full_url, "GET", is_devicehub, j2s_dm_resp, &json_resp, (void *)resp);
    if (ret) {
        return ret;
    }
    if (cbs->got_resp) {
        cbs->got_resp(json_resp);
    }
    //获取成功，但是字段为失败也当做失败返回
    if (!resp->header.status) {
        LOG_PRINTF("ERR: get rps failed, err_code(%s) err_msg(%s)\n", resp->header.err_code, resp->header.err_msg);
        mip_free((void **)&json_resp);
        return -2;
    }
    mip_free((void **)&json_resp);

    if (do_http_download_file(resp->data.cert_url, pfpath->mqtt_cert_path) ||
        do_http_download_file(resp->data.prikey_url, pfpath->mqtt_prikey_path) ||
        do_http_download_file(resp->data.ca_cert_url, pfpath->mqtt_ca_cert_path)) {
        LOG_PRINTF("ERR: download file failed\n");
        return -3;
    }
    if (cbs->downloaded) {
        cbs->downloaded();
    }
    return 0;
}

int mip_dm_init(dm_cb_t *cbs, mqtt_cb_t *mqtt_cbs)
{
    if (!cbs) {
        LOG_PRINTF("ERR: cbs is null\n");
        return -1;
    }

    if (!mqtt_cbs || !mqtt_cbs->mqtt_start || !mqtt_cbs->mqtt_stop || !mqtt_cbs->mqtt_publish ||
        !mqtt_cbs->mqtt_is_connected || !mqtt_cbs->mqtt_get_timestamp) {
        LOG_PRINTF("ERR: mqtt_cbs is null\n");
        return -2;
    }

    g_mqtt_cb = *mqtt_cbs;
    devents[EVENT_RESTART].cb = cbs->reboot;
    devents[EVENT_FIRMWARE_UPGRADE].cb = cbs->upgrade;
    devents[EVENT_PROFILE_RETRIEVAL].cb = cbs->profile_get;
    devents[EVENT_PROFILE_UPDATE].cb = cbs->profile_update;
    devents[EVENT_HISTORY_RETRIEVAL].cb = cbs->history_get;
    devents[EVENT_RULES_UPDATE].cb = cbs->rule_update;
    devents[EVENT_MODBUS_UPDATE].cb = cbs->modbus_update;
    devents[EVENT_WAKE_UP].cb = cbs->wake_up;
    devents[EVENT_SERVICE].cb = cbs->service;
    devents[EVENT_PROPERTY].cb = cbs->property;
    devents[EVENT_TIMESTAMP].cb = cbs->timestamp;
    devents[EVENT_API_TOKEN].cb = cbs->api_token;

    devents[EVENT_PROFILE_UPDATE].after_cb = cbs->after_profile_update;
    devents[EVENT_RESTART].after_cb = cbs->after_reboot;
    devents[EVENT_FIRMWARE_UPGRADE].after_cb = cbs->after_upgrade;

    g_dm_update_status_cb = cbs->mip_dm_update_con_status;
    snprintf(resp_topic, sizeof(resp_topic), "iot/v1/device/%s/uplink/response", g_sign.sn);

    dm_msg_id = MIP_RANDOM();

    return 0;
}

int mip_dm_deinit(void)
{
    int i = 0;

    for (i = 0; i < sizeof(devents) / sizeof(devents[0]); i++) {
        devents[i].cb = NULL;
    }
    memset(resp_topic, 0, sizeof(resp_topic));

    return 0;
}

int mip_dm_start(dm_resp_t *cfg, const dm_profile_path_t *pfpath)
{
    mqtt_t mqtt;
    char topic[64] = {0};

    if (!cfg) {
        LOG_PRINTF("ERR: cfg(%p)is null\n", cfg);
        return -1;
    }
    memset(&mqtt, 0, sizeof(mqtt_t));
    mqtt.host = strdup(cfg->data.addr);
    mqtt.port = cfg->data.port;
    mqtt.user = strdup(cfg->data.user);
    mqtt.pass = strdup(cfg->data.pass);
    mqtt.client_id = strdup(g_sign.sn);
    mqtt.cert_path = NULL;
    mqtt.key_path = NULL;
    mqtt.ca_cert_path = NULL;

    if (pfpath) {
        if (strlen(cfg->data.cert_url)) {
            mqtt.cert_path = strdup(pfpath->mqtt_cert_path);
        }
        if (strlen(cfg->data.prikey_url)) {
            mqtt.key_path = strdup(pfpath->mqtt_prikey_path);
        }
        if (strlen(cfg->data.ca_cert_url)) {
            mqtt.ca_cert_path = strdup(pfpath->mqtt_ca_cert_path);
        }
    }
    mqtt.topic_cnt = 1;
    snprintf(topic, sizeof(topic), "iot/v1/device/%s/downlink/#", g_sign.sn);
    char *tmp = topic;
    mqtt.topics = &tmp;

    if (g_mqtt_cb.mqtt_start(&mqtt, dm_downlink_cb, dm_connect_status_cb) != 0) {
        LOG_PRINTF("ERR: mqtt start failed\n");
        return -2;
    }

    mip_free((void **)&mqtt.host);
    mip_free((void **)&mqtt.user);
    mip_free((void **)&mqtt.pass);
    mip_free((void **)&mqtt.cert_path);
    mip_free((void **)&mqtt.key_path);
    mip_free((void **)&mqtt.ca_cert_path);
    mip_free((void **)&mqtt.client_id);
    return 0;
}

int mip_dm_stop(void)
{
    g_mqtt_cb.mqtt_stop();
    return 0;
}

int mip_dm_uplink(dm_downlink_header_t *dh, dm_downlink_result_t *dres, const char *event, const char *msg)
{
    char *buf = NULL;
    char topic[128] = {0};
    dm_uplink_t up;

    if (!event || (strcmp(event, "response") && !msg)) {
        LOG_PRINTF("ERR: topic(%p) or msg(%p) is null or publish flag is not none\n", event, msg);
        return -1;
    }
    if (!g_mqtt_cb.mqtt_is_connected()) {
        LOG_PRINTF("ERR: mqtt is not connected\n");
        return -2;
    }
    memset(&up, 0, sizeof(up));

    pthread_mutex_lock(&msg_id_mutex);
    if (g_mqtt_cb.mqtt_get_timestamp) {
        g_mqtt_cb.mqtt_get_timestamp(up.ts, 13);
        snprintf(up.msg_id, sizeof(up.msg_id), "%s%011d", up.ts, ++dm_msg_id);
    } else {
        snprintf(up.msg_id, sizeof(up.msg_id), "%024d", ++dm_msg_id);
    }
    pthread_mutex_unlock(&msg_id_mutex);

    snprintf(up.ver, sizeof(up.ver), DM_MQTT_VERISON);
    snprintf(up.event, sizeof(up.event), "%s", event);

    up.data.data = (char *)msg;
    if (dh && dres) {
        memcpy(&up.data.dres, dres, sizeof(dm_downlink_result_t));
        buf = s2j_dm_uplink(dh, &up);
    } else {
        buf = s2j_dm_uplink(NULL, &up);
    }

    snprintf(topic, sizeof(topic), "iot/v1/device/%s/uplink/%s", g_sign.sn, event);
    // LOG_PRINTF("DEBUG: dm uplink msg(%s)\n", buf);
    if (g_mqtt_cb.mqtt_publish(topic, buf, 3) != 0) {
        LOG_PRINTF("ERR: mqtt publish failed\n");
        mip_free((void **)&buf);
        return -3;
    }
    mip_free((void **)&buf);
    return 0;
}

int mip_dm_uplink_property(const char *msg)
{
    return mip_dm_uplink(NULL, NULL, "property", msg);
}

int mip_dm_uplink_response(dm_downlink_header_t *dh, dm_downlink_result_t *dres, const char *msg)
{
    return mip_dm_uplink(dh, dres, "response", msg);
}

int mip_dm_uplink_http(const char *url, const char *token, const char *msg)
{
    char full_url[128] = {0};
    snprintf(full_url, sizeof(full_url), "%s/api/v1/public/iot/device/%s/uplink/properties", url, g_sign.sn);
    return http_post(full_url, token, "TEMP_TOKEN", "property", msg);
}
