#ifndef __HTTP_H__
#define __HTTP_H__

#ifdef __cplusplus
extern "C" {
#endif

/**
 * HTTP server result codes
 */
typedef enum httpResult {
    RES_FAIL = -1,              ///< Operation failed
    RES_OK = 1000,              ///< Operation succeeded
    RES_WIFI_CONNECTED = 1001,  ///< WiFi connected event
    RES_WIFI_DISCONNECTED = 1002, ///< WiFi disconnected event
    RES_OTA_FAILED = 1003,      ///< OTA update failed
} httpResult_e;

/**
 * Initialize and start HTTP server
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t http_open(void);

/**
 * Stop and deinitialize HTTP server
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t http_close(void);

/**
 * Check if any HTTP clients are connected
 * @return true if clients connected, false otherwise
 */
bool http_hasClient(void);

#ifdef __cplusplus
}
#endif

#endif /* __HTTP_H__ */
