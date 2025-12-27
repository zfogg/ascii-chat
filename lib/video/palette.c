
/**
 * @file palette.c
 * @ingroup palette
 * @brief 🎨 Terminal color palette management with Unicode character width detection
 */

#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <wchar.h>
#include <locale.h>
#include <stdlib.h>
#ifndef _WIN32
#include <langinfo.h>
#include <unistd.h>
#else
// Windows compatibility - wcwidth is not available
static int wcwidth(wchar_t wc) {
  // Simple implementation for Windows - most characters are width 1
  // Wide characters (CJK) are width 2, control characters are -1
  if (wc < 32)
    return -1; // Control characters
  if (wc >= 0x1100 && wc <= 0x115F)
    return 2; // Hangul Jamo
  if (wc >= 0x2E80 && wc <= 0x9FFF)
    return 2; // CJK range
  if (wc >= 0xAC00 && wc <= 0xD7AF)
    return 2; // Hangul Syllables
  if (wc >= 0xF900 && wc <= 0xFAFF)
    return 2; // CJK Compatibility Ideographs
  if (wc >= 0xFE10 && wc <= 0xFE19)
    return 2; // Vertical forms
  if (wc >= 0xFE30 && wc <= 0xFE6F)
    return 2; // CJK Compatibility Forms
  if (wc >= 0xFF00 && wc <= 0xFF60)
    return 2; // Fullwidth Forms
  if (wc >= 0xFFE0 && wc <= 0xFFE6)
    return 2; // Fullwidth Forms
  return 1;   // Most characters are width 1
}
#endif
#include "palette.h"
#include "common.h"
#include "platform/terminal.h"

/* Default palette constants for legacy functions */
const char DEFAULT_ASCII_PALETTE[] = PALETTE_CHARS_STANDARD;
const size_t DEFAULT_ASCII_PALETTE_LEN = 23; // strlen(PALETTE_CHARS_STANDARD)

// Built-in palette definitions
static const palette_def_t builtin_palettes[PALETTE_COUNT] = {
    [PALETTE_STANDARD] = {.name = "standard",
                          .chars = PALETTE_CHARS_STANDARD,
                          .length = 23,
                          .requires_utf8 = false,
                          .is_validated = true},
    [PALETTE_BLOCKS] =
        {.name = "blocks", .chars = PALETTE_CHARS_BLOCKS, .length = 11, .requires_utf8 = true, .is_validated = true},
    [PALETTE_DIGITAL] =
        {.name = "digital", .chars = PALETTE_CHARS_DIGITAL, .length = 10, .requires_utf8 = true, .is_validated = true},
    [PALETTE_MINIMAL] =
        {.name = "minimal", .chars = PALETTE_CHARS_MINIMAL, .length = 8, .requires_utf8 = false, .is_validated = true},
    [PALETTE_COOL] =
        {.name = "cool", .chars = PALETTE_CHARS_COOL, .length = 11, .requires_utf8 = true, .is_validated = true},
    // PALETTE_CUSTOM is handled specially - no predefined entry
};

// Get a built-in palette definition
const palette_def_t *get_builtin_palette(palette_type_t type) {
  if (type >= PALETTE_COUNT || type == PALETTE_CUSTOM) {
    return NULL;
  }
  return &builtin_palettes[type];
}

// Check if a palette string contains UTF-8 characters
bool palette_requires_utf8_encoding(const char *chars, size_t len) {
  // Handle NULL or empty string
  if (!chars || len == 0) {
    return false;
  }

  for (size_t i = 0; i < len; i++) {
    // Any byte with high bit set indicates UTF-8
    if ((unsigned char)chars[i] >= 128) {
      return true;
    }
  }
  return false;
}

// Validate UTF-8 character sequences and terminal width
bool validate_palette_chars(const char *chars, size_t len) {
  if (!chars || len == 0) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Palette validation failed: empty or NULL palette");
    return false;
  }

  if (len > 256) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Palette validation failed: palette too long (%zu chars, max 256)", len);
    return false;
  }

  // Set locale for UTF-8 support
  char *current_locale = setlocale(LC_CTYPE, NULL);
  char *old_locale = NULL;
  if (current_locale) {
    // Copy the locale string before calling setlocale again (CERT ENV30-C)
    SAFE_STRDUP(old_locale, current_locale);
  }
  if (!setlocale(LC_CTYPE, "")) {
    log_warn("Failed to set locale for UTF-8 validation, continuing anyway");
  }

  const char *p = chars;
  size_t char_count = 0;
  size_t byte_count = 0;

  while (byte_count < len && *p) {
    wchar_t wc;
    int bytes = mbtowc(&wc, p, len - byte_count);

    if (bytes <= 0) {
      SET_ERRNO(ERROR_INVALID_PARAM, "Palette validation failed: invalid UTF-8 sequence at position %zu", char_count);
      // Restore old locale
      if (old_locale) {
        (void)setlocale(LC_CTYPE, old_locale);
        SAFE_FREE(old_locale);
      }
      return false;
    }

    // Check character width - allow width 1 and 2 (for emoji and wide characters)
    int width = wcwidth(wc);
    if (width < 0 || width > 2) {
      SET_ERRNO(ERROR_INVALID_PARAM,
                "Palette validation failed: character at position %zu has invalid width %d (must be 1 or 2)",
                char_count, width);
      // Restore old locale
      if (old_locale) {
        (void)setlocale(LC_CTYPE, old_locale);
        SAFE_FREE(old_locale);
      }
      return false;
    }

    // Check for control characters (except tab)
    if (wc < 32 && wc != '\t') {
      SET_ERRNO(ERROR_INVALID_PARAM, "Palette validation failed: control character at position %zu", char_count);
      // Restore old locale
      if (old_locale) {
        (void)setlocale(LC_CTYPE, old_locale);
        SAFE_FREE(old_locale);
      }
      return false;
    }

    char_count++;
    byte_count += bytes;
    p += bytes;
  }

  // Restore old locale
  if (old_locale) {
    (void)setlocale(LC_CTYPE, old_locale);
    SAFE_FREE(old_locale);
  }

  log_debug("Palette validation successful: %zu characters validated", char_count);
  return true;
}

// Detect client UTF-8 support from environment
bool detect_client_utf8_support(utf8_capabilities_t *caps) {
  if (!caps) {
    return false;
  }

  // Initialize structure
  SAFE_MEMSET(caps, sizeof(utf8_capabilities_t), 0, sizeof(utf8_capabilities_t));

  // Check environment variables
  const char *term = SAFE_GETENV("TERM");

  // Store terminal type
  if (term) {
    SAFE_STRNCPY(caps->terminal_type, term, sizeof(caps->terminal_type));
  }

  // Use platform-specific UTF-8 detection from platform abstraction layer
  caps->utf8_support = terminal_supports_utf8();

  if (caps->utf8_support) {
    SAFE_STRNCPY(caps->locale_encoding, "UTF-8", sizeof(caps->locale_encoding));
  } else {
    // Try to detect encoding via locale
    char *current_locale = setlocale(LC_CTYPE, NULL);
    char *old_locale = NULL;
    if (current_locale) {
      SAFE_STRDUP(old_locale, current_locale);
    }
    if (setlocale(LC_CTYPE, "")) {
#ifndef _WIN32
      const char *codeset = nl_langinfo(CODESET);
      if (codeset) {
        SAFE_STRNCPY(caps->locale_encoding, codeset, sizeof(caps->locale_encoding));
      }
#else
      // Windows may not have locale set but still support UTF-8
      SAFE_STRNCPY(caps->locale_encoding, "CP1252", sizeof(caps->locale_encoding));
#endif
      // Restore old locale
      if (old_locale) {
        (void)setlocale(LC_CTYPE, old_locale);
      }
    }
    // Always free old_locale regardless of setlocale success
    if (old_locale) {
      SAFE_FREE(old_locale);
    }
  }

  // Check for known UTF-8 supporting terminals
  if (term) {
    const char *utf8_terminals[] = {
        "xterm-256color", "screen-256color", "tmux-256color", "alacritty",   "kitty", "iterm",
        "iterm2",         "gnome-terminal",  "konsole",       "terminology", NULL};

    for (int i = 0; utf8_terminals[i]; i++) {
      if (strstr(term, utf8_terminals[i])) {
        caps->utf8_support = true;
        break;
      }
    }
  }

  log_debug("UTF-8 support detection: %s (term=%s, encoding=%s)", caps->utf8_support ? "supported" : "not supported",
            caps->terminal_type[0] ? caps->terminal_type : "unknown",
            caps->locale_encoding[0] ? caps->locale_encoding : "unknown");

  return caps->utf8_support;
}

// Select a compatible palette based on client capabilities
palette_type_t select_compatible_palette(palette_type_t requested, bool client_utf8) {
  const palette_def_t *palette = get_builtin_palette(requested);

  // Custom palettes are validated separately
  if (requested == PALETTE_CUSTOM) {
    return PALETTE_CUSTOM;
  }

  if (!palette) {
    log_warn("Invalid palette type %d, falling back to standard", requested);
    return PALETTE_STANDARD;
  }

  // If palette requires UTF-8 but client doesn't support it, find fallback
  if (palette->requires_utf8 && !client_utf8) {
    log_warn("Client doesn't support UTF-8, falling back from %s", palette->name);

    // Fallback hierarchy
    switch (requested) {
    case PALETTE_BLOCKS:
    case PALETTE_DIGITAL:
    case PALETTE_COOL:
    default:
      return PALETTE_STANDARD; // ASCII equivalent
    }
  }

  return requested; // Compatible, use as requested
}

// Apply palette configuration for client-side initialization only (no global cache)
int apply_palette_config(palette_type_t type, const char *custom_chars) {
  // This function is now used only for client-side initialization
  // Server uses initialize_client_palette() for per-client palettes

  log_info("Client palette config: type=%d, custom_chars=%s", type, custom_chars ? custom_chars : "(none)");

  // Just validate the palette - no global state changes
  if (type == PALETTE_CUSTOM) {
    if (!custom_chars || strlen(custom_chars) == 0) {
      SET_ERRNO(ERROR_INVALID_PARAM, "Custom palette requested but no characters provided");
      return -1;
    }

    if (!validate_palette_chars(custom_chars, strlen(custom_chars))) {
      SET_ERRNO(ERROR_INVALID_PARAM, "Custom palette validation failed");
      return -1;
    }
  } else {
    const palette_def_t *palette = get_builtin_palette(type);
    if (!palette) {
      SET_ERRNO(ERROR_INVALID_PARAM, "Invalid palette type: %d", type);
      return -1;
    }
  }

  return 0; // Validation successful, no global state to update
}

// Build a per-client luminance palette without affecting global cache
int build_client_luminance_palette(const char *palette_chars, size_t palette_len, char luminance_mapping[256]) {
  if (!palette_chars || palette_len == 0 || !luminance_mapping) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters for client luminance palette");
    return -1;
  }

  // Map 256 luminance values to palette indices for this specific client
  for (int i = 0; i < 256; i++) {
    // Linear mapping with proper rounding
    size_t palette_index = (i * (palette_len - 1) + 127) / 255;
    if (palette_index >= palette_len) {
      palette_index = palette_len - 1;
    }
    luminance_mapping[i] = palette_chars[palette_index];
  }

  return 0;
}

// Initialize a client's palette cache from their capabilities
int initialize_client_palette(palette_type_t palette_type, const char *custom_chars, char client_palette_chars[256],
                              size_t *client_palette_len, char client_luminance_palette[256]) {
  const palette_def_t *palette = NULL;
  const char *chars_to_use = NULL;
  size_t len_to_use = 0;

  if (palette_type == PALETTE_CUSTOM) {
    if (!custom_chars) {
      SET_ERRNO(ERROR_INVALID_PARAM, "Client requested custom palette but custom_chars is NULL");
      return -1;
    }

    len_to_use = strlen(custom_chars);
    if (len_to_use == 0) {
      SET_ERRNO(ERROR_INVALID_PARAM, "Client requested custom palette but custom_chars is empty");
      return -1;
    }
    if (len_to_use >= 256) {
      SET_ERRNO(ERROR_INVALID_PARAM, "Client custom palette too long: %zu chars", len_to_use);
      return -1;
    }

    // Validate custom palette
    if (!validate_palette_chars(custom_chars, len_to_use)) {
      SET_ERRNO(ERROR_INVALID_PARAM, "Client custom palette validation failed");
      return -1;
    }

    chars_to_use = custom_chars;
  } else {
    palette = get_builtin_palette(palette_type);
    if (!palette) {
      SET_ERRNO(ERROR_INVALID_PARAM, "Invalid client palette type: %d", palette_type);
      return -1;
    }

    chars_to_use = palette->chars;
    len_to_use = strlen(palette->chars); // Use actual byte count for UTF-8, not character count

    // Skip validation for built-in palettes since they're already validated
    log_debug("Using built-in palette: %s, chars='%s', char_count=%zu, byte_len=%zu", palette->name, chars_to_use,
              palette->length, len_to_use);
  }

  // Copy palette to client cache
  SAFE_MEMCPY(client_palette_chars, len_to_use, chars_to_use, len_to_use);
  client_palette_chars[len_to_use] = '\0';
  *client_palette_len = len_to_use;

  // Build client-specific luminance mapping
  if (build_client_luminance_palette(chars_to_use, len_to_use, client_luminance_palette) != 0) {
    SET_ERRNO(ERROR_INVALID_STATE, "Failed to build client luminance palette");
    return -1;
  }

  log_info("Initialized client palette: type=%d, %zu chars, first_char='%c', last_char='%c'", palette_type, len_to_use,
           chars_to_use[0], chars_to_use[len_to_use - 1]);

  return 0;
}

/* UTF-8 Palette Functions Implementation */

// Create a UTF-8 palette structure from a string
utf8_palette_t *utf8_palette_create(const char *palette_string) {
  if (!palette_string || *palette_string == '\0') {
    return NULL;
  }

  utf8_palette_t *palette;
  palette = SAFE_MALLOC(sizeof(utf8_palette_t), utf8_palette_t *);

  // Count UTF-8 characters (not bytes)
  size_t char_count = 0;
  const char *p = palette_string;
  size_t total_bytes = strlen(palette_string);

  // First pass: count characters
  size_t bytes_processed = 0;
  while (bytes_processed < total_bytes) {
    int bytes = 1;
    unsigned char c = (unsigned char)p[0];

    if ((c & 0x80) == 0) {
      bytes = 1; // ASCII
    } else if ((c & 0xE0) == 0xC0) {
      bytes = 2; // 2-byte UTF-8
    } else if ((c & 0xF0) == 0xE0) {
      bytes = 3; // 3-byte UTF-8
    } else if ((c & 0xF8) == 0xF0) {
      bytes = 4; // 4-byte UTF-8
    }

    // Verify we have enough bytes left
    if (bytes_processed + bytes > total_bytes) {
      break;
    }

    p += bytes;
    bytes_processed += bytes;
    char_count++;
  }

  // Validate we got at least one character
  if (char_count == 0) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Palette string contains no valid UTF-8 characters");
    SAFE_FREE(palette);
    return NULL;
  }

  // Allocate character array
  palette->chars = SAFE_MALLOC(char_count * sizeof(utf8_char_info_t), utf8_char_info_t *);
  palette->raw_string = SAFE_MALLOC(total_bytes + 1, char *);
  // Explicit NULL checks to satisfy static analyzer (SAFE_MALLOC calls FATAL on failure)
  if (palette->chars == NULL || palette->raw_string == NULL) {
    SAFE_FREE(palette->chars);
    SAFE_FREE(palette->raw_string);
    SAFE_FREE(palette);
    SET_ERRNO(ERROR_MEMORY, "Failed to allocate palette character array");
    return NULL;
  }

  SAFE_MEMCPY(palette->raw_string, total_bytes + 1, palette_string, total_bytes + 1);
  palette->char_count = char_count;
  palette->total_bytes = total_bytes; // Use strlen() value

  // Second pass: parse characters
  p = palette_string;
  size_t char_idx = 0;
  bytes_processed = 0;

  // Set locale for wcwidth - save a copy of the old locale
  char old_locale[256] = {0};
  char *current_locale = setlocale(LC_CTYPE, NULL);
  if (current_locale) {
    SAFE_STRNCPY(old_locale, current_locale, sizeof(old_locale));
  }
  (void)setlocale(LC_CTYPE, "");

  while (char_idx < char_count && bytes_processed < total_bytes) {
    utf8_char_info_t *char_info = &palette->chars[char_idx];

    // Determine UTF-8 byte length
    unsigned char c = (unsigned char)*p;
    int bytes = 1;

    if ((c & 0x80) == 0) {
      bytes = 1;
    } else if ((c & 0xE0) == 0xC0) {
      bytes = 2;
    } else if ((c & 0xF0) == 0xE0) {
      bytes = 3;
    } else if ((c & 0xF8) == 0xF0) {
      bytes = 4;
    }

    // Verify we have enough bytes left
    if (bytes_processed + bytes > total_bytes) {
      break;
    }

    // Copy bytes and null-terminate
    SAFE_MEMCPY(char_info->bytes, bytes, p, bytes);
    if (bytes < 4) {
      SAFE_MEMSET(char_info->bytes + bytes, 4 - bytes, 0, 4 - bytes);
    }
    char_info->byte_len = bytes;

    // Get display width
    wchar_t wc;
    if (mbtowc(&wc, p, bytes) > 0) {
      int width = wcwidth(wc);
      char_info->display_width = (width > 0 && width <= 2) ? width : 1;
    } else {
      char_info->display_width = 1;
    }

    p += bytes;
    bytes_processed += bytes;
    char_idx++;
  }

  // Restore locale
  if (old_locale[0] != '\0') {
    (void)setlocale(LC_CTYPE, old_locale);
  }

  return palette;
}

// Destroy a UTF-8 palette structure
void utf8_palette_destroy(utf8_palette_t *palette) {
  if (palette) {
    SAFE_FREE(palette->chars);
    SAFE_FREE(palette->raw_string);
    SAFE_FREE(palette);
  }
}

// Get the nth character from the palette
const utf8_char_info_t *utf8_palette_get_char(const utf8_palette_t *palette, size_t index) {
  if (!palette || index >= palette->char_count) {
    return NULL;
  }
  return &palette->chars[index];
}

// Get the number of characters in the palette
size_t utf8_palette_get_char_count(const utf8_palette_t *palette) {
  if (!palette) {
    return 0;
  }
  return palette->char_count;
}

// Check if palette contains a specific UTF-8 character
bool utf8_palette_contains_char(const utf8_palette_t *palette, const char *utf8_char, size_t char_bytes) {
  if (!palette || !utf8_char || char_bytes == 0 || char_bytes > 4) {
    return false;
  }

  for (size_t i = 0; i < palette->char_count; i++) {
    const utf8_char_info_t *char_info = &palette->chars[i];
    if (char_info->byte_len == char_bytes && memcmp(char_info->bytes, utf8_char, char_bytes) == 0) {
      return true;
    }
  }

  return false;
}

// Find the index of a UTF-8 character in the palette
size_t utf8_palette_find_char_index(const utf8_palette_t *palette, const char *utf8_char, size_t char_bytes) {
  if (!palette || !utf8_char || char_bytes == 0 || char_bytes > 4) {
    return (size_t)-1;
  }

  for (size_t i = 0; i < palette->char_count; i++) {
    const utf8_char_info_t *char_info = &palette->chars[i];
    if (char_info->byte_len == char_bytes && memcmp(char_info->bytes, utf8_char, char_bytes) == 0) {
      return i;
    }
  }

  return (size_t)-1;
}

// Find all indices of a UTF-8 character in the palette (handles duplicates)
// Returns the number of indices found, fills indices array up to max_indices
size_t utf8_palette_find_all_char_indices(const utf8_palette_t *palette, const char *utf8_char, size_t char_bytes,
                                          size_t *indices, size_t max_indices) {
  if (!palette || !utf8_char || char_bytes == 0 || char_bytes > 4 || !indices || max_indices == 0) {
    return 0;
  }

  size_t found_count = 0;

  for (size_t i = 0; i < palette->char_count && found_count < max_indices; i++) {
    const utf8_char_info_t *char_info = &palette->chars[i];
    if (char_info->byte_len == char_bytes && memcmp(char_info->bytes, utf8_char, char_bytes) == 0) {
      indices[found_count++] = i;
    }
  }

  return found_count;
}
