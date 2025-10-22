#include <sys/param.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_console.h"
#include "esp_http_server.h"
#include "esp_rom_crc.h"
#include "esp_timer.h"
#include "img_converters.h"
#include "system.h"
#include "esp_log.h"
#include "camera.h"
#include "debug.h"
#include "http.h"
#include "wifi.h"
#include "mqtt.h"
#include "sleep.h"
#include "ota.h"
#include "misc.h"
#include "morse.h"
#include "s2j.h"
#include "iot_mip.h"
#include "cat1.h"
#include "net_module.h"
#include "storage.h"
#include "utils.h"

#define TAG "-->HTTP"  // Logging tag for HTTP module

// Web server timeout (5 minutes)
#define WEB_TIMEOUT_SECONDS (60*5)

// Maximum HTTP buffer size
#define HTTP_BUFF_MAX_SIZE (8192)
// Embedded web resources
extern const char root_start[] asm("_binary_index_html_start");
extern const char root_end[] asm("_binary_index_html_end");
extern const char favicon_start[] asm("_binary_favicon_ico_start");
extern const char favicon_end[] asm("_binary_favicon_ico_end");
extern const char js_start[] asm("_binary_index_js_start");
extern const char js_end[] asm("_binary_index_js_end");
extern const char css_start[] asm("_binary_index_css_start");
extern const char css_end[] asm("_binary_index_css_end");

/**
 * HTTP response structure
 */
typedef struct httpResp {
    httpResult_e result;  ///< Response result code
} httpResp_t;

/**
 * HTTP server state
 */
typedef struct mdHttp {
    uint32_t webTimeoutSeconds;  ///< Seconds since last client activity
    esp_timer_handle_t timer;    ///< Timeout timer handle
    bool isLiveView;             ///< Live view streaming status
    bool hasClient;              ///< Client connection status
} mdHttp_t;

static mdHttp_t g_http = {0};  // Global HTTP server state

// MJPEG stream constants
#define PART_BOUNDARY "123456789000000000000987654321"
static const char *_STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *_STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char *_STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\nX-Timestamp: %d.%06d\r\n\r\n";

// HTTP server handles
static httpd_handle_t g_webServer = NULL;     // Main web server
static httpd_handle_t g_streamServer = NULL;  // Stream server

/**
 * Reset web timeout counter and mark client as active
 */
static void clear_timeout()
{
    g_http.webTimeoutSeconds = 0;
    g_http.hasClient = true;
}

/**
 * Get content from HTTP request
 * @param req HTTP request handle
 * @return Pointer to allocated content buffer, or NULL on failure
 */
static char *http_get_content_from_req(httpd_req_t *req)
{
    char *content = (char *)malloc(req->content_len);
    int ret = httpd_req_recv(req, content, req->content_len);
    if (ret <= 0) {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            httpd_resp_send_408(req);
        }
        free(content);
        return NULL;
    }
    return content;
}

/**
 * Free HTTP content buffer
 * @param content Content buffer to free
 */
static void http_free_content(char *content)
{
    if (content) {
        free(content);
    }
}

/**
 * Send JSON formatted HTTP response
 * @param req HTTP request handle
 * @param result Response result code
 */
static void http_send_json_response(httpd_req_t *req, uint32_t result)
{
    httpResp_t response = {result};
    char *str = NULL;

    s2j_create_json_obj(json_obj);

    httpd_resp_set_type(req, "application/json");
    s2j_json_set_basic_element(json_obj, &response, int, result);
    str = cJSON_PrintUnformatted(json_obj);
    httpd_resp_sendstr(req, str);
    cJSON_free(str);
    s2j_delete_json_obj(json_obj);
}
/**
 * Root page GET handler
 * @param req HTTP request handle
 * @return ESP_OK on success
 */
static esp_err_t get_root_handler(httpd_req_t *req)
{
    const uint32_t len = root_end - root_start;
    clear_timeout();
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, root_start, len);
    ESP_LOGI(TAG, "Serve root");
    return ESP_OK;
}

/**
 * Favicon GET handler
 * @param req HTTP request handle
 * @return ESP_OK on success
 */
static esp_err_t get_favicon_handler(httpd_req_t *req)
{
    const uint32_t len = favicon_end - favicon_start;
    clear_timeout();
    httpd_resp_set_type(req, "image/x-icon");
    httpd_resp_send(req, (const char *)favicon_start, len);
    return ESP_OK;
}

/**
 * JavaScript GET handler
 * @param req HTTP request handle
 * @return ESP_OK on success
 */
static esp_err_t get_js_handler(httpd_req_t *req)
{
    const uint32_t len = js_end - js_start;
    clear_timeout();
    httpd_resp_set_type(req, "text/javascript");
    httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=604800");
    httpd_resp_send(req, (const char *)js_start, len);
    return ESP_OK;
}
/**
 * CSS GET handler
 * @param req HTTP request handle
 * @return ESP_OK on success
 */
static esp_err_t get_css_handler(httpd_req_t *req)
{
    const uint32_t len = css_end - css_start;
    clear_timeout();
    httpd_resp_set_type(req, "text/css");
    httpd_resp_send(req, (const char *)css_start, len);
    return ESP_OK;
}
/**
 * 404 Error handler - Redirects to root page
 * @param req HTTP request handle
 * @param err Error code
 * @return ESP_OK
 */
static esp_err_t error_404_handler(httpd_req_t *req, httpd_err_code_t err)
{
    // httpd_resp_send_404(req);
    return ESP_OK;
}

esp_err_t get_cam_param_handle(httpd_req_t *req)
{
    ESP_LOGI(TAG, "%s", req->uri);
    imgAttr_t image;
    char *str = NULL;
    clear_timeout();
    httpd_resp_set_type(req, "application/json");

    cfg_get_image_attr(&image);
    // 因为相机默认是开启水平翻转的，返回WEB时开关状态取反，来达到开关关闭时图像是正常的效果
    // Because the camera defaults to horizontal flipping, when returning to WEB,
    // the switch status is reversed to achieve the effect that the image is normal when the switch is turned off
    image.bHorizonetal = !image.bHorizonetal;
    /* create Student JSON object */
    s2j_create_json_obj(json_obj);
    /* serialize data to JSON object. */
    s2j_json_set_basic_element(json_obj, &image, int, brightness);
    s2j_json_set_basic_element(json_obj, &image, int, contrast);
    s2j_json_set_basic_element(json_obj, &image, int, saturation);
    s2j_json_set_basic_element(json_obj, &image, int, aeLevel);
    s2j_json_set_basic_element(json_obj, &image, int, bAgc);
    s2j_json_set_basic_element(json_obj, &image, int, gain);
    s2j_json_set_basic_element(json_obj, &image, int, gainCeiling);
    s2j_json_set_basic_element(json_obj, &image, int, bHorizonetal);
    s2j_json_set_basic_element(json_obj, &image, int, bVertical);
    // Apply camera settings
    s2j_json_set_basic_element(json_obj, &image, int, quality);
    s2j_json_set_basic_element(json_obj, &image, int, sharpness);
    s2j_json_set_basic_element(json_obj, &image, int, denoise);
    s2j_json_set_basic_element(json_obj, &image, int, specialEffect);
    s2j_json_set_basic_element(json_obj, &image, int, bAwb);
    s2j_json_set_basic_element(json_obj, &image, int, bAwbGain);
    s2j_json_set_basic_element(json_obj, &image, int, wbMode);
    s2j_json_set_basic_element(json_obj, &image, int, bAec);
    s2j_json_set_basic_element(json_obj, &image, int, bAec2);
    s2j_json_set_basic_element(json_obj, &image, int, aecValue);
    s2j_json_set_basic_element(json_obj, &image, int, bBpc);
    s2j_json_set_basic_element(json_obj, &image, int, bWpc);
    s2j_json_set_basic_element(json_obj, &image, int, bRawGma);
    s2j_json_set_basic_element(json_obj, &image, int, bLenc);
    s2j_json_set_basic_element(json_obj, &image, int, bDcw);
    s2j_json_set_basic_element(json_obj, &image, int, bColorbar);

    str = cJSON_PrintUnformatted(json_obj);
    httpd_resp_sendstr(req, str);
    cJSON_free(str);
    s2j_delete_json_obj(json_obj);
    return ESP_OK;
}

esp_err_t set_cam_param_handle(httpd_req_t *req)
{
    ESP_LOGI(TAG, "%s", req->uri);
    clear_timeout();
    char *content = http_get_content_from_req(req);
    if (content) {
        s2j_create_struct_obj(image, imgAttr_t);
        cfg_get_image_attr(image);
        /* deserialize data to Student structure object. */
        cJSON *json = cJSON_Parse(content);
        s2j_struct_get_basic_element(image, json, int, brightness);
        s2j_struct_get_basic_element(image, json, int, contrast);
        s2j_struct_get_basic_element(image, json, int, saturation);
        s2j_struct_get_basic_element(image, json, int, aeLevel);
        s2j_struct_get_basic_element(image, json, int, bAgc);
        s2j_struct_get_basic_element(image, json, int, gain);
        s2j_struct_get_basic_element(image, json, int, gainCeiling);
        s2j_struct_get_basic_element(image, json, int, bHorizonetal);
        s2j_struct_get_basic_element(image, json, int, bVertical);
        // Apply camera settings
        if (cJSON_HasObjectItem(json, "quality")) {
            s2j_struct_get_basic_element(image, json, int, quality);
            s2j_struct_get_basic_element(image, json, int, sharpness);
            s2j_struct_get_basic_element(image, json, int, denoise);
            s2j_struct_get_basic_element(image, json, int, specialEffect);
            s2j_struct_get_basic_element(image, json, int, bAwb);
            s2j_struct_get_basic_element(image, json, int, bAwbGain);
            s2j_struct_get_basic_element(image, json, int, wbMode);
            s2j_struct_get_basic_element(image, json, int, bAec);
            s2j_struct_get_basic_element(image, json, int, bAec2);
            s2j_struct_get_basic_element(image, json, int, aecValue);
            s2j_struct_get_basic_element(image, json, int, bBpc);
            s2j_struct_get_basic_element(image, json, int, bWpc);
            s2j_struct_get_basic_element(image, json, int, bRawGma);
            s2j_struct_get_basic_element(image, json, int, bLenc);
            s2j_struct_get_basic_element(image, json, int, bDcw);
            s2j_struct_get_basic_element(image, json, int, bColorbar);
        }

        // 因为返回WEB时开关状态是取反的，再接收前端配置时需再次取反恢复
        // Because the switch status is reversed when returning to WEB, it needs to be reversed again when receiving front-end configuration for restoration
        image->bHorizonetal = !image->bHorizonetal;
        if (camera_set_image(image, false) == ESP_OK) {
            http_send_json_response(req, RES_OK);
            cfg_set_image_attr(image);
        } else {
            http_send_json_response(req, RES_FAIL);
        }
        s2j_delete_struct_obj(image);
        s2j_delete_json_obj(json);
        http_free_content(content);
        return ESP_OK;
    }
    return ESP_FAIL;
}

esp_err_t get_light_param_handle(httpd_req_t *req)
{
    ESP_LOGI(TAG, "%s", req->uri);
    lightAttr_t light;
    char *str = NULL;

    clear_timeout();
    httpd_resp_set_type(req, "application/json");

    cfg_get_light_attr(&light);
    light.value = misc_get_light_value_rate();
    /* create Student JSON object */
    s2j_create_json_obj(json_obj);
    /* serialize data to JSON object. */
    s2j_json_set_basic_element(json_obj, &light, int, lightMode);
    s2j_json_set_basic_element(json_obj, &light, int, threshold);
    s2j_json_set_basic_element(json_obj, &light, int, value);
    s2j_json_set_basic_element(json_obj, &light, int, duty);
    s2j_json_set_basic_element(json_obj, &light, string, startTime);
    s2j_json_set_basic_element(json_obj, &light, string, endTime);
    str = cJSON_PrintUnformatted(json_obj);
    httpd_resp_sendstr(req, str);
    cJSON_free(str);
    s2j_delete_json_obj(json_obj);
    return ESP_OK;
}

esp_err_t set_light_param_handle(httpd_req_t *req)
{
    ESP_LOGI(TAG, "%s", req->uri);
    clear_timeout();
    char *content = http_get_content_from_req(req);
    if (content) {
        s2j_create_struct_obj(light, lightAttr_t);
        /* deserialize data to Student structure object. */
        cJSON *json = cJSON_Parse(content);
        cfg_get_light_attr(light);
        s2j_struct_get_basic_element(light, json, int, lightMode);
        s2j_struct_get_basic_element(light, json, int, threshold);
        s2j_struct_get_basic_element(light, json, int, duty);
        s2j_struct_get_basic_element(light, json, string, startTime);
        s2j_struct_get_basic_element(light, json, string, endTime);
        if (camera_flash_led_ctrl(light) == ESP_OK) {
            http_send_json_response(req, RES_OK);
            cfg_set_light_attr(light);
        } else {
            http_send_json_response(req, RES_FAIL);
        }
        misc_set_flash_duty(light->duty); /*实时更新当前占空比*/
        s2j_delete_struct_obj(light);
        s2j_delete_json_obj(json);
        http_free_content(content);
        return ESP_OK;
    }
    return ESP_FAIL;
}

static cJSON *struct_to_json_timedNode_t(void *struct_obj)
{
    s2j_create_json_obj(json_obj_);
    timedNode_t *struct_obj_ = (timedNode_t *)struct_obj;
    s2j_json_set_basic_element(json_obj_, struct_obj_, int, day);
    s2j_json_set_basic_element(json_obj_, struct_obj_, string, time);
    return json_obj_;
}

esp_err_t get_cap_param_handle(httpd_req_t *req)
{
    ESP_LOGI(TAG, "%s", req->uri);
    capAttr_t capture;
    char *str = NULL;

    clear_timeout();
    httpd_resp_set_type(req, "application/json");

    cfg_get_cap_attr(&capture);
    /* create Student JSON object */
    s2j_create_json_obj(json_obj);
    /* serialize data to JSON object. */
    s2j_json_set_basic_element(json_obj, &capture, int, bScheCap);
    s2j_json_set_basic_element(json_obj, &capture, int, bAlarmInCap);
    s2j_json_set_basic_element(json_obj, &capture, int, bButtonCap);
    s2j_json_set_basic_element(json_obj, &capture, int, scheCapMode);
    s2j_json_set_basic_element(json_obj, &capture, int, intervalValue);
    s2j_json_set_basic_element(json_obj, &capture, int, intervalUnit);
    s2j_json_set_basic_element(json_obj, &capture, int, timedCount);
    s2j_json_set_struct_array_element_by_func(json_obj, &capture, timedNode_t, timedNodes, capture.timedCount);

    str = cJSON_PrintUnformatted(json_obj);
    httpd_resp_sendstr(req, str);
    cJSON_free(str);
    s2j_delete_json_obj(json_obj);

    return ESP_OK;
}

void *json_to_struct_timedNode_t(cJSON *json_obj)
{
    s2j_create_struct_obj(struct_obj_, timedNode_t);
    s2j_struct_get_basic_element(struct_obj_, json_obj, int, day);
    s2j_struct_get_basic_element(struct_obj_, json_obj, string, time);
    return struct_obj_;
}

esp_err_t set_cap_param_handle(httpd_req_t *req)
{
    ESP_LOGI(TAG, "%s", req->uri);
    clear_timeout();

    char *content = http_get_content_from_req(req);
    if (content) {
        s2j_create_struct_obj(capture, capAttr_t);
        /* deserialize data to Student structure object. */
        cJSON *json = cJSON_Parse(content);
        cfg_get_cap_attr(capture);
        s2j_struct_get_basic_element(capture, json, int, bScheCap);
        s2j_struct_get_basic_element(capture, json, int, bAlarmInCap);
        s2j_struct_get_basic_element(capture, json, int, bButtonCap);
        s2j_struct_get_basic_element(capture, json, int, scheCapMode);
        s2j_struct_get_basic_element(capture, json, int, intervalValue);
        s2j_struct_get_basic_element(capture, json, int, intervalUnit);
        s2j_struct_get_basic_element(capture, json, int, timedCount);
        s2j_struct_get_struct_array_element_by_func(capture, json, timedNode_t, timedNodes);
        http_send_json_response(req, RES_OK);
        cfg_set_cap_attr(capture);
        sleep_set_last_capture_time(time(NULL));
        s2j_delete_struct_obj(capture);
        s2j_delete_json_obj(json);
        http_free_content(content);
        return ESP_OK;
    }
    return ESP_FAIL;
}

esp_err_t get_upload_param_handle(httpd_req_t *req)
{
    ESP_LOGI(TAG, "%s", req->uri);
    uploadAttr_t upload;
    char *str = NULL;
    clear_timeout();

    httpd_resp_set_type(req, "application/json");

    cfg_get_upload_attr(&upload);
    /* create Student JSON object */
    s2j_create_json_obj(json_obj);
    /* serialize data to JSON object. */
    s2j_json_set_basic_element(json_obj, &upload, int, uploadMode);
    s2j_json_set_basic_element(json_obj, &upload, int, retryCount);
    s2j_json_set_basic_element(json_obj, &upload, int, timedCount);
    s2j_json_set_struct_array_element_by_func(json_obj, &upload, timedNode_t, timedNodes, upload.timedCount);
    str = cJSON_PrintUnformatted(json_obj);
    httpd_resp_sendstr(req, str);
    cJSON_free(str);
    s2j_delete_json_obj(json_obj);
    return ESP_OK;
}

esp_err_t set_upload_param_handle(httpd_req_t *req)
{
    ESP_LOGI(TAG, "%s", req->uri);
    clear_timeout();

    char *content = http_get_content_from_req(req);
    if (content) {
        s2j_create_struct_obj(upload, uploadAttr_t);
        /* deserialize data to Student structure object. */
        cJSON *json = cJSON_Parse(content);
        cfg_get_upload_attr(upload);
        s2j_struct_get_basic_element(upload, json, int, uploadMode);
        s2j_struct_get_basic_element(upload, json, int, retryCount);
        s2j_struct_get_basic_element(upload, json, int, timedCount);
        s2j_struct_get_struct_array_element_by_func(upload, json, timedNode_t, timedNodes);
        http_send_json_response(req, RES_OK);
        cfg_set_upload_attr(upload);
        if (upload->uploadMode == 0) {
            storage_upload_start();
        } else {
            storage_upload_stop();
        }
        s2j_delete_struct_obj(upload);
        s2j_delete_json_obj(json);
        http_free_content(content);
        return ESP_OK;
    }
    return ESP_FAIL;
}

esp_err_t get_wifi_param_handle(httpd_req_t *req)
{
    ESP_LOGI(TAG, "%s", req->uri);
    wifiAttr_t wifi;
    char *str = NULL;
    clear_timeout();

    httpd_resp_set_type(req, "application/json");

    cfg_get_wifi_attr(&wifi);
    wifi.isConnected = wifi_sta_is_connected(); // debug
    /* create Student JSON object */
    s2j_create_json_obj(json_obj);
    /* serialize data to JSON object. */
    s2j_json_set_basic_element(json_obj, &wifi, string, ssid);
    s2j_json_set_basic_element(json_obj, &wifi, string, password);
    s2j_json_set_basic_element(json_obj, &wifi, int, isConnected);
    str = cJSON_PrintUnformatted(json_obj);
    httpd_resp_sendstr(req, str);
    cJSON_free(str);
    s2j_delete_json_obj(json_obj);
    return ESP_OK;
}

esp_err_t set_wifi_param_handle(httpd_req_t *req)
{
    ESP_LOGI(TAG, "%s", req->uri);
    clear_timeout();

    char *content = http_get_content_from_req(req);
    if (content) {
        s2j_create_struct_obj(wifi, wifiAttr_t);
        /* deserialize data to Student structure object. */
        cJSON *json = cJSON_Parse(content);
        cfg_get_wifi_attr(wifi);
        s2j_struct_get_basic_element(wifi, json, string, ssid);
        s2j_struct_get_basic_element(wifi, json, string, password);
        if (wifi_sta_reconnect(wifi->ssid, wifi->password) == ESP_OK) {
            ESP_LOGI(TAG, "WIFI connect success");
            http_send_json_response(req, RES_WIFI_CONNECTED);
            cfg_set_wifi_attr(wifi);
        } else {
            ESP_LOGI(TAG, "WIFI connect failed");
            http_send_json_response(req, RES_WIFI_DISCONNECTED);
        }
        s2j_delete_struct_obj(wifi);
        s2j_delete_json_obj(json);
        http_free_content(content);
        return ESP_OK;
    }
    return ESP_FAIL;
}

static cJSON *struct_to_json_wifiNode_t(void *struct_obj)
{
    s2j_create_json_obj(json_obj_);
    wifiNode_t *struct_obj_ = (wifiNode_t *)struct_obj;
    s2j_json_set_basic_element(json_obj_, struct_obj_, string, ssid);
    s2j_json_set_basic_element(json_obj_, struct_obj_, int, rssi);
    s2j_json_set_basic_element(json_obj_, struct_obj_, int, bAuthenticate);
    return json_obj_;
}

esp_err_t get_wifi_list_handle(httpd_req_t *req)
{
    ESP_LOGI(TAG, "%s", req->uri);
    wifiList_t wlist;
    char *str = NULL;
    clear_timeout();

    httpd_resp_set_type(req, "application/json");

    if (wifi_get_list(&wlist) == ESP_OK) {
        s2j_create_json_obj(json_obj);
        /* serialize data to JSON object. */
        s2j_json_set_basic_element(json_obj, &wlist, int, count);
        s2j_json_set_struct_array_element_by_func(json_obj, &wlist, wifiNode_t, nodes, wlist.count);
        str = cJSON_PrintUnformatted(json_obj);
        httpd_resp_sendstr(req, str);
        cJSON_free(str);
        s2j_delete_json_obj(json_obj);
        wifi_put_list(&wlist);
        return ESP_OK;
    }
    /* create Student JSON object */
    return ESP_FAIL;
}

esp_err_t get_dev_info_handle(httpd_req_t *req)
{
    ESP_LOGI(TAG, "%s", req->uri);
    deviceInfo_t device;
    char *str = NULL;
    clear_timeout();

    httpd_resp_set_type(req, "application/json");

    cfg_get_device_info(&device);
    /* create Student JSON object */
    s2j_create_json_obj(json_obj);
    /* serialize data to JSON object. */
    s2j_json_set_basic_element(json_obj, &device, string, name);
    s2j_json_set_basic_element(json_obj, &device, string, mac);
    s2j_json_set_basic_element(json_obj, &device, string, sn);
    s2j_json_set_basic_element(json_obj, &device, string, hardVersion);
    s2j_json_set_basic_element(json_obj, &device, string, softVersion);
    s2j_json_set_basic_element(json_obj, &device, string, model);
    s2j_json_set_basic_element(json_obj, &device, string, countryCode);
    s2j_json_set_basic_element(json_obj, &device, string, netmod);
    s2j_json_set_basic_element(json_obj, &device, string, camera);
    str = cJSON_PrintUnformatted(json_obj);
    httpd_resp_sendstr(req, str);
    cJSON_free(str);
    s2j_delete_json_obj(json_obj);
    return ESP_OK;
}

esp_err_t set_dev_info_handle(httpd_req_t *req)
{
    ESP_LOGI(TAG, "%s", req->uri);
    clear_timeout();

    char *content = http_get_content_from_req(req);
    if (content) {
        s2j_create_struct_obj(device, deviceInfo_t);
        cfg_get_device_info(device);
        /* deserialize data to Student structure object. */
        cJSON *json = cJSON_Parse(content);
        s2j_struct_get_basic_element(device, json, string, name);
        s2j_struct_get_basic_element(device, json, string, mac);
        s2j_struct_get_basic_element(device, json, string, sn);
        s2j_struct_get_basic_element(device, json, string, hardVersion);
        s2j_struct_get_basic_element(device, json, string, softVersion);
        s2j_struct_get_basic_element(device, json, string, model);
        s2j_struct_get_basic_element(device, json, string, countryCode);

        if (netModule_is_mmwifi()) {
            mm_wifi_set_country_code(device->countryCode);

        }

        http_send_json_response(req, RES_OK);
        cfg_set_device_info(device);
        s2j_delete_struct_obj(device);
        s2j_delete_json_obj(json);
        http_free_content(content);
        return ESP_OK;
    }
    return ESP_FAIL;
}

esp_err_t get_mqtt_param_handle(httpd_req_t *req)
{
    ESP_LOGI(TAG, "%s", req->uri);
    mqttAttr_t mqtt;
    deviceInfo_t device;
    char *str = NULL;
    clear_timeout();

    httpd_resp_set_type(req, "application/json");

    cfg_get_mqtt_attr(&mqtt);
    cfg_get_device_info(&device);
    if (cfg_is_undefined(mqtt.user)) {
        strcpy(mqtt.user, device.sn);
    }
    /* create Student JSON object */
    s2j_create_json_obj(json_obj);
    /* serialize data to JSON object. */
    s2j_json_set_basic_element(json_obj, &mqtt, string, host);
    s2j_json_set_basic_element(json_obj, &mqtt, string, user);
    s2j_json_set_basic_element(json_obj, &mqtt, string, password);
    s2j_json_set_basic_element(json_obj, &mqtt, string, topic);
    s2j_json_set_basic_element(json_obj, &mqtt, int, port);
    s2j_json_set_basic_element(json_obj, &mqtt, int, tlsEnable);
    s2j_json_set_basic_element(json_obj, &mqtt, string, caName);
    s2j_json_set_basic_element(json_obj, &mqtt, string, certName);
    s2j_json_set_basic_element(json_obj, &mqtt, string, keyName);
    str = cJSON_PrintUnformatted(json_obj);
    httpd_resp_sendstr(req, str);
    cJSON_free(str);
    s2j_delete_json_obj(json_obj);
    return ESP_OK;
}

esp_err_t set_mqtt_param_handle(httpd_req_t *req)
{
    ESP_LOGI(TAG, "%s", req->uri);
    clear_timeout();

    char *content = http_get_content_from_req(req);
    if (content) {
        s2j_create_struct_obj(mqtt, mqttAttr_t);
        /* deserialize data to Student structure object. */
        cJSON *json = cJSON_Parse(content);
        cfg_get_mqtt_attr(mqtt);
        s2j_struct_get_basic_element(mqtt, json, string, host);
        s2j_struct_get_basic_element(mqtt, json, string, user);
        s2j_struct_get_basic_element(mqtt, json, string, password);
        s2j_struct_get_basic_element(mqtt, json, string, topic);
        s2j_struct_get_basic_element(mqtt, json, int, port);
        s2j_struct_get_basic_element(mqtt, json, int, tlsEnable);
        s2j_struct_get_basic_element(mqtt, json, string, caName);
        s2j_struct_get_basic_element(mqtt, json, string, certName);
        s2j_struct_get_basic_element(mqtt, json, string, keyName);
        http_send_json_response(req, RES_OK);
        cfg_set_mqtt_attr(mqtt);
        s2j_delete_struct_obj(mqtt);
        s2j_delete_json_obj(json);
        http_free_content(content);
        if (wifi_sta_is_connected() || netModule_is_cat1()) {
            mqtt_restart();
        }
        return ESP_OK;
    }
    return ESP_FAIL;
}

esp_err_t get_platform_param_handle(httpd_req_t *req)
{
    // ESP_LOGI(TAG, "%s", req->uri);

    // clear_timeout(); //deleted!  web can connect to the server every 2 seconds

    httpd_resp_set_type(req, "application/json");

    platformParamAttr_t param;
    cfg_get_platform_param_attr(&param);

    param.mqttPlatform.isConnected = mqtt_mip_is_connected(); 
    //
    s2j_create_json_obj(json_obj);
    s2j_json_set_basic_element(json_obj, &param, int, currentPlatformType);

    s2j_json_set_struct_element(json_sensing, json_obj, sensingParam, &param, sensingPlatformAttr_t, sensingPlatform);
    s2j_json_set_basic_element(json_sensing, sensingParam, int, platformType);
    s2j_json_set_basic_element(json_sensing, sensingParam, string, platformName);
    s2j_json_set_basic_element(json_sensing, sensingParam, string, host);
    s2j_json_set_basic_element(json_sensing, sensingParam, int, mqttPort);
    s2j_json_set_basic_element(json_sensing, sensingParam, int, httpPort);

    s2j_json_set_struct_element(json_mqtt, json_obj, mqttParam, &param, mqttPlatformAttr_t, mqttPlatform);
    s2j_json_set_basic_element(json_mqtt, mqttParam, int, platformType);
    s2j_json_set_basic_element(json_mqtt, mqttParam, string, platformName);
    s2j_json_set_basic_element(json_mqtt, mqttParam, string, host);
    s2j_json_set_basic_element(json_mqtt, mqttParam, int, mqttPort);
    s2j_json_set_basic_element(json_mqtt, mqttParam, string, topic);
    s2j_json_set_basic_element(json_mqtt, mqttParam, string, clientId);
    s2j_json_set_basic_element(json_mqtt, mqttParam, int, qos);
    s2j_json_set_basic_element(json_mqtt, mqttParam, string, username);
    s2j_json_set_basic_element(json_mqtt, mqttParam, string, password);
    s2j_json_set_basic_element(json_mqtt, mqttParam, int, isConnected);
    s2j_json_set_basic_element(json_mqtt, mqttParam, int, tlsEnable);
    s2j_json_set_basic_element(json_mqtt, mqttParam, string, caName);
    s2j_json_set_basic_element(json_mqtt, mqttParam, string, certName);
    s2j_json_set_basic_element(json_mqtt, mqttParam, string, keyName);

    char *str = cJSON_PrintUnformatted(json_obj);
    httpd_resp_sendstr(req, str);
    cJSON_free(str);
    s2j_delete_json_obj(json_obj);
    return ESP_OK;
}

esp_err_t set_platform_param_handle(httpd_req_t *req)
{
    ESP_LOGI(TAG, "%s", req->uri);
    clear_timeout();

    char *content = http_get_content_from_req(req);
    if (content) {
        cJSON *json = cJSON_Parse(content);

        s2j_create_struct_obj(param, platformParamAttr_t);
        cfg_get_platform_param_attr(param);
        s2j_struct_get_basic_element(param, json, int, currentPlatformType);

        switch (param->currentPlatformType) {
            case PLATFORM_TYPE_SENSING: {
                s2j_struct_get_struct_element(sensingParam, param, json_sensing, json, sensingPlatformAttr_t, sensingPlatform);
                s2j_struct_get_basic_element(sensingParam, json_sensing, string, host);
                s2j_struct_get_basic_element(sensingParam, json_sensing, int, mqttPort);
                s2j_struct_get_basic_element(sensingParam, json_sensing, int, httpPort);
                break;
            }
            case PLATFORM_TYPE_MQTT: {
                s2j_struct_get_struct_element(mqttParam, param, json_mqtt, json, mqttPlatformAttr_t, mqttPlatform);
                s2j_struct_get_basic_element(mqttParam, json_mqtt, string, host);
                s2j_struct_get_basic_element(mqttParam, json_mqtt, int, mqttPort);
                s2j_struct_get_basic_element(mqttParam, json_mqtt, string, topic);
                s2j_struct_get_basic_element(mqttParam, json_mqtt, string, clientId);
                s2j_struct_get_basic_element(mqttParam, json_mqtt, int, qos);
                s2j_struct_get_basic_element(mqttParam, json_mqtt, string, username);
                s2j_struct_get_basic_element(mqttParam, json_mqtt, string, password);
                s2j_struct_get_basic_element(mqttParam, json_mqtt, int, tlsEnable);
                s2j_struct_get_basic_element(mqttParam, json_mqtt, string, caName);
                s2j_struct_get_basic_element(mqttParam, json_mqtt, string, certName);
                s2j_struct_get_basic_element(mqttParam, json_mqtt, string, keyName);
                break;
            }
            default:
                break;
        }

        http_send_json_response(req, RES_OK);
        cfg_set_platform_param_attr(param);
        s2j_delete_struct_obj(param);
        s2j_delete_json_obj(json);
        http_free_content(content);
        if (wifi_sta_is_connected() || netModule_is_cat1()) {
            mqtt_restart();
        }
        return ESP_OK;
    }
    return ESP_FAIL;
}

esp_err_t get_iot_param_handle(httpd_req_t *req)
{
    ESP_LOGI(TAG, "%s", req->uri);
    IoTAttr_t iot;
    char *str = NULL;
    clear_timeout();

    httpd_resp_set_type(req, "application/json");

    cfg_get_iot_attr(&iot);
    /* create Student JSON object */
    s2j_create_json_obj(json_obj);
    /* serialize data to JSON object. */
    s2j_json_set_basic_element(json_obj, &iot, int, autop_enable);
    s2j_json_set_basic_element(json_obj, &iot, int, dm_enable);
    str = cJSON_PrintUnformatted(json_obj);
    httpd_resp_sendstr(req, str);
    cJSON_free(str);
    s2j_delete_json_obj(json_obj);
    return ESP_OK;
}

esp_err_t set_iot_param_handle(httpd_req_t *req)
{
    ESP_LOGI(TAG, "%s", req->uri);
    clear_timeout();
    bool last_autop_enable = false;
    bool last_dm_enable = false;

    char *content = http_get_content_from_req(req);
    if (content) {
        s2j_create_struct_obj(iot, IoTAttr_t);
        /* deserialize data to Student structure object. */
        cJSON *json = cJSON_Parse(content);
        cfg_get_iot_attr(iot);
        last_autop_enable = iot->autop_enable;
        last_dm_enable = iot->dm_enable;
        s2j_struct_get_basic_element(iot, json, int, autop_enable);
        s2j_struct_get_basic_element(iot, json, int, dm_enable);
        http_send_json_response(req, RES_OK);
        cfg_set_iot_attr(iot);
        if (last_autop_enable != iot->autop_enable) {
            iot_mip_autop_enable(iot->autop_enable);
        }
        if (last_dm_enable != iot->dm_enable) {
            mqtt_stop();
            iot_mip_dm_enable(iot->dm_enable);
            mqtt_start();
        }
        s2j_delete_struct_obj(iot);
        s2j_delete_json_obj(json);
        http_free_content(content);
        return ESP_OK;
    }
    return ESP_FAIL;
}

esp_err_t get_cellular_param_handle(httpd_req_t *req)
{
    ESP_LOGI(TAG, "%s", req->uri);

    clear_timeout();

    httpd_resp_set_type(req, "application/json");

    cellularParamAttr_t param;
    cfg_get_cellular_param_attr(&param);

    s2j_create_json_obj(json_obj);
    s2j_json_set_basic_element(json_obj, &param, string, imei);
    s2j_json_set_basic_element(json_obj, &param, string, apn);
    s2j_json_set_basic_element(json_obj, &param, string, user);
    s2j_json_set_basic_element(json_obj, &param, string, password);
    s2j_json_set_basic_element(json_obj, &param, string, pin);
    s2j_json_set_basic_element(json_obj, &param, int, authentication);

    char *str = cJSON_PrintUnformatted(json_obj);
    httpd_resp_sendstr(req, str);
    cJSON_free(str);
    s2j_delete_json_obj(json_obj);
    return ESP_OK;
}

esp_err_t set_cellular_param_handle(httpd_req_t *req)
{
    ESP_LOGI(TAG, "%s", req->uri);

    clear_timeout();

    char *content = http_get_content_from_req(req);
    if (content) {
        cJSON *json = cJSON_Parse(content);

        s2j_create_struct_obj(param, cellularParamAttr_t);
        cfg_get_cellular_param_attr(param);
        s2j_struct_get_basic_element(param, json, string, imei);
        s2j_struct_get_basic_element(param, json, string, apn);
        s2j_struct_get_basic_element(param, json, string, user);
        s2j_struct_get_basic_element(param, json, string, password);
        s2j_struct_get_basic_element(param, json, string, pin);
        s2j_struct_get_basic_element(param, json, int, authentication);

        cfg_set_cellular_param_attr(param);
        cat1_restart();

        http_send_json_response(req, RES_OK);
        s2j_delete_struct_obj(param);
        s2j_delete_json_obj(json);
        http_free_content(content);
        return ESP_OK;
    } else {
        return ESP_FAIL;
    }
}

esp_err_t send_cellular_command_handle(httpd_req_t *req)
{
    ESP_LOGI(TAG, "%s", req->uri);

    clear_timeout();

    char *content = http_get_content_from_req(req);
    if (content) {
        cJSON *json = cJSON_Parse(content);
        s2j_create_struct_obj(param, cellularCommand_t);
        s2j_struct_get_basic_element(param, json, string, command);

        cellularCommandResp_t commandResp;
        memset(&commandResp, 0, sizeof(cellularCommandResp_t));
        cat1_send_at(param->command, &commandResp);

        s2j_create_json_obj(json_obj);
        s2j_json_set_basic_element(json_obj, &commandResp, int, result);
        s2j_json_set_basic_element(json_obj, &commandResp, string, message);
        char *str = cJSON_PrintUnformatted(json_obj);
        httpd_resp_sendstr(req, str);

        s2j_delete_struct_obj(param);
        s2j_delete_json_obj(json);
        http_free_content(content);

        cJSON_free(str);
        s2j_delete_json_obj(json_obj);

        return ESP_OK;
    } else {
        return ESP_FAIL;
    }
}

esp_err_t get_cellular_status_handle(httpd_req_t *req)
{
    ESP_LOGI(TAG, "%s", req->uri);

    clear_timeout();

    httpd_resp_set_type(req, "application/json");

    cellularStatusAttr_t param;
    memset(&param, 0, sizeof(cellularStatusAttr_t));
    cat1_get_cellular_status(&param);

    s2j_create_json_obj(json_obj);
    s2j_json_set_basic_element(json_obj, &param, string, networkStatus);
    s2j_json_set_basic_element(json_obj, &param, string, modemStatus);
    s2j_json_set_basic_element(json_obj, &param, string, model);
    s2j_json_set_basic_element(json_obj, &param, string, version);
    s2j_json_set_basic_element(json_obj, &param, string, signalLevel);
    s2j_json_set_basic_element(json_obj, &param, string, registerStatus);
    s2j_json_set_basic_element(json_obj, &param, string, imei);
    s2j_json_set_basic_element(json_obj, &param, string, imsi);
    s2j_json_set_basic_element(json_obj, &param, string, iccid);
    s2j_json_set_basic_element(json_obj, &param, string, isp);
    s2j_json_set_basic_element(json_obj, &param, string, networkType);
    s2j_json_set_basic_element(json_obj, &param, string, plmnId);
    s2j_json_set_basic_element(json_obj, &param, string, lac);
    s2j_json_set_basic_element(json_obj, &param, string, cellId);
    s2j_json_set_basic_element(json_obj, &param, string, ipv4Address);
    s2j_json_set_basic_element(json_obj, &param, string, ipv4Gateway);
    s2j_json_set_basic_element(json_obj, &param, string, ipv4Dns);
    s2j_json_set_basic_element(json_obj, &param, string, ipv6Address);
    s2j_json_set_basic_element(json_obj, &param, string, ipv6Gateway);
    s2j_json_set_basic_element(json_obj, &param, string, ipv6Dns);

    char *str = cJSON_PrintUnformatted(json_obj);
    httpd_resp_sendstr(req, str);
    cJSON_free(str);
    s2j_delete_json_obj(json_obj);
    return ESP_OK;
}

esp_err_t get_dev_battery_handle(httpd_req_t *req)
{
    ESP_LOGI(TAG, "%s", req->uri);
    batteryAttr_t battery;
    char *str = NULL;
    clear_timeout();

    httpd_resp_set_type(req, "application/json");

    /* get the battery */
    // todo
    battery.bBattery = true;
    battery.freePercent = misc_get_battery_voltage_rate();
    /* create Student JSON object */
    s2j_create_json_obj(json_obj);
    /* serialize data to JSON object. */
    s2j_json_set_basic_element(json_obj, &battery, int, bBattery);
    s2j_json_set_basic_element(json_obj, &battery, int, freePercent);
    str = cJSON_PrintUnformatted(json_obj);
    httpd_resp_sendstr(req, str);
    cJSON_free(str);
    s2j_delete_json_obj(json_obj);
    return ESP_OK;
}

esp_err_t get_dev_time_handle(httpd_req_t *req)
{
    ESP_LOGI(TAG, "%s", req->uri);
    timeAttr_t time;
    char *str = NULL;
    clear_timeout();

    httpd_resp_set_type(req, "application/json");

    system_get_time(&time);
    /* create Student JSON object */
    s2j_create_json_obj(json_obj);
    /* serialize data to JSON object. */
    s2j_json_set_basic_element(json_obj, &time, string, tz);
    s2j_json_set_basic_element(json_obj, &time, int, ts);
    str = cJSON_PrintUnformatted(json_obj);
    httpd_resp_sendstr(req, str);
    cJSON_free(str);
    s2j_delete_json_obj(json_obj);
    return ESP_OK;
}

esp_err_t set_dev_time_handle(httpd_req_t *req)
{
    ESP_LOGI(TAG, "%s", req->uri);
    clear_timeout();

    char *content = http_get_content_from_req(req);
    if (content) {
        s2j_create_struct_obj(time, timeAttr_t);
        /* deserialize data to Student structure object. */
        cJSON *json = cJSON_Parse(content);

        s2j_struct_get_basic_element(time, json, string, tz);
        s2j_struct_get_basic_element(time, json, int, ts);
        if (system_set_time(time) != ESP_OK) {
            http_send_json_response(req, RES_FAIL);
        } else {
            http_send_json_response(req, RES_OK);
        }
        s2j_delete_struct_obj(time);
        s2j_delete_json_obj(json);
        http_free_content(content);
        return ESP_OK;
    }
    return ESP_FAIL;
}

esp_err_t set_dev_sleep_handle(httpd_req_t *req)
{
    ESP_LOGI(TAG, "%s", req->uri);
    clear_timeout();
    http_send_json_response(req, RES_OK);
    sleep_set_event_bits(SLEEP_NO_OPERATION_TIMEOUT_BIT);
    return ESP_OK;
}

esp_err_t set_dev_upgrade_handle(httpd_req_t *req)
{
    /* Content length of the request gives
     * the size of the file being uploaded */
    ESP_LOGI(TAG, "%s", req->uri);
    char *buf = malloc(HTTP_BUFF_MAX_SIZE);
    int remaining = req->content_len;
    int received = 0;
    int timeout = 0;
    uint32_t crc = 0;
    httpResult_e res = RES_OK;
    bool image_header_was_checked = false;
    otaHandle_t handle;
    netModule_deinit();
    while (remaining > 0) {
        ESP_LOGI(TAG, "Remaining size : %d", remaining);
        /* Receive the file part by part into a buffer */
        if ((received = httpd_req_recv(req, buf, MIN(remaining, HTTP_BUFF_MAX_SIZE))) <= 0) {
            if (received == HTTPD_SOCK_ERR_TIMEOUT) {
                /* Retry if timeout occurred */
                timeout++;
                if (timeout < 3) {
                    ESP_LOGW(TAG, "timeout occurred");
                    continue;
                }
            }
            /* In case of unrecoverable error,
             * close and delete the unfinished file*/
            ESP_LOGE(TAG, "File reception failed!");
            /* Respond with 500 Internal Server Error */
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to receive file");
            res = RES_OTA_FAILED;
            break;
        }
        crc = esp_rom_crc32_le(crc, (uint8_t *)buf, received);
        if (image_header_was_checked == false) {
            if (ota_vertify(buf, received, req->content_len) != ESP_OK) {
                ESP_LOGE(TAG, "invalid image, aborted OTA");
                res = RES_OTA_FAILED;
                break;
            }
            ESP_LOGI(TAG, "wait camera stop before ota ...");
            // g_http.isLiveView = false;
            // camera_wait(CAMERA_STOP_BIT, 10000);
            // camera_close();
            ESP_LOGI(TAG, "ota_start ...");
            if (ota_start(&handle, req->content_len) != ESP_OK) {
                ESP_LOGE(TAG, "ota_start FAILED");
                res = RES_OTA_FAILED;
                break;
            }
            image_header_was_checked = true;
            ESP_LOGI(TAG, "ota_run ...");
        }
        if (ota_run(&handle, buf, received) != ESP_OK) {
            ESP_LOGE(TAG, "ota_run FAILED");
            res = RES_OTA_FAILED;
            break;
        }
        /* Keep track of remaining size of
         * the file left to be uploaded */
        remaining -= received;
    }
    if (ota_stop(&handle) != ESP_OK) {
        res = RES_OTA_FAILED;
        ESP_LOGE(TAG, "ota_stop FAILED");
    }
    /* Close file upon upload completion */
    if (res == RES_OK) {
        http_send_json_response(req, RES_OK);
        free(buf);
        cfg_set_firmware_crc32(crc);
        ESP_LOGI(TAG, "OTA successfully, CRC32: 0x%08lx", crc);
        vTaskDelay(500 / portTICK_PERIOD_MS);
        system_restart();
    } else {
        http_send_json_response(req, res);
        ESP_LOGI(TAG, "OTA failed");
    }
    free(buf);
    return ESP_OK;
}

static esp_err_t upload_to_path(httpd_req_t *req, const char *path)
{
    ESP_LOGI(TAG, "upload_to_path %s", path);
    int remaining = req->content_len;
    int received = 0;
    int timeout = 0;
    char *buf = malloc(HTTP_BUFF_MAX_SIZE);
    if (!buf) {
        http_send_json_response(req, RES_FAIL);
        return ESP_FAIL;
    }
    
    // 使用 filesystem_write 需要先收集完整数据
    char *file_data = malloc(req->content_len);
    if (!file_data) {
        free(buf);
        http_send_json_response(req, RES_FAIL);
        return ESP_FAIL;
    }
    
    char *ptr = file_data;
    while (remaining > 0) {
        if ((received = httpd_req_recv(req, buf, MIN(remaining, HTTP_BUFF_MAX_SIZE))) <= 0) {
            if (received == HTTPD_SOCK_ERR_TIMEOUT && timeout++ < 3) {
                continue;
            }
            free(buf);
            free(file_data);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to receive file");
            return ESP_FAIL;
        }
        memcpy(ptr, buf, received);
        ptr += received;
        remaining -= received;
    }
    
    int result = filesystem_write(path, file_data, req->content_len);
    free(buf);
    free(file_data);
    
    if (result != 0) {
        ESP_LOGE(TAG, "Failed to write file: %s", path);
        http_send_json_response(req, RES_FAIL);
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "File uploaded successfully: %s (%d bytes)", path, req->content_len);
    return ESP_OK;
}

static esp_err_t parse_req_filename(httpd_req_t *req, char *filename)
{
    size_t header_len = httpd_req_get_hdr_value_len(req, "X-File-Name");
    if (header_len > 0) {
        if (httpd_req_get_hdr_value_str(req, "X-File-Name", filename, header_len + 1) == ESP_OK) {          
            return ESP_OK;
        }
    }
    return ESP_FAIL;
}

static esp_err_t set_upload_mqtt_ca_handle(httpd_req_t *req)
{
    ESP_LOGI(TAG, "%s", req->uri);
    clear_timeout();

    mqttAttr_t mqtt;
    char filename[128] = {0};
    if (parse_req_filename(req, filename) == ESP_FAIL) {
        http_send_json_response(req, RES_FAIL);
        return ESP_FAIL;
    }
    if (upload_to_path(req, MQTT_CA_PATH) == ESP_OK) {
        cfg_get_mqtt_attr(&mqtt);
        strncpy(mqtt.caName, filename, sizeof(mqtt.caName) - 1);
        mqtt.caName[sizeof(mqtt.caName) - 1] = '\0';
        cfg_set_mqtt_attr(&mqtt);
        ESP_LOGI(TAG, "CA filename saved: %s", mqtt.caName);
        http_send_json_response(req, RES_OK);
    } else {
        ESP_LOGE(TAG, "Failed to upload CA file or parse filename");
        http_send_json_response(req, RES_FAIL);
    }
    return ESP_OK;
}

static esp_err_t set_upload_mqtt_cert_handle(httpd_req_t *req)
{
    ESP_LOGI(TAG, "%s", req->uri);
    clear_timeout();

    mqttAttr_t mqtt;
    char filename[128] = {0};
    if (parse_req_filename(req, filename) == ESP_FAIL) {
        http_send_json_response(req, RES_FAIL);
        return ESP_FAIL;
    }
    if (upload_to_path(req, MQTT_CERT_PATH) == ESP_OK) {
        cfg_get_mqtt_attr(&mqtt);
        strncpy(mqtt.certName, filename, sizeof(mqtt.certName) - 1);
        mqtt.certName[sizeof(mqtt.certName) - 1] = '\0';
        ESP_LOGI(TAG, "Cert filename saved: %s", mqtt.certName);
        cfg_set_mqtt_attr(&mqtt);
        http_send_json_response(req, RES_OK);
    } else {
        ESP_LOGE(TAG, "Failed to upload cert file or parse filename");
        http_send_json_response(req, RES_FAIL);
    }
    return ESP_OK;
}

static esp_err_t set_upload_mqtt_key_handle(httpd_req_t *req)
{
    ESP_LOGI(TAG, "%s", req->uri);
    clear_timeout();

    mqttAttr_t mqtt;
    char filename[128] = {0};
    if (parse_req_filename(req, filename) == ESP_FAIL) {
        http_send_json_response(req, RES_FAIL);
        return ESP_FAIL;
    }
    if (upload_to_path(req, MQTT_KEY_PATH) == ESP_OK) {
        cfg_get_mqtt_attr(&mqtt);
        strncpy(mqtt.keyName, filename, sizeof(mqtt.keyName) - 1);
        mqtt.keyName[sizeof(mqtt.keyName) - 1] = '\0';
        ESP_LOGI(TAG, "Key filename saved: %s", mqtt.keyName);
        cfg_set_mqtt_attr(&mqtt);
        http_send_json_response(req, RES_OK);
    } else {
        ESP_LOGE(TAG, "Failed to upload key file or parse filename");
        http_send_json_response(req, RES_FAIL);
    }
    return ESP_OK;
}

static esp_err_t delete_cert_file(const char *cert_path)
{
    // delete certificate file
    if (filesystem_is_exist(cert_path)) {
        unlink(cert_path);
    }
    
    return ESP_OK;
}

static esp_err_t delete_mqtt_ca_handle(httpd_req_t *req)
{
    ESP_LOGI(TAG, "%s", req->uri);
    clear_timeout();

    mqttAttr_t mqtt;
    delete_cert_file(MQTT_CA_PATH);

    cfg_get_mqtt_attr(&mqtt);
    strcpy(mqtt.caName, "");
    cfg_set_mqtt_attr(&mqtt);

    http_send_json_response(req, RES_OK);

    return ESP_OK;
}

static esp_err_t delete_mqtt_cert_handle(httpd_req_t *req)
{
    ESP_LOGI(TAG, "%s", req->uri);
    clear_timeout();

    mqttAttr_t mqtt;
    delete_cert_file(MQTT_CERT_PATH);

    cfg_get_mqtt_attr(&mqtt);
    strcpy(mqtt.certName, "");
    cfg_set_mqtt_attr(&mqtt);

    http_send_json_response(req, RES_OK);
    
return ESP_OK;
}

static esp_err_t delete_mqtt_key_handle(httpd_req_t *req)
{
    ESP_LOGI(TAG, "%s", req->uri);
    clear_timeout();

    mqttAttr_t mqtt;
    delete_cert_file(MQTT_KEY_PATH);

    cfg_get_mqtt_attr(&mqtt);
    strcpy(mqtt.keyName, "");
    cfg_set_mqtt_attr(&mqtt);

    http_send_json_response(req, RES_OK);
    
return ESP_OK;
}

/**
 * MJPEG stream handler
 * @param req HTTP request handle
 * @return ESP_OK on success
 */
static esp_err_t get_jpeg_stream_handle(httpd_req_t *req)
{
    /* Register commands */
    ESP_LOGI(TAG, "%s", req->uri);
    camera_fb_t *frame = NULL;
    struct timeval _timestamp;
    esp_err_t res = ESP_OK;
    size_t _jpg_buf_len = 0;
    uint8_t *_jpg_buf = NULL;
    char *part_buf[128];

    clear_timeout();
    res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
    if (res != ESP_OK) {
        return res;
    }
    g_http.isLiveView = true;
    res = camera_start();
    if (res != ESP_OK) {
        return res;
    }
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "X-Framerate", "60");

    while (g_http.isLiveView) {
        frame = camera_fb_get();
        if (frame) {
            _timestamp.tv_sec = frame->timestamp.tv_sec;
            _timestamp.tv_usec = frame->timestamp.tv_usec;
            if (frame->format == PIXFORMAT_JPEG) {
                _jpg_buf = frame->buf;
                _jpg_buf_len = frame->len;
            } else if (!frame2jpg(frame, 60, &_jpg_buf, &_jpg_buf_len)) {
                ESP_LOGE(TAG, "JPEG compression failed");
                res = ESP_FAIL;
            }
        } else {
            res = ESP_FAIL;
        }

        if (res == ESP_OK) {
            res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
            if (res == ESP_OK) {
                size_t hlen = snprintf((char *)part_buf, 128, _STREAM_PART, _jpg_buf_len, _timestamp.tv_sec, _timestamp.tv_usec);
                res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
            }
            if (res == ESP_OK) {
                res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
            }

            if (frame->format != PIXFORMAT_JPEG) {
                free(_jpg_buf);
                _jpg_buf = NULL;
            }
        }
        if (frame) {
            camera_fb_return(frame);
            if (res != ESP_OK) {
                // ESP_LOGE(TAG, "Break stream handler");
                break;
            }
        }
    }
    // camera_stop();
    return ESP_OK;
}

static const httpd_uri_t g_webHandlers[] = {
    {
        .uri = "/",
        .method = HTTP_GET,
        .handler = get_root_handler,
    },
    {
        .uri = "/favicon.ico",
        .method = HTTP_GET,
        .handler = get_favicon_handler,
    },
    {
        .uri = "/assets/index.js",
        .method = HTTP_GET,
        .handler = get_js_handler,
    },
    {
        .uri = "/assets/index.css",
        .method = HTTP_GET,
        .handler = get_css_handler,
    },
    {
        .uri = "/api/v1/image/setCamParam",
        .method = HTTP_POST,
        .handler = set_cam_param_handle,
    },
    {
        .uri = "/api/v1/image/getCamParam",
        .method = HTTP_GET,
        .handler = get_cam_param_handle,
    },
    {
        .uri = "/api/v1/image/setLightParam",
        .method = HTTP_POST,
        .handler = set_light_param_handle,
    },
    {
        .uri = "/api/v1/image/getLightParam",
        .method = HTTP_GET,
        .handler = get_light_param_handle,
    },
    {
        .uri = "/api/v1/capture/setCapParam",
        .method = HTTP_POST,
        .handler = set_cap_param_handle,
    },
    {
        .uri = "/api/v1/capture/getCapParam",
        .method = HTTP_GET,
        .handler = get_cap_param_handle,
    },
    {
        .uri = "/api/v1/capture/setUploadParam",
        .method = HTTP_POST,
        .handler = set_upload_param_handle,
    },
    {
        .uri = "/api/v1/capture/getUploadParam",
        .method = HTTP_GET,
        .handler = get_upload_param_handle,
    },
    {
        .uri = "/api/v1/network/getWifiParam",
        .method = HTTP_GET,
        .handler = get_wifi_param_handle,
    },
    {
        .uri = "/api/v1/network/setWifiParam",
        .method = HTTP_POST,
        .handler = set_wifi_param_handle,
    },
    {
        .uri = "/api/v1/network/getWifiList",
        .method = HTTP_GET,
        .handler = get_wifi_list_handle,
    },
    {
        .uri = "/api/v1/network/getMqttParam",
        .method = HTTP_GET,
        .handler = get_mqtt_param_handle,
    },
    {
        .uri = "/api/v1/network/setMqttParam",
        .method = HTTP_POST,
        .handler = set_mqtt_param_handle,
    },
    {
        .uri = "/api/v1/network/getPlatformParam",
        .method = HTTP_GET,
        .handler = get_platform_param_handle,
    },
    {
        .uri = "/api/v1/network/setPlatformParam",
        .method = HTTP_POST,
        .handler = set_platform_param_handle,
    },
    {
        .uri = "/api/v1/network/getIoTParam",
        .method = HTTP_GET,
        .handler = get_iot_param_handle,
    },
    {
        .uri = "/api/v1/network/setIoTParam",
        .method = HTTP_POST,
        .handler = set_iot_param_handle,
    },
    {
        .uri = "/api/v1/network/getCellularParam",
        .method = HTTP_GET,
        .handler = get_cellular_param_handle,
    },
    {
        .uri = "/api/v1/network/setCellularParam",
        .method = HTTP_POST,
        .handler = set_cellular_param_handle,
    },
    {
        .uri = "/api/v1/network/sendCellularCommand",
        .method = HTTP_POST,
        .handler = send_cellular_command_handle,
    },
    {
        .uri = "/api/v1/network/getCellularStatus",
        .method = HTTP_GET,
        .handler = get_cellular_status_handle,
    },
    {
        .uri = "/api/v1/system/getDevInfo",
        .method = HTTP_GET,
        .handler = get_dev_info_handle,
    },
    {
        .uri = "/api/v1/system/setDevInfo",
        .method = HTTP_POST,
        .handler = set_dev_info_handle,
    },
    {
        .uri = "/api/v1/system/getDevTime",
        .method = HTTP_GET,
        .handler = get_dev_time_handle,
    },
    {
        .uri = "/api/v1/system/getDevBattery",
        .method = HTTP_GET,
        .handler = get_dev_battery_handle,
    },
    {
        .uri = "/api/v1/system/setDevTime",
        .method = HTTP_POST,
        .handler = set_dev_time_handle,
    },
    {
        .uri = "/api/v1/system/setDevSleep",
        .method = HTTP_POST,
        .handler = set_dev_sleep_handle,
    },
    {
        .uri = "/api/v1/system/setDevUpgrade",
        .method = HTTP_POST,
        .handler = set_dev_upgrade_handle,
    },
    // 证书上传（三个静态路径，直接写入 LittleFS）
    {
        .uri = "/api/v1/network/uploadMqttCa",
        .method = HTTP_POST,
        .handler = set_upload_mqtt_ca_handle,
    },
    {
        .uri = "/api/v1/network/uploadMqttCert",
        .method = HTTP_POST,
        .handler = set_upload_mqtt_cert_handle,
    },
    {
        .uri = "/api/v1/network/uploadMqttKey",
        .method = HTTP_POST,
        .handler = set_upload_mqtt_key_handle,
    },
    {
        .uri = "/api/v1/network/deleteMqttCa",
        .method = HTTP_POST,
        .handler = delete_mqtt_ca_handle,
    },
    {
        .uri = "/api/v1/network/deleteMqttCert",
        .method = HTTP_POST,
        .handler = delete_mqtt_cert_handle,
    },
    {
        .uri = "/api/v1/network/deleteMqttKey",
        .method = HTTP_POST,
        .handler = delete_mqtt_key_handle,
    },
};

static const httpd_uri_t g_streamHandlers[] = {
    {
        .uri = "/api/v1/liveview/getJpegStream",
        .method = HTTP_GET,
        .handler = get_jpeg_stream_handle,
    },
};

/**
 * Start web server
 * @param port Port to listen on
 * @return ESP_OK on success
 */
static esp_err_t web_server_start(uint16_t port)
{
    int i;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_open_sockets = 6;
    config.max_uri_handlers = sizeof(g_webHandlers) / sizeof(httpd_uri_t);
    config.lru_purge_enable = true;
    config.keep_alive_enable = true;
    config.server_port = port;
    config.stack_size = 16384;
    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&g_webServer, &config) == ESP_OK) {
        for (i = 0; i < config.max_uri_handlers; i++) {
            httpd_register_uri_handler(g_webServer, &g_webHandlers[i]);
        }
        httpd_register_err_handler(g_webServer, HTTPD_404_NOT_FOUND, error_404_handler);
    } else {
        ESP_LOGE(TAG, "Failed to start web server");
        return ESP_FAIL;
    }
    return ESP_OK;
}

/**
 * Start stream server
 * @param port Port to listen on
 * @return ESP_OK on success
 */
static esp_err_t stream_server_start(uint16_t port)
{
    int i;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_open_sockets = 4;
    config.max_uri_handlers = sizeof(g_streamHandlers) / sizeof(httpd_uri_t);
    config.server_port = port;
    config.lru_purge_enable = true;
    config.keep_alive_enable = true;
    config.ctrl_port += 1;
    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&g_streamServer, &config) == ESP_OK) {
        for (i = 0; i < config.max_uri_handlers; i++) {
            httpd_register_uri_handler(g_streamServer, &g_streamHandlers[i]);
        }
        httpd_register_err_handler(g_streamServer, HTTPD_404_NOT_FOUND, error_404_handler);
    } else {
        ESP_LOGE(TAG, "Failed to start stream server");
        return ESP_FAIL;
    }
    return ESP_OK;
}

/**
 * Timeout timer callback
 * @param arg Pointer to mdHttp_t state
 */
static void timer_cb(void *arg)
{
    mdHttp_t *http = (mdHttp_t *)arg;
    if (http->webTimeoutSeconds++ >= WEB_TIMEOUT_SECONDS) {
        ESP_LOGI(TAG, "web has nothing to do over %ds, will go to sleep", WEB_TIMEOUT_SECONDS);
        sleep_set_event_bits(SLEEP_NO_OPERATION_TIMEOUT_BIT);
    }
}

/**
 * Start HTTP timeout timer
 */
static void http_timer_start()
{
    const esp_timer_create_args_t timer_args = {
        timer_cb,
        &g_http,
        ESP_TIMER_TASK,
        "http_timer",
        true,
    };
    esp_timer_create(&timer_args, &g_http.timer);
    esp_timer_start_periodic(g_http.timer, 1000 * 1000); //1s
}

/**
 * Initialize and start HTTP servers
 * @return ESP_OK on success
 */
esp_err_t http_open(void)
{
    memset(&g_http, 0, sizeof(g_http));
    web_server_start(80);
    stream_server_start(8080);
    http_timer_start();
    return ESP_OK;
}

/**
 * Stop HTTP servers
 * @return ESP_OK on success
 */
esp_err_t http_close(void)
{
    if (g_webServer) {
        return httpd_stop(g_webServer);
    }
    if (g_streamServer) {
        return httpd_stop(g_streamServer);
    }
    return ESP_FAIL;
}

/**
 * Check if any HTTP clients are connected
 * @return true if clients connected, false otherwise
 */
bool http_hasClient(void)
{
    return g_http.hasClient;
}

/**
 * Clear web timeout counter
 */
void http_clear_timeout(void)
{
    g_http.webTimeoutSeconds = 0;
}
