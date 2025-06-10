#include <stdio.h>
#include <string.h>
#include "esp_err.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "config.h"
#include "system.h"
#include "ota.h"
#include "s2j.h"
#include "utils.h"
#include "http_client.h"
#include "esp_crt_bundle.h"
#include "esp_rom_crc.h"

#define MAX_HTTP_RECV_BUFFER 4096

#define TAG "-->HTTP_CLIENT"

typedef struct user_data {
    void *data;
    uint32_t len;
    uint32_t remain;
} user_data_t;

static esp_err_t event_handle(esp_http_client_event_t *evt)
{
    user_data_t *user_data = (user_data_t *)evt->user_data;
    switch (evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGI(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            // ESP_LOGI(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            // ESP_LOGI(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            // ESP_LOGI(TAG, "HTTP_EVENT_ON_HEADER");
            // printf("%.*s", evt->data_len, (char *)evt->data);
            break;
        case HTTP_EVENT_ON_DATA:
            // ESP_LOGI(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            // if (!esp_http_client_is_chunked_response(evt->client))
            if (user_data) {
                if (user_data->remain >= evt->data_len) {
                    memcpy(user_data->data + user_data->len, evt->data, evt->data_len);
                    user_data->remain -= evt->data_len;
                    user_data->len += evt->data_len;
                    ESP_LOGI(TAG, "downloading, %lu bytes", user_data->len);
                }
                // printf("%.*s", evt->data_len, (char *)evt->data);
            }
            break;
        case HTTP_EVENT_ON_FINISH:
            // ESP_LOGI(TAG, "HTTP_EVENT_ON_FINISH");
            break;
        case HTTP_EVENT_DISCONNECTED:
            // ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
            break;
        default:
            break;
    }
    return ESP_OK;
}

static esp_err_t http_client_post(char *url, char *data, char *header)
{
    esp_http_client_config_t config = {
        .url = replace_space(url, '+'),
        .method = HTTP_METHOD_POST,
        // .event_handler = event_handle,
    };
    if (strncasecmp(url, "https", 5) == 0) {
        config.crt_bundle_attach = esp_crt_bundle_attach;
    }
    esp_http_client_handle_t client = esp_http_client_init(&config);

    esp_http_client_set_header(client, "Content-Type", header);
    esp_http_client_set_post_field(client, data, strlen(data));
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTP POST Status = %d, content_length = %lld",
                 esp_http_client_get_status_code(client),
                 esp_http_client_get_content_length(client));
    }
    esp_http_client_cleanup(client);
    return err;
}

static int http_client_get(char *url, char *data, uint32_t len)
{
    user_data_t user_data = {
        .data = data,
        .len = 0,
        .remain = len,
    };
    esp_http_client_config_t config = {
        .method = HTTP_METHOD_GET,
        .url = replace_space(url, '+'),
        .event_handler = event_handle,
        .user_data = &user_data,
        .timeout_ms = 20000,
        .buffer_size = 1024,
    };
    if (strncasecmp(url, "https", 5) == 0) {
        config.crt_bundle_attach = esp_crt_bundle_attach;
    }
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "http_client_init failed");
        return 0;
    }
    esp_http_client_perform(client);
    esp_http_client_cleanup(client);
    return user_data.len;
}

static esp_err_t get_ota_data(char *url, char *data, uint32_t *len)
{
    *len = http_client_get(url, data, OTA_BIN_MAX_SIZE);
    if (*len > 0) {
        return ESP_OK;
    }
    return ESP_FAIL;
}

static esp_err_t get_ota_package(char *url, OTApackage_t *package)
{
    char content[256] = {0};
    if (http_client_get(url, content, sizeof(content)) > 0) {
        cJSON *json_temp;
        /* deserialize data to Student structure object. */
        cJSON *json = cJSON_Parse(content);
        s2j_struct_get_basic_element(package, json, string, fwTitle);
        s2j_struct_get_basic_element(package, json, string, fwChecksum);
        s2j_struct_get_basic_element(package, json, string, cfTitle);
        s2j_struct_get_basic_element(package, json, string, cfChecksum);
        s2j_delete_json_obj(json);
        return ESP_OK;
    }
    return ESP_FAIL;
}

static esp_err_t post_ota_package(char *url, OTApackage_t *package)
{
    char *data = NULL;

    s2j_create_json_obj(json_obj);
    if (strlen(package->fwTitle)) {
        s2j_json_set_basic_element(json_obj, package, string, fwTitle);
    }
    if (strlen(package->fwChecksum)) {
        s2j_json_set_basic_element(json_obj, package, string, fwChecksum);
    }
    if (strlen(package->cfTitle)) {
        s2j_json_set_basic_element(json_obj, package, string, cfTitle);
    }
    if (strlen(package->cfChecksum)) {
        s2j_json_set_basic_element(json_obj, package, string, cfChecksum);
    }
    data = cJSON_PrintUnformatted(json_obj);
    if (http_client_post(url, data, "application/json") == ESP_OK) {
        cJSON_free(data);
        s2j_delete_json_obj(json_obj);
        return ESP_OK;
    }
    cJSON_free(data);
    s2j_delete_json_obj(json_obj);
    return ESP_FAIL;
}

static esp_err_t update_firmware(char *url, char *title, char *crc)
{
    char *ota_data = NULL;
    uint32_t ota_len = 0;
    uint32_t fwChecksum = 0, devChecksum = 0;

    // 1. Get cloud device firmware information
    if (strlen(title) == 0) {
        ESP_LOGI(TAG, "no firmware need to update");
        return ESP_FAIL;
    }
    devChecksum = cfg_get_firmware_crc32();
    fwChecksum = strtoul(crc, NULL, 16);
    // 2. Compare the cloud firmware information with the local firmware information. If they are inconsistent, download the firmware and update it
    if (fwChecksum != devChecksum) {
        ESP_LOGI(TAG, "fwChecksum = %lx != devChecksum = %lx, will try updating", fwChecksum, devChecksum);
        ota_data = (char *)malloc(OTA_BIN_MAX_SIZE);
        if (ota_data == NULL) {
            ESP_LOGE(TAG, "malloc ota_data failed");
            return ESP_FAIL;
        }
        if (get_ota_data(url, ota_data, &ota_len) == ESP_OK &&
            fwChecksum == esp_rom_crc32_le(0, (uint8_t *)ota_data, ota_len)) {
            ESP_LOGI(TAG, "ota_len = %ld", ota_len);
            if (ota_update(ota_data, ota_len) == ESP_OK) {
                cfg_set_firmware_crc32(fwChecksum);
                free(ota_data);
                return ESP_OK;
            }
        } else {
            ESP_LOGE(TAG, "get_ota_data[len=%lu] failed from url = %s", ota_len, url);
        }
        free(ota_data);
        return ESP_FAIL;
    }
    return ESP_FAIL;
}

static esp_err_t update_config(char *url, char *title, char *crc)
{
    char *ota_data = NULL;
    uint32_t ota_len = 0;
    uint32_t cfChecksum = 0, devChecksum = 0;

    // 1. Get cloud device firmware information
    if (strlen(title) == 0) {
        ESP_LOGI(TAG, "no config need to update");
        return ESP_FAIL;
    }
    devChecksum = cfg_get_config_crc32();
    cfChecksum = strtoul(crc, NULL, 16);
    // 2. Compare the cloud firmware information with the local firmware information. If they are inconsistent, download the firmware and update it
    if (cfChecksum != devChecksum) {
        ESP_LOGI(TAG, "cfChecksum = %lx != devChecksum = %lx, will try updating", cfChecksum, devChecksum);
        ota_data = (char *)malloc(OTA_CFG_MAX_SIZE);
        if (ota_data == NULL) {
            ESP_LOGE(TAG, "malloc ota_data failed");
            return ESP_FAIL;
        }
        if (get_ota_data(url, ota_data, &ota_len) == ESP_OK &&
            cfChecksum == esp_rom_crc32_le(0, (uint8_t *)ota_data, ota_len)) {
            ESP_LOGI(TAG, "ota_len = %ld", ota_len);
            if (cfg_import(ota_data, ota_len) == ESP_OK) {
                cfg_set_config_crc32(cfChecksum);
                free(ota_data);
                return ESP_OK;
            }
        } else {
            ESP_LOGE(TAG, "get_ota_data failed from url = %s", url);
        }
        free(ota_data);
        return ESP_FAIL;
    }
    return ESP_FAIL;
}

esp_err_t http_client_sync_server_time()
{
    mqttAttr_t mqtt;
    cfg_get_mqtt_attr(&mqtt);

    char url[256] = {0};
    char content[128] = {0};
    int ret = 0;

    sprintf(url, "http://%s:%ld/api/v1/serverTime", mqtt.host, mqtt.httpPort);
    ret = http_client_get(url, content, sizeof(content));
    if (ret <= 0) {
        sprintf(url, "https://%s:%ld/api/v1/serverTime", mqtt.host, mqtt.httpPort);
        ret = http_client_get(url, content, sizeof(content));
    }
    if (ret > 0) {
        s2j_create_struct_obj(time, timeAttr_t);
        /* deserialize data to Student structure object. */
        cJSON *json = cJSON_Parse(content);
        s2j_struct_get_basic_element(time, json, int, ts);
        system_set_time(time);
        s2j_delete_struct_obj(time);
        s2j_delete_json_obj(json);
    } else {
        ESP_LOGE(TAG, "http_client_get failed from url = %s", url);
        return ESP_FAIL;
    }
    return ESP_OK;
}

void http_client_check_update()
{
    mqttAttr_t mqtt;
    deviceInfo_t device;
    char url[512] = {0};
    OTApackage_t package, respone;
    esp_err_t fwRet = ESP_FAIL;
    esp_err_t cfRet = ESP_FAIL;
    bool isHttps = false;

    cfg_get_mqtt_attr(&mqtt);

    // 1. Get cloud device firmware information
    cfg_get_device_info(&device);
    sprintf(url, "http://%s:%ld/api/v1/%s/latestOtaPackage", mqtt.host, mqtt.httpPort, device.sn);
    if (get_ota_package(url, &package) != ESP_OK) {
        ESP_LOGE(TAG, "get_ota_package failed from url = %s", url);
        sprintf(url, "https://%s:%ld/api/v1/%s/latestOtaPackage", mqtt.host, mqtt.httpPort, device.sn);
        if (get_ota_package(url, &package) != ESP_OK) {
            ESP_LOGE(TAG, "get_ota_package failed from url = %s", url);
            return;
        } else {
            isHttps = true;
        }
    } else {
        isHttps = false;
    }
    ESP_LOGI(TAG, "fwTitle = %s", package.fwTitle);
    ESP_LOGI(TAG, "fwChecksum = %s", package.fwChecksum);
    ESP_LOGI(TAG, "cfTitle = %s", package.cfTitle);
    ESP_LOGI(TAG, "cfChecksum = %s", package.cfChecksum);
    memset(&respone, 0, sizeof(respone));

    // 2.1 Compare the cloud firmware information with the local firmware information. If they are inconsistent, download the firmware and update it
    if (isHttps) {
        sprintf(url, "https://%s:%ld/api/v1/%s/firmware?title=%s", mqtt.host, mqtt.httpPort, device.sn, package.fwTitle);
    } else {
        sprintf(url, "http://%s:%ld/api/v1/%s/firmware?title=%s", mqtt.host, mqtt.httpPort, device.sn, package.fwTitle);
    }
    fwRet = update_firmware(url, package.fwTitle, package.fwChecksum);
    if (fwRet == ESP_OK) {
        strcpy(respone.fwTitle, package.fwTitle);
        strcpy(respone.fwChecksum, package.fwChecksum);
    }
    // 2.2 Compare the cloud configuration information with the local configuration information. If they are inconsistent, download the configuration and update it
    if (isHttps) {
        sprintf(url, "https://%s:%ld/api/v1/%s/configure?title=%s", mqtt.host, mqtt.httpPort, device.sn, package.cfTitle);
    } else {
        sprintf(url, "http://%s:%ld/api/v1/%s/configure?title=%s", mqtt.host, mqtt.httpPort, device.sn, package.cfTitle);
    }
    cfRet = update_config(url, package.cfTitle, package.cfChecksum);
    if (cfRet == ESP_OK) {
        strcpy(respone.cfTitle, package.cfTitle);
        strcpy(respone.cfChecksum, package.cfChecksum);
    }
    if (fwRet == ESP_FAIL && cfRet == ESP_FAIL) {
        ESP_LOGI(TAG, "no firmware or configure need to update");
        return;
    }
    // 3. Reply to cloud firmware upgrade success
    if (isHttps) {
        sprintf(url, "https://%s:%ld/api/v1/%s/otaPackage", mqtt.host, mqtt.httpPort, device.sn);
    } else {
        sprintf(url, "http://%s:%ld/api/v1/%s/otaPackage", mqtt.host, mqtt.httpPort, device.sn);
    }
    if (post_ota_package(url, &respone) == ESP_OK) {
        ESP_LOGI(TAG, "post_ota_package success");
    } else {
        ESP_LOGE(TAG, "post_ota_package failed");
    }
}

int8_t http_client_send_req(http_t *http)
{
    int ret = 0;
    int retry_cnt = 0;
    esp_http_client_config_t config;

    memset(&config, 0, sizeof(config));
    config.url = http->url;
    if (strcmp(http->method, "GET") == 0) {
        config.method = HTTP_METHOD_GET;
    } else if (strcmp(http->method, "POST") == 0) {
        config.method = HTTP_METHOD_POST;
    } else if (strcmp(http->method, "PUT") == 0) {
        config.method = HTTP_METHOD_PUT;
    } else if (strcmp(http->method, "DELETE") == 0) {
        config.method = HTTP_METHOD_DELETE;
    } else {
        ESP_LOGE(TAG, "Unsupported HTTP method: %s", http->method);
        return -1;
    }
    config.timeout_ms = http->timeout * 1000;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    // config.skip_cert_common_name_check = true;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "Failed to initialise HTTP connection");
        return -1;
    }
    esp_http_client_set_header(client, "Content-Type", "application/json");
    if (http->header_cnt > 0) {
        for (int i = 0; i < http->header_cnt; i++) {
            if (http->headers[i].key != NULL && http->headers[i].value != NULL) {
                ret |= esp_http_client_set_header(client, http->headers[i].key, http->headers[i].value);
            }
        }
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set HTTP headers");
            goto FAIL;
        }
    }

    int write_len = 0;

    if (http->body != NULL) {
        write_len = strlen(http->body);
    }

    ret = esp_http_client_open(client, write_len);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(ret));
        goto FAIL;
    }

    if (write_len) {
        ret = esp_http_client_write(client, http->body, write_len);
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to write HTTP body");
            goto FAIL;
        }
    }

    ret = esp_http_client_fetch_headers(client);
    if (ret == ESP_FAIL) {
        ESP_LOGE(TAG, "Failed to fetch HTTP headers");
        goto FAIL;
    }

    char *local_response_buffer = malloc(MAX_HTTP_RECV_BUFFER + 1);
    int buf_loc = 0;
    do {
        ret = esp_http_client_read_response(client, local_response_buffer + buf_loc, MAX_HTTP_RECV_BUFFER);
        if (ret < 0) {
            ESP_LOGE(TAG, "Error: SSL data read error");
            if (retry_cnt++ < 3) {
                ESP_LOGE(TAG, "Retry %d", retry_cnt);
                sleep(2);
                continue;
            }
            free(local_response_buffer);
            goto FAIL;
        }
        retry_cnt = 0;
        buf_loc += ret;
        if (ret == MAX_HTTP_RECV_BUFFER) {
            local_response_buffer = realloc(local_response_buffer, buf_loc + MAX_HTTP_RECV_BUFFER + 1);
        }
    } while (ret == MAX_HTTP_RECV_BUFFER);

    if (buf_loc) {
        local_response_buffer[buf_loc] = '\0';
        *http->resp = local_response_buffer;
        // ESP_LOGI(TAG, "HTTP response: %s", local_response_buffer);
    } else {
        free(local_response_buffer);
    }

    esp_http_client_cleanup(client);
    return 0;
FAIL:
    esp_http_client_cleanup(client);
    return -1;
}

int8_t http_client_download_file(const char *url, const char *filename, int timeout, int filesize, const char *md5,
                                 const char *crc32)
{
    int ret = 0;
    esp_http_client_config_t config;
    int retry_cnt = 0;
    char *buff = NULL;

    buff = malloc(MAX_HTTP_RECV_BUFFER + 1);
    if (buff == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for HTTP buffer");
        return -1;
    }

    memset(&config, 0, sizeof(config));
    config.url = url;
    config.method = HTTP_METHOD_GET;
    config.timeout_ms = timeout * 1000;
    config.crt_bundle_attach = esp_crt_bundle_attach;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "Failed to initialise HTTP connection");
        free(buff);
        return -1;
    }

    ret = esp_http_client_open(client, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(ret));
        goto FAIL;
    }
    ret = esp_http_client_fetch_headers(client);
    if (ret == ESP_FAIL) {
        ESP_LOGE(TAG, "Failed to fetch HTTP headers");
        goto FAIL;
    }

    FILE *f = fopen(filename, "wb");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file for writing");
        goto FAIL;
    }

    int data_read = 0;
    do {
        data_read = esp_http_client_read_response(client, buff, MAX_HTTP_RECV_BUFFER);
        if (data_read < 0) {
            ESP_LOGW(TAG, "Error: SSL data read error");
            if (retry_cnt++ < 3) {
                ESP_LOGW(TAG, "Retry %d", retry_cnt);
                sleep(2);
                continue;
            }
            goto FAIL;
        } else if (data_read > 0) {
            fwrite(buff, data_read, 1, f);
        }
        retry_cnt = 0;
    } while (data_read > 0);

    fclose(f);
    free(buff);
    buff = NULL;
    esp_http_client_cleanup(client);
    //file check
    //todo crc32
    if (filesize > 0 || md5 != NULL) {
        char *content = filesystem_read(filename);
        if (content == NULL) {
            ESP_LOGE(TAG, "read file failed");
            return -1;
        }
        if (filesize > 0 && filesize != strlen(content)) {
            ESP_LOGE(TAG, "file size check failed %d != %d", filesize, strlen(content));
            return -1;
        }
        if (md5 != NULL) {
            char *calc_md5 = NULL;
            if (md5_calc((unsigned char *)content, strlen(content), (unsigned char **)&calc_md5) != 0) {
                free(content);
                ESP_LOGE(TAG, "md5 calc failed");
                return -1;
            }
            if (strcmp(md5, calc_md5) != 0) {
                free(content);
                free(calc_md5);
                ESP_LOGE(TAG, "md5 check failed");
                return -1;
            }
            free(calc_md5);
        }
        free(content);
    }
    return 0;
FAIL:
    esp_http_client_cleanup(client);
    return ret;
}

int8_t http_client_upload_file(const char *url, const char *filename, int timeout)
{
    int ret = 0;
    esp_http_client_config_t config;
    char *buff = NULL;

    buff = malloc(4096);
    if (buff == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for HTTP buffer");
        return -1;
    }

    memset(&config, 0, sizeof(config));
    config.url = url;
    config.method = HTTP_METHOD_POST;
    config.timeout_ms = timeout * 1000;
    config.crt_bundle_attach = esp_crt_bundle_attach;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "Failed to initialise HTTP connection");
        return -1;
    }

    ret = esp_http_client_open(client, -1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(ret));
        goto FAIL;
    }
    ret = esp_http_client_fetch_headers(client);
    if (ret == ESP_FAIL) {
        ESP_LOGE(TAG, "Failed to fetch HTTP headers");
        goto FAIL;
    }

    FILE *f = fopen(filename, "rb");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file for reading");
        goto FAIL;
    }

    int data_read = 0;
    do {
        data_read = fread(buff, 1, 4096, f);
        if (data_read < 0) {
            ESP_LOGE(TAG, "Error: SSL data read error");
            goto FAIL;
        } else if (data_read > 0) {
            ret = esp_http_client_write(client, buff, data_read);
            if (ret < 0) {
                ESP_LOGE(TAG, "Error: SSL data write error");
                goto FAIL;
            }
        }
    } while (data_read > 0);

    fclose(f);
    esp_http_client_cleanup(client);
    return 0;
FAIL:
    esp_http_client_cleanup(client);
    return ret;
}
