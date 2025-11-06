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

typedef struct camera_vtable {
    const char *name;
	camera_fb_t *(*fb_get)(void);
	void (*fb_return)(camera_fb_t *fb);
	esp_err_t (*init)(void);
	void (*deinit)(void);
	esp_err_t (*set_image)(imgAttr_t *image);
} camera_vtable_t;

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
	const camera_vtable_t *vt;   // Backend vtable
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
static camera_fb_t *csi_fb_get(void)
{
    return esp_camera_fb_get();
}

/**
 * @brief Return frame buffer after processing
 * @param fb Pointer to frame buffer structure
 */
static void csi_fb_return(camera_fb_t *fb)
{
    esp_camera_fb_return(fb);
}

/**
 * @brief Free camera queue node
 * @param node Pointer to queue node
 * @param event Event type
 */
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
static camera_fb_t *uvc_fb_get(void)
{
	return uvc_stream_fb_get();
}

static void uvc_fb_return(camera_fb_t *fb)
{
	uvc_camera_fb_return(fb);
}

static esp_err_t csi_init(void)
{
    esp_err_t err;
    err = esp_camera_init(&camera_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "CSI Init Failed");
        return err;
    }
    imgAttr_t image;
    sensor_t *s = esp_camera_sensor_get();

    cfg_get_image_attr(&image);
    s->set_ae_level(s, image.aeLevel);
    s->set_gain_ctrl(s, 1);
    s->set_gainceiling(s, 0);
    s->set_hmirror(s, !image.bHorizonetal);
    s->set_vflip(s, image.bVertical);
    s->set_contrast(s, image.contrast);
    s->set_saturation(s, image.saturation);
    s->set_brightness(s, image.brightness);

    return ESP_OK;
}

static void csi_deinit(void)
{
    // esp_camera_deinit() intentionally omitted if not provided in SDK
}

static esp_err_t csi_set_image(imgAttr_t *image)
{
    imgAttr_t current;
    sensor_t *s = esp_camera_sensor_get();
    cfg_get_image_attr(&current);
    if (current.bHorizonetal != image->bHorizonetal) {
        s->set_hmirror(s, !image->bHorizonetal);
        ESP_LOGI(TAG, "set_horizonetalt : %d", image->bHorizonetal);
    }
    if (current.bVertical != image->bVertical) {
        s->set_vflip(s, image->bVertical);
        ESP_LOGI(TAG, "set_vertical : %d", image->bVertical);
    }
    if (current.brightness != image->brightness) {
        s->set_brightness(s, image->brightness);
        ESP_LOGI(TAG, "set_brightness : %d", image->brightness);
    }
    if (current.contrast != image->contrast) {
        s->set_contrast(s, image->contrast);
        ESP_LOGI(TAG, "set_contrast : %d", image->contrast);
    }
    if (current.saturation != image->saturation) {
        s->set_saturation(s, image->saturation);
        ESP_LOGI(TAG, "set_saturation : %d", image->saturation);
    }
    return ESP_OK;
}

static esp_err_t uvc_set_image(imgAttr_t *image)
{
    (void)image;
    return ESP_OK;
}

static const camera_vtable_t VTABLE_CSI = {
	.name = "CSI",
	.fb_get = csi_fb_get,
	.fb_return = csi_fb_return,
	.init = csi_init,
	.deinit = csi_deinit,
	.set_image = csi_set_image,
};

static const camera_vtable_t VTABLE_UVC = {
	.name = "USB",
	.fb_get = uvc_fb_get,
	.fb_return = uvc_fb_return,
	.init = uvc_init,
	.deinit = uvc_deinit,
	.set_image = uvc_set_image,
};

static esp_err_t init_camera(mdCamera_t *handle)
{
    // Try CSI first, then UVC
    if (VTABLE_CSI.init() == ESP_OK) {
        handle->vt = &VTABLE_CSI;
        return ESP_OK;
    }

    if (VTABLE_UVC.init() == ESP_OK) {
        handle->vt = &VTABLE_UVC;
        return ESP_OK;
    }

    handle->vt = NULL;
    ESP_LOGE(TAG, "Camera Init Failed (no backend)");
    return ESP_FAIL;
}

esp_err_t camera_open(QueueHandle_t in, QueueHandle_t out)
{
    struct mdCamera *handle = &g_mdCamera;
    if (system_get_mode() != MODE_CONFIG){
        lightAttr_t light;
        cfg_get_light_attr(&light);
        camera_flash_led_ctrl(&light);
    }
    if (ESP_OK != init_camera(handle)) {
        sleep_set_event_bits(SLEEP_SNAPSHOT_STOP_BIT); //如果后续无截图任务，将进入休眠；
        return ESP_FAIL;
    }
    handle->mutex = xSemaphoreCreateMutex();
    handle->in = in;
    handle->out = out;
    handle->eventGroup = xEventGroupCreate();
    handle->bInit = true;
    // wait for sensor stable with configurable delay
    capAttr_t capAttr;
    cfg_get_cap_attr(&capAttr);
    ESP_LOGI(TAG, "wait for sensor stable with configurable delay %d ms", (int)capAttr.camWarmupMs);
    vTaskDelay(pdMS_TO_TICKS(capAttr.camWarmupMs));
    sleep_set_event_bits(SLEEP_SNAPSHOT_STOP_BIT);          //如果后续无截图任务，将进入休眠；
    misc_get_battery_voltage();
    
    return ESP_OK;
}

esp_err_t camera_close()
{
    mdCamera_t *h = &g_mdCamera;
    if (!h->bInit) {
        return ESP_FAIL;
    }
    if (h->vt && h->vt->deinit) {
        h->vt->deinit();
    }
    misc_io_set(CAMERA_POWER_IO,  CAMERA_POWER_OFF);
    return ESP_OK;
}

/* removed legacy no-handle close */

esp_err_t camera_start()
{
    mdCamera_t *h = &g_mdCamera;
    if (!h->bInit) {
        return ESP_FAIL;
    }
    xEventGroupClearBits(h->eventGroup, CAMERA_STOP_BIT);
    xEventGroupSetBits(h->eventGroup, CAMERA_START_BIT);
    return ESP_OK;
}

/* removed legacy no-handle start */

esp_err_t camera_stop()
{
    mdCamera_t *h = &g_mdCamera;
    if (!h->bInit) {
        return ESP_FAIL;
    }
    xEventGroupClearBits(h->eventGroup, CAMERA_START_BIT);
    xEventGroupSetBits(h->eventGroup, CAMERA_STOP_BIT);
    return ESP_OK;
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

/* Control API stays non-handle: keep original implementation */
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
    mdCamera_t *h = &g_mdCamera;
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
    h->bSnapShot = true;
    int try_count = 5;
    while (try_count--) {
        camera_fb_t *frame = h->vt && h->vt->fb_get ? h->vt->fb_get() : NULL;
        if (frame) {
            queueNode_t *node = camera_queue_node_malloc(frame, type);
            if (node) {
                if (pdTRUE == xQueueSend(h->out, &node, 0)) {
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
        h->bSnapShotSuccess = false;
    } else {
        h->bSnapShotSuccess = true;
    }
    // camera_flash_led_close();
    if (type == SNAP_TIMER) {
        sleep_set_last_capture_time(time(NULL));
    }
    ESP_LOGI(TAG, "camera_snapshot Stop");
    return ESP_OK;
}

/* removed legacy no-handle snapshot */

esp_err_t camera_set_image(imgAttr_t *image)
{
    mdCamera_t *h = &g_mdCamera;
    return (h->vt && h->vt->set_image) ? h->vt->set_image(image) : ESP_OK;
}

bool camera_is_snapshot_fail()
{
    mdCamera_t *h = &g_mdCamera;
    return h->bSnapShot && !h->bSnapShotSuccess;
}

camera_fb_t *camera_fb_get()
{
    mdCamera_t *h = &g_mdCamera;
    if (!h->bInit || !h->vt) return NULL;
    return h->vt->fb_get();
}

void camera_fb_return(camera_fb_t *fb)
{
    mdCamera_t *h = &g_mdCamera;
    if (!h->bInit || !h->vt) return;
    h->vt->fb_return(fb);
}

const char *camera_get_backend_name()
{
    mdCamera_t *h = &g_mdCamera;
    return h->vt ? h->vt->name : "UNKNOWN";
}