/**
 * Session log: mirror ESP-IDF log (esp_log_*) to LittleFS.
 * Keeps the last SESSION_LOG_BOOT_SLOTS boot sessions as session_log_0.txt (newest) .. _{N-1}.txt (oldest).
 */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "storage.h"
#include "session_log.h"

#define TAG_SESSION_LOG "session_log"

#define SESSION_LOG_SLOT_CURRENT 0

static FILE *s_log_fp;
static SemaphoreHandle_t s_log_mutex;
static vprintf_like_t s_prev_vprintf;
static bool s_inited;

static void session_log_build_path(char *buf, size_t buflen, int slot)
{
    snprintf(buf, buflen, "%s/session_log_%d.txt", STORAGE_ROOT, slot);
}

/**
 * Rotate files before a new boot log: drop oldest, shift others, free slot 0 for this boot.
 * session_log_0 -> _1, _1 -> _2, delete previous _2, then open new _0.
 */
/** First line of each boot log file: wall-clock date/time (may be unset before NTP). */
static void session_log_write_boot_first_line(void)
{
    time_t ts = time(NULL);
    char tbuf[48];
    struct tm lt;

    if (localtime_r(&ts, &lt) != NULL) {
        strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S", &lt);
    } else {
        strncpy(tbuf, "(time error)", sizeof(tbuf) - 1);
        tbuf[sizeof(tbuf) - 1] = '\0';
    }

    fprintf(s_log_fp, "===== boot log start: %s =====\n", tbuf);
    fflush(s_log_fp);
}

static void session_log_rotate_boot_files(void)
{
    char path_old[72];
    char path_new[72];
    const int last = SESSION_LOG_BOOT_SLOTS - 1;

    session_log_build_path(path_old, sizeof(path_old), last);
    if (unlink(path_old) != 0 && errno != ENOENT) {
        ESP_LOGW(TAG_SESSION_LOG, "unlink %s: %d", path_old, errno);
    }

    for (int i = last - 1; i >= 0; i--) {
        session_log_build_path(path_old, sizeof(path_old), i);
        session_log_build_path(path_new, sizeof(path_new), i + 1);
        if (rename(path_old, path_new) != 0 && errno != ENOENT) {
            ESP_LOGW(TAG_SESSION_LOG, "rename %s -> %s: %d", path_old, path_new, errno);
        }
    }
}

static int session_log_vprintf(const char *fmt, va_list args)
{
    va_list copy;
    int ret;

    va_copy(copy, args);
    ret = s_prev_vprintf ? s_prev_vprintf(fmt, copy) : vprintf(fmt, copy);
    va_end(copy);

    if (s_log_fp && s_log_mutex) {
        if (xSemaphoreTakeRecursive(s_log_mutex, portMAX_DELAY) == pdTRUE) {
            va_copy(copy, args);
            (void)vfprintf(s_log_fp, fmt, copy);
            va_end(copy);
            (void)fflush(s_log_fp);
            xSemaphoreGiveRecursive(s_log_mutex);
        }
    }
    return ret;
}

void session_log_init(void)
{
    char path[72];

    if (s_inited) {
        return;
    }
    s_inited = true;

    s_log_mutex = xSemaphoreCreateRecursiveMutex();
    if (!s_log_mutex) {
        ESP_LOGE(TAG_SESSION_LOG, "mutex create failed");
        return;
    }

    session_log_rotate_boot_files();
    session_log_build_path(path, sizeof(path), SESSION_LOG_SLOT_CURRENT);
    s_log_fp = fopen(path, "w");
    if (!s_log_fp) {
        ESP_LOGE(TAG_SESSION_LOG, "open %s failed", path);
        return;
    }

    session_log_write_boot_first_line();

    s_prev_vprintf = esp_log_set_vprintf(session_log_vprintf);
    ESP_LOGI(TAG_SESSION_LOG,
             "Session log: %s (keeping last %d boots, slot 0 = this boot)",
             path, SESSION_LOG_BOOT_SLOTS);
}

void session_log_flush(void)
{
    if (!s_log_fp || !s_log_mutex) {
        return;
    }
    if (xSemaphoreTakeRecursive(s_log_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        fflush(s_log_fp);
        int fd = fileno(s_log_fp);
        if (fd >= 0) {
            (void)fsync(fd);
        }
        xSemaphoreGiveRecursive(s_log_mutex);
    }
}

bool session_log_pause_for_stat(void)
{
    if (!s_log_mutex) {
        return false;
    }
    if (xSemaphoreTakeRecursive(s_log_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        return false;
    }

    if (s_log_fp) {
        fflush(s_log_fp);
        int fd = fileno(s_log_fp);
        if (fd >= 0) {
            (void)fsync(fd);
        }
        fclose(s_log_fp);
        s_log_fp = NULL;
    }
    return true; /* mutex still held */
}

void session_log_resume_after_stat(bool paused)
{
    if (!paused) {
        return;
    }
    /* Reopen slot0 for this boot and continue mirroring logs. */
    char path[72];
    session_log_build_path(path, sizeof(path), SESSION_LOG_SLOT_CURRENT);
    s_log_fp = fopen(path, "a");
    if (!s_log_fp) {
        ESP_LOGE(TAG_SESSION_LOG, "resume open %s failed", path);
    }
    xSemaphoreGiveRecursive(s_log_mutex);
}

void session_log_close_for_sleep(void)
{
    if (!s_log_mutex) {
        return;
    }
    if (xSemaphoreTakeRecursive(s_log_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        return;
    }
    if (s_log_fp) {
        fflush(s_log_fp);
        int fd = fileno(s_log_fp);
        if (fd >= 0) {
            (void)fsync(fd);
        }
        fclose(s_log_fp);
        s_log_fp = NULL;
    }
    xSemaphoreGiveRecursive(s_log_mutex);
}

/** Send file contents as chunks; returns false on HTTP send failure */
static bool session_log_send_file_chunks(httpd_req_t *req, const char *path, esp_err_t *res_out)
{
    char buf[512];
    size_t n;
    FILE *rd = fopen(path, "r");
    if (!rd) {
        return true; /* missing file is OK when merging slots */
    }
    while ((n = fread(buf, 1, sizeof(buf), rd)) > 0) {
        if (httpd_resp_send_chunk(req, buf, n) != ESP_OK) {
            *res_out = ESP_FAIL;
            fclose(rd);
            return false;
        }
    }
    fclose(rd);
    return true;
}

esp_err_t session_log_http_export(httpd_req_t *req)
{
    char path[72];
    esp_err_t res = ESP_OK;
    static const char sep[] =
        "\r\n\r\n---------- boot log session_log_%d.txt (older -> newer) ----------\r\n\r\n";
    bool exists[SESSION_LOG_BOOT_SLOTS];

    if (!s_log_mutex) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        httpd_resp_set_type(req, "text/plain");
        return httpd_resp_sendstr(req, "Session logging disabled.");
    }

    session_log_flush();

    if (xSemaphoreTakeRecursive(s_log_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        httpd_resp_set_type(req, "text/plain");
        return httpd_resp_sendstr(req, "Log busy, retry.");
    }

    if (s_log_fp) {
        fflush(s_log_fp);
        int fd = fileno(s_log_fp);
        if (fd >= 0) {
            (void)fsync(fd);
        }
        fclose(s_log_fp);
        s_log_fp = NULL;
    }

    bool any = false;
    for (int slot = 0; slot < SESSION_LOG_BOOT_SLOTS; slot++) {
        session_log_build_path(path, sizeof(path), slot);
        FILE *probe = fopen(path, "r");
        exists[slot] = (probe != NULL);
        if (probe) {
            fclose(probe);
            any = true;
        }
    }

    if (!any) {
        session_log_build_path(path, sizeof(path), SESSION_LOG_SLOT_CURRENT);
        s_log_fp = fopen(path, "a");
        xSemaphoreGiveRecursive(s_log_mutex);
        httpd_resp_set_status(req, "404 Not Found");
        httpd_resp_set_type(req, "text/plain");
        return httpd_resp_sendstr(req, "No session log files.");
    }

    char disp_hdr[80];
    snprintf(disp_hdr, sizeof(disp_hdr),
             "attachment; filename=\"session_logs_last%d.txt\"", SESSION_LOG_BOOT_SLOTS);

    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    httpd_resp_set_hdr(req, "Content-Disposition", disp_hdr);
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    bool first_out = true;
    /* Oldest retained first -> newest (slot 0), chronological */
    for (int slot = SESSION_LOG_BOOT_SLOTS - 1; slot >= 0; slot--) {
        if (!exists[slot]) {
            continue;
        }
        session_log_build_path(path, sizeof(path), slot);
        if (!first_out) {
            char hdr[96];
            int hlen = snprintf(hdr, sizeof(hdr), sep, slot);
            if (hlen > 0 && hlen < (int)sizeof(hdr) &&
                httpd_resp_send_chunk(req, hdr, (size_t)hlen) != ESP_OK) {
                res = ESP_FAIL;
                goto reopen;
            }
        }
        first_out = false;
        if (!session_log_send_file_chunks(req, path, &res)) {
            goto reopen;
        }
    }

reopen:
    session_log_build_path(path, sizeof(path), SESSION_LOG_SLOT_CURRENT);
    s_log_fp = fopen(path, "a");
    if (!s_log_fp) {
        ESP_LOGE(TAG_SESSION_LOG, "reopen %s for append failed", path);
    }

    if (httpd_resp_send_chunk(req, NULL, 0) != ESP_OK) {
        res = ESP_FAIL;
    }
    xSemaphoreGiveRecursive(s_log_mutex);
    return res;
}
