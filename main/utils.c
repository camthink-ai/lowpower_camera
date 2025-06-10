#include "esp_err.h"
#include "esp_log.h"
#include "mbedtls/md5.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "utils.h"

#define TAG "-->UTILS"

uint8_t *mac_str2hex(const char *mac_str, uint8_t *mac_hex)
{
    uint32_t mac_data[6] = {0};

    sscanf(mac_str, "%02lX:%02lX:%02lX:%02lX:%02lX:%02lX",
           mac_data, mac_data + 1, mac_data + 2, mac_data + 3, mac_data + 4, mac_data + 5);

    for (int i = 0; i < 6; i++) {
        mac_hex[i] = mac_data[i];
    }

    return mac_hex;
}

char *mac_hex2str(const uint8_t *mac_hex, char *mac_str)
{
    sprintf(mac_str, "%02X:%02X:%02X:%02X:%02X:%02X",
            mac_hex[0], mac_hex[1], mac_hex[2], mac_hex[3], mac_hex[4], mac_hex[5]);
    return mac_str;
}

void misc_show_time(char *log, time_t t)
{
    char strftime_buf[64];
    struct tm timeinfo;

    localtime_r(&t, &timeinfo);
    strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
    ESP_LOGI(TAG, "%s: %s", log, strftime_buf);
}
#include <ctype.h>

bool is_valid_mac(const char *mac_str)
{
    int i = 0;
    int s = 0;
    while (*mac_str) {
        if (isxdigit((unsigned char)*mac_str)) {
            i++;
        } else if (*mac_str == ':' || *mac_str == '-') {

            if (i == 0 || i / 2 - 1 != s) {
                break;
            }
            ++s;
        } else {
            s = -1;
            return 0;
        }
        ++mac_str;
    }
    return (i == 12 && (s == 5 || s == 0));
}

char *replace_space(char *str, char ch)
{
    char *p = str;
    while (*p != '\0') {
        if (*p == ' ') {
            *p = ch;
        }
        p++;
    }
    return str;
}

uint64_t get_time_ms()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

int8_t get_timestamp(char *timestamp, int len)
{
    if (timestamp == NULL || len <= 0) {
        return -1;
    }
    struct timeval tv;
    gettimeofday(&tv, NULL);
    snprintf(timestamp, len + 1, "%013lld", tv.tv_sec * 1000 + tv.tv_usec / 1000);
    return 0;
}

void generate_random_string(char *str, size_t len)
{
    const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    size_t charset_length = strlen(charset);

    for (size_t i = 0; i < len; ++i) {
        int index = rand() % charset_length;
        str[i] = charset[index];
    }

    str[len] = '\0';
}

int md5_calc(const unsigned char *input, size_t ilen, unsigned char **output)
{
    if (input == NULL || ilen == 0 || output == NULL) {
        return -1;
    }
    uint8_t md5sum[17] = {0};
    if (mbedtls_md5(input, ilen, md5sum)) {
        return -1;
    }
    char *out_line = malloc(33);
    if (out_line == NULL) {
        return -1;
    }
    memset(out_line, 0, 33);
    for (int i = 0; i < 16; i++) {
        snprintf(out_line + i * 2, 3, "%02x", md5sum[i]);
    }
    *output = (unsigned char *)out_line;
    return 0;
}

int crc32_calc(const char *in, char *out)
{
    // TODO: CRC32 implementation placeholder - using MD5 instead for now
    return 0;
}

char *filesystem_read(const char *filename)
{
    FILE *file = fopen(filename, "r");
    if (file == NULL) {
        return NULL;
    }

    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    fseek(file, 0, SEEK_SET);
    char *buffer = malloc(length + 1);
    if (buffer == NULL) {
        fclose(file);
        return NULL;
    }

    fread(buffer, 1, length, file);
    buffer[length] = '\0';
    fclose(file);

    return buffer;
}

int filesystem_write(const char *filename, const char *data, size_t len)
{
    FILE *file = fopen(filename, "w");
    if (file == NULL) {
        return -1;
    }

    fwrite(data, 1, len, file);
    fclose(file);

    return 0;
}

int filesystem_dump(const char *filename)
{
    FILE *file = fopen(filename, "r");
    if (file == NULL) {
        return -1;
    }

    char buffer[1024];
    while (fgets(buffer, sizeof(buffer), file)) {
        printf("%s", buffer);
    }
    printf("\n");
    fclose(file);
    return 0;
}

bool filesystem_is_exist(const char *filename)
{
    FILE *file = fopen(filename, "r");
    if (file == NULL) {
        return false;
    }
    fclose(file);
    return true;
}

int filesystem_delete(const char *filename)
{
    if (remove(filename) != 0) {
        return -1;
    }
    return 0;
}

typedef struct taskHDL {
    TaskHandle_t xhandle;
    StackType_t *s_stack;
    StaticTask_t *s_task;
} taskHDL_t;

/* 
 * Create static task with memory allocated from SPIRAM to solve on-chip memory shortage
 * Note: For ESP IDF V5.1.0+, taskfunc must not contain SPI flash operations (SPI conflict)
 *       or it will cause exceptions (e.g. fopen, fread, fwrite etc.)
 */
void *task_create(void *taskfunc, const char *name, uint32_t stack_size, void *param, uint32_t prio, uint32_t core_id)
{
    taskHDL_t *handle = heap_caps_malloc(sizeof(taskHDL_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (handle == NULL) {
        ESP_LOGE(TAG, "malloc task handle failed");
        return NULL;
    }
    handle->s_task = heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (handle->s_task == NULL) {
        ESP_LOGE(TAG, "malloc task failed");
        free(handle);
        return NULL;
    }
    handle->s_stack = heap_caps_malloc(stack_size * sizeof(StackType_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (handle->s_stack == NULL) {
        ESP_LOGE(TAG, "malloc task stack failed");
        free(handle->s_task);
        free(handle);
        return NULL;
    }
    handle->xhandle = xTaskCreateStaticPinnedToCore((TaskFunction_t)taskfunc, name, stack_size, param, prio, handle->s_stack, handle->s_task, core_id);
    if (handle->xhandle == NULL) {
        ESP_LOGE(TAG, "create task failed");
        free(handle->s_task);
        free(handle->s_stack);
        free(handle);
        return NULL;
    }
    return (void *)handle;
}

void task_delete(void *handle)
{
    taskHDL_t *hdl = (taskHDL_t *)handle;
    if (hdl == NULL) {
        return;
    }
    if (hdl->s_task != NULL) {
        free(hdl->s_task);
    }
    if (hdl->s_stack != NULL) {
        free(hdl->s_stack);
    }
    free(hdl);
}
