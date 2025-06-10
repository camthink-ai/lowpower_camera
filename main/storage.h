#ifndef __STORAGE_H__
#define __STORAGE_H__

#include "system.h"

#ifdef __cplusplus
extern "C" {
#endif

#define STORAGE_ROOT "/littlefs"
#define STORAGE_PART "littlefs"

/**
 * Open storage with specified input and output queues
 * @param in Input queue handle
 * @param out Output queue handle
 */
void storage_open(QueueHandle_t in, QueueHandle_t out);

/**
 * Close the storage
 */
void storage_close();

/**
 * Check the status of the SD card
 */
void storage_sd_check(void);

/**
 * Display the files in the storage
 */
void storage_show_file();

/**
 * Start the upload process to storage
 */
void storage_upload_start();

/**
 * Stop the upload process to storage
 */
void storage_upload_stop();

/**
 * Format the storage
 */
void storage_format();

#ifdef __cplusplus
}
#endif


#endif /* __STORAGE_H__ */
