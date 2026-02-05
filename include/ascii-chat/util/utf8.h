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
#include <stdbool.h>

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

/* ============================================================================
 * UTF-8 Validation Functions
 * ========================================================================== */

/**
 * @brief Check if a string is valid UTF-8
 * @param str String to validate (must not be NULL)
 * @return true if valid UTF-8, false if invalid sequences detected
 *
 * Scans the entire string and validates that all byte sequences conform to
 * UTF-8 encoding rules. Returns false immediately on first invalid sequence.
 *
 * @note Returns true for empty string (valid)
 * @note Returns false for NULL input (not validated)
 * @note More efficient than decoding all codepoints if only validation is needed
 *
 * @par Example
 * @code
 * if (!utf8_is_valid(user_input)) {
 *     log_error("Invalid UTF-8 in user input");
 *     return false;
 * }
 * @endcode
 *
 * @ingroup util
 */
bool utf8_is_valid(const char *str);

/**
 * @brief Check if a string contains only ASCII characters
 * @param str String to check (must not be NULL)
 * @return true if all characters are ASCII (0x00-0x7F), false if any non-ASCII found
 *
 * Validates UTF-8 encoding and checks that all decoded codepoints are ASCII.
 * Useful for security-sensitive strings that should only contain ASCII
 * (e.g., session identifiers to prevent homograph attacks).
 *
 * Returns false if string contains invalid UTF-8 sequences.
 *
 * @note Empty string is considered ASCII-only (returns true)
 * @note Returns false for NULL input (not validated)
 * @note Prevents homograph attack vector (e.g., Cyrillic 'а' vs ASCII 'a')
 *
 * @par Example
 * @code
 * // Verify session string contains only ASCII to prevent homograph spoofing
 * if (!utf8_is_ascii_only(session_string)) {
 *     log_warn("Session string contains non-ASCII characters");
 *     return false;
 * }
 * @endcode
 *
 * @ingroup util
 */
bool utf8_is_ascii_only(const char *str);

/**
 * @brief Count UTF-8 characters (not bytes)
 * @param str String to count (must not be NULL)
 * @return Number of UTF-8 characters, or SIZE_MAX if invalid UTF-8 detected
 *
 * Counts the number of Unicode codepoints in the string, not byte count.
 * Properly handles multi-byte UTF-8 sequences (1-4 bytes per character).
 *
 * Returns SIZE_MAX if the string contains invalid UTF-8 sequences.
 *
 * @note Empty string has 0 characters
 * @note Returns SIZE_MAX for NULL input or invalid UTF-8
 * @note Slower than strlen() but necessary for correct UTF-8 processing
 * @note Used by Levenshtein distance and other algorithms needing character counts
 *
 * @par Example
 * @code
 * // Check if password is long enough (in characters, not bytes)
 * size_t pwd_len = utf8_char_count(password);
 * if (pwd_len == SIZE_MAX) {
 *     log_error("Password contains invalid UTF-8");
 *     return false;
 * }
 * if (pwd_len < 8) {
 *     return false; // Too short
 * }
 * @endcode
 *
 * @ingroup util
 */
size_t utf8_char_count(const char *str);

/**
 * @brief Convert UTF-8 string to array of Unicode codepoints
 * @param str UTF-8 string to convert (must not be NULL)
 * @param out_codepoints Output array for decoded codepoints (must not be NULL)
 * @param max_codepoints Maximum number of codepoints to decode
 * @return Number of codepoints decoded, or SIZE_MAX if invalid UTF-8 detected
 *
 * Decodes a UTF-8 string into an array of Unicode codepoints (32-bit values).
 * Stops when reaching end of string, hitting max_codepoints limit, or detecting
 * invalid UTF-8.
 *
 * Returns SIZE_MAX if invalid UTF-8 sequences are encountered during decoding.
 *
 * @note Returns 0 if str is NULL, out_codepoints is NULL, or max_codepoints is 0
 * @note Used by fuzzy matching (Levenshtein), Unicode algorithms, and validation
 * @note More efficient than calling utf8_decode multiple times in a loop
 *
 * @par Example
 * @code
 * // Compare two strings at codepoint level
 * uint32_t str1_codepoints[256];
 * uint32_t str2_codepoints[256];
 *
 * size_t count1 = utf8_to_codepoints("café", str1_codepoints, 256);
 * size_t count2 = utf8_to_codepoints("cafe", str2_codepoints, 256);
 *
 * if (count1 == SIZE_MAX || count2 == SIZE_MAX) {
 *     return false; // Invalid UTF-8
 * }
 *
 * // Now compare at codepoint level instead of bytes
 * for (size_t i = 0; i < count1 && i < count2; i++) {
 *     if (str1_codepoints[i] != str2_codepoints[i]) {
 *         // Different codepoint
 *     }
 * }
 * @endcode
 *
 * @ingroup util
 */
size_t utf8_to_codepoints(const char *str, uint32_t *out_codepoints, size_t max_codepoints);

/**
 * @brief Get byte length of next UTF-8 character
 * @param str Pointer to UTF-8 byte sequence (must not be NULL)
 * @param max_bytes Maximum bytes to read from str
 * @return Number of bytes in next character (1-4), or -1 if invalid UTF-8
 *
 * Determines how many bytes are needed for the next complete UTF-8 character.
 * Uses utf8proc for robust UTF-8 handling.
 *
 * Useful for interactive text input where you need to handle multi-byte
 * UTF-8 characters character-by-character (e.g., password prompts).
 *
 * @note Returns -1 for invalid UTF-8 sequences or if end of string reached
 * @note Requires utf8proc library which is already a project dependency
 *
 * @par Example
 * @code
 * // Handle multi-byte UTF-8 in password input
 * const char *input = "café";
 * int bytes_in_char = utf8_next_char_bytes(input, strlen(input));
 * // Returns 1 for 'c', 1 for 'a', 1 for 'f', 2 for 'é'
 * @endcode
 *
 * @ingroup util
 */
int utf8_next_char_bytes(const char *str, size_t max_bytes);

/**
 * @brief Determine how many additional bytes are needed to complete a UTF-8 character
 * @param first_byte The first byte of a potential UTF-8 sequence
 * @return Number of continuation bytes needed (0-3), or -1 if invalid start byte
 *
 * Given the first byte of a UTF-8 sequence, determines how many additional
 * bytes are needed to complete the character. Returns 0 for ASCII (1-byte chars),
 * 1 for 2-byte sequences, 2 for 3-byte sequences, 3 for 4-byte sequences.
 *
 * Useful in interactive input loops where bytes arrive one at a time and
 * you need to know when a complete UTF-8 character has been received.
 *
 * @par Example
 * @code
 * // Read multi-byte UTF-8 character interactively
 * unsigned char first_byte = getchar();
 * int continuation_bytes = utf8_continuation_bytes_needed(first_byte);
 * if (continuation_bytes < 0) {
 *     // Invalid start byte
 *     continue;
 * }
 * // Read continuation_bytes more bytes
 * for (int i = 0; i < continuation_bytes; i++) {
 *     unsigned char next_byte = getchar();
 *     // Validate it's a continuation byte (10xxxxxx)
 *     if ((next_byte & 0xC0) != 0x80) {
 *         // Invalid continuation byte
 *         break;
 *     }
 * }
 * @endcode
 *
 * @ingroup util
 */
int utf8_continuation_bytes_needed(unsigned char first_byte);

/**
 * @brief Read continuation bytes and insert them into buffer at cursor position
 * @param buffer Text buffer to insert bytes into
 * @param cursor Current cursor position (modified to reflect new position)
 * @param len Current buffer length (modified to reflect new length)
 * @param max_len Maximum buffer size
 * @param continuation_bytes Number of continuation bytes to read
 * @param read_byte_fn Callback function to read next byte (should return int like getchar)
 * @return 0 on success, -1 if EOF or buffer overflow
 *
 * Reads continuation_bytes bytes from input using the provided callback function,
 * shifts existing buffer content right, and inserts each byte at the cursor position.
 * Updates cursor and len to reflect the new state.
 *
 * Useful for handling multi-byte UTF-8 input in interactive prompts where bytes
 * arrive one at a time (password input, terminal prompts, etc).
 *
 * @par Example
 * @code
 * // In interactive password input
 * int continuation_bytes = utf8_continuation_bytes_needed((unsigned char)first_byte);
 * if (continuation_bytes > 0) {
 *     int result = utf8_read_and_insert_continuation_bytes(
 *         buffer, &cursor, &len, max_len, continuation_bytes, getchar);
 *     if (result < 0) {
 *         // Handle EOF or overflow
 *     }
 * }
 * @endcode
 *
 * @ingroup util
 */
int utf8_read_and_insert_continuation_bytes(char *buffer, size_t *cursor, size_t *len, size_t max_len,
                                            int continuation_bytes, int (*read_byte_fn)(void));

/* ============================================================================
 * UTF-8 String Search Functions
 * ========================================================================== */

/**
 * @brief Case-insensitive substring search with full Unicode support
 * @param haystack String to search in (must not be NULL)
 * @param needle Substring to search for (must not be NULL)
 * @return Pointer to first occurrence of needle in haystack, or NULL if not found
 *
 * Performs case-insensitive substring search using Unicode case folding.
 * Properly handles all Unicode characters including accented letters, Greek,
 * Cyrillic, CJK, emoji, and other scripts.
 *
 * Uses utf8proc library for accurate Unicode case folding according to Unicode
 * standard. This ensures correct matching for:
 * - ASCII: "TEST" matches "test"
 * - Accented: "CAFÉ" matches "café"
 * - Greek: "ΕΛΛΗΝΙΚΆ" matches "ελληνικά"
 * - Cyrillic: "РУССКИЙ" matches "русский"
 *
 * @note Returns NULL if needle is empty string
 * @note Returns haystack if needle is empty
 * @note Case folding follows Unicode standard (more complex than simple toupper/tolower)
 * @note Uses utf8proc for accurate Unicode handling
 *
 * @par Example
 * @code
 * const char *found = utf8_strcasestr("Hello WORLD", "world");
 * // Returns pointer to "WORLD" in the haystack
 *
 * const char *found2 = utf8_strcasestr("Café français", "FRANÇAIS");
 * // Returns pointer to "français" (case-insensitive + accents)
 * @endcode
 *
 * @ingroup util
 */
const char *utf8_strcasestr(const char *haystack, const char *needle);
