/**
 * Camera module implementation for ESP32-CAM
 * 
 * Handles camera initialization, configuration, and snapshot capture
 * Interfaces with ESP32-CAM hardware and manages image capture workflow
 */

// Board configuration - using ESP32-CAM AI Thinker module
#define BOARD_ESP32CAM_AITHINKER

// Includes for camera functionality

#include <esp_log.h>
#include <esp_system.h>
#include <nvs_flash.h>
#include <sys/param.h>
#include <string.h>
#include <esp_timer.h>
#include "img_converters.h"

// Support both IDF 5.x
#ifndef portTICK_RATE_MS
    #define portTICK_RATE_MS portTICK_PERIOD_MS
#endif
#include "camera.h"
#include "sleep.h"
#include "misc.h"
#include "utils.h"
#include "uvc.h"

#define TAG "-->CAMERA"  // Logging tag for camera module

#define CAMERA_MODULE_NAME "ESP-S3-EYE"
#define CAMERA_PIN_PWDN -1  // Not used
#define CAMERA_PIN_RESET -1 // Not used

// Camera interface pins
#define CAMERA_PIN_VSYNC 6   // Vertical sync
#define CAMERA_PIN_HREF 7    // Horizontal reference
#define CAMERA_PIN_PCLK 13   // Pixel clock
#define CAMERA_PIN_XCLK 15   // System clock

// I2C pins for camera control
#define CAMERA_PIN_SIOD 4    // I2C data
#define CAMERA_PIN_SIOC 5    // I2C clock

// Camera data bus pins
#define CAMERA_PIN_D0 11     // Data bit 0
#define CAMERA_PIN_D1 9      // Data bit 1
#define CAMERA_PIN_D2 8      // Data bit 2
#define CAMERA_PIN_D3 10     // Data bit 3
#define CAMERA_PIN_D4 12     // Data bit 4
#define CAMERA_PIN_D5 18     // Data bit 5
#define CAMERA_PIN_D6 17     // Data bit 6
#define CAMERA_PIN_D7 16     // Data bit 7

/**
 * Camera module state structure
 */
typedef struct mdCamera {
    QueueHandle_t in;            // Input queue for commands
    QueueHandle_t out;           // Output queue for captured frames
    SemaphoreHandle_t mutex;     // Mutex for thread-safe operations
    uint8_t captureCount;        // Number of active captures
    EventGroupHandle_t eventGroup; // Event group for camera state
    bool bFlashLedON;            // Flash LED state
    bool bInit;                  // Initialization flag
    bool bSnapShot;              // Snapshot in progress flag
    bool bSnapShotSuccess;       // Last snapshot success status
} mdCamera_t;

static mdCamera_t g_mdCamera = {0};  // Global camera state instance

/**
 * Lock camera mutex for thread-safe operations
 */
static void camera_lock(void)
{
    if (g_mdCamera.mutex) {
        xSemaphoreTake(g_mdCamera.mutex, portMAX_DELAY);
    }
}

/**
 * Unlock camera mutex
 */
static void camera_unlock(void)
{
    if (g_mdCamera.mutex) {
        xSemaphoreGive(g_mdCamera.mutex);
    }
}

/**
 * @brief Get frame buffer for streaming
 * @return Pointer to frame buffer structure
 */
camera_fb_t *camera_fb_get()
{
    if(CAMERA_USE_UVC){
        return uvc_stream_fb_get();
    }else{
        return esp_camera_fb_get();
    }
}

/**
 * @brief Return frame buffer after processing
 * @param fb Pointer to frame buffer structure
 */
void camera_fb_return(camera_fb_t *fb)
{
    if(CAMERA_USE_UVC){
        uvc_camera_fb_return(fb);
    }else{
        esp_camera_fb_return(fb);
    }
}

static void camera_queue_node_free(queueNode_t *node, nodeEvent_e event)
{
    if (node && node->context) {
        camera_fb_return((camera_fb_t *)node->context); // Return frame buffer
        free(node); // Free node memory
        ESP_LOGI(TAG, "camera_queue_node_free");
        camera_lock();
        g_mdCamera.captureCount--;  // Decrement active capture count
        if (g_mdCamera.captureCount == 0) {
            sleep_set_event_bits(SLEEP_SNAPSHOT_STOP_BIT);  // Signal no active captures
        }
        camera_unlock();
    }
}

/**
 * Allocate and initialize a new camera queue node
 * @param frame Camera frame buffer
 * @param type Snapshot type
 * @return Pointer to new node, or NULL on failure
 */
static queueNode_t *camera_queue_node_malloc(camera_fb_t *frame, snapType_e type)
{
    queueNode_t *node = calloc(1, sizeof(queueNode_t));
    if (node) {
        // Initialize node fields
        node->from = FROM_CAMERA;
        node->pts = get_time_ms();
        node->type = type;
        node->data = frame->buf;
        node->len = frame->len;
        node->context = frame;
        node->free_handler = camera_queue_node_free;
        node->ntp_sync_flag = system_get_ntp_sync_flag();
        
        ESP_LOGI(TAG, "camera_queue_node_malloc");
        camera_lock();
        g_mdCamera.captureCount++;  // Increment active capture count
        sleep_clear_event_bits(SLEEP_SNAPSHOT_STOP_BIT);  // Signal active capture
        camera_unlock();
        return node;
    }
    return NULL;  // Allocation failed
}

/**
 * Camera hardware configuration
 */
static camera_config_t camera_config = {
    .ledc_channel = LEDC_CHANNEL_0,    // LEDC channel for XCLK
    .ledc_timer = LEDC_TIMER_0,        // LEDC timer for XCLK
    .pin_d0 = CAMERA_PIN_D0,           // Data pins
    .pin_d1 = CAMERA_PIN_D1,
    .pin_d2 = CAMERA_PIN_D2,
    .pin_d3 = CAMERA_PIN_D3,
    .pin_d4 = CAMERA_PIN_D4,
    .pin_d5 = CAMERA_PIN_D5,
    .pin_d6 = CAMERA_PIN_D6,
    .pin_d7 = CAMERA_PIN_D7,
    .pin_xclk = CAMERA_PIN_XCLK,       // XCLK pin
    .pin_pclk = CAMERA_PIN_PCLK,       // PCLK pin
    .pin_vsync = CAMERA_PIN_VSYNC,     // VSYNC pin
    .pin_href = CAMERA_PIN_HREF,       // HREF pin
    .pin_sscb_sda = CAMERA_PIN_SIOD,   // I2C SDA
    .pin_sscb_scl = CAMERA_PIN_SIOC,   // I2C SCL
    .pin_pwdn = CAMERA_PIN_PWDN,       // Power down (not used)
    .pin_reset = CAMERA_PIN_RESET,     // Reset (not used)
    .xclk_freq_hz = 5000000,          // XCLK frequency (5MHz)
    .pixel_format = PIXFORMAT_JPEG,    // Output format (JPEG)
    .frame_size = FRAMESIZE_FHD,       // Resolution (Full HD)
    .jpeg_quality = 12,                // JPEG quality (12-63, lower=better)
    .fb_count = 2,                     // Frame buffer count
    .fb_location = CAMERA_FB_IN_PSRAM, // Store frames in PSRAM
    .grab_mode = CAMERA_GRAB_LATEST,   // Always get latest frame
};

extern modeSel_e main_mode;
/**
 * Initialize camera hardware with configured settings
 * @return ESP_OK on success, error code otherwise
 */
static esp_err_t init_camera()
{
    esp_err_t err;
    //initialize the camera
    if (CAMERA_USE_UVC) {
        err = uvc_init();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Camera Init Failed");
            return err;
        }
    } else {
        // if (main_mode == MODE_CONFIG) camera_config.xclk_freq_hz = 5000000;
        // else camera_config.xclk_freq_hz = 24000000;
        err = esp_camera_init(&camera_config);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Camera Init Failed");
            return err;
        }
        // Get camera sensor and apply image settings
        imgAttr_t image;
        sensor_t *s = esp_camera_sensor_get();
        s->status.gainceiling = 0;
        s->status.hmirror = 1;
        
        cfg_get_image_attr(&image);
        err = camera_set_image(&image, true);
        ESP_LOGI(TAG, "camera_set_image ret = %s", esp_err_to_name(err));
        // s->set_gain_ctrl(s, image.bAgc);
        // if (image.bAgc) {
        //     s->set_gainceiling(s, image.gainCeiling);
        // } else {
        //     s->set_agc_gain(s, image.gain);
        // }
    
        // s->set_ae_level(s, image.aeLevel);
        // s->set_gain_ctrl(s, 1);
        // s->set_gainceiling(s, 0);
        // s->set_hmirror(s, image.bHorizonetal);
        // s->set_vflip(s, !image.bVertical);
        // s->set_contrast(s, image.contrast);
        // s->set_saturation(s, image.saturation);
        // s->set_brightness(s, image.brightness);
    }

    return ESP_OK;
}

esp_err_t camera_open(QueueHandle_t in, QueueHandle_t out)
{
    memset(&g_mdCamera, 0, sizeof(mdCamera_t));
    // misc_io_cfg(CAMERA_POWER_IO, 0, 1);
    // misc_io_set(CAMERA_POWER_IO,  CAMERA_POWER_ON);
    // camera_flash_led_open();
    if (system_get_mode() != MODE_CONFIG){
        lightAttr_t light;
        cfg_get_light_attr(&light);
        camera_flash_led_ctrl(&light);
    }
    if (ESP_OK != init_camera()) {
        sleep_set_event_bits(SLEEP_SNAPSHOT_STOP_BIT); //如果后续无截图任务，将进入休眠；
        return ESP_FAIL;
    }
    g_mdCamera.mutex = xSemaphoreCreateMutex();
    g_mdCamera.in = in;
    g_mdCamera.out = out;
    g_mdCamera.eventGroup = xEventGroupCreate();
    g_mdCamera.bInit = true;
    if (CAMERA_USE_UVC) vTaskDelay(pdMS_TO_TICKS(5000));    // wait for sensor stable
    else vTaskDelay(pdMS_TO_TICKS(5000));                   // wait for sensor stable
    sleep_set_event_bits(SLEEP_SNAPSHOT_STOP_BIT);          //如果后续无截图任务，将进入休眠；
    misc_get_battery_voltage();
    
    return ESP_OK;
}

esp_err_t camera_close()
{
    if (!g_mdCamera.bInit) {
        return ESP_FAIL;
    }
    if(CAMERA_USE_UVC){
        uvc_deinit();
    }
    misc_io_set(CAMERA_POWER_IO,  CAMERA_POWER_OFF);

    return ESP_OK;
}

esp_err_t camera_start()
{
    if (!g_mdCamera.bInit) {
        return ESP_FAIL;
    }
    xEventGroupClearBits(g_mdCamera.eventGroup, CAMERA_STOP_BIT);
    xEventGroupSetBits(g_mdCamera.eventGroup, CAMERA_START_BIT);
    return ESP_OK;
}

esp_err_t camera_stop()
{
    if (!g_mdCamera.bInit) {
        return ESP_FAIL;
    }
    xEventGroupClearBits(g_mdCamera.eventGroup, CAMERA_START_BIT);
    xEventGroupSetBits(g_mdCamera.eventGroup, CAMERA_STOP_BIT);
    return ESP_OK;
}

void camera_wait(cameraEvent_e event, uint32_t timeout_ms)
{
    if (!g_mdCamera.bInit) {
        return;
    }
    xEventGroupWaitBits(g_mdCamera.eventGroup, event, false, false, pdMS_TO_TICKS(timeout_ms));
}

static bool flash_led_is_time_open(char *startTime, char *endTime)
{
    int Hour, Minute;
    int nowMins, startMins, endMins;
    struct tm timeinfo;
    time_t now;

    time(&now);
    localtime_r(&now, &timeinfo);
    // 计算当前时间距离00:00:00的分钟数
    nowMins = timeinfo.tm_hour * 60 + timeinfo.tm_min;
    if (sscanf(startTime, "%02d:%02d", &Hour, &Minute) != 2) {
        ESP_LOGE(TAG, "invalid startTime %s", startTime);
        return false;
    }
    startMins = Hour * 60 + Minute;
    if (sscanf(endTime, "%02d:%02d", &Hour, &Minute) != 2) {
        ESP_LOGE(TAG, "invalid endTime %s", endTime);
        return false;
    }
    endMins = Hour * 60 + Minute;
    ESP_LOGI(TAG, " nowMins %d startMins %d, endMins %d", nowMins, startMins, endMins);
    if (startMins <= endMins) { //当天
        if (nowMins < startMins || nowMins > endMins) {
            return false;
        } else {
            return true;
        }
    } else { //跨天
        if (nowMins < startMins && nowMins > endMins) {
            return false;
        } else {
            return true;
        }
    }
    return false;
}

esp_err_t camera_flash_led_ctrl(lightAttr_t *light)
{
    switch (light->lightMode) {
        case 0:
            if (misc_get_light_value_rate() <= light->threshold) {
                misc_flash_led_open();
            } else {
                misc_flash_led_close();
            }
            break;
        case 1:
            if (flash_led_is_time_open(light->startTime, light->endTime)) {
                misc_flash_led_open();
            } else {
                misc_flash_led_close();
            }
            break;
        case 2:
            misc_flash_led_open();
            break;
        case 3:
            misc_flash_led_close();
            break;
        default:
            return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t camera_snapshot(snapType_e type, uint8_t count)
{
    if (!g_mdCamera.bInit) {
        return ESP_FAIL;
    }

    capAttr_t capture;
    cfg_get_cap_attr(&capture);
    if (type == SNAP_BUTTON && capture.bButtonCap == false) {
        ESP_LOGI(TAG, "snapshot fail, button is disabled");
        return ESP_FAIL;
    }
    if (type == SNAP_ALARMIN && capture.bAlarmInCap == false) {
        ESP_LOGI(TAG, "snapshot fail, alarmIn is disabled");
        return ESP_FAIL;
    }
    // lightAttr_t light;
    // cfg_get_light_attr(&light);
    // camera_flash_led_ctrl(&light);
    ESP_LOGI(TAG, "camera_snapshot Start");
    // esp_camera_fb_return(esp_camera_fb_get());
    g_mdCamera.bSnapShot = true;
    int try_count = 5;
    while (try_count--) {
        camera_fb_t *frame = camera_fb_get();
        if (frame) {
            queueNode_t *node = camera_queue_node_malloc(frame, type);
            if (node) {
                if (pdTRUE == xQueueSend(g_mdCamera.out, &node, 0)) {
                    count--;
                } else {
                    ESP_LOGW(TAG, "device BUSY, wait to try again");
                    camera_queue_node_free(node, EVENT_FAIL);
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
        if (count == 0) {
            break;
        }
    }
    if (count > 0) {
        ESP_LOGE(TAG, "snapshot fail, count=%d", count);
        g_mdCamera.bSnapShotSuccess = false;
    } else {
        g_mdCamera.bSnapShotSuccess = true;
    }
    // camera_flash_led_close();
    if (type == SNAP_TIMER) {
        sleep_set_last_capture_time(time(NULL));
    }
    ESP_LOGI(TAG, "camera_snapshot Stop");
    return ESP_OK;
}

esp_err_t camera_set_image(imgAttr_t *image, bool is_force)
{
    int ret = 0;
    if (image == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (CAMERA_USE_UVC != 1) {
        sensor_t *s = esp_camera_sensor_get();
        if (s == NULL) return ESP_ERR_INVALID_STATE;
        
        if (s->status.hmirror != image->bHorizonetal || is_force) {
            ret = s->set_hmirror(s, image->bHorizonetal);
            ESP_LOGI(TAG, "set_horizonetalt: %d, ret: %d", image->bHorizonetal, ret);
        }
        if (s->status.vflip != image->bVertical || is_force) {
            ret = s->set_vflip(s, image->bVertical);
            ESP_LOGI(TAG, "set_vertical: %d, ret: %d", image->bVertical, ret);
        }
        if (s->status.brightness != image->brightness || is_force) {
            ret = s->set_brightness(s, image->brightness);
            ESP_LOGI(TAG, "set_brightness: %d, ret: %d", image->brightness, ret);
        }
        if (s->status.contrast != image->contrast || is_force) {
            ret = s->set_contrast(s, image->contrast);
            ESP_LOGI(TAG, "set_contrast: %d, ret: %d", image->contrast, ret);
            
        }
        if (s->status.saturation != image->saturation || is_force) {
            ret = s->set_saturation(s, image->saturation);
            ESP_LOGI(TAG, "set_saturation: %d, ret: %d", image->saturation, ret);
        }
        // additional settings
        if (s->status.sharpness != image->sharpness || is_force) {
            ret = s->set_sharpness(s, image->sharpness);
            ESP_LOGI(TAG, "set_sharpness: %d, ret: %d", image->sharpness, ret);
        }
        if (s->status.denoise != image->denoise || is_force) {
            ret = s->set_denoise(s, image->denoise);
            ESP_LOGI(TAG, "set_denoise: %d, ret: %d", image->denoise, ret);
        }
        if (s->status.special_effect != image->specialEffect || is_force) {
            ret = s->set_special_effect(s, image->specialEffect);
            ESP_LOGI(TAG, "set_special_effect: %d, ret: %d", image->specialEffect, ret);
        }
        if (s->status.awb != image->bAwb || is_force) {
            ret = s->set_whitebal(s, image->bAwb);
            ESP_LOGI(TAG, "set_whitebal: %d, ret: %d", image->bAwb, ret);
        }
        if (s->status.awb_gain != image->bAwbGain || is_force) {
            ret = s->set_awb_gain(s, image->bAwbGain);
            ESP_LOGI(TAG, "set_awb_gain: %d, ret: %d", image->bAwbGain, ret);
        }
        if (s->status.wb_mode != image->wbMode || is_force) {
            ret = s->set_wb_mode(s, image->wbMode);
            ESP_LOGI(TAG, "set_wb_mode: %d, ret: %d", image->wbMode, ret);
        }
        if (s->status.aec != image->bAec || is_force) {
            ret = s->set_exposure_ctrl(s, image->bAec);
            ESP_LOGI(TAG, "set_exposure_ctrl: %d, ret: %d", image->bAec, ret);
        }
        if (s->status.aec2 != image->bAec2 || is_force) {
            ret = s->set_aec2(s, image->bAec2);
            ESP_LOGI(TAG, "set_aec2: %d, ret: %d", image->bAec2, ret);
        }
        if (s->status.ae_level != image->aeLevel || is_force) {
            ret = s->set_ae_level(s, image->aeLevel);
            ESP_LOGI(TAG, "set_ae_level: %d, ret: %d", image->aeLevel, ret);
        }
        // ESP_LOGI(TAG, "aec_value: %d, aecValue: %d", s->status.aec_value, image->aecValue);
        if (s->status.aec_value != image->aecValue || is_force) {
            ret = s->set_aec_value(s, image->aecValue);
            ESP_LOGI(TAG, "set_aec_value: %d, ret: %d", image->aecValue, ret);
        }
        if (s->status.agc != image->bAgc || is_force) {
            ret = s->set_gain_ctrl(s, image->bAgc);
            ESP_LOGI(TAG, "set_gain_ctrl: %d, ret: %d", image->bAgc, ret);
        }
        if (s->status.agc_gain != image->gain || is_force) {
            ret = s->set_agc_gain(s, image->gain);
            ESP_LOGI(TAG, "set_agc_gain: %d, ret: %d", image->gain, ret);
        }
        if (s->status.gainceiling != image->gainCeiling || is_force) {
            ret = s->set_gainceiling(s, image->gainCeiling);
            ESP_LOGI(TAG, "set_gainceiling: %d, ret: %d", image->gainCeiling, ret);
        }
        if (s->status.bpc != image->bBpc || is_force) {
            ret = s->set_bpc(s, image->bBpc);
            ESP_LOGI(TAG, "set_bpc: %d, ret: %d", image->bBpc, ret);
        }
        // ESP_LOGI(TAG, "wpc: %d, bWpc: %d", s->status.wpc, image->bWpc);
        if (s->status.wpc != image->bWpc || is_force) {
            ret = s->set_wpc(s, image->bWpc);
            ESP_LOGI(TAG, "set_wpc: %d, ret: %d", image->bWpc, ret);
        }
        if (s->status.raw_gma != image->bRawGma || is_force) {
            ret = s->set_raw_gma(s, image->bRawGma);
            ESP_LOGI(TAG, "set_raw_gma: %d, ret: %d", image->bRawGma, ret);
        }
        if (s->status.lenc != image->bLenc || is_force) {
            ret = s->set_lenc(s, image->bLenc);
            ESP_LOGI(TAG, "set_lenc: %d, ret: %d", image->bLenc, ret);
        }
        if (s->status.dcw != image->bDcw || is_force) {
            ret = s->set_dcw(s, image->bDcw);
            ESP_LOGI(TAG, "set_dcw: %d, ret: %d", image->bDcw, ret);
        }
        if (s->status.colorbar != image->bColorbar || is_force) {
            ret = s->set_colorbar(s, image->bColorbar);
            ESP_LOGI(TAG, "set_colorbar: %d, ret: %d", image->bColorbar, ret);
        }
    } else return ESP_ERR_NOT_SUPPORTED;

    // if (current.aeLevel != image->aeLevel) {
    //     s->set_ae_level(s, image->aeLevel);
    //     ESP_LOGI(TAG, "set_ae_level : %d", image->aeLevel);
    // }
    // if (current.bAgc != image->bAgc) {
    //     s->set_gain_ctrl(s, image->bAgc);
    //     ESP_LOGI(TAG, "set_agc : %d", image->bAgc);
    //     if (image->bAgc) {
    //         s->set_gainceiling(s, image->gainCeiling);
    //     } else {
    //         s->set_agc_gain(s, image->gain);
    //     }
    // }
    // if (current.gain != image->gain) {
    //     s->set_agc_gain(s, image->gain);
    //     ESP_LOGI(TAG, "set_gain : %d", image->gain);
    // }
    // if (current.gainCeiling != image->gainCeiling) {
    //     s->set_gainceiling(s, image->gainCeiling);
    //     ESP_LOGI(TAG, "set_gainceiling : %d", image->gainCeiling);
    // }

    return ESP_OK;
}

bool camera_is_snapshot_fail()
{
    return g_mdCamera.bSnapShot && !g_mdCamera.bSnapShotSuccess;
}
