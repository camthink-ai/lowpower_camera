#ifndef SESSION_LOG_H
#define SESSION_LOG_H

#include "esp_err.h"
#include "esp_http_server.h"

/** Number of boot sessions to keep on LittleFS (session_log_0 = current ... _{N-1} = oldest). */
#ifndef SESSION_LOG_BOOT_SLOTS
#define SESSION_LOG_BOOT_SLOTS 5
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Rotate session_log_{0..N-1}.txt (drop oldest), then open session_log_0.txt for this boot
 * and mirror ESP_LOG output to it (and UART).
 */
void session_log_init(void);

/** Flush current session log file to flash (call before deep sleep / power-down paths). */
void session_log_flush(void);

/**
 * Pause writer for file stat/read:
 * - takes the internal mutex
 * - flush+fsync+closes current writer (slot0)
 * Mutex is kept held until resume.
 * @return true if paused successfully (mutex held)
 */
bool session_log_pause_for_stat(void);

/**
 * Resume writer after session_log_pause_for_stat().
 * Reopen slot0 with "a" and release mutex.
 */
void session_log_resume_after_stat(bool paused);

/**
 * Close writer before deep sleep (flush+fsync+fclose). No reopen.
 */
void session_log_close_for_sleep(void);

/**
 * HTTP GET: send merged logs (oldest boot -> newest) as text/plain attachment (chunked).
 */
esp_err_t session_log_http_export(httpd_req_t *req);

#ifdef __cplusplus
}
#endif

#endif /* SESSION_LOG_H */
