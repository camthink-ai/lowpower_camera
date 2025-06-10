/*
 * Copyright 2021-2023 Morse Micro
 *
 * This file is licensed under terms that can be found in the LICENSE.md file in the root
 * directory of the Morse Micro IoT SDK software package.
 */

 /**
  * @defgroup MMWLAN Morse Micro Wireless LAN (mmwlan) API
  *
  * Wireless LAN control and datapath.
  *
  * @warning Aside from specific exceptions, the functions in this API must not be called
  *          concurrently (e.g., from different thread contexts). The exception to this
  *          is the TX API (@ref mmwlan_tx() and @ref mmwlan_tx_tid()).
  *
  * @{
  */

#pragma once

#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Enumeration of status return codes. */
enum mmwlan_status
{
    /** The operation was successful. */
    MMWLAN_SUCCESS,
    /** The operation failed with an unspecified error. */
    MMWLAN_ERROR,
    /** The operation failed due to an invalid argument. */
    MMWLAN_INVALID_ARGUMENT,
    /** Functionality is temporarily unavailable. */
    MMWLAN_UNAVAILABLE,
    /** Unable to proceed because channel list has not been set.
     *  @see mmwlan_set_channel_list(). */
    MMWLAN_CHANNEL_LIST_NOT_SET,
    /** Failed due to memory allocation failure. */
    MMWLAN_NO_MEM,
    /** Failed due to timeout */
    MMWLAN_TIMED_OUT,
    /** Used to indicate that a call to @ref mmwlan_sta_disable() did not shutdown
     *  the transceiver. */
    MMWLAN_SHUTDOWN_BLOCKED,
    /** Attempted to tune to a channel that was not in the regulatory database or not supported. */
    MMWLAN_CHANNEL_INVALID,
};

/** Maximum allowable length of an SSID. */
#define MMWLAN_SSID_MAXLEN          (32)

/** Maximum allowable length of a passphrase when connecting to an AP. */
#define MMWLAN_PASSPHRASE_MAXLEN    (100)

/** Maximum allowable Restricted Access Window (RAW) priority for STA. */
#define MMWLAN_RAW_MAX_PRIORITY     (7)

/** Length of a WLAN MAC address. */
#define MMWLAN_MAC_ADDR_LEN         (6)

/** Maximum allowable number of EC Groups. */
#define MMWLAN_MAX_EC_GROUPS        (4)

/** Default Background scan short interval in seconds.
 *  Setting to 0 will disable background scanning. */
#define DEFAULT_BGSCAN_SHORT_INTERVAL_S (0)

/** Default Background scan signal threshold in dBm. */
#define DEFAULT_BGSCAN_THRESHOLD_DBM    (0)

/** Default Background scan long interval in seconds.
 *  Setting to 0 will disable background scanning. */
#define DEFAULT_BGSCAN_LONG_INTERVAL_S  (0)

/** Default Target Wake Time (TWT) interval in micro seconds. */
#define DEFAULT_TWT_WAKE_INTERVAL_US        (300000000)

/** Default min Target Wake Time (TWT) duration in micro seconds. */
#define DEFAULT_TWT_MIN_WAKE_DURATION_US    (65280)

/** Enumeration of supported security types. */
enum mmwlan_security_type
{
    /** Open (no security) */
    MMWLAN_OPEN,
    /** Opportunistic Wireless Encryption (OWE) */
    MMWLAN_OWE,
    /** Simultaneous Authentication of Equals (SAE) */
    MMWLAN_SAE,
};

/** Enumeration of supported 802.11 power save modes. */
enum mmwlan_ps_mode
{
    /** Power save disabled */
    MMWLAN_PS_DISABLED,
    /** Power save enabled */
    MMWLAN_PS_ENABLED
};

/** Enumeration of Protected Management Frame (PMF) modes (802.11w). */
enum mmwlan_pmf_mode
{
    /** Protected management frames must be used */
    MMWLAN_PMF_REQUIRED,
    /** No protected management frames */
    MMWLAN_PMF_DISABLED
};

/** Enumeration of Centralized Authentication Control (CAC) modes. */
enum mmwlan_cac_mode
{
    /** CAC disabled */
    MMWLAN_CAC_DISABLED,
    /** CAC enabled */
    MMWLAN_CAC_ENABLED
};

/**
 * Enumeration of Target Wake Time (TWT) modes.
 *
 * @note TWT is only supported as a requester.
 */
enum mmwlan_twt_mode
{
    /** TWT disabled */
    MMWLAN_TWT_DISABLED,
    /** TWT enabled as a requester */
    MMWLAN_TWT_REQUESTER,
    /** TWT enabled as a responder */
    MMWLAN_TWT_RESPONDER
};

/** Enumeration of Target Wake Time (TWT) setup commands. */
enum mmwlan_twt_setup_command
{
    /** TWT setup request command */
    MMWLAN_TWT_SETUP_REQUEST,
    /** TWT setup suggest command */
    MMWLAN_TWT_SETUP_SUGGEST,
    /** TWT setup demand command */
    MMWLAN_TWT_SETUP_DEMAND
};

/**
 * @defgroup MMWLAN_REGDB    WLAN Regulatory Database API
 *
 * @{
 */

/**
 * If either the global or s1g operating class is set to this, the operating class will
 * not be checked when associating to an AP.
 */
#define MMWLAN_SKIP_OP_CLASS_CHECK -1

/** Regulatory domain information about an S1G channel. */
struct mmwlan_s1g_channel
{
    /** Center frequency of the channel (in Hz). */
    uint32_t centre_freq_hz;
    /** STA Duty Cycle (in 100th of %). */
    uint16_t duty_cycle_sta;
    /** Boolean indicating whether to omit control response frames from duty cycle. */
    bool duty_cycle_omit_ctrl_resp;
    /** Global operating class. If @ref MMWLAN_SKIP_OP_CLASS_CHECK, check is skipped */
    int16_t global_operating_class;
    /** S1G operating class. If @ref MMWLAN_SKIP_OP_CLASS_CHECK, check is skipped */
    int16_t s1g_operating_class;
    /** S1G channel number. */
    uint8_t s1g_chan_num;
    /** Channel operating bandwidth (in MHz). */
    uint8_t bw_mhz;
    /** Maximum transmit power (EIRP in dBm). */
    int8_t max_tx_eirp_dbm;
    /** The length of time to close the tx window between packets (in microseconds). */
    uint32_t pkt_spacing_us;
    /** The minimum packet airtime duration to trigger spacing (in microseconds). */
    uint32_t airtime_min_us;
    /** The maximum allowable packet airtime duration (in microseconds). */
    uint32_t airtime_max_us;
};

/** A list of S1G channels supported by a given regulatory domain. */
struct mmwlan_s1g_channel_list
{
    /** Two character country code (null-terminated) used to identify the regulatory domain. */
    uint8_t country_code[3];
    /** The number of channels in the list. */
    unsigned num_channels;
    /** The channel data. */
    const struct mmwlan_s1g_channel *channels;
};

/**
 * Regulatory database data structure. This is a list of @c mmwlan_s1g_channel_list structs, where
 * each channel list corresponds to a regulatory domain.
 */
struct mmwlan_regulatory_db
{
    /** Number of regulatory domains in the database. */
    unsigned num_domains;

    /** The regulatory domain data */
    const struct mmwlan_s1g_channel_list **domains;
};

/**
 * Look up the given country code in the regulatory database and return the matching channel
 * list if found.
 *
 * @param db            The regulatory database.
 * @param country_code  Country code to look up.
 *
 * @returns the matching channel list if found, else NULL.
 */
static inline const struct mmwlan_s1g_channel_list *
mmwlan_lookup_regulatory_domain(const struct mmwlan_regulatory_db *db, const char *country_code)
{
    unsigned ii;

    if (db == NULL)
    {
        return NULL;
    }

    for (ii = 0; ii < db->num_domains; ii++)
    {
        const struct mmwlan_s1g_channel_list *channel_list = db->domains[ii];
        if (channel_list->country_code[0] == country_code[0] &&
            channel_list->country_code[1] == country_code[1])
        {
            return channel_list;
        }
    }
    return NULL;
}

/**
 * Set the list of channels that are supported by the regulatory domain in which the device
 * resides.
 *
 * @note Must be invoked after WLAN initialization (see @ref mmwlan_init()) but only when inactive
 *       (i.e., STA not enabled).
 *
 * @warning This function takes a reference to the given channel list. It expects the channel
 *          list to remain valid in memory as long as WLAN is in use, or until a new channel
 *          list is configured.
 *
 * @param channel_list  The channel list to set. The list must remain valid in memory.
 *
 * @return @ref MMWLAN_SUCCESS if the channel list was valid and updated successfully,
 *         @ref MMWLAN_UNAVAILABLE if the WLAN subsystem was currently active.
 */
enum mmwlan_status mmwlan_set_channel_list(const struct mmwlan_s1g_channel_list *channel_list);

/** @} */

/**
 * @defgroup MMWLAN_CTRL    WLAN Control API
 *
 * @{
 */

/** Maximum length of the Morselib version string. */
#define MMWLAN_MORSELIB_VERSION_MAXLEN  (32)
/** Maximum length of the firmware version string. */
#define MMWLAN_FW_VERSION_MAXLEN        (32)

/** Structure for retrieving version information from the mmwlan subsystem. */
struct mmwlan_version
{
    /** Morselib version string. Null terminated. */
    char morselib_version[MMWLAN_MORSELIB_VERSION_MAXLEN];
    /** Morse transceiver firmware version string. Null terminated. */
    char morse_fw_version[MMWLAN_MORSELIB_VERSION_MAXLEN];
    /** Morse transceiver chip ID. */
    uint32_t morse_chip_id;
};

/**
 * Retrieve version information from morselib and the connected Morse transceiver.
 *
 * @param version   The data structure to fill out with version information.
 *
 * @note If the Morse transceiver has not previously been powered on then the @c fw_version
 *       field of @p version will be set to a zero length string.
 *
 * @returns @c MMWLAN_SUCCESS on success else an error code.
 */
enum mmwlan_status mmwlan_get_version(struct mmwlan_version *version);

/**
 * Override the maximum TX power. If no override is specified then the maximum TX power used
 * will be the maximum TX power allowed for the channel in the current regulatory domain.
 *
 * @note Must be invoked after WLAN initialization (see @ref mmwlan_init()). Only takes
 *       effect when switching channel (e.g., during scan or AP connection procedure).
 *
 * @note This will not increase the TX power over the maximum allowed for the current channel
 *       in the configured regulatory domain. Therefore, this override in effect will only
 *       reduce the maximum TX power and cannot increase it.
 *
 * @param tx_power_dbm  The maximum TX power override to set (in dBm). Set to zero to disable
 *                      the override.
 *
 * @return @ref MMWLAN_SUCCESS if the country code was valid and updated successfully,
 *         @ref MMWLAN_UNAVAILABLE if the WLAN subsystem was currently active.
 */
enum mmwlan_status mmwlan_override_max_tx_power(uint16_t tx_power_dbm);

/**
 * Set the RTS threshold.
 *
 * When packets larger than the RTS threshold are transmitted they are protected by an RTS/CTS
 * exchange.
 *
 * @param rts_threshold The RTS threshold (in octets) to set, or 0 to disable.
 *
 * @return @ref MMWLAN_SUCCESS on success, else an appropriate error code.
 */
enum mmwlan_status mmwlan_set_rts_threshold(unsigned rts_threshold);

/**
 * Sets whether or not Short Guard Interval (SGI) support is enabled. Defaults to enabled
 * if not set otherwise.
 *
 * @note This will not force use of SGI, only enable support for it. The rate control algorithm
 *       will make the decision as to which guard interval to use unless explicitly overridden
 *       by @ref mmwlan_ate_override_rate_control().
 *
 * @note This must only be invoked when MMWLAN is inactive (i.e., STA mode not enabled).
 *
 * @param sgi_enabled   Boolean value indicating whether SGI support should be enabled.
 *
 * @return @ref MMWLAN_SUCCESS on success, else an appropriate error code.
 */
enum mmwlan_status mmwlan_set_sgi_enabled(bool sgi_enabled);

/**
 * Sets whether or not sub-band support is enabled for transmit. Defaults to enabled
 * if not set otherwise.
 *
 * @note This will not force use of sub-bands, only enable support for it. The rate control
 *       algorithm will make the decision as to which bandwidth to use unless explicitly overridden
 *       by @ref mmwlan_ate_override_rate_control().
 *
 * @note This must only be invoked when MMWLAN is inactive (i.e., STA mode not enabled).
 *
 * @param subbands_enabled   Boolean value indicating whether sub-band support should be enabled.
 *
 * @return @ref MMWLAN_SUCCESS on success, else an appropriate error code.
 */
enum mmwlan_status mmwlan_set_subbands_enabled(bool subbands_enabled);

/**
 * Sets whether or not the 802.11 power save is enabled. Defaults to @ref MMWLAN_PS_ENABLED
 *
 * @param mode   enum indicating which 802.11 power save mode to use.
 *
 * @return @ref MMWLAN_SUCCESS on success, else an appropriate error code.
 */
enum mmwlan_status mmwlan_set_power_save_mode(enum mmwlan_ps_mode mode);

/**
 * Sets whether or not Aggregated MAC Protocol Data Unit (A-MPDU) support is enabled.
 * This defaults to enabled, if not set otherwise.
 *
 * @note This must only be invoked when MMWLAN is inactive (i.e., STA mode not enabled).
 *
 * @param ampdu_enabled   Boolean value indicating whether AMPDU support should be enabled.
 *
 * @return @ref MMWLAN_SUCCESS on success, else an appropriate error code.
 */
enum mmwlan_status mmwlan_set_ampdu_enabled(bool ampdu_enabled);

/**
 * Minimum value of fragmentation threshold that can be set with
 * @ref mmwlan_set_fragment_threshold().
 */
#define MMWLAN_MINIMUM_FRAGMENT_THRESHOLD   (256)

/**
 * Set the Fragmentation threshold.
 *
 * Maximum length of the frame, beyond which packets must be fragmented into two or more frames.
 *
 * @note Even if the fragmentation threshold is set to 0 (disabled), fragmentation may still occur
 *       if a given packet is too large to be transmitted at the selected rate.
 *
 * @warning Setting a fragmentation threshold may have unintended side effects due to restrictions
 *          at lower bandwidths and MCS rates. In normal operation the fragmentation threshold
 *          should be disabled, in which case packets will be fragmented automatically as necessary
 *          based on the selected rate.
 *
 * @param fragment_threshold The fragmentation threshold (in octets) to set,
 *                           or zero to disable the fragmentation threshold. Minimum value
 *                           (if not zero) is given by @ref MMWLAN_MINIMUM_FRAGMENT_THRESHOLD.
 *
 * @return @ref MMWLAN_SUCCESS on success, else an appropriate error code.
 */
enum mmwlan_status mmwlan_set_fragment_threshold(unsigned fragment_threshold);

/** Structure for storing Target Wake Time (TWT) configuration arguments. */
struct mmwlan_twt_config_args
{
    /** Target Wake Time (TWT) modes, @ref mmwlan_twt_mode. */
    enum mmwlan_twt_mode twt_mode;
    /**
     * TWT service period interval in micro seconds.
     * This parameter will be ignored if @c twt_wake_interval_mantissa or
     * @c twt_wake_interval_exponent is non-zero.
     */
    uint64_t twt_wake_interval_us;
    /**
     * TWT Wake interval mantissa
     * If non-zero, this parameter will be used to calculate @c twt_wake_interval_us.
     */
    uint16_t twt_wake_interval_mantissa;
    /**
     * TWT Wake interval exponent
     * If non-zero, this parameter will be used to calculate @c twt_wake_interval_us.
     */
    uint8_t twt_wake_interval_exponent;
    /** Minimum TWT wake duration in micro seconds. */
    uint32_t twt_min_wake_duration_us;
    /** TWT setup command, @ref mmwlan_twt_setup_command. */
    enum mmwlan_twt_setup_command twt_setup_command;
};

/**
 * Initializer for @ref mmwlan_twt_config_args.
 *
 * For example:
 *
 * @code{c}
 * struct mmwlan_twt_config_args twt_config_args = MMWLAN_TWT_CONFIG_ARGS_INIT;
 * @endcode
 */
#define MMWLAN_TWT_CONFIG_ARGS_INIT { MMWLAN_TWT_DISABLED, DEFAULT_TWT_WAKE_INTERVAL_US, 0, 0,     \
                                      DEFAULT_TWT_MIN_WAKE_DURATION_US, MMWLAN_TWT_SETUP_REQUEST }

/**
 * Add configurations for Target Wake Time (TWT).
 *
 * @note This is used to add TWT configuration for a new TWT agreement.
 *       This function should be invoked before @ref mmwlan_sta_enable.
 *
 * @param twt_config_args TWT configuration arguments @ref mmwlan_twt_config_args.
 *
 * @return @ref MMWLAN_SUCCESS on success, else an appropriate error code.
 */
enum mmwlan_status mmwlan_twt_add_configuration(
    const struct mmwlan_twt_config_args *twt_config_args);

/**
 * Arguments data structure for @ref mmwlan_boot().
 *
 * This structure should be initialized using @ref MMWLAN_BOOT_ARGS_INIT for sensible
 * default values, particularly for forward compatibility with new releases that may add
 * new fields to the struct. For example:
 *
 * @note This struct currently does not include any sensible arguments yet, and is provided
 *       for forwards compatibility with potential future API extensions.
 *
 * @code{.c}
 *     enum mmwlan_status status;
 *     struct mmwlan_boot_args boot_args = MMWLAN_BOOT_ARGS_INIT;
 *     // HERE: initialize arguments
 *     status = mmwlan_boot(&boot_args);
 * @endcode
 */
struct mmwlan_boot_args
{
    /** Note this field should not be used and will be removed in future. */
    uint8_t reserved;
};

/**
 * Initializer for @ref mmwlan_boot_args.
 *
 * @see mmwlan_boot_args
 */
#define MMWLAN_BOOT_ARGS_INIT { 0 }

/**
 * Boot the Morse Micro transceiver and leave it in an idle state.
 *
 * @note In general, it is not necessary to use this function as @ref mmwlan_sta_enable()
 *       will automatically boot the chip if required. It may be used, for example, to power
 *       on the chip for production test, etc.
 *
 * @warning Channel list must be set before booting the transceiver. @ref mmwlan_set_channel_list().
 *
 * @param args  Boot arguments. May be @c NULL, in which case default values will be used.
 *
 * @return @ref MMWLAN_SUCCESS on success, else an appropriate error code.
 */
enum mmwlan_status mmwlan_boot(const struct mmwlan_boot_args *args);

/**
 * Perform a clean shutdown of the Morse Micro transceiver, including cleanly disconnecting
 * from a connected AP, if necessary.
 *
 * Has no effect if the transceiver is already shutdown.
 *
 * @return @ref MMWLAN_SUCCESS on success, else an appropriate error code.
 */
enum mmwlan_status mmwlan_shutdown(void);

/**
 * Enumeration of states in STA mode.
 */
enum mmwlan_sta_state
{
    MMWLAN_STA_DISABLED,
    MMWLAN_STA_CONNECTING,
    MMWLAN_STA_CONNECTED,
};

/**
 * Enumeration of S1G non-AP STA types.
 */
enum mmwlan_station_type
{
    MMWLAN_STA_TYPE_SENSOR = 0x01,
    MMWLAN_STA_TYPE_NON_SENSOR = 0x02,
};

/** STA status callback function prototype. */
typedef void (*mmwlan_sta_status_cb_t)(enum mmwlan_sta_state sta_state);

/**
 * Arguments data structure for @ref mmwlan_sta_enable().
 *
 * This structure should be initialized using @ref MMWLAN_STA_ARGS_INIT for sensible
 * default values, particularly for forward compatibility with new releases that may add
 * new fields to the struct. For example:
 *
 * @code{.c}
 *     enum mmwlan_status status;
 *     struct mmwlan_sta_args sta_args = MMWLAN_STA_ARGS_INIT;
 *     // HERE: initialize arguments
 *     status = mmwlan_sta_enable(&sta_args);
 * @endcode
 */
struct mmwlan_sta_args
{
    /** SSID of the AP to connect to. */
    uint8_t ssid[MMWLAN_SSID_MAXLEN];
    /** Length of the SSID. */
    uint16_t ssid_len;
    /**
     * BSSID of the AP to connect to.
     * If non-zero, the STA will only connect to an AP that matches this value.
     */
    uint8_t bssid[MMWLAN_MAC_ADDR_LEN];
    /** Type of security to use. If @c MMWLAN_SAE then a @c passphrase must be specified. */
    enum mmwlan_security_type security_type;
    /** Passphrase (only used if @c security_type is @c MMWLAN_SAE, otherwise ignored. */
    char passphrase[MMWLAN_PASSPHRASE_MAXLEN + 1];
    /** Length of @c passphrase. May be zero if @c passphrase is null-terminated. */
    uint16_t passphrase_len;
    /** Protected Management Frame mode to use (802.11w) */
    enum mmwlan_pmf_mode pmf_mode;
    /**
     * Priority used by the AP to assign a STA to a Restricted Access Window (RAW) group.
     * Valid range is 0 - @ref MMWLAN_RAW_MAX_PRIORITY, or -1 to disable RAW.
     */
    int16_t raw_sta_priority;
    /** S1G non-AP STA type. For valid STA types, @ref mmwlan_station_type */
    enum mmwlan_station_type sta_type;
    /**
     * Preference list of enabled elliptic curve groups for SAE and OWE.
     * By default (if this parameter is not set), the mandatory group 19 is preferred.
     */
    int sae_owe_ec_groups[MMWLAN_MAX_EC_GROUPS];
    /** Whether Centralized Authentication Controlled is enabled on the STA. */
    enum mmwlan_cac_mode cac_mode;
    /**
     * Short background scan interval in seconds.
     * Setting this to zero will disable background scanning.
     */
    uint16_t bgscan_short_interval_s;
    /** Signal strength threshold for background scanning. */
    int bgscan_signal_threshold_dbm;
    /**
     * Long background scan interval in seconds.
     * Setting this to zero will disable background scanning.
     */
    uint16_t bgscan_long_interval_s;
};

/**
 * Initializer for @ref mmwlan_sta_args.
 *
 * @see mmwlan_sta_args
 */
#define MMWLAN_STA_ARGS_INIT                                                                       \
    { { 0 }, 0, { 0 }, MMWLAN_OPEN, { 0 }, 0, MMWLAN_PMF_REQUIRED, -1, MMWLAN_STA_TYPE_NON_SENSOR, \
      { 0 }, MMWLAN_CAC_DISABLED, DEFAULT_BGSCAN_SHORT_INTERVAL_S, DEFAULT_BGSCAN_THRESHOLD_DBM,   \
      DEFAULT_BGSCAN_LONG_INTERVAL_S }

/**
 * Enable station mode.
 *
 * This will power on the transceiver then initiate connection to the given Access Point. If
 * station mode is already enabled when this function is invoked then it will disconnect from
 * (if already connected) and initiate connection to the given AP.
 *
 * @note The STA status callback (@p sta_status_cb) must not block and MMWLAN API functions
 *       may not be invoked from the callback.
 *
 * @warning Channel list must be set before enabling station mode. @ref mmwlan_set_channel_list().
 *
 * @param args              STA arguments (e.g., SSID, etc.). See @ref mmwlan_sta_args.
 * @param sta_status_cb     Optional callback to be invoked on STA state changes. May be @c NULL.
 *
 * @return @ref MMWLAN_SUCCESS on success, else an appropriate error code.
 */
enum mmwlan_status mmwlan_sta_enable(const struct mmwlan_sta_args *args,
                                     mmwlan_sta_status_cb_t sta_status_cb);

/**
 * Disable station mode.
 *
 * This will disconnect from the AP. It will also shut down the transceiver if nothing else
 * is holding it open. Note that if the transceiver was booted by @c mmwlan_boot() then
 * this function will shut down the transceiver.
 *
 * @return @ref MMWLAN_SUCCESS if successful and the transceiver was also shut down,
 *         @ref MMWLAN_SHUTDOWN_BLOCKED if successful and the transceiver was not shut down,
 *         else an appropriate error code.
 */
enum mmwlan_status mmwlan_sta_disable(void);

/**
 * Gets the current WLAN STA state.
 *
 * @return the current WLAN STA state.
 */
enum mmwlan_sta_state mmwlan_get_sta_state(void);

/** Default value for @c mmwlan_scan_args.dwell_time_ms
 *  This is calculated to encompass the default beacon interval of 100 TUs.
 */
#define MMWLAN_SCAN_DEFAULT_DWELL_TIME_MS (105)

/** Minimum value for @c mmwlan_scan_args.dwell_time_ms */
#define MMWLAN_SCAN_MIN_DWELL_TIME_MS     (65)

/**
 * Enumeration of states in Scan mode.
 */
enum mmwlan_scan_state
{
    /** Scan was successful and all channels were scanned. */
    MMWLAN_SCAN_SUCCESSFUL,
    /** Scan was incomplete. One or more channels may have been scanned and therefore an
     *  incomplete set of scan results may still have been received. */
    MMWLAN_SCAN_TERMINATED,
    /** Scanning in progress. */
    MMWLAN_SCAN_RUNNING,
};

/** Result of the scan request. */
struct mmwlan_scan_result
{
    /** RSSI of the received frame. */
    int16_t rssi;
    /** Pointer to the BSSID field within the Probe Response frame. */
    const uint8_t *bssid;
    /** Pointer to the SSID within the SSID IE of the Probe Response frame. */
    const uint8_t *ssid;
    /** Pointer to the start of the Information Elements within the Probe Response frame. */
    const uint8_t *ies;
    /** Value of the Beacon Interval field. */
    uint16_t beacon_interval;
    /** Value of the Capability Information  field. */
    uint16_t capability_info;
    /** Length of the Information Elements (@c ies). */
    uint16_t ies_len;
    /** Length of the SSID (@c ssid). */
    uint8_t ssid_len;
    /** Center frequency in Hz of the channel where the frame was received. */
    uint32_t channel_freq_hz;
    /** Bandwidth, in MHz, where the frame was received. */
    uint8_t bw_mhz;
    /** Operating bandwidth, in MHz, of the access point. */
    uint8_t op_bw_mhz;
    /** TSF timestamp in the Probe Response frame. */
    uint64_t tsf;
};

/** mmwlan scan rx callback function prototype. */
typedef void (*mmwlan_scan_rx_cb_t)(const struct mmwlan_scan_result *result, void *arg);

/** mmwlan scan complete callback function prototype. */
typedef void (*mmwlan_scan_complete_cb_t)(enum mmwlan_scan_state scan_state, void *arg);

/**
 * Structure to hold scan arguments. This structure should be initialized using
 * @ref MMWLAN_SCAN_ARGS_INIT for forward compatibility.
 */
struct mmwlan_scan_args
{
    /**
     * Time to dwell on a channel waiting for probe responses/beacons.
     *
     * @note This includes the time it takes to actually tune to the channel.
     */
    uint32_t dwell_time_ms;
    /**
     * Extra Information Elements to include in Probe Request frames.
     * May be @c NULL if @c extra_ies_len is zero.
     */
    uint8_t *extra_ies;
    /** Length of @c extra_ies. */
    size_t extra_ies_len;
    /**
     * SSID used for scan.
     * May be @c NULL, with @c ssid_len set to zero for an undirected scan.
     */
    uint8_t ssid[MMWLAN_SSID_MAXLEN];
    /** Length of the SSID. */
    uint16_t ssid_len;
};

/**
 * Initializer for @ref mmwlan_scan_args.
 *
 * For example:
 *
 * @code{c}
 * struct mmwlan_scan_args scan_args = MMWLAN_SCAN_ARGS_INIT;
 * @endcode
 */
#define MMWLAN_SCAN_ARGS_INIT { MMWLAN_SCAN_DEFAULT_DWELL_TIME_MS, NULL, 0, { 0 }, 0 }

/**
 * Structure to hold arguments specific to a given instance of a scan.
 */
struct mmwlan_scan_req
{
    /** Scan response receive callback. Must not be @c NULL. */
    mmwlan_scan_rx_cb_t scan_rx_cb;
    /** Scan complete callback. Must not be @c NULL. */
    mmwlan_scan_complete_cb_t scan_complete_cb;
    /** Opaque argument to be passed to the callbacks. */
    void *scan_cb_arg;
    /** Scan arguments to be used @ref mmwlan_scan_args. */
    struct mmwlan_scan_args args;
};

/**
 * Initializer for @ref mmwlan_scan_req.
 *
 * For example:
 *
 * @code{c}
 * struct mmwlan_scan_req scan_req = MMWLAN_SCAN_REQ_INIT;
 * @endcode
 */
#define MMWLAN_SCAN_REQ_INIT { NULL, NULL, NULL, MMWLAN_SCAN_ARGS_INIT }

/**
 * Request a scan.
 *
 * If the transceiver is not already powered on, it will be powered on before the scan is
 * initiated. The power on procedure will block this function. The transceiver will remain
 * powered on after scan completion and must be shutdown by invoking @ref mmwlan_shutdown().
 *
 * @note Just because a scan is requested does not mean it will happen immediately.
 *       It may take some time for the request to be serviced.
 *
 * @note The scan request may be rejected, in which case the complete callback will be
 *       invoked immediately with an error status.
 *
 * @param scan_req          Scan request instance. See @ref mmwlan_scan_req.
 *
 * @return @ref MMWLAN_SUCCESS on success, else an appropriate error code.
 */
enum mmwlan_status mmwlan_scan_request(const struct mmwlan_scan_req *scan_req);

/**
 * Abort in progress or pending scans.
 *
 * The scan callback will be called back with result code @ref MMWLAN_SCAN_TERMINATED for
 * all aborted scans.
 *
 * @return @ref MMWLAN_SUCCESS on success, else an appropriate error code.
 */
enum mmwlan_status mmwlan_scan_abort(void);

/**
 * Gets the MAC address of this device.
 *
 * The MAC address address comes from one of the following sources, in descending priority order:
 * 1. @c mmhal_read_mac_addr()
 * 2. Morse Micro transceiver
 * 3. Randomly generated in the form `02:01:XX:XX:XX:XX`
 *
 * @note If a MAC address override is not provided (via @c mmhal_read_mac_addr()) then the
 *       transceiver must have been booted at least once before this function is invoked.
 *
 * @param mac_addr  Buffer to receive the MAC address. Length must be @ref MMWLAN_MAC_ADDR_LEN.
 *
 * @return @ref MMWLAN_SUCCESS on success, @ref MMWLAN_UNAVAILABLE if the MAC address was not
 *         able to be read from the transceiver because it was not booted, else an appropriate
 *         error code.
 */
enum mmwlan_status mmwlan_get_mac_addr(uint8_t *mac_addr);

/**
 * Gets the station's AID
 *
 * @return AID of station, or 0 if not associated
 */
uint16_t mmwlan_get_aid(void);

/**
 * Gets the BSSID of the AP to which the STA is associated.
 *
 * @param bssid  Buffer to receive the BSSID. Length must be @ref MMWLAN_MAC_ADDR_LEN.
 *               Will be set to 00:00:00:00:00:00 if the station is not currently associated.
 *
 * @return @ref MMWLAN_SUCCESS on success, else an appropriate error code.
 */
enum mmwlan_status mmwlan_get_bssid(uint8_t *bssid);

/**
 * Gets the RSSI measured from the AP.
 *
 * When power save is enabled, this will only be updated when traffic is received from the AP.
 *
 * @returns last known RSSI of the AP or INT32_MIN on error.
 */
int32_t mmwlan_get_rssi(void);

/** @} */

/**
 * @defgroup MMWLAN_WNM     WNM Sleep management
 *
 * @{
 *
 * @note It is highly recommended to refer to Morse Micro App Note 04 on WNM sleep for a more
 *       detailed explanation on how this feature works.
 *
 * WNM sleep is an extended power save mode in which the STA need not listen for every DTIM beacon
 * and need not perform rekey, allowing the STA to remain associated and reduce power consumption
 * when it has no traffic to send or receive from the AP. While a STA is in WNM sleep it will be
 * unable to communicate with the AP, and thus any traffic for the STA may be buffered or dropped
 * at the discretion of the AP while the STA is in WNM sleep.
 *
 * @note WNM sleep can only be entered after the STA is associated to the AP. The AP must also
 *       support WNM sleep.
 *
 * When entry to WNM sleep is enabled via this API, the STA sends a request to the AP and after
 * successful negotiation, the chip is either put into a lower power mode or powered down
 * completely, depending on arguments given when WNM sleep mode was enabled. The datapath is paused
 * during this time. If the STA does not successfully negotiate with the AP to enter WNM Sleep, it
 * will return an error code and the chip will not enter a low power mode.
 *
 * When WNM sleep is disabled, the chip returns to normal operation and sends a request to the AP
 * to exit WNM sleep. Note that the chip will exit low power mode regardless of whether or not the
 * exit request was successful. WNM sleep will be exited if @ref mmwlan_shutdown or
 * @ref mmwlan_sta_disable is invoked.
 *
 * If the AP takes down the link, then the station will be unaware until it sends the WNM Sleep
 * exit request. At that point the AP will send a de-authentication frame with reason code
 * "non-associated station". After validating this is actually the AP the station was connected
 * to, the station will bring down the connection and return @ref MMWLAN_ERROR. The station will
 * attempt to re-associate but will not re-enter WNM sleep. The application needs to enable WNM
 * sleep again via this API if required.
 *
 * A high level overview of enabling and disabling WNM sleep is shown below.
 *
 * @include{doc} wnm_sleep_overview.puml
 */

/** Structure for storing WNM sleep extended arguments. */
struct mmwlan_set_wnm_sleep_enabled_args
{
    /** Boolean indicating whether WNM sleep is enabled. */
    bool wnm_sleep_enabled;
    /** Boolean indicating whether chip should be powered down during WNM sleep. */
    bool chip_powerdown_enabled;
};

/**
 * Initializer for @ref mmwlan_set_wnm_sleep_enabled_args.
 *
 * For example:
 *
 * @code{c}
 * struct mmwlan_set_wnm_sleep_enabled_args wnm_sleep_args = MMWLAN_SET_WNM_SLEEP_ENABLED_ARGS_INIT;
 * @endcode
 */
#define MMWLAN_SET_WNM_SLEEP_ENABLED_ARGS_INIT { false, false }

/**
 * Sets extended WNM sleep mode.
 *
 * Provides an extended interface for setting WNM sleep. See @ref mmwlan_set_wnm_sleep_enabled_args
 * for parameter details. If WNM sleep mode is enabled then the transceiver will sleep across
 * multiple DTIM periods until WNM sleep mode is disabled. This allows the transceiver to use less
 * power with the caveat that it will not wake up for group- or individually-addressed traffic. If
 * a group rekey occurs while the device is in WNM sleep it will be applied when the device exits
 * WNM sleep.
 *
 * @note Data should not be queued for transmission (using, e.g., @ref mmwlan_tx())  during
 *       WNM sleep.
 * @note 802.11 power save must be enabled for any benefit to be obtained
 *       (see @ref mmwlan_set_power_save_mode()).
 * @note Negotiation with the AP is required to enter WNM sleep. As such the transceiver will only
 *       be placed into low power mode when a connection has been establish and the AP has accepted
 *       the request to enter WNM sleep. This will automatically be handled within mmwlan once WNM
 *       sleep mode is enabled.
 *
 * @param args  WNM sleep arguments - see @ref mmwlan_set_wnm_sleep_enabled_args.
 *
 * @return @ref MMWLAN_SUCCESS on success, MMWLAN_UNAVAILABLE if already requested,
 *              else an appropriate error code.
 *         @ref MMWLAN_TIMED_OUT if the maximum retry request reached. For WNM sleep exit request,
 *              this means that the device exited WNM sleep but failed to inform the AP.
 */
enum mmwlan_status mmwlan_set_wnm_sleep_enabled_ext(
    const struct mmwlan_set_wnm_sleep_enabled_args *args);

/**
 * Sets whether WNM sleep mode is enabled.
 *
 * If WNM sleep mode is enabled then the transceiver will sleep across multiple DTIM periods
 * until WNM sleep mode is disabled. This allows the transceiver to use less power with the
 * caveat that it will not wake up for group- or individually-addressed traffic. If a group
 * rekey occurs while the device is in WNM sleep it will be applied when the device exits
 * WNM sleep.
 *
 * @note Data should not be queued for transmission (using, e.g., @ref mmwlan_tx())  during
 *       WNM sleep.
 * @note 802.11 power save must be enabled for any benefit to be obtained
 *       (see @ref mmwlan_set_power_save_mode()).
 * @note Negotiation with the AP is required to enter WNM sleep. As such the transceiver will only
 *       be placed into low power mode when a connection has been establish and the AP has accepted
 *       the request to enter WNM sleep. This will automatically be handled within mmwlan once WNM
 *       sleep mode is enabled.
 *
 * @param wnm_sleep_enabled   Boolean indicating whether WNM sleep is enabled.
 *
 * @return @ref MMWLAN_SUCCESS on success, MMWLAN_UNAVAILABLE if already requested,
 *              else an appropriate error code.
 *         @ref MMWLAN_TIMED_OUT if the maximum retry request reached. For WNM sleep exit request,
 *              this means that the device exited WNM sleep but failed to inform the AP.
 */
static inline enum mmwlan_status mmwlan_set_wnm_sleep_enabled(bool wnm_sleep_enabled)
{
    struct mmwlan_set_wnm_sleep_enabled_args wnm_sleep_args =
        MMWLAN_SET_WNM_SLEEP_ENABLED_ARGS_INIT;
    wnm_sleep_args.wnm_sleep_enabled = wnm_sleep_enabled;
    return mmwlan_set_wnm_sleep_enabled_ext(&wnm_sleep_args);
}

/**
 * @}
 */

/*
 * ---------------------------------------------------------------------------------------------
 */

/**
 * @defgroup MMWLAN_INIT    WLAN Initialization/Deinitialization API
 *
 * @note If using LWIP, this API is invoked by the LWIP @c netif for MMWLAN, and thus does not
 *       need to be used directly by the application.
 *
 * @{
 */

/**
 * Initialize the MMWLAN subsystem.
 *
 * @warning @ref mmhal_init() must be called before this function is executed.
 */
void mmwlan_init(void);


/**
 * Deinitialize the MMWLAN subsystem, freeing any allocated memory.
 *
 * Must not be called while there is any activity in progress (e.g., while connected).
 *
 * @warning @ref mmwlan_shutdown must be called before executing this function.
 */
void mmwlan_deinit(void);

/** @} */

/*
 * ---------------------------------------------------------------------------------------------
 */

/**
 * @defgroup MMWLAN_DATA    WLAN Datapath API
 *
 * @{
 */

/** Enumeration of link states. */
enum mmwlan_link_state
{
    MMWLAN_LINK_DOWN,       /**< The link is down. */
    MMWLAN_LINK_UP,         /**< The link is up. */
};

/**
 * Prototype for link state change callbacks.
 *
 * @param link_state    The new link state.
 * @param arg           Opaque argument that was given when the callback was registered.
 */
typedef void (*mmwlan_link_state_cb_t)(enum mmwlan_link_state link_state, void *arg);

/**
 * Register a link status callback.
 *
 * @note Only one link status callback may be registered. Further registration will overwrite the
 *       previously registered callback.
 *
 * @note The link status callback must not block and MMWLAN API functions may not be invoked
 *       from the callback.
 *
 * @param callback  The callback to register.
 * @param arg       Opaque argument to be passed to the callback.
 *
 * @return @ref MMWLAN_SUCCESS on success, else an appropriate error code.
 */
enum mmwlan_status mmwlan_register_link_state_cb(mmwlan_link_state_cb_t callback, void *arg);

/**
 * Receive data packet callback function.
 *
 * @param header        Buffer containing the 802.3 header for this packet.
 * @param header_len    Length of the @p header.
 * @param payload       Packet payload (excluding header).
 * @param payload_len   Length of @p payload.
 * @param arg           Opaque argument that was given when the callback was registered.
 */
typedef void (*mmwlan_rx_cb_t)(uint8_t *header, unsigned header_len,
                               uint8_t *payload, unsigned payload_len,
                               void *arg);

/**
 * Register a receive callback.
 *
 * @note Only one receive callback may be registered. Further registration will overwrite the
 *       previously registered callback.
 *
 * @note Only a single receive callback may be registered at a time.
 *
 * @param callback  The callback to register (@c NULL to unregister).
 * @param arg       Opaque argument to be passed to the callback.
 *
 * @return @ref MMWLAN_SUCCESS on success, else an appropriate error code.
 */
enum mmwlan_status mmwlan_register_rx_cb(mmwlan_rx_cb_t callback, void *arg);

/** Default QoS Traffic ID (TID) to use for TX (@c mmwlan_tx()). */
#define MMWLAN_TX_DEFAULT_QOS_TID   (0)

/** Maximum Traffic ID (TID) supported for QoS traffic. */
#define MMWLAN_MAX_QOS_TID          (7)

/**
 * Transmit the given packet using the given QoS Traffic ID (TID). The packet must start with
 * an 802.3 header that will be translated into an 802.11 header.
 *
 * @code
 *
 *    +----------+----------+-----------+------------------+
 *    | DST ADDR | SRC ADDR | ETHERTYPE |   Payload        |
 *    +----------+----------+-----------+------------------+
 *    ^                                 ^                  ^
 *    |--------802.3 MAC Header---------|                  |
 *    |                                                    |
 *    |                                                    |
 *    |                                                    |
 *    |<----------------------len------------------------->|
 *  data
 *
 * @endcode
 *
 * @param data  Packet data.
 * @param len   Length of packet.
 * @param tid   TID to use (0 - @ref MMWLAN_MAX_QOS_TID).
 *
 * @return @ref MMWLAN_SUCCESS on success, else an appropriate error code.
 */
enum mmwlan_status mmwlan_tx_tid(const uint8_t *data, unsigned len, uint8_t tid);

/**
 * Transmit the given packet using @ref MMWLAN_TX_DEFAULT_QOS_TID. The packet must start with an
 * 802.3 header that will be translated into an 802.11 header.
 *
 * @code
 *
 *    +----------+----------+-----------+------------------+
 *    | DST ADDR | SRC ADDR | ETHERTYPE |   Payload        |
 *    +----------+----------+-----------+------------------+
 *    ^                                 ^                  ^
 *    |--------802.3 MAC Header---------|                  |
 *    |                                                    |
 *    |                                                    |
 *    |                                                    |
 *    |<----------------------len------------------------->|
 *  data
 *
 * @endcode
 *
 * @param data  Packet data.
 * @param len   Length of packet.
 *
 * @return @ref MMWLAN_SUCCESS on success, else an appropriate error code.
 */
enum mmwlan_status mmwlan_tx(const uint8_t *data, unsigned len);

/**
 * Transmit the given packet using the @ref MMWLAN_TX_DEFAULT_QOS_TID QoS Traffic ID (TID).
 */
#define mmwlan_tx(_data, _len)  mmwlan_tx_tid(_data, _len, MMWLAN_TX_DEFAULT_QOS_TID)

/** @} */

/*
 * ---------------------------------------------------------------------------------------------
 */

/**
 * @defgroup MMWLAN_STATS    Statistics API
 *
 * API for retrieving statistics information from the WLAN subsystem.
 *
 * @{
 */

/** Rate control statistics data structure. */
struct mmwlan_rc_stats
{
    /** The number of rate table entries. */
    uint32_t n_entries;
    /** Rate info for each rate table entry. */
    uint32_t *rate_info;
    /** Total number of packets sent for each rate table entry. */
    uint32_t *total_sent;
    /** Total successes for each rate table entry. */
    uint32_t *total_success;
};

/**
 * Enumeration defined offsets into the bit field of rate information (@c rate_info in
 * @ref mmwlan_rc_stats).
 *
 * ```
 * 31         9       8      4    0
 * +----------+-------+------+----+
 * | Reserved | Guard | Rate | BW |
 * |    23    |   1   |  4   | 4  |
 * +----------+-------+------+----+
 * ```
 * * BW: 0 = 1 MHz, 1 = 2 MHz, 2 = 4 MHz
 * * Rate: MCS Rate
 * * Guard: 0 = LGI, 1 = SGI
 */
enum mmwlan_rc_stats_rate_info_offsets
{
    MMWLAN_RC_STATS_RATE_INFO_BW_OFFSET = 0,
    MMWLAN_RC_STATS_RATE_INFO_RATE_OFFSET = 4,
    MMWLAN_RC_STATS_RATE_INFO_GUARD_OFFSET = 8,
};

/**
 * Retrieves WLAN rate control statistics.
 *
 * @note The returned data structure must be freed using @ref mmwlan_free_rc_stats().
 *
 * @returns the statistics data structure on success or @c NULL on failure.
 */
struct mmwlan_rc_stats *mmwlan_get_rc_stats(void);

/**
 * Free a mmwlan_rc_stats structure that was allocated with @ref mmwlan_get_rc_stats().
 *
 * @param stats     The structure to be freed (may be @c NULL).
 */
void mmwlan_free_rc_stats(struct mmwlan_rc_stats *stats);

/**
 * Data structure used to represent an opaque buffer containing Morse statistics. This is
 * returned by @c mmwlan_get_morse_stats() and must be freed by @c mmwlan_free_morse_stats().
 */
struct mmwlan_morse_stats
{
    /** Buffer containing the stats. */
    uint8_t *buf;
    /** Length of stats in @c buf. */
    uint32_t len;
};

/**
 * Retrieves statistics from the Morse transceiver. The stats are returned as a binary blob
 * that can be parsed by host tools.
 *
 * @param core_num  The core to retrieve stats for.
 * @param reset     Boolean indicating whether to reset the stats after retrieving.
 *
 * @note The returned @c mmwlan_morse_stats instance must be freed using
 *       @ref mmwlan_free_morse_stats.
 *
 * @returns a @c mmwlan_morse_stats instance on success or @c NULL on failure.
 */
struct mmwlan_morse_stats *mmwlan_get_morse_stats(uint32_t core_num, bool reset);

/**
 * Frees a @c mmwlan_morse_stats instance that was returned by @c mmwlan_get_morse_stats().
 *
 * @param stats     The instance to free. May be @ NULL.
 */
void mmwlan_free_morse_stats(struct mmwlan_morse_stats *stats);

/** @} */

/*
 * ---------------------------------------------------------------------------------------------
 */

/**
 * @defgroup MMWLAN_TEST    Test (ATE) API
 *
 * Extended API particularly intended for test use cases.
 *
 * @{
 */


/** Enumeration of MCS rates. */
enum mmwlan_mcs
{
    MMWLAN_MCS_NONE  = -1,          /**< Use-case specific special value */
    MMWLAN_MCS_0  = 0,              /**< MCS0 */
    MMWLAN_MCS_1,                   /**< MCS1 */
    MMWLAN_MCS_2,                   /**< MCS2 */
    MMWLAN_MCS_3,                   /**< MCS3*/
    MMWLAN_MCS_4,                   /**< MCS4 */
    MMWLAN_MCS_5,                   /**< MCS5 */
    MMWLAN_MCS_6,                   /**< MCS6 */
    MMWLAN_MCS_7,                   /**< MCS7 */
    MMWLAN_MCS_MAX = MMWLAN_MCS_7   /**< Maximum supported MCS rate */
};

/** Enumeration of bandwidths. */
enum mmwlan_bw
{
    MMWLAN_BW_NONE = -1,                /**< Use-case specific special value */
    MMWLAN_BW_1MHZ = 1,                 /**< 1 MHz bandwidth */
    MMWLAN_BW_2MHZ = 2,                 /**< 2 MHz bandwidth */
    MMWLAN_BW_4MHZ = 4,                 /**< 4 MHz bandwidth */
    MMWLAN_BW_8MHZ = 8,                 /**< 8 MHz bandwidth */
    MMWLAN_BW_MAX = MMWLAN_BW_8MHZ,     /**< Maximum supported bandwidth */
};

/** Enumeration of guard intervals. */
enum mmwlan_gi
{
    MMWLAN_GI_NONE  = -1,           /**< Use-case specific special value */
    MMWLAN_GI_SHORT = 0,            /**< Short guard interval */
    MMWLAN_GI_LONG,                 /**< Long guard interval */
    MMWLAN_GI_MAX = MMWLAN_GI_LONG, /**< Maximum valid value of this @c enum. */
};

/**
 * Enable/disable override of rate control parameters.
 *
 * @param tx_rate_override      Overrides the transmit MCS rate. Set to @ref MMWLAN_MCS_NONE for no
 *                              override.
 * @param bandwidth_override    Overrides the TX bandwidth. Set to @ref MMWLAN_BW_NONE for no
 *                              override.
 * @param gi_override           Overrides the guard interval. Set to @ref MMWLAN_GI_NONE for no
 *                              override.
 *
 * @return @ref MMWLAN_SUCCESS on success, else an appropriate error code.
 */
enum mmwlan_status mmwlan_ate_override_rate_control(enum mmwlan_mcs tx_rate_override,
                                                    enum mmwlan_bw bandwidth_override,
                                                    enum mmwlan_gi gi_override);

/**
 * Execute a test/debug command. The format of command and response is opaque to this API.
 *
 * @param[in]     command       Buffer containing the command to be executed. Note that buffer
 *                              contents may be modified by this function.
 * @param[in]     command_len   Length of the command to be executed.
 * @param[out]    response      Buffer to received the response to the command.
 *                              May be NULL, in which case @p response_len should also be NULL.
 * @param[in,out] response_len  Pointer to a @c uint32_t that is initialized to the length of the
 *                              response buffer. On success, the value will be updated to the
 *                              length of data that was put into the response buffer.
 *                              May be NULL, in which case @p response must also be NULL.
 *
 * @returns @c MMWLAN_SUCCESS on success, else an appropriate error code.
 */
enum mmwlan_status mmwlan_ate_execute_command(uint8_t *command, uint32_t command_len,
                                              uint8_t *response, uint32_t *response_len);

/** @} */

#ifdef __cplusplus
}
#endif

/** @} */
