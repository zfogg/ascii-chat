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

#include <ascii-chat/discovery/strings.h>
#include <ascii-chat/discovery/adjectives.h>
#include <ascii-chat/discovery/nouns.h>
#include <ascii-chat/log/logging.h>
#include <ascii-chat/util/utf8.h>
// NOTE: Use explicit path to avoid Windows include resolution picking up options/common.h
#include <ascii-chat/common.h>
#include <ascii-chat/platform/init.h>
#include <ascii-chat/uthash/uthash.h>
#include <sodium.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <ascii-chat/util/pcre2.h>
#include <pcre2.h>

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
// Mutex to protect lazy initialization of word validation caches
static static_mutex_t g_cache_init_mutex = STATIC_MUTEX_INIT;

/**
 * @brief Cleanup function for session string cache
 * Called by asciichat_shared_shutdown() during library cleanup.
 * Safe to call multiple times (idempotent).
 */
void acds_strings_cleanup(void) {
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

/**
 * @brief Build hashtable caches for word validation (lazy initialization)
 * Called on first validation, not during init - avoids 7500+ slow HASH_ADD_KEYPTR calls
 *
 * Uses mutex to prevent multiple threads from building caches concurrently,
 * which could cause memory leaks and hashtable corruption.
 */
static asciichat_error_t build_validation_caches(void) {
  static_mutex_lock(&g_cache_init_mutex);

  // Double-check under lock: another thread may have already built while we waited
  if (g_cache_initialized) {
    static_mutex_unlock(&g_cache_init_mutex);
    return ASCIICHAT_OK;
  }

  // Build adjectives cache
  for (size_t i = 0; i < adjectives_count; i++) {
    word_cache_entry_t *entry = SAFE_MALLOC(sizeof(word_cache_entry_t), word_cache_entry_t *);
    if (!entry) {
      static_mutex_unlock(&g_cache_init_mutex);
      acds_strings_cleanup();
      return SET_ERRNO(ERROR_MEMORY, "Failed to allocate adjectives cache entry");
    }

    size_t word_len = strlen(adjectives[i]) + 1;
    entry->word = SAFE_MALLOC(word_len, char *);
    if (!entry->word) {
      SAFE_FREE(entry);
      static_mutex_unlock(&g_cache_init_mutex);
      acds_strings_cleanup();
      return SET_ERRNO(ERROR_MEMORY, "Failed to allocate memory for adjective word");
    }
    strcpy(entry->word, adjectives[i]);

    HASH_ADD_KEYPTR(hh, g_adjectives_cache, entry->word, strlen(entry->word), entry);
  }

  // Build nouns cache
  for (size_t i = 0; i < nouns_count; i++) {
    word_cache_entry_t *entry = SAFE_MALLOC(sizeof(word_cache_entry_t), word_cache_entry_t *);
    if (!entry) {
      static_mutex_unlock(&g_cache_init_mutex);
      acds_strings_cleanup();
      return SET_ERRNO(ERROR_MEMORY, "Failed to allocate nouns cache entry");
    }

    size_t word_len = strlen(nouns[i]) + 1;
    entry->word = SAFE_MALLOC(word_len, char *);
    if (!entry->word) {
      SAFE_FREE(entry);
      static_mutex_unlock(&g_cache_init_mutex);
      acds_strings_cleanup();
      return SET_ERRNO(ERROR_MEMORY, "Failed to allocate memory for noun word");
    }
    strcpy(entry->word, nouns[i]);

    HASH_ADD_KEYPTR(hh, g_nouns_cache, entry->word, strlen(entry->word), entry);
  }

  g_cache_initialized = true;

  // NOTE: Cleanup is now handled by asciichat_shared_shutdown() called from application code.
  // Library code does not call atexit() - that's the application's responsibility.

  static_mutex_unlock(&g_cache_init_mutex);
  log_debug("Session string word cache initialized (%zu adjectives, %zu nouns)", adjectives_count, nouns_count);
  return ASCIICHAT_OK;
}

// ============================================================================
// PCRE2 Session String Format Validator
// ============================================================================

/**
 * @brief Session string format validator using PCRE2 singleton
 *
 * Validates the format adjective-noun-noun with regex:
 * - Each word is 2-12 lowercase letters
 * - Exactly 2 hyphens separating 3 words
 * - No leading/trailing hyphens
 * - No consecutive hyphens
 *
 * This regex validator handles FORMAT validation only.
 * Dictionary validation (adjective/noun caches) is handled separately.
 */

static const char *SESSION_STRING_FORMAT_PATTERN = "^(?<adj>[a-z]{2,12})-(?<noun1>[a-z]{2,12})-(?<noun2>[a-z]{2,12})$";

static pcre2_singleton_t *g_session_format_regex = NULL;

/**
 * Get compiled session string format regex (lazy initialization)
 * Returns NULL if compilation failed
 */
static pcre2_code *session_format_regex_get(void) {
  if (g_session_format_regex == NULL) {
    g_session_format_regex = asciichat_pcre2_singleton_compile(SESSION_STRING_FORMAT_PATTERN, PCRE2_CASELESS);
  }
  return asciichat_pcre2_singleton_get_code(g_session_format_regex);
}

asciichat_error_t acds_string_init(void) {
  // Fast initialization - only init libsodium
  // Hashtable building is deferred until actually needed for validation
  if (sodium_init() < 0) {
    return SET_ERRNO(ERROR_CRYPTO_INIT, "Failed to initialize libsodium");
  }
  return ASCIICHAT_OK;
}

asciichat_error_t acds_string_generate(char *output, size_t output_size) {
  if (!output || output_size < SESSION_STRING_BUFFER_SIZE) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "output buffer must be at least %zu bytes",
                     (size_t)SESSION_STRING_BUFFER_SIZE);
  }

  // libsodium is guaranteed to be initialized by acds_string_init() before this is called
  // No need to call sodium_init() again (redundant initialization removed)

  // Pick random adjective
  uint32_t adj_idx = randombytes_uniform((uint32_t)adjectives_count);
  const char *adj = adjectives[adj_idx];

  // Pick two random nouns
  uint32_t noun1_idx = randombytes_uniform((uint32_t)nouns_count);
  uint32_t noun2_idx = randombytes_uniform((uint32_t)nouns_count);
  const char *noun1 = nouns[noun1_idx];
  const char *noun2 = nouns[noun2_idx];

  // Format: adjective-noun-noun
  int written = safe_snprintf(output, output_size, "%s-%s-%s", adj, noun1, noun2);
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

  size_t len = strlen(str);

  // Check length bounds
  if (len < 5 || len > 47) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Session string length %zu outside valid range 5-47", len);
    return false;
  }

  // Session strings must be ASCII-only (homograph attack prevention)
  if (!utf8_is_ascii_only(str)) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Session string contains non-ASCII characters");
    return false;
  }

  // Get compiled regex (lazy initialization)
  pcre2_code *regex = session_format_regex_get();
  if (!regex) {
    log_warn("Session string validator not initialized; falling back to format validation");
    bool valid = acds_string_validate(str);
    if (!valid) {
      SET_ERRNO(ERROR_INVALID_PARAM, "Session string has invalid format");
    }
    return valid;
  }

  // Validate format using PCRE2 regex
  pcre2_match_data *match_data = pcre2_match_data_create_from_pattern(regex, NULL);
  if (!match_data) {
    log_error("Failed to allocate match data for session string regex");
    SET_ERRNO(ERROR_MEMORY, "Failed to allocate match data");
    return false;
  }

  int match_result = pcre2_jit_match(regex, (PCRE2_SPTR8)str, len, 0, 0, match_data, NULL);

  pcre2_match_data_free(match_data);

  if (match_result < 0) {
    // Format validation failed
    SET_ERRNO(ERROR_INVALID_PARAM, "Session string format does not match pattern");
    return false;
  }

  // Extract the three words by finding hyphens
  // Format is guaranteed to be: word-word-word where each word is 2-12 lowercase letters
  const char *hyphen1 = strchr(str, '-');
  if (!hyphen1) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Session string missing first hyphen");
    return false;
  }

  const char *hyphen2 = strchr(hyphen1 + 1, '-');
  if (!hyphen2) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Session string missing second hyphen");
    return false;
  }

  // Extract three words
  size_t adj_len = hyphen1 - str;
  size_t noun1_len = hyphen2 - hyphen1 - 1;
  size_t noun2_len = len - (hyphen2 - str) - 1;

  if (adj_len >= 32 || noun1_len >= 32 || noun2_len >= 32) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Session string word length out of bounds");
    return false;
  }

  char adj[32], noun1[32], noun2[32];
  memcpy(adj, str, adj_len);
  adj[adj_len] = '\0';
  memcpy(noun1, hyphen1 + 1, noun1_len);
  noun1[noun1_len] = '\0';
  memcpy(noun2, hyphen2 + 1, noun2_len);
  noun2[noun2_len] = '\0';

  // Lazy initialization: build validation caches on first use
  // Note: build_validation_caches() handles synchronization internally
  if (!g_cache_initialized) {
    asciichat_error_t cache_err = build_validation_caches();
    if (cache_err != ASCIICHAT_OK) {
      log_warn("Failed to initialize session string cache; accepting format-valid string");
      log_debug("Valid session string format (cache unavailable): %s", str);
      return true; // Format is valid, cache is unavailable, accept anyway
    }
  }

  // Validate first word is an adjective
  word_cache_entry_t *adj_entry = NULL;
  HASH_FIND_STR(g_adjectives_cache, adj, adj_entry);
  if (!adj_entry) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Session string first word '%s' is not a valid adjective", adj);
    return false;
  }

  // Validate second and third words are nouns
  word_cache_entry_t *noun_entry1 = NULL;
  HASH_FIND_STR(g_nouns_cache, noun1, noun_entry1);
  if (!noun_entry1) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Session string second word '%s' is not a valid noun", noun1);
    return false;
  }

  word_cache_entry_t *noun_entry2 = NULL;
  HASH_FIND_STR(g_nouns_cache, noun2, noun_entry2);
  if (!noun_entry2) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Session string third word '%s' is not a valid noun", noun2);
    return false;
  }

  log_debug("Valid session string: %s", str);
  return true;
}
