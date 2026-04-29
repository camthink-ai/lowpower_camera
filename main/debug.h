#ifndef __DEBUG_H__
#define __DEBUG_H__

#include "esp_idf_version.h"
#include "esp_console.h"

/* Helper macro for esp_console_cmd_t initialization with version compatibility */
/* Note: esp_console_cmd_t field layout changes across ESP-IDF versions */
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 0)
#define ESP_CONSOLE_CMD_INIT(cmd, help, hint, func, argtable) \
    {cmd, help, hint, func, NULL, argtable, NULL}
#elif ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 1, 0)
#define ESP_CONSOLE_CMD_INIT(cmd, help, hint, func, argtable) \
    {cmd, help, hint, func, argtable}
#else
#define ESP_CONSOLE_CMD_INIT(cmd, help, hint, func, argtable) \
    {cmd, help, hint, func, argtable}
#endif

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
