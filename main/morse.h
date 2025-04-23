#ifndef __MORSE_H__
#define __MORSE_H__

#include "esp_netif.h"
#include "mmwlan.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MM_WIFI_DEFAULT_CONFIG() \
    { \
        .ssid = "morse", \
                .password = "12345678", \
                            .country_code = "US", \
    }

#define MAX_SCAN_ITEM_COUNT   (128)

typedef struct scan_item {
    /** RSSI of the received frame. */
    int16_t rssi;
    /** Pointer to the BSSID field within the Probe Response frame. */
    uint8_t bssid[MMWLAN_MAC_ADDR_LEN];
    /** Pointer to the SSID within the SSID IE of the Probe Response frame. */
    char ssid[MMWLAN_SSID_MAXLEN];
    /** auth mode */
    uint8_t authmode; /*0: open, otherwise: SAE */
} scan_item_t;

typedef struct mm_scan_result {
    /** Number of items found. */
    uint16_t items_count;
    /** List of items found. */
    scan_item_t items[MAX_SCAN_ITEM_COUNT];
} mm_scan_result_t;

/**
 * @brief Creates default WIFI STA. In case of any init error this API aborts.
 *
 * @note The API creates esp_netif object with default WiFi station config,
 * attaches the netif to wifi and registers wifi handlers to the default event loop.
 * This API uses assert() to check for potential errors, so it could abort the program.
 * (Note that the default event loop needs to be created prior to calling this API)
 *
 * @return pointer to esp-netif instance
 */
esp_netif_t *mm_netif_create_default_wifi_sta(void);

/**
 * @brief Destroys default WIFI netif created with mm_netif_create_default_wifi_...() API.
 *
 * @param[in] esp_netif object to detach from WiFi and destroy
 *
 * @note This API unregisters wifi handlers and detaches the created object from the wifi.
 * (this function is a no-operation if esp_netif is NULL)
 */
void mm_netif_destroy_wifi_sta(esp_netif_t *esp_netif);

/**
 * @brief Initialize the WiFi driver.
 *
 * @param[in] esp_netif netif object
 * @param[in] mac_addr mac address
 * @param[in] country_code country code (default: "US") see mmwlan_regdb.def
 *
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t mm_wifi_init(esp_netif_t *esp_netif, uint8_t *mac_addr, const char *country_code);
/**
 * @brief Deinitialize the WiFi driver.
 *
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t mm_wifi_deinit(void);

/**
 * @brief shutdown WiFi
 * 
 */
void mm_wifi_shutdown();

/**
 * @brief Scan for WiFi networks
 *
 * @param[in] result scan result
 *
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t mm_wifi_scan(mm_scan_result_t *result);

/**
 * @brief Enable WiFi STA
 *
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t mm_wifi_connect();

/**
 * @brief Disable WiFi STA
 *
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t mm_wifi_disconnect();

/**
 * @brief Set WiFi configuration
 *
 * @param[in] ssid SSID
 * @param[in] password password
 *
 * @return ESP_OK on success, ESP_FAIL on error
*/
esp_err_t mm_wifi_set_config(const char *ssid, const char *password);

/**
 * @brief Get WiFi MAC
 *
 * @param[out] mac MAC
 *
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t mm_wifi_get_mac(uint8_t *mac);

/**
 * @brief Set WiFi MAC
 *
 * @param[in] mac MAC
 *
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t mm_wifi_set_mac(uint8_t *mac);

/**
 * @brief Set WiFi country code
 *
 * @param[in] country_code country code (default: "US") see mmwlan_regdb.def
 *
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t mm_wifi_set_country_code(const char *country_code);

/**
 * @brief Get WiFi country code
 *
 * @param[out] country_code country code (default: "US") see mmwlan_regdb.def
 *
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t mm_wifi_get_country_code(char *country_code);

#ifdef __cplusplus
}
#endif

#endif
