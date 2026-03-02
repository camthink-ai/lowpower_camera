/**
 * @file ping.h
 * @brief Ping API for network connectivity test
 */
#ifndef PING_H
#define PING_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Execute ping and return result as string
 * Blocks until ping completes or timeout.
 *
 * @param host    Target host (IP or hostname)
 * @param count   Number of ping packets (1-100, default 4)
 * @param out_buf Buffer to store ping result text
 * @param out_len Size of out_buf
 * @return ESP_OK on success
 */
esp_err_t ping_execute(const char *host, int count, char *out_buf, size_t out_len);

#ifdef __cplusplus
}
#endif

#endif /* PING_H */
