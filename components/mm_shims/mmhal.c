/*
 * Copyright 2021-2023 Morse Micro
 *
 * This file is licensed under terms that can be found in the LICENSE.md file in the root
 * directory of the Morse Micro IoT SDK software package.
 */

#include "mmhal.h"
#include "mmosal.h"
#include "mmwlan.h"
#include "sdkconfig.h"
#include "esp_random.h"
#include "esp_system.h"
#include "driver/gpio.h"

#ifndef UNUSED
#define UNUSED(x) ((void)(x))
#endif

static uint8_t g_mac_addr[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};

/**
 * Get the name of the given AKM Suite as a string.
 *
 * @param akm_suite_oui     The OUI of the AKM suite as a big endian integer.
 *
 * @returns the string representation.
 */
const char *mmhal_akm_suite_to_string(uint32_t akm_suite_oui)
{
    switch (akm_suite_oui)
    {
    case AKM_SUITE_NONE:
        return "None";

    case AKM_SUITE_PSK:
        return "PSK";

    case AKM_SUITE_SAE:
        return "SAE";

    case AKM_SUITE_OWE:
        return "OWE";

    default:
        return "Other";
    }
}

/** Tag number of the RSN information element, in which we can find security details of the AP. */
#define RSN_INFORMATION_IE_TYPE (48)


int mmhal_parse_rsn_information(const uint8_t *ies, unsigned ies_len, struct rsn_information *output)
{
    size_t offset = 0;
    memset(output, 0, sizeof(*output));

    while (offset < ies_len)
    {
        uint8_t type = ies[offset++];
        uint8_t length = ies[offset++];

        if (type == RSN_INFORMATION_IE_TYPE)
        {
            uint16_t num_pairwise_cipher_suites;
            uint16_t num_akm_suites;
            uint16_t ii;


            if (offset + length > ies_len)
            {
                printf("*WRN* RSN IE extends past end of IEs\n");
                return -1;
            }

            if (length < 8)
            {
                printf("*WRN* RSN IE too short\n");
                return -1;
            }

            /* Skip version field */
            output->version = ies[offset] | ies[offset+1] << 8;
            offset += 2;
            length -= 2;

            output->group_cipher_suite =
                ies[offset] << 24 | ies[offset+1] << 16 | ies[offset+2] << 8 | ies[offset+3];
            offset += 4;
            length -= 4;

            num_pairwise_cipher_suites = ies[offset] | ies[offset+1] << 8;
            offset += 2;
            length -= 2;

            output->num_pairwise_cipher_suites = num_pairwise_cipher_suites;
            if (num_pairwise_cipher_suites > RSN_INFORMATION_MAX_PAIRWISE_CIPHER_SUITES)
            {
                output->num_pairwise_cipher_suites = RSN_INFORMATION_MAX_PAIRWISE_CIPHER_SUITES;
            }

            if (length < 4 * num_pairwise_cipher_suites + 2)
            {
                printf("*WRN* RSN IE too short\n");
                return -1;
            }

            for (ii = 0; ii < num_pairwise_cipher_suites; ii++)
            {
                if (ii < output->num_pairwise_cipher_suites)
                {
                    output->pairwise_cipher_suites[ii] =
                        ies[offset] << 24 | ies[offset+1] << 16 |
                        ies[offset+2] << 8 | ies[offset+3];
                }
                offset += 4;
                length -= 4;
            }

            num_akm_suites = ies[offset] | ies[offset+1] << 8;
            offset += 2;
            length -= 2;

            output->num_akm_suites = num_akm_suites;
            if (num_akm_suites > RSN_INFORMATION_MAX_AKM_SUITES)
            {
                output->num_akm_suites = RSN_INFORMATION_MAX_AKM_SUITES;
            }

            if (length < 4 * num_akm_suites + 2)
            {
                printf("*WRN* RSN IE too short\n");
                return -1;
            }

            for (ii = 0; ii < num_akm_suites; ii++)
            {
                if (ii < output->num_akm_suites)
                {
                    output->akm_suites[ii] =
                        ies[offset] << 24 | ies[offset+1] << 16 |
                        ies[offset+2] << 8 | ies[offset+3];
                }
                offset += 4;
                length -= 4;
            }

            output->rsn_capabilities = ies[offset] | ies[offset+1] << 8;
            return 1;
        }

        offset += length;
    }

    /* No RSE IE found; implies open security. */
    return 0;
}


void mmhal_init(void)
{
    /* We initialise the MM_RESET_N Pin here so that we can hold the MM6108 in reset regardless of
     * whether the mmhal_wlan_init/deinit function have been called. This allows us to ensure the
     * chip is in its lowest power state. You may want to revise this depending on your particular
     * hardware configuration. */
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (1 << CONFIG_MM_RESET_N);
    io_conf.pull_down_en = 0;
    io_conf.pull_up_en = 0;
    gpio_config(&io_conf);

    gpio_set_level(CONFIG_MM_RESET_N, 0);

    /* Initialise the gpio ISR handler service. This allows per-pin GPIO interrupt handlers and is
     * what is used to register all the wlan related interrupt. */
    gpio_install_isr_service(0);
}


void mmhal_log_write(const uint8_t *data, size_t length)
{
    while (length--)
    {
        putc(*data++, stdout);
    }
}

void mmhal_log_flush(void)
{
}

void mmhal_read_mac_addr(uint8_t *mac_addr)
{
    /* We do not override the MAC address here. Therefore the driver will attempt to read it from
     * the chip and failing that will assign a randomly generated address. */
    memcpy(mac_addr, g_mac_addr, 6);
}

void mmhal_write_mac_addr(uint8_t *mac_addr)
{
    memcpy(g_mac_addr, mac_addr, 6);
}

uint32_t mmhal_random_u32(uint32_t min, uint32_t max)
{
    /* Note: the below implementation does not guarantee a uniform distribution. */

    uint32_t random_value = esp_random();

    if (min == 0 && max == UINT32_MAX)
    {
        return random_value;
    }
    else
    {
        /* Calculate the range and shift required to fit within [min, max] */
        return (random_value % (max - min + 1)) + min;
    }
}

void mmhal_reset(void)
{
    printf("mmhal Resetting...\n");
    fflush(stdout);
    esp_restart();
    while (1)
    { }
}

void mmhal_set_deep_sleep_veto(uint8_t veto_id)
{
    UNUSED(veto_id);
}

void mmhal_clear_deep_sleep_veto(uint8_t veto_id)
{
    UNUSED(veto_id);
}

void mmhal_set_led(uint8_t led, uint8_t level)
{
    UNUSED(led);
    UNUSED(level);
}

bool mmhal_datalink_set_deepsleep_mode(enum mmhal_datalink_deepsleep_mode mode)
{
    /* Do nothing as we currently do not support deep sleep on this platform */
    UNUSED(mode);
    return false;
}

bool mmhal_get_hardware_version(char * version_buffer, size_t version_buffer_length)
{
    /* Note: You need to identify the correct hardware and or version
     *       here using whatever means available (GPIO's, version number stored in EEPROM, etc)
     *       and return the correct string here. */
    return !mmosal_safer_strcpy(version_buffer, "MM-ESP32S3 V1.0", version_buffer_length);
}

/*
 * ---------------------------------------------------------------------------------------------
 *                                      BCF Retrieval
 * ---------------------------------------------------------------------------------------------
 */


/*
 * The following implementation reads the BCF File from the config store.
 */

void mmhal_wlan_read_bcf_file(uint32_t offset, uint32_t requested_len, struct mmhal_robuf *robuf)
{
    /** Points to the start of the BCF binary image. Defined as part of the Makefile */
    extern uint8_t bcf_binary_start;
    /** Points to the end of the BCF binary image. Defined as part of the Makefile */
    extern uint8_t bcf_binary_end;

    size_t bcf_len = &bcf_binary_end - &bcf_binary_start;

    /* Initialise robuf */
    robuf->buf = NULL;
    robuf->len = 0;
    robuf->free_arg = NULL;
    robuf->free_cb = NULL;

    /* Sanity check */
    if (bcf_len < offset)
    {
        printf("Detected an attempt to start reading off the end of the bcf file.\n");
        return;
    }

    robuf->buf = (uint8_t*)&bcf_binary_start + offset;
    robuf->len = bcf_len - offset;
    robuf->len = (robuf->len < requested_len) ? robuf->len : requested_len;
}

/*
 * ---------------------------------------------------------------------------------------------
 *                                    Firmware Retrieval
 * ---------------------------------------------------------------------------------------------
 */
/** Points to the start of the firmware binary image. Defined as part of the Makefile */
extern uint8_t firmware_binary_start;
/** Points to the end of the firmware binary image. Defined as part of the Makefile */
extern uint8_t firmware_binary_end;

void mmhal_wlan_read_fw_file(uint32_t offset, uint32_t requested_len, struct mmhal_robuf *robuf)
{
    uint32_t firmware_len = &firmware_binary_end - &firmware_binary_start;
    if (offset > firmware_len)
    {
        printf("Detected an attempt to start read off the end of the firmware file.\n");
        robuf->buf = NULL;
        return;
    }

    robuf->buf = (&firmware_binary_start + offset);
    firmware_len -= offset;

    robuf->len = (firmware_len < requested_len) ? firmware_len : requested_len;
}
