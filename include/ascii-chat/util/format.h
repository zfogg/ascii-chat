#pragma once

/**
 * @file util/format.h
 * @brief 📊 String Formatting Utilities
 * @ingroup util
 * @addtogroup util
 * @{
 *
 * This header provides utilities for formatting various data types into
 * human-readable strings. Functions handle byte counts, sizes, and other
 * numeric values with appropriate units and formatting.
 *
 * CORE FEATURES:
 * ==============
 * - Byte count formatting with units (B, KB, MB, GB, TB)
 * - Human-readable size strings
 * - Automatic unit selection based on value
 * - Fixed decimal precision
 * - Buffer overflow protection
 *
 * BYTE FORMATTING:
 * ===============
 * Byte counts are formatted with appropriate binary units:
 * - Bytes (B): 0-1023 bytes
 * - Kilobytes (KB): 1024-1048575 bytes
 * - Megabytes (MB): 1048576-1073741823 bytes
 * - Gigabytes (GB): 1073741824-1099511627775 bytes
 * - Terabytes (TB): 1099511627776 and above
 *
 * @note All formatting functions ensure buffer overflow protection.
 * @note Output strings are null-terminated.
 * @note Decimal precision is fixed (2 decimal places for units >= KB).
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date October 2025
 */

#include <stddef.h>

/* ============================================================================
 * Byte Formatting Functions
 * @{
 */

/**
 * @brief Format byte count into human-readable string
 * @param bytes Number of bytes to format
 * @param out Output buffer for formatted string (must not be NULL)
 * @param out_capacity Size of output buffer (must be > 0)
 *
 * Formats a byte count into a human-readable string with appropriate units.
 * Automatically selects the appropriate unit (B, KB, MB, GB, TB, PB, EB) based on
 * the value. Uses binary units (1024-based) for calculations.
 *
 * FORMATTING RULES:
 * - 0-1023 bytes: "N B" (e.g., "512 B")
 * - 1024-1048575 bytes: "N.NN KB" (e.g., "1.50 KB")
 * - 1048576-1073741823 bytes: "N.NN MB" (e.g., "2.34 MB")
 * - 1073741824-1099511627775 bytes: "N.NN GB" (e.g., "1.25 GB")
 * - 1099511627776-1125899906842623 bytes: "N.NN TB" (e.g., "1.50 TB")
 * - 1125899906842624-1152921504606846975 bytes: "N.NN PB" (e.g., "1.50 PB")
 * - 1152921504606846976 and above: "N.NN EB" (e.g., "1.00 EB")
 *
 * @note Output buffer should be at least 32 bytes to accommodate all formats.
 * @note Function truncates output if buffer is too small.
 * @note Decimal precision is 2 places for units >= KB.
 * @note Uses binary units (1024-based), not decimal (1000-based).
 *
 * @par Example
 * @code
 * char buf[64];
 * format_bytes_pretty(1024, buf, sizeof(buf));        // "1.00 KB"
 * format_bytes_pretty(1048576, buf, sizeof(buf));    // "1.00 MB"
 * format_bytes_pretty(1536, buf, sizeof(buf));       // "1.50 KB"
 * format_bytes_pretty(1125899906842624, buf, sizeof(buf)); // "1.00 PB"
 * format_bytes_pretty(1152921504606846976ULL, buf, sizeof(buf)); // "1.00 EB"
 * @endcode
 *
 * @ingroup util
 */
void format_bytes_pretty(size_t bytes, char *out, size_t out_capacity);

/** @} */
