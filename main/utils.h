#ifndef __UTILS_H__
#define __UTILS_H__

#include <ctype.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/time.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ABS(a) (((a)>0)?(a):-(a))
#ifndef MAX
#define MAX(a,b) (((a)>(b))?(a):(b))
#endif
#ifndef MIN
#define MIN(a,b) (((a)<(b))?(a):(b))
#endif

/**
 * @brief Display formatted time in logs
 * @param log Prefix string for log message
 * @param t Time value to display
 */
void misc_show_time(char *log, time_t t);

/**
 * @brief Convert MAC address string to hex array
 * @param mac_str MAC address string (format: XX:XX:XX:XX:XX:XX)
 * @param mac_hex Output buffer for hex values
 * @return Pointer to mac_hex buffer
 */
uint8_t *mac_str2hex(const char *mac_str, uint8_t *mac_hex);

/**
 * @brief Convert MAC address hex array to string
 * @param mac_hex MAC address as hex array
 * @param mac_str Output buffer for string
 * @return Pointer to mac_str buffer
 */
char *mac_hex2str(const uint8_t *mac_hex, char *mac_str);

/**
 * @brief Validate MAC address string format
 * @param mac_str MAC address string to validate
 * @return true if valid, false otherwise
 */
bool is_valid_mac(const char *mac_str);

/**
 * @brief Replace all spaces in string with specified character
 * @param str String to process
 * @param ch Replacement character
 * @return Modified string
 */
char *replace_space(char *str, char ch);

/**
 * @brief Get current time in milliseconds
 * @return Current time in ms since epoch
 */
uint64_t get_time_ms();

/**
 * @brief Get current timestamp as string
 * @param timestamp Output buffer
 * @param len Buffer length
 * @return 0 on success, -1 on error
 */
int8_t get_timestamp(char *timestamp, int len);

/**
 * @brief Generate random alphanumeric string
 * @param str Output buffer
 * @param len Desired string length
 */
void generate_random_string(char *str, size_t len);

/**
 * @brief Calculate MD5 hash of input data
 * @param input Data to hash
 * @param ilen Input data length
 * @param output Pointer to receive hash string (allocated, caller must free)
 * @return 0 on success, -1 on error
 */
int md5_calc(const unsigned char *input, size_t ilen, unsigned char **output);

/**
 * @brief Calculate CRC32 checksum (placeholder implementation)
 * @param in Input data
 * @param out Output buffer
 * @return Currently always returns 0
 */
int crc32_calc(const char *in, char *out);

/**
 * @brief Read entire file into memory
 * @param filename File to read
 * @return Allocated buffer with file contents (caller must free), NULL on error
 */
char *filesystem_read(const char *filename);

/**
 * @brief Write data to file
 * @param filename File to write
 * @param data Data to write
 * @param len Data length
 * @return 0 on success, -1 on error
 */
int filesystem_write(const char *filename, const char *data, size_t len);

/**
 * @brief Delete file
 * @param filename File to delete
 * @return 0 on success, -1 on error
 */
int filesystem_delete(const char *filename);

/**
 * @brief Dump file contents to stdout
 * @param filename File to dump
 * @return 0 on success, -1 on error
 */
int filesystem_dump(const char *filename);

/**
 * @brief Check if file exists
 * @param filename File to check
 * @return true if exists, false otherwise
 */
bool filesystem_is_exist(const char *filename);

/**
 * @brief Create a new task with memory allocated from SPIRAM
 * @param taskfunc Task function
 * @param name Task name
 * @param stack_size Stack size in bytes
 * @param param Task parameter
 * @param prio Task priority
 * @param core_id Core to pin task to
 * @return Task handle, NULL on error
 * @note For ESP IDF V5.1.0+, taskfunc must not contain SPI flash operations
 */
void *task_create(void *taskfunc, const char *name, uint32_t stack_size, void *param, uint32_t prio, uint32_t core_id);

/**
 * @brief Delete a task and free its resources
 * @param handle Task handle to delete
 */
void task_delete(void *handle);

#ifdef __cplusplus
}
#endif


#endif /* __UTILS_H__ */
