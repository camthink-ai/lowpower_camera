/*
 * PCF8563 RTC Library
 *
 * Complete Real-Time Clock driver for PCF8563 using ESP-IDF I2C driver.
 * No third-party libraries required.
 *
 * This module serves as the primary time source for the system, providing
 * accurate timekeeping during deep sleep when the ESP32's internal RTC
 * loses accuracy.
 *
 * Hardware connections (per board schematic):
 *   - SDA: GPIO4 (shared with camera sensor SCCB)
 *   - SCL: GPIO5 (shared with camera sensor SCCB)
 *   - INT: GPIO20 (active low), used for alarm wakeup
 *   - Power: Provided by sensor module (SENSOR_POWER_IO = GPIO3)
 *
 * I2C Bus Sharing:
 *   - The I2C bus is shared with the camera sensor module
 *   - Camera driver (esp_camera_init) installs the I2C driver
 *   - RTC functions will install I2C driver if not already installed
 *   - RTC does NOT uninstall I2C driver (leave it for camera or deep sleep)
 */

#include "rtc_pcf8563.h"

#include <string.h>
#include <sys/time.h>
#include <inttypes.h>
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "misc.h"
#include "esp_err.h"

// PCF8563 I2C address
#define PCF8563_ADDR            0x51

// Register map
#define REG_CTRL_STATUS1        0x00
#define REG_CTRL_STATUS2        0x01
#define REG_VL_SECONDS          0x02
#define REG_MINUTES             0x03
#define REG_HOURS               0x04
#define REG_DAYS                0x05
#define REG_WEEKDAYS            0x06
#define REG_CENT_MONTHS         0x07
#define REG_YEARS               0x08
#define REG_ALARM_MIN           0x09
#define REG_ALARM_HOUR          0x0A
#define REG_ALARM_DAY           0x0B
#define REG_ALARM_WDAY          0x0C

// Bits in CTRL_STATUS2
#define BIT_AIE                 (1 << 1)    // Alarm interrupt enable
#define BIT_AF                  (1 << 3)    // Alarm flag

// Alarm enable (AE) bit in alarm registers (bit7)
#define BIT_AE                  (1 << 7)

// INT pin used for wakeup
#define PCF8563_INT_GPIO        GPIO_NUM_3

// I2C bus configuration (shared with camera)
#if CONFIG_SCCB_HARDWARE_I2C_PORT1
    #define PCF8563_I2C_PORT    I2C_NUM_1
#else
    #define PCF8563_I2C_PORT    I2C_NUM_0
#endif

#define PCF8563_I2C_SDA         GPIO_NUM_4
#define PCF8563_I2C_SCL         GPIO_NUM_5
#define PCF8563_I2C_FREQ_HZ     100000      // 100kHz for PCF8563

static bool g_is_initialized = false;

static const char *TAG = "-->RTC";

// BCD conversion helpers
static inline uint8_t bin2bcd(uint8_t val)
{
    return ((val / 10) << 4) | (val % 10);
}

static inline uint8_t bcd2bin(uint8_t val)
{
    return ((val >> 4) * 10) + (val & 0x0F);
}

// I2C write operation with retry
// Note: Caller should ensure I2C driver is installed before calling this function
static esp_err_t pcf8563_write_reg(uint8_t reg, const uint8_t *data, size_t len)
{
    const int max_retries = 3;
    for (int retry = 0; retry < max_retries; retry++) {
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        if (!cmd) {
            return ESP_ERR_NO_MEM;
        }

        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (PCF8563_ADDR << 1) | I2C_MASTER_WRITE, true);
        i2c_master_write_byte(cmd, reg, true);
        if (len > 0 && data) {
            i2c_master_write(cmd, (uint8_t *)data, len, true);
        }
        i2c_master_stop(cmd);

        esp_err_t ret = i2c_master_cmd_begin(PCF8563_I2C_PORT, cmd, pdMS_TO_TICKS(500));
        i2c_cmd_link_delete(cmd);
        
        if (ret == ESP_OK) {
            return ESP_OK;
        }
        
        if (ret == ESP_ERR_INVALID_STATE) {
            return ret;  // Don't retry if driver not installed
        }
        
        if (retry < max_retries - 1) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
    
    return ESP_ERR_TIMEOUT;
}

// I2C read operation with retry
// Note: Caller should ensure I2C driver is installed before calling this function
static esp_err_t pcf8563_read_reg(uint8_t reg, uint8_t *data, size_t len)
{
    if (!data || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    const int max_retries = 3;
    for (int retry = 0; retry < max_retries; retry++) {
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        if (!cmd) {
            return ESP_ERR_NO_MEM;
        }

        // Write register address
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (PCF8563_ADDR << 1) | I2C_MASTER_WRITE, true);
        i2c_master_write_byte(cmd, reg, true);

        // Re-start and read data
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (PCF8563_ADDR << 1) | I2C_MASTER_READ, true);

        if (len > 1) {
            i2c_master_read(cmd, data, len - 1, I2C_MASTER_ACK);
        }
        i2c_master_read_byte(cmd, data + len - 1, I2C_MASTER_NACK);
        i2c_master_stop(cmd);

        esp_err_t ret = i2c_master_cmd_begin(PCF8563_I2C_PORT, cmd, pdMS_TO_TICKS(500));
        i2c_cmd_link_delete(cmd);
        
        if (ret == ESP_OK) {
            return ESP_OK;
        }
        
        if (ret == ESP_ERR_INVALID_STATE) {
            return ret;  // Don't retry if driver not installed
        }
        
        if (retry < max_retries - 1) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
    
    return ESP_ERR_TIMEOUT;
}

/*
 * Install I2C driver if not already installed
 *
 * I2C Bus Sharing Strategy:
 * - Camera sensor uses the same I2C bus (GPIO4/GPIO5) for SCCB communication
 * - Camera driver (esp_camera_init) installs I2C driver when camera opens
 * - Camera driver (esp_camera_deinit) may uninstall I2C driver when camera closes
 * 
 * RTC needs I2C in these scenarios:
 * 1. System boot after misc_open(): Read RTC time before camera init
 *    → Install I2C, read RTC, then uninstall to let camera install cleanly
 * 2. Camera active: I2C already installed by camera → Reuse existing driver
 * 3. Before deep sleep: Camera closed, I2C may be uninstalled → Install and keep
 *
 */
static esp_err_t install_i2c_driver(void)
{
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = PCF8563_I2C_SDA,
        .scl_io_num = PCF8563_I2C_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = PCF8563_I2C_FREQ_HZ,  // 100kHz compatible with PCF8563 and camera sensor
    };

    esp_err_t ret = i2c_param_config(PCF8563_I2C_PORT, &conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C param config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = i2c_driver_install(PCF8563_I2C_PORT, conf.mode, 0, 0, 0);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "I2C driver install failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "I2C driver installed for RTC");
    
    return ESP_OK;
}

/*
 * Calculate rounded alarm time (minute-precision)
 *
 * PCF8563 alarm only supports minute-precision (no seconds field).
 * This function rounds the target time to the nearest minute and ensures
 * the alarm is always in the future.
 *
 * Rounding rules:
 * - Target seconds < 30: round down to current minute (XX:YY:00)
 * - Target seconds >= 30: round up to next minute (XX:YY+1:00)
 *
 * @param rtc_now Current RTC time
 * @param seconds_from_now Requested alarm offset in seconds
 * @return Actual alarm time (Unix timestamp)
 */
static time_t calculate_alarm_time(time_t rtc_now, uint32_t seconds_from_now)
{
    time_t target = rtc_now + seconds_from_now;
    struct tm target_tm;
    localtime_r(&target, &target_tm);

    // Round to nearest minute
    if (target_tm.tm_sec >= 30) {
        target += 60 - target_tm.tm_sec;
    } else {
        target -= target_tm.tm_sec;
    }

    // Ensure alarm is in the future (handle edge case where rounding causes past time)
    if (target <= rtc_now) {
        ESP_LOGW(TAG, "Calculated alarm is in past, advancing by 1 minute");
        target += 60;  // Advance by 1 minute to ensure future time
    }

    return target;
}

/*
 * Program PCF8563 alarm registers
 *
 * Sets alarm registers in a safe sequence:
 * 1. Disable alarm interrupt and clear flag (prevent immediate trigger)
 * 2. Write alarm time registers (minute, hour, day, weekday)
 * 3. Enable alarm interrupt
 *
 * @param alarm_time Alarm time
 * @return ESP_OK on success, error code otherwise
 */
static esp_err_t program_alarm_registers(time_t alarm_time)
{
    struct tm alarm_tm;
    localtime_r(&alarm_time, &alarm_tm);
    esp_err_t ret;

    // STEP 1: Disable alarm interrupt and clear flag (prevent immediate trigger)
    uint8_t status2 = 0;
    ret = pcf8563_read_reg(REG_CTRL_STATUS2, &status2, 1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read CTRL_STATUS2: %s", esp_err_to_name(ret));
        return ret;
    }

    status2 &= ~(BIT_AIE | BIT_AF);
    ret = pcf8563_write_reg(REG_CTRL_STATUS2, &status2, 1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to disable alarm: %s", esp_err_to_name(ret));
        return ret;
    }
    vTaskDelay(pdMS_TO_TICKS(5));

    // STEP 2: Program alarm registers
    uint8_t alarm_regs[4];
    alarm_regs[0] = bin2bcd(alarm_tm.tm_min) & 0x7F;  // minutes, AE=0
    alarm_regs[1] = bin2bcd(alarm_tm.tm_hour) & 0x3F; // hours, AE=0
    alarm_regs[2] = bin2bcd(alarm_tm.tm_mday) & 0x3F;
    alarm_regs[3] = alarm_tm.tm_wday & 0x07;
    ESP_LOGD(TAG, "Alarm registers: %02X, %02X, %02X, %02X", alarm_regs[0], alarm_regs[1], alarm_regs[2], alarm_regs[3]);

    ret = pcf8563_write_reg(REG_ALARM_MIN, alarm_regs, 4);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write alarm registers: %s", esp_err_to_name(ret));
        return ret;
    }
    vTaskDelay(pdMS_TO_TICKS(5));

    // STEP 3: Enable alarm interrupt (with flag cleared)
    ret = pcf8563_read_reg(REG_CTRL_STATUS2, &status2, 1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read CTRL_STATUS2: %s", esp_err_to_name(ret));
        return ret;
    }

    status2 &= ~BIT_AF;  // Ensure flag is clear
    status2 |= BIT_AIE;  // Enable alarm interrupt
    ret = pcf8563_write_reg(REG_CTRL_STATUS2, &status2, 1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable alarm: %s", esp_err_to_name(ret));
        return ret;
    }

    return ESP_OK;
}

/******************************************************************************
 * Public API Implementation
 ******************************************************************************/

static esp_err_t pcf8563_init(void)
{
    if (g_is_initialized) {
        return ESP_OK;
    }
    esp_err_t ret = ESP_OK;
    misc_io_set(CAMERA_POWER_IO, 1);
    vTaskDelay(pdMS_TO_TICKS(10));

    ret = install_i2c_driver();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install I2C driver: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "RTC initialized (I2C port %d, SDA=%d, SCL=%d, INT=%d)",
             PCF8563_I2C_PORT, PCF8563_I2C_SDA, PCF8563_I2C_SCL, PCF8563_INT_GPIO);
    g_is_initialized = true;

    return ESP_OK;
}

static esp_err_t pcf8563_deinit(void)
{
    if (!g_is_initialized) {
        return ESP_OK;
    }
    esp_err_t ret = i2c_driver_delete(PCF8563_I2C_PORT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to delete I2C driver: %s", esp_err_to_name(ret));
        return ret;
    }
    g_is_initialized = false;
    return ESP_OK;
}

static esp_err_t pcf8563_get_time(time_t *t)
{
    // Read time registers (7 bytes: seconds, minutes, hours, day, weekday, month, year)
    uint8_t data[7];
    esp_err_t ret = pcf8563_read_reg(REG_VL_SECONDS, data, 7);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read time: %s", esp_err_to_name(ret));
        return ret;
    }

    // Parse BCD values
    struct tm tm_time = {0};
    tm_time.tm_sec = bcd2bin(data[0] & 0x7F);
    tm_time.tm_min = bcd2bin(data[1] & 0x7F);
    tm_time.tm_hour = bcd2bin(data[2] & 0x3F);
    tm_time.tm_mday = bcd2bin(data[3] & 0x3F);
    tm_time.tm_wday = data[4] & 0x07;
    tm_time.tm_mon = bcd2bin(data[5] & 0x1F) - 1;  // PCF8563: 1-12, tm: 0-11
    
    // PCF8563 stores year as 0-99 (last two digits)
    // Map 0-99 to 2000-2099
    uint8_t year_bcd = bcd2bin(data[6]);
    tm_time.tm_year = year_bcd + 100;  // tm_year: years since 1900
    
    tm_time.tm_isdst = -1;  // Let mktime determine DST

    // Convert to time_t
    *t = mktime(&tm_time);
    
    return ESP_OK;
}

static esp_err_t pcf8563_set_time(time_t t)
{
    struct tm tm_time;
    localtime_r(&t, &tm_time);

    // Prepare BCD-encoded time data
    uint8_t data[7];
    data[0] = bin2bcd((uint8_t)tm_time.tm_sec) & 0x7F;   // seconds, VL=0
    data[1] = bin2bcd((uint8_t)tm_time.tm_min) & 0x7F;   // minutes
    data[2] = bin2bcd((uint8_t)tm_time.tm_hour) & 0x3F;  // hours
    data[3] = bin2bcd((uint8_t)tm_time.tm_mday) & 0x3F;  // day
    data[4] = (uint8_t)tm_time.tm_wday & 0x07;            // weekday
    data[5] = bin2bcd((uint8_t)(tm_time.tm_mon + 1)) & 0x1F;  // month (1-12)
    data[6] = bin2bcd((uint8_t)(tm_time.tm_year % 100));      // year (last 2 digits)

    esp_err_t ret = pcf8563_write_reg(REG_VL_SECONDS, data, 7);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write time: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "RTC time set to: %04d-%02d-%02d %02d:%02d:%02d",
             tm_time.tm_year + 1900, tm_time.tm_mon + 1, tm_time.tm_mday,
             tm_time.tm_hour, tm_time.tm_min, tm_time.tm_sec);

    return ESP_OK;
}

static esp_err_t pcf8563_sync_from_system_if_diff(time_t sys_time)
{
    time_t rtc_time;
    esp_err_t ret = pcf8563_get_time(&rtc_time);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "RTC read failed: %s", esp_err_to_name(ret));
        return ret;
    }
    if (sys_time - rtc_time > 10 || rtc_time - sys_time > 10) {
        ESP_LOGW(TAG, "RTC time is different from system time by more than 10 seconds, syncing from system");
        return pcf8563_set_time(sys_time);
    }

    return ESP_OK;
}

esp_err_t rtc_sync_to_system(void)
{
    pcf8563_init();
    
    time_t rtc_time;
    esp_err_t ret = pcf8563_get_time(&rtc_time);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "RTC read failed: %s", esp_err_to_name(ret));
        ESP_LOGW(TAG, "System will use default time until NTP sync");
        pcf8563_deinit();
        return ret;
    }

    // Verify RTC time is reasonable (2000-01-01 00:00:00 to 2037-12-31 23:59:59）
    // if (rtc_time < 946684800 || rtc_time > 2145887999) {
    //     rtc_time = 1735689600; //2025-01-01 00:00:00
    //     pcf8563_set_time(rtc_time);
    // }

    struct timeval tv = {
        .tv_sec = rtc_time,
        .tv_usec = 0,
    };
    
    settimeofday(&tv, NULL);
    
    struct tm tm_time;
    localtime_r(&rtc_time, &tm_time);
    ESP_LOGI(TAG, "System time synchronized from RTC: %04d-%02d-%02d %02d:%02d:%02d",
             tm_time.tm_year + 1900, tm_time.tm_mon + 1, tm_time.tm_mday,
             tm_time.tm_hour, tm_time.tm_min, tm_time.tm_sec);

    pcf8563_deinit();
    return ESP_OK;
}

esp_err_t rtc_sync_from_system(void)
{
    time_t sys_time = time(NULL);
    
    // Verify system time is reasonable (after 2020-01-01)
    // if (sys_time < 1577836800) {
    //     ESP_LOGW(TAG, "System time appears invalid (before 2020), skipping RTC update");
    //     sys_time = 1735689600; //2025-01-01 00:00:00
    // }
    
    pcf8563_init();
    esp_err_t ret = pcf8563_set_time(sys_time);
    if (ret == ESP_OK) {
        struct tm tm_time;
        localtime_r(&sys_time, &tm_time);
        ESP_LOGI(TAG, "RTC synchronized from system: %04d-%02d-%02d %02d:%02d:%02d",
                 tm_time.tm_year + 1900, tm_time.tm_mon + 1, tm_time.tm_mday,
                 tm_time.tm_hour, tm_time.tm_min, tm_time.tm_sec);
    }
    pcf8563_deinit();
    return ret;
}

esp_err_t rtc_set_alarm(uint32_t seconds_from_now, time_t *actual_wakeup_time)
{
    // Validate input
    if (seconds_from_now == 0) {
        ESP_LOGE(TAG, "Invalid alarm time: seconds_from_now must be > 0");
        return ESP_ERR_INVALID_ARG;
    }
    pcf8563_init();
    time_t sys_time = time(NULL);   
    esp_err_t ret = pcf8563_sync_from_system_if_diff(sys_time);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to sync from system: %s", esp_err_to_name(ret));
        pcf8563_deinit();
        return ret;
    }
    // Calculate rounded alarm time (minute-precision)
    time_t alarm_time = calculate_alarm_time(sys_time, seconds_from_now);

    // Program PCF8563 alarm registers
    if (program_alarm_registers(alarm_time) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to program alarm registers");
        pcf8563_deinit();
        return ESP_ERR_INVALID_STATE;
    }

    // Return actual alarm time to caller
    if (actual_wakeup_time) {
        *actual_wakeup_time = alarm_time;
    }

    // Log alarm info
    struct tm tm_time;
    localtime_r(&alarm_time, &tm_time);
    int32_t actual_diff = (int32_t)(alarm_time - sys_time);
    ESP_LOGI(TAG, "Alarm: %04d-%02d-%02d %02d:%02d (req: %" PRIu32 "s, act: %lds)",
             tm_time.tm_year + 1900, tm_time.tm_mon + 1, tm_time.tm_mday,
             tm_time.tm_hour, tm_time.tm_min,
             seconds_from_now, (long)actual_diff);

    pcf8563_deinit();
    return ESP_OK;
}

void rtc_clear_alarm(void)
{
    pcf8563_init();
    // Now try to read status register
    uint8_t status2 = 0;
    esp_err_t ret = pcf8563_read_reg(REG_CTRL_STATUS2, &status2, 1);
    if (ret != ESP_OK) {
        ESP_LOGD(TAG, "Failed to read CTRL_STATUS2 for alarm clear: %s", 
                 esp_err_to_name(ret));
        pcf8563_deinit();
        return;
    }

    if (status2 & BIT_AF) {
        status2 &= ~BIT_AF;
        ret = pcf8563_write_reg(REG_CTRL_STATUS2, &status2, 1);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Alarm flag cleared (was set)");
        } else {
            ESP_LOGW(TAG, "Failed to clear alarm flag: %s", esp_err_to_name(ret));
        }
    } else {
        ESP_LOGD(TAG, "Alarm flag already clear");
    }
    pcf8563_deinit();
}

uint64_t rtc_get_wakeup_mask(void)
{
    return (1ULL << PCF8563_INT_GPIO);
}
