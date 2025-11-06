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

// Cache for supported controls
static struct {
    bool initialized;
    bool supports_hdr;
    bool supports_auto_exposure;
    bool supports_brightness;
    bool supports_contrast;
    bool supports_saturation;
    bool supports_auto_focus;
} uvc_caps = {0};

/**
 * @brief Detect which controls are supported by the camera
 * 
 * This should be called once after camera initialization
 */
esp_err_t camera_uvc_detect_capabilities(void)
{
    ESP_LOGI(TAG, "Detecting UVC camera capabilities...");
    
    // Test HDR support
    uint16_t test_hdr = 1;
    esp_err_t ret = usb_streaming_control(STREAM_UVC, CTRL_UVC_BACKLIGHT_COMPENSATION, 
                                         (void *)(uintptr_t)test_hdr);
    uvc_caps.supports_hdr = (ret == ESP_OK);
    ESP_LOGI(TAG, "HDR/Backlight Compensation: %s", uvc_caps.supports_hdr ? "YES" : "NO");
    
    // Test auto exposure
    uint8_t test_ae = 2;
    ret = usb_streaming_control(STREAM_UVC, CTRL_UVC_AUTO_EXPOSURE_MODE, 
                                (void *)(uintptr_t)test_ae);
    uvc_caps.supports_auto_exposure = (ret == ESP_OK);
    ESP_LOGI(TAG, "Auto Exposure: %s", uvc_caps.supports_auto_exposure ? "YES" : "NO");
    
    // Test brightness
    int16_t test_brightness = 128;
    ret = usb_streaming_control(STREAM_UVC, CTRL_UVC_BRIGHTNESS, 
                                (void *)(uintptr_t)test_brightness);
    uvc_caps.supports_brightness = (ret == ESP_OK);
    ESP_LOGI(TAG, "Brightness: %s", uvc_caps.supports_brightness ? "YES" : "NO");
    
    // Test contrast
    uint16_t test_contrast = 128;
    ret = usb_streaming_control(STREAM_UVC, CTRL_UVC_CONTRAST, 
                                (void *)(uintptr_t)test_contrast);
    uvc_caps.supports_contrast = (ret == ESP_OK);
    ESP_LOGI(TAG, "Contrast: %s", uvc_caps.supports_contrast ? "YES" : "NO");
    
    // Test saturation
    uint16_t test_saturation = 128;
    ret = usb_streaming_control(STREAM_UVC, CTRL_UVC_SATURATION, 
                                (void *)(uintptr_t)test_saturation);
    uvc_caps.supports_saturation = (ret == ESP_OK);
    ESP_LOGI(TAG, "Saturation: %s", uvc_caps.supports_saturation ? "YES" : "NO");
    
    // Test auto focus
    uint8_t test_af = 1;
    ret = usb_streaming_control(STREAM_UVC, CTRL_UVC_FOCUS_AUTO, 
                                (void *)(uintptr_t)test_af);
    uvc_caps.supports_auto_focus = (ret == ESP_OK);
    ESP_LOGI(TAG, "Auto Focus: %s", uvc_caps.supports_auto_focus ? "YES" : "NO");
    
    uvc_caps.initialized = true;
    ESP_LOGI(TAG, "Capability detection complete");
    
    return ESP_OK;
}

/**
 * @brief Check if HDR is supported
 */
bool camera_uvc_is_hdr_supported(void)
{
    return uvc_caps.supports_hdr;
}

/**
 * @brief Set HDR level
 * 
 * @param level HDR level (0=off, higher values = more compensation)
 * @return ESP_OK on success
 */
esp_err_t camera_uvc_set_hdr(uint16_t level)
{
    if (!uvc_caps.supports_hdr) {
        ESP_LOGW(TAG, "HDR not supported by this camera");
        return ESP_ERR_NOT_SUPPORTED;
    }
    
    esp_err_t ret = usb_streaming_control(STREAM_UVC, 
                                         CTRL_UVC_BACKLIGHT_COMPENSATION,
                                         (void *)(uintptr_t)level);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "HDR set to level %d", level);
    } else {
        ESP_LOGE(TAG, "Failed to set HDR level");
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
    if (!uvc_caps.supports_auto_exposure) {
        ESP_LOGW(TAG, "Auto exposure not supported");
        return ESP_ERR_NOT_SUPPORTED;
    }
    
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
    if (!uvc_caps.supports_brightness) {
        ESP_LOGW(TAG, "Brightness control not supported");
        return ESP_ERR_NOT_SUPPORTED;
    }
    
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
    if (!uvc_caps.supports_contrast) {
        ESP_LOGW(TAG, "Contrast control not supported");
        return ESP_ERR_NOT_SUPPORTED;
    }
    
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
    if (!uvc_caps.supports_saturation) {
        ESP_LOGW(TAG, "Saturation control not supported");
        return ESP_ERR_NOT_SUPPORTED;
    }
    
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
    if (uvc_caps.supports_auto_exposure) {
        camera_uvc_set_auto_exposure(true);
    }
    
    // Enable HDR for better shadow/highlight balance
    if (uvc_caps.supports_hdr) {
        camera_uvc_set_hdr(2);  // Medium HDR
    }
    
    // Increase brightness slightly for indoor lighting
    if (uvc_caps.supports_brightness) {
        camera_uvc_set_brightness(140);
    }
    
    // Increase saturation for better colors
    if (uvc_caps.supports_saturation) {
        camera_uvc_set_saturation(130);
    }
    
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
    if (uvc_caps.supports_auto_exposure) {
        camera_uvc_set_auto_exposure(true);
    }
    
    // Higher HDR for bright sunlight
    if (uvc_caps.supports_hdr) {
        camera_uvc_set_hdr(3);  // High HDR
    }
    
    // Normal brightness
    if (uvc_caps.supports_brightness) {
        camera_uvc_set_brightness(128);
    }
    
    // Slightly reduced saturation to avoid oversaturation
    if (uvc_caps.supports_saturation) {
        camera_uvc_set_saturation(120);
    }
    
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
    if (uvc_caps.supports_auto_exposure) {
        camera_uvc_set_auto_exposure(true);
    }
    
    // Enable HDR
    if (uvc_caps.supports_hdr) {
        camera_uvc_set_hdr(2);
    }
    
    // Increase brightness significantly
    if (uvc_caps.supports_brightness) {
        camera_uvc_set_brightness(180);
    }
    
    // Increase contrast for better detail
    if (uvc_caps.supports_contrast) {
        camera_uvc_set_contrast(150);
    }
    
    ESP_LOGI(TAG, "Low-light settings applied");
    return ESP_OK;
}

/**
 * @brief Get current capabilities
 */
const camera_uvc_capabilities_t* camera_uvc_get_capabilities(void)
{
    static camera_uvc_capabilities_t caps;
    
    caps.hdr_supported = uvc_caps.supports_hdr;
    caps.auto_exposure_supported = uvc_caps.supports_auto_exposure;
    caps.brightness_supported = uvc_caps.supports_brightness;
    caps.contrast_supported = uvc_caps.supports_contrast;
    caps.saturation_supported = uvc_caps.supports_saturation;
    caps.auto_focus_supported = uvc_caps.supports_auto_focus;
    
    return &caps;
}









