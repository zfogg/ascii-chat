/**
 * @file util/utf8.h
 * @brief 🔤 UTF-8 Encoding and Decoding Utilities
 * @ingroup util
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
#include <stddef.h>

/* ============================================================================
 * UTF-8 Decoding Functions
 * ========================================================================== */

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

/* ============================================================================
 * UTF-8 Display Width Calculation
 * ========================================================================== */

/**
 * @brief Calculate terminal display width of a UTF-8 string
 * @param str UTF-8 string (must not be NULL)
 * @return Display width in terminal columns (0 on empty or invalid input)
 *
 * Converts UTF-8 byte sequences to Unicode codepoints and calculates the terminal
 * display width using utf8proc_charwidth(). Properly handles:
 * - Multi-byte UTF-8 sequences (1-4 bytes per character)
 * - Wide characters (CJK, emoji - typically 2 columns)
 * - Control characters and combining marks (0 width)
 * - Invalid UTF-8 sequences (stops processing)
 *
 * Terminal display width differs from byte count:
 * - ASCII 'A' = 1 byte = 1 column
 * - 'é' (é) = 2 bytes = 1 column
 * - '中' (CJK) = 3 bytes = 2 columns
 * - '😀' (emoji) = 4 bytes = 2 columns
 *
 * @note Uses utf8proc library for accurate Unicode character-width computation
 * @note Returns 0 for NULL or empty string
 * @note Invalid UTF-8 sequences stop processing (partial width returned)
 *
 * @par Example
 * @code
 * int width = utf8_display_width("Hello");      // Returns 5 (5 ASCII chars)
 * int width = utf8_display_width("Café");       // Returns 4 (é is 1 column)
 * int width = utf8_display_width("中国");       // Returns 4 (2 CJK chars × 2 columns)
 * int width = utf8_display_width("😀");         // Returns 2 (emoji is 2 columns)
 * @endcode
 *
 * @ingroup util
 */
int utf8_display_width(const char *str);

/**
 * @brief Calculate terminal display width of a UTF-8 string up to byte limit
 * @param str UTF-8 string (must not be NULL)
 * @param max_bytes Maximum bytes to process
 * @return Display width in terminal columns
 *
 * Like utf8_display_width() but stops after processing max_bytes bytes.
 * Useful for calculating display width of substrings or with length-limited buffers.
 *
 * @note Stops processing at max_bytes boundary or null terminator, whichever comes first
 * @note Invalid UTF-8 sequences stop processing (partial width returned)
 * @note Uses utf8proc library for accurate Unicode character-width computation
 *
 * @par Example
 * @code
 * const char *str = "Hello World";
 * int width = utf8_display_width_n(str, 5);  // Returns 5 (width of "Hello")
 * @endcode
 *
 * @ingroup util
 */
int utf8_display_width_n(const char *str, size_t max_bytes);
