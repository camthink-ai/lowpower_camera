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

#define TAG "-->MAIN"

// Queue configuration constants
#define MQTT_QUEUE_SIZE     3
#define STORAGE_QUEUE_SIZE  2

// LED blink configuration for status indication
#define STATUS_LED_BLINK_COUNT    1
#define STATUS_LED_BLINK_INTERVAL 1000

modeSel_e main_mode;

/**
 * @brief Handle power-on reset scenario
 * @return Selected mode for power-on reset
 */
static modeSel_e handle_power_on_reset(void)
{
    comp_init();
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
    // time_compensation_boot();
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
    
    camera_open(NULL, xQueueMqtt);
    if (snapType == SNAP_BUTTON) {
        camera_snapshot(snapType, 1);
    }
    sleep_reset_wakeup_todo();
    netModule_open(main_mode);
    http_open();
    
    sleep_wait_event_bits(SLEEP_SNAPSHOT_STOP_BIT | SLEEP_STORAGE_UPLOAD_STOP_BIT | 
                          SLEEP_NO_OPERATION_TIMEOUT_BIT | SLEEP_MIP_DONE_BIT, true);
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
    misc_open((uint8_t*)&main_mode);
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
    // main_mode = MODE_SNAPSHOT; //TODO: for test
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
