#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <wchar.h>
#include <locale.h>
#include <stdlib.h>
#include <langinfo.h>
#include <unistd.h>
#include "palette.h"
#include "common.h"
#include "ascii_simd.h"

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
    log_error("Palette validation failed: empty or NULL palette");
    return false;
  }

  if (len > 256) {
    log_error("Palette validation failed: palette too long (%zu chars, max 256)", len);
    return false;
  }

  // Set locale for UTF-8 support
  char *old_locale = setlocale(LC_CTYPE, NULL);
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
      log_error("Palette validation failed: invalid UTF-8 sequence at position %zu", char_count);
      // Restore old locale
      if (old_locale) {
        setlocale(LC_CTYPE, old_locale);
      }
      return false;
    }

    // Check character width - allow width 1 and 2 (for emoji and wide characters)
    int width = wcwidth(wc);
    if (width < 0 || width > 2) {
      log_error("Palette validation failed: character at position %zu has invalid width %d (must be 1 or 2)",
                char_count, width);
      // Restore old locale
      if (old_locale) {
        setlocale(LC_CTYPE, old_locale);
      }
      return false;
    }

    // Check for control characters (except space)
    if (wc < 32 && wc != ' ' && wc != '\t') {
      log_error("Palette validation failed: control character at position %zu", char_count);
      // Restore old locale
      if (old_locale) {
        setlocale(LC_CTYPE, old_locale);
      }
      return false;
    }

    char_count++;
    byte_count += bytes;
    p += bytes;
  }

  // Restore old locale
  if (old_locale) {
    setlocale(LC_CTYPE, old_locale);
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
  memset(caps, 0, sizeof(utf8_capabilities_t));

  // Check environment variables
  const char *lang = getenv("LANG");
  const char *lc_all = getenv("LC_ALL");
  const char *lc_ctype = getenv("LC_CTYPE");
  const char *term = getenv("TERM");

  // Store terminal type
  if (term) {
    SAFE_STRNCPY(caps->terminal_type, term, sizeof(caps->terminal_type));
  }

  // Check for UTF-8 in locale environment variables
  if ((lang && strstr(lang, "UTF-8")) || (lc_all && strstr(lc_all, "UTF-8")) ||
      (lc_ctype && strstr(lc_ctype, "UTF-8"))) {
    caps->utf8_support = true;
    SAFE_STRNCPY(caps->locale_encoding, "UTF-8", sizeof(caps->locale_encoding));
  } else {
    // Try system locale detection
    char *old_locale = setlocale(LC_CTYPE, NULL);
    if (setlocale(LC_CTYPE, "")) {
      const char *codeset = nl_langinfo(CODESET);
      if (codeset) {
        SAFE_STRNCPY(caps->locale_encoding, codeset, sizeof(caps->locale_encoding));
        if (strcmp(codeset, "UTF-8") == 0 || strcmp(codeset, "utf8") == 0) {
          caps->utf8_support = true;
        }
      }
      // Restore old locale
      if (old_locale) {
        setlocale(LC_CTYPE, old_locale);
      }
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
      return PALETTE_STANDARD; // ASCII equivalent
    default:
      return PALETTE_STANDARD; // Safe default
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
      log_error("Custom palette requested but no characters provided");
      return -1;
    }

    if (!validate_palette_chars(custom_chars, strlen(custom_chars))) {
      log_error("Custom palette validation failed");
      return -1;
    }
  } else {
    const palette_def_t *palette = get_builtin_palette(type);
    if (!palette) {
      log_error("Invalid palette type: %d", type);
      return -1;
    }
  }

  return 0; // Validation successful, no global state to update
}

// Build a per-client luminance palette without affecting global cache
int build_client_luminance_palette(const char *palette_chars, size_t palette_len, char luminance_mapping[256]) {
  if (!palette_chars || palette_len == 0 || !luminance_mapping) {
    log_error("Invalid parameters for client luminance palette");
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
    if (!custom_chars || strlen(custom_chars) == 0) {
      log_error("Client requested custom palette but no characters provided");
      return -1;
    }

    len_to_use = strlen(custom_chars);
    if (len_to_use >= 256) {
      log_error("Client custom palette too long: %zu chars", len_to_use);
      return -1;
    }

    // Validate custom palette
    if (!validate_palette_chars(custom_chars, len_to_use)) {
      log_error("Client custom palette validation failed");
      return -1;
    }

    chars_to_use = custom_chars;
  } else {
    palette = get_builtin_palette(palette_type);
    if (!palette) {
      log_error("Invalid client palette type: %d", palette_type);
      return -1;
    }

    chars_to_use = palette->chars;
    len_to_use = strlen(palette->chars); // Use actual byte count for UTF-8, not character count

    // Skip validation for built-in palettes since they're already validated
    log_debug("Using built-in palette: %s, chars='%s', char_count=%zu, byte_len=%zu", palette->name, chars_to_use,
              palette->length, len_to_use);
  }

  // Copy palette to client cache
  memcpy(client_palette_chars, chars_to_use, len_to_use);
  client_palette_chars[len_to_use] = '\0';
  *client_palette_len = len_to_use;

  // Build client-specific luminance mapping
  if (build_client_luminance_palette(chars_to_use, len_to_use, client_luminance_palette) != 0) {
    log_error("Failed to build client luminance palette");
    return -1;
  }

  log_info("Initialized client palette: type=%d, %zu chars, first_char='%c', last_char='%c'", palette_type, len_to_use,
           chars_to_use[0], chars_to_use[len_to_use - 1]);

  return 0;
}
