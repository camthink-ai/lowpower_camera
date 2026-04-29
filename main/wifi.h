#ifndef __WIFI_H__
#define __WIFI_H__

#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_mac.h"
#include "lwip/inet.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * WiFi network node information
 */
typedef struct wifiNode {
    char ssid[33];         ///< SSID of the network
    uint8_t bAuthenticate; ///< Authentication required (0/1)
    int8_t  rssi;          ///< Signal strength in dBm
} wifiNode_t;

/**
 * List of available WiFi networks
 */
typedef struct wifiList {
    uint32_t count;        ///< Number of networks
    wifiNode_t *nodes;     ///< Array of network nodes
} wifiList_t;

/**
 * Initialize WiFi with specified mode
 * @param mode WiFi mode (WIFI_MODE_APSTA, WIFI_MODE_AP, WIFI_MODE_STA)
 */
void wifi_open(wifi_mode_t mode);

/**
 * Deinitialize WiFi
 */
void wifi_close(void);

/**
 * Reconnect to a WiFi network
 * @param ssid Network SSID
 * @param password Network password
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t wifi_sta_reconnect(const char *ssid, const char *password);

/**
 * Scan for available WiFi networks
 * @param list Output parameter for network list
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t wifi_get_list(wifiList_t *list);

/**
 * Free WiFi network list resources
 * @param list List to free
 */
void wifi_put_list(wifiList_t *list);

/**
 * Check if station is connected to WiFi
 * @return true if connected, false otherwise
 */
bool wifi_sta_is_connected();

/**
 * Get device MAC address
 * @param mac_hex Output buffer for MAC (6 bytes)
 * 
 * MAC address assignments:
 * 1. Wi-Fi Station = base
 * 2. SoftAP = base+1 
 * 3. Bluetooth = base+2
 * 4. Ethernet = base+3
 */
void wifi_get_mac(uint8_t *mac_hex);

/**
 * Set device MAC address
 * @param mac_hex New MAC address (6 bytes)
 */
void wifi_set_mac(uint8_t *mac_hex);

/**
 * Get access point network interface
 * @return Pointer to AP netif
 */
esp_netif_t * wifi_get_Apnetif(void);

/**
 * Clear wifi timeout counter
 */
void wifi_clear_timeout(void);

#ifdef __cplusplus
}
#endif

#endif /* __WIFI_H__ */
