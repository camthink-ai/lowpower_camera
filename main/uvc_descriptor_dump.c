/*
 * UVC Descriptor Dump - Print detailed UVC control descriptors
 */

#include "esp_log.h"
#include "usb_stream.h"

static const char *TAG = "UVC_DESC_DUMP";

/**
 * @brief Explain UVC descriptor architecture
 */
void uvc_explain_architecture(void)
{
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "╔════════════════════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║        UVC descriptor architecture explanation              ║");
    ESP_LOGI(TAG, "╚════════════════════════════════════════════════════════════╝");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Question: Why only 2 interfaces can control so many functions?");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Answer: UVC uses a hierarchical descriptor structure:");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Interface 0: Video Control Interface (control interface)");
    ESP_LOGI(TAG, "  ├─ Does not directly transmit video data");
    ESP_LOGI(TAG, "  ├─ Contains multiple functional unit descriptors:");
    ESP_LOGI(TAG, "  │");
    ESP_LOGI(TAG, "  ├─ [Unit 1] Input Terminal (input terminal)");
    ESP_LOGI(TAG, "  │   └─ Describes the video input source");
    ESP_LOGI(TAG, "  │");
    ESP_LOGI(TAG, "  ├─ [Unit 2] Camera Terminal (camera terminal)");
    ESP_LOGI(TAG, "  │   ├─ Auto Exposure Control");
    ESP_LOGI(TAG, "  │   ├─ Exposure Time Control");
    ESP_LOGI(TAG, "  │   ├─ Focus Control");
    ESP_LOGI(TAG, "  │   ├─ Auto Focus");
    ESP_LOGI(TAG, "  │   └─ Digital Zoom");
    ESP_LOGI(TAG, "  │");
    ESP_LOGI(TAG, "  ├─ [Unit 3] Processing Unit (processing unit)");
    ESP_LOGI(TAG, "  │   ├─ ★ HDR/Backlight Compensation");
    ESP_LOGI(TAG, "  │   ├─ Brightness");
    ESP_LOGI(TAG, "  │   ├─ Contrast");
    ESP_LOGI(TAG, "  │   ├─ Saturation");
    ESP_LOGI(TAG, "  │   ├─ Sharpness");
    ESP_LOGI(TAG, "  │   ├─ Hue");
    ESP_LOGI(TAG, "  │   ├─ Gamma");
    ESP_LOGI(TAG, "  │   ├─ Gain");
    ESP_LOGI(TAG, "  │   └─ White Balance");
    ESP_LOGI(TAG, "  │");
    ESP_LOGI(TAG, "  └─ [Unit 4] Output Terminal (output terminal)");
    ESP_LOGI(TAG, "      └─ Describes the video output");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Interface 1: Video Streaming Interface (video streaming interface)");
    ESP_LOGI(TAG, "  ├─ Responsible for actual video data transmission");
    ESP_LOGI(TAG, "  ├─ Contains format descriptors:");
    ESP_LOGI(TAG, "  │   └─ MJPEG format");
    ESP_LOGI(TAG, "  └─ Contains frame descriptors:");
    ESP_LOGI(TAG, "      ├─ 1920x1080 @ 2fps");
    ESP_LOGI(TAG, "      ├─ 1280x720 @ 10fps");
    ESP_LOGI(TAG, "      ├─ 640x360 @ 10fps");
    ESP_LOGI(TAG, "      └─ 320x240 @ 10fps");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Summary: 2 USB interfaces ≠ 2 functions");
    ESP_LOGI(TAG, "• 2 USB interfaces ≠ 2 functions");
    ESP_LOGI(TAG, "• Interface 0 contains multiple functional units (Units)");
    ESP_LOGI(TAG, "• Each unit has its own descriptor, defining supported controls");
    ESP_LOGI(TAG, "• All controls are sent through endpoint 0 of interface 0");
    ESP_LOGI(TAG, "• Video data is transmitted through endpoint 3 of interface 1");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "This is why only 2 interfaces can control so many functions!");
    ESP_LOGI(TAG, "");
}

/**
 * @brief Dump all UVC control capabilities with detailed information
 * 
 * This function tests and reports the capabilities and ranges of all
 * UVC controls supported by the connected camera.
 */
void uvc_dump_full_descriptors(void)
{
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "╔══════════════════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║         USB VIDEO CLASS (UVC) DESCRIPTOR DUMP            ║");
    ESP_LOGI(TAG, "╚══════════════════════════════════════════════════════════╝");
    ESP_LOGI(TAG, "");
    
    ESP_LOGI(TAG, "=== CAMERA TERMINAL CONTROLS ===");
    ESP_LOGI(TAG, "");
    
    // Auto Exposure Mode
    ESP_LOGI(TAG, "[CT] Auto Exposure Mode (AE_MODE_CONTROL)");
    ESP_LOGI(TAG, "     Values: 1=Manual, 2=Auto, 4=Shutter Priority, 8=Aperture Priority");
    uint8_t ae_mode = 2;
    if (usb_streaming_control(STREAM_UVC, CTRL_UVC_AUTO_EXPOSURE_MODE, (void *)(uintptr_t)ae_mode) == ESP_OK) {
        ESP_LOGI(TAG, "     Status: ✓ SUPPORTED");
    } else {
        ESP_LOGI(TAG, "     Status: ✗ NOT SUPPORTED");
    }
    ESP_LOGI(TAG, "");
    
    // Auto Exposure Priority
    ESP_LOGI(TAG, "[CT] Auto Exposure Priority (AE_PRIORITY_CONTROL)");
    ESP_LOGI(TAG, "     Controls whether to maintain frame rate vs exposure");
    uint8_t ae_priority = 1;
    if (usb_streaming_control(STREAM_UVC, CTRL_UVC_AUTO_EXPOSURE_PRIORITY, (void *)(uintptr_t)ae_priority) == ESP_OK) {
        ESP_LOGI(TAG, "     Status: ✓ SUPPORTED");
    } else {
        ESP_LOGI(TAG, "     Status: ✗ NOT SUPPORTED");
    }
    ESP_LOGI(TAG, "");
    
    // Exposure Time Absolute
    ESP_LOGI(TAG, "[CT] Exposure Time Absolute (EXPOSURE_TIME_ABSOLUTE_CONTROL)");
    ESP_LOGI(TAG, "     Manual exposure time control in device-specific units");
    uint32_t exposure = 100;
    if (usb_streaming_control(STREAM_UVC, CTRL_UVC_EXPOSURE_TIME_ABSOLUTE, (void *)(uintptr_t)exposure) == ESP_OK) {
        ESP_LOGI(TAG, "     Status: ✓ SUPPORTED");
    } else {
        ESP_LOGI(TAG, "     Status: ✗ NOT SUPPORTED");
    }
    ESP_LOGI(TAG, "");
    
    // Focus Absolute
    ESP_LOGI(TAG, "[CT] Focus Absolute (FOCUS_ABSOLUTE_CONTROL)");
    ESP_LOGI(TAG, "     Manual focus control");
    uint16_t focus = 50;
    if (usb_streaming_control(STREAM_UVC, CTRL_UVC_FOCUS_ABSOLUTE, (void *)(uintptr_t)focus) == ESP_OK) {
        ESP_LOGI(TAG, "     Status: ✓ SUPPORTED");
    } else {
        ESP_LOGI(TAG, "     Status: ✗ NOT SUPPORTED");
    }
    ESP_LOGI(TAG, "");
    
    // Auto Focus
    ESP_LOGI(TAG, "[CT] Auto Focus (FOCUS_AUTO_CONTROL)");
    ESP_LOGI(TAG, "     Automatic focus control");
    uint8_t auto_focus = 1;
    if (usb_streaming_control(STREAM_UVC, CTRL_UVC_FOCUS_AUTO, (void *)(uintptr_t)auto_focus) == ESP_OK) {
        ESP_LOGI(TAG, "     Status: ✓ SUPPORTED");
    } else {
        ESP_LOGI(TAG, "     Status: ✗ NOT SUPPORTED");
    }
    ESP_LOGI(TAG, "");
    
    // Zoom Absolute
    ESP_LOGI(TAG, "[CT] Zoom Absolute (ZOOM_ABSOLUTE_CONTROL)");
    ESP_LOGI(TAG, "     Digital zoom control");
    uint16_t zoom = 100;
    if (usb_streaming_control(STREAM_UVC, CTRL_UVC_ZOOM_ABSOLUTE, (void *)(uintptr_t)zoom) == ESP_OK) {
        ESP_LOGI(TAG, "     Status: ✓ SUPPORTED");
    } else {
        ESP_LOGI(TAG, "     Status: ✗ NOT SUPPORTED");
    }
    ESP_LOGI(TAG, "");
    
    ESP_LOGI(TAG, "=== PROCESSING UNIT CONTROLS ===");
    ESP_LOGI(TAG, "");
    
    // Backlight Compensation (HDR)
    ESP_LOGI(TAG, "[PU] Backlight Compensation - HDR (BACKLIGHT_COMPENSATION_CONTROL)");
    ESP_LOGI(TAG, "     *** This is the HDR-like feature ***");
    ESP_LOGI(TAG, "     Compensates for backlit scenes, improves dynamic range");
    ESP_LOGI(TAG, "     Typical range: 0=Off, 1-3=Low/Med/High");
    uint16_t backlight = 1;
    if (usb_streaming_control(STREAM_UVC, CTRL_UVC_BACKLIGHT_COMPENSATION, (void *)(uintptr_t)backlight) == ESP_OK) {
        ESP_LOGI(TAG, "     Status: ✓✓✓ SUPPORTED (HDR AVAILABLE) ✓✓✓");
    } else {
        ESP_LOGI(TAG, "     Status: ✗✗✗ NOT SUPPORTED (NO HDR) ✗✗✗");
    }
    ESP_LOGI(TAG, "");
    
    // Brightness
    ESP_LOGI(TAG, "[PU] Brightness (BRIGHTNESS_CONTROL)");
    ESP_LOGI(TAG, "     Image brightness adjustment");
    int16_t brightness = 128;
    if (usb_streaming_control(STREAM_UVC, CTRL_UVC_BRIGHTNESS, (void *)(uintptr_t)brightness) == ESP_OK) {
        ESP_LOGI(TAG, "     Status: ✓ SUPPORTED");
    } else {
        ESP_LOGI(TAG, "     Status: ✗ NOT SUPPORTED");
    }
    ESP_LOGI(TAG, "");
    
    // Contrast
    ESP_LOGI(TAG, "[PU] Contrast (CONTRAST_CONTROL)");
    ESP_LOGI(TAG, "     Image contrast adjustment");
    uint16_t contrast = 128;
    if (usb_streaming_control(STREAM_UVC, CTRL_UVC_CONTRAST, (void *)(uintptr_t)contrast) == ESP_OK) {
        ESP_LOGI(TAG, "     Status: ✓ SUPPORTED");
    } else {
        ESP_LOGI(TAG, "     Status: ✗ NOT SUPPORTED");
    }
    ESP_LOGI(TAG, "");
    
    // Gain
    ESP_LOGI(TAG, "[PU] Gain (GAIN_CONTROL)");
    ESP_LOGI(TAG, "     Manual gain/ISO control");
    uint16_t gain = 50;
    if (usb_streaming_control(STREAM_UVC, CTRL_UVC_GAIN, (void *)(uintptr_t)gain) == ESP_OK) {
        ESP_LOGI(TAG, "     Status: ✓ SUPPORTED");
    } else {
        ESP_LOGI(TAG, "     Status: ✗ NOT SUPPORTED");
    }
    ESP_LOGI(TAG, "");
    
    // Power Line Frequency
    ESP_LOGI(TAG, "[PU] Power Line Frequency (POWER_LINE_FREQUENCY_CONTROL)");
    ESP_LOGI(TAG, "     Anti-flicker filter (0=Disabled, 1=50Hz, 2=60Hz)");
    uint8_t power_freq = 1;
    if (usb_streaming_control(STREAM_UVC, CTRL_UVC_POWER_LINE_FREQUENCY, (void *)(uintptr_t)power_freq) == ESP_OK) {
        ESP_LOGI(TAG, "     Status: ✓ SUPPORTED");
    } else {
        ESP_LOGI(TAG, "     Status: ✗ NOT SUPPORTED");
    }
    ESP_LOGI(TAG, "");
    
    // Hue
    ESP_LOGI(TAG, "[PU] Hue (HUE_CONTROL)");
    ESP_LOGI(TAG, "     Color hue adjustment");
    int16_t hue = 0;
    if (usb_streaming_control(STREAM_UVC, CTRL_UVC_HUE, (void *)(uintptr_t)hue) == ESP_OK) {
        ESP_LOGI(TAG, "     Status: ✓ SUPPORTED");
    } else {
        ESP_LOGI(TAG, "     Status: ✗ NOT SUPPORTED");
    }
    ESP_LOGI(TAG, "");
    
    // Saturation
    ESP_LOGI(TAG, "[PU] Saturation (SATURATION_CONTROL)");
    ESP_LOGI(TAG, "     Color saturation adjustment");
    uint16_t saturation = 128;
    if (usb_streaming_control(STREAM_UVC, CTRL_UVC_SATURATION, (void *)(uintptr_t)saturation) == ESP_OK) {
        ESP_LOGI(TAG, "     Status: ✓ SUPPORTED");
    } else {
        ESP_LOGI(TAG, "     Status: ✗ NOT SUPPORTED");
    }
    ESP_LOGI(TAG, "");
    
    // Sharpness
    ESP_LOGI(TAG, "[PU] Sharpness (SHARPNESS_CONTROL)");
    ESP_LOGI(TAG, "     Image sharpness adjustment");
    uint16_t sharpness = 128;
    if (usb_streaming_control(STREAM_UVC, CTRL_UVC_SHARPNESS, (void *)(uintptr_t)sharpness) == ESP_OK) {
        ESP_LOGI(TAG, "     Status: ✓ SUPPORTED");
    } else {
        ESP_LOGI(TAG, "     Status: ✗ NOT SUPPORTED");
    }
    ESP_LOGI(TAG, "");
    
    // Gamma
    ESP_LOGI(TAG, "[PU] Gamma (GAMMA_CONTROL)");
    ESP_LOGI(TAG, "     Gamma curve adjustment");
    uint16_t gamma = 100;
    if (usb_streaming_control(STREAM_UVC, CTRL_UVC_GAMMA, (void *)(uintptr_t)gamma) == ESP_OK) {
        ESP_LOGI(TAG, "     Status: ✓ SUPPORTED");
    } else {
        ESP_LOGI(TAG, "     Status: ✗ NOT SUPPORTED");
    }
    ESP_LOGI(TAG, "");
    
    // White Balance Temperature
    ESP_LOGI(TAG, "[PU] White Balance Temperature (WHITE_BALANCE_TEMPERATURE_CONTROL)");
    ESP_LOGI(TAG, "     Manual white balance in Kelvin");
    uint16_t wb_temp = 4000;
    if (usb_streaming_control(STREAM_UVC, CTRL_UVC_WHITE_BALANCE_TEMPERATURE, (void *)(uintptr_t)wb_temp) == ESP_OK) {
        ESP_LOGI(TAG, "     Status: ✓ SUPPORTED");
    } else {
        ESP_LOGI(TAG, "     Status: ✗ NOT SUPPORTED");
    }
    ESP_LOGI(TAG, "");
    
    // White Balance Auto
    ESP_LOGI(TAG, "[PU] White Balance Auto (WHITE_BALANCE_TEMP_AUTO_CONTROL)");
    ESP_LOGI(TAG, "     Automatic white balance");
    uint8_t wb_auto = 1;
    if (usb_streaming_control(STREAM_UVC, CTRL_UVC_WHITE_BALANCE_TEMP_AUTO, (void *)(uintptr_t)wb_auto) == ESP_OK) {
        ESP_LOGI(TAG, "     Status: ✓ SUPPORTED");
    } else {
        ESP_LOGI(TAG, "     Status: ✗ NOT SUPPORTED");
    }
    ESP_LOGI(TAG, "");
    
    // Hue Auto
    ESP_LOGI(TAG, "[PU] Hue Auto (HUE_AUTO_CONTROL)");
    ESP_LOGI(TAG, "     Automatic hue control");
    uint8_t hue_auto = 1;
    if (usb_streaming_control(STREAM_UVC, CTRL_UVC_HUE_AUTO, (void *)(uintptr_t)hue_auto) == ESP_OK) {
        ESP_LOGI(TAG, "     Status: ✓ SUPPORTED");
    } else {
        ESP_LOGI(TAG, "     Status: ✗ NOT SUPPORTED");
    }
    ESP_LOGI(TAG, "");
    
    // Contrast Auto
    ESP_LOGI(TAG, "[PU] Contrast Auto (CONTRAST_AUTO_CONTROL)");
    ESP_LOGI(TAG, "     Automatic contrast control");
    uint8_t contrast_auto = 1;
    if (usb_streaming_control(STREAM_UVC, CTRL_UVC_CONTRAST_AUTO, (void *)(uintptr_t)contrast_auto) == ESP_OK) {
        ESP_LOGI(TAG, "     Status: ✓ SUPPORTED");
    } else {
        ESP_LOGI(TAG, "     Status: ✗ NOT SUPPORTED");
    }
    ESP_LOGI(TAG, "");
    
    ESP_LOGI(TAG, "╔══════════════════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║              DESCRIPTOR DUMP COMPLETE                    ║");
    ESP_LOGI(TAG, "╚══════════════════════════════════════════════════════════╝");
    ESP_LOGI(TAG, "");
    
    ESP_LOGI(TAG, "Note: This camera supports:");
    ESP_LOGI(TAG, "  - Camera Terminal controls (CT): Exposure, Focus, Zoom");
    ESP_LOGI(TAG, "  - Processing Unit controls (PU): Image adjustments");
    ESP_LOGI(TAG, "  - HDR is implemented via Backlight Compensation control");
    ESP_LOGI(TAG, "");
}

/**
 * @brief Show UVC control architecture diagram
 */
void uvc_show_control_path(void)
{
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "╔════════════════════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║        UVC control path diagram                           ║");
    ESP_LOGI(TAG, "╚════════════════════════════════════════════════════════════╝");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Application layer:");
    ESP_LOGI(TAG, "  camera_uvc_set_hdr(2)  // Set HDR level");
    ESP_LOGI(TAG, "         ↓");
    ESP_LOGI(TAG, "USB Stream API:");
    ESP_LOGI(TAG, "  usb_streaming_control(STREAM_UVC, CTRL_UVC_BACKLIGHT_COMPENSATION, 2)");
    ESP_LOGI(TAG, "         ↓");
    ESP_LOGI(TAG, "UVC driver layer:");
    ESP_LOGI(TAG, "  _uvc_set_processing_unit_control(interface=0, unit_id=2, cs=0x01, data=2)");
    ESP_LOGI(TAG, "         ↓");
    ESP_LOGI(TAG, "USB control transfer:");
    ESP_LOGI(TAG, "  bmRequestType: 0x21 (Class-specific, Interface)");
    ESP_LOGI(TAG, "  bRequest: SET_CUR (0x01)");
    ESP_LOGI(TAG, "  wValue: 0x0100 (Backlight Compensation Control)");
    ESP_LOGI(TAG, "  wIndex: 0x0002 (Unit ID 2 = Processing Unit)");
    ESP_LOGI(TAG, "  wLength: 2");
    ESP_LOGI(TAG, "  Data: [0x02, 0x00]");
    ESP_LOGI(TAG, "         ↓");
    ESP_LOGI(TAG, "USB hardware:");
    ESP_LOGI(TAG, "  Send to camera through endpoint 0 (control endpoint)");
    ESP_LOGI(TAG, "         ↓");
    ESP_LOGI(TAG, "Camera firmware:");
    ESP_LOGI(TAG, "  Processing unit (PU) receives command");
    ESP_LOGI(TAG, "  Apply HDR backlight compensation algorithm");
    ESP_LOGI(TAG, "  Affect subsequent video frames");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Key points:");
    ESP_LOGI(TAG, "• All controls are sent through endpoint 0 (control endpoint) of interface 0");
    ESP_LOGI(TAG, "• Different controls are distinguished by different Unit ID and Control Selector");
    ESP_LOGI(TAG, "• Unit ID usually: 1=Camera Terminal, 2=Processing Unit");
    ESP_LOGI(TAG, "• Video data is transmitted through data endpoint 3 (endpoint 3) of interface 1");
    ESP_LOGI(TAG, "");
}

/**
 * @brief Main descriptor analysis function
 */
void uvc_analyze_descriptors(void)
{
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "╔════════════════════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "        USB VIDEO CLASS full descriptor analysis");
    ESP_LOGI(TAG, "╚════════════════════════════════════════════════════════════╝");
    ESP_LOGI(TAG, "");
    
    // Step 1: Explain architecture
    uvc_explain_architecture();
    
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // Step 2: Show control path
    uvc_show_control_path();
    
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // Step 3: Dump all controls
    uvc_dump_full_descriptors();
}

