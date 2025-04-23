#ifndef __DEBUG_H__
#define __DEBUG_H__

#include "esp_console.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Open debug console interface
 */
void debug_open(void);

/**
 * Initialize debug subsystem
 */
void debug_init(void);

/**
 * Add debug commands to console
 * @param cmd Array of console commands
 * @param count Number of commands in array
 */
void debug_cmd_add(esp_console_cmd_t *cmd, uint32_t count);

#ifdef __cplusplus
}
#endif


#endif /* __DEBUG_H__ */
