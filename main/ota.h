#ifndef __OTA_H__
#define __OTA_H__

#include "esp_ota_ops.h"
#include "esp_flash_partitions.h"
#include "esp_partition.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum size for OTA binary (2.5MB) */
#define OTA_BIN_MAX_SIZE 0x280000
/* Maximum size for OTA configuration (64KB) */ 
#define OTA_CFG_MAX_SIZE 0x10000

/**
 * @brief OTA update handle structure
 * @param update_handle ESP OTA update handle
 * @param update_partition Pointer to partition being updated
 */
typedef struct otaHandle {
    esp_ota_handle_t update_handle;
    const esp_partition_t *update_partition;
} otaHandle_t;

/**
 * @brief Verify OTA image header
 * @param header_data Pointer to OTA header data
 * @param header_size Size of header data
 * @param ota_size Total size of OTA image
 * @return ESP_OK if valid, error code otherwise
 */
esp_err_t ota_vertify(char *header_data, size_t header_size, size_t ota_size);

/**
 * @brief Start OTA update process
 * @param handle Pointer to OTA handle structure
 * @param size Total size of OTA image
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t ota_start(otaHandle_t *handle, size_t size);

/**
 * @brief Write OTA data chunk
 * @param handle Pointer to OTA handle structure
 * @param data Pointer to data to write
 * @param size Size of data to write
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t ota_run(otaHandle_t *handle, void *data, size_t size);

/**
 * @brief Finalize OTA update
 * @param handle Pointer to OTA handle structure
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t ota_stop(otaHandle_t *handle);

/**
 * @brief Perform complete OTA update
 * @param data Pointer to OTA image data
 * @param size Size of OTA image
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t ota_update(void *data, size_t size);

#ifdef __cplusplus
}
#endif


#endif /* __OTA_H__ */
