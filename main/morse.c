#include <stdio.h>
#include <string.h>
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_err.h"
#include "esp_log.h"
#include "config.h"
#include "system.h"
#include "utils.h"
#include "mmwlan_regdb.def"
#include "mmosal.h"
#include "mmwlan.h"
#include "morse.h"

#define TAG "-->MORSE"

struct mm_netif_driver {
    esp_netif_driver_base_t base;
    void *handle;
};

typedef struct mm_netif_driver *mm_netif_driver_t;

struct mm_scan_args {
    struct mm_scan_result *result;
    struct mmosal_semb *semaphore;
};

struct mm_wifi_config {
    char ssid[MMWLAN_SSID_MAXLEN];
    char password[MMWLAN_PASSPHRASE_MAXLEN];
    char country_code[3];
    esp_netif_t *netif;
};

struct mm_wifi_config g_mm_wifi_config = {
    .ssid = "morse",
    .password = "12345678",
    .country_code = "EU"
};

/*--------------------------------------------wifi interface-----------------------------------------*/
static void wifi_free(void *h, void *buffer)
{
    if (buffer) {
        free(buffer);
    }
}

static esp_err_t wifi_transmit(void *h, void *buffer, size_t len)
{
    // mm_netif_driver_t driver = h;
    uint8_t *data = buffer;
    enum mmwlan_status status = mmwlan_tx(data, len);
    if (status != MMWLAN_SUCCESS) {
        ESP_LOGE(TAG, "Failed to send data[len %d] to wifi interface: %d", len, status);
        return ESP_FAIL;
    }

    // ESP_LOGI(TAG, "<---------Sent %d bytes to morse", len);
    // for (int i = 0; i < len; i++) {
    //     if (i % 16 == 0) {
    //         printf("\n");
    //     }
    //     printf("%02x ", data[i]);
    // }
    // printf("\n--------------end----------------\n");
    return ESP_OK;
}

static esp_err_t wifi_transmit_wrap(void *h, void *buffer, size_t len, void *netstack_buf)
{
    return wifi_transmit(h, buffer, len);
}

static esp_err_t wifi_driver_start(esp_netif_t *esp_netif, void *args)
{
    mm_netif_driver_t driver = args;
    driver->base.netif = esp_netif;
    esp_netif_driver_ifconfig_t driver_ifconfig = {
        .handle =  driver,
        .transmit = wifi_transmit,
        .transmit_wrap = wifi_transmit_wrap,
        .driver_free_rx_buffer = wifi_free
    };

    return esp_netif_set_driver_config(esp_netif, &driver_ifconfig);
}

static mm_netif_driver_t wifi_create_if_driver()
{
    mm_netif_driver_t driver = calloc(1, sizeof(struct mm_netif_driver));
    if (driver == NULL) {
        ESP_LOGE(TAG, "No memory to create a wifi interface handle");
        return NULL;
    }
    driver->handle = NULL; // TODO
    driver->base.post_attach = wifi_driver_start;
    return driver;
}

static void wifi_destroy_if_driver(mm_netif_driver_t driver)
{
    if (driver) {
        free(driver);
    }
}

static esp_err_t disconnect_and_destroy(esp_netif_t *esp_netif)
{
    mm_netif_driver_t driver = esp_netif_get_io_driver(esp_netif);
    esp_netif_driver_ifconfig_t driver_ifconfig = { };
    esp_err_t  ret = esp_netif_set_driver_config(esp_netif, &driver_ifconfig);
    wifi_destroy_if_driver(driver);
    return ret;
}

static esp_err_t wifi_clear_default_sta_handlers(void)
{
    return ESP_OK;
}

static esp_err_t wifi_set_default_sta_handlers(void)
{
    return ESP_OK;
}

static void wifi_rx_cb(uint8_t *pHeader, unsigned head_size, uint8_t *pPayload, unsigned len, void *arg)
{
    esp_netif_t *esp_netif = (esp_netif_t *)arg;
    uint8_t *buffer = calloc(1, head_size + len);
    if (buffer == NULL) {
        ESP_LOGE(TAG, "No memory to create a wifi interface handle");
        return;
    }
    memcpy(buffer, pHeader, head_size);
    memcpy(buffer + head_size, pPayload, len);
    esp_netif_receive(esp_netif, buffer, head_size + len, buffer);
    // ESP_LOGI(TAG, "--------->Received %d bytes from morse", head_size + len);
}

static void wifi_link_state_cb(enum mmwlan_link_state link_state, void *arg)
{
    esp_netif_t *esp_netif = (esp_netif_t *)arg;
    if (link_state == MMWLAN_LINK_DOWN) {
        ESP_LOGI(TAG, "Link down");
        esp_netif_action_disconnected(esp_netif, WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, NULL);
        ESP_ERROR_CHECK(esp_event_post(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, NULL, 0, portMAX_DELAY));
    } else {
        ESP_LOGI(TAG, "Link up");
        esp_netif_action_connected(esp_netif, WIFI_EVENT, WIFI_EVENT_STA_CONNECTED, NULL);
        ESP_ERROR_CHECK(esp_event_post(WIFI_EVENT, WIFI_EVENT_STA_CONNECTED, NULL, 0, portMAX_DELAY));
    }
}

static void wifi_sta_state_cb(enum mmwlan_sta_state sta_state)
{
    esp_netif_t *esp_netif = (esp_netif_t *)g_mm_wifi_config.netif;

    switch (sta_state) {
        case MMWLAN_STA_DISABLED:
            ESP_LOGI(TAG, "Disconnected");
            esp_netif_action_stop(esp_netif, NULL, 0, NULL);
            ESP_ERROR_CHECK(esp_event_post(WIFI_EVENT, WIFI_EVENT_STA_STOP, NULL, 0, portMAX_DELAY));
            break;
        case MMWLAN_STA_CONNECTING:
            ESP_LOGI(TAG, "Connecting");
            break;
        case MMWLAN_STA_CONNECTED:
            ESP_LOGI(TAG, "Connected");
            esp_netif_action_start(esp_netif, NULL, 0, NULL);
            ESP_ERROR_CHECK(esp_event_post(WIFI_EVENT, WIFI_EVENT_STA_START, NULL, 0, portMAX_DELAY));
            break;
        default:
            break;
    }
}

static void wifi_scan_rx_cb(const struct mmwlan_scan_result *result, void *arg)
{
    struct mm_scan_args *args = (struct mm_scan_args *)arg;
    struct scan_item *item = NULL;
    struct rsn_information rsn_info;

    char bssid_str[18];
    char ssid_str[MMWLAN_SSID_MAXLEN];

    if (args->result->items_count > MAX_SCAN_ITEM_COUNT) {
        ESP_LOGE(TAG, "Too many scan results");
        return;
    }
    // deduplicate
    for (int i = 0; i < args->result->items_count; i++) {
        if (memcmp(result->bssid, args->result->items[i].bssid, MMWLAN_MAC_ADDR_LEN) == 0) {
            return;
        }
    }

    item = &args->result->items[args->result->items_count];
    memcpy(item->bssid, result->bssid, MMWLAN_MAC_ADDR_LEN),
           snprintf(item->ssid, (result->ssid_len + 1), "%s", result->ssid);
    item->rssi = result->rssi;
    rsn_info.num_akm_suites = 0;
    snprintf(bssid_str, sizeof(bssid_str), "%02x:%02x:%02x:%02x:%02x:%02x",
             result->bssid[0], result->bssid[1], result->bssid[2], result->bssid[3],
             result->bssid[4], result->bssid[5]);
    snprintf(ssid_str, (result->ssid_len + 1), "%s", result->ssid);

    ESP_LOGI(TAG,
             "%2d. RSSI: %3d, BSSID: %s, SSID: %s, BW: %uMb, freq: %luHz, Beacon Interval(TUs): %u, Capability Info: 0x%04x",
             args->result->items_count, result->rssi, bssid_str, ssid_str, result->op_bw_mhz,
             result->channel_freq_hz, result->beacon_interval, result->capability_info);

    int ret = mmhal_parse_rsn_information(result->ies, result->ies_len, &rsn_info);
    if (ret < 0) {
        ESP_LOGE(TAG, "Invalid probe response\n");
    } else if (rsn_info.num_akm_suites == 0) {
        ESP_LOGI(TAG, "Security: None\n");
    } else if (ret > 0) {
        ESP_LOGI(TAG, "Security: %s", mmhal_akm_suite_to_string(rsn_info.akm_suites[0]));
    }
    item->authmode = rsn_info.akm_suites[0] == AKM_SUITE_SAE ? 1 : 0;
    args->result->items_count++;
}

static void wifi_scan_completed_cb(enum mmwlan_scan_state state, void *arg)
{
    struct mm_scan_args *args = (struct mm_scan_args *)arg;
    (void)(state);
    ESP_LOGI(TAG, "Scanning completed.");
    mmosal_semb_give(args->semaphore);
}

/*--------------------------------------------netif interface------------------------- ----------------*/

static esp_err_t netif_attach_wifi_station(esp_netif_t *esp_netif)
{
    mm_netif_driver_t driver = wifi_create_if_driver();
    if (driver == NULL) {
        ESP_LOGE(TAG, "Failed to create wifi interface handle");
        return ESP_FAIL;
    }
    return esp_netif_attach(esp_netif, driver);
}

/*--------------------------------------------external interface-----------------------------------------*/

esp_netif_t *mm_netif_create_default_wifi_sta(void)
{
    esp_netif_config_t cfg = ESP_NETIF_DEFAULT_WIFI_STA();
    esp_netif_t *netif = esp_netif_new(&cfg);
    assert(netif);
    ESP_ERROR_CHECK(netif_attach_wifi_station(netif));
    ESP_ERROR_CHECK(wifi_set_default_sta_handlers());
    return netif;
}

void mm_netif_destroy_wifi_sta(esp_netif_t *esp_netif)
{
    if (esp_netif) {
        wifi_clear_default_sta_handlers();
        disconnect_and_destroy(esp_netif);
    }
    esp_netif_destroy(esp_netif);
}

esp_err_t mm_wifi_init(esp_netif_t *esp_netif, uint8_t *mac_addr, const char *country_code)
{
    enum mmwlan_status status;
    struct mmwlan_version version;
    struct mmwlan_boot_args boot_args = MMWLAN_BOOT_ARGS_INIT;
    const struct mmwlan_s1g_channel_list *channel_list;

    if (strlen(country_code) != 2) {
        ESP_LOGE(TAG, "Invalid country code: %s", country_code);
        return ESP_FAIL;
    }
#ifdef CONFIG_MM_BCF_MF08251_FCC
    ESP_LOGI(TAG, "BCF MF08251 FCC");
#elifdef CONFIG_MM_BCF_MF08251_CE
    ESP_LOGI(TAG, "BCF MF08251 CE");
#endif
    mmhal_init();
    mmwlan_init();
    mm_wifi_set_mac(mac_addr);

    status = mmwlan_register_rx_cb(wifi_rx_cb, esp_netif);
    if (status != MMWLAN_SUCCESS) {
        ESP_LOGE(TAG, "Failed to register %s callback", "rx");
        return ESP_FAIL;
    }

    status = mmwlan_register_link_state_cb(wifi_link_state_cb, esp_netif);
    if (status != MMWLAN_SUCCESS) {
        ESP_LOGE(TAG, "Failed to register %s callback", "link state");
        return ESP_FAIL;
    }

    channel_list = mmwlan_lookup_regulatory_domain(get_regulatory_db(), country_code);
    if (channel_list == NULL) {
        ESP_LOGE(TAG, "Could not find specified regulatory domain matching country code %s", country_code);
        return ESP_FAIL;
    }

    status = mmwlan_set_channel_list(channel_list);
    if (status != MMWLAN_SUCCESS) {
        ESP_LOGE(TAG, "Failed to set country code %s", channel_list->country_code);
        return ESP_FAIL;
    }

    status = mmwlan_boot(&boot_args);
    if (status != MMWLAN_SUCCESS) {
        ESP_LOGE(TAG, "Boot failed with code %d", status);
        return ESP_FAIL;
    }

    /* Read and display version information. */
    status = mmwlan_get_version(&version);
    if (status != MMWLAN_SUCCESS) {
        ESP_LOGE(TAG, "Failed to get version");
        return ESP_FAIL;
    }

    ESP_ERROR_CHECK(esp_netif_set_mac(esp_netif, mac_addr));

    ESP_LOGI(TAG, "Morse firmware version %s, morselib version %s, Morse chip ID 0x%lx, MAC %02X:%02X:%02X:%02X:%02X:%02X",
             version.morse_fw_version, version.morselib_version,
             version.morse_chip_id, mac_addr[0], mac_addr[1],
             mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);

    g_mm_wifi_config.netif = esp_netif;
    strncpy(g_mm_wifi_config.country_code, country_code, 3);
    ESP_LOGI(TAG, "initialized OK");

    return ESP_OK;
}

esp_err_t mm_wifi_deinit(void)
{
    mmwlan_shutdown();
    mmwlan_deinit();
    return ESP_OK;
}

void mm_wifi_shutdown()
{
    mm_wifi_deinit();
    mmhal_wlan_shutdown();
}

esp_err_t mm_wifi_set_config(const char *ssid, const char *password)
{
    if (ssid == NULL || password == NULL) {
        ESP_LOGE(TAG, "Invalid parameter");
        return ESP_FAIL;
    }
    if (strlen(ssid)) {
        strncpy(g_mm_wifi_config.ssid, ssid, MMWLAN_SSID_MAXLEN);
    }
    if (strlen(password)) {
        strncpy(g_mm_wifi_config.password, password, MMWLAN_PASSPHRASE_MAXLEN);
    } else {
        memset(g_mm_wifi_config.password, 0, MMWLAN_PASSPHRASE_MAXLEN);
    }

    return ESP_OK;
}

esp_err_t mm_wifi_scan(mm_scan_result_t *result)
{
    static bool scanning = false;
    enum mmwlan_status status;
    struct mmwlan_scan_req scan_req = MMWLAN_SCAN_REQ_INIT;
    struct mm_scan_args scan_args = {0};

    if (scanning) {
        ESP_LOGE(TAG, "Already scanning");
        return ESP_FAIL;
    }
    scanning = true;
    mmwlan_scan_abort(); // abort previous scan

    scan_req.scan_rx_cb = wifi_scan_rx_cb;
    scan_req.scan_complete_cb = wifi_scan_completed_cb;
    scan_req.scan_cb_arg = &scan_args;
    scan_args.result = result;
    scan_args.semaphore = mmosal_semb_create("scan");
    result->items_count = 0;

    status = mmwlan_scan_request(&scan_req);
    if (status != MMWLAN_SUCCESS) {
        ESP_LOGE(TAG, "Failed to scan: %d", status);
        mmwlan_scan_abort();
        mmosal_semb_delete(scan_args.semaphore);
        scanning = false;
        return ESP_FAIL;
    }

    mmosal_semb_wait(scan_args.semaphore, 30000); // 30s
    mmosal_semb_delete(scan_args.semaphore);
    scanning = false;
    return ESP_OK;
}

esp_err_t mm_wifi_get_mac(uint8_t *mac)
{
    mmhal_read_mac_addr(mac);
    return ESP_OK;
}

esp_err_t mm_wifi_set_mac(uint8_t *mac)
{
    mmhal_write_mac_addr(mac);
    return ESP_OK;
}

esp_err_t mm_wifi_set_country_code(const char *country_code)
{
    if (strlen(country_code) != 2) {
        ESP_LOGE(TAG, "Invalid country code: %s", country_code);
        return ESP_FAIL;
    }

    if (strncmp(country_code, g_mm_wifi_config.country_code, 3) == 0) {
        ESP_LOGI(TAG, "Country code already set to %s", country_code);
        return ESP_OK;
    }

    const struct mmwlan_s1g_channel_list *channel_list;
    channel_list = mmwlan_lookup_regulatory_domain(get_regulatory_db(), country_code);
    if (channel_list == NULL) {
        ESP_LOGE(TAG, "Could not find specified regulatory domain matching country code %s",
                 country_code);
        return ESP_FAIL;
    }

    mm_wifi_disconnect();
    if (mmwlan_shutdown() != MMWLAN_SUCCESS) {
        ESP_LOGE(TAG, "Failed to shutdown");
        return ESP_FAIL;
    }
    enum mmwlan_status status = mmwlan_set_channel_list(channel_list);
    if (status != MMWLAN_SUCCESS) {
        ESP_LOGE(TAG, "Failed to set country code %s (%d)", channel_list->country_code, status);
        return ESP_FAIL;
    }
    // mm_wifi_connect();
    strncpy(g_mm_wifi_config.country_code, country_code, 3);

    struct mmwlan_boot_args boot_args = MMWLAN_BOOT_ARGS_INIT;
    if (mmwlan_boot(&boot_args) != MMWLAN_SUCCESS) {
        ESP_LOGE(TAG, "Boot failed with code %d", status);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Set country code to %s", country_code);

    return ESP_OK;
}

esp_err_t mm_wifi_get_country_code(char *country_code)
{
    strncpy(country_code, g_mm_wifi_config.country_code, 3);
    return ESP_OK;
}

esp_err_t mm_wifi_connect()
{
    struct mmwlan_sta_args sta_args = MMWLAN_STA_ARGS_INIT;
    enum mmwlan_status status;

    char *ssid = g_mm_wifi_config.ssid;
    char *password = g_mm_wifi_config.password;

    sta_args.ssid_len = strlen(ssid);
    memcpy(sta_args.ssid, ssid, sta_args.ssid_len);
    ESP_LOGI(TAG, "Connecting to %s", ssid);

    sta_args.passphrase_len = strlen(password);
    if (sta_args.passphrase_len > 0) {
        memcpy(sta_args.passphrase, password, sta_args.passphrase_len);
        sta_args.security_type = MMWLAN_SAE;
    } else {
        sta_args.security_type = MMWLAN_OPEN;
    }

    status = mmwlan_sta_enable(&sta_args, wifi_sta_state_cb);
    if (status != MMWLAN_SUCCESS) {
        ESP_LOGE(TAG, "Failed to enable station mode: %d", status);
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t mm_wifi_disconnect()
{
    enum mmwlan_status status = mmwlan_sta_disable();
    if (status != MMWLAN_SUCCESS) {
        ESP_LOGE(TAG, "Failed to disable station mode: %d", status);
        return ESP_FAIL;
    }
    return ESP_OK;
}
