#pragma once

/**
 * @file acds/strings.h
 * @brief Session string generation for discovery service
 *
 * Generates memorable session strings in the format: adjective-noun-noun
 * Example: "swift-river-mountain", "quiet-forest-peak"
 *
 * This is a minimal inline implementation with embedded wordlists.
 * Future lib/discovery/session_string.c will load from files for larger wordlists.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "core/common.h"

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
 * @brief Initialize random number generator for string generation
 * @return ASCIICHAT_OK on success, error code otherwise
 *
 * Must be called before acds_string_generate().
 * Uses libsodium's randombytes for cryptographically secure randomness.
 */
asciichat_error_t acds_string_init(void);
