/* SPIFFS filesystem example.
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include "esp_err.h"
#include "esp_log.h"
#include "esp_vfs.h"
// #include "esp_spiffs.h"
#include "esp_littlefs.h"
#include "utils.h"
#include "storage.h"
#include "sleep.h"
#include "misc.h"
#include "debug.h"

#define STORAGE_UPLOAD_START_BIT BIT(0)
#define STORAGE_UPLOAD_STOP_BIT BIT(1)
#define STORAGE_UPLOAD_DONE_BIT BIT(2)
#define STORAGE_UPLOAD_DONE_TIMEOUT_MS  (30000) // 30s
#define PATH_MAX_lEN (266)


#define TAG "-->STROAGE"

typedef struct mdStorage {
    EventGroupHandle_t eventGroup;
    QueueHandle_t in;
    QueueHandle_t out;
    SemaphoreHandle_t mutex;
} mdStorage_t;

static mdStorage_t g_mdStorage;

static void storage_queue_node_free(queueNode_t *node, nodeEvent_e event)
{
    if (event != EVENT_OK) {
        xEventGroupSetBits(g_mdStorage.eventGroup, STORAGE_UPLOAD_STOP_BIT);
    } else {
        xEventGroupSetBits(g_mdStorage.eventGroup, STORAGE_UPLOAD_DONE_BIT);
    }
    if (node) {
        free(node->data);
        free(node);
        ESP_LOGI(TAG, "storage_queue_node_free");

    }
}

static queueNode_t *storage_queue_node_malloc(void *data, size_t len, uint64_t pts, snapType_e type)
{
    queueNode_t *node = calloc(1, sizeof(queueNode_t));
    if (node) {
        node->from = FROM_STORAGE;
        node->pts = pts;
        node->type = type;
        node->data = data;
        node->len = len;
        node->free_handler = storage_queue_node_free;
        ESP_LOGI(TAG, "storage_queue_node_malloc");
        return node;
    }
    ESP_LOGW(TAG, "storage_queue_node_malloc FAILED");
    return NULL;
}

static size_t storage_free_space()
{
    size_t total = 0, used = 0;
    if (esp_littlefs_info(STORAGE_PART, &total, &used) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get SPIFFS partition information");
    } else {
        ESP_LOGI(TAG, "space :total :%d used :%d", total, used);
        return (total - used);
    }
    return 0;
}

void storage_show_file()
{
    time_t pts;
    char type;
    uint32_t num = 0;
    struct dirent *entry;
    struct stat fstat;
    char filename[32];
    DIR *dir = opendir(STORAGE_ROOT);

    while ((entry = readdir(dir)) != NULL) {
        if (sscanf(entry->d_name, "%c%lld.jpg", &type, &pts) == 2) {
            sprintf(filename, "%s/%c%lld.jpg", STORAGE_ROOT, type, pts);
            stat(filename, &fstat);
            ESP_LOGI(TAG, "------ %s(type %c, pts %lld size %ld)", entry->d_name, type, pts, fstat.st_size);
            num++;
        }
    }
    ESP_LOGI(TAG, "Total files: %ld", num);
    storage_free_space();
    closedir(dir);
}

static esp_err_t storage_rm_oldest_file(char *root)
{
    time_t pts = 0;
    time_t tmp = 0;
    char type;
    struct dirent *entry;
    char path[PATH_MAX_lEN];
    DIR *dir = opendir(root);

    while ((entry = readdir(dir)) != NULL) {
        if (sscanf(entry->d_name, "%c%lld.jpg", &type, &pts) == 2) {
            if (tmp == 0) {
                tmp = pts;
                sprintf(path, "%s/%s", root, entry->d_name);
            }
            if (tmp > pts) {
                tmp = pts;
                sprintf(path, "%s/%s", root, entry->d_name);
            }
        }
    }
    if (tmp) {
        ESP_LOGI(TAG, "Removing %s", path);
        unlink(path);
        closedir(dir);
        return ESP_OK;
    }
    closedir(dir);
    return ESP_FAIL;
}

static void storage_write_file(void *data, size_t len, uint64_t pts, snapType_e type)
{
    char filename[32];
    while (storage_free_space() <  len * 5) {
        if (storage_rm_oldest_file(STORAGE_ROOT) != ESP_OK) {
            break;
        }
    }
    sprintf(filename, "%s/%c%llu.jpg", STORAGE_ROOT, type, pts);
    FILE *f = fopen(filename, "w");
    if (f) {
        int res = fwrite(data, len, 1, f);
        if (res != 1) {
            ESP_LOGE(TAG, "Failed to write %s err %d", filename, res);
        }
        fclose(f);
        ESP_LOGI(TAG, "Success to save %s size %d", filename, len);
    } else {
        ESP_LOGE(TAG, "Failed to open %s", filename);
    }
}

static esp_err_t storage_upload_file(char *filename, uint64_t pts, snapType_e type)
{
    struct stat fstat;
    FILE *f = NULL;
    queueNode_t *node = NULL;
    void *data = NULL;

    stat(filename, &fstat);
    if (fstat.st_size == 0) {
        ESP_LOGE(TAG, "invalid file size %ld, delete", fstat.st_size);
        unlink(filename);
        return ESP_FAIL;
    }
    f = fopen(filename, "r");
    if (f) {
        void *data = malloc(fstat.st_size);
        if (data) {
            fread(data, 1, fstat.st_size, f);
            node = storage_queue_node_malloc(data, fstat.st_size, pts, type);
            if (node) {
                xQueueSend(g_mdStorage.out, &node, portMAX_DELAY);
                fclose(f);
                return ESP_OK;
            }
        }
    }
    if (data) {
        free(data);
    }
    if (f) {
        fclose(f);
    }
    return ESP_FAIL;
}

static void task(mdStorage_t *self)
{
    ESP_LOGI(TAG, "task Start");
    while (true) {
        queueNode_t *node;
        if (xQueueReceive(self->in, &node, portMAX_DELAY)) {
            if (node->from == FROM_CAMERA) {
                // write_to_flash();
                xSemaphoreTake(self->mutex, portMAX_DELAY);
                storage_write_file(node->data, node->len, node->pts, node->type);
                xSemaphoreGive(self->mutex);
                ESP_LOGI(TAG, "SAVE TO FLASH");
                node->free_handler(node, EVENT_OK);
            } else if (node->from == FROM_STORAGE)  {
                ESP_LOGI(TAG, "IS SELF");
                node->free_handler(node, EVENT_FAIL);
            }
        }
    }
    ESP_LOGI(TAG, "Stop");
    vTaskDelete(NULL);
}

static void upload(mdStorage_t *self)
{
    EventBits_t uxBits;
    char path[PATH_MAX_lEN];
    uint64_t pts;
    char type;

    ESP_LOGI(TAG, "upload Start");
    while (true) {
        sleep_set_event_bits(SLEEP_STORAGE_UPLOAD_STOP_BIT); // 如果flash里无剩余待上传图片，将进入休眠
        xEventGroupWaitBits(self->eventGroup, STORAGE_UPLOAD_START_BIT, true, true, portMAX_DELAY);
        sleep_clear_event_bits(SLEEP_STORAGE_UPLOAD_STOP_BIT);
        struct dirent *entry;
        DIR *dir = opendir(STORAGE_ROOT);
        while ((entry = readdir(dir)) != NULL) {
            if (sscanf(entry->d_name, "%c%llu.jpg", &type, &pts) != 2) {
                // ESP_LOGW(TAG, "invalid file %s", entry->d_name);
                continue;
            }
            sprintf(path, "%s/%s", STORAGE_ROOT, entry->d_name);
            ESP_LOGI(TAG, "upload file %s", path);
            xSemaphoreTake(self->mutex, portMAX_DELAY);
            if (storage_upload_file(path, pts, type) != ESP_OK) {
                xSemaphoreGive(self->mutex);
                continue;
            }
            xSemaphoreGive(self->mutex);
            uxBits = xEventGroupWaitBits(self->eventGroup, STORAGE_UPLOAD_DONE_BIT |
                                         STORAGE_UPLOAD_STOP_BIT, true, false,
                                         pdMS_TO_TICKS(STORAGE_UPLOAD_DONE_TIMEOUT_MS));
            if (uxBits & STORAGE_UPLOAD_DONE_BIT) {
                unlink(path);
                ESP_LOGI(TAG, "unlink file %s", path);
                continue;
            } else {
                ESP_LOGI(TAG, "stop upload");
                break;
            }
        }
        ESP_LOGI(TAG, "upload nothing");
        closedir(dir);
        xEventGroupSetBits(self->eventGroup, STORAGE_UPLOAD_STOP_BIT);
    }
    ESP_LOGI(TAG, "Stop");
    vTaskDelete(NULL);
}

static int do_tf_cmd(int argc, char **argv)
{
    storage_sd_check();
    return ESP_OK;
}

static esp_console_cmd_t g_cmd[] = {
    {"tf", "show TF card status", NULL, do_tf_cmd, NULL},
};

void storage_upload_start()
{
    ESP_LOGI(TAG, "storage_upload_start");
    xEventGroupClearBits(g_mdStorage.eventGroup, STORAGE_UPLOAD_STOP_BIT);
    xEventGroupSetBits(g_mdStorage.eventGroup, STORAGE_UPLOAD_START_BIT);
}

void storage_upload_stop()
{
    ESP_LOGI(TAG, "storage_upload_stop");
    xEventGroupClearBits(g_mdStorage.eventGroup, STORAGE_UPLOAD_START_BIT);
    xEventGroupSetBits(g_mdStorage.eventGroup, STORAGE_UPLOAD_STOP_BIT);
}

void storage_format()
{
    xSemaphoreTake(g_mdStorage.mutex, portMAX_DELAY);
    ESP_LOGI(TAG, "storage_format ...");
    if (esp_littlefs_format(STORAGE_PART) != ESP_OK) {
        ESP_LOGE(TAG, "format failed");
    } else {
        ESP_LOGI(TAG, "format successfully");
    }
    xSemaphoreGive(g_mdStorage.mutex);
}

void storage_open(QueueHandle_t in, QueueHandle_t out)
{
    ESP_LOGI(TAG, "Initializing SPIFFS");
    memset(&g_mdStorage, 0, sizeof(g_mdStorage));

    esp_vfs_littlefs_conf_t conf = {
        .base_path = STORAGE_ROOT,
        .partition_label = STORAGE_PART,
        // .max_files = 5,
        .format_if_mount_failed = true,
        .dont_mount = false,
    };
    esp_err_t ret = esp_vfs_littlefs_register(&conf);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find SPIFFS partition");
        } else {
            ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
        return;
    }
    g_mdStorage.in = in;
    g_mdStorage.out = out;
    g_mdStorage.eventGroup = xEventGroupCreate();
    g_mdStorage.mutex = xSemaphoreCreateMutex();
    xTaskCreatePinnedToCore((TaskFunction_t)task, "record", 4 * 1024, &g_mdStorage, 4, NULL, 0);
    xTaskCreatePinnedToCore((TaskFunction_t)upload, "upload", 4 * 1024, &g_mdStorage, 4, NULL, 1);
    debug_cmd_add(g_cmd, sizeof(g_cmd) / sizeof(esp_console_cmd_t));
}

void storage_close()
{
    // storage_sd_check();
}
/* SD card and FAT filesystem example.
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

// This example uses SDMMC peripheral to communicate with SD card.
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#define MOUNT_POINT "/sdcard"

void storage_sd_check(void)
{
    esp_err_t ret;
    misc_io_cfg(TF_POWER_IO, 0, 1);
    misc_io_set(TF_POWER_IO, TF_POWER_ON);
    // Options for mounting the filesystem.
    // If format_if_mount_failed is set to true, SD card will be partitioned and
    // formatted in case when mounting fails.
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
#ifdef CONFIG_EXAMPLE_FORMAT_IF_MOUNT_FAILED
        .format_if_mount_failed = true,
#else
        .format_if_mount_failed = true,
#endif // EXAMPLE_FORMAT_IF_MOUNT_FAILED
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };
    sdmmc_card_t *card;
    const char mount_point[] = MOUNT_POINT;
    ESP_LOGI(TAG, "Initializing SD card");

    // Use settings defined above to initialize SD card and mount FAT filesystem.
    // Note: esp_vfs_fat_sdmmc/sdspi_mount is all-in-one convenience functions.
    // Please check its source code and implement error recovery when developing
    // production applications.

    ESP_LOGI(TAG, "Using SDMMC peripheral");

    // By default, SD card frequency is initialized to SDMMC_FREQ_DEFAULT (20MHz)
    // For setting a specific frequency, use host.max_freq_khz (range 400kHz - 40MHz for SDMMC)
    // Example: for fixed frequency of 10MHz, use host.max_freq_khz = 10000;
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();

    // This initializes the slot without card detect (CD) and write protect (WP) signals.
    // Modify slot_config.gpio_cd and slot_config.gpio_wp if your board has these signals.
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();

    // Set bus width to use:
    slot_config.width = 1;

    // On chips where the GPIOs used for SD card can be configured, set them in
    // the slot_config structure:
    slot_config.clk = 39;
    slot_config.cmd = 38;
    slot_config.d0 = 40;
    // Enable internal pullups on enabled pins. The internal pullups
    // are insufficient however, please make sure 10k external pullups are
    // connected on the bus. This is for debug / example purpose only.
    // slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    ESP_LOGI(TAG, "Mounting filesystem");
    ret = esp_vfs_fat_sdmmc_mount(mount_point, &host, &slot_config, &mount_config, &card);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount filesystem. "
                     "If you want the card to be formatted, set the EXAMPLE_FORMAT_IF_MOUNT_FAILED menuconfig option.");
        } else {
            ESP_LOGE(TAG, "Failed to initialize the card (%s). "
                     "Make sure SD card lines have pull-up resistors in place.", esp_err_to_name(ret));
        }
        return;
    }
    ESP_LOGI(TAG, "Filesystem mounted");

    // Card has been initialized, print its properties
    sdmmc_card_print_info(stdout, card);
    esp_vfs_fat_sdcard_unmount(mount_point, card);
    ESP_LOGI(TAG, "Card unmounted");
}
