#include "cat1.h"
#include "mqtt.h"
#include "system.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_ppp.h"
#include "esp_modem_api.h"
#include "iot_mip.h"

#define TAG "-->CAT1"  // Logging tag for CAT1 module

// CAT1 module configuration
#define CAT1_BAUD_RATE (921600)  // Default baud rate for CAT1 module

// Timeout constants
#define CAT1_POWER_ON_TIMEOUT_MS (30000)  // Max time to power on module
#define CAT1_PPP_CONNECT_TIMEOUT_MS (60000)  // Max time to establish PPP connection

// Event group bits
#define CAT1_POWER_ON_BIT BIT(0)  // Module powered on
#define CAT1_STA_CONNECT_BIT BIT(1)  // PPP connection established
#define CAT1_STA_DISCONNECT_BIT BIT(2)  // PPP connection lost

// Hardware pin definitions
#define MODEM_UART_TX_PIN 46  // UART TX pin
#define MODEM_UART_RX_PIN 45  // UART RX pin
#define GPIO_OUTPUT_PWRKEY  ((gpio_num_t)48)  // Power key pin
#define GPIO_OUTPUT_PIN_SEL (1ULL<<GPIO_OUTPUT_PWRKEY)  // Power key pin mask

/**
 * CAT1 module state structure
 */
typedef struct mdCat1 {
    bool is_init;               ///< Initialization flag
    int mode;                   ///< Operation mode
    bool is_opened;             ///< UART communication established
    bool is_restarting;         ///< Module restarting flag
    cat1Status_e cat1_status;   ///< Current module status
    EventGroupHandle_t event_group; ///< Event group for state tracking
    esp_netif_t *esp_netif;     ///< Network interface handle
    esp_modem_dce_t *dce;       ///< DCE (Data Circuit-terminating Equipment) handle
    cellularParamAttr_t param;  ///< Configuration parameters
    cellularStatusAttr_t status; ///< Current status attributes
} mdCat1_t;

static mdCat1_t g_cat1 = {0};  // Global CAT1 module state

/**
 * PPP state change handler
 * @param arg Unused
 * @param event_base Event base
 * @param event_id Event ID
 * @param event_data Event data
 */
static void on_ppp_changed(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    ESP_LOGI(TAG, "PPP state changed event %ld", event_id);
    if (event_id == NETIF_PPP_ERRORUSER) {
        /* User interrupted event from esp-netif */
        esp_netif_t *netif = event_data;
        ESP_LOGI(TAG, "User interrupted event from netif:%p", netif);
    }
}

/**
 * IP event handler
 * @param arg Unused
 * @param event_base Event base
 * @param event_id Event ID
 * @param event_data Event data
 */
static void on_ip_event(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    ESP_LOGD(TAG, "IP event! %ld", event_id);
    if (event_id == IP_EVENT_PPP_GOT_IP) {
        esp_netif_dns_info_t dns_info1;
        esp_netif_dns_info_t dns_info2;

        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        esp_netif_t *netif = event->esp_netif;

        ESP_LOGI(TAG, "Modem Connect to PPP Server");
        ESP_LOGI(TAG, "~~~~~~~~~~~~~~");
        ESP_LOGI(TAG, "IP          : " IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "Netmask     : " IPSTR, IP2STR(&event->ip_info.netmask));
        ESP_LOGI(TAG, "Gateway     : " IPSTR, IP2STR(&event->ip_info.gw));
        esp_netif_get_dns_info(netif, 0, &dns_info1);
        ESP_LOGI(TAG, "Name Server1: " IPSTR, IP2STR(&dns_info1.ip.u_addr.ip4));
        esp_netif_get_dns_info(netif, 1, &dns_info2);
        ESP_LOGI(TAG, "Name Server2: " IPSTR, IP2STR(&dns_info2.ip.u_addr.ip4));
        ESP_LOGI(TAG, "~~~~~~~~~~~~~~");
        xEventGroupSetBits(g_cat1.event_group, CAT1_STA_CONNECT_BIT);

        ESP_LOGI(TAG, "GOT ip event!!!");

        snprintf(g_cat1.status.networkStatus, sizeof(g_cat1.status.networkStatus), "%s", "Connected");
        snprintf(g_cat1.status.ipv4Address, sizeof(g_cat1.status.ipv4Address), IPSTR, IP2STR(&event->ip_info.ip));
        snprintf(g_cat1.status.ipv4Gateway, sizeof(g_cat1.status.ipv4Gateway), IPSTR, IP2STR(&event->ip_info.gw));
        snprintf(g_cat1.status.ipv4Dns, sizeof(g_cat1.status.ipv4Dns), IPSTR, IP2STR(&dns_info1.ip.u_addr.ip4));
        snprintf(g_cat1.status.ipv6Address, sizeof(g_cat1.status.ipv6Address), "%s", "::");
        snprintf(g_cat1.status.ipv6Gateway, sizeof(g_cat1.status.ipv6Gateway), "%s", "::");
        snprintf(g_cat1.status.ipv6Dns, sizeof(g_cat1.status.ipv6Dns), "%s", "::");

        if (iot_mip_autop_is_enable()) {
            iot_mip_autop_async_start(NULL);
        }
        if(system_get_mode() != MODE_SCHEDULE){
            system_ntp_time();
        }
        mqtt_start();
    } else if (event_id == IP_EVENT_PPP_LOST_IP) {
        ESP_LOGI(TAG, "Modem Disconnect from PPP Server");

        snprintf(g_cat1.status.networkStatus, sizeof(g_cat1.status.networkStatus), "%s", "Disconnected");
        snprintf(g_cat1.status.ipv4Address, sizeof(g_cat1.status.ipv4Address), "%s", "0.0.0.0/0");
        snprintf(g_cat1.status.ipv4Gateway, sizeof(g_cat1.status.ipv4Gateway), "%s", "0.0.0.0");
        snprintf(g_cat1.status.ipv4Dns, sizeof(g_cat1.status.ipv4Dns), "%s", "0.0.0.0");
        mqtt_stop();
    } else if (event_id == IP_EVENT_GOT_IP6) {
        ESP_LOGI(TAG, "GOT IPv6 event!");

        ip_event_got_ip6_t *event = (ip_event_got_ip6_t *)event_data;
        ESP_LOGI(TAG, "Got IPv6 address " IPV6STR, IPV62STR(event->ip6_info.ip));

        snprintf(g_cat1.status.ipv6Address, sizeof(g_cat1.status.ipv6Address), IPV6STR, IPV62STR(event->ip6_info.ip));
        snprintf(g_cat1.status.ipv6Gateway, sizeof(g_cat1.status.ipv6Gateway), "%s", "::");
        snprintf(g_cat1.status.ipv6Dns, sizeof(g_cat1.status.ipv6Dns), "%s", "::");
    }
}

/**
 * Configure UART for CAT1 module
 * @param baud_rate Desired baud rate
 */
static void configure_uart(uint32_t baud_rate)
{
    uart_config_t uart_config = {};
    uart_config.baud_rate = baud_rate;
    uart_config.data_bits = UART_DATA_8_BITS;
    uart_config.parity = UART_PARITY_DISABLE;
    uart_config.stop_bits = UART_STOP_BITS_1;
    uart_config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;

    uart_param_config(UART_NUM_1, &uart_config);
    uart_set_pin(UART_NUM_1, MODEM_UART_TX_PIN, MODEM_UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
}

/**
 * Send AT command and wait for response
 * @param atCmd AT command string
 * @param atCmdLen Command length
 * @param atResp Response buffer
 * @param atRespLen Response buffer length
 * @param timeout Timeout in ms
 * @param pass_phrase Success response phrase
 * @param fail_phrase Failure response phrase
 * @return ESP_OK on success
 */
static esp_err_t cat1_write_at(const char *atCmd, int atCmdLen, char *atResp, int atRespLen, int timeout, const char *pass_phrase, const char *fail_phrase)
{
    esp_err_t err = ESP_OK;
    int txLen = uart_write_bytes(UART_NUM_1, atCmd, atCmdLen);
    if (txLen != atCmdLen) {
        ESP_LOGE(TAG, "uart_write_bytes failed");
        err = ESP_FAIL;
    } else {
        int time = 0;
        int len = 0;
        memset(atResp, 0, atRespLen);
        while (time < timeout) {
            int remainLen = atRespLen - len;
            if (remainLen <= 0) {
                ESP_LOGE(TAG, "atResp buffer is too small");
                err = ESP_FAIL;
            }
            int rxLen = uart_read_bytes(UART_NUM_1, (uint8_t *)atResp + len, remainLen, pdMS_TO_TICKS(100));
            if (rxLen > 0) {
                len += rxLen;
                if (strstr(atResp, pass_phrase) != NULL) {
                    err = ESP_OK;
                    break;
                }
                if (strstr(atResp, fail_phrase) != NULL) {
                    err = ESP_FAIL;
                    break;
                }
            }
            time += 100;
        }
        if (timeout > 0 && time >= timeout) {
            err = ESP_ERR_TIMEOUT;
        }
    }
    return err;
}

/**
 * Get current baud rate from module
 * @return Current baud rate or -1 on error
 */
static int32_t cat1_get_baud_rate()
{
    int32_t baud_rate = -1;
    char atResp[256];

    memset(atResp, 0, sizeof(atResp));
    esp_err_t err = cat1_write_at("AT+IPR?\r", strlen("AT+IPR?\r"), atResp, sizeof(atResp), 300, "OK", "ERROR");
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "AT+IPR? failed with %d", err);
        return baud_rate;
    }
    ESP_LOGI(TAG, "AT+IPR?=>%s", atResp);
    char *p = strstr(atResp, "+IPR:");
    if (p) {
        p += strlen("+IPR:");
        baud_rate = atoi(p);
    }
    return baud_rate;
}

/**
 * Set module baud rate
 * @param baud_rate Desired baud rate
 * @return ESP_OK on success
 */
static esp_err_t cat1_set_baud_rate(uint32_t baud_rate)
{
    esp_err_t err = ESP_FAIL;
    char atCmd[64];
    char atResp[256];

    // EC800E default baud rate is 115200, needs to be changed to 921600
    int *baud_rate_array = NULL;
    int baud_rate_len = 0;
    int baud_rate_index = 0;
    if (baud_rate != CAT1_BAUD_RATE) {
        baud_rate_array = (int[]){115200, 230400, 460800, 921600};
        baud_rate_len = 4;
    } else {
        baud_rate_array = (int[]){CAT1_BAUD_RATE};
        baud_rate_len = 1;
    }
    int try_count = 0;
    while (true) {
        try_count++;
        if (try_count > 30) {
            ESP_LOGE(TAG, "get baud rate failed");
            break;
        }
        // Error handling to avoid failing to find suitable baud rate
        if (try_count > 20 && baud_rate_len != 4) {
            baud_rate_array = (int[]){115200, 230400, 460800, 921600};
            baud_rate_len = 4;
            baud_rate_index = 0;
        }
        ESP_LOGI(TAG, "use baud rate %d to get baud rate", baud_rate_array[baud_rate_index]);
        configure_uart(baud_rate_array[baud_rate_index]);

        int32_t n = cat1_get_baud_rate();
        if (n < 0) {
            baud_rate_index++;
            if (baud_rate_index >= baud_rate_len) {
                baud_rate_index = 0;
            }
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        ESP_LOGI(TAG, "current baud rate is %ld", n);
        err = ESP_OK;
        if (n != CAT1_BAUD_RATE) {
            snprintf(atCmd, sizeof(atCmd), "AT+IPR=%d;&W\r", CAT1_BAUD_RATE);
            err = cat1_write_at(atCmd, strlen(atCmd), atResp, sizeof(atResp), 1000, "OK", "ERROR");
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "AT+IPR failed with %d", err);
                vTaskDelay(pdMS_TO_TICKS(100));
                continue;
            }
            ESP_LOGI(TAG, "set baud rate to %d", CAT1_BAUD_RATE);
        }
        break;
    }
    return err;
}

/**
 * Get cellular signal quality metrics
 * @param sq Signal quality structure to populate
 * @return ESP_OK on success
 */
static esp_err_t get_signal_quality(cellularSignalQuality_t *sq)
{
    memset(sq, 0, sizeof(cellularSignalQuality_t));

    int rssi, ber;
    esp_err_t err = esp_modem_get_signal_quality(g_cat1.dce, &rssi, &ber);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_modem_get_signal_quality failed with %d %s", err, esp_err_to_name(err));
        return err;
    }
        if (rssi >= 0 && rssi <= 31) {
        int dBm = -113 + 2 * rssi;
        int asu = dBm + 140;
        int dBmLevel;
        if (dBm >= -53) {
            dBmLevel = 5;
        } else if (dBm >= -63) {
            dBmLevel = 4;
        } else if (dBm >= -73) {
            dBmLevel = 3;
        } else if (dBm >= -83) {
            dBmLevel = 2;
        } else if (dBm >= -93) {
            dBmLevel = 1;
        } else {
            dBmLevel = 0;
        }
        ESP_LOGI(TAG, "Signal quality: rssi=%d, ber=%d, dBmLevel=%d", rssi, ber, dBmLevel);

        sq->rssi = rssi;
        sq->ber = ber;
        sq->dbm = dBm;
        sq->asu = asu;
        sq->level = dBmLevel;
        snprintf(sq->quality, sizeof(sq->quality), "%dasu(%ddBm)", asu, dBm);
    } else {
        sq->rssi = rssi;
        sq->ber = ber;
        sq->dbm = 0;
        sq->asu = 0;
        sq->level = 0;
        snprintf(sq->quality, sizeof(sq->quality), "-");
    }

    return ESP_OK;
}

/**
 * Get complete cellular module status
 * @param status Status structure to populate
 * @return ESP_OK on success
 */
static esp_err_t get_status(cellularStatusAttr_t *status)
{
    // 
    esp_err_t err;
    char atResp[MAX_LEN_64];
    char atCmd[MAX_LEN_64];

    // Check SIM card status
    bool simCardReady = false;
    memset(atResp, 0, sizeof(atResp));
    err = esp_modem_at(g_cat1.dce, "AT+CPIN?", atResp, 500);
    ESP_LOGI(TAG, "AT+CPIN?=>%s", atResp);
    if (strstr(atResp, "+CPIN") != NULL) {
        if (strstr(atResp, "READY")) {
            snprintf(status->modemStatus, sizeof(status->modemStatus), "%s", "Ready");
            simCardReady = true;
        } else if (strstr(atResp, "SIM PIN")) {
            if (g_cat1.param.pin[0] != '\0') {
                snprintf(status->modemStatus, sizeof(status->modemStatus), "%s", "PIN Error");
            } else {
                snprintf(status->modemStatus, sizeof(status->modemStatus), "%s", "PIN Required");
            }
        } else if (strstr(atResp, "SIM PUK")) {
            snprintf(status->modemStatus, sizeof(status->modemStatus), "%s", "PUK Required");
        } else {
            snprintf(status->modemStatus, sizeof(status->modemStatus), "%s", atResp);
        }
    } else if (strstr(atResp, "+CME ERROR") != NULL) {
        int errCode = -1;
        sscanf(atResp, "+CME ERROR: %d", &errCode);
        switch (errCode) {
        case 10:
            snprintf(status->modemStatus, sizeof(g_cat1.status.modemStatus), "%s", "No SIM Card");
            break;
        default:
            snprintf(status->modemStatus, sizeof(g_cat1.status.modemStatus), "%s", atResp);
        }
    } else {
        if (atResp[0] != '\0') {
            snprintf(status->modemStatus, sizeof(status->modemStatus), "%s", atResp);
        } else {
            snprintf(status->modemStatus, sizeof(status->modemStatus), "%s", "Unknown");
        }
    }

    // QENG: "servingcell",<state>,"LTE",<is_tdd>,<mcc>,<mnc>,<cellID>,<pcid>,<earfcn>,<freq_band_ind>,<ul_bandwidth>,<dl_bandwidth>,<tac>,<rsrp>,<rsrq>,<rssi>,<sinr>,<srxlev>
    // AT+QENG="servingcell"=>+QENG: "servingcell","NOCONN","LTE","FDD",460,11,E0B70B,374,100,1,5,5,5F0C,-80,-6,-53,30,39
    memset(atResp, 0, sizeof(atResp));
    snprintf(atCmd, sizeof(atCmd), "%s", "AT+QENG=\"servingcell\"");
    err = esp_modem_at(g_cat1.dce, atCmd, atResp, 500);
    ESP_LOGI(TAG, "%s=>%s", atCmd, atResp);

    // IMSI
    memset(atResp, 0, sizeof(atResp));
    err = esp_modem_get_imsi(g_cat1.dce, atResp);
    ESP_LOGI(TAG, "IMSI=>%s", atResp);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_modem_get_imsi failed with %d", err);
        snprintf(status->imsi, sizeof(status->imsi), "%s", "-");
    } else {
        snprintf(status->imsi, sizeof(status->imsi), "%s", atResp);
    }
    // IMEI
    memset(atResp, 0, sizeof(atResp));
    err = esp_modem_get_imei(g_cat1.dce, atResp);
    ESP_LOGI(TAG, "IMEI=>%s", atResp);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_modem_get_imei failed with %d", err);
        snprintf(status->imei, sizeof(status->imei), "%s", "-");
    } else {
        snprintf(status->imei, sizeof(status->imei), "%s", atResp);
    }
    // Module model
    memset(atResp, 0, sizeof(atResp));
    err = esp_modem_at(g_cat1.dce, "AT+CGMM", atResp, 500);
    ESP_LOGI(TAG, "AT+CGMM=>%s", atResp);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_modem_at(AT+CGMM) failed with %d(%s)", err, atResp);
        snprintf(status->model, sizeof(status->model), "%s", "-");
    } else {
        snprintf(status->model, sizeof(status->model), "%s", atResp);
    }
    // Module version
    memset(atResp, 0, sizeof(atResp));
    err = esp_modem_at(g_cat1.dce, "AT+CGMR", atResp, 500);
    ESP_LOGI(TAG, "AT+CGMR=>%s", atResp);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_modem_at(AT+CGMR) failed with %d(%s)", err, atResp);
        snprintf(status->version, sizeof(status->version), "%s", "-");
    } else {
        snprintf(status->version, sizeof(status->version), "%s", atResp);
    }
    // Signal quality
    cellularSignalQuality_t signalQuality;
    err = get_signal_quality(&signalQuality);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "get_signal_quality failed with %d", err);
        snprintf(status->signalLevel, sizeof(status->signalLevel), "%s", "-");
    } else {
        snprintf(status->signalLevel, sizeof(status->signalLevel), "%s", signalQuality.quality);
    }
    // SIM card registration status、LAC、Cell ID, {+CREG: 2,1,"5F0C","E0B70B",7}
    snprintf(status->registerStatus, sizeof(status->registerStatus), "%s", "Unknown");
    snprintf(status->lac, sizeof(status->lac), "%s", "-");
    snprintf(status->cellId, sizeof(status->cellId), "%s", "-");
    memset(atResp, 0, sizeof(atResp));
    err = esp_modem_at(g_cat1.dce, "AT+CREG?", atResp, 500);
    ESP_LOGI(TAG, "AT+CREG?=>%s", atResp);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_modem_at(AT+CREG?) failed with %d(%s)", err, atResp);
    } else {
        char delimiters[] = ":,\"";
        char *saveptr;
        char *p = strtok_r(atResp, delimiters, &saveptr);
        int field = 0;
        while (p) {
            switch (field) {
            case 2: {
                int n = atoi(p);
                switch (n) {
                case 0:
                    snprintf(status->registerStatus, sizeof(status->registerStatus), "%s", "Not registered");
                    break;
                case 1:
                    snprintf(status->registerStatus, sizeof(status->registerStatus), "%s", "Registered (Home network)");
                    break;
                case 2:
                    snprintf(status->registerStatus, sizeof(status->registerStatus), "%s", "Searching");
                    break;
                case 3:
                    snprintf(status->registerStatus, sizeof(status->registerStatus), "%s", "Registration denied");
                    break;
                case 4:
                    snprintf(status->registerStatus, sizeof(status->registerStatus), "%s", "Unknown");
                    break;
                case 5:
                    snprintf(status->registerStatus, sizeof(status->registerStatus), "%s", "Registered (Roaming)");
                    break;
                default:
                    break;
                }
                break;
            }
            case 3: {
                snprintf(status->lac, sizeof(status->lac), "%s", p);
                break;
            }
            case 4: {
                snprintf(status->cellId, sizeof(status->cellId), "%s", p);
                break;
            }
            default:
                break;
            }
            p = strtok_r(NULL, delimiters, &saveptr);
            field++;
        }
    }

    if (strlen(status->imsi) >= 5) {
        strncpy(status->plmnId, status->imsi, 5);
    } else {
        snprintf(status->plmnId, sizeof(status->plmnId), "%s", "-");
    }
    // ICCID，{+QCCID: 89861121206083099081}
    snprintf(status->iccid, sizeof(status->iccid), "%s", "-");
    memset(atResp, 0, sizeof(atResp));
    err = esp_modem_at(g_cat1.dce, "AT+QCCID", atResp, 500);
    ESP_LOGI(TAG, "AT+QCCID=>%s", atResp);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_modem_at(AT+QCCID) failed with %d(%s)", err, atResp);
    } else {
        char delimiters[] = ": ";
        char *saveptr;
        char *p = strtok_r(atResp, delimiters, &saveptr);
        int field = 0;
        while (p) {
            switch (field) {
            case 1: {
                snprintf(status->iccid, sizeof(status->iccid), "%s", p);
                break;
            }
            default:
                break;
            }
            p = strtok_r(NULL, delimiters, &saveptr);
            field++;
        }
    }
    // ISP，{+COPS: 0,0,"CHN-CT",7}
    snprintf(status->isp, sizeof(status->isp), "%s", "-");
    memset(atResp, 0, sizeof(atResp));
    err = esp_modem_at(g_cat1.dce, "AT+COPS?", atResp, 500);
    ESP_LOGI(TAG, "AT+COPS?=>%s", atResp);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_modem_at(AT+COPS?) failed with %d(%s)", err, atResp);
    } else {
        char delimiters[] = ":,\"";
        char *saveptr;
        char *p = strtok_r(atResp, delimiters, &saveptr);
        int field = 0;
        while (p) {
            switch (field) {
            case 3: {
                snprintf(status->isp, sizeof(status->isp), "%s", p);
                break;
            }
            default:
                break;
            }
            p = strtok_r(NULL, delimiters, &saveptr);
            field++;
        }
    }
    // {+QNWINFO: "FDD LTE","46011","LTE BAND 1",100}
    snprintf(status->networkType, sizeof(status->networkType), "%s", "-");
    memset(atResp, 0, sizeof(atResp));
    err = esp_modem_at(g_cat1.dce, "AT+QNWINFO", atResp, 500);
    ESP_LOGI(TAG, "AT+QNWINFO=>%s", atResp);
    if (err == ESP_OK && simCardReady) {
        char delimiters[] = ",\"";
        char *saveptr;
        char *p = strtok_r(atResp, delimiters, &saveptr);
        int field = 0;
        while (p) {
            switch (field) {
            case 1: {
                snprintf(status->networkType, sizeof(status->networkType), "%s", p);
                break;
            }
            default:
                break;
            }
            p = strtok_r(NULL, delimiters, &saveptr);
            field++;
        }
    } else {
        ESP_LOGE(TAG, "esp_modem_at(AT+QNWINFO) failed with %d(%s)", err, atResp);
    }

    //
    return ESP_OK;
}

/**
 * Power on cellular module
 * @return ESP_OK on success
 */
static esp_err_t power_on_modem()
{
    esp_err_t err = ESP_OK;

    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = GPIO_OUTPUT_PIN_SEL;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);

    gpio_set_level(GPIO_OUTPUT_PWRKEY, 0);
    vTaskDelay(pdMS_TO_TICKS(1000));
    gpio_set_level(GPIO_OUTPUT_PWRKEY, 1);
    vTaskDelay(pdMS_TO_TICKS(1000));

    return err;
}

/**
 * Initialize module parameters and status
 * @return ESP_OK on success
 */
static esp_err_t init_param_and_status()
{
    memset(&g_cat1.param, 0, sizeof(g_cat1.param));
    cfg_get_cellular_param_attr(&g_cat1.param);

    memset(&g_cat1.status, 0, sizeof(g_cat1.status));
    snprintf(g_cat1.status.networkStatus, sizeof(g_cat1.status.networkStatus), "%s", "Disconnected");
    snprintf(g_cat1.status.modemStatus, sizeof(g_cat1.status.modemStatus), "%s", "No SIM Card");
    snprintf(g_cat1.status.model, sizeof(g_cat1.status.model), "%s", "-");
    snprintf(g_cat1.status.version, sizeof(g_cat1.status.version), "%s", "-");
    snprintf(g_cat1.status.signalLevel, sizeof(g_cat1.status.signalLevel), "%s", "-");
    snprintf(g_cat1.status.registerStatus, sizeof(g_cat1.status.registerStatus), "%s", "Unknown");
    snprintf(g_cat1.status.imei, sizeof(g_cat1.status.imei), "%s", "-");
    snprintf(g_cat1.status.imsi, sizeof(g_cat1.status.imsi), "%s", "-");
    snprintf(g_cat1.status.iccid, sizeof(g_cat1.status.iccid), "%s", "-");
    snprintf(g_cat1.status.isp, sizeof(g_cat1.status.isp), "%s", "-");
    snprintf(g_cat1.status.networkType, sizeof(g_cat1.status.networkType), "%s", "-");
    snprintf(g_cat1.status.plmnId, sizeof(g_cat1.status.plmnId), "%s", "-");
    snprintf(g_cat1.status.lac, sizeof(g_cat1.status.lac), "%s", "-");
    snprintf(g_cat1.status.cellId, sizeof(g_cat1.status.cellId), "%s", "-");
    snprintf(g_cat1.status.ipv4Address, sizeof(g_cat1.status.ipv4Address), "%s", "0.0.0.0/0");
    snprintf(g_cat1.status.ipv4Gateway, sizeof(g_cat1.status.ipv4Gateway), "%s", "0.0.0.0");
    snprintf(g_cat1.status.ipv4Dns, sizeof(g_cat1.status.ipv4Dns), "%s", "0.0.0.0");
    snprintf(g_cat1.status.ipv6Address, sizeof(g_cat1.status.ipv6Address), "%s", "::");
    snprintf(g_cat1.status.ipv6Gateway, sizeof(g_cat1.status.ipv6Gateway), "%s", "::");
    snprintf(g_cat1.status.ipv6Dns, sizeof(g_cat1.status.ipv6Dns), "%s", "::");

    return ESP_OK;
}

/**
 * Check and configure module baud rate
 * @return ESP_OK on success
 */
static esp_err_t check_baud_rate()
{
    esp_err_t err = ESP_OK;

    uint32_t baudRate = 0;
    cfg_get_cellular_baud_rate(&baudRate);
    ESP_LOGI(TAG, "Baud rate: %ld", baudRate);
    uart_driver_install(UART_NUM_1, 2048, 2048, 0, NULL, 0);
    err = cat1_set_baud_rate(baudRate);
    uart_driver_delete(UART_NUM_1);
    cfg_set_cellular_baud_rate(CAT1_BAUD_RATE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "cat1_set_baud_rate failed with %d", err);
        return err;
    }

    //
    esp_modem_dte_config_t dte_config = ESP_MODEM_DTE_DEFAULT_CONFIG();
    dte_config.uart_config.baud_rate = CAT1_BAUD_RATE;
    dte_config.uart_config.tx_io_num = MODEM_UART_TX_PIN;
    dte_config.uart_config.rx_io_num = MODEM_UART_RX_PIN;
    dte_config.uart_config.rx_buffer_size = 8192;
    dte_config.uart_config.tx_buffer_size = 8192;
    esp_modem_dce_config_t dce_config = ESP_MODEM_DCE_DEFAULT_CONFIG(g_cat1.param.apn);
    esp_netif_config_t netif_ppp_config = ESP_NETIF_DEFAULT_PPP();
    g_cat1.esp_netif = esp_netif_new(&netif_ppp_config);
    g_cat1.dce = esp_modem_new_dev(ESP_MODEM_DCE_EC800E, &dte_config, &dce_config, g_cat1.esp_netif);

    return ESP_OK;
}

/**
 * Check SIM PIN status and enter PIN if required
 * @return ESP_OK on success
 */
static esp_err_t check_pin_status()
{
    char atCmd[256];
    char atResp[256];
    esp_err_t err = ESP_OK;

    memset(atResp, 0, sizeof(atResp));
    snprintf(atCmd, sizeof(atCmd), "%s", "ATE0");
    err = esp_modem_at(g_cat1.dce, atCmd, atResp, 500);
    ESP_LOGI(TAG, "%s=>%s", atCmd, atResp);

    // Check if a PIN is required
    int retry = 0;
    while (retry++ < 10) {
        memset(atResp, 0, sizeof(atResp));
        snprintf(atCmd, sizeof(atCmd), "%s", "AT+CPIN?");
        err = esp_modem_at(g_cat1.dce, atCmd, atResp, 500);
        ESP_LOGI(TAG, "%s=>%s", atCmd, atResp);
        if (err == ESP_OK && strstr(atResp, "+CPIN:") != NULL) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    if (err == ESP_OK) {
        if (strstr(atResp, "READY") != NULL) {
            // No PIN required
            snprintf(g_cat1.status.modemStatus, sizeof(g_cat1.status.modemStatus), "%s", "Ready");
        } else if (strstr(atResp, "SIM PIN")) {
            // PIN code is required, try to enter the PIN code
            if (g_cat1.param.pin[0] == '\0') {
                err = ESP_FAIL;
                ESP_LOGE(TAG, "PIN code is required, please set it in the configuration");
                snprintf(g_cat1.status.modemStatus, sizeof(g_cat1.status.modemStatus), "%s", "PIN Required");
            } else {
                memset(atResp, 0, sizeof(atResp));
                snprintf(atCmd, sizeof(atCmd), "AT+CPIN=%s", g_cat1.param.pin);//兼容EG912U-GL修改
                err = esp_modem_at(g_cat1.dce, atCmd, atResp, 5000);
                ESP_LOGI(TAG, "%s=>%s", atCmd, atResp);
                if (err == ESP_OK) {
                    ESP_LOGI(TAG, "esp_modem_at(%s) success", atCmd);
                    snprintf(g_cat1.status.modemStatus, sizeof(g_cat1.status.modemStatus), "%s", "Ready");
                } else {
                    ESP_LOGE(TAG, "esp_modem_at(%s) failed with %d(%s)", atCmd, err, atResp);
                    snprintf(g_cat1.status.modemStatus, sizeof(g_cat1.status.modemStatus), "%s", "PIN Error");
                }
            }
        } else if (strstr(atResp, "SIM PUK")) {
            // PUK code is required and needs to be solved by the user
            err = ESP_FAIL;
            ESP_LOGE(TAG, "PUK code is required, please contact your service provider");
            snprintf(g_cat1.status.modemStatus, sizeof(g_cat1.status.modemStatus), "%s", "PUK Required");
        } else {
            // Other states are not processed yet and are considered SIM card errors.
            err = ESP_FAIL;
            ESP_LOGE(TAG, "PIN status is not supported");
            snprintf(g_cat1.status.modemStatus, sizeof(g_cat1.status.modemStatus), "%s", "SIM Card Error");
        }
    } else {
        ESP_LOGE(TAG, "SIM card error");
        int errCode = -1;
        sscanf(atResp, "+CME ERROR: %d", &errCode);
        switch (errCode) {
        case 10:
            snprintf(g_cat1.status.modemStatus, sizeof(g_cat1.status.modemStatus), "%s", "No SIM Card");
            break;
        default:
            snprintf(g_cat1.status.modemStatus, sizeof(g_cat1.status.modemStatus), "%s", "SIM Card Error");
        }
    }

    return err;
}

/**
 * Establish network connection
 * @return ESP_OK on success
 */
esp_err_t connect_to_network()
{
    char atCmd[256];
    char atResp[256];
    esp_err_t err = ESP_OK;

    // Set apn related information
    if (g_cat1.param.apn[0] != '\0') {
        memset(atResp, 0, sizeof(atResp));
        snprintf(atCmd, sizeof(atCmd), "AT+QICSGP=1,1,\"%s\",\"%s\",\"%s\",%d", g_cat1.param.apn, g_cat1.param.user, g_cat1.param.password, g_cat1.param.authentication);
        err = esp_modem_at(g_cat1.dce, atCmd, atResp, 500);
        ESP_LOGI(TAG, "%s=>%s", atCmd, atResp);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_modem_at(%s) failed with %d(%s)", atCmd, err, atResp);
            snprintf(g_cat1.status.modemStatus, sizeof(g_cat1.status.modemStatus), "%s", "SIM Card Error");
        }
    }

    // Activate roaming service
    memset(atResp, 0, sizeof(atResp));
    snprintf(atCmd, sizeof(atCmd), "%s", "AT+QCFG=\"roamservice\",2,1");
    err = esp_modem_at(g_cat1.dce, atCmd, atResp, 500);
    ESP_LOGI(TAG, "%s=>%s", atCmd, atResp);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_modem_at(%s) failed with %d(%s)", atCmd, err, atResp);
        snprintf(g_cat1.status.modemStatus, sizeof(g_cat1.status.modemStatus), "%s", "SIM Card Error");
    }

    // Enable network registration with location information, otherwise LAC and Cell ID cannot be obtained
    memset(atResp, 0, sizeof(atResp));
    snprintf(atCmd, sizeof(atCmd), "%s", "AT+CREG=2");
    err = esp_modem_at(g_cat1.dce, atCmd, atResp, 500);
    ESP_LOGI(TAG, "%s=>%s", atCmd, atResp);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_modem_at(%s) failed with %d(%s)", atCmd, err, atResp);
    }

    // CMUX mode dial
    err = esp_modem_set_mode(g_cat1.dce, ESP_MODEM_MODE_CMUX);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_modem_set_mode(ESP_MODEM_MODE_CMUX) failed with %d", err);
        snprintf(g_cat1.status.modemStatus, sizeof(g_cat1.status.modemStatus), "%s", "SIM Card Error");
    }

    return err;
}

/**
 * Task to initialize and start cellular module
 * @param pvParameters Unused
 */
static void task_start_modem(void *pvParameters)
{
    ESP_LOGI(TAG, "task_start_modem start");

    //
    xEventGroupClearBits(g_cat1.event_group, CAT1_STA_CONNECT_BIT);
    xEventGroupClearBits(g_cat1.event_group, CAT1_STA_DISCONNECT_BIT);

    //
    esp_err_t err = ESP_FAIL;
    do {
        if (power_on_modem() != ESP_OK) {
            ESP_LOGE(TAG, "power_on_modem failed");
            break;
        }
        if (init_param_and_status() != ESP_OK) {
            ESP_LOGE(TAG, "init_param_and_status failed");
            break;
        }
        if (check_baud_rate() != ESP_OK) {
            ESP_LOGE(TAG, "check_baud_rate failed");
            break;
        }
        g_cat1.is_opened = true;
        g_cat1.cat1_status = CAT1_STATUS_STARTING;
        if (check_pin_status() != ESP_OK) {
            ESP_LOGE(TAG, "check_pin_status failed");
            break;
        }
        if (connect_to_network() != ESP_OK) {
            ESP_LOGE(TAG, "connect_to_network failed");
            break;
        }
        err = ESP_OK;
        g_cat1.cat1_status = CAT1_STATUS_STARTED;
    } while (0);

    //
    if (err != ESP_OK) {
        xEventGroupSetBits(g_cat1.event_group, CAT1_STA_DISCONNECT_BIT);
    }

    //
    ESP_LOGI(TAG, "task_start_modem exit");
    vTaskDelete(NULL);
}

/**
 * Initialize CAT1 module
 * @param mode Operation mode
 */
void cat1_init(int mode)
{
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, &on_ip_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(NETIF_PPP_STATUS, ESP_EVENT_ANY_ID, &on_ppp_changed, NULL));

    g_cat1.mode = mode;
    g_cat1.event_group = xEventGroupCreate();
}

/**
 * Open CAT1 module connection
 */
void cat1_open()
{
    xEventGroupClearBits(g_cat1.event_group, CAT1_POWER_ON_BIT);
    xEventGroupClearBits(g_cat1.event_group, CAT1_STA_CONNECT_BIT);
    xEventGroupClearBits(g_cat1.event_group, CAT1_STA_DISCONNECT_BIT);
    BaseType_t task = xTaskCreatePinnedToCore((TaskFunction_t)task_start_modem, TAG, 8 * 1024, NULL, 4, NULL, 1);
    if (task == pdPASS) {
    } else {
        ESP_LOGE(TAG, "xTaskCreatePinnedToCore(task_start_modem) failed");
    }
}

/**
 * Wait for CAT1 module to open and connect
 */
void cat1_wait_open(void)
{
    //
    ESP_LOGI(TAG, "Waiting for IP address ...");
    EventBits_t bits = xEventGroupWaitBits(g_cat1.event_group, CAT1_STA_CONNECT_BIT | CAT1_STA_DISCONNECT_BIT, pdFALSE, pdFALSE, pdMS_TO_TICKS(CAT1_PPP_CONNECT_TIMEOUT_MS));
    if (bits & CAT1_STA_CONNECT_BIT) {
        ESP_LOGI(TAG, "Connected to PPP server");
    } else {
        ESP_LOGE(TAG, "Failed to connect to PPP server");
        mqtt_stop();
    }

    get_status(&g_cat1.status);
}

/**
 * Close CAT1 module connection
 */
void cat1_close(void)
{
    // esp_modem_destroy(g_cat1.dce);
    // esp_netif_destroy(g_cat1.esp_netif);
}

/**
 * Restart CAT1 module
 * @return ESP_OK on success
 */
esp_err_t cat1_restart(void)
{
    snprintf(g_cat1.status.networkStatus, sizeof(g_cat1.status.networkStatus), "%s", "Disconnected");
    snprintf(g_cat1.status.ipv4Address, sizeof(g_cat1.status.ipv4Address), "%s", "0.0.0.0/0");
    snprintf(g_cat1.status.ipv4Gateway, sizeof(g_cat1.status.ipv4Gateway), "%s", "0.0.0.0");
    snprintf(g_cat1.status.ipv4Dns, sizeof(g_cat1.status.ipv4Dns), "%s", "0.0.0.0");

    ESP_LOGI(TAG, "cat1_restart 1/3");
    g_cat1.cat1_status = CAT1_STATUS_STOPED;
    esp_modem_destroy(g_cat1.dce);
    g_cat1.dce = NULL;
    esp_netif_destroy(g_cat1.esp_netif);
    g_cat1.esp_netif = NULL;

    ESP_LOGI(TAG, "cat1_restart 2/3");
    cat1_open();
    cat1_wait_open();

    g_cat1.is_restarting = false;
    ESP_LOGI(TAG, "cat1_restart 3/3");

    return ESP_OK;
}

/**
 * Check if module is restarting
 * @return true if restarting, false otherwise
 */
bool cat1_is_restarting(void)
{
    return g_cat1.is_restarting;
}

/**
 * Send AT command to module
 * @param at AT command string
 * @param resp Response structure
 * @return ESP_OK on success
 */
esp_err_t cat1_send_at(const char *at, cellularCommandResp_t *resp)
{
    if (!g_cat1.is_opened) {
        ESP_LOGE(TAG, "cat1 send at failed, cat1 is not started");
        resp->result = ESP_FAIL;
        snprintf(resp->message, sizeof(resp->message), "%s", "ERROR");
        return ESP_FAIL;
    }

    esp_err_t err = ESP_OK;
    ESP_LOGI(TAG, "AT command: %s", at);
    resp->result = esp_modem_at(g_cat1.dce, at, resp->message, 500);
    ESP_LOGI(TAG, "AT response: %s, %d", resp->message, resp->result);
    switch (resp->result) {
    case ESP_FAIL:
        snprintf(resp->message, sizeof(resp->message), "%s", "ERROR");
        break;
    case ESP_ERR_TIMEOUT:
        snprintf(resp->message, sizeof(resp->message), "%s", "TIMEOUT");
        break;
    default:
        break;
    }
    return err;
}

/**
 * Get current cellular status
 * @param status Status structure to populate
 * @return ESP_OK on success
 */
esp_err_t cat1_get_cellular_status(cellularStatusAttr_t *status)
{
    if (g_cat1.is_opened) {
        get_status(&g_cat1.status);
    }

    memcpy(status, &g_cat1.status, sizeof(cellularStatusAttr_t));
    return ESP_OK;
}

/**
 * Check cellular connection status
 * @return ESP_OK if connected
 */
esp_err_t cat1_connect_check(void)
{
    esp_err_t err = ESP_OK;

    uint32_t baudRate = 0;

    power_on_modem();
    init_param_and_status();

    cfg_get_cellular_baud_rate(&baudRate);
    ESP_LOGI(TAG, "Baud rate: %ld", baudRate);
    uart_driver_install(UART_NUM_1, 2048, 2048, 0, NULL, 0);
    err = cat1_set_baud_rate(baudRate);
    uart_driver_delete(UART_NUM_1);
    return err;
}

/**
 * Task to display cellular status
 * @param pvParameters Unused
 */
static void task_show_status(void *pvParameters)
{
    cellularStatusAttr_t param;
    memset(&param, 0, sizeof(cellularStatusAttr_t));
    cat1_get_cellular_status(&param);
    printf("cat1 status:\n");
    printf("\tnetworkStatus: %s\n", param.networkStatus);
    printf("\tmodemStatus: %s\n", param.modemStatus);
    printf("\tmodel: %s\n", param.model);
    printf("\tversion: %s\n", param.version);
    printf("\tsignalLevel: %s\n", param.signalLevel);
    printf("\tregisterStatus: %s\n", param.registerStatus);
    printf("\timei: %s\n", param.imei);
    printf("\timsi: %s\n", param.imsi);
    printf("\ticcid: %s\n", param.iccid);
    printf("\tisp: %s\n", param.isp);
    printf("\tnetworkType: %s\n", param.networkType);
    printf("\tplmnId: %s\n", param.plmnId);
    printf("\tlac: %s\n", param.lac);
    printf("\tcellId: %s\n", param.cellId);
    printf("\tipv4Address: %s\n", param.ipv4Address);
    printf("\tipv4Gateway: %s\n", param.ipv4Gateway);
    printf("\tipv4Dns: %s\n", param.ipv4Dns);
    printf("\tipv6Address: %s\n", param.ipv6Address);
    printf("\tipv6Gateway: %s\n", param.ipv6Gateway);
    printf("\tipv6Dns: %s\n", param.ipv6Dns);

    vTaskDelete(NULL);
}

/**
 * Display cellular status information
 */
void cat1_show_status(void)
{
    xTaskCreatePinnedToCore((TaskFunction_t)task_show_status, TAG, 8 * 1024, NULL, 4, NULL, 1);
}
