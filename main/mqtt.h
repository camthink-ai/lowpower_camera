#ifndef __MQTT_H__
#define __MQTT_H__

#include "system.h"
#include "mip.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize MQTT client with input/output queues
 * @param in Input queue for receiving messages
 * @param out Output queue for sending messages
 */
void mqtt_open(QueueHandle_t in, QueueHandle_t out);

/**
 * Shutdown MQTT client
 */
void mqtt_close();

/**
 * Start MQTT client connection
 */
void mqtt_start();

/**
 * Stop MQTT client connection
 */
void mqtt_stop();

/**
 * Restart MQTT client connection
 */
void mqtt_restart();

/**
 * Start MIP MQTT connection
 * @param mqtt MQTT configuration
 * @param cb Callback for subscription notifications
 * @param status_cb Callback for connection status changes
 * @return 0 on success, negative on error
 */
int8_t mqtt_mip_start(mqtt_t *mqtt, sub_notify_cb cb, connect_status_cb status_cb);

/**
 * Stop MIP MQTT connection
 * @return 0 on success, negative on error
 */
int8_t mqtt_mip_stop();

/**
 * Publish message to MQTT topic
 * @param topic Topic to publish to
 * @param msg Message to publish
 * @param timeout Timeout in milliseconds
 * @return 0 on success, negative on error
 */
int8_t mqtt_mip_publish(const char *topic, const char *msg, int timeout);

/**
 * Check if MIP MQTT is connected
 * @return 1 if connected, 0 otherwise
 */
int8_t mqtt_mip_is_connected();

#ifdef __cplusplus
}
#endif

#endif /* __MQTT_H__ */
