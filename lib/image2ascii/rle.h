#pragma once

/**
 * @file image2ascii/rle.h
 * @brief ANSI RLE (REP) sequence compression and expansion
 * @ingroup image2ascii
 * @addtogroup image2ascii
 * @{
 *
 * This module provides functions for working with ANSI RLE (Run-Length Encoding)
 * escape sequences. The REP sequence (CSI Ps b) repeats the preceding graphic
 * character Ps times.
 *
 * RLE FORMAT:
 * ===========
 * The ANSI REP sequence format is: ESC [ N b
 * - ESC (0x1b) - Escape character
 * - [ - CSI introducer
 * - N - Decimal repeat count (how many times to repeat)
 * - b - REP command character
 *
 * Example: "A\x1b[3b" means "A" followed by 3 more "A"s = "AAAA"
 *
 * USE CASES:
 * ==========
 * - Compression: Reduce bandwidth for ASCII art with repeated characters
 * - Expansion: Convert RLE to plain text for file output or non-RLE terminals
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date December 2025
 */

#include <stddef.h>

/**
 * @brief Expand RLE escape sequences in a string
 * @param input Input string containing ANSI escape codes
 * @param input_len Length of input string
 * @return Newly allocated string with RLE expanded (caller must free)
 *
 * Expands ANSI RLE (REP) sequences while preserving all other escape codes:
 * - RLE sequences (ESC[Nb) are expanded: previous character repeated N times
 * - All other escape sequences (colors, cursor, etc.) are preserved as-is
 *
 * This is useful when outputting to files/pipes where terminals can't
 * interpret RLE, but you still want to preserve color codes.
 *
 * @note The returned string must be freed by the caller using free().
 * @note Returns NULL if input is NULL or allocation fails.
 *
 * @par Example
 * @code
 * const char *frame = "A\x1b[3b\x1b[31mB\n";  // A + repeat 3 + red + B
 * char *expanded = ansi_expand_rle(frame, strlen(frame));
 * // expanded = "AAAA\x1b[31mB\n" (RLE expanded, color preserved)
 * free(expanded);
 * @endcode
 *
 * @ingroup image2ascii
 */
char *ansi_expand_rle(const char *input, size_t input_len);

/**
 * @brief Compress repeated characters using RLE escape sequences
 * @param input Input string (may contain ANSI escape codes)
 * @param input_len Length of input string
 * @return Newly allocated string with RLE compression (caller must free)
 *
 * Compresses runs of repeated printable characters using ANSI RLE sequences.
 * Only compresses when profitable (run length > RLE overhead).
 * Preserves all existing escape sequences.
 *
 * @note The returned string must be freed by the caller using free().
 * @note Returns NULL if input is NULL or allocation fails.
 * @note Only compresses runs where RLE saves bytes (typically 4+ chars).
 *
 * @par Example
 * @code
 * const char *frame = "AAAAAAA\x1b[31mBBBBB\n";
 * char *compressed = ansi_compress_rle(frame, strlen(frame));
 * // compressed = "A\x1b[6b\x1b[31mB\x1b[4b\n"
 * free(compressed);
 * @endcode
 *
 * @ingroup image2ascii
 */
char *ansi_compress_rle(const char *input, size_t input_len);

/** @} */
