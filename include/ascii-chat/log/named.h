/**
 * @file log/named.h
 * @brief Format named objects in log messages (replaces hex addresses with type/name descriptions)
 * @ingroup logging
 * @addtogroup logging
 * @{
 *
 * This module formats named objects in log messages by scanning for hex addresses
 * and replacing them with friendly descriptions from the named object registry when available.
 *
 * For example:
 * - Input:  "Locked 0x7f1234567890 successfully"
 * - Output: "Locked mutex: recv_mutex.2 (0x7f1234567890) successfully"
 *
 * When an address is not registered, it's left unchanged.
 */

#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Format hex addresses in a message as named object descriptions
 * @param message Input message (may contain hex addresses like 0x123456)
 * @param output Output buffer for formatted message
 * @param output_size Size of output buffer (must be > 0)
 * @return Length of output string (excluding null terminator) if any formatting was applied,
 *         -1 if no formatting was applied or on error
 * @ingroup logging
 *
 * Scans the input message for hex addresses in the format 0x[0-9a-fA-F]+.
 * For each address found, checks the named object registry.
 * If registered, replaces the address with "type: name (0xaddress)" format.
 * If not registered, leaves the original hex address unchanged.
 *
 * The output buffer is always null-terminated.
 *
 * @note The formatting is idempotent - applying it twice gives the same result
 * @note This function is efficient for messages with few hex addresses (common case)
 * @note Works correctly with 32-bit and 64-bit address formats
 */
int log_named_format_message(const char *message, char *output, size_t output_size);

/**
 * @brief Format named objects in a message (thread-local buffer)
 * @param message Input message
 * @return Formatted message string if any formatting was applied,
 *         original message if no formatting was applied
 * @ingroup logging
 *
 * Convenience wrapper that handles buffer management internally using
 * a thread-local buffer. The returned pointer is valid only until the
 * next call to this function from the same thread.
 *
 * Safe to use directly in log macros:
 * ```c
 * log_info("State: %s", log_named_format_or_original(msg));
 * ```
 *
 * @note Maximum formatted message length is 4096 bytes
 * @note If format buffer overflows, returns original message
 */
const char *log_named_format_or_original(const char *message);

#ifdef __cplusplus
}
#endif

/** @} */ /* logging */
