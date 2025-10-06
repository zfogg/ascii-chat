/**
 * @file utf8.h
 * @brief UTF-8 encoding and decoding utilities
 *
 * Simple, efficient UTF-8 validation and decoding without external dependencies.
 */

#pragma once

#include <stdint.h>

/**
 * @brief Decode a UTF-8 sequence to a Unicode codepoint
 *
 * @param s Pointer to UTF-8 byte sequence
 * @param codepoint Output parameter for decoded codepoint
 * @return Number of bytes consumed (1-4), or -1 if invalid
 *
 * @example
 * uint32_t cp;
 * const uint8_t *str = (uint8_t*)"Hello 世界";
 * int len = utf8_decode(str, &cp);  // Returns 1, cp = 'H'
 */
int utf8_decode(const uint8_t *s, uint32_t *codepoint);
