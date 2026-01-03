/**
 * @file acds/strings.c
 * @brief Session string generation implementation
 */

#include "acds/strings.h"
#include "log/logging.h"
#include <sodium.h>
#include <string.h>
#include <ctype.h>

// Embedded wordlists (minimal for now - ~100 words each)
// Future: Load from wordlists/adjectives.txt and wordlists/nouns.txt

static const char *adjectives[] = {
    "swift",  "quiet",    "bright",   "gentle",  "bold",     "calm",   "dark",     "free",    "golden",  "happy",
    "icy",    "jolly",    "kind",     "lively",  "noble",    "proud",  "rapid",    "silver",  "tall",    "warm",
    "wild",   "wise",     "young",    "brave",   "clever",   "eager",  "fair",     "great",   "huge",    "just",
    "keen",   "lucky",    "mild",     "neat",    "open",     "pure",   "quick",    "red",     "safe",    "true",
    "vast",   "white",    "yellow",   "zealous", "amber",    "blue",   "cool",     "deep",    "easy",    "fast",
    "good",   "high",     "jade",     "long",    "new",      "old",    "pink",     "rich",    "slow",    "thin",
    "vivid",  "wide",     "zenithed", "assured", "clear",    "divine", "ethereal", "firm",    "grand",   "honest",
    "iron",   "jade",     "keen",     "loyal",   "mellow",   "noble",  "open",     "prime",   "quiet",   "radiant",
    "serene", "tranquil", "unique",   "vibrant", "warm",     "xenial", "youthful", "zestful", "agile",   "brilliant",
    "crisp",  "deft",     "elegant",  "fluid",   "graceful", "humble", "intense",  "jovial",  "kinetic", "lucid",
    "mystic", "nimble",   "ornate",   "placid"};

static const char *nouns[] = {
    "river",    "mountain", "forest",   "ocean",     "valley",     "peak",      "lake",     "hill",      "meadow",
    "canyon",   "delta",    "ridge",    "cliff",     "shore",      "stream",    "bay",      "cove",      "dune",
    "field",    "grove",    "isle",     "marsh",     "plain",      "reef",      "stone",    "trail",     "vista",
    "wave",     "aurora",   "beacon",   "cloud",     "dawn",       "ember",     "flame",    "glow",      "horizon",
    "island",   "jungle",   "moon",     "nebula",    "oasis",      "planet",    "quasar",   "star",      "thunder",
    "universe", "volcano",  "wind",     "crystal",   "diamond",    "echo",      "frost",    "glacier",   "harbor",
    "iceberg",  "jade",     "keystone", "lagoon",    "mesa",       "nexus",     "orbit",    "prism",     "quartz",
    "reef",     "summit",   "temple",   "umbra",     "vertex",     "waterfall", "xenolith", "zenith",    "abyss",
    "bridge",   "castle",   "dome",     "echo",      "fountain",   "garden",    "haven",    "inlet",     "mesa",
    "obelisk",  "portal",   "quarry",   "rapids",    "sanctuary",  "tower",     "vault",    "whirlpool", "asylum",
    "bastion",  "citadel",  "fortress", "sanctuary", "stronghold", "threshold"};

static const size_t adjectives_count = sizeof(adjectives) / sizeof(adjectives[0]);
static const size_t nouns_count = sizeof(nouns) / sizeof(nouns[0]);

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
    } else if (!islower(c)) {
      // Only lowercase letters and hyphens allowed
      return false;
    }
  }

  // Must have exactly 2 hyphens (3 words)
  return hyphen_count == 2;
}
