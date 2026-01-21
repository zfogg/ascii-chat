/**
 * @file discovery/strings.c
 * @brief Session string generation implementation
 */

#include "discovery/strings.h"
#include "discovery/adjectives.h"
#include "discovery/nouns.h"
#include "log/logging.h"
#include "util/utf8.h"
#include <sodium.h>
#include <string.h>
#include <ctype.h>

asciichat_error_t acds_string_init(void) {
  // libsodium's randombytes is already initialized by sodium_init()
  // which should be called at program startup
  if (sodium_init() < 0) {
    return SET_ERRNO(ERROR_CRYPTO_INIT, "Failed to initialize libsodium");
  }

  log_debug("Session string generator initialized (%zu adjectives, %zu nouns)", adjectives_count, nouns_count);
  return ASCIICHAT_OK;
}

asciichat_error_t acds_string_generate(char *output, size_t output_size) {
  if (!output || output_size < 48) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "output buffer must be at least 48 bytes");
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
