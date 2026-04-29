#ifndef __MQTT_H__
#define __MQTT_H__

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "system.h"
#include "mip.h"
#include "camera.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MQTT_CERT_PATH      "/littlefs/mqtts_cert.pem"
#define MQTT_KEY_PATH       "/littlefs/mqtts_key.pem"
#define MQTT_CA_PATH        "/littlefs/mqtts_ca.pem"

// Send buffer size for JSON payload construction (shared with push.c)
#define PUSH_SEND_BUFFER_SIZE  (1536000)

/**
 * Initialize MQTT module (allocates send buffer, no queue management)
 */
void mqtt_open(void);

/**
 * Shutdown MQTT module, free send buffer
 */
void mqtt_close(void);

/**
 * Start MQTT client connection
 */
void mqtt_start(void);

/**
 * Stop MQTT client connection
 */
void mqtt_stop(void);

/**
 * Restart MQTT client connection
 */
void mqtt_restart(void);

/**
 * Publish a queueNode_t as JSON via MQTT
 * @param node Queue node containing image data
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t mqtt_publish_node(queueNode_t *node);

/**
 * Build JSON payload string from a queueNode_t.
 * Uses internal send buffer for base64 encoding.
 * Caller must free the returned string with cJSON_free()
 * @param node Queue node containing image data
 * @return Allocated JSON string, or NULL on failure
 */
char *push_build_json_payload(queueNode_t *node);

// MIP interface (unchanged)
int8_t mqtt_mip_start(mqtt_t *mqtt, sub_notify_cb cb, connect_status_cb status_cb);
int8_t mqtt_mip_stop(void);
int8_t mqtt_mip_publish(const char *topic, const char *msg, int timeout);
int8_t mqtt_mip_is_connected(void);

#ifdef __cplusplus
}
#endif

#endif /* __MQTT_H__ */
