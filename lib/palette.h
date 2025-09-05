#pragma once

#include <stdbool.h>
#include <stddef.h>

/* ASCII Palette Types and Structures */
typedef enum {
  PALETTE_STANDARD = 0, // Default: "   ...',;:clodxkO0KXNWM"
  PALETTE_BLOCKS,       // Unicode blocks: "   ░░▒▒▓▓██"
  PALETTE_DIGITAL,      // Digital/glitch: "   -=≡≣▰▱◼"
  PALETTE_MINIMAL,      // Simple ASCII: "   .-+*#"
  PALETTE_COOL,         // Ascending blocks: "   ▁▂▃▄▅▆▇█"
  PALETTE_CUSTOM,       // User-defined via --palette-chars
  PALETTE_COUNT         // Number of palette types
} palette_type_t;

typedef struct {
  const char *name;   // Human-readable name
  const char *chars;  // Character sequence
  size_t length;      // Number of characters
  bool requires_utf8; // True if palette contains UTF-8 characters
  bool is_validated;  // True if palette passed validation
} palette_def_t;

/* UTF-8 Support Detection */
typedef struct {
  bool utf8_support;        // True if terminal supports UTF-8
  bool forced_utf8;         // True if user forced UTF-8 via --utf8
  char terminal_type[32];   // $TERM environment variable
  char locale_encoding[16]; // Current locale encoding
} utf8_capabilities_t;

/* Built-in Palette Character Constants */
#define PALETTE_CHARS_STANDARD "   ...',;:clodxkO0KXNWM"
#define PALETTE_CHARS_BLOCKS "   ░░▒▒▓▓██"
#define PALETTE_CHARS_DIGITAL "   -=≡≣▰▱◼"
#define PALETTE_CHARS_MINIMAL "   .-+*#"
#define PALETTE_CHARS_COOL "   ▁▂▃▄▅▆▇█"

/* Default palette for legacy functions */
extern const char DEFAULT_ASCII_PALETTE[];
extern const size_t DEFAULT_ASCII_PALETTE_LEN;

/* Palette Management Function Declarations */
const palette_def_t *get_builtin_palette(palette_type_t type);
bool validate_palette_chars(const char *chars, size_t len);
bool palette_requires_utf8_encoding(const char *chars, size_t len);
bool detect_client_utf8_support(utf8_capabilities_t *caps);
palette_type_t select_compatible_palette(palette_type_t requested, bool client_utf8);
int apply_palette_config(palette_type_t type, const char *custom_chars);
int build_client_luminance_palette(const char *palette_chars, size_t palette_len, char luminance_mapping[256]);
int initialize_client_palette(palette_type_t palette_type, const char *custom_chars, char client_palette_chars[256],
                              size_t *client_palette_len, char client_luminance_palette[256]);