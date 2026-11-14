/**
 * @file platform/wasm_console.h
 * @brief WASM browser console logging API
 * @ingroup platform
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

/**
 * @brief Route output to browser console based on log level
 *
 * Parses the log level from the message format [LEVEL] and routes
 * to the appropriate console method (console.debug, console.log, etc).
 *
 * @param fd File descriptor (typically 1 for stdout, 2 for stderr)
 * @param buf Message buffer
 * @param count Message length
 */
void wasm_log_to_console(int fd, const uint8_t *buf, size_t count);

/**
 * @brief Parse log level from formatted message
 *
 * Expects format: "[LEVEL] message..."
 *
 * @param buf Message buffer
 * @param count Message length
 * @return log_level_t enum value (0-5), or -1 if not found
 */
int wasm_parse_log_level(const uint8_t *buf, size_t count);
