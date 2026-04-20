/* 
 * Sleep management module for ESP32-CAM
 * Handles deep sleep configuration, wakeup sources, and sleep timing
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include <math.h>
#include "sdkconfig.h"
#include "soc/soc_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_sleep.h"
#include "esp_log.h"
#include "driver/rtc_io.h"
#include "soc/rtc.h"
#include "sleep.h"
#include "config.h"
#include "utils.h"
#include "misc.h"
#include "wifi.h"
#include "cat1.h"
#include "camera.h"
#include "mqtt.h"
#include "push.h"
#include "pir.h"
#include "net_module.h"
#include "rtc_pcf8563.h"
#include "session_log.h"

#define TAG "-->SLEEP"  // Logging tag

#define SLEEP_WAIT_TIMEOUT_MS (30*60*1000) // 30 minute timeout
#define uS_TO_S_FACTOR 1000000ULL          // Microseconds to seconds conversion


// Time compensation removed - using external PCF8563 RTC as time source
/**
 * Sleep module state structure
 */
typedef struct mdSleep {
    EventGroupHandle_t eventGroup;  // Event group for sleep synchronization
} mdSleep_t;

// RTC memory preserved variables
static RTC_DATA_ATTR uint32_t g_wakeupTodo = 0;  // each 4 bits represent one action, 8 actions total, 32 bits total, lower bits have higher priority
static RTC_DATA_ATTR time_t g_lastCapTime = 0;          // Timestamp of last capture
static RTC_DATA_ATTR time_t g_lastUploadTime = 0;       // Timestamp of last upload
static RTC_DATA_ATTR time_t g_lastScheduleTime = 0;      // Timestamp of last schedule
static RTC_DATA_ATTR time_t g_willWakeupTime = 0;       // Timestamp of will wakeup

static mdSleep_t g_sleep = {0};  // Global sleep state

/* Initialize compensation controller */
// All time compensation functions removed - using PCF8563 RTC as accurate time source

/**
 * Find the most recent time interval for scheduled wakeups
 * @param timedCount Number of scheduled time nodes
 * @param timedNodes Array of scheduled time configurations
 * @return Seconds until next scheduled wakeup
 */
static uint32_t find_most_recent_time_interval(uint8_t timedCount, const timedNode_t *timedNodes)
{
    int Hour, Minute, Second;
    struct tm timeinfo;
    uint8_t i = 0;
    time_t now;
    time_t tmp;
    time_t now2sunday;
    time_t intervalSeconds = 0;

    time(&now);
    localtime_r(&now, &timeinfo);
    // Calculate seconds since last Sunday 00:00:00
    now2sunday = ((timeinfo.tm_wday * 24 + timeinfo.tm_hour) * 60 + timeinfo.tm_min) * 60 + timeinfo.tm_sec;
    
    for (i = 0; i < timedCount; i++) {
        if (sscanf(timedNodes[i].time, "%02d:%02d:%02d", &Hour, &Minute, &Second) != 3) {
            ESP_LOGE(TAG, "invalid date %s", timedNodes[i].time);
            continue;
        }
        
        if (timedNodes[i].day < 7) { // Day of week specified
            tmp = ((timedNodes[i].day * 24 + Hour) * 60 + Minute) * 60 + Second;
            if (tmp < now2sunday) { // Time is in past, schedule for next week
                tmp += 7 * 24 * 60 * 60; // Add one week
            }
        } else { // Daily schedule
            tmp = ((timeinfo.tm_wday * 24 + Hour) * 60 + Minute) * 60 + Second;
            if (tmp < now2sunday) { // Time is in past, schedule for next day
                tmp += 1 * 24 * 60 * 60; // Add one day
            }
        }
        
        if (intervalSeconds == 0) {
            intervalSeconds = tmp - now2sunday;
        } else {
            intervalSeconds = MIN(intervalSeconds, (tmp - now2sunday)); // Find nearest wakeup time
        }
    }
    return timedCount ? MAX(intervalSeconds, 1) : 0; // Ensure minimum 1 second interval
}

/**
 * Convert interval value to seconds based on unit
 * @param intervalValue The interval value
 * @param intervalUnit The unit (0: minutes, 1: hours, 2: days)
 * @return Interval in seconds, 0 if invalid unit
 */
static uint32_t convert_interval_to_seconds(uint32_t intervalValue, uint8_t intervalUnit)
{
    if (intervalValue == 0) {
        return 0;
    }
    
    switch (intervalUnit) {
        case 0: // Minutes
            return intervalValue * 60;
        case 1: // Hours
            return intervalValue * 60 * 60;
        case 2: // Days
            return intervalValue * 60 * 60 * 24;
        default:
            ESP_LOGW(TAG, "Invalid interval unit: %d", intervalUnit);
            return 0;
    }
}

/**
 * Calculate capture wakeup time
 * @param capture Capture configuration
 * @param lastCapTime Last capture timestamp
 * @param now Current time
 * @return Seconds until next capture, 0 if disabled
 */
static uint32_t calculate_capture_wakeup(const capAttr_t *capture, time_t lastCapTime, time_t now)
{
    if (!capture) {
        ESP_LOGE(TAG, "Invalid capture configuration pointer");
        return 0;
    }

    if (capture->bScheCap == 0) {
        ESP_LOGD(TAG, "Capture scheduling disabled");
        return 0; // Schedule mode disabled
    }

    if (capture->scheCapMode == 1) {
        // Interval-based capture mode
        uint32_t interval_sec = convert_interval_to_seconds(capture->intervalValue, capture->intervalUnit);
        if (interval_sec == 0) {
            ESP_LOGW(TAG, "Invalid capture interval configuration");
            return 0;
        }

        ESP_LOGD(TAG, "Capture interval mode: %lu seconds", interval_sec);

        // Handle missed captures
        if (lastCapTime && lastCapTime > 0) {
            if (now >= lastCapTime + interval_sec) {
                ESP_LOGI(TAG, "Missed capture window, triggering immediate capture");
                return 1; // Capture immediately if missed window
            } else {
                uint32_t next_capture = lastCapTime + interval_sec - now;
                ESP_LOGD(TAG, "Next capture in %lu seconds", next_capture);
                return next_capture; // Time until next capture
            }
        }
        
        // Force immediate capture if last snapshot failed
        if (camera_is_snapshot_fail()) {
            ESP_LOGI(TAG, "Last snapshot failed, triggering immediate retry");
            return 1; 
        }
        
        return interval_sec;
    } else if (capture->scheCapMode == 0) {
        // Time-based capture mode
        if (capture->timedCount == 0) {
            ESP_LOGW(TAG, "Time-based capture mode enabled but no times configured");
            return 0;
        }
        ESP_LOGD(TAG, "Time-based capture mode with %d scheduled times", capture->timedCount);
        return find_most_recent_time_interval(capture->timedCount, capture->timedNodes);
    } else {
        ESP_LOGW(TAG, "Unknown capture schedule mode: %d", capture->scheCapMode);
    }

    return 0;
}

/**
 * Calculate upload wakeup time
 * @param upload Upload configuration
 * @param lastUploadTime Last upload timestamp
 * @param now Current time
 * @return Seconds until next upload, 0 if disabled or instant mode
 */
static uint32_t calculate_upload_wakeup(const uploadAttr_t *upload, time_t lastUploadTime, time_t now)
{
    if (!upload) {
        ESP_LOGE(TAG, "Invalid upload configuration pointer");
        return 0;
    }

    if (upload->uploadMode != 1) {
        ESP_LOGD(TAG, "Upload mode %d - no scheduled wakeup needed", upload->uploadMode);
        return 0; // Instant upload mode or disabled
    }

    ESP_LOGD(TAG, "Scheduled upload mode - TimedCount: %d", upload->timedCount);

    if (upload->timedCount > 0) {
        // Time-based upload mode
        if (upload->timedCount > 10) {  // Validate maximum allowed
            ESP_LOGW(TAG, "Upload timed count exceeds maximum: %d", upload->timedCount);
            return 0;
        }
        ESP_LOGD(TAG, "Time-based upload with %d scheduled times", upload->timedCount);
        return find_most_recent_time_interval(upload->timedCount, upload->timedNodes);
    } else {
        ESP_LOGW(TAG, "Scheduled upload mode enabled but no timed configuration found");
    }

    return 0;
}

/**
 * Calculate schedule wakeup time
 * @param scheTimeNode Schedule time node
 * @param lastScheduleTime Last schedule timestamp
 * @param now Current time
 * @return Seconds until next schedule, 0 if disabled
 */
static uint32_t calculate_schedule_wakeup(const timedNode_t *scheTimeNode, time_t lastScheduleTime, time_t now)
{
    time_t tmp;
    tmp = find_most_recent_time_interval(1, scheTimeNode);
    // if the next schedule time is less than 3 hours from the last schedule time, next day schedule
    if (now + tmp < lastScheduleTime + 3 * 60 * 60) {
        return tmp + 24 * 60 * 60;
    } else {
        return tmp;
    }
}
/**
 * Update the wakeup todo list with the earliest wakeup time and corresponding action with automatic conflict resolution
 * @param capture_time Capture wakeup time in seconds
 * @param upload_time Upload wakeup time in seconds  
 * @param schedule_time Schedule wakeup time in seconds
 */
static void update_wakeup_todo_list(uint32_t earliest_time, uint32_t capture_time, uint32_t upload_time, uint32_t schedule_time)
{

    // add all tasks scheduled at earliest time to queue, sorted by priority
    if (capture_time == earliest_time) {
        sleep_set_wakeup_todo(WAKEUP_TODO_SNAPSHOT, 0);  // highest priority
        ESP_LOGI(TAG, "Scheduled SNAPSHOT at time %lu with priority 0", earliest_time);
    }
    
    if (upload_time == earliest_time) {
        sleep_set_wakeup_todo(WAKEUP_TODO_UPLOAD, 1);    // medium priority
        ESP_LOGI(TAG, "Scheduled UPLOAD at time %lu with priority 1", earliest_time);
    }
    
    if (schedule_time == earliest_time) {
        sleep_set_wakeup_todo(WAKEUP_TODO_SCHEDULE, 2);  // lowest priority
        ESP_LOGI(TAG, "Scheduled SCHEDULE at time %lu with priority 2", earliest_time);
    }

    ESP_LOGI(TAG, "Wakeup times - Capture: %lu, Upload: %lu, Schedule: %lu, Selected: %lu, Queued tasks: 0x%08lx",
             capture_time, upload_time, schedule_time, earliest_time, g_wakeupTodo);

}

/**
 * Calculate next wakeup time in seconds
 * @param bUpdateWakeupTodo Whether to update the wakeup todo list
 * @return Seconds until next wakeup
 */
uint32_t  calc_wakeup_time_seconds(bool bUpdateWakeupTodo)
{
    capAttr_t capture;
    uploadAttr_t upload;
    timedNode_t scheTimeNode;
    uint32_t earliest_wakeup = 0;
    time_t lastCapTime = sleep_get_last_capture_time();
    time_t now = time(NULL);

    // Load configurations
    memset(&scheTimeNode, 0, sizeof(scheTimeNode));
    scheTimeNode.day = 7;
    cfg_get_schedule_time(scheTimeNode.time);
    cfg_get_cap_attr(&capture);
    cfg_get_upload_attr(&upload);
    
    ESP_LOGI(TAG, "Calculating wakeup times - Capture enabled: %d, Upload mode: %d", 
             capture.bScheCap, upload.uploadMode);

    // Calculate wakeup times for each module
    time_t lastUploadTime = sleep_get_last_upload_time();
    time_t lastScheduleTime = sleep_get_last_schedule_time();
    uint32_t capture_wakeup = calculate_capture_wakeup(&capture, lastCapTime, now);
    uint32_t upload_wakeup = calculate_upload_wakeup(&upload, lastUploadTime, now);
    uint32_t schedule_wakeup = calculate_schedule_wakeup(&scheTimeNode, lastScheduleTime, now);
    // uint32_t schedule_wakeup = find_most_recent_time_interval(1, &scheTimeNode);
        
    // find earliest execution time
    if (capture_wakeup > 0 && (earliest_wakeup == 0 || capture_wakeup < earliest_wakeup)) {
        earliest_wakeup = capture_wakeup;
    }
    if (upload_wakeup > 0 && (earliest_wakeup == 0 || upload_wakeup < earliest_wakeup)) {
        earliest_wakeup = upload_wakeup;
    }
    if (schedule_wakeup > 0 && (earliest_wakeup == 0 || schedule_wakeup < earliest_wakeup)) {
        earliest_wakeup = schedule_wakeup;
    }

    if (earliest_wakeup == 0) {
        ESP_LOGW(TAG, "No valid wakeup times found");
        return 0;
    }

    // Determine the earliest wakeup time
    if (bUpdateWakeupTodo) {
        update_wakeup_todo_list(earliest_wakeup, capture_wakeup, upload_wakeup, schedule_wakeup);
    }

    return earliest_wakeup;
}

/**
 * Calculate next snapshot time
 * @return Seconds until next snapshot
 */
uint32_t calc_next_snapshot_time()
{
    capAttr_t capture;
    cfg_get_cap_attr(&capture);
    time_t now = time(NULL);
    time_t lastCapTime = sleep_get_last_capture_time();
    return calculate_capture_wakeup(&capture, lastCapTime, now);
}

/**
 * Enter deep sleep mode
 * Configures wakeup sources and enters low-power state
 */
void sleep_start(void)
{
    time_t now;
    capAttr_t capture;
    cfg_get_cap_attr(&capture);
    time(&now);
    misc_show_time("now sleep at", now);
    
    // Calculate wakeup time
    int wakeup_time_sec = 0;

    if (sleep_has_wakeup_todo()) {
        wakeup_time_sec = 1;
    } else {
        wakeup_time_sec = calc_wakeup_time_seconds(true);
    }

    if (wakeup_time_sec > 0) {
    
        // Use external RTC (PCF8563) for wakeup
        time_t actual_wakeup_time = 0;
        esp_err_t rtc_ret = rtc_set_alarm(wakeup_time_sec, &actual_wakeup_time);
        
        if (rtc_ret == ESP_OK) {
            g_willWakeupTime = actual_wakeup_time;
            ESP_LOGI(TAG, "Using RTC alarm (minute-precision)");
        } else {
            // Fallback to internal timer
            g_willWakeupTime = now + wakeup_time_sec;
            ESP_LOGW(TAG, "RTC alarm failed (%s), using internal timer", esp_err_to_name(rtc_ret));
            esp_sleep_enable_timer_wakeup(wakeup_time_sec * uS_TO_S_FACTOR);
            ESP_LOGI(TAG, "Enabling TIMER wakeup on %ds", wakeup_time_sec);
        }
        
        misc_show_time("wake will at", g_willWakeupTime);
    }
    // Configure EXT1 wakeup: always enable RTC INT pin (active low)
    uint64_t ext1_mask = 0;
    // Configure RTC INT pin for deep sleep wakeup
    ESP_LOGI(TAG, "Configured RTC INT pin (GPIO%d) for EXT1 wakeup", PCF8563_INT_PIN);
    rtc_gpio_pullup_en(PCF8563_INT_PIN);
    rtc_gpio_pulldown_dis(PCF8563_INT_PIN);
    ext1_mask |= BIT64(PCF8563_INT_PIN);

    // Configure button wakeup
    ESP_LOGI(TAG, "Configured BTN INT pin (GPIO%d) for EXT1 wakeup", BTN_WAKEUP_PIN);
    rtc_gpio_pullup_en(BTN_WAKEUP_PIN);
    rtc_gpio_pulldown_dis(BTN_WAKEUP_PIN);
    ext1_mask |= BIT64(BTN_WAKEUP_PIN);
    
    uint8_t trigger_mode = TRIGGER_MODE_DISABLED;
    if(capture.bAlarmInCap == true){
        cfg_get_trigger_mode(&trigger_mode);
        
        if(trigger_mode == TRIGGER_MODE_PIR){
            // PIR trigger mode
            // PIR is active high, ALARMIN is disabled in this configuration.
            // If PIR is used together with PCF8563, only PCF8563 will reliably wake from deep sleep.
            esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
            rtc_gpio_pullup_dis(PIR_WAKEUP_PIN);
            rtc_gpio_pulldown_en(PIR_WAKEUP_PIN);
            esp_sleep_enable_ext0_wakeup(PIR_WAKEUP_PIN, PIR_WAKEUP_LEVEL);
            ESP_LOGI(TAG, "Configured PIR INT pin (GPIO%d) for EXT0 wakeup", PIR_WAKEUP_PIN);
        } else if(trigger_mode == TRIGGER_MODE_ALARM){
            // Alarm input trigger mode
            // Combine external alarm input (active low) with PCF8563 INT pin
            rtc_gpio_pullup_en(ALARMIN_WAKEUP_PIN);
            rtc_gpio_pulldown_dis(ALARMIN_WAKEUP_PIN);
            ext1_mask |= BIT64(ALARMIN_WAKEUP_PIN);
            ESP_LOGI(TAG, "Configured ALARM INT pin (GPIO%d) for EXT1 wakeup", ALARMIN_WAKEUP_PIN);
        } else {
            // TRIGGER_MODE_DISABLED or invalid - no wakeup configured
            ESP_LOGI(TAG, "Trigger mode disabled or invalid, no external wakeup configured");
        }
    } else {
        ESP_LOGI(TAG, "Trigger capture disabled, no external wakeup configured");
    }

    if (ext1_mask != 0) {
        // All wake sources on EXT1 are active low (PCF8563 INT and ALARM_IN)
        ESP_LOGI(TAG, "Enabling EXT1 wakeup on mask 0x%016llx (ANY_LOW)", (unsigned long long)ext1_mask);
        esp_sleep_enable_ext1_wakeup(ext1_mask, ESP_EXT1_WAKEUP_ANY_LOW);
    }
    push_stop();
    wifi_close();
    cat1_close();
    
    if(capture.bAlarmInCap == true && trigger_mode == TRIGGER_MODE_PIR){
        esp_log_level_set("gpio", ESP_LOG_WARN);
        pir_init(1);
    }
    ESP_LOGI(TAG, "Entering deep sleep");
    session_log_close_for_sleep();
    esp_deep_sleep_start();
}

/**
 * Determine wakeup source
 * @return Type of wakeup that occurred
 */
wakeupType_e sleep_wakeup_case()
{
    switch (esp_sleep_get_wakeup_cause()) {
        case ESP_SLEEP_WAKEUP_EXT0: {
            ESP_LOGI(TAG, "Wake up from PIR");
            return WAKEUP_PIR;
        }
        case ESP_SLEEP_WAKEUP_EXT1: {
            uint64_t wakeup_pin_mask = esp_sleep_get_ext1_wakeup_status();
            
            // Log wakeup status
            if (wakeup_pin_mask == 0) {
                ESP_LOGW(TAG, "Wake up from EXT1 but mask is 0 (GPIO detection failed)");
            } else {
                ESP_LOGI(TAG, "Wake up from EXT1 GPIO %d (mask: 0x%llx)", \
                     __builtin_ffsll(wakeup_pin_mask) - 1, \
                     (unsigned long long)wakeup_pin_mask);
            }
            // If wakeup from RTC INT pin, treat as timer wakeup
            if (wakeup_pin_mask & BIT64(PCF8563_INT_PIN)) {
                // Clear RTC alarm flag if wakeup from RTC
                ESP_LOGI(TAG, "Wake up from RTC INT pin");
                rtc_clear_alarm();
                return WAKEUP_TIMER;
            } else if (wakeup_pin_mask & BIT64(BTN_WAKEUP_PIN)) {
                ESP_LOGI(TAG, "Wake up from button");
                return WAKEUP_BUTTON;
            } else if (wakeup_pin_mask & BIT64(ALARMIN_WAKEUP_PIN)) {
                ESP_LOGI(TAG, "Wake up from ALARM_IN");
                return WAKEUP_ALARMIN;
            } else {
                ESP_LOGW(TAG, "Wake up from EXT1 but mask is not 0x%llx", (unsigned long long)wakeup_pin_mask);
            }
            // other EXT1 sources (e.g. ALARM_IN)
            return WAKEUP_ALARMIN;
        }
        case ESP_SLEEP_WAKEUP_TIMER: {
            ESP_LOGI(TAG, "Wake up from timer");
            return WAKEUP_TIMER;
        }
        case ESP_SLEEP_WAKEUP_GPIO: {
            ESP_LOGI(TAG, "Wake up from GPIO");
            return WAKEUP_UNDEFINED;
        }
        case ESP_SLEEP_WAKEUP_UNDEFINED: {
            ESP_LOGI(TAG, "Wake up from UNDEFINED");
            return WAKEUP_UNDEFINED;
        }
        default: {
            ESP_LOGI(TAG, "Not a deep sleep reset");
            return WAKEUP_UNDEFINED;
        }
    }
}
/**
 * Initialize sleep module
 */
void sleep_open()
{
    memset(&g_sleep, 0, sizeof(g_sleep));
    g_sleep.eventGroup = xEventGroupCreate();
}

/**
 * Wait for specified event bits before sleeping
 * @param bits Event bits to wait for
 * @param bWaitAll True to wait for all bits, false for any bit
 */
void sleep_wait_event_bits(sleepBits_e bits, bool bWaitAll)
{
    ESP_LOGI(TAG, "WAIT for event bits to sleep ... ");
    EventBits_t uxBits = xEventGroupWaitBits(g_sleep.eventGroup, bits, \
                                             true, bWaitAll, \
                                             pdMS_TO_TICKS(SLEEP_WAIT_TIMEOUT_MS));
    ESP_LOGI(TAG, "sleep right now, bits=%lu", uxBits);
    sleep_start();
}

/**
 * Set sleep event bits
 * @param bits Event bits to set
 */
void sleep_set_event_bits(sleepBits_e bits)
{
    xEventGroupSetBits(g_sleep.eventGroup, bits);
}

/**
 * Clear sleep event bits
 * @param bits Event bits to clear
 */
void sleep_clear_event_bits(sleepBits_e bits)
{
    xEventGroupClearBits(g_sleep.eventGroup, bits);
}

/**
 * Get action to perform after wakeup
 * @return Scheduled wakeup action
 */
wakeupTodo_e sleep_get_wakeup_todo()
{
    ESP_LOGI(TAG, "todo queue: 0x%lx", g_wakeupTodo);
    
    if (g_wakeupTodo == 0) {
        ESP_LOGI(TAG, "No wakeup todo remaining");
        return WAKEUP_TODO_NOTHING;
    }
    
    // start searching from highest priority (priority 0, bits 0-3)
    for (uint8_t priority = 0; priority < 8; priority++) {
        uint32_t shift_amount = priority * 4;
        uint32_t mask = 0x0000000F << shift_amount;
        uint32_t todo_bits = (g_wakeupTodo & mask) >> shift_amount;
        
        if (todo_bits != 0) {
            wakeupTodo_e todo = (wakeupTodo_e)todo_bits;
            
            // clear this task
            g_wakeupTodo &= ~mask;
            
            ESP_LOGI(TAG, "Retrieved todo %d from priority %d, remaining: 0x%lx", 
                     todo, priority, g_wakeupTodo);
            return todo;
        }
    }
    
    ESP_LOGW(TAG, "No valid todo found in queue");
    return WAKEUP_TODO_NOTHING;
}

/**
 * Set action to perform after wakeup
 * @param todo Action to perform
 * @param priority Priority of the action (0 = highest priority, 7 = lowest priority)
 */
void sleep_set_wakeup_todo(wakeupTodo_e todo, uint8_t priority)
{
    const char *todo_str;
    switch (todo) {
        case WAKEUP_TODO_NOTHING:
            todo_str = "NONE";
            break;
        case WAKEUP_TODO_SNAPSHOT:
            todo_str = "SNAPSHOT";
            break;
        case WAKEUP_TODO_CONFIG:
            todo_str = "CONFIG";
            break;
        case WAKEUP_TODO_UPLOAD:
            todo_str = "UPLOAD";
            break;
        case WAKEUP_TODO_SCHEDULE:
            todo_str = "SCHEDULE";
            break;
        default:
            todo_str = "UNKNOWN";
            break;
    }
    
    ESP_LOGI(TAG, "sleep_set_wakeup_todo %d (%s), priority %d", todo, todo_str, priority);
    
    // ensure priority is within valid range
    if (priority > 7) {
        priority = 7;
    }
    
    // insert task to appropriate position based on priority
    // high priority (small value) placed in lower bits, low priority (large value) placed in higher bits
    uint32_t shift_amount = priority * 4;
    uint32_t mask = 0x0000000F << shift_amount;
    
    // clear existing task at this priority position
    g_wakeupTodo &= ~mask;
    
    // insert new task
    g_wakeupTodo |= ((uint32_t)todo & 0x0F) << shift_amount;
    
    ESP_LOGI(TAG, "Updated wakeup todo queue: 0x%lx", g_wakeupTodo);
}

/**
 * Clear action to perform after wakeup at specific priority
 * @param priority Priority of the action to clear (0 = highest priority, 7 = lowest priority)
 */
void sleep_clear_wakeup_todo(uint8_t priority)
{
    if (priority > 7) {
        priority = 7;
    }
    
    uint32_t shift_amount = priority * 4;
    uint32_t mask = 0x0000000F << shift_amount;
    
    // clear task at this priority position
    g_wakeupTodo &= ~mask;
    
    ESP_LOGI(TAG, "Cleared wakeup todo at priority %d, remaining: 0x%lx", priority, g_wakeupTodo);
}

/**
 * Check if there is any action to perform after wakeup
 * @return true if there is any action to perform, false otherwise
 */
bool sleep_has_wakeup_todo()
{
    return g_wakeupTodo != 0;
}


/**
 * 
 */
void sleep_reset_wakeup_todo()
{
    g_wakeupTodo = 0;
}

/**
 * Set timestamp of last capture
 * @param time Timestamp to store
 */
void sleep_set_last_capture_time(time_t time)
{
    g_lastCapTime = time;
}

/**
 * Get timestamp of last capture
 * @return Last capture timestamp
 */
time_t sleep_get_last_capture_time(void)
{
    return g_lastCapTime;
}

/**
 * Set timestamp of last upload
 * @param time Timestamp to set
 */
void sleep_set_last_upload_time(time_t time)
{
    g_lastUploadTime = time;
}

/**
 * Get timestamp of last upload
 * @return Last upload timestamp
 */
time_t sleep_get_last_upload_time(void)
{
    return g_lastUploadTime;
}

/**
 * Set timestamp of last schedule
 * @param time Timestamp to set
 */
void sleep_set_last_schedule_time(time_t time)
{
    g_lastScheduleTime = time;
}

/**
 * Get timestamp of last schedule
 * @return Last schedule timestamp
 */
time_t sleep_get_last_schedule_time(void)
{
    return g_lastScheduleTime;
}
/**
 * Check if alarm input should trigger restart
 * @return 1 if should restart, 0 otherwise
 */
uint32_t sleep_is_alramin_goto_restart()
{
    return rtc_gpio_get_level(ALARMIN_WAKEUP_PIN) == ALARMIN_WAKEUP_LEVEL;
}

/**
 * Get will wakeup time
 * @return true if the time is reached, false otherwise
 * 
 * Note: For PCF8563 minute-precision alarms, we allow a tolerance of 60 seconds
 * to account for time synchronization differences and prevent unnecessary re-sleep cycles.
 */
bool sleep_is_will_wakeup_time_reached(void)
{
    time_t now = time(NULL);
    time_t diff = g_willWakeupTime - now;
    
    // Allow tolerance of 60 seconds for minute-precision alarms
    // This prevents unnecessary re-sleep when PCF8563 triggers slightly early
    // due to time sync differences or minute-level precision
    if (diff <= 60 && diff >= -60) {
        return true;  // Within tolerance, consider time reached
    }
    
    return g_willWakeupTime <= now;
}
