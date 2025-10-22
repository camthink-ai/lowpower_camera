#ifndef __SLEEP_H__
#define __SLEEP_H__

#include <time.h>
#include "system.h"

#ifdef __cplusplus
extern "C" {
#endif

// Wakeup pin configurations
#define BTN_WAKEUP_PIN  BUTTON_IO        // Button wakeup pin
#define BTN_WAKEUP_LEVEL BUTTON_ACTIVE   // Active level for button wakeup
#define ALARMIN_WAKEUP_PIN  ALARM_IN_IO  // Alarm input wakeup pin
#define ALARMIN_WAKEUP_LEVEL ALARM_IN_ACTIVE // Active level for alarm wakeup
#define PIR_WAKEUP_PIN  PIR_INTDOUT_IO
#define PIR_WAKEUP_LEVEL PIR_IN_ACTIVE

/**
 * Sleep event bits for synchronization
 */
typedef enum sleepBits {
    SLEEP_SNAPSHOT_STOP_BIT = BIT(0),          // Snapshot complete
    SLEEP_STORAGE_UPLOAD_STOP_BIT =  BIT(1),   // Storage upload complete
    SLEEP_NO_OPERATION_TIMEOUT_BIT = BIT(2),   // No operation timeout
    SLEEP_SCHEDULE_DONE_BIT = BIT(3),          // Scheduled tasks complete
    SLEEP_MIP_DONE_BIT = BIT(4),               // MIP operations complete
} sleepBits_e;

/**
 * Wakeup source types
 */
typedef enum wakeupType {
    WAKEUP_UNDEFINED = 0,  // Unknown wakeup source
    WAKEUP_BUTTON,         // Button press wakeup
    WAKEUP_ALARMIN,        // Alarm input wakeup
    WAKEUP_TIMER,          // Timer wakeup
} wakeupType_e;

/**
 * Actions to perform after wakeup
 */
typedef enum wakeupTodo {
    WAKEUP_TODO_NOTHING = 0,   // No specific action
    WAKEUP_TODO_SNAPSHOT,      // Take snapshot
    WAKEUP_TODO_CONFIG,        // Enter config mode
    WAKEUP_TODO_SCHEDULE,      // Perform scheduled tasks
    WAKEUP_TODO_UPLOAD,        // Perform upload tasks
} wakeupTodo_e;

/* Initialize compensation controller */
void comp_init();

/* Process time synchronization event
 * @param real_now Actual real time from reliable source
 * @param sys_now  Current system time 
 */
void record_time_sync(time_t real_now, time_t sys_now);

/**
 * @brief Adjusts the system time at boot based on the predicted drift since the last recorded time.
 */
void time_compensation_boot();

/**
 * @brief Compensates the current time based on the synchronization time.
 */
int time_compensation(time_t time_sec);

/**
 * Calculate next wakeup time in seconds
 * @param bUpdateWakeupTodo Whether to update the wakeup todo list
 * @return Seconds until next wakeup
 */
uint32_t calc_wakeup_time_seconds(bool bUpdateWakeupTodo);

/**
 * Calculate next snapshot time
 * @return Seconds until next snapshot
 */
uint32_t calc_next_snapshot_time();

/**
 * Get the wakeup source that triggered system startup
 * @return Wakeup type
 */
wakeupType_e sleep_wakeup_case();

/**
 * Initialize sleep module
 */
void sleep_open();

/**
 * Wait for specified sleep event bits
 * @param bits Event bits to wait for
 * @param bWaitAll True to wait for all bits, false for any bit
 */
void sleep_wait_event_bits(sleepBits_e bits, bool bWaitAll);

/**
 * Set sleep event bits
 * @param bits Event bits to set
 */
void sleep_set_event_bits(sleepBits_e bits);

/**
 * Clear sleep event bits
 * @param bits Event bits to clear
 */
void sleep_clear_event_bits(sleepBits_e bits);

/**
 * Enter sleep mode
 */
void sleep_start();

/**
 * Get action to perform after wakeup
 * @return Wakeup action
 */
wakeupTodo_e sleep_get_wakeup_todo();

/**
 * @param todo Wakeup action
 * @param priority Priority of the action, 0 is the highest priority, 7 is the lowest priority
 */
void sleep_set_wakeup_todo(wakeupTodo_e todo, uint8_t priority);

/**
 * Clear action to perform after wakeup
 * @param priority Priority of the action, 0 is the highest priority, 7 is the lowest priority
 */
void sleep_clear_wakeup_todo(uint8_t priority);

/**
 * Check if there is any action to perform after wakeup
 * @return true if there is any action to perform, false otherwise
 */
bool sleep_has_wakeup_todo();

/**
 * Reset wakeup todo list
 */
void sleep_reset_wakeup_todo();

/**
 * Set timestamp of last capture
 * @param time Timestamp to set
 */
void sleep_set_last_capture_time(time_t time);

/**
 * Get timestamp of last capture
 * @return Last capture timestamp
 */
time_t sleep_get_last_capture_time(void);

/**
 * Set timestamp of last upload
 * @param time Timestamp to set
 */
void sleep_set_last_upload_time(time_t time);

/**
 * Get timestamp of last upload
 * @return Last upload timestamp
 */
time_t sleep_get_last_upload_time(void);


/**
 * Check if alarm input should trigger restart
 * @return 1 if should restart, 0 otherwise
 */
uint32_t sleep_is_alramin_goto_restart();

#ifdef __cplusplus
}
#endif

#endif /* __SLEEP_H__ */
