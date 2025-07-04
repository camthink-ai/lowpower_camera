/*
 * SPDX-FileCopyrightText: 2022-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include <esp_timer.h>
#ifdef CONFIG_ESP32_S3_USB_OTG
#include "bsp/esp-bsp.h"
#endif
#include "uvc.h"

static const char *TAG = "UVC";

#define ENABLE_UVC_FRAME_RESOLUTION_ANY   1        /* Use any resolution supported by the camera */

#define BIT0_FRAME_START     (0x01 << 0)
#define BIT1_NEW_FRAME_START (0x01 << 1)
#define BIT2_NEW_FRAME_END   (0x01 << 2)

static EventGroupHandle_t s_evt_handle;

#if (ENABLE_UVC_FRAME_RESOLUTION_ANY)
#define DEMO_UVC_FRAME_WIDTH        FRAME_RESOLUTION_ANY
#define DEMO_UVC_FRAME_HEIGHT       FRAME_RESOLUTION_ANY
#else
#define DEMO_UVC_FRAME_WIDTH        1280
#define DEMO_UVC_FRAME_HEIGHT       720
#endif

#define DEMO_UVC_XFER_BUFFER_SIZE (1024 * 1024)

#define FRAME_XFER_DIV 3          /* Frame transfer interval divisor */
#define FRAME_SAVE_CNT 1          /* Number of frames to buffer */
#define UVC_CON_TIMEOUT 10        /* Connection timeout in seconds */

static camera_fb_t s_fb = {0};

static camera_fb_t cap_fb[FRAME_SAVE_CNT] = {0};
static int frame_index = 0;

/**
 * @brief Extract JPEG resolution from buffer data
 * @param buf Pointer to JPEG data buffer
 * @param size Size of JPEG data buffer
 */
int readJPEGResolutionFromBuffer(const unsigned char *buf, size_t size) 
{
    int foundSOF = 0;
    size_t i = 0;

    while (i < size - 1) {
        // Check for SOF0 marker (0xFFC0)
        if (buf[i] == 0xFF && buf[i + 1] == 0xC0) {
            foundSOF = 1;
            break;
        }
        i++;
    }

    if (foundSOF) {
        // Skip SOF0 marker (2 bytes), segment length (2 bytes), and data precision (1 byte)
        i += 5;

        // Extract image height (2 bytes big-endian)
        int height = (buf[i] << 8) + buf[i + 1];
        i += 2;

        // Extract image width (2 bytes big-endian)
        int width = (buf[i] << 8) + buf[i + 1];

        ESP_LOGV(TAG,"Image resolution: %d x %d\n", width, height);
        return 0;
    } else {
        ESP_LOGI(TAG,"No valid SOF0 marker found, invalid JPEG data\n");
        return -1;
    }
}

/**
 * @brief Get frame buffer for streaming
 * @return Pointer to frame buffer structure
 */
camera_fb_t *uvc_stream_fb_get()
{
    xEventGroupSetBits(s_evt_handle, BIT0_FRAME_START);
    xEventGroupWaitBits(s_evt_handle, BIT1_NEW_FRAME_START, true, true, portMAX_DELAY);
    return &s_fb;
}

/**
 * @brief Get captured frame buffer
 * @return Pointer to captured frame buffer or NULL
 */
camera_fb_t *uvc_capture_fb_get()
{
    static int capture_index = 0;
    if(capture_index >= FRAME_SAVE_CNT || capture_index > frame_index)
        return NULL;
    ESP_LOGI(TAG,"capture index:%d sent\n",capture_index);
    return &cap_fb[capture_index++];
}

/**
 * @brief Return frame buffer after processing
 * @param fb Pointer to frame buffer structure
 */
void uvc_camera_fb_return(camera_fb_t *fb)
{
    xEventGroupSetBits(s_evt_handle, BIT2_NEW_FRAME_END);
    return;
}

/**
 * @brief UVC frame callback handler
 * @param frame Received frame data
 * @param ptr User context pointer
 */
static void uvc_frame_cb(uvc_frame_t *frame, void *ptr)
{
    static int retry = 0;
    ESP_LOGV(TAG, "uvc callback! frame_format = %d, seq = %"PRIu32", width = %"PRIu32", height = %"PRIu32", length = %u, ptr = %d",
             frame->frame_format, frame->sequence, frame->width, frame->height, frame->data_bytes, (int) ptr);

    // Frame capture logic (currently commented)
    // if(frame->width == 1920 && frame->height == 1080){
        if(frame_index < FRAME_SAVE_CNT){
            if(cap_fb[frame_index].buf == NULL){
                cap_fb[frame_index].buf = (uint8_t *)malloc(frame->data_bytes);
            }
            memcpy(cap_fb[frame_index].buf, frame->data, frame->data_bytes);
            cap_fb[frame_index].len = frame->data_bytes;
            cap_fb[frame_index].width = frame->width;
            cap_fb[frame_index].height = frame->height;
            cap_fb[frame_index].format = PIXFORMAT_JPEG;
            cap_fb[frame_index].timestamp.tv_sec = frame->sequence;
        }
    // }
    frame_index++;

    if (!(xEventGroupGetBits(s_evt_handle) & BIT0_FRAME_START)) {
        return;
    }

    switch (frame->frame_format) {
    case UVC_FRAME_FORMAT_MJPEG:
        // Frame filtering example (currently commented)
        // if(frame->width == 1920 && frame->height == 1080){
        //     if(frame_index % FRAME_XFER_DIV == 0)
        //         break;
        // }

        s_fb.buf = frame->data;
        s_fb.len = frame->data_bytes;
        s_fb.width = frame->width;
        s_fb.height = frame->height;
        s_fb.format = PIXFORMAT_JPEG;
        s_fb.timestamp.tv_sec = frame->sequence;
        if(readJPEGResolutionFromBuffer(s_fb.buf, s_fb.len) != 0){
            if(retry < 3){
                retry++;
                return;
            }
        }
        retry = 0;
        xEventGroupSetBits(s_evt_handle, BIT1_NEW_FRAME_START);
        ESP_LOGV(TAG, "send frame = %"PRIu32"", frame->sequence);
        xEventGroupWaitBits(s_evt_handle, BIT2_NEW_FRAME_END, true, true, portMAX_DELAY);
        ESP_LOGV(TAG, "send frame done = %"PRIu32"", frame->sequence);
        break;
    default:
        ESP_LOGW(TAG, "Unsupported format");
        assert(0);
        break;
    }
    ESP_LOGV(TAG, "uvc callback end!");
}

/**
 * @brief Handle stream state changes
 * @param event Stream state event
 * @param arg User context pointer
 */
static void stream_state_changed_cb(usb_stream_state_t event, void *arg)
{
    switch (event) {
    case STREAM_CONNECTED: {
        size_t frame_size = 0;
        size_t frame_index = 0;
        uvc_frame_size_list_get(NULL, &frame_size, &frame_index);
        if (frame_size) {
            ESP_LOGI(TAG, "UVC: Frame list size = %u, current index = %u", frame_size, frame_index);
            uvc_frame_size_t *uvc_frame_list = (uvc_frame_size_t *)malloc(frame_size * sizeof(uvc_frame_size_t));
            uvc_frame_size_list_get(uvc_frame_list, NULL, NULL);
            for (size_t i = 0; i < frame_size; i++) {
                ESP_LOGI(TAG, "\tframe[%u] = %ux%u", i, uvc_frame_list[i].width, uvc_frame_list[i].height);
            }
            free(uvc_frame_list);
        } else {
            ESP_LOGW(TAG, "UVC: Empty frame list");
        }
        ESP_LOGI(TAG, "Device connected");
        break;
    }
    case STREAM_DISCONNECTED:
        ESP_LOGI(TAG, "Device disconnected");
        break;
    default:
        ESP_LOGE(TAG, "Unknown event");
        break;
    }
}

/**
 * @brief Initialize UVC subsystem
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t uvc_init(void)
{
#ifdef CONFIG_ESP32_S3_USB_OTG
    bsp_usb_mode_select_host();
    bsp_usb_host_power_mode(BSP_USB_HOST_POWER_MODE_USB_DEV, true);
#endif
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("httpd_txrx", ESP_LOG_INFO);
    esp_err_t ret = ESP_FAIL;
    s_evt_handle = xEventGroupCreate();
    if (s_evt_handle == NULL) {
        ESP_LOGE(TAG, "line-%u: Event group creation failed", __LINE__);
        return ESP_FAIL;
    }

    /* Allocate transfer buffers */
    uint8_t *xfer_buffer_a = (uint8_t *)malloc(DEMO_UVC_XFER_BUFFER_SIZE);
    uint8_t *xfer_buffer_b = (uint8_t *)malloc(DEMO_UVC_XFER_BUFFER_SIZE);
    uint8_t *frame_buffer = (uint8_t *)malloc(DEMO_UVC_XFER_BUFFER_SIZE);

    if(xfer_buffer_a == NULL || xfer_buffer_b == NULL || frame_buffer == NULL){
        ESP_LOGE(TAG, "line-%u: Memory allocation failed", __LINE__);
        return ESP_FAIL;
    }

    uvc_config_t uvc_config = {
        /* Use first available resolution if ENABLE_UVC_FRAME_RESOLUTION_ANY is set */
        .frame_width = DEMO_UVC_FRAME_WIDTH,
        .frame_height = DEMO_UVC_FRAME_HEIGHT,
        .frame_interval = FPS2INTERVAL(15),
        .xfer_buffer_size = DEMO_UVC_XFER_BUFFER_SIZE,
        .xfer_buffer_a = xfer_buffer_a,
        .xfer_buffer_b = xfer_buffer_b,
        .frame_buffer_size = DEMO_UVC_XFER_BUFFER_SIZE,
        .frame_buffer = frame_buffer,
        .frame_cb = &uvc_frame_cb,
        .frame_cb_arg = NULL,
    };

    ret = uvc_streaming_config(&uvc_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "UVC streaming config failed");
        return ESP_FAIL;
    }

    ret = usb_streaming_state_register(&stream_state_changed_cb, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "UVC state callback registration failed");
        return ESP_FAIL;
    }

    ret = usb_streaming_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "UVC streaming start failed");
        return ESP_FAIL;
    }

    ret = usb_streaming_connect_wait((UVC_CON_TIMEOUT * 1000) / portTICK_PERIOD_MS); 
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "UVC connection timeout");
        return ESP_FAIL;
    }
    return ESP_OK;
}

/**
 * @brief Deinitialize UVC subsystem
 * @return Always returns ESP_OK
 */
esp_err_t uvc_deinit(void)
{
    // usb_streaming_control(STREAM_UVC, CTRL_SUSPEND, NULL);
    // usb_streaming_stop();
    return ESP_OK;
}
