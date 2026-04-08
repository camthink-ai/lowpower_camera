/*
 * PCF8563 RTC Library
 *
 * This library provides a complete interface to the PCF8563 Real-Time Clock.
 * It serves as the primary time source for the system, replacing the ESP32's
 * internal RTC which has poor accuracy.
 *
 * Features:
 * - Read/write RTC time
 * - Synchronize system time with RTC
 * - Set minute-precision alarms for deep sleep wakeup
 * - Automatic I2C bus management (shared with camera)
 *
 * Hardware connections (per board schematic):
 * - SDA: GPIO4 (shared with camera sensor SCCB)
 * - SCL: GPIO5 (shared with camera sensor SCCB)
 * - INT: GPIO20 (active low), used as EXT1 wakeup source
 * - Power: Provided by sensor module (SENSOR_POWER_IO = GPIO3)
 *
 * I2C Bus Sharing:
 * - The I2C bus is shared with the camera sensor module
 * - RTC functions will automatically install I2C driver if needed
 * - RTC does NOT uninstall I2C driver (safe for camera module reuse)
 * - Camera module can install/uninstall I2C driver independently
 */

#ifndef RTC_PCF8563_H
#define RTC_PCF8563_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Synchronize system time from PCF8563
 *
 * Reads time from RTC and updates the system time.
 * Should be called at system startup before NTP sync.
 *
 * @note Will install I2C driver if not already installed by camera module.
 * @note Validates RTC time is reasonable (after 2020-01-01) before syncing.
 *
 * @return ESP_OK on success
 *         ESP_ERR_INVALID_STATE if RTC time is invalid or I2C unavailable
 *         Other error codes on I2C failure
 */
esp_err_t rtc_sync_to_system(void);

/**
 * @brief Synchronize PCF8563 from system time
 *
 * Writes current system time to RTC.
 * Should be called after NTP sync or manual time update.
 *
 * @note Will install I2C driver if not already installed by camera module.
 * @note Validates system time is reasonable (after 2020-01-01) before writing.
 *
 * @return ESP_OK on success
 *         ESP_ERR_INVALID_STATE if system time is invalid or I2C unavailable
 *         Other error codes on I2C failure
 */
esp_err_t rtc_sync_from_system(void);

/**
 * @brief Set a wakeup alarm on PCF8563
 *
 * Sets an alarm to trigger after the specified number of seconds from current RTC time.
 * 
 * @note PCF8563 alarm has minute-precision only. The function will round
 *       to the nearest minute: <30s rounds down, >=30s rounds up.
 *
 * @note PCF8563 requires sensor power (SENSOR_POWER_IO) to be on.
 *       Caller should ensure sensor power is ON before calling this function.
 *
 * @note Will install I2C driver if not already installed. I2C driver is NOT
 *       uninstalled after setting alarm (safe for deep sleep).
 *
 * @note For cross-day alarms, day and weekday matching is automatically enabled.
 *
 * @param seconds_from_now Seconds from current RTC time to trigger alarm
 * @param[out] actual_wakeup_time Actual alarm time (minute-aligned), can be NULL
 * @return ESP_OK on success
 *         ESP_ERR_INVALID_ARG if seconds_from_now is 0
 *         ESP_ERR_INVALID_STATE if sensor power is off or I2C not available
 *         Other error codes on I2C failure
 */
esp_err_t rtc_set_alarm(uint32_t seconds_from_now, time_t *actual_wakeup_time);

/**
 * @brief Clear the alarm flag after wakeup
 *
 * Should be called after waking up from an RTC alarm.
 * Safe to call even if alarm didn't trigger.
 *
 * @note Silently returns if I2C driver is not available.
 */
void rtc_clear_alarm(void);

/**
 * @brief Get the EXT1 wakeup mask for the RTC INT pin
 *
 * Use this when configuring ESP32 deep sleep EXT1 wakeup.
 *
 * @return Bitmask for EXT1 wakeup (1ULL << GPIO20)
 */
uint64_t rtc_get_wakeup_mask(void);


#ifdef __cplusplus
}
#endif

#endif /* RTC_PCF8563_H */
