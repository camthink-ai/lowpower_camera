/*
 * Copyright 2023 Morse Micro
 *
 * This file is licensed under terms that can be found in the LICENSE.md file in the root
 * directory of the Morse Micro IoT SDK software package.
 */


/**
 * @ingroup MMHAL
 * @defgroup MMHAL_WLAN WLAN HAL
 *
 * API for communicating with the WLAN transceiver.
 *
 * There are different interfaces supported for communicating with the transceiver:
 *
 * * @ref MMHAL_WLAN_SDIO
 * * @ref MMHAL_WLAN_SPI
 *
 * @warning These functions shall not be called directly by the end application they are for use
 *          by Morselib.
 *
 * @{
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif


/**
 * Function prototype for interrupt handler callbacks.
 */
typedef void (*mmhal_irq_handler_t)(void);

/**
 * Initialize the WLAN HAL.
 *
 * Things to do here may include:
 * * Enable SPI peripheral
 * * Configure GPIOs
 * * Enable power to the Morse Micro transceiver
 *
 * @note If enabling power for the Morse Micro transceiver in this function you may need to add a
 *       blocking delay to allow the power rail to stabilize. This is hardware specific so is not
 *       accounted for in the calling function.
 */
void mmhal_wlan_init(void);

/**
 * Deinitialize the WLAN HAL.
 *
 * Things to do here may include:
 * * Disable SPI peripheral
 * * Disable GPIOs
 * * Disable power to the Morse Micro transceiver
 */
void mmhal_wlan_deinit(void);

/**
 * Shutdown the WLAN HAL.
 *
 * This function should be used to put the WLAN transceiver into its lowest power state. It should
 * be called when the WLAN interface is no longer required.
 */
void mmhal_wlan_shutdown(void);

/**
 * Get MAC address override.
 *
 * This function allows the HAL to override the MAC address to be used by the device. The
 * MAC address override should be written to @p mac_addr. If no override is required then
 * @p mac_addr should be left untouched.
 *
 * @param[out] mac_addr Location where the MAC address will be stored. This will be initialized
 *                      to zero the first time this function is invoked, and to the previously
 *                      configured MAC address on subsequent invocations.
 */
void mmhal_read_mac_addr(uint8_t *mac_addr);

/**
 * Set MAC address override.
 *
 * This function allows the HAL to override the MAC address to be used by the device. The
 * MAC address override should be read from @p mac_addr.
 *
 * @param[in] mac_addr The MAC address to be used by the device.
 */
void mmhal_write_mac_addr(uint8_t *mac_addr);

/**
 * Assert the WLAN wake pin.
 */
void mmhal_wlan_wake_assert(void);

/**
 * Deassert the WLAN wake pin.
 */
void mmhal_wlan_wake_deassert(void);

/**
 * Tests whether the busy pin is currently asserted.
 *
 * @note This is whether it is logically asserted and does not necessarily
 *       represent the level of the GPIO pin.
 *
 * @returns @c true if asserted, else @c false.
 */
bool mmhal_wlan_busy_is_asserted(void);

/**
 * Register a handler for busy interrupts.
 *
 * @param handler   The handler to register.
 */
void mmhal_wlan_register_busy_irq_handler(mmhal_irq_handler_t handler);

/**
 * Sets whether the busy interrupt is enabled.
 *
 * @warning The interrupt handler function must be configured using
 *          mmhal_wlan_register_busy_irq_handler() before enabling the interrupt.
 *
 * @param enabled   @c true to enable or @c false to disable.
 */
void mmhal_wlan_set_busy_irq_enabled(bool enabled);

/**
 * Read-only buffer data structure.
 *
 * The design of this data structure allows the buffer to exist either in statically or
 * dynamically allocated memory.
 *
 * For statically allocated memory, the field @c free_cb may be set to @c NULL and @c free_arg
 * ignored. For example:
 *
 * @code{.c}
 * const uint8_t some_data[] = { 0x00, 0x01, 0x02 };
 *
 * void put_some_data_into_buf(struct mmhal_robuf *robuf)
 * {
 *     robuf->buf = some_data;
 *     robuf->len = sizeof(some_data);
 *     robuf->free_cb = NULL;
 * }
 * @endcode
 *
 * For dynamically allocated memory, the field @c free_cb is set to the appropriate function to
 * free the buffer and @c free_arg is an opaque argument to the free function. This approach might
 * be used, for example, when reading into a temporary buffer from storage that is not memory
 * mapped. For example:
 *
 * @code{.c}
 * #define SOME_DATA_MAXLEN (64)
 *
 * void put_some_data_into_buf(struct mmhal_robuf *robuf)
 * {
 *     uint8_t *buf = malloc(SOME_DATA_MAXLEN);
 *     robuf->buf = buf;
 *     if (robuf->buf == NULL)
 *         return;
 *
 *     // HERE: copy data into buf and set robuf->len as appropriate
 *
 *     robuf->free_cb = free;
 *     robuf->free_arg = buf;
 * }
 * @endcode
 *
 */
struct mmhal_robuf
{
    /** Pointer to the start of the read-only buffer. May be NULL only if @c len is zero. */
    const uint8_t *buf;
    /** Length of the buffer contents. */
    uint32_t len;
    /**
     * Optional callback to be invoked by the consumer to release the buffer when it is
     * no longer required. If not required, set to @c NULL.
     *
     * @note The values of @c buf and @c len in this structure may be modified before
     *       @c free_cb() is invoked. However, the value of @c free_arg will be passed
     *       to @c free_cb().
     */
    void (*free_cb)(void *arg);
    /** Optional argument to @c free_cb. Ignored if @c free_cb is @c NULL. */
    void *free_arg;
};

/** Minimum length of data to be returned by @ref mmhal_wlan_read_bcf_file() and
 *  @ref mmhal_wlan_read_fw_file(). */
#define MMHAL_WLAN_FW_BCF_MIN_READ_LENGTH   (4)

/**
 * Retrieves the content of the Morse Micro Board Configuration File and places it into the given
 * buffer.
 *
 * @param offset        Offset from which to start reading the bcf
 * @param requested_len Length of data we would like to read. The length of the data returned
 *                      by this function may be less than @p requested_len, but must be at
 *                      least @ref MMHAL_WLAN_FW_BCF_MIN_READ_LENGTH.
 * @param robuf         Read-only buffer data structure to be filled out by this function.
 *
 * @note On error, this function should set @c robuf->buf to @c NULL.
 * @note The caller must zero @p robuf before invoking the function.
 * @note The BCF must be in mbin format.
 *
 * @warning The caller is responsible for checking @c robuf->free_cb and calling when the buffer is
 * no longer required. Ignored if @c robuf->free_cb is @c NULL.
 */
void mmhal_wlan_read_bcf_file(uint32_t offset, uint32_t requested_len, struct mmhal_robuf *robuf);

/**
 * Retrieves the content of the Morse Micro Chip Firmware and places it into the given buffer.
 *
 * @param offset        Offset from which to start reading the bcf
 * @param requested_len Length of data we would like to read. The length of the data returned
 *                      by this function may be less than @p requested_len, but must be at
 *                      least @ref MMHAL_WLAN_FW_BCF_MIN_READ_LENGTH.
 * @param robuf         Read-only buffer data structure to be filled out by this function.
 *
 * @note On error, this function should set @c robuf->buf to @c NULL.
 * @note The caller must zero @p robuf before invoking the function.
 * @note The firmware must be in mbin format.
 *
 * @warning The caller is responsible for checking @c robuf->free_cb and calling when the buffer
 * is no longer required. Ignored if @c robuf->free_cb is @c NULL.
 */
void mmhal_wlan_read_fw_file(uint32_t offset, uint32_t requested_len, struct mmhal_robuf *robuf);


/**
 * @defgroup MMHAL_WLAN_SPI WLAN HAL API for SPI interface
 *
 * API for communicating with the WLAN transceiver over an SPI interface.
 *
 * @note These functions should only be implemented if using a SPI interface. They are not
 *       required when using the @ref MMHAL_WLAN_SDIO.
 *
 * @warning These functions shall not be called directly by the end application they are for use
 *          by Morselib.
 *
 * @{
 */

/**
 * Assert the WLAN SPI chip select pin.
 */
void mmhal_wlan_spi_cs_assert(void);

/**
 * Deassert the WLAN SPI chip select pin.
 */
void mmhal_wlan_spi_cs_deassert(void);

/**
 * Simultaneously read and write on the SPI bus.
 *
 * @param data   Data to be written.
 *
 * @return the value that was read.
 */
uint8_t mmhal_wlan_spi_rw(uint8_t data);

/**
 * Receive multiple octets of data from SPI bus.
 *
 * @param buf       The buffer to receive into.
 * @param len       The number of octets to receive.
 */
void mmhal_wlan_spi_read_buf(uint8_t *buf, unsigned len);

/**
 * Transmit multiple octets of data to SPI bus.
 *
 * @param buf       The buffer to transmit from.
 * @param len       The number of octets to transmit.
 *
 * @note Blocks until transfer complete.
 */
void mmhal_wlan_spi_write_buf(const uint8_t *buf, unsigned len);

/**
 * Hard reset the chip by asserting and then releasing the reset pin.
 *
 * @warning This function must return with the chip in a fully booted state. i.e only return once
 *          the reset_n line has been high for at least the boot time specified in the data sheet.
 *          Failure to do so may lead to undefined behavior.
 */
void mmhal_wlan_hard_reset(void);

/**
 * Issue the training sequence required to put the transceiver into SPI mode.
 */
void mmhal_wlan_send_training_seq(void);

/**
 * Register a handler for SPI interrupts.
 *
 * @param handler   The handler to register.
 */
void mmhal_wlan_register_spi_irq_handler(mmhal_irq_handler_t handler);

/**
 * Sets whether the SPI interrupt is enabled.
 *
 * @warning The interrupt handler function must be configured using
 *          @ref mmhal_wlan_register_spi_irq_handler() before enabling the interrupt.
 *
 * @param enabled   @c true to enable or @c false to disable.
 */
void mmhal_wlan_set_spi_irq_enabled(bool enabled);

/**
 * Tests whether the SPI interrupt pin is currently asserted.
 *
 * @note This is whether it is logically asserted and does not necessarily
 *       represent the level of the GPIO pin.
 *
 * @returns @c true if asserted, else @c false.
 */
bool mmhal_wlan_spi_irq_is_asserted(void);

/**
 * Clear the SPI IRQ.
 *
 * @deprecated Do not invoke this function because it is deprecated and will be removed from the
 *             mmhal API in a future release. This function need not be implemented as a weak
 *             stub is used in morselib.
 */
void mmhal_wlan_clear_spi_irq(void);

/** @} */


/**
 * @defgroup MMHAL_WLAN_SDIO WLAN HAL API for SDIO interface
 *
 * API for communicating with the WLAN transceiver over an SDIO interface
 *
 * @warning These functions shall not be called directly by the end application they are for use
 *          by Morselib.
 *
 * @{
 */

/** Enumeration of error codes that may be returned from @c mmhal_wlan_sdio_XXX() functions. */
enum mmhal_sdio_error_codes
{
    /** Invalid argument given (e.g., incorrect buffer alignment). */
    MMHAL_SDIO_INVALID_ARGUMENT = -1,
    /** Local hardware error (e.g., issue with SDIO controller). */
    MMHAL_SDIO_HW_ERROR = -2,
    /** Timeout executing SDIO command. */
    MMHAL_SDIO_CMD_TIMEOUT = -3,
    /** CRC error executing SDIO command. */
    MMHAL_SDIO_CMD_CRC_ERROR = -4,
    /** Timeout transferring data. */
    MMHAL_SDIO_DATA_TIMEOUT = -5,
    /** CRC error transferring data. */
    MMHAL_SDIO_DATA_CRC_ERROR = -6,
    /** Underflow filling SDIO controller FIFO. */
    MMHAL_SDIO_DATA_UNDERFLOW = -7,
    /** Overflow reading from SDIO controller FIFO. */
    MMHAL_SDIO_DATA_OVERRUN = -8,
    /** Another error not covered by the above error codes. */
    MMHAL_SDIO_OTHER_ERROR = -9,
};

/**
 * Perform transport specific startup.
 *
 * @returns 0 on success, an error code from @ref mmhal_sdio_error_codes on failure.
 */
int mmhal_wlan_sdio_startup(void);

/**
 * Execute an SDIO command without data.
 *
 * @param[in]  cmd_idx  The Command Index.
 * @param[in]  arg      Command argument. This corresponds to the 32 bits of the command between
 *                      the Command Index field and the CRC7 field.
 * @param[out] rsp      The contents of the command response between the Command Index field
 *                      and the CRC7 field. May be @c NULL if the response is not required.
 *                      The returned value is undefined if the return code is not zero.
 *
 * @returns 0 on success, an error code from @ref mmhal_sdio_error_codes on failure.
*/
int mmhal_wlan_sdio_cmd(uint8_t cmd_idx, uint32_t arg, uint32_t *rsp);

/**
 * Arguments structure for @ref mmhal_wlan_sdio_cmd53_write().
 */
struct mmhal_wlan_sdio_cmd53_write_args
{
    /** The SDIO argument. This corresponds to the 32 bits of the command between
      * the Command Index field and the CRC7 field. */
    uint32_t sdio_arg;
    /** Pointer to the data buffer. 32 bit word aligned. */
    const uint8_t *data;
    /** Transfer length measured in blocks if block_size is non-zero otherwise in bytes.
     *  If transfer_length is measured in bytes, it will be a multiple of 4. */
    uint16_t transfer_length;
    /**
     * If non-zero this indicates that the data should be transferred in block mode with
     * the given block size. If zero then the data should be transferred in byte mode and
     * @c transfer_length is guaranteed to not exceed the block size of the function.
     */
    uint16_t block_size;
};

/**
 * Execute an SDIO CMD53 write.
 *
 * @param args  The write arguments.
 *
 * @returns 0 on success, an error code from @ref mmhal_sdio_error_codes on failure.
 */
int mmhal_wlan_sdio_cmd53_write(const struct mmhal_wlan_sdio_cmd53_write_args *args);

/**
 * Arguments structure for @ref mmhal_wlan_sdio_cmd53_read().
 */
struct mmhal_wlan_sdio_cmd53_read_args
{
    /** The SDIO argument. This corresponds to the 32 bits of the command between
      * the Command Index field and the CRC7 field. */
    uint32_t sdio_arg;
    /** Pointer to the data buffer to receive the data. 32 bit word aligned. */
    uint8_t *data;
    /** Transfer length measured in blocks if block_size is non-zero otherwise in bytes.
     *  If transfer_length is measured in bytes, it will be a multiple of 4. */
    uint16_t transfer_length;
    /**
     * If non-zero this indicates that the data should be transferred in block mode with
     * the given block size. If zero then the data should be transferred in byte mode and
     * @c transfer_length is guaranteed to not exceed the block size of the function.
     */
    uint16_t block_size;
};

/**
 * Execute an SDIO CMD53 read.
 *
 * @param args  The read arguments.
 *
 * @returns 0 on success, an error code from @ref mmhal_sdio_error_codes on failure.
 */
int mmhal_wlan_sdio_cmd53_read(const struct mmhal_wlan_sdio_cmd53_read_args *args);


/**
 * @defgroup MMHAL_WLAN_SDIO_UTILS SDIO Utilities
 *
 * Useful macros and inline utilities function for use by SDIO and SPI HALs.
 *
 * @{
 */


/*
 * SDIO argument definition, per SDIO Specification Version 4.10, Part E1, Section 5.3.
 */

/** SDIO CMD52/CMD53 R/W flag. */
enum mmhal_sdio_rw
{
    MMHAL_SDIO_READ  = 0,               /**< Read operation */
    MMHAL_SDIO_WRITE = (1ul << 31),     /**< Write operation */
};

/** SDIO CMD52/CMD53 function number. */
enum mmhal_sdio_function
{
    MMHAL_SDIO_FUNCTION_0 = 0,              /** Function 0 */
    MMHAL_SDIO_FUNCTION_1 = (1ul << 28),    /** Function 1 */
    MMHAL_SDIO_FUNCTION_2 = (2ul << 28),    /** Function 2 */
};

/** SDIO CMD53 block mode*/
enum mmhal_sdio_mode
{
    MMHAL_SDIO_MODE_BYTE = 0,               /** Byte mode */
    MMHAL_SDIO_MODE_BLOCK = (1ul << 27),    /** Block mode */
};

/** SDIO CMD53 OP code */
enum mmhal_sdio_opcode
{
    /** Operate on a single, fixed address. */
    MMHAL_SDIO_OPCODE_FIXED_ADDR = 0,
    /** Increment address by 1 after each byte. */
    MMHAL_SDIO_OPCODE_INC_ADDR   = (1ul << 26),
};

/** CMD52/53 Register Address (17 bit) offset. */
#define MMHAL_SDIO_ADDRESS_OFFSET (9)
/** CMD52/53 Register Address maximum value. */
#define MMHAL_SDIO_ADDRESS_MAX    ((1ul << 18) - 1)

/** CMD53 Byte/block count offset (9 bit). */
#define MMHAL_SDIO_COUNT_OFFSET   (0)
/**CMD53 Byte/block count maximum value. */
#define MMHAL_SDIO_COUNT_MAX      ((1ul << 10) - 1)

/** CMD52 Data (8 bit) offset */
#define MMHAL_SDIO_CMD52_DATA_OFFSET  (0)

/**
 * Construct an SDIO CMD52 argument based on the given arguments.
 *
 * @param rw            Flag indication direction (read or write).
 * @param fn            The applicable function.
 * @param address       The address to read/write. Must be <= @c MMHAL_SDIO_ADDRESS_MAX.
 * @param write_data    The data to write if this is a write operation. Should be set to zero
 *                      for a read operation.
 *
 * @return the SDIO CMD52 argument generated based on the given arguments.
 */
static inline uint32_t mmhal_make_cmd52_arg(enum mmhal_sdio_rw rw, enum mmhal_sdio_function fn,
                                            uint32_t address, uint8_t write_data)
{
    uint32_t arg;

    arg = rw | fn;
    arg |= (address << MMHAL_SDIO_ADDRESS_OFFSET);
    arg |= (write_data << MMHAL_SDIO_CMD52_DATA_OFFSET);
    return arg;
}

/**
 * Construct an SDIO CMD53 argument based on the given arguments.
 *
 * @param rw            Flag indication direction (read or write).
 * @param fn            The applicable function.
 * @param mode          Selects between byte and block mode.
 * @param address        The address to read/write. Must be <= @c MMHAL_SDIO_ADDRESS_MAX.
 * @param count         The count of bytes/blocks (depending on @p mode) to transfer. Must
 *                      be <= @c MMHAL_SDIO_COUNT_MAX.
 *
 * @note OP Code 1 (incrementing address) is assumed. See also @ref MMHAL_SDIO_OPCODE_INC_ADDR.
 *
 * @return the SDIO CMD53 argument generated based on the given arguments.
 */
static inline uint32_t mmhal_make_cmd53_arg(enum mmhal_sdio_rw rw, enum mmhal_sdio_function fn,
                                            enum mmhal_sdio_mode mode, uint32_t address,
                                            uint16_t count)
{
    uint32_t arg;

    arg = rw | fn | MMHAL_SDIO_OPCODE_INC_ADDR | mode;
    arg |= (address << MMHAL_SDIO_ADDRESS_OFFSET);
    arg |= (count << MMHAL_SDIO_COUNT_OFFSET);
    return arg;
}

/** @} */

/** @} */


#ifdef __cplusplus
}
#endif

/** @} */