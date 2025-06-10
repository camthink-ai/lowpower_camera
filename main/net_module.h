#ifndef __NET_MODULE_H__
#define __NET_MODULE_H__

#include "config.h"
#include "system.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Network module types
 */
typedef enum {
    NET_NONE = 0,  ///< No network
    NET_CAT1,      ///< CAT1 cellular network
    NET_HALOW,     ///< HaLow (802.11ah) network
    NET_WIFI,      ///< Standard WiFi network
} NET_MODE_E;

/**
 * Network module state
 */
typedef struct {
    NET_MODE_E mode;       ///< Current network mode
    int       check_flag;  ///< Flag indicating if network check is needed
} net_module_t;

#ifdef __cplusplus
}
#endif

/**
 * Check if using mmWave WiFi
 * @return true if using mmWave WiFi, false otherwise
 */
bool netModule_is_mmwifi(void);

/**
 * Check if using CAT1 cellular network
 * @return true if using CAT1, false otherwise
 */
bool netModule_is_cat1(void);

/**
 * Perform network module check
 */
void netModule_check(void);

/**
 * Initialize network module
 * @param mode System operation mode
 */
void netModule_init(modeSel_e mode);

/**
 * Open network connection
 * @param mode System operation mode
 */
void netModule_open(modeSel_e mode);

/**
 * Check if network check flag is set
 * @return 1 if flag is set, 0 otherwise
 */
int netModule_is_check_flag(void);

/**
 * Clear network check flag
 * @return Previous flag value
 */
int netModule_clear_check_flag(void);

/**
 * Deinitialize network module
 */
void netModule_deinit(void);

#endif /* __NET_MODULE_H__ */
