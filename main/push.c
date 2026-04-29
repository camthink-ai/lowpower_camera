/**
 * Push Dispatcher
 *
 * Routes image payloads to either MQTT or Webhook backend
 * based on the push:mode configuration.
 */
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "config.h"
#include "system.h"
#include "mqtt.h"
#include "webhook.h"
#include "push.h"
#include "storage.h"
#include "iot_mip.h"
#include "camera.h"

#define TAG "-->PUSH"

#define PUSH_READY_BIT BIT(0)
#define PUSH_EXIT_BIT BIT(1)

#define PUSH_READY_TIMEOUT_MS 90000

static EventGroupHandle_t g_eventGroup = NULL;
static QueueHandle_t g_in = NULL;
static QueueHandle_t g_out = NULL;
static TaskHandle_t g_taskHandle = NULL;
static bool g_isRunning = false;

static RTC_DATA_ATTR int g_send_total = 0;
static RTC_DATA_ATTR int g_send_success = 0;

static uint8_t get_push_mode(void)
{
    uint8_t mode = 0;
    cfg_get_u8(KEY_PUSH_MODE, &mode, 0);
    return mode;
}

static void push_task(void *arg)
{
    xEventGroupWaitBits(g_eventGroup, PUSH_READY_BIT | PUSH_EXIT_BIT, true, false, PUSH_READY_TIMEOUT_MS);
    ESP_LOGI(TAG, "push task started, mode=%s", get_push_mode() == 1 ? "Webhook" : "MQTT");
    while (true) {
        queueNode_t *node;
        if (xQueueReceive(g_in, &node, portMAX_DELAY)) {
            // Correct timestamp if not NTP-synced
            if (node->from == FROM_CAMERA && node->ntp_sync_flag == 0) {
                node->pts = node->pts + (system_get_time_delta() * 1000);
                node->ntp_sync_flag = system_get_ntp_sync_flag();
            }

            uploadAttr_t upload;
            cfg_get_upload_attr(&upload);
            modeSel_e currentMode = system_get_mode();

            if (upload.uploadMode == 0 || currentMode == MODE_UPLOAD) {
                ESP_LOGI(TAG, "PUSH ... (mode: %d, pushMode: %d)", currentMode, get_push_mode());

                esp_err_t res = ESP_FAIL;
                if (get_push_mode() == 1) {
                    // Webhook mode
                    char *json_str = push_build_json_payload(node);
                    if (json_str != NULL) {
                        res = (webhook_publish(json_str) == 0) ? ESP_OK : ESP_FAIL;
                        cJSON_free(json_str);
                    }
                } else {
                    // MQTT mode
                    res = mqtt_publish_node(node);
                }

                if (res != ESP_OK) {
                    if (g_out) {
                        ESP_LOGI(TAG, "PUSH FAIL, save to flash");
                        xQueueSend(g_out, &node, portMAX_DELAY);
                    } else {
                        ESP_LOGW(TAG, "PUSH FAIL, no storage queue");
                        node->free_handler(node, EVENT_FAIL);
                    }
                } else {
                    ESP_LOGI(TAG, "PUSH SUCCESS");
                    node->free_handler(node, EVENT_OK);
                    g_send_success += 1;
                }
            } else {
                ESP_LOGI(TAG, "PUSH SKIP (mode: %d, uploadMode: %d)", currentMode, upload.uploadMode);
                if (g_out) {
                    xQueueSend(g_out, &node, portMAX_DELAY);
                } else {
                    ESP_LOGW(TAG, "No storage queue for scheduled upload");
                    node->free_handler(node, EVENT_FAIL);
                }
            }
            g_send_total += 1;
        }
    }
    xEventGroupClearBits(g_eventGroup, PUSH_READY_BIT);
}

void push_open(QueueHandle_t in, QueueHandle_t out)
{
    g_in = in;
    g_out = out;

    mqtt_open();
    webhook_open();
    g_eventGroup = xEventGroupCreate();
    xEventGroupClearBits(g_eventGroup, PUSH_READY_BIT | PUSH_EXIT_BIT);
    xTaskCreatePinnedToCore(push_task, TAG, 8 * 1024, NULL, 4, &g_taskHandle, 1);
    g_isRunning = true;
}

void push_ready(void)
{
    xEventGroupSetBits(g_eventGroup, PUSH_READY_BIT);
}

void push_exit(void)
{
    xEventGroupSetBits(g_eventGroup, PUSH_EXIT_BIT);
}

void push_start(void)
{
    if (!g_isRunning) return;

    if (get_push_mode() == 1) {
        ESP_LOGI(TAG, "starting in Webhook mode");
        webhook_start();
        push_ready();
    } else {
        ESP_LOGI(TAG, "starting in MQTT mode");
        mqtt_start();
    }
}

void push_stop(void)
{
    if (!g_isRunning) return;

    if (get_push_mode() == 1) {
        ESP_LOGI(TAG, "stopping Webhook mode");
        webhook_stop();
    } else {
        ESP_LOGI(TAG, "stopping MQTT mode");
        mqtt_stop();
    }
    push_exit();
}

void push_close(void)
{
    if (g_taskHandle) {
        vTaskDelete(g_taskHandle);
        g_taskHandle = NULL;
    }
    mqtt_close();
    webhook_close();
    vEventGroupDelete(g_eventGroup);
    g_eventGroup = NULL;
    g_isRunning = false;
}

void push_restart(void)
{
    push_stop();
    push_start();
}
