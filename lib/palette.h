#pragma once

/**
 * @file palette.h
 * @defgroup palette Palette Management
 * @ingroup palette
 * @brief ASCII Palette Management for Video-to-ASCII Conversion
 *
 * This header provides ASCII palette management for converting video frames to
 * ASCII art in ASCII-Chat. The system supports multiple built-in palettes and
 * custom user-defined palettes, with full UTF-8 support for enhanced visual effects.
 *
 * CORE FEATURES:
 * ==============
 * - Multiple built-in palette types (standard, blocks, digital, minimal, cool)
 * - Custom user-defined palettes via command-line options
 * - UTF-8 character support for enhanced visual effects
 * - Automatic UTF-8 detection and compatibility checking
 * - Palette validation to ensure proper character sequences
 * - Luminance mapping for optimal brightness-to-character conversion
 *
 * BUILT-IN PALETTES:
 * ==================
 * The system provides five built-in palette types:
 * - Standard: "   ...',;:clodxkO0KXNWM" (classic ASCII)
 * - Blocks:   "   ░░▒▒▓▓██"             (Unicode block characters)
 * - Digital:  "   -=≡≣▰▱◼"              (digital/glitch aesthetic)
 * - Minimal:  "   .-+*#"                (simple ASCII)
 * - Cool:     "   ▁▂▃▄▅▆▇█"             (ascending blocks)
 *
 * UTF-8 SUPPORT:
 * ==============
 * The system provides:
 * - Automatic UTF-8 capability detection
 * - UTF-8 palette parsing and validation
 * - Per-character UTF-8 byte tracking
 * - Display width calculation for proper rendering
 * - Terminal compatibility checking
 *
 * PALETTE ARCHITECTURE:
 * ====================
 * Palettes are organized as:
 * - Character sequence: Ordered from darkest to lightest
 * - Luminance mapping: Direct brightness-to-character index lookup
 * - UTF-8 encoding: Proper handling of multi-byte characters
 * - Validation: Ensures palettes contain valid, ordered characters
 *
 * CLIENT COMPATIBILITY:
 * =====================
 * The system automatically:
 * - Detects client UTF-8 support
 * - Selects compatible palettes based on capabilities
 * - Falls back to ASCII-only palettes when needed
 * - Validates palette compatibility before use
 *
 * @note Palettes are ordered from darkest to lightest character.
 * @note UTF-8 palettes require terminal UTF-8 support for proper display.
 * @note Custom palettes are validated to ensure proper character ordering
 *       and compatibility.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date 2025
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @name ASCII Palette Types
 * @{
 * @ingroup module_video
 */

/**
 * @brief Built-in palette type enumeration
 *
 * Defines the available built-in palette types. Palettes are ordered
 * from darkest to lightest character for optimal brightness mapping.
 *
 * @ingroup module_video
 */
typedef enum {
  /** @brief Standard ASCII palette: "   ...',;:clodxkO0KXNWM" */
  PALETTE_STANDARD = 0,
  /** @brief Unicode block characters: "   ░░▒▒▓▓██" */
  PALETTE_BLOCKS,
  /** @brief Digital/glitch aesthetic: "   -=≡≣▰▱◼" */
  PALETTE_DIGITAL,
  /** @brief Simple ASCII: "   .-+*#" */
  PALETTE_MINIMAL,
  /** @brief Ascending blocks: "   ▁▂▃▄▅▆▇█" */
  PALETTE_COOL,
  /** @brief User-defined via --palette-chars */
  PALETTE_CUSTOM,
  /** @brief Number of palette types (not a valid palette) */
  PALETTE_COUNT
} palette_type_t;

/** @} */

/**
 * @brief Palette definition structure
 *
 * Contains all information about a palette, including its character
 * sequence, UTF-8 requirements, and validation status.
 *
 * @note Character sequence must be ordered from darkest to lightest
 *       for proper luminance mapping.
 *
 * @ingroup module_video
 */
typedef struct {
  /** @brief Human-readable palette name */
  const char *name;
  /** @brief Character sequence (ordered from dark to light) */
  const char *chars;
  /** @brief Number of characters in sequence */
  size_t length;
  /** @brief True if palette contains UTF-8 multi-byte characters */
  bool requires_utf8;
  /** @brief True if palette passed validation checks */
  bool is_validated;
} palette_def_t;

/**
 * @brief UTF-8 capability detection structure
 *
 * Stores information about terminal UTF-8 support, including detection
 * results and terminal environment information.
 *
 * @ingroup module_video
 */
typedef struct {
  /** @brief True if terminal supports UTF-8 (detected automatically) */
  bool utf8_support;
  /** @brief True if user forced UTF-8 via --utf8 flag */
  bool forced_utf8;
  /** @brief $TERM environment variable value (for detection) */
  char terminal_type[32];
  /** @brief Current locale encoding (e.g., "UTF-8") */
  char locale_encoding[16];
} utf8_capabilities_t;

/**
 * @name Built-in Palette Character Strings
 * @{
 * @ingroup module_video
 */

/**
 * @brief Standard ASCII palette character string
 *
 * Classic ASCII palette with varying character density.
 *
 * @ingroup module_video
 */
#define PALETTE_CHARS_STANDARD "   ...',;:clodxkO0KXNWM"

/**
 * @brief Unicode block palette character string
 *
 * Unicode block characters (half and full blocks) for solid rendering.
 *
 * @ingroup module_video
 */
#define PALETTE_CHARS_BLOCKS "   ░░▒▒▓▓██"

/**
 * @brief Digital/glitch palette character string
 *
 * Digital aesthetic characters for unique visual style.
 *
 * @ingroup module_video
 */
#define PALETTE_CHARS_DIGITAL "   -=≡≣▰▱◼"

/**
 * @brief Minimal ASCII palette character string
 *
 * Simple ASCII characters for basic rendering.
 *
 * @ingroup module_video
 */
#define PALETTE_CHARS_MINIMAL "   .-+*#"

/**
 * @brief Cool ascending blocks palette character string
 *
 * Unicode ascending blocks for smooth gradient effect.
 *
 * @ingroup module_video
 */
#define PALETTE_CHARS_COOL "   ▁▂▃▄▅▆▇█"

/** @} */

/**
 * @brief UTF-8 character information structure
 *
 * Stores UTF-8 character encoding information including byte sequence,
 * byte length, and display width for proper terminal rendering.
 *
 * @note UTF-8 characters can be 1-4 bytes long.
 *
 * @note Display width can be 1 or 2 (for full-width characters).
 *
 * @ingroup module_video
 */
typedef struct {
  /** @brief UTF-8 byte sequence (max 4 bytes per character) */
  char bytes[4];
  /** @brief Number of bytes in UTF-8 encoding (1-4) */
  uint8_t byte_len;
  /** @brief Terminal display width in character cells (1-2) */
  uint8_t display_width;
} utf8_char_info_t;

/**
 * @brief UTF-8 palette structure
 *
 * Properly handles multi-byte UTF-8 characters in palettes. Stores
 * per-character information for efficient lookup and rendering.
 *
 * @note char_count is the number of characters (not bytes), which is
 *       important for proper palette indexing.
 *
 * @note total_bytes is the total byte length of the raw string, useful
 *       for memory management.
 *
 * @ingroup module_video
 */
typedef struct {
  /** @brief Array of UTF-8 character information */
  utf8_char_info_t *chars;
  /** @brief Number of characters (not bytes!) */
  size_t char_count;
  /** @brief Total byte length of palette string */
  size_t total_bytes;
  /** @brief Original palette string (for reference/debugging) */
  char *raw_string;
} utf8_palette_t;

/**
 * @name Default Palette Constants
 * @{
 * @ingroup module_video
 */

/**
 * @brief Default ASCII palette for legacy functions
 *
 * Standard ASCII palette used as fallback for legacy code paths.
 *
 * @ingroup module_video
 */
extern const char DEFAULT_ASCII_PALETTE[];

/**
 * @brief Length of default ASCII palette
 *
 * Number of characters in DEFAULT_ASCII_PALETTE.
 *
 * @ingroup module_video
 */
extern const size_t DEFAULT_ASCII_PALETTE_LEN;

/** @} */

/**
 * @name Palette Management Functions
 * @{
 * @ingroup module_video
 */

/**
 * @brief Get built-in palette definition
 * @param type Palette type (must be < PALETTE_COUNT and != PALETTE_CUSTOM)
 * @return Pointer to palette definition, or NULL if type is invalid
 *
 * Returns a pointer to the built-in palette definition for the specified type.
 * The returned pointer points to a statically allocated structure and should
 * not be freed.
 *
 * @note PALETTE_CUSTOM is not a built-in palette and returns NULL.
 * @note The palette definition includes character sequence, length, and
 *       UTF-8 requirement information.
 *
 * @ingroup module_video
 */
const palette_def_t *get_builtin_palette(palette_type_t type);

/**
 * @brief Validate palette character sequence
 * @param chars Character sequence to validate (must not be NULL)
 * @param len Length of character sequence in bytes
 * @return true if palette is valid, false otherwise
 *
 * Validates that a palette character sequence is properly formed and ordered.
 * Checks for:
 * - Non-empty sequence
 * - Valid character ordering (if applicable)
 * - Proper UTF-8 encoding (for multi-byte characters)
 *
 * @note Validation rules may vary for ASCII vs UTF-8 palettes.
 * @note Empty palettes or invalid sequences return false.
 *
 * @ingroup module_video
 */
bool validate_palette_chars(const char *chars, size_t len);

/**
 * @brief Check if palette requires UTF-8 encoding
 * @param chars Character sequence to check (must not be NULL)
 * @param len Length of character sequence in bytes
 * @return true if palette contains UTF-8 multi-byte characters, false otherwise
 *
 * Determines whether a palette contains UTF-8 multi-byte characters that
 * require UTF-8 terminal support. ASCII-only palettes return false.
 *
 * @note Single-byte ASCII characters (< 0x80) are not considered UTF-8.
 * @note This check determines if client must have UTF-8 support for palette.
 *
 * @ingroup module_video
 */
bool palette_requires_utf8_encoding(const char *chars, size_t len);

/**
 * @brief Detect client UTF-8 support capabilities
 * @param caps Output structure for capabilities (must not be NULL)
 * @return true if detection succeeded, false otherwise
 *
 * Detects client terminal UTF-8 support by checking environment variables,
 * locale settings, and terminal type. Results are stored in the capabilities
 * structure.
 *
 * @note Detection checks $TERM environment variable and locale encoding.
 * @note User can force UTF-8 support via --utf8 flag (stored in forced_utf8).
 *
 * @ingroup module_video
 */
bool detect_client_utf8_support(utf8_capabilities_t *caps);

/**
 * @brief Select compatible palette based on client capabilities
 * @param requested Requested palette type
 * @param client_utf8 True if client supports UTF-8, false otherwise
 * @return Compatible palette type (may differ from requested)
 *
 * Selects a palette that is compatible with the client's UTF-8 capabilities.
 * If the requested palette requires UTF-8 but the client doesn't support it,
 * falls back to an ASCII-only palette.
 *
 * @note UTF-8 palettes (BLOCKS, DIGITAL, COOL) require UTF-8 support.
 * @note ASCII palettes (STANDARD, MINIMAL) work with any terminal.
 * @note Returns requested palette if compatible, otherwise returns STANDARD.
 *
 * @ingroup module_video
 */
palette_type_t select_compatible_palette(palette_type_t requested, bool client_utf8);

/**
 * @brief Apply palette configuration (set global palette)
 * @param type Palette type to apply
 * @param custom_chars Custom palette characters (required if type is PALETTE_CUSTOM, NULL otherwise)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Applies a palette configuration globally. For built-in palettes, uses the
 * built-in character sequence. For custom palettes, validates and uses the
 * provided custom_chars string.
 *
 * @note Custom palette characters must pass validation before being applied.
 * @note This function sets the global palette used for video-to-ASCII conversion.
 *
 * @warning Custom palette characters must be ordered from darkest to lightest.
 *
 * @ingroup module_video
 */
int apply_palette_config(palette_type_t type, const char *custom_chars);

/**
 * @brief Build luminance mapping table from palette characters
 * @param palette_chars Palette character sequence (must not be NULL)
 * @param palette_len Length of palette character sequence
 * @param luminance_mapping Output array for luminance mapping (must not be NULL, 256 elements)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Builds a luminance mapping table that maps pixel brightness values (0-255)
 * to palette character indices. The mapping is used for efficient video-to-ASCII
 * conversion by direct lookup instead of searching.
 *
 * @note The mapping array must have 256 elements (one per possible brightness value).
 * @note Palette characters are mapped proportionally across the brightness range.
 * @note Resulting mapping provides optimal brightness-to-character conversion.
 *
 * @ingroup module_video
 */
int build_client_luminance_palette(const char *palette_chars, size_t palette_len, char luminance_mapping[256]);

/**
 * @brief Initialize client palette with full configuration
 * @param palette_type Palette type to initialize
 * @param custom_chars Custom palette characters (required if type is PALETTE_CUSTOM, NULL otherwise)
 * @param client_palette_chars Output buffer for palette characters (must not be NULL, 256 elements)
 * @param client_palette_len Output pointer for palette length (must not be NULL)
 * @param client_luminance_palette Output buffer for luminance mapping (must not be NULL, 256 elements)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Initializes a complete client palette configuration including character
 * sequence and luminance mapping. This is a convenience function that combines
 * palette selection, validation, and luminance mapping creation.
 *
 * @note Output buffers must be large enough (256 elements for palette_chars
 *       and luminance_palette).
 * @note The function handles both built-in and custom palettes.
 * @note Luminance mapping is automatically generated from the character sequence.
 *
 * @ingroup module_video
 */
int initialize_client_palette(palette_type_t palette_type, const char *custom_chars, char client_palette_chars[256],
                              size_t *client_palette_len, char client_luminance_palette[256]);

/**
 * @name UTF-8 Palette Functions
 * @{
 * @ingroup module_video
 */

/**
 * @brief Create a UTF-8 palette from string
 * @param palette_string UTF-8 palette string (must not be NULL)
 * @return Pointer to UTF-8 palette, or NULL on failure
 *
 * Creates a UTF-8 palette structure by parsing a UTF-8 character string.
 * The string is analyzed to extract individual UTF-8 characters and their
 * encoding information (bytes, byte length, display width).
 *
 * @note The palette string can contain multi-byte UTF-8 characters.
 * @note Character count (not byte count) is stored in the palette structure.
 * @note Display width is calculated for proper terminal rendering.
 *
 * @warning Must call utf8_palette_destroy() to free resources.
 *
 * @ingroup module_video
 */
utf8_palette_t *utf8_palette_create(const char *palette_string);

/**
 * @brief Destroy a UTF-8 palette and free resources
 * @param palette UTF-8 palette to destroy (can be NULL)
 *
 * Destroys a UTF-8 palette and frees all associated memory including
 * character arrays and the raw string. Safe to call multiple times
 * or with NULL pointer.
 *
 * @note All memory allocated by utf8_palette_create() is freed.
 *
 * @ingroup module_video
 */
void utf8_palette_destroy(utf8_palette_t *palette);

/**
 * @brief Get UTF-8 character information at index
 * @param palette UTF-8 palette (must not be NULL)
 * @param index Character index (0-based, must be < char_count)
 * @return Pointer to UTF-8 character info, or NULL if index is invalid
 *
 * Returns a pointer to the UTF-8 character information structure at the
 * specified index. The returned pointer points to internal palette data
 * and should not be freed.
 *
 * @note Index is character-based, not byte-based (important for multi-byte characters).
 * @note Returns NULL if index is out of range.
 * @note The returned structure contains bytes, byte_len, and display_width.
 *
 * @ingroup module_video
 */
const utf8_char_info_t *utf8_palette_get_char(const utf8_palette_t *palette, size_t index);

/**
 * @brief Get number of characters in UTF-8 palette
 * @param palette UTF-8 palette (must not be NULL)
 * @return Number of characters (not bytes)
 *
 * Returns the number of characters in the UTF-8 palette. This is the character
 * count, not the byte count, which is important for proper palette indexing.
 *
 * @note For multi-byte UTF-8 characters, char_count is less than total_bytes.
 * @note Use this for palette iteration and bounds checking.
 *
 * @ingroup module_video
 */
size_t utf8_palette_get_char_count(const utf8_palette_t *palette);

/**
 * @brief Check if UTF-8 palette contains a specific character
 * @param palette UTF-8 palette (must not be NULL)
 * @param utf8_char UTF-8 character bytes (must not be NULL)
 * @param char_bytes Number of bytes in UTF-8 character (1-4)
 * @return true if character is found in palette, false otherwise
 *
 * Checks whether the UTF-8 palette contains a specific UTF-8 character by
 * comparing the character's byte sequence with all characters in the palette.
 *
 * @note char_bytes must match the actual UTF-8 byte length of the character.
 * @note Character comparison is byte-accurate for multi-byte characters.
 *
 * @ingroup module_video
 */
bool utf8_palette_contains_char(const utf8_palette_t *palette, const char *utf8_char, size_t char_bytes);

/**
 * @brief Find index of UTF-8 character in palette
 * @param palette UTF-8 palette (must not be NULL)
 * @param utf8_char UTF-8 character bytes (must not be NULL)
 * @param char_bytes Number of bytes in UTF-8 character (1-4)
 * @return Character index if found, SIZE_MAX if not found
 *
 * Searches the UTF-8 palette for a specific character and returns its index
 * (0-based) if found. Returns SIZE_MAX if the character is not in the palette.
 *
 * @note Returns the first occurrence if character appears multiple times.
 * @note For multiple occurrences, use utf8_palette_find_all_char_indices().
 * @note Index is character-based, suitable for palette lookup operations.
 *
 * @ingroup module_video
 */
size_t utf8_palette_find_char_index(const utf8_palette_t *palette, const char *utf8_char, size_t char_bytes);

/**
 * @brief Find all indices of UTF-8 character in palette
 * @param palette UTF-8 palette (must not be NULL)
 * @param utf8_char UTF-8 character bytes (must not be NULL)
 * @param char_bytes Number of bytes in UTF-8 character (1-4)
 * @param indices Output array for indices (must not be NULL)
 * @param max_indices Maximum number of indices to return (size of indices array)
 * @return Number of indices found (may be less than max_indices)
 *
 * Finds all occurrences of a UTF-8 character in the palette and stores their
 * indices in the provided array. Returns the number of indices found.
 *
 * @note If more occurrences exist than max_indices, only the first max_indices
 *       are stored.
 * @note Returns 0 if character is not found in palette.
 * @note Indices are stored in order of appearance in the palette.
 *
 * @warning Indices array must be large enough to hold max_indices size_t values.
 *
 * @ingroup module_video
 */
size_t utf8_palette_find_all_char_indices(const utf8_palette_t *palette, const char *utf8_char, size_t char_bytes,
                                          size_t *indices, size_t max_indices);

/** @} */
