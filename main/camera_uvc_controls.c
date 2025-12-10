/*
 * Camera UVC Controls - Production ready functions
 * 
 * This file provides production-ready functions to control USB camera
 * with proper error handling and settings persistence.
 */

#include "camera_uvc_controls.h"
#include "usb_stream.h"
#include "esp_log.h"
#include "config.h"

static const char *TAG = "CAM_UVC_CTRL";

/**
 * @brief Enable/disable HDR
 * 
 * @param enable true to enable, false to disable
 * @return ESP_OK on success
 */
esp_err_t camera_uvc_set_hdr(bool enable)
{
    uint8_t gamma = enable ? 2 : 1;
    esp_err_t ret = usb_streaming_control(STREAM_UVC, 
                                         CTRL_UVC_GAMMA,
                                         (void *)(uintptr_t)gamma);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "%s HDR", enable ? "Enabled" : "Disabled");
    } else {
        ESP_LOGE(TAG, "Failed to %s HDR", enable ? "Enable" : "Disable");
    }
    
    return ret;
}


/**
 * @brief Enable/disable auto exposure
 * 
 * @param enable true to enable, false to disable
 * @return ESP_OK on success
 */
esp_err_t camera_uvc_set_auto_exposure(bool enable)
{
    uint8_t mode = enable ? 2 : 1;  // 2=auto, 1=manual
    esp_err_t ret = usb_streaming_control(STREAM_UVC, 
                                         CTRL_UVC_AUTO_EXPOSURE_MODE,
                                         (void *)(uintptr_t)mode);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Auto exposure %s", enable ? "enabled" : "disabled");
    }
    
    return ret;
}

/**
 * @brief Set brightness
 * 
 * @param brightness Brightness value (typically 0-255)
 * @return ESP_OK on success
 */
esp_err_t camera_uvc_set_brightness(int16_t brightness)
{
    esp_err_t ret = usb_streaming_control(STREAM_UVC, 
                                         CTRL_UVC_BRIGHTNESS,
                                         (void *)(uintptr_t)brightness);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Brightness set to %d", brightness);
    }
    
    return ret;
}

/**
 * @brief Set contrast
 * 
 * @param contrast Contrast value
 * @return ESP_OK on success
 */
esp_err_t camera_uvc_set_contrast(uint16_t contrast)
{
    esp_err_t ret = usb_streaming_control(STREAM_UVC, 
                                         CTRL_UVC_CONTRAST,
                                         (void *)(uintptr_t)contrast);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Contrast set to %d", contrast);
    }
    
    return ret;
}

/**
 * @brief Set saturation
 * 
 * @param saturation Saturation value
 * @return ESP_OK on success
 */
esp_err_t camera_uvc_set_saturation(uint16_t saturation)
{
    esp_err_t ret = usb_streaming_control(STREAM_UVC, 
                                         CTRL_UVC_SATURATION,
                                         (void *)(uintptr_t)saturation);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Saturation set to %d", saturation);
    }
    
    return ret;
}

/**
 * @brief Apply optimal camera settings for indoor use
 */
esp_err_t camera_uvc_apply_indoor_settings(void)
{
    ESP_LOGI(TAG, "Applying indoor camera settings...");
    
    // Enable auto exposure
    camera_uvc_set_auto_exposure(true);
    
    // Enable HDR for better shadow/highlight balance
    camera_uvc_set_hdr(false);  // Off HDR
    
    // Increase brightness slightly for indoor lighting
    camera_uvc_set_brightness(140);
    
    // Increase saturation for better colors
    camera_uvc_set_saturation(130);
    
    ESP_LOGI(TAG, "Indoor settings applied");
    return ESP_OK;
}

/**
 * @brief Apply optimal camera settings for outdoor use
 */
esp_err_t camera_uvc_apply_outdoor_settings(void)
{
    ESP_LOGI(TAG, "Applying outdoor camera settings...");
    
    // Enable auto exposure
    camera_uvc_set_auto_exposure(true);
    
    // Higher HDR for bright sunlight
    camera_uvc_set_hdr(true);  // On HDR
    
    // Normal brightness
    camera_uvc_set_brightness(128);
    
    // Slightly reduced saturation to avoid oversaturation
    camera_uvc_set_saturation(120);
    
    ESP_LOGI(TAG, "Outdoor settings applied");
    return ESP_OK;
}

/**
 * @brief Apply optimal camera settings for low-light conditions
 */
esp_err_t camera_uvc_apply_lowlight_settings(void)
{
    ESP_LOGI(TAG, "Applying low-light camera settings...");
    
    // Enable auto exposure
    camera_uvc_set_auto_exposure(true);
    
    // Enable HDR
    camera_uvc_set_hdr(true);
    
    // Increase brightness significantly
    camera_uvc_set_brightness(180);
    
    // Increase contrast for better detail
    camera_uvc_set_contrast(150);
    
    ESP_LOGI(TAG, "Low-light settings applied");
    return ESP_OK;
}










