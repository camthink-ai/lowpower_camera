#ifndef __WEBHOOK_H__
#define __WEBHOOK_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize webhook module
 */
void webhook_open(void);

/**
 * Deinitialize webhook module, release resources
 */
void webhook_close(void);

/**
 * Start webhook module
 */
void webhook_start(void);

/**
 * Stop webhook module
 */
void webhook_stop(void);

/**
 * Publish JSON payload to configured webhook URL via HTTP POST
 * @param json_str Null-terminated JSON string to send
 * @return 0 on success (HTTP 2xx), -1 on failure
 */
int8_t webhook_publish(const char *json_str);

#ifdef __cplusplus
}
#endif

#endif /* __WEBHOOK_H__ */
