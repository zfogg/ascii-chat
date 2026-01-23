/**
 * @file discovery/strings.c
 * @brief Session string generation and validation implementation
 *
 * This module generates memorable session strings in the format:
 * adjective-noun-noun (e.g., "swift-river-mountain")
 *
 * It maintains cached hashtables of adjectives and nouns for fast O(1)
 * validation, ensuring session strings only contain real words.
 */

#include "discovery/strings.h"
#include "discovery/adjectives.h"
#include "discovery/nouns.h"
#include "log/logging.h"
#include "util/utf8.h"
#include "common.h"
#include "uthash/uthash.h"
#include <sodium.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

// ============================================================================
// Word Cache Implementation (Hashtable for O(1) validation)
// ============================================================================

/**
 * @brief Hashtable entry for cached word lookups
 */
typedef struct {
  char *word;        ///< The word string (key)
  UT_hash_handle hh; ///< uthash handle
} word_cache_entry_t;

/**
 * @brief Global hashtables for caching adjectives and nouns
 */
static word_cache_entry_t *g_adjectives_cache = NULL;
static word_cache_entry_t *g_nouns_cache = NULL;
static bool g_cache_initialized = false;

/**
 * @brief Cleanup function called on program exit
 */
static void acds_strings_cleanup(void) {
  if (!g_cache_initialized) {
    return;
  }

  // Cleanup adjectives cache
  word_cache_entry_t *adj_entry, *adj_tmp;
  HASH_ITER(hh, g_adjectives_cache, adj_entry, adj_tmp) {
    HASH_DEL(g_adjectives_cache, adj_entry);
    SAFE_FREE(adj_entry->word);
    SAFE_FREE(adj_entry);
  }
  g_adjectives_cache = NULL;

  // Cleanup nouns cache
  word_cache_entry_t *noun_entry, *noun_tmp;
  HASH_ITER(hh, g_nouns_cache, noun_entry, noun_tmp) {
    HASH_DEL(g_nouns_cache, noun_entry);
    SAFE_FREE(noun_entry->word);
    SAFE_FREE(noun_entry);
  }
  g_nouns_cache = NULL;

  g_cache_initialized = false;
  log_debug("Session string word cache cleaned up");
}

asciichat_error_t acds_string_init(void) {
  // libsodium's randombytes is already initialized by sodium_init()
  // which should be called at program startup
  if (sodium_init() < 0) {
    return SET_ERRNO(ERROR_CRYPTO_INIT, "Failed to initialize libsodium");
  }

  // Only initialize cache once
  if (g_cache_initialized) {
    return ASCIICHAT_OK;
  }

  // Build adjectives cache
  for (size_t i = 0; i < adjectives_count; i++) {
    word_cache_entry_t *entry = SAFE_MALLOC(sizeof(word_cache_entry_t), word_cache_entry_t *);
    if (!entry) {
      acds_strings_cleanup();
      return SET_ERRNO(ERROR_MEMORY, "Failed to allocate adjectives cache entry");
    }

    entry->word = strdup(adjectives[i]);
    if (!entry->word) {
      SAFE_FREE(entry);
      acds_strings_cleanup();
      return SET_ERRNO(ERROR_MEMORY, "Failed to duplicate adjective word");
    }

    HASH_ADD_KEYPTR(hh, g_adjectives_cache, entry->word, strlen(entry->word), entry);
  }

  // Build nouns cache
  for (size_t i = 0; i < nouns_count; i++) {
    word_cache_entry_t *entry = SAFE_MALLOC(sizeof(word_cache_entry_t), word_cache_entry_t *);
    if (!entry) {
      acds_strings_cleanup();
      return SET_ERRNO(ERROR_MEMORY, "Failed to allocate nouns cache entry");
    }

    entry->word = strdup(nouns[i]);
    if (!entry->word) {
      SAFE_FREE(entry);
      acds_strings_cleanup();
      return SET_ERRNO(ERROR_MEMORY, "Failed to duplicate noun word");
    }

    HASH_ADD_KEYPTR(hh, g_nouns_cache, entry->word, strlen(entry->word), entry);
  }

  g_cache_initialized = true;

  // Register cleanup function to run on program exit
  if (atexit(acds_strings_cleanup) != 0) {
    log_warn("Failed to register atexit handler for session string cache cleanup");
  }

  log_debug("Session string word cache initialized (%zu adjectives, %zu nouns)", adjectives_count, nouns_count);
  return ASCIICHAT_OK;
}

asciichat_error_t acds_string_generate(char *output, size_t output_size) {
  if (!output || output_size < SESSION_STRING_BUFFER_SIZE) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "output buffer must be at least %zu bytes",
                     (size_t)SESSION_STRING_BUFFER_SIZE);
  }

  // Ensure libsodium is initialized before using randombytes_uniform
  // sodium_init() is idempotent - safe to call multiple times
  if (sodium_init() < 0) {
    return SET_ERRNO(ERROR_CRYPTO_INIT, "Failed to initialize libsodium");
  }

  // Pick random adjective
  uint32_t adj_idx = randombytes_uniform((uint32_t)adjectives_count);
  const char *adj = adjectives[adj_idx];

  // Pick two random nouns
  uint32_t noun1_idx = randombytes_uniform((uint32_t)nouns_count);
  uint32_t noun2_idx = randombytes_uniform((uint32_t)nouns_count);
  const char *noun1 = nouns[noun1_idx];
  const char *noun2 = nouns[noun2_idx];

  // Format: adjective-noun-noun
  int written = snprintf(output, output_size, "%s-%s-%s", adj, noun1, noun2);
  if (written < 0 || (size_t)written >= output_size) {
    return SET_ERRNO(ERROR_BUFFER_OVERFLOW, "Session string too long for buffer");
  }

  log_debug("Generated session string: %s", output);
  return ASCIICHAT_OK;
}

bool acds_string_validate(const char *str) {
  if (!str) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Session string is NULL");
    return false;
  }

  size_t len = strlen(str);
  if (len == 0 || len > 47) {
    return false;
  }

  // Session strings must be ASCII-only (homograph attack prevention)
  // Example: Cyrillic "а" (U+0430) looks identical to ASCII "a" but is a different character
  if (!utf8_is_ascii_only(str)) {
    return false;
  }

  // Must not start or end with hyphen
  if (str[0] == '-' || str[len - 1] == '-') {
    return false;
  }

  // Count hyphens and validate characters
  int hyphen_count = 0;
  for (size_t i = 0; i < len; i++) {
    char c = str[i];
    if (c == '-') {
      hyphen_count++;
      // No consecutive hyphens
      if (i > 0 && str[i - 1] == '-') {
        return false;
      }
    } else if (!islower((unsigned char)c)) {
      // Only lowercase letters and hyphens allowed
      return false;
    }
  }

  // Must have exactly 2 hyphens (3 words)
  return hyphen_count == 2;
}

bool is_session_string(const char *str) {
  if (!str || str[0] == '\0') {
    SET_ERRNO(ERROR_INVALID_PARAM, "Session string is NULL or empty");
    return false;
  }

  // Lazy initialization: initialize cache on first use
  if (!g_cache_initialized) {
    asciichat_error_t init_err = acds_string_init();
    if (init_err != ASCIICHAT_OK) {
      log_warn("Failed to initialize session string cache; using format-only validation");
      // Fall back to format validation
      bool valid = acds_string_validate(str);
      if (!valid) {
        SET_ERRNO(ERROR_INVALID_PARAM, "Session string has invalid format");
      }
      return valid;
    }
  }

  size_t len = strlen(str);

  // Check length bounds
  if (len < 5 || len > 47) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Session string length %zu outside valid range 5-47", len);
    return false;
  }

  // Must not start or end with hyphen
  if (str[0] == '-' || str[len - 1] == '-') {
    SET_ERRNO(ERROR_INVALID_PARAM, "Session string must not start or end with hyphen");
    return false;
  }

  // Session strings must be ASCII-only (homograph attack prevention)
  if (!utf8_is_ascii_only(str)) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Session string contains non-ASCII characters");
    return false;
  }

  // Parse the string into words
  char buffer[48];
  SAFE_STRNCPY(buffer, str, sizeof(buffer));

  // Split by hyphens
  char *words[3] = {NULL, NULL, NULL};
  int word_count = 0;
  char *word = strtok(buffer, "-");

  while (word != NULL && word_count < 3) {
    if (strlen(word) == 0) {
      // Empty word (consecutive hyphens)
      SET_ERRNO(ERROR_INVALID_PARAM, "Session string contains consecutive hyphens");
      return false;
    }

    // Validate each character is lowercase letter
    for (const char *p = word; *p != '\0'; p++) {
      if (!islower((unsigned char)*p)) {
        SET_ERRNO(ERROR_INVALID_PARAM, "Session string contains non-lowercase characters");
        return false;
      }
    }

    words[word_count++] = word;
    word = strtok(NULL, "-");
  }

  // Must have exactly 3 words
  if (word_count != 3) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Session string must contain exactly 3 words, found %d", word_count);
    return false;
  }

  // Validate first word is an adjective
  word_cache_entry_t *adj_entry = NULL;
  HASH_FIND_STR(g_adjectives_cache, words[0], adj_entry);
  if (!adj_entry) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Session string first word '%s' is not a valid adjective", words[0]);
    return false;
  }

  // Validate second and third words are nouns
  word_cache_entry_t *noun_entry1 = NULL;
  HASH_FIND_STR(g_nouns_cache, words[1], noun_entry1);
  if (!noun_entry1) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Session string second word '%s' is not a valid noun", words[1]);
    return false;
  }

  word_cache_entry_t *noun_entry2 = NULL;
  HASH_FIND_STR(g_nouns_cache, words[2], noun_entry2);
  if (!noun_entry2) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Session string third word '%s' is not a valid noun", words[2]);
    return false;
  }

  log_debug("Valid session string: %s", str);
  return true;
}
