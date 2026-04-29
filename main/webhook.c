/**
 * Webhook HTTP Push Implementation
 *
 * Sends JSON payloads to a configured URL via HTTP POST.
 * Supports one custom header for authentication.
 */
#include <string.h>
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "config.h"
#include "webhook.h"
#include "storage.h"

#define TAG "-->WEBHOOK"
#define WEBHOOK_TIMEOUT_MS 20000

void webhook_open(void)
{
    ESP_LOGI(TAG, "webhook opened");
}

void webhook_close(void)
{
    ESP_LOGI(TAG, "webhook closed");
}

void webhook_start(void)
{
    ESP_LOGI(TAG, "webhook started");
    storage_upload_start();
}

void webhook_stop(void)
{
    ESP_LOGI(TAG, "webhook stopped");
    storage_upload_stop();
}

int8_t webhook_publish(const char *json_str)
{
    webhookAttr_t webhook;
    cfg_get_webhook_attr(&webhook);

    if (strlen(webhook.url) == 0) {
        ESP_LOGE(TAG, "webhook URL is empty");
        return -1;
    }

    esp_http_client_config_t config = {
        .url = webhook.url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = WEBHOOK_TIMEOUT_MS,
    };
    if (strncasecmp(webhook.url, "https", 5) == 0) {
        config.crt_bundle_attach = esp_crt_bundle_attach;
    }

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "Failed to init HTTP client");
        return -1;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");

    // Add custom header if configured (e.g. "Authorization: Bearer xxx")
    if (strlen(webhook.header) > 0) {
        // Parse "Key: Value" format
        char header_copy[256];
        strncpy(header_copy, webhook.header, sizeof(header_copy) - 1);
        header_copy[sizeof(header_copy) - 1] = '\0';
        char *colon = strchr(header_copy, ':');
        if (colon != NULL) {
            *colon = '\0';
            char *key = header_copy;
            char *value = colon + 1;
            // Skip leading whitespace in value
            while (*value == ' ') value++;
            esp_http_client_set_header(client, key, value);
        }
    }

    esp_http_client_set_post_field(client, json_str, strlen(json_str));

    esp_err_t err = esp_http_client_perform(client);
    int8_t result = -1;

    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "HTTP POST Status = %d, content_length = %lld",
                 status_code, esp_http_client_get_content_length(client));
        if (status_code >= 200 && status_code < 300) {
            result = 0;
        } else {
            ESP_LOGE(TAG, "webhook returned non-2xx status: %d", status_code);
        }
    } else {
        ESP_LOGE(TAG, "HTTP POST failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    return result;
}
