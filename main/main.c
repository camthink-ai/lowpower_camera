/**
 * @file main.c
 * @brief Main application entry point for ESP32-based IoT device
 * 
 * This file contains the main application logic for an ESP32-based IoT device
 * that supports multiple operational modes including:
 * - Work mode: Image capture and processing
 * - Config mode: Device configuration via web interface
 * - Schedule mode: Scheduled tasks and time synchronization
 * - Upload mode: Data upload operations
 * - Sleep mode: Low power operation
 * 
 * The device automatically selects the appropriate mode based on restart reasons
 * and wakeup sources, providing a robust state machine for different use cases.
 * 
 * @author ESP32 Development Team
 * @date 2024
 * 
 * This code is in the Public Domain (or CC0 licensed, at your option.)
 * Unless required by applicable law or agreed to in writing, this
 * software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "config.h"
#include "system.h"
#include "wifi.h"
#include "debug.h"
#include "http.h"
#include "misc.h"
#include "sleep.h"
#include "storage.h"
#include "camera.h"
#include "mqtt.h"
#include "cat1.h"
#include "iot_mip.h"
#include "net_module.h"
#include "rtc_pcf8563.h"
#include "morse.h"
#include "utils.h"
#include "storage.h"
#include "session_log.h"

#define TAG "-->MAIN"

// Queue configuration constants
#define MQTT_QUEUE_SIZE     3
#define STORAGE_QUEUE_SIZE  2

// LED blink configuration for status indication
#define STATUS_LED_BLINK_COUNT    1
#define STATUS_LED_BLINK_INTERVAL 1000

modeSel_e main_mode;

/**
 * @brief Print system information for factory verification
 * Prints device MAC, SN, firmware version, camera config, and wakeup config
 */
static void print_system_info(void)
{
    deviceInfo_t device;
    imgAttr_t image;
    capAttr_t capture;
    uint8_t trigger_mode;
    pirAttr_t pir_attr;
    wifiAttr_t wifi;
    
    // Get device information
    cfg_get_device_info(&device);
    
    // Get camera configuration
    cfg_get_image_attr(&image);
    
    // Get capture configuration
    cfg_get_cap_attr(&capture);
    
    // Get trigger mode
    cfg_get_trigger_mode(&trigger_mode);
    
    // Get PIR attributes if in PIR mode
    cfg_get_pir_attr(&pir_attr);
    
    // Get WiFi configuration
    cfg_get_wifi_attr(&wifi);
    
    // Resolution mapping
    const char* resolution_names[] = {
        [5] = "320x240 (QVGA)",
        [8] = "640x480 (VGA)",
        [9] = "800x600 (SVGA)",
        [10] = "1024x768 (XGA)",
        [11] = "1280x720 (HD)",
        [12] = "1280x1024 (SXGA)",
        [13] = "1600x1200 (UXGA)",
        [14] = "1920x1080 (FHD)",
        [17] = "2048x1536 (QXGA)",
        [21] = "2560x1920 (QSXGA)"
    };
    
    const char* resolution_str = "Unknown";
    if (image.frameSize < sizeof(resolution_names)/sizeof(resolution_names[0]) && 
        resolution_names[image.frameSize] != NULL) {
        resolution_str = resolution_names[image.frameSize];
    }
    
    // Trigger mode string
    const char* trigger_mode_str = "Disabled";
    if (capture.bAlarmInCap) {
        switch (trigger_mode) {
            case TRIGGER_MODE_ALARM:
                trigger_mode_str = "Alarm Input";
                break;
            case TRIGGER_MODE_PIR:
                trigger_mode_str = "PIR Sensor";
                break;
            default:
                trigger_mode_str = "Disabled";
                break;
        }
    }
    
    // Get network module type
    bool is_cat1 = netModule_is_cat1();
    bool is_halow = netModule_is_mmwifi();
    
    // Calculate AP name (format: model_MAC_last3bytes)
    char ap_name[32] = "N/A";
    if (device.mac[0] && is_valid_mac(device.mac)) {
        uint8_t mac_hex[6];
        mac_str2hex(device.mac, mac_hex);
        snprintf(ap_name, sizeof(ap_name), "%s_%02X%02X%02X", 
                 device.model, mac_hex[3], mac_hex[4], mac_hex[5]);
    }
    
    printf("========================================\n");
    printf("    SYSTEM INFORMATION (POWER-ON)      \n");
    printf("========================================\n");
    printf("Device Information:\n");
    printf("  Model: %s\n", device.model);
    printf("  Device Name: %s\n", device.name);
    printf("  MAC Address: %s\n", device.mac[0] ? device.mac : "N/A");
    printf("  AP Name: %s\n", ap_name);
    printf("  SN: %s\n", (device.sn[0] && strcmp(device.sn, "undefined") != 0) ? device.sn : "N/A");
    printf("  Hardware Version: %s\n", device.hardVersion);
    printf("  Firmware Version: %s\n", device.softVersion);
    printf("  Camera Backend: %s\n", device.camera);
    printf("  Network Module: %s\n", device.netmod[0] ? device.netmod : "N/A");
    printf("  Country Code: %s\n", device.countryCode);
    // Print regulatory domain info if HaLow is configured (compile-time info, independent of module insertion)
#ifdef CONFIG_MM_BCF_MF08251_FCC
    printf("  HaLow Regulatory Domain: FCC (915 MHz)\n");
#elif defined(CONFIG_MM_BCF_MF08251_CE)
    printf("  HaLow Regulatory Domain: CE (868 MHz)\n");
#endif
    printf("\n");
    printf("Network Information:\n");
    if (is_cat1) {
        printf("  Type: Cellular (CAT1)\n");
    } else if (is_halow) {
        printf("  Type: Wi-Fi HaLow (802.11ah)\n");
        printf("  SSID: %s\n", wifi.ssid[0] ? wifi.ssid : "N/A");
        printf("  Country Code: %s\n", device.countryCode);
    } else {
        printf("  Type: WiFi\n");
        printf("  SSID: %s\n", wifi.ssid[0] ? wifi.ssid : "N/A");
    }
    printf("\n");
    printf("Camera Configuration:\n");
    printf("  Resolution: %s (frameSize=%d)\n", resolution_str, image.frameSize);
    printf("  JPEG Quality: %d (0-63, lower=better)\n", image.quality);
    printf("  Brightness: %d\n", image.brightness);
    printf("  Contrast: %d\n", image.contrast);
    printf("  Saturation: %d\n", image.saturation);
    printf("  AE Level: %d\n", image.aeLevel);
    printf("  AGC: %s\n", image.bAgc ? "Enabled" : "Disabled");
    printf("  Horizontal Flip: %s\n", image.bHorizonetal ? "Yes" : "No");
    printf("  Vertical Flip: %s\n", image.bVertical ? "Yes" : "No");
    printf("  HDR: %s\n", image.hdrEnable ? "Enabled" : "Disabled");
    printf("\n");
    printf("Capture Configuration:\n");
    printf("  Scheduled Capture: %s\n", capture.bScheCap ? "Enabled" : "Disabled");
    printf("  Capture Mode: %s\n", capture.scheCapMode == 0 ? "Timed" : "Interval");
    printf("  Trigger Capture: %s\n", capture.bAlarmInCap ? "Enabled" : "Disabled");
    printf("  Button Capture: %s\n", capture.bButtonCap ? "Enabled" : "Disabled");
    printf("  Camera Warmup Delay: %lu ms\n", capture.camWarmupMs);
    if (capture.scheCapMode == 1) {
        const char* unit_str[] = {"min", "hour", "day"};
        printf("  Interval: %lu %s\n", capture.intervalValue, 
               capture.intervalUnit < 3 ? unit_str[capture.intervalUnit] : "unknown");
    }
    printf("\n");
    printf("Wakeup Configuration:\n");
    printf("  Trigger Mode: %s\n", trigger_mode_str);
    if (capture.bAlarmInCap && trigger_mode == TRIGGER_MODE_PIR) {
        printf("  PIR Settings:\n");
        printf("    Sensitivity: %d (0-255, recommended >20)\n", pir_attr.sens);
        printf("    Blind Time: %d (0-15, %.1fs)\n", pir_attr.blind, 
               (pir_attr.blind * 0.5) + 0.5);
        printf("    Pulse Count: %d (0-3, %d times)\n", pir_attr.pulse, pir_attr.pulse + 1);
        printf("    Window Time: %d (0-3, %ds)\n", pir_attr.window, 
               (pir_attr.window * 2) + 2);
    }
    printf("========================================\n");
}

/**
 * @brief Handle power-on reset scenario
 * @return Selected mode for power-on reset
 */
static modeSel_e handle_power_on_reset(void)
{
    netModule_check();
    return MODE_SCHEDULE;
}

/**
 * @brief Handle network module check scenario
 * @return Selected mode for network check
 */
static modeSel_e handle_network_check(void)
{
    ESP_LOGI(TAG, "mode_selector netModule_is_check_reset");
    // Print system information after network check is complete
    // This ensures accurate network module type information
    print_system_info();
    netModule_clear_check_flag();
    return MODE_SCHEDULE;
}

/**
 * @brief Handle timer wakeup from deep sleep
 * @param snapType Output parameter for snapshot type
 * @return Selected mode based on wakeup todo
 */
static modeSel_e handle_timer_wakeup(snapType_e *snapType)
{
    wakeupTodo_e todo = sleep_get_wakeup_todo();
    
    switch (todo) {
        case WAKEUP_TODO_SNAPSHOT:
            *snapType = SNAP_TIMER;
            return MODE_SNAPSHOT;
        case WAKEUP_TODO_SCHEDULE:
            return MODE_SCHEDULE;
        case WAKEUP_TODO_CONFIG:
            return MODE_CONFIG;
        case WAKEUP_TODO_UPLOAD:
            return MODE_UPLOAD;
        default:
            ESP_LOGW(TAG, "Unknown wakeup todo: %d", todo);
            return MODE_SLEEP;
    }
}

/**
 * @brief Handle deep sleep wakeup scenarios
 * @param snapType Output parameter for snapshot type
 * @return Selected mode based on wakeup type
 */
static modeSel_e handle_deep_sleep_wakeup(snapType_e *snapType)
{
    wakeupType_e type = sleep_wakeup_case();

    switch (type) {
        case WAKEUP_TIMER:
            if (!sleep_is_will_wakeup_time_reached()) {
                ESP_LOGI(TAG, "Wake up from timer, but the time is not reached, sleep again");
                return MODE_SLEEP;
            }
            return handle_timer_wakeup(snapType);
            
        case WAKEUP_ALARMIN:
            sleep_reset_wakeup_todo();  // Clear pending todo since timer schedule is no longer valid
            *snapType = SNAP_ALARMIN;
            return MODE_SNAPSHOT;
            
        case WAKEUP_BUTTON:
            *snapType = SNAP_BUTTON;
            return MODE_CONFIG;
            
        default:
            ESP_LOGW(TAG, "Unknown wakeup type: %d", type);
            return MODE_SLEEP;
    }
}

/**
 * @brief Determine the operating mode based on system restart reason
 * @param snapType Output parameter for snapshot type (only set for certain modes)
 * @return Selected operating mode
 */
static modeSel_e mode_selector(snapType_e *snapType)
{
    rstReason_e rst = system_restart_reasons();

    // Initialize snapType to undefined
    *snapType = SNAP_UNDEFINED;

    // Handle power-on reset first
    if (rst == RST_POWER_ON) {
        return handle_power_on_reset();
    }

    // Check for network module flag (has priority over other restart reasons)
    if (netModule_is_check_flag()) {
        return handle_network_check();
    }

    // Handle other restart reasons
    switch (rst) {
        case RST_SOFTWARE:
            return MODE_CONFIG;
            
        case RST_DEEP_SLEEP:
            return handle_deep_sleep_wakeup(snapType);
            
        default:
            ESP_LOGE(TAG, "Unknown restart reason: %d", rst);
            return MODE_SLEEP;
    }
}

/**
 * @brief System crash handler
 * Logs crash information before system shutdown
 */
void crash_handler(void) 
{
    esp_reset_reason_t reason = esp_reset_reason();
    ESP_LOGE("CrashHandler", "ESP32 Crashed! Reset reason: %d", reason);
    // esp_rom_printf(100);
}


/**
 * @brief Common initialization for all operational modes
 */
static void common_init(void)
{
    ESP_LOGI(TAG, "start main..");
    esp_register_shutdown_handler(crash_handler);
    srand(esp_random());

    debug_open();
    cfg_init();
    sleep_open();
    iot_mip_init();
}

/**
 * @brief Handle snapshot mode operations (image capture)
 * @param snapType Type of snapshot trigger
 * @param xQueueMqtt MQTT queue handle
 * @param xQueueStorage Storage queue handle
 */
static void handle_snapshot_mode(snapType_e snapType, QueueHandle_t xQueueMqtt, 
                                  QueueHandle_t xQueueStorage)
{
    misc_led_blink(STATUS_LED_BLINK_COUNT, STATUS_LED_BLINK_INTERVAL);
    ESP_LOGI(TAG, "snapshot mode");
    uint8_t need_netModule = 0;
    ntpSync_t ntp_sync;
    uploadAttr_t upload;

    system_get_ntp_sync(&ntp_sync);
    cfg_get_upload_attr(&upload);
    
    need_netModule = ntp_sync.enable || upload.uploadMode == 0; //If the NTP synchronization is enabled or the upload mode is instant upload, the network module is needed.

    ESP_LOGI(TAG,"ntp_sync.enable: %d", ntp_sync.enable);
    ESP_LOGI(TAG, "upload.uploadMode: %d", upload.uploadMode);
    ESP_LOGI(TAG, "need_netModule: %d", need_netModule);

    if (need_netModule) {
        camera_open(NULL, xQueueMqtt); //If the network module is needed, the camera send the image to the MQTT server.
    } else {
        camera_open(NULL, xQueueStorage); //If the network module is not needed, the camera send the image to the storage.
    }

    camera_snapshot(snapType, 1);
    camera_close();
    misc_flash_led_close();
    
    if (need_netModule) {
        netModule_open(main_mode);
    }
    
    sleep_wait_event_bits(SLEEP_SNAPSHOT_STOP_BIT | SLEEP_STORAGE_UPLOAD_STOP_BIT | SLEEP_MIP_DONE_BIT, true);
}

/**
 * @brief Handle configuration mode operations
 * @param snapType Type of snapshot trigger
 * @param xQueueMqtt MQTT queue handle
 */
static void handle_config_mode(snapType_e snapType, QueueHandle_t xQueueMqtt)
{
    misc_led_blink(STATUS_LED_BLINK_COUNT, STATUS_LED_BLINK_INTERVAL);
    ESP_LOGI(TAG, "config mode");
    
    http_open();
    sleep_reset_wakeup_todo();
    netModule_open(main_mode);
    camera_open(NULL, xQueueMqtt);
    if (snapType == SNAP_BUTTON) {
        camera_snapshot(snapType, 1);
    }
    sleep_wait_event_bits(SLEEP_SNAPSHOT_STOP_BIT | SLEEP_STORAGE_UPLOAD_STOP_BIT | 
                          SLEEP_NO_OPERATION_TIMEOUT_BIT | SLEEP_MIP_DONE_BIT | SLEEP_NO_DEBUG_BIT, true);
}

/**
 * @brief Handle schedule mode operations
 */
static void handle_schedule_mode(void)
{
    misc_led_blink(STATUS_LED_BLINK_COUNT, STATUS_LED_BLINK_INTERVAL);
    ESP_LOGI(TAG, "schedule mode");
    
    netModule_open(main_mode);
    system_schedule_todo();
    sleep_wait_event_bits(SLEEP_SCHEDULE_DONE_BIT | SLEEP_STORAGE_UPLOAD_STOP_BIT | SLEEP_MIP_DONE_BIT, true);
}

/**
 * @brief Handle upload mode operations
 */
static void handle_upload_mode(void)
{
    misc_led_blink(STATUS_LED_BLINK_COUNT, STATUS_LED_BLINK_INTERVAL);
    ESP_LOGI(TAG, "upload mode");
    
    netModule_open(main_mode);
    system_upload_todo();
    sleep_wait_event_bits(SLEEP_STORAGE_UPLOAD_STOP_BIT | SLEEP_MIP_DONE_BIT, true);
}


/**
 * @brief Initialize queues and start services for operational modes
 * @param xQueueMqtt Output parameter for MQTT queue handle
 * @param xQueueStorage Output parameter for Storage queue handle
 * @return ESP_OK on success, ESP_FAIL on failure
 */
static esp_err_t init_queues_and_services(QueueHandle_t *xQueueMqtt, QueueHandle_t *xQueueStorage)
{
    // Validate input parameters
    if (!xQueueMqtt || !xQueueStorage) {
        ESP_LOGE(TAG, "Invalid queue parameters");
        return ESP_ERR_INVALID_ARG;
    }

    // Initialize hardware and network module
    misc_open((uint8_t*)&main_mode);  // Turns on sensor power (GPIO3)
    rtc_sync_to_system();
    
    netModule_init(main_mode);

    // Create queues with error checking
    *xQueueMqtt = xQueueCreate(MQTT_QUEUE_SIZE, sizeof(queueNode_t *));
    if (*xQueueMqtt == NULL) {
        ESP_LOGE(TAG, "Failed to create MQTT queue");
        return ESP_ERR_NO_MEM;
    }

    *xQueueStorage = xQueueCreate(STORAGE_QUEUE_SIZE, sizeof(queueNode_t *));
    if (*xQueueStorage == NULL) {
        ESP_LOGE(TAG, "Failed to create Storage queue");
        vQueueDelete(*xQueueMqtt);  // Cleanup on failure
        *xQueueMqtt = NULL;
        return ESP_ERR_NO_MEM;
    }
    
    // Start services
    storage_open(*xQueueStorage, *xQueueMqtt);
    mqtt_open(*xQueueMqtt, *xQueueStorage);
    
    return ESP_OK;
}

void app_main(void)
{
    /* Mount LittleFS and start session log file (truncated each boot) before other init. */
    if (storage_ensure_mounted() == ESP_OK) {
        session_log_init();
    }

    // Initialize common components
    common_init();

    // Determine operating mode and snapshot type
    snapType_e snapType;
    main_mode = mode_selector(&snapType);

    // Handle sleep mode early exit
    if (main_mode == MODE_SLEEP) {
        ESP_LOGI(TAG, "sleep mode");
        sleep_start();
        return;
    }

    // Initialize queues and services for operational modes
    QueueHandle_t xQueueMqtt = NULL, xQueueStorage = NULL;
    esp_err_t ret = init_queues_and_services(&xQueueMqtt, &xQueueStorage);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize queues and services: %s", esp_err_to_name(ret));
        goto cleanup;
    }

    // Handle different operational modes
    // main_mode = MODE_CONFIG; //TODO: for test
    modeSel_e temp_mode = system_get_temporary_mode();
    if (temp_mode != MODE_UNDEFINED) {
        main_mode = temp_mode;
        ESP_LOGI(TAG, "temporary mode: %d", main_mode);
    }
    switch (main_mode) {
        case MODE_SNAPSHOT:
            handle_snapshot_mode(snapType, xQueueMqtt, xQueueStorage);
            break;
            
        case MODE_CONFIG:
            handle_config_mode(snapType, xQueueMqtt);
            break;
            
        case MODE_SCHEDULE:
            handle_schedule_mode();
            break;
            
        case MODE_UPLOAD:
            handle_upload_mode();
            break;
            
            
        default:
            ESP_LOGE(TAG, "Unknown mode: %d", main_mode);
            break;
    }

cleanup:
    // Cleanup resources before exit
    if (xQueueMqtt) {
        vQueueDelete(xQueueMqtt);
    }
    if (xQueueStorage) {
        vQueueDelete(xQueueStorage);
    }
    
    ESP_LOGI(TAG, "end main....");
}
