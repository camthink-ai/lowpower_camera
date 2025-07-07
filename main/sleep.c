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
#include "pir.h"
#include "net_module.h"

#define TAG "-->SLEEP"  // Logging tag

#define SLEEP_WAIT_TIMEOUT_MS (30*60*1000) // 30 minute timeout
#define uS_TO_S_FACTOR 1000000ULL          // Microseconds to seconds conversion


#define MAX_HISTORY   5      // Keep last 5 error records
#define WRITE_CFG_CNT 10    // Every 20 records are written to config.
#define ALPHA 0.4f  // Error value smoothing factor (0-1), the larger the value, the higher the weight of recent data

/* Time compensation controller structure
 * Maintains timing references and error history for drift compensation */
typedef struct {
    time_t real_prev;       // Last synchronized real time
    float errors[MAX_HISTORY]; // Error rate history (circular buffer)
    int err_index;         // Current index in error buffer
    int err_count;         // Valid error records count
    uint32_t total_count;         // Total records count
} TimeCompensator;
/**
 * Sleep module state structure
 */
typedef struct mdSleep {
    EventGroupHandle_t eventGroup;  // Event group for sleep synchronization
} mdSleep_t;

// RTC memory preserved variables
static RTC_DATA_ATTR enum wakeupTodo g_wakeupTodo = 0;  // Action to perform after wakeup
static RTC_DATA_ATTR time_t g_lastCapTime = 0;          // Timestamp of last capture
static RTC_DATA_ATTR TimeCompensator g_TimeCompensator = {0};

static mdSleep_t g_sleep = {0};  // Global sleep state

/* Initialize compensation controller */
void comp_init()
{
    int32_t err_rate;
    g_TimeCompensator.real_prev = 0;
    g_TimeCompensator.err_index = 0;
    g_TimeCompensator.err_count = 0;
    g_TimeCompensator.total_count = 0;
    for(int i=0; i<MAX_HISTORY; i++) g_TimeCompensator.errors[i] = 0;

    cfg_get_time_err_rate(&err_rate);
    if(err_rate != 0){
        g_TimeCompensator.err_count = 1;
        g_TimeCompensator.errors[0] = err_rate / (float)(10000);
        g_TimeCompensator.err_index = 1;
        ESP_LOGI(TAG, "Default error rate: %.2f%%", g_TimeCompensator.errors[0]*100);
    }
}

/* Calculate smoothed error rate using moving average
 * @return Weighted error rates */
static float get_smoothed_error() 
{
    if(g_TimeCompensator.err_count == 0) {
        ESP_LOGD(TAG, "No error history available");
        return 0.0f;
    }

    float weighted_error = 0;
    float total_weight = 0;
    float weight = 1.0f;  // Initial weights for recent data
    
    // Calculate the weighted average from the newest to the oldest data
    for(int i = 0; i < g_TimeCompensator.err_count; i++) {
        int idx = (g_TimeCompensator.err_index - 1 - i + MAX_HISTORY) % MAX_HISTORY;
        weighted_error += g_TimeCompensator.errors[idx] * weight;
        total_weight += weight;
        weight *= (1.0f - ALPHA);  // Gradually decay weight
        
        ESP_LOGD(TAG, "[%d] err=%.2f%% weight=%.2f", 
                i, g_TimeCompensator.errors[idx]*100, weight);
    }

    float result = weighted_error / total_weight;
    ESP_LOGI(TAG, "Weighted error: %.2f%% (α=%.1f, %d samples)", 
            result*100, ALPHA, g_TimeCompensator.err_count);
    return result;
}

/* Process time synchronization event
 * @param real_now Actual real time from reliable source
 * @param sys_now  Current system time */
void record_time_sync(time_t real_now, time_t sys_now) 
{
    ESP_LOGI(TAG,"Sync event - real: %lld, sys: %lld", real_now, sys_now);
    // Initial synchronization
    if(g_TimeCompensator.real_prev == 0) {
        g_TimeCompensator.real_prev = real_now;
        return;
    }

    // Calculate time deltas
    time_t delta_real = real_now - g_TimeCompensator.real_prev;
    time_t delta_sys = sys_now - g_TimeCompensator.real_prev;
    ESP_LOGI(TAG,"Time deltas - real: %lld, sys: %lld", delta_real, delta_sys);

    // Handle abnormal cases (time rollback)
    if(delta_sys <= 0 || delta_real < 0) {
        g_TimeCompensator.err_count = 0;  // Reset error history
        g_TimeCompensator.real_prev = real_now;
        return;
    }

    // Calculate error rate: (real_delta - sys_delta)/sys_delta
    float err_rate = (delta_real - delta_sys)/(float)delta_sys;
    //If the error rate exceeds the threshold or the time delta is too small, the data will be discarded.
    if( (delta_real < 300 || delta_sys < 300) || 
        err_rate < -0.1f || err_rate > 0.1f){
        g_TimeCompensator.real_prev = real_now;
        return;
    }
    ESP_LOGI(TAG, "New error rate calculated: %.2f%%", err_rate*100);

    // Update circular buffer
    g_TimeCompensator.errors[g_TimeCompensator.err_index] = err_rate;
    g_TimeCompensator.err_index = (g_TimeCompensator.err_index + 1) % MAX_HISTORY;
    if(g_TimeCompensator.err_count < MAX_HISTORY) g_TimeCompensator.err_count++;

    g_TimeCompensator.total_count++;
    if((g_TimeCompensator.total_count % WRITE_CFG_CNT) == 0){
        int32_t w_rate = (int32_t)(get_smoothed_error() * 10000);
        cfg_set_time_err_rate(w_rate);
        ESP_LOGI(TAG, "write cfg rate: %.2f%%", (float)(w_rate/100));
    }
    // Update time references
    g_TimeCompensator.real_prev = real_now;
}

/**
 * @brief Calculate the time compensation value based on historical error
 * @param interval The nominal sleep interval in seconds
 * @return The calculated compensation value in seconds (positive means system is slow, negative means fast)
 */
static int calculate_compensation(time_t interval)
{
    float err = get_smoothed_error();
    
    // When the sleep time is too long, manually compensate for the error to make the system wake up later.
    if(interval > (5 * 3600)){
        err -= 0.001f;
    }
    // Calculate raw compensation value (in seconds)
    float compensation = interval * err;  // err = (real_delta - sys_delta)/sys_delta
    
    // Apply safety bounds (adjust these values as needed)
    const float MAX_COMPENSATION = interval * 0.3f; // Limit to ±30% of interval
    if (compensation > MAX_COMPENSATION) {
        compensation = MAX_COMPENSATION;
        ESP_LOGI(TAG, "Compensation clamped to +%.1fs (upper bound)", MAX_COMPENSATION);
    } else if (compensation < -MAX_COMPENSATION) {
        compensation = -MAX_COMPENSATION;
        ESP_LOGI(TAG, "Compensation clamped to -%.1fs (lower bound)", MAX_COMPENSATION);
    }

    // Round to nearest second
    int final_compensation = (int)(compensation + (compensation > 0 ? 0.5f : -0.5f));
    

    ESP_LOGI(TAG, "Compensation calc: nominal=%lld, err=%.3f%%, comp=%+.1fs (%+ds)", 
             interval, err*100, compensation, final_compensation);
    
    return final_compensation;
}

/**
 * @brief Adjusts the system time at boot based on the predicted drift since the last recorded time.
 */
void time_compensation_boot() 
{
    time_t now = time(NULL);

    if(now <= g_TimeCompensator.real_prev || g_TimeCompensator.real_prev == 0)
        return;

    int predicted_drift = calculate_compensation(now - g_TimeCompensator.real_prev);
    time_t adjusted_time = now + predicted_drift;

    ESP_LOGI(TAG, "Boot time adjustment: sys=%lld, pred=%lld (drift=%ds)",
                now, adjusted_time, predicted_drift);
    
    struct timeval tv = { .tv_sec = adjusted_time };
    settimeofday(&tv, NULL);
    ESP_LOGI(TAG, "System time adjusted by %+lld seconds",adjusted_time - now);

}

int time_compensation(time_t time_sec) 
{
    if(time_sec <= g_TimeCompensator.real_prev || g_TimeCompensator.real_prev == 0)
        return 0;

    int predicted_drift = calculate_compensation(time_sec - g_TimeCompensator.real_prev);

    ESP_LOGI(TAG, "compensation drift=%ds", predicted_drift);
    return predicted_drift;
}
/**
 * Find the most recent time interval for scheduled wakeups
 * @param timedCount Number of scheduled time nodes
 * @param timedNodes Array of scheduled time configurations
 * @return Seconds until next scheduled wakeup
 */
static uint32_t find_most_recent_time_interval(uint8_t timedCount, timedCapNode_t *timedNodes)
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
 * Calculate next wakeup time in seconds
 * @return Seconds until next wakeup
 */
uint32_t calc_wakeup_time_seconds()
{
    capAttr_t capture;
    timedCapNode_t scheTimeNode;
    uint32_t sche_wakeup_sec = 0;
    uint32_t cfg_wakeup_sec = 0;
    time_t lastCapTime = sleep_get_last_capture_time();
    time_t now = time(NULL);

    scheTimeNode.day = 7;
    cfg_get_schedule_time(scheTimeNode.time);
    cfg_get_cap_attr(&capture);
    
    if (capture.bScheCap == 0) {
        // Schedule mode disabled - only check for scheduled tasks
        cfg_wakeup_sec = 0;
        sleep_set_wakeup_todo(WAKEUP_TODO_SCHEDULE);
        return find_most_recent_time_interval(1, &scheTimeNode);
    } else if (capture.scheCapMode == 1) {
        // Interval-based capture mode
        if (capture.intervalValue == 0) {
            cfg_wakeup_sec = 0; // No interval set
        } else {
            // Convert interval to seconds based on unit
            if (capture.intervalUnit == 0) { // Minutes
                cfg_wakeup_sec = capture.intervalValue * 60;
            } else if (capture.intervalUnit == 1) { // Hours
                cfg_wakeup_sec = capture.intervalValue * 60 * 60;
            } else if (capture.intervalUnit == 2) { // Days
                cfg_wakeup_sec = capture.intervalValue * 60 * 60 * 24;
            }

            // Handle missed captures
            if (lastCapTime) {
                if (now >= lastCapTime + cfg_wakeup_sec) {
                    cfg_wakeup_sec = 1; // Capture immediately if missed window
                } else {
                    cfg_wakeup_sec = lastCapTime + cfg_wakeup_sec - now; // Time until next capture
                }
            }
            
            // Force immediate capture if last snapshot failed
            if (camera_is_snapshot_fail()) {
                cfg_wakeup_sec = 1; 
            }
        }
    } else if (capture.scheCapMode == 0) {
        // Time-based capture mode
        cfg_wakeup_sec = find_most_recent_time_interval(capture.timedCount, capture.timedNodes);
    }

    // Determine whether to wake for snapshot or schedule
    sche_wakeup_sec = find_most_recent_time_interval(1, &scheTimeNode);
    if (cfg_wakeup_sec == 0 || sche_wakeup_sec < cfg_wakeup_sec) {
        sleep_set_wakeup_todo(WAKEUP_TODO_SCHEDULE);
        // Add random delay to prevent all devices waking simultaneously
        return sche_wakeup_sec + (rand() % 60); 
    } else {
        sleep_set_wakeup_todo(WAKEUP_TODO_SNAPSHOT);
        return cfg_wakeup_sec;
    }
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
    
    // Calculate and set timer wakeup
    int wakeup_time_sec = calc_wakeup_time_seconds();
    int calculate_sec;
    calculate_sec = calculate_compensation(wakeup_time_sec);
    wakeup_time_sec -= calculate_sec;
    if (wakeup_time_sec > 0) {
        esp_sleep_enable_timer_wakeup(wakeup_time_sec * uS_TO_S_FACTOR);
        misc_show_time("wake will at", now + wakeup_time_sec + calculate_sec);
        ESP_LOGI(TAG, "Enabling TIMER wakeup on %ds", wakeup_time_sec);
    }

    // Configure button wakeup
    ESP_LOGI(TAG, "Enabling EXT0 wakeup on pin GPIO%d", BTN_WAKEUP_PIN);
    rtc_gpio_pullup_en(BTN_WAKEUP_PIN);
    rtc_gpio_pulldown_dis(BTN_WAKEUP_PIN);
    esp_sleep_enable_ext0_wakeup(BTN_WAKEUP_PIN, BTN_WAKEUP_LEVEL);

#if PIR_ENABLE
    esp_sleep_enable_ext1_wakeup(BIT64(PIR_WAKEUP_PIN), PIR_IN_ACTIVE);
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
    rtc_gpio_pullup_dis(PIR_WAKEUP_PIN);
    rtc_gpio_pulldown_en(PIR_WAKEUP_PIN);
#else
    // if(capture.bAlarmInCap == true){
    //     rtc_gpio_pullup_en(ALARMIN_WAKEUP_PIN);
    //     rtc_gpio_pulldown_dis(ALARMIN_WAKEUP_PIN);
    //     esp_sleep_enable_ext1_wakeup(BIT64(ALARMIN_WAKEUP_PIN), ALARMIN_WAKEUP_LEVEL);
    // }
#endif

    mqtt_stop();
    wifi_close();
    cat1_close();
#if PIR_ENABLE
    esp_log_level_set("gpio", ESP_LOG_WARN);
    pir_init(1);
#endif
    ESP_LOGI(TAG, "Entering deep sleep");
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
            ESP_LOGI(TAG, "Wake up button");
            return WAKEUP_BUTTON;
        }
        case ESP_SLEEP_WAKEUP_EXT1: {
            uint64_t wakeup_pin_mask = esp_sleep_get_ext1_wakeup_status();
            ESP_LOGI(TAG, "Alarm in Wake up from GPIO %d", __builtin_ffsll(wakeup_pin_mask) - 1);
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
    ESP_LOGI(TAG, "sleep_get_wakeup_todo %d", g_wakeupTodo);
    return g_wakeupTodo;
}

/**
 * Set action to perform after wakeup
 * @param todo Action to perform
 */
void sleep_set_wakeup_todo(wakeupTodo_e todo)
{
    ESP_LOGI(TAG, "sleep_set_wakeup_todo %d", todo);
    g_wakeupTodo = todo;
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
 * Check if alarm input should trigger restart
 * @return 1 if should restart, 0 otherwise
 */
uint32_t sleep_is_alramin_goto_restart()
{
    return rtc_gpio_get_level(ALARMIN_WAKEUP_PIN) == ALARMIN_WAKEUP_LEVEL;
}
