#include "config.h"
#include "debug.h"
#include "system.h"
#include "sleep.h"
#include "s2j.h"
#include "iniparser.h"
#include "utils.h"
#include "cat1.h"
#include "camera.h"

#define NVS_CFG_UNDEFINED "undefined"
#define NVS_CFG_PARTITION "cfg"
#define NVS_USER_NAMESPACE "userspace"
#define NVS_FACTORY_NAMESPACE "factoryspace"
#define TAG "-->CONFIG"

// Handles for NVS namespaces
static nvs_handle_t g_userHandle = 0;      ///< Handle for user configuration namespace
static nvs_handle_t g_factoryHandle = 0;   ///< Handle for factory configuration namespace  
static SemaphoreHandle_t g_mutex = 0;      ///< Mutex for thread-safe access

/**
 * Create configuration mutex
 * Initializes the mutex used for thread-safe configuration access
 */
static void mutex_create(void)
{
    g_mutex = xSemaphoreCreateMutex();
}

/**
 * Free configuration mutex
 * Releases the mutex resources
 */
static void mutex_free(void)
{
    vSemaphoreDelete(g_mutex);
}

/**
 * Lock configuration mutex
 * Acquires the mutex for thread-safe operations
 */
static void mutex_lock(void)
{
    if (g_mutex) {
        xSemaphoreTake(g_mutex, portMAX_DELAY);
    }
}

/**
 * Unlock configuration mutex 
 * Releases the mutex after operations complete
 */
static void mutex_unlock(void)
{
    if (g_mutex) {
        xSemaphoreGive(g_mutex);
    }
}

/**
 * Commit configuration changes to NVS
 * @param handle NVS namespace handle
 * @return ESP_OK on success, error code otherwise
 */
static esp_err_t commit_cfg(nvs_handle_t handle)
{
    esp_err_t err = ESP_OK;
    err = nvs_commit(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "commit failed");
    }
    return err;
}

/**
 * Open an NVS namespace
 * @param namespace Name of the namespace to open
 * @param handle Output parameter for namespace handle
 * @return ESP_OK on success, error code otherwise
 */
static esp_err_t namespace_open(const char *namespace, nvs_handle_t *handle)
{
    esp_err_t err = ESP_OK;

    if (handle == NULL || namespace == NULL) {
        ESP_LOGE(TAG, "Open, invalid nvs param");
        return ESP_ERR_INVALID_ARG;
    }

    err = nvs_open_from_partition(NVS_CFG_PARTITION, namespace, NVS_READWRITE, handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Open, nvs_open failed, err %d", err);
        return err;
    }

    ESP_LOGI(TAG, "Open namespace done, name \"%s\"", namespace);
    return err;
}

/**
 * Close an NVS namespace
 * @param handle Namespace handle to close
 */
static void namespace_close(nvs_handle_t handle)
{
    nvs_close(handle);
}

// static esp_err_t user_erase_key(nvs_handle_t handle, const char *key)
// {
//     esp_err_t err = ESP_OK;
//     err = nvs_erase_key(handle, key);
//     if (err != ESP_OK) {
//         ESP_LOGE(TAG, "Failed to erase key[%s] (err %d)", key, err);
//     }
//     return err;
// }

// static esp_err_t get_i32(nvs_handle_t handle, const char *key, int32_t *value, int32_t def)
// {
//     esp_err_t err = ESP_OK;
//     err = nvs_get_i32(handle, key, value);
//     if (err != ESP_OK) {
//         *value = def;
//     }
//     return err;
// }

// static esp_err_t set_i32(nvs_handle_t handle, const char *key, int32_t value)
// {
//     esp_err_t err = ESP_OK;
//     err = nvs_set_i32(handle, key, value);
//     if (err != ESP_OK) {
//         ESP_LOGE(TAG, "set key:%s value:%d failed", key, value);

//     }
//     return err;
// }

// static esp_err_t get_u32(nvs_handle_t handle, const char *key, uint32_t *value, uint32_t def)
// {
//     esp_err_t err = ESP_OK;
//     err = nvs_get_u32(handle, key, value);
//     if (err != ESP_OK) {
//         *value = def;
//     }
//     return err;
// }

// static esp_err_t set_u32(nvs_handle_t handle, const char *key, uint32_t value)
// {
//     esp_err_t err = ESP_OK;
//     err = nvs_set_u32(handle, key, value);
//     if (err != ESP_OK) {
//         ESP_LOGE(TAG, "set key:%s value:%d failed", key, value);
//     }
//     return err;
// }

// static esp_err_t get_u8(nvs_handle_t handle, const char *key, uint8_t *value, uint8_t def)
// {
//     esp_err_t err = ESP_OK;
//     err = nvs_get_u8(handle, key, value);
//     if (err != ESP_OK) {
//         *value = def;
//     }
//     return err;
// }

// static esp_err_t set_u8(nvs_handle_t handle, const char *key, uint8_t value)
// {
//     esp_err_t err = ESP_OK;
//     err = nvs_set_u8(handle, key, value);
//     if (err != ESP_OK) {
//         ESP_LOGE(TAG, "set key:%s value:%d failed[%s]", key, value, esp_err_to_name(err));
//     }
//     return err;
// }

// static esp_err_t get_i8(nvs_handle_t handle, const char *key, int8_t *value, int8_t def)
// {
//     esp_err_t err = ESP_OK;
//     err = nvs_get_i8(handle, key, value);
//     if (err != ESP_OK) {
//         *value = def;
//     }
//     return err;
// }

// static esp_err_t set_i8(nvs_handle_t handle, const char *key, int8_t value)
// {
//     esp_err_t err = ESP_OK;
//     err = nvs_set_i8(handle, key, value);
//     if (err != ESP_OK) {
//         ESP_LOGE(TAG, "set key:%s value:%d failed", key, value);
//     }
//     return err;
// }

static esp_err_t get_u32(nvs_handle_t handle, const char *key, uint32_t *value, uint32_t def)
{
    esp_err_t err = ESP_OK;
    char out_value[32] = {0};
    size_t len = sizeof(out_value);

    err = nvs_get_str(handle, key, out_value, &len);
    if (err != ESP_OK) {
        *value = def;
    } else {
        *value = (uint32_t)strtoul(out_value, NULL, 10);
    }
    return err;
}

static esp_err_t set_u32(nvs_handle_t handle, const char *key, uint32_t value)
{
    esp_err_t err = ESP_OK;
    char in_value[32] = {0};

    snprintf(in_value, sizeof(in_value), "%lu", value);
    err = nvs_set_str(handle, key, in_value);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set key:%s value:%ld failed", key, value);
    }
    return err;
}

static esp_err_t get_i32(nvs_handle_t handle, const char *key, int32_t *value, int32_t def)
{
    esp_err_t err = ESP_OK;
    char out_value[32] = {0};
    size_t len = sizeof(out_value);

    err = nvs_get_str(handle, key, out_value, &len);
    if (err != ESP_OK) {
        *value = def;
    } else {
        *value = (int32_t)strtol(out_value, NULL, 10);
    }
    return err;
}

static esp_err_t set_i32(nvs_handle_t handle, const char *key, int32_t value)
{
    esp_err_t err = ESP_OK;
    char in_value[32] = {0};

    snprintf(in_value, sizeof(in_value), "%ld", value);
    err = nvs_set_str(handle, key, in_value);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set key:%s value:%ld failed", key, value);
    }
    return err;
}

static esp_err_t get_u8(nvs_handle_t handle, const char *key, uint8_t *value, uint8_t def)
{
    esp_err_t err = ESP_OK;
    char out_value[32] = {0};
    size_t len = sizeof(out_value);

    err = nvs_get_str(handle, key, out_value, &len);
    if (err != ESP_OK) {
        *value = def;
    } else {
        *value = (uint8_t)strtoul(out_value, NULL, 10);
    }
    return err;
}

static esp_err_t get_u16(nvs_handle_t handle, const char *key, uint16_t *value, uint16_t def)
{
    esp_err_t err = ESP_OK;
    char out_value[32] = {0};
    size_t len = sizeof(out_value);

    err = nvs_get_str(handle, key, out_value, &len);
    if (err != ESP_OK) {
        *value = def;
    } else {
        *value = (uint16_t)strtoul(out_value, NULL, 10);
    }
    return err;
}

static esp_err_t set_u8(nvs_handle_t handle, const char *key, uint8_t value)
{
    esp_err_t err = ESP_OK;
    char in_value[32] = {0};

    snprintf(in_value, sizeof(in_value), "%u", value);
    err = nvs_set_str(handle, key, in_value);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set key:%s value:%d failed", key, value);
    }
    return err;
}

static esp_err_t set_u16(nvs_handle_t handle, const char *key, uint16_t value)
{
    esp_err_t err = ESP_OK;
    char in_value[32] = {0};

    snprintf(in_value, sizeof(in_value), "%u", value);
    err = nvs_set_str(handle, key, in_value);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set key:%s value:%d failed", key, value);
    }
    return err;
}

static esp_err_t get_i8(nvs_handle_t handle, const char *key, int8_t *value, int8_t def)
{
    esp_err_t err = ESP_OK;
    char out_value[32] = {0};
    size_t len = sizeof(out_value);

    err = nvs_get_str(handle, key, out_value, &len);
    if (err != ESP_OK) {
        *value = def;
    } else {
        *value = (int8_t)strtol(out_value, NULL, 10);
    }
    return err;
}

static esp_err_t set_i8(nvs_handle_t handle, const char *key, int8_t value)
{
    esp_err_t err = ESP_OK;
    char in_value[32] = {0};

    snprintf(in_value, sizeof(in_value), "%d", value);
    err = nvs_set_str(handle, key, in_value);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set key:%s value:%d failed", key, value);
    }
    return err;
}

static esp_err_t get_str(nvs_handle_t handle, const char *key, char *value, size_t length, const char *def)
{
    esp_err_t err = ESP_OK;
    size_t len = length;

    err = nvs_get_str(handle, key, value, &len);
    if (err != ESP_OK) {
        if (def) {
            strncpy(value, def, length);
        }
    }
    return err;
}

static esp_err_t set_str(nvs_handle_t handle, const char *key, const char *value)
{
    esp_err_t err = ESP_OK;
    err = nvs_set_str(handle, key, value);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set key:%s value:%s failed", key, value);
    }
    return err;
}

static void print_blob(const char *blob, size_t len)
{
    for (int i = 0; i < len; i++) {
        printf("%02x", blob[i]);
    }
    printf("\n");
}

static esp_err_t get_value_from_nvs(const char *namespace, const char *key, nvs_type_t type)
{
    nvs_handle_t nvs;
    esp_err_t err;

    if (type == NVS_TYPE_ANY) {
        ESP_LOGE(TAG, "Type '%d' is undefined", type);
        return ESP_ERR_NVS_TYPE_MISMATCH;
    }

    err = nvs_open_from_partition(NVS_CFG_PARTITION, namespace, NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open %s failed", namespace);
        return err;
    }

    if (type == NVS_TYPE_I8) {
        int8_t value;
        err = nvs_get_i8(nvs, key, &value);
        if (err == ESP_OK) {
            printf("%d\n", value);
        }
    } else if (type == NVS_TYPE_U8) {
        uint8_t value;
        err = nvs_get_u8(nvs, key, &value);
        if (err == ESP_OK) {
            printf("%u\n", value);
        }
    } else if (type == NVS_TYPE_I16) {
        int16_t value;
        err = nvs_get_i16(nvs, key, &value);
        if (err == ESP_OK) {
            printf("%u\n", value);
        }
    } else if (type == NVS_TYPE_U16) {
        uint16_t value;
        if ((err = nvs_get_u16(nvs, key, &value)) == ESP_OK) {
            printf("%u\n", value);
        }
    } else if (type == NVS_TYPE_I32) {
        int32_t value;
        if ((err = nvs_get_i32(nvs, key, &value)) == ESP_OK) {
            printf("%ld\n", value);
        }
    } else if (type == NVS_TYPE_U32) {
        uint32_t value;
        if ((err = nvs_get_u32(nvs, key, &value)) == ESP_OK) {
            printf("%lu\n", value);
        }
    } else if (type == NVS_TYPE_I64) {
        int64_t value;
        if ((err = nvs_get_i64(nvs, key, &value)) == ESP_OK) {
            printf("%lld\n", value);
        }
    } else if (type == NVS_TYPE_U64) {
        uint64_t value;
        if ((err = nvs_get_u64(nvs, key, &value)) == ESP_OK) {
            printf("%llu\n", value);
        }
    } else if (type == NVS_TYPE_STR) {
        size_t len;
        if ((err = nvs_get_str(nvs, key, NULL, &len)) == ESP_OK) {
            char *str = (char *)malloc(len);
            if ((err = nvs_get_str(nvs, key, str, &len)) == ESP_OK) {
                printf("%s\n", str);
            }
            free(str);
        }
    } else if (type == NVS_TYPE_BLOB) {
        size_t len;
        if ((err = nvs_get_blob(nvs, key, NULL, &len)) == ESP_OK) {
            char *blob = (char *)malloc(len);
            if ((err = nvs_get_blob(nvs, key, blob, &len)) == ESP_OK) {
                print_blob(blob, len);
            }
            free(blob);
        }
    }
    nvs_close(nvs);
    return err;
}

void cfg_set_u8(const char *key, uint8_t value)
{
    mutex_lock();
    set_u8(g_userHandle, key, value);
    commit_cfg(g_userHandle);
    mutex_unlock();
}

void cfg_set_i8(const char *key, int8_t value)
{
    mutex_lock();
    set_i8(g_userHandle, key, value);
    commit_cfg(g_userHandle);
    mutex_unlock();
}

void cfg_set_u32(const char *key, uint32_t value)
{
    mutex_lock();
    set_u32(g_userHandle, key, value);
    commit_cfg(g_userHandle);
    mutex_unlock();
}

void cfg_set_str(const char *key, const char *value)
{
    mutex_lock();
    set_str(g_userHandle, key, value);
    commit_cfg(g_userHandle);
    mutex_unlock();
}

void cfg_get_u8(const char *key, uint8_t *value, uint8_t def)
{
    mutex_lock();
    get_u8(g_userHandle, key, value, def);
    mutex_unlock();
}

void cfg_get_i8(const char *key, int8_t *value, int8_t def)
{
    mutex_lock();
    get_i8(g_userHandle, key, value, def);
    mutex_unlock();
}

void cfg_get_u32(const char *key, uint32_t *value, uint32_t def)
{
    mutex_lock();
    get_u32(g_userHandle, key, value, def);
    mutex_unlock();
}

void cfg_get_str(const char *key, char *value, size_t length, const char *def)
{
    mutex_lock();
    get_str(g_userHandle, key, value, length, def);
    mutex_unlock();
}

void cfg_erase_key(const char *key)
{
    mutex_lock();
    nvs_erase_key(g_userHandle, key);
    commit_cfg(g_userHandle);
    mutex_unlock();
}

void cfg_dump()
{
    nvs_iterator_t it = NULL;
    esp_err_t ret = nvs_entry_find(NVS_CFG_PARTITION, NULL, NVS_TYPE_ANY, &it);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "No such entry was found");
        return;
    }
    while (ret == ESP_OK) {
        nvs_entry_info_t info;
        nvs_entry_info(it, &info);
        printf("%s: %s = ", info.namespace_name, info.key);
        get_value_from_nvs(info.namespace_name, info.key, info.type);
        printf("\n");
        ret = nvs_entry_next(&it);
    }
    nvs_release_iterator(it);

    nvs_stats_t nvs_stats;
    nvs_get_stats(NULL, &nvs_stats);
    printf("Count: UsedEntries = (%d), FreeEntries = (%d), AllEntries = (%d)\n",
           nvs_stats.used_entries, nvs_stats.free_entries, nvs_stats.total_entries);
}

/*--------------------------debug--------------------------------------*/
static int do_fset_cmd(int argc, char **argv)
{
    const char *key[] = {KEY_DEVICE_MAC, KEY_DEVICE_SN, KEY_DEVICE_HVER, KEY_DEVICE_MODEL, KEY_DEVICE_COUNTRY, KEY_DEVICE_SECRETKEY};
    uint8_t i, n;
    n = sizeof(key) / sizeof(key[0]);

    if (argc < 2) {
        goto USAGE;
    }
    for (i = 0; i < n; i++) {
        if (strcmp(argv[1], key[i]) == 0) {
            break;
        }
    }
    if (i < n) {
        mutex_lock();
        if (argc == 2) {
            printf("erase %s\n", key[i]);
            nvs_erase_key(g_factoryHandle, key[i]);
        } else {
            set_str(g_factoryHandle, key[i], argv[2]);
        }
        commit_cfg(g_factoryHandle);
        mutex_unlock();
    } else {
        goto USAGE;
    }

    return ESP_OK;
USAGE:
    printf("invalid argvment, use these please:\n");
    for (i = 0; i < n; i++) {
        printf("\tfset %s xxx\n", key[i]);
    }
    return ESP_OK;
}

static int do_fget_cmd(int argc, char **argv)
{
    char value[32];

    if (argc < 2) {
        cfg_dump();
        return ESP_OK;
    }
    mutex_lock();
    get_str(g_factoryHandle, argv[1], value, sizeof(value), "no find");
    printf("%s\n", value);
    mutex_unlock();
    return ESP_OK;
}

static int do_reboot_cmd(int argc, char **argv)
{
    system_restart();
    return ESP_OK;
}

static int do_sleep_cmd(int argc, char **argv)
{
    sleep_start();
    return ESP_OK;
}

static int do_version_cmd(int argc, char **argv)
{
    printf("%s\n", system_get_version());
    return ESP_OK;
}

static int do_schetime_cmd(int argc, char **argv)
{
    if (argc < 2) {
        printf("invalid argvment, eg: schedule 03:00:00\n");
        return ESP_OK;
    }
    cfg_set_schedule_time(argv[1]);
    return ESP_OK;
}

static int do_cat1_cmd(int argc, char **argv)
{
    cat1_show_status();
    return ESP_OK;
}

static int do_tz_cmd(int argc, char **argv)
{
    if (argc < 2) {
        printf("invalid argvment, eg: tz GMT+8\n");
        return ESP_OK;
    }
    timeAttr_t tAttr;
    system_get_time(&tAttr);
    strncpy(tAttr.tz, argv[1], sizeof(tAttr.tz));
    system_set_time(&tAttr);
    return ESP_OK;
}

static int do_date_cmd(int argc, char **argv)
{
    timeAttr_t tAttr;
    system_get_time(&tAttr);
    //显示时区和本地时间
    misc_show_time(tAttr.tz, tAttr.ts);
    return ESP_OK;
}

static int do_rpsurl_cmd(int argc, char **argv)
{
    if (argc < 2) {
        cfg_erase_key(KEY_IOT_RPS_URL);
        printf("rps url has been erased\n");
        return ESP_OK;
    }
    cfg_set_str(KEY_IOT_RPS_URL, argv[1]);
    return ESP_OK;
}

static esp_console_cmd_t g_cmd[] = {
    {"fset", "factory setting: fset [key] [value]", NULL, do_fset_cmd, NULL},
    {"fget", "factory getting: fget [key]", NULL, do_fget_cmd, NULL},
    {"reboot", "system restart", NULL, do_reboot_cmd, NULL},
    {"sleep", "system sleep", NULL, do_sleep_cmd, NULL},
    {"version", "system software version", NULL, do_version_cmd, NULL},
    {"schedule", "set schedule time, default 03:00:00", NULL, do_schetime_cmd, NULL},
    {"cat1", "cat1 status", NULL, do_cat1_cmd, NULL},
    {"tz", "set time zone", NULL, do_tz_cmd, NULL},
    {"date", "show system date", NULL, do_date_cmd, NULL},
    {"rpsurl", "set rps url", NULL, do_rpsurl_cmd, NULL},
};

/*------------------------------------------------------------------------*/
esp_err_t cfg_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    err = nvs_flash_init_partition(NVS_CFG_PARTITION);
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase_partition(NVS_CFG_PARTITION));
        err = nvs_flash_init_partition(NVS_CFG_PARTITION);
    }
    ESP_ERROR_CHECK(err);
    err = namespace_open(NVS_USER_NAMESPACE, &g_userHandle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Open %s failed", NVS_USER_NAMESPACE);
        return err;
    }
    err = namespace_open(NVS_FACTORY_NAMESPACE, &g_factoryHandle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Open %s failed", NVS_FACTORY_NAMESPACE);
        return err;
    }
    mutex_create();
    
    char tz[32];
    cfg_get_timezone(tz);
    system_set_timezone(tz);
    debug_cmd_add(g_cmd, sizeof(g_cmd) / sizeof(esp_console_cmd_t));
    return err;
}

esp_err_t cfg_deinit()
{
    namespace_close(g_userHandle);
    namespace_close(g_factoryHandle);
    mutex_free();
    return nvs_flash_deinit();
}

bool cfg_is_undefined(char *str)
{
    return strcmp(str, NVS_CFG_UNDEFINED) == 0;
}

esp_err_t cfg_get_device_info(deviceInfo_t *device)
{
    mutex_lock();
    memset(device, 0, sizeof(deviceInfo_t));
    get_str(g_userHandle, KEY_DEVICE_NAME, device->name, sizeof(device->name), "NE101 Sensing Camera");
    get_str(g_factoryHandle, KEY_DEVICE_MAC, device->mac, sizeof(device->mac), NULL);
    get_str(g_factoryHandle, KEY_DEVICE_SN, device->sn, sizeof(device->sn), NVS_CFG_UNDEFINED);
    get_str(g_factoryHandle, KEY_DEVICE_HVER, device->hardVersion, sizeof(device->hardVersion), "V1.0");
    strncpy(device->softVersion, system_get_version(), sizeof(device->softVersion));
    get_str(g_factoryHandle, KEY_DEVICE_MODEL, device->model, sizeof(device->model), "NE101");
    get_str(g_factoryHandle, KEY_DEVICE_SECRETKEY, device->secretKey, sizeof(device->secretKey), NVS_CFG_UNDEFINED);
    if (get_str(g_userHandle, KEY_DEVICE_COUNTRY, device->countryCode, sizeof(device->countryCode), NULL) != ESP_OK ||
        strlen(device->countryCode) != 2) {
        get_str(g_factoryHandle, KEY_DEVICE_COUNTRY, device->countryCode, sizeof(device->countryCode), "US");
    }
    if(CAMERA_USE_UVC){
        strncpy(device->camera, "USB",sizeof(device->camera));
    }else{
        strncpy(device->camera, "CSI",sizeof(device->camera));
    }
    get_str(g_userHandle, KEY_DEVICE_NETMOD, device->netmod, sizeof(device->netmod), "");
    mutex_unlock();
    return ESP_OK;
}
esp_err_t cfg_set_device_info(deviceInfo_t *device)
{
    mutex_lock();
    set_str(g_userHandle, KEY_DEVICE_NAME, device->name);
    set_str(g_userHandle, KEY_DEVICE_COUNTRY, device->countryCode);
    set_str(g_userHandle, KEY_DEVICE_NETMOD, device->netmod);
    commit_cfg(g_userHandle);
    set_str(g_factoryHandle, KEY_DEVICE_MAC, device->mac);
    set_str(g_factoryHandle, KEY_DEVICE_SN, device->sn);
    set_str(g_factoryHandle, KEY_DEVICE_HVER, device->hardVersion);
    set_str(g_factoryHandle, KEY_DEVICE_MODEL, device->model);
    commit_cfg(g_factoryHandle);
    mutex_unlock();
    return ESP_OK;
}

esp_err_t cfg_get_image_attr(imgAttr_t *image)
{
    sensor_t *s = esp_camera_sensor_get();
    if (s == NULL) {
        ESP_LOGE(TAG, "Camera sensor not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    mutex_lock();
    memset(image, 0, sizeof(imgAttr_t));
    get_i8(g_userHandle, KEY_IMG_BRIGHTNESS, &image->brightness, s->status.brightness);
    get_i8(g_userHandle, KEY_IMG_CONTRAST, &image->contrast, s->status.contrast);
    get_i8(g_userHandle, KEY_IMG_SATURATION, &image->saturation, s->status.saturation);
    get_i8(g_userHandle, KEY_IMG_AELEVEL, &image->aeLevel, s->status.ae_level);
    get_u8(g_userHandle, KEY_IMG_AGC, &image->bAgc, s->status.agc);
    get_u8(g_userHandle, KEY_IMG_GAIN, &image->gain, s->status.agc_gain);
    get_u8(g_userHandle, KEY_IMG_GAINCEILING, &image->gainCeiling, s->status.gainceiling);
    get_u8(g_userHandle, KEY_IMG_HOR, &image->bHorizonetal, s->status.hmirror);
    get_u8(g_userHandle, KEY_IMG_VER, &image->bVertical, s->status.vflip);
    // additional attributes can be added here
    get_i8(g_userHandle, KEY_IMG_SHARPNESS, &image->sharpness, s->status.sharpness);
    get_u8(g_userHandle, KEY_IMG_QUALITY, &image->quality, s->status.quality);
    get_u8(g_userHandle, KEY_IMG_DENOISE, &image->denoise, s->status.denoise);
    get_u8(g_userHandle, KEY_IMG_EFFECT, &image->specialEffect, s->status.special_effect);
    get_u8(g_userHandle, KEY_IMG_AWB, &image->bAwb, s->status.awb);
    get_u8(g_userHandle, KEY_IMG_AWB_GAIN, &image->bAwbGain, s->status.awb_gain);
    get_u8(g_userHandle, KEY_IMG_WB_MODE, &image->wbMode, s->status.wb_mode);
    get_u8(g_userHandle, KEY_IMG_AEC, &image->bAec, s->status.aec);
    get_u8(g_userHandle, KEY_IMG_AEC2, &image->bAec2, s->status.aec2);
    get_u16(g_userHandle, KEY_IMG_AEC_VALUE, &image->aecValue, s->status.aec_value);
    get_u8(g_userHandle, KEY_IMG_BPC, &image->bBpc, s->status.bpc);
    get_u8(g_userHandle, KEY_IMG_WPC, &image->bWpc, s->status.wpc);
    get_u8(g_userHandle, KEY_IMG_RAW_GMA, &image->bRawGma, s->status.raw_gma);
    get_u8(g_userHandle, KEY_IMG_LENC, &image->bLenc, s->status.lenc);
    get_u8(g_userHandle, KEY_IMG_DCW, &image->bDcw, s->status.dcw);
    get_u8(g_userHandle, KEY_IMG_COLORBAR, &image->bColorbar, s->status.colorbar);

    mutex_unlock();
    return ESP_OK;
}

esp_err_t cfg_set_image_attr(imgAttr_t *image)
{
    mutex_lock();
    set_i8(g_userHandle, KEY_IMG_BRIGHTNESS, image->brightness);
    set_i8(g_userHandle, KEY_IMG_CONTRAST, image->contrast);
    set_i8(g_userHandle, KEY_IMG_SATURATION, image->saturation);
    set_i8(g_userHandle, KEY_IMG_AELEVEL, image->aeLevel);
    set_u8(g_userHandle, KEY_IMG_AGC, image->bAgc);
    set_u8(g_userHandle, KEY_IMG_GAIN, image->gain);
    set_u8(g_userHandle, KEY_IMG_GAINCEILING, image->gainCeiling);
    set_u8(g_userHandle, KEY_IMG_HOR, image->bHorizonetal);
    set_u8(g_userHandle, KEY_IMG_VER, image->bVertical);
    // additional attributes can be added here
    set_i8(g_userHandle, KEY_IMG_SHARPNESS, image->sharpness);
    set_u8(g_userHandle, KEY_IMG_QUALITY, image->quality);
    set_u8(g_userHandle, KEY_IMG_DENOISE, image->denoise);
    set_u8(g_userHandle, KEY_IMG_EFFECT, image->specialEffect);
    set_u8(g_userHandle, KEY_IMG_AWB, image->bAwb);
    set_u8(g_userHandle, KEY_IMG_AWB_GAIN, image->bAwbGain);
    set_u8(g_userHandle, KEY_IMG_WB_MODE, image->wbMode);
    set_u8(g_userHandle, KEY_IMG_AEC, image->bAec);
    set_u8(g_userHandle, KEY_IMG_AEC2, image->bAec2);
    set_u16(g_userHandle, KEY_IMG_AEC_VALUE, image->aecValue);
    set_u8(g_userHandle, KEY_IMG_BPC, image->bBpc);
    set_u8(g_userHandle, KEY_IMG_WPC, image->bWpc);
    set_u8(g_userHandle, KEY_IMG_RAW_GMA, image->bRawGma);
    set_u8(g_userHandle, KEY_IMG_LENC, image->bLenc);
    set_u8(g_userHandle, KEY_IMG_DCW, image->bDcw);
    set_u8(g_userHandle, KEY_IMG_COLORBAR, image->bColorbar);
    commit_cfg(g_userHandle);
    mutex_unlock();
    return ESP_OK;
}

esp_err_t cfg_get_light_attr(lightAttr_t *light)
{
    mutex_lock();
    memset(light, 0, sizeof(lightAttr_t));
    get_u8(g_userHandle, KEY_LIGHT_MODE, &light->lightMode, 0);
    get_u8(g_userHandle, KEY_LIGHT_THRESHOLD, &light->threshold, 55);
    get_u8(g_userHandle, KEY_LIGHT_DUTY, &light->duty, 50);
    get_str(g_userHandle, KEY_LIGHT_STIME, light->startTime, sizeof(light->startTime), "23:00");
    get_str(g_userHandle, KEY_LIGHT_ETINE, light->endTime, sizeof(light->endTime), "07:00");
    mutex_unlock();
    return ESP_OK;
}

esp_err_t cfg_set_light_attr(lightAttr_t *light)
{
    mutex_lock();
    set_u8(g_userHandle, KEY_LIGHT_MODE, light->lightMode);
    set_u8(g_userHandle, KEY_LIGHT_THRESHOLD, light->threshold);
    set_u8(g_userHandle, KEY_LIGHT_DUTY, light->duty);
    set_str(g_userHandle, KEY_LIGHT_STIME, light->startTime);
    set_str(g_userHandle, KEY_LIGHT_ETINE, light->endTime);
    commit_cfg(g_userHandle);
    mutex_unlock();
    return ESP_OK;
}

esp_err_t cfg_get_cap_attr(capAttr_t *capture)
{
    mutex_lock();
    memset(capture, 0, sizeof(capAttr_t));
    get_u8(g_userHandle, KEY_CAP_SCHE, &capture->bScheCap, 0);
    get_u8(g_userHandle, KEY_CAP_ALARMIN, &capture->bAlarmInCap, 1);
    get_u8(g_userHandle, KEY_CAP_BUTTON, &capture->bButtonCap, 1);
    get_u8(g_userHandle, KEY_CAP_MODE, &capture->scheCapMode, 0);
    get_u8(g_userHandle, KEY_CAP_TIME_COUNT, &capture->timedCount, 0);
    get_u32(g_userHandle, KEY_CAP_INTERVAL_V, &capture->intervalValue, 8);
    get_u8(g_userHandle, KEY_CAP_INTERVAL_U, &capture->intervalUnit, 1);
    char key[32];
    for (size_t i = 0; i < capture->timedCount; i++) {
        if (i >= sizeof(capture->timedNodes) / sizeof(capture->timedNodes[0])) {
            break;
        }
        sprintf(key, "cap:t%d.day", i);
        get_u8(g_userHandle, key, &capture->timedNodes[i].day, 0);
        sprintf(key, "cap:t%d.time", i);
        get_str(g_userHandle, key, capture->timedNodes[i].time, sizeof(capture->timedNodes[i].time), "00:00:00");
    }
    mutex_unlock();
    return ESP_OK;
}

esp_err_t cfg_set_cap_attr(capAttr_t *capture)
{
    mutex_lock();
    set_u8(g_userHandle, KEY_CAP_SCHE, capture->bScheCap);
    set_u8(g_userHandle, KEY_CAP_ALARMIN, capture->bAlarmInCap);
    set_u8(g_userHandle, KEY_CAP_BUTTON, capture->bButtonCap);
    set_u8(g_userHandle, KEY_CAP_MODE, capture->scheCapMode);
    set_u8(g_userHandle, KEY_CAP_TIME_COUNT, capture->timedCount);
    set_u32(g_userHandle, KEY_CAP_INTERVAL_V, capture->intervalValue);
    set_u8(g_userHandle, KEY_CAP_INTERVAL_U, capture->intervalUnit);
    char key[32];
    for (size_t i = 0; i < capture->timedCount; i++) {
        if (i >= sizeof(capture->timedNodes) / sizeof(capture->timedNodes[0])) {
            break;
        }
        sprintf(key, "cap:t%d.day", i);
        set_u8(g_userHandle, key, capture->timedNodes[i].day);
        sprintf(key, "cap:t%d.time", i);
        set_str(g_userHandle, key, capture->timedNodes[i].time);
    }
    commit_cfg(g_userHandle);
    mutex_unlock();
    return ESP_OK;
}

esp_err_t cfg_get_mqtt_attr(mqttAttr_t *mqtt)
{
    platformParamAttr_t platformParam;
    cfg_get_platform_param_attr(&platformParam);

    mutex_lock();

    memset(mqtt, 0, sizeof(mqttAttr_t));
    switch (platformParam.currentPlatformType) {
        case PLATFORM_TYPE_SENSING:
            snprintf(mqtt->host, sizeof(mqtt->host), "%s", platformParam.sensingPlatform.host);
            snprintf(mqtt->topic, sizeof(mqtt->topic), "%s", platformParam.sensingPlatform.topic);
            snprintf(mqtt->user, sizeof(mqtt->user), "%s", platformParam.sensingPlatform.username);
            snprintf(mqtt->password, sizeof(mqtt->password), "%s", platformParam.sensingPlatform.password);
            snprintf(mqtt->clientId, sizeof(mqtt->clientId), "%s", platformParam.sensingPlatform.clientId);
            mqtt->port = platformParam.sensingPlatform.mqttPort;
            mqtt->qos = platformParam.sensingPlatform.qos;
            mqtt->httpPort = platformParam.sensingPlatform.httpPort;
            break;
        case PLATFORM_TYPE_MQTT:
            snprintf(mqtt->host, sizeof(mqtt->host), "%s", platformParam.mqttPlatform.host);
            snprintf(mqtt->topic, sizeof(mqtt->topic), "%s", platformParam.mqttPlatform.topic);
            snprintf(mqtt->user, sizeof(mqtt->user), "%s", platformParam.mqttPlatform.username);
            snprintf(mqtt->password, sizeof(mqtt->password), "%s", platformParam.mqttPlatform.password);
            snprintf(mqtt->clientId, sizeof(mqtt->clientId), "%s", platformParam.mqttPlatform.clientId);
            mqtt->port = platformParam.mqttPlatform.mqttPort;
            mqtt->qos = platformParam.mqttPlatform.qos;
            mqtt->httpPort = 5220;
            break;
        default:
            break;
    }
    mutex_unlock();
    return ESP_OK;
}

esp_err_t cfg_set_mqtt_attr(mqttAttr_t *mqtt)
{
    mutex_lock();
    set_u32(g_userHandle, KEY_MQTT_PORT, mqtt->port);
    set_str(g_userHandle, KEY_MQTT_HOST, mqtt->host);
    set_str(g_userHandle, KEY_MQTT_TOPIC, mqtt->topic);
    set_str(g_userHandle, KEY_MQTT_USER, mqtt->user);
    set_str(g_userHandle, KEY_MQTT_PASSWORD, mqtt->password);
    commit_cfg(g_userHandle);
    mutex_unlock();
    return ESP_OK;
}

esp_err_t cfg_get_wifi_attr(wifiAttr_t *wifi)
{
    mutex_lock();
    memset(wifi, 0, sizeof(wifiAttr_t));
    get_str(g_userHandle, KEY_WIFI_SSID, wifi->ssid, sizeof(wifi->ssid), NVS_CFG_UNDEFINED);
    get_str(g_userHandle, KEY_WIFI_PASSWORD, wifi->password, sizeof(wifi->password), NULL);
    mutex_unlock();
    return ESP_OK;
}

esp_err_t cfg_set_wifi_attr(wifiAttr_t *wifi)
{
    mutex_lock();
    set_str(g_userHandle, KEY_WIFI_SSID, wifi->ssid);
    set_str(g_userHandle, KEY_WIFI_PASSWORD, wifi->password);
    commit_cfg(g_userHandle);
    mutex_unlock();
    return ESP_OK;
}

esp_err_t cfg_get_iot_attr(IoTAttr_t *iot)
{
    deviceInfo_t device;
    cfg_get_device_info(&device);

    mutex_lock();
    memset(iot, 0, sizeof(IoTAttr_t));
    if (cfg_is_undefined(device.secretKey)) {
        get_u8(g_userHandle, KEY_IOT_AUTOP, &iot->autop_enable, 0);
        get_u8(g_userHandle, KEY_IOT_DM, &iot->dm_enable, 0);
    } else {
        get_u8(g_userHandle, KEY_IOT_AUTOP, &iot->autop_enable, 1);
        get_u8(g_userHandle, KEY_IOT_DM, &iot->dm_enable, 1);
    }
    get_u8(g_userHandle, KEY_IOT_AUTOP_DONE, &iot->autop_done, 0);
    get_u8(g_userHandle, KEY_IOT_DM_DONE, &iot->dm_done, 0);
    mutex_unlock();
    return ESP_OK;
}

esp_err_t cfg_set_iot_attr(IoTAttr_t *iot)
{
    mutex_lock();
    set_u8(g_userHandle, KEY_IOT_AUTOP, iot->autop_enable);
    set_u8(g_userHandle, KEY_IOT_DM, iot->dm_enable);
    set_u8(g_userHandle, KEY_IOT_AUTOP_DONE, iot->autop_done);
    set_u8(g_userHandle, KEY_IOT_DM_DONE, iot->dm_done);
    commit_cfg(g_userHandle);
    mutex_unlock();
    return ESP_OK;
}

esp_err_t cfg_get_platform_param_attr(platformParamAttr_t *platform)
{
    deviceInfo_t device;
    cfg_get_device_info(&device);

    mutex_lock();

    memset(platform, 0, sizeof(platformParamAttr_t));
    get_u8(g_userHandle, KEY_PLATFORM_TYPE, &platform->currentPlatformType, 0);

    platform->sensingPlatform.platformType = 0;
    snprintf(platform->sensingPlatform.platformName, sizeof(platform->sensingPlatform.platformName), "%s",
             "Sensing Platform");
    get_str(g_userHandle, KEY_MQTT_HOST, platform->sensingPlatform.host, sizeof(platform->sensingPlatform.host), "");
    get_u32(g_userHandle, KEY_MQTT_PORT, &platform->sensingPlatform.mqttPort, 1883);
    get_u32(g_userHandle, KEY_SNS_HTTP_PORT, &platform->sensingPlatform.httpPort, 5220);
    snprintf(platform->sensingPlatform.topic, sizeof(platform->sensingPlatform.topic), "%s", "v1/devices/me/telemetry");
    snprintf(platform->sensingPlatform.username, sizeof(platform->sensingPlatform.username), "%s", device.sn);
    platform->sensingPlatform.qos = 1;

    platform->mqttPlatform.platformType = 1;
    snprintf(platform->mqttPlatform.platformName, sizeof(platform->mqttPlatform.platformName), "%s", "Other MQTT Platform");
    get_str(g_userHandle, KEY_MQTT_HOST, platform->mqttPlatform.host, sizeof(platform->mqttPlatform.host), "");
    get_u32(g_userHandle, KEY_MQTT_PORT, &platform->mqttPlatform.mqttPort, 1883);
    get_str(g_userHandle, KEY_MQTT_TOPIC, platform->mqttPlatform.topic, sizeof(platform->mqttPlatform.topic),
            "NE101SensingCam/Snapshot");
    get_str(g_userHandle, KEY_MQTT_CLIENT_ID, platform->mqttPlatform.clientId, sizeof(platform->mqttPlatform.clientId), "");
    get_u8(g_userHandle, KEY_MQTT_QOS, &platform->mqttPlatform.qos, 1);
    get_str(g_userHandle, KEY_MQTT_USER, platform->mqttPlatform.username, sizeof(platform->mqttPlatform.username), "");
    get_str(g_userHandle, KEY_MQTT_PASSWORD, platform->mqttPlatform.password, sizeof(platform->mqttPlatform.password), "");

    mutex_unlock();

    return ESP_OK;
}

esp_err_t cfg_set_platform_param_attr(platformParamAttr_t *platform)
{
    mutex_lock();
    set_u8(g_userHandle, KEY_PLATFORM_TYPE, platform->currentPlatformType);
    switch (platform->currentPlatformType) {
        case PLATFORM_TYPE_SENSING: {
            set_str(g_userHandle, KEY_MQTT_HOST, platform->sensingPlatform.host);
            set_u32(g_userHandle, KEY_MQTT_PORT, platform->sensingPlatform.mqttPort);
            set_u32(g_userHandle, KEY_SNS_HTTP_PORT, platform->sensingPlatform.httpPort);
            break;
        }
        case PLATFORM_TYPE_MQTT: {
            set_str(g_userHandle, KEY_MQTT_HOST, platform->mqttPlatform.host);
            set_u32(g_userHandle, KEY_MQTT_PORT, platform->mqttPlatform.mqttPort);
            set_str(g_userHandle, KEY_MQTT_TOPIC, platform->mqttPlatform.topic);
            // 如果client id为空，则随机生成一个23位的字符串
            if (strlen(platform->mqttPlatform.clientId) == 0) {
                char id[24] = {0};
                generate_random_string(id, sizeof(id) - 1);
                snprintf(platform->mqttPlatform.clientId, sizeof(platform->mqttPlatform.clientId), "%s", id);
            }
            set_str(g_userHandle, KEY_MQTT_CLIENT_ID, platform->mqttPlatform.clientId);
            set_u8(g_userHandle, KEY_MQTT_QOS, platform->mqttPlatform.qos);
            set_str(g_userHandle, KEY_MQTT_USER, platform->mqttPlatform.username);
            set_str(g_userHandle, KEY_MQTT_PASSWORD, platform->mqttPlatform.password);
            break;
        }
        default:
            break;
    }
    commit_cfg(g_userHandle);
    mutex_unlock();
    return ESP_OK;
}

esp_err_t cfg_get_cellular_param_attr(cellularParamAttr_t *cellularParam)
{
    mutex_lock();
    memset(cellularParam, 0, sizeof(cellularParamAttr_t));
    get_str(g_userHandle, KEY_CAT1_IMEI, cellularParam->imei, sizeof(cellularParam->imei), "");
    get_str(g_userHandle, KEY_CAT1_APN, cellularParam->apn, sizeof(cellularParam->apn), "");
    get_str(g_userHandle, KEY_CAT1_USER, cellularParam->user, sizeof(cellularParam->user), "");
    get_str(g_userHandle, KEY_CAT1_PASSWORD, cellularParam->password, sizeof(cellularParam->password), "");
    get_str(g_userHandle, KEY_CAT1_PIN, cellularParam->pin, sizeof(cellularParam->pin), "");
    get_u8(g_userHandle, KEY_CAT1_AUTH_TYPE, &cellularParam->authentication, 0);
    mutex_unlock();
    return ESP_OK;
}

esp_err_t cfg_set_cellular_param_attr(cellularParamAttr_t *cellularParam)
{
    mutex_lock();
    set_str(g_userHandle, KEY_CAT1_APN, cellularParam->apn);
    set_str(g_userHandle, KEY_CAT1_USER, cellularParam->user);
    set_str(g_userHandle, KEY_CAT1_PASSWORD, cellularParam->password);
    set_str(g_userHandle, KEY_CAT1_PIN, cellularParam->pin);
    set_u8(g_userHandle, KEY_CAT1_AUTH_TYPE, cellularParam->authentication);
    commit_cfg(g_userHandle);
    mutex_unlock();
    return ESP_OK;
}

esp_err_t cfg_get_cellular_baud_rate(uint32_t *baudRate)
{
    mutex_lock();
    get_u32(g_factoryHandle, KEY_CAT1_BAUD_RATE, baudRate, 0);
    mutex_unlock();
    return ESP_OK;
}

esp_err_t cfg_set_cellular_baud_rate(uint32_t baudRate)
{
    mutex_lock();
    set_u32(g_factoryHandle, KEY_CAT1_BAUD_RATE, baudRate);
    commit_cfg(g_factoryHandle);
    mutex_unlock();
    return ESP_OK;
}

esp_err_t cfg_user_erase_all()
{
    esp_err_t err = ESP_OK;
    mutex_lock();

    err = nvs_erase_all(g_userHandle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to erase all (err %d)", err);
        goto OUT;
    }

    err = nvs_commit(g_userHandle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit nvs (err %d)", err);
        goto OUT;
    }
OUT:
    mutex_unlock();
    return err;
}

esp_err_t cfg_set_firmware_crc32(uint32_t crc)
{
    mutex_lock();
    set_u32(g_factoryHandle, KEY_SYS_CRC32, crc);
    commit_cfg(g_factoryHandle);
    mutex_unlock();
    return ESP_OK;
}

uint32_t cfg_get_firmware_crc32()
{
    uint32_t crc = 0;
    mutex_lock();
    get_u32(g_factoryHandle, KEY_SYS_CRC32, &crc, 0);
    mutex_unlock();
    return crc;
}

esp_err_t cfg_set_config_crc32(uint32_t crc)
{
    mutex_lock();
    set_u32(g_factoryHandle, KEY_CFG_CRC32, crc);
    commit_cfg(g_factoryHandle);
    mutex_unlock();
    return ESP_OK;
}

uint32_t cfg_get_config_crc32()
{
    uint32_t crc = 0;
    mutex_lock();
    get_u32(g_factoryHandle, KEY_CFG_CRC32, &crc, 0xaa);
    mutex_unlock();
    return crc;
}

esp_err_t cfg_set_schedule_time(char *time)
{
    mutex_lock();
    set_str(g_userHandle, KEY_SYS_SCHE_TIME, time);
    commit_cfg(g_userHandle);
    mutex_unlock();
    return ESP_OK;
}

esp_err_t cfg_get_schedule_time(char *time)
{
    mutex_lock();
    get_str(g_userHandle, KEY_SYS_SCHE_TIME, time, 32, "03:03:30");
    mutex_unlock();
    return ESP_OK;
}

esp_err_t cfg_set_timezone(char *tz)
{
    mutex_lock();
    set_str(g_userHandle, KEY_SYS_TIME_ZONE, tz);
    commit_cfg(g_userHandle);
    mutex_unlock();
    return ESP_OK;
}

esp_err_t cfg_get_timezone(char *tz)
{
    mutex_lock();
    get_str(g_userHandle, KEY_SYS_TIME_ZONE, tz, 32, "CST-8");
    mutex_unlock();
    return ESP_OK;
}

esp_err_t cfg_set_time_err_rate(int32_t err_rate)
{
    mutex_lock();
    set_i32(g_factoryHandle, KEY_SYS_TIME_ERR_RATE, err_rate);
    commit_cfg(g_factoryHandle);
    mutex_unlock();
    return ESP_OK;
}

esp_err_t cfg_get_time_err_rate(int32_t *err_rate)
{
    mutex_lock();
    get_i32(g_factoryHandle, KEY_SYS_TIME_ERR_RATE, err_rate, 0);
    mutex_unlock();
    return ESP_OK;
}

esp_err_t cfg_import(char *data, size_t len)
{
    ESP_LOGI(TAG, "Importing config data");
    dictionary *d = iniparser_load_ex(data, len);
    if (d == NULL) {
        ESP_LOGE(TAG, "Failed to load config data");
        return ESP_FAIL;
    }
    //debug
    iniparser_dump(d, stderr);
    // 1. check model
    const char *model = iniparser_getstring(d, "Hardware:model", NULL);
    if (model == NULL || strcmp(model, "NE101") != 0) {
        ESP_LOGE(TAG, "Invalid config data");
        iniparser_freedict(d);
        return ESP_FAIL;
    }
    // 2. check key and value
    size_t n;
    // size_t size = 0;
    // char out[32] = {0};
    for (n = 0 ; n < d->size ; n++) {
        if (d->key[n] == NULL || d->val[n] == NULL) {
            continue;
        }
        // size = sizeof(out);
        // // not found
        // if (nvs_get_str(g_userHandle, d->key[n], out, &size) != ESP_OK) {
        //     ESP_LOGE(TAG, "Invalid key: %s", d->key[n]);
        //     continue;
        // }
        // // same value
        // if (strcmp(d->val[n], out) == 0) {
        //     ESP_LOGI(TAG, "Key: %s, value: %s same", d->key[n], d->val[n]);
        //     continue;
        // set value
        // }
        ESP_LOGI(TAG, "Importing key: %s, value: %s", d->key[n], d->val[n]);
        set_str(g_userHandle, d->key[n], d->val[n]);
    }
    iniparser_freedict(d);
    return ESP_OK;
}
