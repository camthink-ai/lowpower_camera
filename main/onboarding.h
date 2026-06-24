/**
 * @file onboarding.h
 * @brief HaLowLink device onboarding - receive cloud configuration via default MQTT topic
 *
 * Handles the CamThinkAI camera onboarding process onto the HaLowLink network.
 * Default MQTT broker: provisioncamthink.qa.halow.link:1883
 * Default topic:      camera/{cameramac}/default
 *
 * Flow:
 *  1. Device connects to MQTT (default onboarding broker if no user config)
 *  2. On connect, always subscribe to camera/{mac}/default
 *  3. Cloud publishes JSON config to the topic
 *  4. Device parses config, updates non-MQTT settings directly
 *  5. If MQTT parameters changed, restart MQTT with new broker
 *  6. Next wake-up cycle: device uses updated config, still subscribes to default topic
 */

#ifndef __ONBOARDING_H__
#define __ONBOARDING_H__

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ONBOARDING_DEFAULT_HOST    "mqtt://provisioncamthink.qa.halow.link"
#define ONBOARDING_DEFAULT_PORT     1883
#define ONBOARDING_TOPIC_PREFIX     "camera/"
#define ONBOARDING_TOPIC_SUFFIX     "/default"
#define ONBOARDING_TOPIC_MAX_LEN    128

/**
 * @brief Check if the device has a user-defined MQTT broker configured.
 * @return true if user config exists, false if we should fall back to onboarding broker
 */
bool onboarding_has_user_config(void);

/**
 * @brief Get the default onboarding topic using device MAC address
 *        Format: camera/{lowercase_mac_no_colons}/default
 * @param topic Output buffer
 * @param len   Buffer length
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t onboarding_get_default_topic(char *topic, size_t len);

/**
 * @brief Get default onboarding MQTT host (used when no user config exists)
 * @return Pointer to host string
 */
const char* onboarding_get_default_host(void);

/**
 * @brief Get default onboarding MQTT port
 * @return Port number
 */
uint32_t onboarding_get_default_port(void);

/**
 * @brief Check if a topic matches the default onboarding topic
 * @param topic      Topic string to check
 * @param topic_len  Length of topic string
 * @return true if it is the default onboarding topic
 */
bool onboarding_is_default_topic(const char *topic, size_t topic_len);

/**
 * @brief Handle incoming cloud configuration JSON payload.
 *
 * Full field reference: docs/onboarding-json.md
 *
 * @param data              JSON payload from cloud
 * @param len               Payload length
 * @param mqtt_changed[out] Set to true if MQTT connection parameters changed
 * @return ESP_OK on success
 */
esp_err_t onboarding_handle_config(const char *data, size_t len, bool *mqtt_changed);

#ifdef __cplusplus
}
#endif

#endif /* __ONBOARDING_H__ */
