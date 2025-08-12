/* Base mac address example

 This example code is in the Public Domain (or CC0 licensed, at your option.)

 Unless required by applicable law or agreed to in writing, this
 software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 CONDITIONS OF ANY KIND, either express or implied.
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

modeSel_e main_mode;

static modeSel_e mode_selector(snapType_e *snapType)
{
    rstReason_e rst;
    wakeupType_e type;
    wakeupTodo_e todo;

    rst = system_restart_reasons();

    if (rst == RST_POWER_ON) {
        comp_init();
        netModule_check();
        return MODE_SCHEDULE;
    } else if(netModule_is_check_flag()){
        ESP_LOGI(TAG, "mode_selector netModule_is_check_reset");
        netModule_clear_check_flag();
        return MODE_SCHEDULE;
    }else if (rst == RST_SOFTWARE) {
        return MODE_CONFIG;
    } else if (rst == RST_DEEP_SLEEP) {
        type = sleep_wakeup_case();
        if (type == WAKEUP_TIMER) {
            todo = sleep_get_wakeup_todo();
            if (todo == WAKEUP_TODO_SNAPSHOT) {
                *snapType = SNAP_TIMER;
                return MODE_WORK;
            } else if (todo == WAKEUP_TODO_SCHEDULE) {
                return MODE_SCHEDULE;
            }else if (todo == WAKEUP_TODO_CONFIG) {
                return MODE_CONFIG;
            }
        } else if (type == WAKEUP_ALARMIN) {
            *snapType = SNAP_ALARMIN;
            return MODE_WORK;
        } else if (type == WAKEUP_BUTTON) {
            *snapType = SNAP_BUTTON;
            return MODE_CONFIG;
        }
    }
    ESP_LOGE(TAG, "unknown wakeup %d", rst);
    return MODE_SLEEP;
}

void crash_handler(void) 
{
    esp_reset_reason_t reason = esp_reset_reason();
    ESP_LOGE("CrashHandler", "ESP32 Crashed! Reset reason: %d", reason);
    // esp_rom_printf(100);
}


void app_main(void)
{
    ESP_LOGI(TAG, "start main..");
    esp_register_shutdown_handler(crash_handler);
    // time_compensation_boot();
    srand(esp_random());

    debug_open();
    cfg_init();

    snapType_e snapType;
    main_mode = mode_selector(&snapType);

    sleep_open();
    iot_mip_init();
    if (main_mode == MODE_SLEEP) {
        ESP_LOGI(TAG, "sleep mode");
        sleep_start();
        return;
    }
    misc_open((uint8_t*)&main_mode);
    netModule_init(main_mode);

    QueueHandle_t xQueueMqtt = xQueueCreate(3, sizeof(queueNode_t *));
    QueueHandle_t xQueueStorage = xQueueCreate(2, sizeof(queueNode_t *));
    storage_open(xQueueStorage, xQueueMqtt);
    mqtt_open(xQueueMqtt, xQueueStorage);
    // main_mode = MODE_WORK; //TODO: for test
    if (main_mode == MODE_WORK) {
        misc_led_blink(1, 1000);
        ESP_LOGI(TAG, "work mode");
        camera_open(NULL, xQueueMqtt);
        camera_snapshot(snapType, 1);
        camera_close();
        misc_flash_led_close();
        netModule_open(main_mode);
        sleep_wait_event_bits(SLEEP_SNAPSHOT_STOP_BIT | SLEEP_STORAGE_UPLOAD_STOP_BIT | SLEEP_MIP_DONE_BIT, true);
    } else if (main_mode == MODE_CONFIG) {
        misc_led_blink(1, 1000);
        ESP_LOGI(TAG, "coinfig mode");
        camera_open(NULL, xQueueMqtt);
        if(snapType == SNAP_BUTTON){
            camera_snapshot(snapType, 1);
        }
        netModule_open(main_mode);
        http_open();
        sleep_wait_event_bits(SLEEP_SNAPSHOT_STOP_BIT | SLEEP_STORAGE_UPLOAD_STOP_BIT | SLEEP_NO_OPERATION_TIMEOUT_BIT |
                              SLEEP_MIP_DONE_BIT, true);
    } else if (main_mode == MODE_SCHEDULE) {
        misc_led_blink(1, 1000);
        ESP_LOGI(TAG, "schedule mode");
        netModule_open(main_mode);
        system_schedule_todo();
        sleep_wait_event_bits(SLEEP_SCHEDULE_DONE_BIT | SLEEP_STORAGE_UPLOAD_STOP_BIT | SLEEP_MIP_DONE_BIT, true);
    }
    ESP_LOGI(TAG, "end main....");
}
