#ifndef __CAT1_H__
#define __CAT1_H__

#include "config.h"

/**
 * CAT1 module status
 */
typedef enum cat1Status {
    CAT1_STATUS_STOPED = 0,    ///< Module stopped
    CAT1_STATUS_STARTING,      ///< UART communication established
    CAT1_STATUS_STARTED,       ///< Dial-up completed (network connection unknown)
} cat1Status_e;

/**
 * Cellular AT command structure
 */
typedef struct cellularCommand {
    char command[MAX_LEN_128];  ///< AT command string
} cellularCommand_t;

/**
 * Cellular command response structure
 */
typedef struct cellularCommandResp {
    int result;              ///< Command result code
    char message[1024];      ///< Response message
} cellularCommandResp_t;

/**
 * Cellular module status attributes
 */
typedef struct cellularStatusAttr {
    char networkStatus[MAX_LEN_64];   ///< Current network status
    char modemStatus[MAX_LEN_64];     ///< Modem operational status
    char model[MAX_LEN_64];           ///< Modem model
    char version[MAX_LEN_64];         ///< Firmware version
    char signalLevel[MAX_LEN_64];     ///< Signal strength level
    char registerStatus[MAX_LEN_64];  ///< Network registration status
    char imei[MAX_LEN_64];            ///< IMEI number
    char imsi[MAX_LEN_64];            ///< IMSI number
    char iccid[MAX_LEN_64];           ///< SIM ICCID
    char isp[MAX_LEN_64];             ///< Network provider
    char networkType[MAX_LEN_64];     ///< Connected network type
    char plmnId[MAX_LEN_64];          ///< PLMN ID
    char lac[MAX_LEN_64];             ///< Location Area Code
    char cellId[MAX_LEN_64];          ///< Cell ID
    char ipv4Address[MAX_LEN_64];     ///< IPv4 address
    char ipv4Gateway[MAX_LEN_64];     ///< IPv4 gateway
    char ipv4Dns[MAX_LEN_64];         ///< IPv4 DNS
    char ipv6Address[MAX_LEN_64];     ///< IPv6 address
    char ipv6Gateway[MAX_LEN_64];     ///< IPv6 gateway
    char ipv6Dns[MAX_LEN_64];         ///< IPv6 DNS
} cellularStatusAttr_t;

/**
 * Cellular signal quality metrics
 */
typedef struct cellularSignalQuality {
    int rssi;                ///< Received Signal Strength Indicator
    int ber;                 ///< Bit Error Rate
    int dbm;                 ///< Signal power in dBm
    int asu;                 ///< Arbitrary Strength Unit
    int level;               ///< Signal level (0-5)
    char quality[MAX_LEN_64]; ///< Signal quality description
} cellularSignalQuality_t;

/**
 * Initialize CAT1 module
 * @param mode Initialization mode
 */
void cat1_init(int mode);

/**
 * Open CAT1 module connection
 */
void cat1_open(void);

/**
 * Wait for CAT1 module to open
 */
void cat1_wait_open(void);

/**
 * Close CAT1 module connection
 */
void cat1_close(void);

/**
 * Restart CAT1 module
 * @return ESP_OK on success
 */
esp_err_t cat1_restart(void);

/**
 * Check if CAT1 is restarting
 * @return true if restarting, false otherwise
 */
bool cat1_is_restarting(void);

/**
 * Send AT command to CAT1 module
 * @param at AT command string
 * @param resp Response structure pointer
 * @return ESP_OK on success
 */
esp_err_t cat1_send_at(const char *at, cellularCommandResp_t *resp);

/**
 * Get cellular module status
 * @param status Status structure to populate
 * @return ESP_OK on success
 */
esp_err_t cat1_get_cellular_status(cellularStatusAttr_t *status);

/**
 * Check cellular connection status
 * @return ESP_OK if connected
 */
esp_err_t cat1_connect_check(void);

/**
 * Display cellular status information
 */
void cat1_show_status(void);

#endif // __CAT1_H__
