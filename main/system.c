#include <sys/time.h>
#include <time.h>
#include "esp_err.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_heap_caps.h"
#include "esp_netif_sntp.h"
#include "config.h"
#include "system.h"
#include "storage.h"
#include "http_client.h"
#include "wifi.h"
#include "iot_mip.h"
#include "net_module.h"

#define TAG "-->SYSTEM"  // Logging tag for system module

static int time_delta = 0;    //When synchronizing time, the error time between the system and the actual time, in seconds.
static char ntp_sync_flag = 0;  //The flag indicating whether ntp is synchronized.
/**
 * Get the current system mode
 * @return modeSel_e
 */
modeSel_e system_get_mode(void)
{
    return main_mode;
}

/**
 * Synchronize system time with NTP server
 * @return ESP_OK on success, ESP_FAIL on timeout
 */
esp_err_t system_ntp_time(void)
{
    int retry = 0;
    const int retry_count = 7;  // Maximum retry attempts
    time_t sys_now;

    ESP_LOGI(TAG, "Initializing SNTP");
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(3,
                               ESP_SNTP_SERVER_LIST("pool.ntp.org", "ntp.aliyun.com", "time.windows.com"));
    esp_netif_sntp_init(&config);
    
    time(&sys_now);
    // Wait for time synchronization with retries
    while (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(2000)) != ESP_OK && ++retry < retry_count) {
        ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
    }

    // Print current system time
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    ESP_LOGI(TAG, "The current time is: %s", asctime(&timeinfo));

    esp_netif_sntp_deinit();
    if (retry == retry_count) {
        ESP_LOGE(TAG, "Failed to obtain time");
        return ESP_FAIL;
    }
    sys_now += retry;
    record_time_sync(now, sys_now);
    time_delta = (int)(now - sys_now);
    ntp_sync_flag = 1;
    return ESP_OK;
}

/**
 * System get time delta.
 */
int system_get_time_delta()
{
    return time_delta;
}

/**
 * System get ntp sync flag.
 */
int system_get_ntp_sync_flag()
{
    return ntp_sync_flag;
}

/**
 * Set system time and timezone
 * @param tAttr Time attributes containing timestamp and timezone
 * @return ESP_OK on success
 */
esp_err_t system_set_time(timeAttr_t *tAttr)
{
    time_t t_of_day = tAttr->ts;
    char buf[64];

    // Get timezone from config if not provided
    if (strlen(tAttr->tz) == 0) {
        cfg_get_timezone(tAttr->tz);
    }
    
    ESP_LOGI(TAG, "set timezone: %s", tAttr->tz);
    if (system_set_timezone(tAttr->tz) == ESP_OK) {
        cfg_set_timezone(tAttr->tz);  // Persist timezone to config
    }

    // Set system time
    struct timeval epoch = {t_of_day, 0};
    settimeofday(&epoch, NULL);
    
    // Log the new time
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", localtime(&t_of_day));
    ESP_LOGI(TAG, "Sync clock to: %s", buf);
    return ESP_OK;
}

/**
 * Get current system time and timezone
 * @param tAttr Output parameter for time attributes
 * @return ESP_OK
 */
esp_err_t system_get_time(timeAttr_t *tAttr)
{
    char *tz = getenv("TZ");
    memset(tAttr, 0, sizeof(timeAttr_t));
    tAttr->ts = time(NULL);  // Get current timestamp
    
    // Copy timezone if set
    if (tz) {
        strncpy(tAttr->tz, tz, sizeof(tAttr->tz));
    }
    return ESP_OK;
}

/**
 * Set system timezone
 * @param tz Timezone string in POSIX format
 * @return ESP_OK
 */
esp_err_t system_set_timezone(const char *tz)
{
    setenv("TZ", tz, 1);  // Set timezone environment variable
    tzset();              // Apply timezone change
    return ESP_OK;
}

/**
 * Get firmware version string
 * @return Version string from app description
 */
const char *system_get_version()
{
    const esp_app_desc_t *desc = esp_app_get_description();
    return desc->version;
}

/**
 * Reset system configuration and storage
 * Erases all user settings and formats storage
 */
void system_reset()
{
    cfg_user_erase_all();  // Clear all configuration
    storage_format();      // Format storage
}

/**
 * Restart the system
 */
void system_restart()
{
    esp_restart();  // ESP32 system restart
}

/**
 * Get system restart reason
 * @return Restart reason code
 */
rstReason_e system_restart_reasons()
{
    int reason = esp_reset_reason();
    switch (reason) {
        case ESP_RST_POWERON:
            return RST_POWER_ON;     // Power-on reset
        case ESP_RST_SW:
            return RST_SOFTWARE;     // Software reset
        case ESP_RST_DEEPSLEEP:
            return RST_DEEP_SLEEP;   // Wake from deep sleep
        default:
            ESP_LOGW(TAG, "unknown wakeup reason [%d]", reason);
            return RST_POWER_ON;     // Default to power-on
    }
}

/**
 * Print memory usage information
 * Shows both internal and SPI RAM heap usage
 */
void system_show_meminfo()
{
    ESP_LOGI(TAG, "show meminfo:");
    heap_caps_print_heap_info(MALLOC_CAP_INTERNAL);  // Internal RAM
    heap_caps_print_heap_info(MALLOC_CAP_SPIRAM);    // SPI RAM
}

/**
 * Execute scheduled system tasks
 * Performs time sync, firmware updates, and cloud platform operations
 * based on current platform configuration
 */
void system_schedule_todo()
{
    platformParamAttr_t platformParam;
    cfg_get_platform_param_attr(&platformParam);
    
    if (wifi_sta_is_connected() || netModule_is_cat1()) {
        if (iot_mip_dm_is_enable()) {
            // Device management operations for MIP platform
            ESP_LOGI(TAG, "Pending DM ...");
            iot_mip_dm_pending(30000);
            iot_mip_dm_request_timestamp();
            iot_mip_dm_response_wake_up();
            iot_mip_dm_request_api_token();
            
            // Fallback to NTP if cloud platform not connected
            if (!mqtt_mip_is_connected()) {
                system_ntp_time();
            }
            ESP_LOGI(TAG, "Pending DM Done");
        } else if (platformParam.currentPlatformType == PLATFORM_TYPE_SENSING) {
            // Operations for Sensing platform
            // 1. Time synchronization
            if (http_client_sync_server_time() == ESP_FAIL) {
                system_ntp_time();  // Fallback to NTP
            }
            // 2. Firmware/config updates
            http_client_check_update();
        } else if (platformParam.currentPlatformType == PLATFORM_TYPE_MQTT) {
            // Operations for MQTT platform
            ESP_LOGI(TAG, "NTP Synchronizing");
            if (system_ntp_time() == ESP_FAIL) {
                ESP_LOGI(TAG, "NTP Failed");
            }
        }
    }
    sleep_set_event_bits(SLEEP_SCHEDULE_DONE_BIT);  // Signal completion
}

/**
 * Execute upload tasks
 * Triggers storage upload process for scheduled upload mode
 */
void system_upload_todo()
{
    uploadAttr_t upload;
    esp_err_t ret = cfg_get_upload_attr(&upload);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get upload configuration: %s", esp_err_to_name(ret));
        return;
    }
    
    ESP_LOGI(TAG, "Upload task - Mode: %d, TimedCount: %d", 
             upload.uploadMode, upload.timedCount);
    
    switch (upload.uploadMode) {
        case 0:
            // Instant upload mode - uploads happen immediately after capture
            ESP_LOGI(TAG, "Instant upload mode - no scheduled action needed");
            break;
            
        case 1:
            // Scheduled upload mode - trigger storage upload
            ESP_LOGI(TAG, "Triggering scheduled storage upload");
            storage_upload_start();
            // Update last upload time
            sleep_set_last_upload_time(time(NULL));
            break;
            
        default:
            ESP_LOGW(TAG, "Unknown upload mode: %d", upload.uploadMode);
            break;
    }
}
