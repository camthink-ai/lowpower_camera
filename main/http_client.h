#ifndef __HTTP_CLIENT_H__
#define __HTTP_CLIENT_H__

#include "mip.h"
#ifdef __cplusplus
extern "C" {
#endif

/**
 * Structure for OTA update package information
 */
typedef struct OTApackage {
    char fwTitle[32];
    char fwChecksum[32];
    char cfTitle[32];
    char cfChecksum[32];
} OTApackage_t;

/**
 * Check for available firmware updates
 */
void http_client_check_update();

/**
 * Synchronize device time with server
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t http_client_sync_server_time();

/**
 * Send HTTP request
 * @param http HTTP request structure
 * @return 0 on success, -1 on failure
 */
int8_t http_client_send_req(http_t *http);

/**
 * Download file via HTTP
 * @param url File URL to download
 * @param filename Local filename to save
 * @param timeout Operation timeout in seconds
 * @param filesize Expected file size
 * @param md5 Expected MD5 checksum
 * @param crc32 Expected CRC32 checksum
 * @return 0 on success, -1 on failure
 */
int8_t http_client_download_file(const char *url, const char *filename, int timeout, int filesize, const char *md5,
                              const char *crc32);

/**
 * Upload file via HTTP
 * @param url Target URL for upload
 * @param filename Local file to upload
 * @param timeout Operation timeout in seconds
 * @return 0 on success, -1 on failure
 */
int8_t http_client_upload_file(const char *url, const char *filename, int timeout);

#ifdef __cplusplus
}
#endif

#endif /* __HTTP_CLIENT_H__ */
