#ifndef UVC_H_
#define UVC_H_

#include "esp_err.h"
#include "usb_stream.h"
#include "esp_camera.h"

#define USB_POWER_IO (3)

/**
 * @brief Initialize UVC subsystem
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t uvc_init(void);

/**
 * @brief Deinitialize UVC subsystem
 */
void uvc_deinit(void);

/**
 * @brief Get frame buffer for streaming
 * @return Pointer to frame buffer structure
 */
camera_fb_t *uvc_stream_fb_get();

/**
 * @brief Return frame buffer after processing
 * @param fb Pointer to frame buffer structure
 */
void uvc_camera_fb_return(camera_fb_t *fb);

#endif