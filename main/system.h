#ifndef __SYSTEM_H__
#define __SYSTEM_H__

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "sleep.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CAPTURE_ERROR_THRESHOLD_S 1 // The allowed capture error time value, in seconds
/**
 * System operation modes
 */
typedef enum modeSel {
    MODE_SLEEP = 0,    ///< Low-power sleep mode
    MODE_SNAPSHOT,     ///< Snapshot mode
    MODE_CONFIG,       ///< Configuration mode
    MODE_SCHEDULE,     ///< Scheduled tasks mode
    MODE_UPLOAD,       ///< Upload mode
} modeSel_e;

/**
 * System restart reasons
 */
typedef enum rstReason {
    RST_POWER_ON = 0,  ///< Power-on reset
    RST_SOFTWARE,      ///< Software-triggered reset
    RST_DEEP_SLEEP,    ///< Wake from deep sleep
} rstReason_e;

/**
 * Snapshot trigger types
 */
typedef enum snapType {
    SNAP_UNDEFINED = 'U',  ///< Undefined snapshot type
    SNAP_TIMER = 'T',      ///< Timer-triggered snapshot
    SNAP_BUTTON = 'B',     ///< Button-triggered snapshot
    SNAP_ALARMIN = 'A',    ///< Alarm-triggered snapshot
} snapType_e;

/**
 * Data source types
 */
typedef enum cameaFrom {
    FROM_CAMERA = 0,   ///< Data from camera
    FROM_STORAGE = 1,  ///< Data from storage
    FROM_UNDEFINED,    ///< Unknown data source
} cameaFrom_e;

/**
 * Node event status
 */
typedef enum nodeEvent {
    EVENT_FAIL = -1,   ///< Operation failed
    EVENT_OK = 0,      ///< Operation succeeded
} nodeEvent_e;

/**
 * Queue node structure for inter-task communication
 */
typedef struct queueNode {
    snapType_e type;           ///< Snapshot type
    cameaFrom_e from;          ///< Data source
    uint64_t pts;              ///< Timestamp in milliseconds
    void *context;             ///< Context pointer
    void (*free_handler)(struct queueNode *node, nodeEvent_e event); ///< Cleanup handler
    void *data;                ///< Data pointer
    size_t len;                ///< Data length
    char ntp_sync_flag;        ///< Check whether there is a flag for ntp synchronization. If not, the timestamp will be corrected during upload.
} queueNode_t;

/**
 * Time attributes structure
 */
typedef struct timeAttr {
    char tz[64];       ///< Timezone in POSIX format
    uint64_t ts;       ///< UTC timestamp in seconds
} timeAttr_t;

extern modeSel_e main_mode;
/**
 * Set system time
 * @param tAttr Time attributes to set
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t system_set_time(timeAttr_t *tAttr);

/**
 * System get time delta.
 */
int system_get_time_delta();

/**
 * System get ntp sync flag.
 */
int system_get_ntp_sync_flag();

/**
 * Get current system time
 * @param tAttr Output parameter for time attributes
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t system_get_time(timeAttr_t *tAttr);

/**
 * Set system timezone
 * @param tz Timezone string in POSIX format
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t system_set_timezone(const char *tz);

/**
 * Sync time with NTP server
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t system_ntp_time(void);

/**
 * Get firmware version string
 * @return Version string
 */
const char *system_get_version();

/**
 * Reset the system
 */
void system_reset();

/**
 * Restart the system
 */
void system_restart();

/**
 * Get system restart reason
 * @return Restart reason code
 */
rstReason_e system_restart_reasons();

/**
 * Print memory usage information
 */
void system_show_meminfo();

/**
 * Execute scheduled tasks
 */
void system_schedule_todo();

/**
 * Execute upload tasks
 */
void system_upload_todo();

/**
 * Add ping command to console
 */
void add_ping_cmd(void);

/**
 * Get the current system mode
 * @return modeSel_e
 */
modeSel_e system_get_mode(void);

#ifdef __cplusplus
}
#endif

#endif /* __SYSTEM_H__ */
