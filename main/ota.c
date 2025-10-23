/*
 * SPDX-FileCopyrightText: 2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
/* HTTP File Server Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include <string.h>
#include "esp_err.h"
#include "esp_log.h"
#include "config.h"
#include "system.h"
#include "storage.h"
#include "ota.h"

#include "esp_app_format.h"

#define TAG "-->OTA"


/* Max size of an individual file. Make sure this
 * value is same as that set in upload_script.html */
#define MAX_FILE_SIZE   (2048*1024) // 2048 KB
#define MAX_FILE_SIZE_STR "2048KB"

/**
 * @brief Get hardware version number from version string
 * @param version Version string in format like "NE_101.2.0.2"
 * @return Hardware version number (number after first dot):
 */
static int get_hardware_version(const char *version)
{
    char str[32] = {0};
    strncpy(str, version, sizeof(str) - 1);

    char delimiters[] = ".-";
    char *saveptr;
    char *p = strtok_r(str, delimiters, &saveptr);
    int field = 0;
    while (p) {
        switch (field) {
            case 1: {
                return atoi(p);
            }
            default:
                break;
        }
        p = strtok_r(NULL, delimiters, &saveptr);
        field++;
    }
    ESP_LOGE(TAG, "get_hardware_version failed, version: %s\n", version);
    return 0;
}

/**
 * @brief Verify OTA image header and version compatibility
 * @param header_data Pointer to OTA image header data
 * @param header_size Size of header data
 * @param ota_size Total size of OTA image
 * @return ESP_OK if valid and compatible, ESP_FAIL otherwise
 */
esp_err_t ota_vertify(char *header_data, size_t header_size, size_t ota_size)
{
    esp_app_desc_t new_app_info;
    if (header_size > sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t)) {
        // check current version with downloading
        memcpy(&new_app_info, &header_data[sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t)],
               sizeof(esp_app_desc_t));
        ESP_LOGI(TAG, "New firmware version: %s", new_app_info.version);

        esp_app_desc_t running_app_info;
        const esp_partition_t *running = esp_ota_get_running_partition();
        if (esp_ota_get_partition_description(running, &running_app_info) == ESP_OK) {
            ESP_LOGI(TAG, "Running firmware version: %s", running_app_info.version);
        }

        const esp_partition_t *last_invalid_app = esp_ota_get_last_invalid_partition();
        esp_app_desc_t invalid_app_info;
        if (esp_ota_get_partition_description(last_invalid_app, &invalid_app_info) == ESP_OK) {
            ESP_LOGI(TAG, "Last invalid firmware version: %s", invalid_app_info.version);
        }

        // check current version with last invalid partition
        // if (last_invalid_app != NULL) {
        //     if (memcmp(invalid_app_info.version, new_app_info.version, sizeof(new_app_info.version)) == 0) {
        //         ESP_LOGW(TAG, "New version is the same as invalid version.");
        //         ESP_LOGW(TAG, "Previously, there was an attempt to launch the firmware with %s version, but it failed.",
        //                  invalid_app_info.version);
        //         ESP_LOGW(TAG, "The firmware has been rolled back to the previous version.");
        //         return ESP_FAIL;
        //     }
        // }
        // if (memcmp(new_app_info.version, running_app_info.version, sizeof(new_app_info.version)) == 0) {
        //     ESP_LOGW(TAG, "Current running version is the same as a new. We will not continue the update.");
        //     return ESP_FAIL;
        // }
        // // 判断硬件版本号
        // int running_hardware_version = get_hardware_version(running_app_info.version);
        // int new_hardware_version = get_hardware_version(new_app_info.version);
        // if (running_hardware_version != new_hardware_version) {
        //     ESP_LOGW(TAG, "Current running hardware version is %d, new hardware version is %d. We will not continue the update.",
        //              running_hardware_version, new_hardware_version);
        //     return ESP_FAIL;
        // }

        // Check if the version string starts with "NE_101"
        if (strncmp(new_app_info.version, "NE_101", 6) != 0) {
            ESP_LOGE(TAG, "get_hardware_version failed, version does not start with NE_101: %s\n", new_app_info.version);
            return ESP_FAIL;
        }
    }

    return ESP_OK;
}

/**
 * @brief Initialize OTA update process
 * @param handle Pointer to OTA handle structure to initialize
 * @param size Total size of OTA image
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t ota_start(otaHandle_t *handle, size_t size)
{
    handle->update_partition = esp_ota_get_next_update_partition(NULL);
    assert(handle->update_partition != NULL);
    ESP_LOGI(TAG, "Writing to partition subtype %d at offset 0x%lx, size %lu",
             handle->update_partition->subtype, handle->update_partition->address,
             handle->update_partition->size);
    esp_err_t err = esp_ota_begin(handle->update_partition, size, &handle->update_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed (%s)", esp_err_to_name(err));
        esp_ota_abort(handle->update_handle);
    }
    return err;
}

/**
 * @brief Write chunk of OTA data
 * @param handle Initialized OTA handle
 * @param data Pointer to data to write
 * @param size Size of data to write
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t ota_run(otaHandle_t *handle, void *data, size_t size)
{
    esp_err_t err = esp_ota_write(handle->update_handle, (const void *)data, size);
    if (err != ESP_OK) {
        esp_ota_abort(handle->update_handle);
        ESP_LOGE(TAG, "ota_run failed (%s)!", esp_err_to_name(err));
    }
    return err;
}

/**
 * @brief Finalize OTA update and set boot partition
 * @param handle Initialized OTA handle
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t ota_stop(otaHandle_t *handle)
{
    esp_err_t err = esp_ota_end(handle->update_handle);
    if (err != ESP_OK) {
        if (err == ESP_ERR_OTA_VALIDATE_FAILED) {
            ESP_LOGE(TAG, "Image validation failed, image is corrupted");
        } else {
            ESP_LOGE(TAG, "esp_ota_end failed (%s)!", esp_err_to_name(err));
        }
        return err;
    }
    err = esp_ota_set_boot_partition(handle->update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed (%s)!", esp_err_to_name(err));
    }
    return err;
}

/**
 * @brief Perform complete OTA update in one operation
 * @param data Pointer to complete OTA image data
 * @param size Size of OTA image
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t ota_update(void *data, size_t size)
{
    otaHandle_t handle;

    if (ota_vertify(data, size, size) != ESP_OK) {
        return ESP_FAIL;
    }
    if (ota_start(&handle, size) != ESP_OK) {
        return ESP_FAIL;
    }
    if (ota_run(&handle, data, size) != ESP_OK) {
        return ESP_FAIL;
    }
    if (ota_stop(&handle) != ESP_OK) {
        return ESP_FAIL;
    }
    return ESP_OK;
}
