/*
 * Camera UVC Controls - Production ready header
 */

#ifndef CAMERA_UVC_CONTROLS_H_
#define CAMERA_UVC_CONTROLS_H_

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Camera capabilities structure
 */
typedef struct {
    bool hdr_supported;
    bool auto_exposure_supported;
    bool brightness_supported;
    bool contrast_supported;
    bool saturation_supported;
    bool auto_focus_supported;
} camera_uvc_capabilities_t;

/**
 * @brief Detect which controls are supported by the camera
 * 
 * Call this once after camera initialization to discover capabilities.
 * 
 * @return ESP_OK on success
 */
esp_err_t camera_uvc_detect_capabilities(void);

/**
 * @brief Check if HDR is supported
 * 
 * @return true if HDR/Backlight Compensation is supported
 */
bool camera_uvc_is_hdr_supported(void);

/**
 * @brief Set HDR level
 * 
 * @param level HDR level (0=off, 1=low, 2=medium, 3=high)
 *              Actual range depends on camera hardware
 * @return ESP_OK on success
 *         ESP_ERR_NOT_SUPPORTED if HDR not supported
 */
esp_err_t camera_uvc_set_hdr(uint16_t level);

/**
 * @brief Enable/disable auto exposure
 * 
 * @param enable true to enable auto exposure, false for manual
 * @return ESP_OK on success
 *         ESP_ERR_NOT_SUPPORTED if not supported
 */
esp_err_t camera_uvc_set_auto_exposure(bool enable);

/**
 * @brief Set brightness
 * 
 * @param brightness Brightness value (typical range: 0-255)
 * @return ESP_OK on success
 *         ESP_ERR_NOT_SUPPORTED if not supported
 */
esp_err_t camera_uvc_set_brightness(int16_t brightness);

/**
 * @brief Set contrast
 * 
 * @param contrast Contrast value
 * @return ESP_OK on success
 *         ESP_ERR_NOT_SUPPORTED if not supported
 */
esp_err_t camera_uvc_set_contrast(uint16_t contrast);

/**
 * @brief Set saturation
 * 
 * @param saturation Saturation value
 * @return ESP_OK on success
 *         ESP_ERR_NOT_SUPPORTED if not supported
 */
esp_err_t camera_uvc_set_saturation(uint16_t saturation);

/**
 * @brief Apply optimal camera settings for indoor use
 * 
 * Applies preset values for:
 * - Auto exposure: enabled
 * - HDR: medium level
 * - Brightness: increased
 * - Saturation: enhanced
 * 
 * @return ESP_OK on success
 */
esp_err_t camera_uvc_apply_indoor_settings(void);

/**
 * @brief Apply optimal camera settings for outdoor use
 * 
 * Applies preset values for:
 * - Auto exposure: enabled
 * - HDR: high level (for bright sunlight)
 * - Brightness: normal
 * - Saturation: slightly reduced
 * 
 * @return ESP_OK on success
 */
esp_err_t camera_uvc_apply_outdoor_settings(void);

/**
 * @brief Apply optimal camera settings for low-light conditions
 * 
 * Applies preset values for:
 * - Auto exposure: enabled
 * - HDR: enabled
 * - Brightness: significantly increased
 * - Contrast: increased
 * 
 * @return ESP_OK on success
 */
esp_err_t camera_uvc_apply_lowlight_settings(void);

/**
 * @brief Get current camera capabilities
 * 
 * @return Pointer to capabilities structure (static, do not free)
 */
const camera_uvc_capabilities_t* camera_uvc_get_capabilities(void);

#endif /* CAMERA_UVC_CONTROLS_H_ */




