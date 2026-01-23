#pragma once

/**
 * @file acds/strings.h
 * @brief Session string generation and validation for discovery service
 *
 * Generates and validates memorable session strings in the format: adjective-noun-noun
 * Example: "swift-river-mountain", "quiet-forest-peak"
 *
 * Provides both generation (acds_string_generate) and validation (is_session_string).
 * Validation is hashtable-backed for O(1) lookup against real wordlists.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "common.h"

/**
 * @brief Generate random session string
 * @param output Output buffer for session string
 * @param output_size Size of output buffer (should be at least 48 bytes)
 * @return ASCIICHAT_OK on success, error code otherwise
 *
 * Generates strings in format: adjective-noun-noun
 * Example: "swift-river-mountain" (20 characters)
 *
 * Entropy: ~100 adjectives * 100 nouns * 100 nouns = 1 million combinations
 * (Full wordlist version will have ~10 million combinations)
 */
asciichat_error_t acds_string_generate(char *output, size_t output_size);

/**
 * @brief Validate session string format
 * @param str Session string to validate
 * @return true if valid format, false otherwise
 *
 * Valid format:
 * - Lowercase letters only
 * - Exactly 2 hyphens (3 words)
 * - No leading/trailing hyphens
 * - Length <= 47 characters
 */
bool acds_string_validate(const char *str);

/**
 * @brief Initialize session string system with cached word validation
 * @return ASCIICHAT_OK on success, error code otherwise
 *
 * Initializes the random number generator and caches adjectives/nouns
 * in a hashtable for fast validation. Must be called before acds_string_generate()
 * and before any is_session_string() validation calls.
 *
 * Uses libsodium's randombytes for cryptographically secure randomness.
 * Cleans up cached data on program exit via atexit() handler.
 */
asciichat_error_t acds_string_init(void);

/**
 * @brief Check if a string is a valid session string
 * @param str String to validate
 * @return true if valid session string (adjective-noun-noun format), false otherwise
 *
 * A valid session string must:
 * - Be in format: adjective-noun-noun
 * - Contain exactly 3 words separated by exactly 2 hyphens
 * - Have each word be a real adjective or noun from cached wordlists
 * - Contain only lowercase ASCII letters and hyphens
 * - Be 5-47 characters long
 * - Not start or end with hyphen
 * - Not contain consecutive hyphens
 *
 * This function validates against the cached wordlists populated by acds_string_init().
 * Set errno via SET_ERRNO() on failure for detailed error context.
 */
bool is_session_string(const char *str);
