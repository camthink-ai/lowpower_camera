#include "config.h"
#include "wifi.h"
#include "debug.h"
#include "utils.h"
#include "mqtt.h"
#include "sleep.h"
#include "http.h"
#include "morse.h"
#include "camera.h"
#include "misc.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "iot_mip.h"
#include "net_module.h"

#define TAG "-->WIFI"  // Logging tag for WiFi module

// WiFi connection timeout constants
#define AP_TIMEOUT_SECONDS (60)  // AP mode inactivity timeout
#define WIFI_STA_CONNECT_BIT BIT(0)  // Station connected event bit
#define WIFI_STA_DISCONNECT_BIT BIT(1)  // Station disconnected event bit
#define WIFI_STA_CONNECT_TIMEOUT_MS (20000)  // Max wait for connection
#define WIFI_STA_DISCONNECT_TIMEOUT_MS (2000)  // Max wait for disconnection
#define WIFI_STA_CHECK_TIMEOUT_MS (20000)  // Initial connection check timeout
#define WIFI_STA_CONNECT_MAX_RETRIES (3)   //The number of retries when automatically connecting in WiFi STA mode.
/**
 * WiFi module state structure
 */
typedef struct mdWifi {
    bool bInit;                 ///< Initialization flag
    EventGroupHandle_t eventGroup; ///< Event group for connection state
    bool isConnected;           ///< Current connection status
    uint32_t apTimeoutSeconds;  ///< AP mode inactivity timer
    esp_timer_handle_t timer;   ///< AP timeout timer handle
    uint8_t apUserCount;        ///< Number of connected AP clients
    esp_netif_t *netif;         ///< Network interface handle
} mdWifi_t;

static mdWifi_t g_wifi = {0};  // Global WiFi state

/**
 * WiFi event handler
 * @param arg Pointer to mdWifi_t state
 * @param event_base Event base type
 * @param event_id Specific event ID
 * @param event_data Event data
 */
static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    mdWifi_t *wifi = (mdWifi_t *)arg;
    /* AP mode */
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi->apUserCount++;
        ESP_LOGI(TAG, "WIFI_EVENT_AP_STACONNECTED count: %d", wifi->apUserCount);
        if (system_get_mode() == MODE_CONFIG){
            lightAttr_t light;
            cfg_get_light_attr(&light);
            camera_flash_led_ctrl(&light);
        }
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi->apUserCount--;
        ESP_LOGI(TAG, "WIFI_EVENT_AP_STADISCONNECTED count: %d", wifi->apUserCount);
        if(wifi->apUserCount == 0){
            misc_flash_led_close();
        }
    }
    /* Sta mode */
    if (event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "WIFI_EVENT_STA_START");
        if(!netModule_is_mmwifi())
            esp_wifi_connect();
    }
    if (event_id == WIFI_EVENT_STA_STOP) {
        ESP_LOGI(TAG, "WIFI_EVENT_STA_STOP");
    }
    if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "WIFI_EVENT_STA_DISCONNECTED");
        xEventGroupClearBits(wifi->eventGroup, WIFI_STA_CONNECT_BIT);
        xEventGroupSetBits(wifi->eventGroup, WIFI_STA_DISCONNECT_BIT);
        wifi->isConnected = false;
        if (iot_mip_autop_is_enable()) {
            iot_mip_autop_stop();
        }
        mqtt_stop();
    }
    if (event_id == WIFI_EVENT_STA_CONNECTED) {
        ESP_LOGI(TAG, "WIFI_EVENT_STA_CONNECTED");
    }
    return;
}

/**
 * IP event handler
 * @param arg Pointer to mdWifi_t state
 * @param event_base Event base type
 * @param event_id Specific event ID
 * @param event_data Event data
 */
static void ip_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    mdWifi_t *wifi = (mdWifi_t *)arg;
    ESP_LOGI(TAG, "ip ev_handle_called. event_id[%ld]", event_id);
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        wifi->isConnected = true;
        xEventGroupClearBits(wifi->eventGroup, WIFI_STA_DISCONNECT_BIT);
        xEventGroupSetBits(wifi->eventGroup, WIFI_STA_CONNECT_BIT);
        if (iot_mip_autop_is_enable()) {
            iot_mip_autop_async_start(NULL);
        }
        if(system_get_mode() != MODE_SCHEDULE){
            system_ntp_time();
        }
        mqtt_start();
    }
}

/**
 * Configure SoftAP mode
 * @param netif Network interface
 * @param ssid AP SSID
 * @param password AP password (NULL for open)
 * @param host IP address for AP
 */
static void wifi_cfg_softap(esp_netif_t *netif, const char *ssid, const char *password, const char *host)
{
    wifi_config_t wifi_config;
    memset(&wifi_config, 0, sizeof(wifi_config_t));

    if (strcmp(host, "192.168.4.1")) {
        int a, b, c, d;
        sscanf(host, "%d.%d.%d.%d", &a, &b, &c, &d);
        esp_netif_ip_info_t ip_info;
        IP4_ADDR(&ip_info.ip, a, b, c, d);
        IP4_ADDR(&ip_info.gw, a, b, c, d);
        IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0);
        ESP_ERROR_CHECK(esp_netif_dhcps_stop(netif));
        ESP_ERROR_CHECK(esp_netif_set_ip_info(netif, &ip_info));
        ESP_ERROR_CHECK(esp_netif_dhcps_start(netif));
    }

    if (ssid && strlen(ssid)) {
        wifi_config.ap.ssid_len = strlen((char *)wifi_config.ap.ssid);
        snprintf((char *)wifi_config.ap.ssid, 32, "%s", ssid);
    } else {
        ESP_LOGE(TAG, "SSID IS NULL");
        return;
    }
    if (password && strlen(password)) {
        snprintf((char *)wifi_config.ap.password, 64, "%s", password);
        wifi_config.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;
    } else {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }
    wifi_config.ap.max_connection = 5;
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config));
    ESP_LOGI(TAG, "wifi_init_softap finished.SSID:%s", wifi_config.ap.ssid);
}

/**
 * Configure Station mode
 * @param ssid Network SSID to connect to
 * @param password Network password
 */
static void wifi_cfg_sta(const char *ssid, const char *password)
{
    if(!netModule_is_mmwifi()){
        wifi_config_t wifi_config;

        memset(&wifi_config, 0, sizeof(wifi_config_t));
        if (ssid && strlen(ssid)) {
            snprintf((char *)wifi_config.sta.ssid, 32, "%s", ssid);
        } else {
            ESP_LOGE(TAG, "SSID IS NULL");
            return;
        }
        if (password && strlen(password)) {
            snprintf((char *)wifi_config.sta.password, 64, "%s", password);
        }
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    }else{
        ESP_ERROR_CHECK(mm_wifi_set_config(ssid, password));
    }

    ESP_LOGI(TAG, "wifi_init_sta finished.");
    ESP_LOGI(TAG, "connect to ap SSID:%s", ssid);
}

/**
 * AP timeout timer callback
 * @param arg Pointer to mdWifi_t state
 */
static void timer_cb(void *arg)
{
    mdWifi_t *wifi = (mdWifi_t *)arg;
    if (http_hasClient()) {
        wifi->apTimeoutSeconds = 0;
        return;
    }
    if (wifi->apUserCount == 0) {
        wifi->apTimeoutSeconds++;
    } else {
        wifi->apTimeoutSeconds = 0;
    }
    if (wifi->apTimeoutSeconds >= AP_TIMEOUT_SECONDS) {
        ESP_LOGI(TAG, "AP nobody to connect over %ds, will go to sleep", AP_TIMEOUT_SECONDS);
        sleep_set_event_bits(SLEEP_NO_OPERATION_TIMEOUT_BIT);
    }
}

/**
 * Start AP timeout timer
 */
static void wifi_timer_start()
{
    const esp_timer_create_args_t timer_args = {
        timer_cb,
        &g_wifi,
        ESP_TIMER_TASK,
        "wifi_timer",
        true,
    };
    esp_timer_create(&timer_args, &g_wifi.timer);
    esp_timer_start_periodic(g_wifi.timer, 1000 * 1000); //1s
}

/**
 * Stop AP timeout timer
 */
static void wifi_timer_stop()
{
    esp_timer_stop(g_wifi.timer);
}

/**
 * Console command handler for WiFi scan
 * @param argc Argument count
 * @param argv Argument values
 * @return 0 on success
 */
static int do_scan_cmd(int argc, char **argv)
{
    wifiList_t list;
    if (wifi_get_list(&list) == ESP_OK) {
        wifi_put_list(&list);
    }

    return 0;
}

#define SERVER_IP "192.168.1.100"  // Default server IP address
#define SERVER_PORT 8866           // Default server port
#define DATA_SIZE 1024             // Default data size for test
#define PACKET_SIZE 128            // Default packet size for test

/**
 * Console command handler for TCP client test
 * @param argc Argument count
 * @param argv Argument values
 * @return 0 on success
 */
static int do_tcp_client(int argc, char **argv)
{
    wifi_timer_stop();
    char *server_ip = SERVER_IP;
    int server_port = SERVER_PORT;
    int data_size = DATA_SIZE;
    int packet_size = PACKET_SIZE;

    if (argc > 1) {
        server_ip = argv[1];
    }
    if (argc > 2) {
        server_port = atoi(argv[2]);
    }
    if (argc > 3) {
        data_size = atoi(argv[3]);
    }
    if (argc > 4) {
        packet_size = atoi(argv[4]);
    }
    ESP_LOGI(TAG, "server_ip: %s server_port: %d data_size: %d packet_size: %d",
             server_ip, server_port, data_size, packet_size);
    // Create TCP socket
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        ESP_LOGE(TAG, "Failed to create socket");
        return ESP_FAIL;
    }

    // Configure server address
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    inet_pton(AF_INET, server_ip, &server_addr.sin_addr);

    // Connect to server
    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) != 0) {
        ESP_LOGE(TAG, "Failed to connect to server");
        close(sock);
        return ESP_FAIL;
    }

    // Send data to server
    char *data = malloc(data_size);
    memset(data, 'A', data_size);
    int bytes_sent = 0;
    int total_bytes_sent = 0;
    TickType_t start_time = xTaskGetTickCount(); // Record start time
    while (total_bytes_sent < data_size) {
        int bytes_to_send = (data_size - total_bytes_sent) > packet_size ? packet_size : (data_size - total_bytes_sent);
        bytes_sent = send(sock, data + total_bytes_sent, bytes_to_send, 0);
        if (bytes_sent < 0) {
            ESP_LOGE(TAG, "Failed to send data to server");
            break;
        }
        total_bytes_sent += bytes_sent;

    }
    TickType_t end_time = xTaskGetTickCount(); // Record end time
    free(data);

    // Close socket
    close(sock);

    // 计算发送速率
    TickType_t  elapsed_time = end_time - start_time;
    float send_rate = (float)total_bytes_sent / ((float)elapsed_time / configTICK_RATE_HZ);
    ESP_LOGI(TAG, "Send rate: %.2f bytes/s", send_rate);
    return ESP_OK;
}

#define LISTEN_PORT 8866  // Default listening port
#define BUFFER_SIZE 1024  // Default receive buffer size

/**
 * Console command handler for TCP server test
 * @param argc Argument count
 * @param argv Argument values
 * @return 0 on success
 */
static int do_tcp_server(int argc, char **argv)
{
    wifi_timer_stop();
    int listen_port = LISTEN_PORT;
    int buffer_size = BUFFER_SIZE;

    if (argc > 1) {
        listen_port = atoi(argv[1]);
    }

    if (argc > 2) {
        buffer_size = atoi(argv[2]);
    }

    // Create TCP socket
    int listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_sock < 0) {
        ESP_LOGE(TAG, "Failed to create socket");
        return ESP_FAIL;
    }

    // Configure server address
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(listen_port);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    // Bind address and port
    if (bind(listen_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) != 0) {
        ESP_LOGE(TAG, "Failed to bind socket");
        close(listen_sock);
        return ESP_FAIL;
    }

    // Listen for connection requests
    if (listen(listen_sock, 1) != 0) {
        ESP_LOGE(TAG, "Failed to listen");
        close(listen_sock);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Listening on port: %d", listen_port);
    // Accept connection request
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    int client_sock = accept(listen_sock, (struct sockaddr *)&client_addr, &client_addr_len);
    if (client_sock < 0) {
        ESP_LOGE(TAG, "Failed to accept connection");
        close(listen_sock);
        return ESP_FAIL;
    }

    // Receive data
    char *buffer = malloc(buffer_size);
    int bytes_received = 0;
    int total_bytes_received = 0;
    // int packet_count = 0;
    TickType_t start_time = xTaskGetTickCount(); // Record start time
    while ((bytes_received = recv(client_sock, buffer, buffer_size, 0)) > 0) {
        total_bytes_received += bytes_received;
    }
    TickType_t end_time = xTaskGetTickCount(); // Record end time
    free(buffer);

    // Close connection
    close(client_sock);
    close(listen_sock);

    // Calculate receive rate
    TickType_t elapsed_time = end_time - start_time;
    float receive_rate = (float)total_bytes_received / ((float)elapsed_time / configTICK_RATE_HZ);
    ESP_LOGI(TAG, "Receive rate: %.2f bytes/s", receive_rate);
    return ESP_OK;
}

static esp_console_cmd_t g_cmd[] = {
    {"wifiscan", "scan ssid list", NULL, do_scan_cmd, NULL},
    {"tcpclient", "tcp client", NULL, do_tcp_client, NULL},
    {"tcpserver", "tcp server", NULL, do_tcp_server, NULL},
};

/**
 * Initialize WiFi module
 * @param mode WiFi mode (WIFI_MODE_APSTA, WIFI_MODE_AP, WIFI_MODE_STA)
 */
void wifi_open(wifi_mode_t mode)
{
    if (g_wifi.bInit) {
        return;
    }
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    uint8_t mac_hex[6];
    deviceInfo_t device;

    memset(&g_wifi, 0, sizeof(g_wifi));
    g_wifi.eventGroup = xEventGroupCreate();
    cfg_get_device_info(&device);
    ESP_LOGI(TAG, "mac string: %s", device.mac);
    if (strlen(device.mac) && is_valid_mac(device.mac)) {
        mac_str2hex(device.mac, mac_hex);
    } else {
        esp_read_mac(mac_hex, ESP_MAC_WIFI_STA);
        wifi_set_mac(mac_hex);
        mac_hex2str(mac_hex, device.mac);
        ESP_LOGW(TAG, "invalid mac, use default %s", device.mac);
    }

    ESP_ERROR_CHECK(esp_base_mac_addr_set(mac_hex));
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, &g_wifi));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, &ip_event_handler, &g_wifi));

    if(!netModule_is_mmwifi())
        ESP_ERROR_CHECK(esp_wifi_set_mode(mode));

    if (mode & WIFI_MODE_AP) {
        g_wifi.netif = esp_netif_create_default_wifi_ap();
        char apSsid[32];
        if(netModule_is_mmwifi())
            ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));

        snprintf(apSsid, sizeof(apSsid), "%s_%02X%02X%02X", device.model, mac_hex[3], mac_hex[4], mac_hex[5]);
        wifi_cfg_softap(g_wifi.netif, apSsid, NULL, "192.168.1.1");
        wifi_timer_start();
        if(netModule_is_mmwifi())
            ESP_ERROR_CHECK(esp_wifi_start());

    }
    if (mode & WIFI_MODE_STA) {
        if(!netModule_is_mmwifi())
            esp_netif_create_default_wifi_sta();
        else
            mm_wifi_init(mm_netif_create_default_wifi_sta(), mac_hex, device.countryCode);
        wifiAttr_t wifi;
        cfg_get_wifi_attr(&wifi);
        wifi_cfg_sta(wifi.ssid, wifi.password);
    }
    if(!netModule_is_mmwifi())
        ESP_ERROR_CHECK(esp_wifi_start());
    else
        mm_wifi_connect();

    ESP_LOGI(TAG, "wifi init finished.");
    debug_cmd_add(g_cmd, sizeof(g_cmd) / sizeof(esp_console_cmd_t));
    if (!(mode & WIFI_MODE_AP)) {
        if(netModule_is_mmwifi()){
            xEventGroupWaitBits(g_wifi.eventGroup, WIFI_STA_DISCONNECT_BIT | WIFI_STA_CONNECT_BIT, \
                                false, false, pdMS_TO_TICKS(WIFI_STA_CHECK_TIMEOUT_MS)); 
        }else{
            int retry_count = 0;
            EventBits_t event_bits;
            
            while (retry_count < WIFI_STA_CONNECT_MAX_RETRIES) {
                event_bits = xEventGroupWaitBits(g_wifi.eventGroup, 
                                                WIFI_STA_DISCONNECT_BIT | WIFI_STA_CONNECT_BIT, 
                                                false, 
                                                false, 
                                                pdMS_TO_TICKS(WIFI_STA_CHECK_TIMEOUT_MS));
                                                
                if (event_bits & WIFI_STA_CONNECT_BIT) {
                    // Connected, no need to retry
                    break;
                } else if (event_bits & WIFI_STA_DISCONNECT_BIT) {
                    // Disconnected, retry
                    ESP_LOGI(TAG, "Disconnected from WiFi. Retrying connection... (%d/%d)\n", retry_count + 1, WIFI_STA_CONNECT_MAX_RETRIES);
                    esp_wifi_connect();
                    retry_count++;
                } else {
                    // Timeout occurred, consider as a disconnect and retry
                    ESP_LOGI(TAG, "Timeout waiting for WiFi event. Retrying connection... (%d/%d)\n", retry_count + 1, WIFI_STA_CONNECT_MAX_RETRIES);
                    esp_wifi_connect();
                    retry_count++;
                }
            }
        }
    }
    g_wifi.bInit = true;
}

/**
 * Reconnect to a WiFi network
 * @param ssid Network SSID
 * @param password Network password
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t wifi_sta_reconnect(const char *ssid, const char *password)
{
    EventBits_t uxBits;
    if(!netModule_is_mmwifi())
        esp_wifi_disconnect();
    else
        mm_wifi_disconnect();

    xEventGroupWaitBits(g_wifi.eventGroup, WIFI_STA_DISCONNECT_BIT, true, true, \
                        pdMS_TO_TICKS(WIFI_STA_DISCONNECT_TIMEOUT_MS));
    wifi_cfg_sta(ssid, password);
    if(!netModule_is_mmwifi())
        esp_wifi_connect();
    else
        mm_wifi_connect();

    uxBits = xEventGroupWaitBits(g_wifi.eventGroup, WIFI_STA_CONNECT_BIT, true, true, \
                                 pdMS_TO_TICKS(WIFI_STA_CONNECT_TIMEOUT_MS));
    if (uxBits & WIFI_STA_CONNECT_BIT) {
        return ESP_OK;
    }
    return ESP_FAIL;
}

/**
 * Check if station is connected to WiFi
 * @return true if connected, false otherwise
 */
bool wifi_sta_is_connected()
{
    return g_wifi.isConnected;
}

/**
 * Deinitialize WiFi module
 */
void wifi_close()
{
    if (g_wifi.bInit) {
        esp_wifi_stop();
        if(netModule_is_mmwifi())
            mm_wifi_shutdown();
    }

}

/**
 * Scan for available WiFi networks
 * @param list Output parameter for network list
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t wifi_get_list(wifiList_t *list)
{
    if(netModule_is_cat1()){
        return ESP_FAIL;
    }
    if(!netModule_is_mmwifi()){
        wifi_ap_record_t *ap_info = NULL;
        uint16_t ap_count = 0;

        ESP_ERROR_CHECK(esp_wifi_scan_start(NULL, true));
        ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&ap_count));
        ap_info = (wifi_ap_record_t *)calloc(ap_count + 1, sizeof(wifi_ap_record_t));
        if (ap_info == NULL) {
            ESP_LOGE(TAG, "Failed to malloc buffer to print scan results");
            return ESP_FAIL;
        }
        list->nodes = (wifiNode_t *)calloc(ap_count + 1, sizeof(wifiNode_t));
        if (list->nodes == NULL) {
            ESP_LOGE(TAG, "Failed to malloc buffer to wifi list");
            free(ap_info);
            return ESP_FAIL;
        }
        ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&ap_count, ap_info));
        ESP_LOGI(TAG, "Total APs scanned = %u", ap_count);
        char buf[32];
        for (int i = 0; i < ap_count; i++) {
            ESP_LOGI(TAG, "[%d] %s %s", i, ap_info[i].ssid, mac_hex2str(ap_info[i].bssid, buf));
            memcpy(list->nodes[i].ssid, ap_info[i].ssid, sizeof(list->nodes[i].ssid));
            list->nodes[i].rssi = ap_info[i].rssi;
            list->nodes[i].bAuthenticate = !!ap_info[i].authmode;
        }
        list->count = ap_count;
        free(ap_info);
    }else{
        mm_scan_result_t *result = calloc(1, sizeof(mm_scan_result_t));
        if (result == NULL) {
            ESP_LOGE(TAG, "Failed to malloc buffer to print scan results");
            return ESP_FAIL;
        }

        if (mm_wifi_scan(result) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to scan wifi");
            free(result);
            return ESP_FAIL;
        }

        list->count = result->items_count;
        list->nodes = (wifiNode_t *)calloc(list->count + 1, sizeof(wifiNode_t));
        if (list->nodes == NULL) {
            ESP_LOGE(TAG, "Failed to malloc buffer to wifi list");
            free(result);
            return ESP_FAIL;
        }
        for (int i = 0; i < list->count; i++) {
            memcpy(list->nodes[i].ssid, result->items[i].ssid, strlen(result->items[i].ssid));
            list->nodes[i].rssi = result->items[i].rssi;
            list->nodes[i].bAuthenticate = !!result->items[i].authmode;
        }
        free(result);
    }
    return ESP_OK;
}

/**
 * Free WiFi network list resources
 * @param list List to free
 */
void wifi_put_list(wifiList_t *list)
{
    if (list->nodes) {
        free(list->nodes);
    }
}

/**
 * Get device MAC address
 * @param mac_hex Output buffer for MAC (6 bytes)
 */
void wifi_get_mac(uint8_t *mac_hex)
{
    deviceInfo_t device;
    cfg_get_device_info(&device);
    mac_str2hex(device.mac, mac_hex);
}

/**
 * Set device MAC address
 * @param mac_hex New MAC address (6 bytes)
 */
void wifi_set_mac(uint8_t *mac_hex)
{
    deviceInfo_t device;
    cfg_get_device_info(&device);
    mac_hex2str(mac_hex, device.mac);
    cfg_set_device_info(&device);
}

/**
 * Get access point network interface
 * @return Pointer to AP netif
 */
esp_netif_t * wifi_get_Apnetif(void)
{
    return g_wifi.netif;
}

/**
 * Clear wifi timeout counter
 */
void wifi_clear_timeout(void)
{
    g_wifi.apTimeoutSeconds = 0;
}

