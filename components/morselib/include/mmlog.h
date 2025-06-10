/*
 * Morse logging API
 *
 * Copyright 2023 Morse Micro
 *
 * This file is licensed under terms that can be found in the LICENSE.md file in the root
 * directory of the Morse Micro IoT SDK software package.
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

/**
 * Macro for printing a @c uint64_t as two separate @c uint32_t values. This is to allow printing of
 * these values even when the @c printf implementation doesn't support it.
 */
#define MM_X64_VAL(value) ((uint32_t) (value>>32)), ((uint32_t)value)

/** Macro for format specifier to print @ref MM_X64_VAL */
#define MM_X64_FMT "%08lx%08lx"

/**
 * Initialize Morse logging API.
 *
 * This should be invoked after OS initialization since it will create a mutex for
 * logging.
 */
void mm_logging_init(void);

/**
 * Dumps a binary buffer in hex.
 *
 * @param level         A single character indicating log level.
 * @param function      Name of function this was invoked from.
 * @param line_number   Line number this was invoked from.
 * @param title         Title of the buffer.
 * @param buf           The buffer to dump.
 * @param len           Length of the buffer.
 */
void mm_hexdump(char level, const char *function, unsigned line_number,
                      const char *title, const uint8_t *buf, size_t len);

#ifdef __cplusplus
}
#endif
