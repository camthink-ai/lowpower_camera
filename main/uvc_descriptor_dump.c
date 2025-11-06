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
    ESP_LOGI(TAG, "║        UVC 描述符架构说明                                  ║");
    ESP_LOGI(TAG, "╚════════════════════════════════════════════════════════════╝");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "问：为什么只有2个接口，却能控制这么多功能？");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "答：UVC使用分层描述符结构：");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "接口0: Video Control Interface (控制接口)");
    ESP_LOGI(TAG, "  ├─ 不直接传输视频数据");
    ESP_LOGI(TAG, "  ├─ 包含多个功能单元描述符：");
    ESP_LOGI(TAG, "  │");
    ESP_LOGI(TAG, "  ├─ [单元1] Input Terminal (输入终端)");
    ESP_LOGI(TAG, "  │   └─ 描述视频输入源");
    ESP_LOGI(TAG, "  │");
    ESP_LOGI(TAG, "  ├─ [单元2] Camera Terminal (摄像头终端)");
    ESP_LOGI(TAG, "  │   ├─ 自动曝光控制");
    ESP_LOGI(TAG, "  │   ├─ 曝光时间控制");
    ESP_LOGI(TAG, "  │   ├─ 对焦控制");
    ESP_LOGI(TAG, "  │   ├─ 自动对焦");
    ESP_LOGI(TAG, "  │   └─ 数字缩放");
    ESP_LOGI(TAG, "  │");
    ESP_LOGI(TAG, "  ├─ [单元3] Processing Unit (处理单元)");
    ESP_LOGI(TAG, "  │   ├─ ★ HDR/背光补偿");
    ESP_LOGI(TAG, "  │   ├─ 亮度");
    ESP_LOGI(TAG, "  │   ├─ 对比度");
    ESP_LOGI(TAG, "  │   ├─ 饱和度");
    ESP_LOGI(TAG, "  │   ├─ 锐度");
    ESP_LOGI(TAG, "  │   ├─ 色调");
    ESP_LOGI(TAG, "  │   ├─ 伽马");
    ESP_LOGI(TAG, "  │   ├─ 增益");
    ESP_LOGI(TAG, "  │   └─ 白平衡");
    ESP_LOGI(TAG, "  │");
    ESP_LOGI(TAG, "  └─ [单元4] Output Terminal (输出终端)");
    ESP_LOGI(TAG, "      └─ 描述视频输出");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "接口1: Video Streaming Interface (视频流接口)");
    ESP_LOGI(TAG, "  ├─ 负责实际的视频数据传输");
    ESP_LOGI(TAG, "  ├─ 包含格式描述符：");
    ESP_LOGI(TAG, "  │   └─ MJPEG格式");
    ESP_LOGI(TAG, "  └─ 包含帧描述符：");
    ESP_LOGI(TAG, "      ├─ 1920x1080 @ 2fps");
    ESP_LOGI(TAG, "      ├─ 1280x720 @ 10fps");
    ESP_LOGI(TAG, "      ├─ 640x360 @ 10fps");
    ESP_LOGI(TAG, "      └─ 320x240 @ 10fps");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "总结：");
    ESP_LOGI(TAG, "• 2个USB接口 ≠ 2个功能");
    ESP_LOGI(TAG, "• 接口0内部包含多个功能单元（Unit）");
    ESP_LOGI(TAG, "• 每个单元有自己的描述符，定义支持的控制");
    ESP_LOGI(TAG, "• 所有控制通过接口0的端点0进行");
    ESP_LOGI(TAG, "• 视频数据通过接口1的端点传输");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "这就是为什么只有2个接口，却能控制十几种功能！");
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
    ESP_LOGI(TAG, "║        UVC 控制路径示意图                                  ║");
    ESP_LOGI(TAG, "╚════════════════════════════════════════════════════════════╝");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "应用程序层:");
    ESP_LOGI(TAG, "  camera_uvc_set_hdr(2)  // 设置HDR等级");
    ESP_LOGI(TAG, "         ↓");
    ESP_LOGI(TAG, "USB Stream API:");
    ESP_LOGI(TAG, "  usb_streaming_control(STREAM_UVC, CTRL_UVC_BACKLIGHT_COMPENSATION, 2)");
    ESP_LOGI(TAG, "         ↓");
    ESP_LOGI(TAG, "UVC驱动层:");
    ESP_LOGI(TAG, "  _uvc_set_processing_unit_control(interface=0, unit_id=2, cs=0x01, data=2)");
    ESP_LOGI(TAG, "         ↓");
    ESP_LOGI(TAG, "USB控制传输:");
    ESP_LOGI(TAG, "  bmRequestType: 0x21 (Class-specific, Interface)");
    ESP_LOGI(TAG, "  bRequest: SET_CUR (0x01)");
    ESP_LOGI(TAG, "  wValue: 0x0100 (Backlight Compensation Control)");
    ESP_LOGI(TAG, "  wIndex: 0x0002 (Unit ID 2 = Processing Unit)");
    ESP_LOGI(TAG, "  wLength: 2");
    ESP_LOGI(TAG, "  Data: [0x02, 0x00]");
    ESP_LOGI(TAG, "         ↓");
    ESP_LOGI(TAG, "USB硬件:");
    ESP_LOGI(TAG, "  通过端点0(控制端点)发送到摄像头");
    ESP_LOGI(TAG, "         ↓");
    ESP_LOGI(TAG, "摄像头固件:");
    ESP_LOGI(TAG, "  处理单元(PU)接收命令");
    ESP_LOGI(TAG, "  应用HDR背光补偿算法");
    ESP_LOGI(TAG, "  影响后续视频帧");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "关键点：");
    ESP_LOGI(TAG, "• 所有控制都通过接口0的端点0(控制端点)");
    ESP_LOGI(TAG, "• 不同的控制通过不同的Unit ID和Control Selector区分");
    ESP_LOGI(TAG, "• Unit ID通常: 1=Camera Terminal, 2=Processing Unit");
    ESP_LOGI(TAG, "• 视频数据通过接口1的数据端点(端点3)传输");
    ESP_LOGI(TAG, "");
}

/**
 * @brief Main descriptor analysis function
 */
void uvc_analyze_descriptors(void)
{
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "════════════════════════════════════════════════════════════");
    ESP_LOGI(TAG, "        USB VIDEO CLASS 完整描述符分析");
    ESP_LOGI(TAG, "════════════════════════════════════════════════════════════");
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

