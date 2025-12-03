/**
 * @file util/utf8.h
 * @brief 🔤 UTF-8 Encoding and Decoding Utilities
 * @ingroup util
 * @addtogroup util
 * @{
 *
 * This header provides simple, efficient UTF-8 validation and decoding
 * without external dependencies. The implementation handles multi-byte
 * UTF-8 sequences and validates encoding correctness.
 *
 * CORE FEATURES:
 * ==============
 * - Multi-byte UTF-8 sequence decoding (1-4 bytes)
 * - Unicode codepoint extraction
 * - UTF-8 validation during decoding
 * - No external dependencies (pure C implementation)
 * - Safe handling of invalid sequences
 *
 * UTF-8 ENCODING:
 * ==============
 * UTF-8 encodes Unicode codepoints using 1-4 bytes:
 * - 1 byte: ASCII characters (0x00-0x7F)
 * - 2 bytes: Latin-1 supplement, etc. (0x80-0x7FF)
 * - 3 bytes: Most CJK characters (0x800-0xFFFF)
 * - 4 bytes: Rare characters, emoji (0x10000-0x10FFFF)
 *
 * VALIDATION:
 * ===========
 * The decoder validates UTF-8 sequences during decoding:
 * - Checks for valid byte sequences according to UTF-8 rules
 * - Detects overlong encodings (security feature)
 * - Detects invalid byte patterns
 * - Returns error on invalid sequences
 *
 * @note This is a minimal implementation for basic UTF-8 handling.
 * @note For full Unicode support, consider using a library like ICU.
 * @note Invalid sequences return -1 to indicate error.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date October 2025
 */

#pragma once

#include <stdint.h>

/* ============================================================================
 * UTF-8 Decoding Functions
 * @{
 */

/**
 * @brief Decode a UTF-8 sequence to a Unicode codepoint
 * @param s Pointer to UTF-8 byte sequence (must not be NULL)
 * @param codepoint Output parameter for decoded codepoint (must not be NULL)
 * @return Number of bytes consumed (1-4), or -1 if invalid sequence
 *
 * Decodes a UTF-8 byte sequence starting at s to a Unicode codepoint. Validates
 * the UTF-8 encoding during decoding and returns the number of bytes consumed.
 * Returns -1 if the sequence is invalid (overlong encoding, invalid bytes, etc.).
 *
 * @note Input pointer must point to the start of a valid UTF-8 sequence.
 * @note Output codepoint is set even if sequence is invalid (check return value).
 * @note Function does not advance the pointer - caller must advance by return value.
 * @note Return value of -1 indicates invalid UTF-8 sequence.
 *
 * @par Example
 * @code
 * const uint8_t *str = (uint8_t*)"Hello 世界";
 * uint32_t cp;
 * int len;
 *
 * // Decode 'H' (1 byte)
 * len = utf8_decode(str, &cp);  // Returns 1, cp = 'H'
 * str += len;
 *
 * // Decode '世' (3 bytes)
 * len = utf8_decode(str, &cp);  // Returns 3, cp = U+4E16
 * str += len;
 * @endcode
 *
 * @ingroup util
 */
int utf8_decode(const uint8_t *s, uint32_t *codepoint);

/** @} */
