#ifndef __IOT_MIP_H__
#define __IOT_MIP_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "mip.h"
#include "http_client.h"
#include "mqtt.h"

/* MIP (Modular IoT Platform) version */
#define MIP_VERSION "V1.0"
/* File paths for MIP configuration and data */
#define MIP_AUTOP_RESP_PATH     "/littlefs/autop_resp.json"      // Auto-provisioning response file
#define MIP_AUTOP_PROFILE_PATH  "/littlefs/autop_profile.json"   // Auto-provisioning profile file  
#define MIP_DM_RESP_PATH        "/littlefs/dm_resp.json"         // Device management response file
#define MIP_MQTT_CERT_PATH      "/littlefs/dm_cert.pem"          // MQTT client certificate
#define MIP_MQTT_KEY_PATH       "/littlefs/dm_key.pem"           // MQTT client private key
#define MIP_MQTT_CA_CERT_PATH   "/littlefs/dm_ca.pem"            // MQTT CA certificate

/* Callback type for async operations completion */
typedef void (*mip_after_cb)(void);

/* Core MIP functions */
int8_t iot_mip_init();      // Initialize MIP module
int8_t iot_mip_deinit();    // Deinitialize MIP module

/* Auto-provisioning functions */
int8_t iot_mip_autop_init();    // Initialize auto-provisioning
int8_t iot_mip_autop_deinit();  // Deinitialize auto-provisioning
int8_t iot_mip_autop_enable(bool enable);  // Enable/disable auto-provisioning
int8_t iot_mip_autop_start();   // Start auto-provisioning
int8_t iot_mip_autop_stop();    // Stop auto-provisioning
int8_t iot_mip_autop_async_start(mip_after_cb cb);  // Async start auto-provisioning
int8_t iot_mip_autop_async_stop(mip_after_cb cb);   // Async stop auto-provisioning
bool iot_mip_autop_is_enable(); // Check if auto-provisioning is enabled

/* Device management functions */
int8_t iot_mip_dm_init();    // Initialize device management
int8_t iot_mip_dm_deinit();  // Deinitialize device management
int8_t iot_mip_dm_enable(bool enable);  // Enable/disable device management
int8_t iot_mip_dm_start();   // Start device management
int8_t iot_mip_dm_stop();    // Stop device management
int8_t iot_mip_dm_async_start(mip_after_cb cb);  // Async start device management
int8_t iot_mip_dm_async_stop(mip_after_cb cb);   // Async stop device management
bool iot_mip_dm_is_enable(); // Check if device management is enabled

/* Device management status */
void iot_mip_dm_done(void);  // Signal device management completion
int8_t iot_mip_dm_pending(int32_t timeout_ms);  // Wait for device management completion

/* Device management commands */
int8_t iot_mip_dm_request_timestamp();  // Request time synchronization
int8_t iot_mip_dm_request_profile();    // Request device profile
int8_t iot_mip_dm_request_api_token();  // Request API access token
int8_t iot_mip_dm_request_sleep();      // Request sleep mode
int8_t iot_mip_dm_response_wake_up();   // Respond to wake up event
int8_t iot_mip_dm_uplink_picture(const char *msg);  // Upload picture with metadata

#ifdef __cplusplus
}
#endif

#endif //__IOT_MIP_H__
