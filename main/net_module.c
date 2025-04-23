#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "cat1.h"
#include "wifi.h"
#include "morse.h"
#include "misc.h"
#include "utils.h"
#include "net_module.h"
#include "driver/rtc_io.h"
#include "soc/rtc.h"
#include "esp_sleep.h"
#include "sleep.h"
#include "wifi_iperf.h"

#define TAG "-->NET_MODULE"  // Logging tag for network module

// Network module state preserved in RTC memory
static RTC_DATA_ATTR net_module_t g_NetModule = {0}; 

/**
 * Check if using mmWave WiFi (HaLow)
 * @return true if using HaLow, false otherwise
 */
bool netModule_is_mmwifi(void)
{
    return g_NetModule.mode == NET_HALOW;
}

/**
 * Check if using CAT1 cellular network
 * @return true if using CAT1, false otherwise
 */
bool netModule_is_cat1(void)
{
    return g_NetModule.mode == NET_CAT1;
}

/**
 * Set network mode in device info and save to config
 * @param dev Device info structure
 * @param mode Network mode to set
 */
static void set_netmod(deviceInfo_t *dev, NET_MODE_E mode) 
{
    const char* mode_str = NULL;

    switch (mode) {
        case NET_CAT1:
            mode_str = "cat1";
            break;
        case NET_HALOW:
            mode_str = "halow";
            break;
        case NET_WIFI:
            mode_str = "wifi";
            break;
        default:
            mode_str = "";
            break;
    }

    if (strcmp(dev->netmod, mode_str) != 0) {
        strncpy(dev->netmod, mode_str, sizeof(dev->netmod) - 1);
        dev->netmod[sizeof(dev->netmod) - 1] = '\0';  // Ensure null-termination
        cfg_set_device_info(dev);
    }
}

/**
 * Get network mode enum from string
 * @param netmod Network mode string ("cat1", "halow", "wifi")
 * @return Corresponding NET_MODE_E value
 */
static NET_MODE_E get_mode_from_netmod(const char *netmod) {
    if (strcmp(netmod, "cat1") == 0) {
        return NET_CAT1;
    } else if (strcmp(netmod, "halow") == 0) {
        return NET_HALOW;
    } else if (strcmp(netmod, "wifi") == 0) {
        return NET_WIFI;
    } else {
        return NET_NONE;
    }
}

/**
 * Check available network interfaces and select best one
 * Sets mode and triggers restart after short delay
 */
void netModule_check(void)
{
    g_NetModule.mode = NET_NONE;

    uint8_t mac_hex[6];
    deviceInfo_t device;
    cfg_get_device_info(&device);
    if (strlen(device.mac) && is_valid_mac(device.mac)) {
        mac_str2hex(device.mac, mac_hex);
    } else {
        esp_read_mac(mac_hex, ESP_MAC_WIFI_STA);
        wifi_set_mac(mac_hex);
        mac_hex2str(mac_hex, device.mac);
        ESP_LOGW(TAG, "invalid mac, use default %s", device.mac);
    }
    if(ESP_OK == mm_wifi_init(mm_netif_create_default_wifi_sta(), mac_hex, device.countryCode)){
        mm_wifi_deinit();
        g_NetModule.mode = NET_HALOW;
    }else if(ESP_OK == cat1_connect_check()){
        g_NetModule.mode = NET_CAT1;
    }else{
        g_NetModule.mode = NET_WIFI;
    }
    ESP_LOGI(TAG, "netModule_check mode :%d .enter sleep", g_NetModule.mode);
    set_netmod(&device, g_NetModule.mode);
    g_NetModule.check_flag = 1;

    esp_sleep_enable_timer_wakeup(100000ULL);
    esp_deep_sleep_start();
}

/**
 * Check if network check flag is set
 * @return 1 if flag is set, 0 otherwise
 */
int netModule_is_check_flag(void)
{
    return g_NetModule.check_flag;
}

/**
 * Clear network check flag
 * @return Previous flag value
 */
int netModule_clear_check_flag(void)
{
    return g_NetModule.check_flag = 0;
}
/**
 * Initialize network module based on configured mode
 * @param mode System operation mode
 */
void netModule_init(modeSel_e mode)
{
    deviceInfo_t device;
    cfg_get_device_info(&device);
    g_NetModule.mode = get_mode_from_netmod(device.netmod);
    if(g_NetModule.mode == NET_NONE){
        ESP_LOGE(TAG, "Invalid mode.");
        netModule_check();
        return;
    }
    if(g_NetModule.mode == NET_CAT1){
        ESP_ERROR_CHECK(esp_netif_init());
        ESP_ERROR_CHECK(esp_event_loop_create_default());
        
        cat1_init(mode);
        cat1_open();
    }
    ESP_LOGI(TAG, "netModule_init mode :%d .", g_NetModule.mode);
}

/**
 * Open network connection based on current mode
 * @param mode System operation mode
 */
void netModule_open(modeSel_e mode)
{
    if(g_NetModule.mode == NET_NONE){
        ESP_LOGE(TAG, "Invalid mode.");
        netModule_check();
        return;
    }

    if(g_NetModule.mode == NET_CAT1){
        if(mode == MODE_CONFIG){
            wifi_open(WIFI_MODE_AP);
        }
        cat1_wait_open();
    }else{
        if(mode == MODE_CONFIG){
            wifi_open(WIFI_MODE_APSTA);
        }else{
            wifi_open(WIFI_MODE_STA);
        }
    }
    add_ping_cmd();
    register_wifi_iperf();
}

/**
 * Deinitialize network module
 */
void netModule_deinit(void)
{
    if(g_NetModule.mode == NET_HALOW){
        mm_wifi_shutdown();
    }
}
