/**
 * @file lib/options/colorscheme.c
 * @brief Color scheme management implementation and early initialization
 * @ingroup options
 *
 * Implements color scheme selection, loading, and compilation for ascii-chat.
 * Supports built-in themes, TOML configuration files, and early initialization
 * before logging starts.
 */

#include <ascii-chat/options/colorscheme.h>
#include <ascii-chat/common.h>
#include <ascii-chat/video/ansi_fast.h>
#include <ascii-chat-deps/tomlc17/src/tomlc17.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <sys/stat.h>

/* ============================================================================
 * Global State
 * ============================================================================ */

static color_scheme_t g_active_scheme = {0};
static bool g_colorscheme_initialized = false;

/* Mutex for color scheme compilation - used by both colorscheme.c and logging.c */
/* Must be statically initialized with PTHREAD_MUTEX_INITIALIZER to avoid deadlock */
#ifdef _WIN32
mutex_t g_colorscheme_mutex = {0}; /* Windows CRITICAL_SECTION - referenced from logging.c via extern */
#else
mutex_t g_colorscheme_mutex = PTHREAD_MUTEX_INITIALIZER; /* POSIX pthread_mutex_t */
#endif

/* ============================================================================
 * Built-In Color Schemes
 * ============================================================================ */

/**
 * @brief Pastel color scheme (default ascii-chat theme)
 * Soft, pleasant pastel colors for terminal viewing
 */
static const color_scheme_t PASTEL_SCHEME = {.name = "pastel",
                                             .description = "Soft pastel colors (ascii-chat default)",
                                             .log_colors_dark =
                                                 {
                                                     {240, 150, 100}, /* DEV: Orange */
                                                     {101, 172, 225}, /* DEBUG: Cyan */
                                                     {144, 224, 112}, /* INFO: Green */
                                                     {240, 204, 145}, /* WARN: Yellow */
                                                     {232, 93, 111},  /* ERROR: Red */
                                                     {200, 160, 216}, /* FATAL: Magenta */
                                                     {128, 128, 128}, /* GREY */
                                                     {255, 255, 255}  /* RESET */
                                                 },
                                             .has_light_variant = true,
                                             .log_colors_light =
                                                 {
                                                     {75, 95, 223},  /* DEV: Darker blue */
                                                     {50, 130, 180}, /* DEBUG: Darker cyan */
                                                     {34, 139, 34},  /* INFO: Darker green */
                                                     {180, 140, 0},  /* WARN: Darker yellow */
                                                     {178, 34, 34},  /* ERROR: Darker red */
                                                     {128, 0, 128},  /* FATAL: Darker magenta */
                                                     {64, 64, 64},   /* GREY: Darker gray */
                                                     {0, 0, 0}       /* RESET */
                                                 },
                                             .is_builtin = true};

/**
 * @brief Nord color scheme
 * Arctic, muted colors inspired by Nord theme
 */
static const color_scheme_t NORD_SCHEME = {.name = "nord",
                                           .description = "Arctic, muted Nord theme colors",
                                           .log_colors_dark =
                                               {
                                                   {191, 144, 97},  /* DEV: Nord orange */
                                                   {143, 188, 187}, /* DEBUG: Nord frost */
                                                   {163, 190, 140}, /* INFO: Nord green */
                                                   {235, 203, 139}, /* WARN: Nord sun */
                                                   {191, 97, 106},  /* ERROR: Nord red */
                                                   {180, 142, 173}, /* FATAL: Nord purple */
                                                   {216, 222, 233}, /* GREY: Nord snow */
                                                   {255, 255, 255}  /* RESET */
                                               },
                                           .has_light_variant = true,
                                           .log_colors_light =
                                               {
                                                   {76, 86, 106},  /* DEV: Nord bg dark */
                                                   {67, 76, 94},   /* DEBUG: Nord bg darker */
                                                   {89, 131, 52},  /* INFO: Darker green */
                                                   {191, 144, 0},  /* WARN: Darker yellow */
                                                   {129, 30, 44},  /* ERROR: Darker red */
                                                   {110, 76, 101}, /* FATAL: Darker purple */
                                                   {76, 86, 106},  /* GREY */
                                                   {0, 0, 0}       /* RESET */
                                               },
                                           .is_builtin = true};

/**
 * @brief Solarized dark color scheme
 * Precision colors for machines and people (dark background)
 */
static const color_scheme_t SOLARIZED_DARK_SCHEME = {.name = "solarized-dark",
                                                     .description = "Solarized dark theme - precision colors",
                                                     .log_colors_dark =
                                                         {
                                                             {203, 75, 22},   /* DEV: Orange */
                                                             {42, 161, 152},  /* DEBUG: Cyan */
                                                             {133, 153, 0},   /* INFO: Green */
                                                             {181, 137, 0},   /* WARN: Yellow */
                                                             {220, 50, 47},   /* ERROR: Red */
                                                             {108, 113, 196}, /* FATAL: Violet */
                                                             {101, 123, 142}, /* GREY: Base0 */
                                                             {255, 255, 255}  /* RESET */
                                                         },
                                                     .has_light_variant = true,
                                                     .log_colors_light =
                                                         {
                                                             {161, 105, 70}, /* DEV: Brown/Orange */
                                                             {20, 110, 101}, /* DEBUG: Darker cyan */
                                                             {89, 100, 0},   /* INFO: Darker green */
                                                             {101, 76, 0},   /* WARN: Darker yellow */
                                                             {153, 0, 0},    /* ERROR: Darker red */
                                                             {68, 68, 153},  /* FATAL: Darker violet */
                                                             {42, 61, 76},   /* GREY */
                                                             {0, 0, 0}       /* RESET */
                                                         },
                                                     .is_builtin = true};

/**
 * @brief Solarized light color scheme
 * Precision colors for machines and people (light background)
 */
static const color_scheme_t SOLARIZED_LIGHT_SCHEME = {.name = "solarized-light",
                                                      .description = "Solarized light theme - precision colors",
                                                      .log_colors_dark =
                                                          {
                                                              {161, 105, 70}, /* DEV: Brown/Orange */
                                                              {20, 110, 101}, /* DEBUG: Darker cyan */
                                                              {89, 100, 0},   /* INFO: Darker green */
                                                              {101, 76, 0},   /* WARN: Darker yellow */
                                                              {153, 0, 0},    /* ERROR: Darker red */
                                                              {68, 68, 153},  /* FATAL: Darker violet */
                                                              {42, 61, 76},   /* GREY */
                                                              {0, 0, 0}       /* RESET */
                                                          },
                                                      .has_light_variant = false,
                                                      .is_builtin = true};

/**
 * @brief Dracula color scheme
 * Dark vampiric theme with vibrant colors
 */
static const color_scheme_t DRACULA_SCHEME = {.name = "dracula",
                                              .description = "Dracula dark theme - vampiric colors",
                                              .log_colors_dark =
                                                  {
                                                      {255, 121, 84},  /* DEV: Orange */
                                                      {139, 233, 253}, /* DEBUG: Cyan */
                                                      {80, 250, 123},  /* INFO: Green */
                                                      {241, 250, 140}, /* WARN: Yellow */
                                                      {255, 121, 198}, /* ERROR: Pink */
                                                      {189, 147, 249}, /* FATAL: Purple */
                                                      {98, 114, 164},  /* GREY: Comment */
                                                      {255, 255, 255}  /* RESET */
                                                  },
                                              .has_light_variant = false,
                                              .is_builtin = true};

/**
 * @brief Gruvbox dark color scheme
 * Retro warm colors with high contrast (dark background)
 */
static const color_scheme_t GRUVBOX_DARK_SCHEME = {.name = "gruvbox-dark",
                                                   .description = "Gruvbox dark theme - retro warm colors",
                                                   .log_colors_dark =
                                                       {
                                                           {254, 128, 25},  /* DEV: bright_orange */
                                                           {142, 192, 124}, /* DEBUG: bright_green */
                                                           {142, 192, 124}, /* INFO: bright_green */
                                                           {250, 189, 47},  /* WARN: bright_yellow */
                                                           {251, 73, 52},   /* ERROR: bright_red */
                                                           {211, 134, 155}, /* FATAL: bright_purple */
                                                           {168, 153, 132}, /* GREY: gray */
                                                           {255, 255, 255}  /* RESET */
                                                       },
                                                   .has_light_variant = true,
                                                   .log_colors_light =
                                                       {
                                                           {175, 58, 3},   /* DEV: faded_orange */
                                                           {121, 116, 14}, /* DEBUG: faded_green */
                                                           {121, 116, 14}, /* INFO: faded_green */
                                                           {181, 118, 20}, /* WARN: faded_yellow */
                                                           {157, 0, 6},    /* ERROR: faded_red */
                                                           {108, 52, 107}, /* FATAL: faded_purple */
                                                           {105, 104, 98}, /* GREY */
                                                           {0, 0, 0}       /* RESET */
                                                       },
                                                   .is_builtin = true};

/**
 * @brief Gruvbox light color scheme
 * Retro warm colors with high contrast (light background)
 */
static const color_scheme_t GRUVBOX_LIGHT_SCHEME = {.name = "gruvbox-light",
                                                    .description = "Gruvbox light theme - retro warm colors",
                                                    .log_colors_dark =
                                                        {
                                                            {175, 58, 3},   /* DEV: faded_orange */
                                                            {121, 116, 14}, /* DEBUG: faded_green */
                                                            {121, 116, 14}, /* INFO: faded_green */
                                                            {181, 118, 20}, /* WARN: faded_yellow */
                                                            {157, 0, 6},    /* ERROR: faded_red */
                                                            {108, 52, 107}, /* FATAL: faded_purple */
                                                            {105, 104, 98}, /* GREY */
                                                            {0, 0, 0}       /* RESET */
                                                        },
                                                    .has_light_variant = false,
                                                    .is_builtin = true};

/**
 * @brief Monokai color scheme
 * Vibrant coding theme
 */
static const color_scheme_t MONOKAI_SCHEME = {.name = "monokai",
                                              .description = "Monokai theme - vibrant coding colors",
                                              .log_colors_dark =
                                                  {
                                                      {253, 151, 31},  /* DEV: Orange */
                                                      {166, 226, 46},  /* DEBUG: Green */
                                                      {174, 213, 129}, /* INFO: Light green */
                                                      {241, 250, 140}, /* WARN: Yellow */
                                                      {249, 38, 114},  /* ERROR: Magenta */
                                                      {174, 129, 255}, /* FATAL: Purple */
                                                      {117, 113, 94},  /* GREY: Comment */
                                                      {255, 255, 255}  /* RESET */
                                                  },
                                              .has_light_variant = false,
                                              .is_builtin = true};

/**
 * @brief Base16 Default dark color scheme
 * Precision colors for machines and people (dark background)
 */
static const color_scheme_t BASE16_DEFAULT_DARK_SCHEME = {.name = "base16-default-dark",
                                                          .description = "Base16 Default dark theme - machine colors",
                                                          .log_colors_dark =
                                                              {
                                                                  {220, 150, 86},  /* DEV: orange (0x09) */
                                                                  {134, 193, 185}, /* DEBUG: cyan (0x0C) */
                                                                  {161, 181, 108}, /* INFO: green (0x0B) */
                                                                  {247, 202, 136}, /* WARN: yellow (0x0A) */
                                                                  {171, 70, 66},   /* ERROR: red (0x08) */
                                                                  {186, 139, 175}, /* FATAL: magenta (0x0E) */
                                                                  {88, 88, 88},    /* GREY: base03 */
                                                                  {255, 255, 255}  /* RESET */
                                                              },
                                                          .has_light_variant = true,
                                                          .log_colors_light =
                                                              {
                                                                  {161, 105, 70},  /* DEV: brown (0x0F) */
                                                                  {88, 88, 88},    /* DEBUG: darker gray */
                                                                  {88, 88, 88},    /* INFO: darker gray */
                                                                  {161, 105, 70},  /* WARN: brown (0x0F) */
                                                                  {171, 70, 66},   /* ERROR: red (0x08) */
                                                                  {88, 88, 88},    /* FATAL: darker gray */
                                                                  {184, 184, 184}, /* GREY: lighter gray */
                                                                  {0, 0, 0}        /* RESET */
                                                              },
                                                          .is_builtin = true};

/**
 * @brief Base16 Default light color scheme
 * Precision colors for machines and people (light background)
 */
static const color_scheme_t BASE16_DEFAULT_LIGHT_SCHEME = {.name = "base16-default-light",
                                                           .description = "Base16 Default light theme - machine colors",
                                                           .log_colors_dark =
                                                               {
                                                                   {161, 105, 70},  /* DEV: brown (0x0F) */
                                                                   {88, 88, 88},    /* DEBUG: darker gray */
                                                                   {88, 88, 88},    /* INFO: darker gray */
                                                                   {161, 105, 70},  /* WARN: brown (0x0F) */
                                                                   {171, 70, 66},   /* ERROR: red (0x08) */
                                                                   {88, 88, 88},    /* FATAL: darker gray */
                                                                   {184, 184, 184}, /* GREY: lighter gray */
                                                                   {0, 0, 0}        /* RESET */
                                                               },
                                                           .has_light_variant = false,
                                                           .is_builtin = true};

/**
 * @brief Array of all built-in color schemes
 */
static const color_scheme_t *BUILTIN_SCHEMES[] = {&PASTEL_SCHEME,
                                                  &NORD_SCHEME,
                                                  &SOLARIZED_DARK_SCHEME,
                                                  &SOLARIZED_LIGHT_SCHEME,
                                                  &DRACULA_SCHEME,
                                                  &GRUVBOX_DARK_SCHEME,
                                                  &GRUVBOX_LIGHT_SCHEME,
                                                  &MONOKAI_SCHEME,
                                                  &BASE16_DEFAULT_DARK_SCHEME,
                                                  &BASE16_DEFAULT_LIGHT_SCHEME};

#define NUM_BUILTIN_SCHEMES (sizeof(BUILTIN_SCHEMES) / sizeof(BUILTIN_SCHEMES[0]))

/* ============================================================================
 * Internal Functions
 * ============================================================================ */

/**
 * @brief Find a built-in scheme by name
 */
static const color_scheme_t *find_builtin_scheme(const char *name) {
  if (!name)
    return NULL;

  /* Handle "default" alias */
  if (strcmp(name, "default") == 0) {
    name = "pastel";
  }

  for (size_t i = 0; i < NUM_BUILTIN_SCHEMES; i++) {
    if (strcmp(BUILTIN_SCHEMES[i]->name, name) == 0) {
      return BUILTIN_SCHEMES[i];
    }
  }

  return NULL;
}

/* ============================================================================
 * Public API: Initialization
 * ============================================================================ */

asciichat_error_t colorscheme_init(void) {
  if (g_colorscheme_initialized) {
    return ASCIICHAT_OK;
  }

  /* NOTE: Mutex is already statically initialized on POSIX with PTHREAD_MUTEX_INITIALIZER.
   * On Windows, we initialize it here. Do NOT call mutex_init() on POSIX because
   * double-initialization of pthread_mutex_t causes undefined behavior and deadlocks. */
#ifdef _WIN32
  static bool mutex_initialized = false;
  if (!mutex_initialized) {
    mutex_init(&g_colorscheme_mutex);
    mutex_initialized = true;
  }
#endif

  /* Load default scheme */
  const color_scheme_t *pastel = find_builtin_scheme("pastel");
  if (!pastel) {
    return SET_ERRNO(ERROR_CONFIG, "Failed to load default pastel scheme");
  }

  memcpy(&g_active_scheme, pastel, sizeof(color_scheme_t));
  g_colorscheme_initialized = true;

  return ASCIICHAT_OK;
}

void colorscheme_cleanup_compiled(compiled_color_scheme_t *compiled) {
  if (!compiled) {
    return;
  }

  /* Free allocated color code strings */
  for (int i = 0; i < 8; i++) {
    char *str_16 = (char *)compiled->codes_16[i];
    char *str_256 = (char *)compiled->codes_256[i];
    char *str_truecolor = (char *)compiled->codes_truecolor[i];
    SAFE_FREE(str_16);
    SAFE_FREE(str_256);
    SAFE_FREE(str_truecolor);
  }
  memset(compiled, 0, sizeof(compiled_color_scheme_t));
}

void colorscheme_shutdown(void) {
  if (!g_colorscheme_initialized) {
    return;
  }

  mutex_lock(&g_colorscheme_mutex);
  memset(&g_active_scheme, 0, sizeof(color_scheme_t));
  g_colorscheme_initialized = false;
  mutex_unlock(&g_colorscheme_mutex);

  /* NOTE: Do NOT call mutex_destroy() on POSIX because the mutex is statically
   * initialized with PTHREAD_MUTEX_INITIALIZER. Destroying a statically-initialized
   * mutex is undefined behavior. On Windows, we must destroy the CRITICAL_SECTION. */
#ifdef _WIN32
  mutex_destroy(&g_colorscheme_mutex);
#endif
}

/* ============================================================================
 * Public API: Scheme Management
 * ============================================================================ */

const color_scheme_t *colorscheme_get_active_scheme(void) {
  if (!g_colorscheme_initialized) {
    /* Lazy initialization of color system */
    colorscheme_init();
  }

  return &g_active_scheme;
}

asciichat_error_t colorscheme_set_active_scheme(const char *name) {
  if (!name) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Scheme name is NULL");
  }

  /* Ensure color system is initialized */
  if (!g_colorscheme_initialized) {
    colorscheme_init();
  }

  color_scheme_t scheme = {0};
  asciichat_error_t result = ASCIICHAT_OK;

  /* Try loading as built-in scheme first */
  const color_scheme_t *builtin = find_builtin_scheme(name);
  if (builtin) {
    memcpy(&scheme, builtin, sizeof(color_scheme_t));
  } else if (strchr(name, '/') || strchr(name, '.')) {
    /* Try loading as file path if it contains / or . */
    result = colorscheme_load_from_file(name, &scheme);
    if (result != ASCIICHAT_OK) {
      return result;
    }
  } else {
    return SET_ERRNO(ERROR_CONFIG, "Unknown color scheme: %s (not a built-in scheme or valid file path)", name);
  }

  mutex_lock(&g_colorscheme_mutex);
  memcpy(&g_active_scheme, &scheme, sizeof(color_scheme_t));
  mutex_unlock(&g_colorscheme_mutex);

  log_debug("Switched to color scheme: %s", name);
  return ASCIICHAT_OK;
}

asciichat_error_t colorscheme_load_builtin(const char *name, color_scheme_t *scheme) {
  if (!name || !scheme) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "NULL name or scheme pointer");
  }

  const color_scheme_t *builtin = find_builtin_scheme(name);
  if (!builtin) {
    return SET_ERRNO(ERROR_CONFIG, "Unknown built-in color scheme: %s", name);
  }

  memcpy(scheme, builtin, sizeof(color_scheme_t));
  return ASCIICHAT_OK;
}

asciichat_error_t colorscheme_load_from_file(const char *path, color_scheme_t *scheme) {
  if (!path || !scheme) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "NULL path or scheme pointer");
  }

  /* Check if file exists and is readable */
  struct stat sb;
  if (stat(path, &sb) != 0 || !S_ISREG(sb.st_mode)) {
    return SET_ERRNO(ERROR_FILE_NOT_FOUND, "Color scheme file not found or not readable: %s", path);
  }

  /* Parse TOML file */
  toml_result_t result = toml_parse_file_ex(path);
  if (!result.ok) {
    toml_free(result);
    return SET_ERRNO(ERROR_CONFIG, "Failed to parse color scheme file '%s': %s", path, result.errmsg);
  }

  /* Extract scheme information */
  memset(scheme, 0, sizeof(color_scheme_t));

  /* Get scheme section */
  toml_datum_t scheme_section = toml_get(result.toptab, "scheme");
  if (scheme_section.type == TOML_TABLE) {
    /* Get scheme name */
    toml_datum_t name_datum = toml_get(scheme_section, "name");
    if (name_datum.type == TOML_STRING) {
      SAFE_STRNCPY(scheme->name, name_datum.u.s, sizeof(scheme->name));
    }

    /* Get scheme description */
    toml_datum_t desc_datum = toml_get(scheme_section, "description");
    if (desc_datum.type == TOML_STRING) {
      SAFE_STRNCPY(scheme->description, desc_datum.u.s, sizeof(scheme->description));
    }
  }

  /* Get colors section */
  toml_datum_t colors_section = toml_get(result.toptab, "colors");
  if (colors_section.type == TOML_TABLE) {
    /* Parse dark mode colors */
    toml_datum_t dark_section = toml_get(colors_section, "dark");
    if (dark_section.type == TOML_TABLE) {
      const char *color_names[] = {"dev", "debug", "warn", "info", "error", "fatal", "grey", "reset"};
      for (int i = 0; i < 8; i++) {
        toml_datum_t color_value = toml_get(dark_section, color_names[i]);
        if (color_value.type == TOML_STRING) {
          parse_hex_color(color_value.u.s, &scheme->log_colors_dark[i].r, &scheme->log_colors_dark[i].g,
                          &scheme->log_colors_dark[i].b);
        }
      }
    }

    /* Parse light mode colors (optional) */
    toml_datum_t light_section = toml_get(colors_section, "light");
    if (light_section.type == TOML_TABLE) {
      scheme->has_light_variant = true;
      const char *color_names[] = {"dev", "debug", "warn", "info", "error", "fatal", "grey", "reset"};
      for (int i = 0; i < 8; i++) {
        toml_datum_t color_value = toml_get(light_section, color_names[i]);
        if (color_value.type == TOML_STRING) {
          parse_hex_color(color_value.u.s, &scheme->log_colors_light[i].r, &scheme->log_colors_light[i].g,
                          &scheme->log_colors_light[i].b);
        }
      }
    }
  }

  scheme->is_builtin = false;
  SAFE_STRNCPY(scheme->source_file, path, sizeof(scheme->source_file));

  toml_free(result);
  return ASCIICHAT_OK;
}

/* ============================================================================
 * Public API: Color Conversion
 * ============================================================================ */

asciichat_error_t parse_hex_color(const char *hex, uint8_t *r, uint8_t *g, uint8_t *b) {
  if (!hex || !r || !g || !b) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "NULL parameter");
  }

  /* Skip '#' prefix if present */
  if (hex[0] == '#') {
    hex++;
  }

  /* Validate hex string length */
  if (strlen(hex) != 6) {
    return SET_ERRNO(ERROR_CONFIG, "Invalid hex color (must be #RRGGBB): %s", hex);
  }

  /* Parse hex digits */
  unsigned int rgb = 0;
  if (SAFE_SSCANF(hex, "%6x", &rgb) != 1) {
    return SET_ERRNO(ERROR_CONFIG, "Invalid hex color format: %s", hex);
  }

  *r = (rgb >> 16) & 0xFF;
  *g = (rgb >> 8) & 0xFF;
  *b = rgb & 0xFF;

  return ASCIICHAT_OK;
}

/* Note: rgb_to_16color() and rgb_to_256color() are already defined in lib/video/ansi_fast.c
 * We use those definitions instead of duplicating them here.
 * External declarations from ansi_fast.h are used above. */

void rgb_to_truecolor_ansi(uint8_t r, uint8_t g, uint8_t b, char *buf, size_t size) {
  if (!buf || size < 20)
    return;
  safe_snprintf(buf, size, "\x1b[38;2;%d;%d;%dm", r, g, b);
}

/* ============================================================================
 * Public API: Scheme Compilation
 * ============================================================================ */

asciichat_error_t colorscheme_compile_scheme(const color_scheme_t *scheme, terminal_color_mode_t mode,
                                             terminal_background_t background, compiled_color_scheme_t *compiled) {
  if (!scheme || !compiled) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "NULL scheme or compiled pointer");
  }

  /* Note: mode parameter reserved for future use */
  (void)mode;

  /* Free any previously compiled strings before recompiling */
  /* This prevents memory leaks when the color scheme is recompiled */
  colorscheme_cleanup_compiled(compiled);

  /* Select color array based on background */
  const rgb_color_t *colors = (background == TERM_BACKGROUND_LIGHT && scheme->has_light_variant)
                                  ? scheme->log_colors_light
                                  : scheme->log_colors_dark;

  /* Helper: allocate and format a color code string */
  char temp_buf[128];

  /* Compile for 16-color mode */
  for (int i = 0; i < 8; i++) {
    if (i == 7) {
      /* RESET */
      SAFE_STRNCPY(temp_buf, "\x1b[0m", sizeof(temp_buf));
    } else {
      uint8_t color_idx = rgb_to_16color(colors[i].r, colors[i].g, colors[i].b);
      /* ANSI color codes: 30-37 for normal, 90-97 for bright */
      if (color_idx < 8) {
        safe_snprintf(temp_buf, sizeof(temp_buf), "\x1b[%dm", 30 + color_idx);
      } else {
        safe_snprintf(temp_buf, sizeof(temp_buf), "\x1b[%dm", 90 + (color_idx - 8));
      }
    }
    /* Allocate string with SAFE_MALLOC and copy */
    size_t len = strlen(temp_buf) + 1;
    char *allocated = SAFE_MALLOC(len, char *);
    if (!allocated) {
      return SET_ERRNO(ERROR_MEMORY, "Failed to allocate color code string");
    }
    memcpy(allocated, temp_buf, len);
    compiled->codes_16[i] = allocated;
  }

  /* Compile for 256-color mode */
  for (int i = 0; i < 8; i++) {
    if (i == 7) {
      SAFE_STRNCPY(temp_buf, "\x1b[0m", sizeof(temp_buf));
    } else {
      uint8_t color_idx = rgb_to_256color(colors[i].r, colors[i].g, colors[i].b);
      safe_snprintf(temp_buf, sizeof(temp_buf), "\x1b[38;5;%dm", color_idx);
    }
    /* Allocate string with SAFE_MALLOC and copy */
    size_t len = strlen(temp_buf) + 1;
    char *allocated = SAFE_MALLOC(len, char *);
    if (!allocated) {
      return SET_ERRNO(ERROR_MEMORY, "Failed to allocate color code string");
    }
    memcpy(allocated, temp_buf, len);
    compiled->codes_256[i] = allocated;
  }

  /* Compile for truecolor mode */
  for (int i = 0; i < 8; i++) {
    if (i == 7) {
      SAFE_STRNCPY(temp_buf, "\x1b[0m", sizeof(temp_buf));
    } else {
      rgb_to_truecolor_ansi(colors[i].r, colors[i].g, colors[i].b, temp_buf, sizeof(temp_buf));
    }
    /* Allocate string with SAFE_MALLOC and copy */
    size_t len = strlen(temp_buf) + 1;
    char *allocated = SAFE_MALLOC(len, char *);
    if (!allocated) {
      return SET_ERRNO(ERROR_MEMORY, "Failed to allocate color code string");
    }
    memcpy(allocated, temp_buf, len);
    compiled->codes_truecolor[i] = allocated;
  }

  return ASCIICHAT_OK;
}

/* ============================================================================
 * Public API: Export
 * ============================================================================ */

asciichat_error_t colorscheme_export_scheme(const char *scheme_name, const char *file_path) {
  if (!scheme_name) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Scheme name is NULL");
  }

  color_scheme_t scheme = {0};
  asciichat_error_t result = colorscheme_load_builtin(scheme_name, &scheme);
  if (result != ASCIICHAT_OK) {
    return result;
  }

  /* Generate TOML content */
  char toml_content[8192] = {0};
  size_t offset = 0;

  /* Scheme header section */
  offset += safe_snprintf(toml_content + offset, sizeof(toml_content) - offset,
                          "[scheme]\n"
                          "name = \"%s\"\n"
                          "description = \"%s\"\n\n",
                          scheme.name, scheme.description);

  /* Dark mode colors */
  const char *color_names[] = {"dev", "debug", "warn", "info", "error", "fatal", "grey", "reset"};
  offset += safe_snprintf(toml_content + offset, sizeof(toml_content) - offset, "[colors.dark]\n");

  for (int i = 0; i < 8; i++) {
    offset +=
        safe_snprintf(toml_content + offset, sizeof(toml_content) - offset, "%s = \"#%02X%02X%02X\"\n", color_names[i],
                      scheme.log_colors_dark[i].r, scheme.log_colors_dark[i].g, scheme.log_colors_dark[i].b);
  }

  /* Light mode colors if available */
  if (scheme.has_light_variant) {
    offset += safe_snprintf(toml_content + offset, sizeof(toml_content) - offset, "\n[colors.light]\n");
    for (int i = 0; i < 8; i++) {
      offset += safe_snprintf(toml_content + offset, sizeof(toml_content) - offset, "%s = \"#%02X%02X%02X\"\n",
                              color_names[i], scheme.log_colors_light[i].r, scheme.log_colors_light[i].g,
                              scheme.log_colors_light[i].b);
    }
  }

  /* Write to file or stdout */
  if (file_path) {
    FILE *fp = fopen(file_path, "w");
    if (!fp) {
      return SET_ERRNO_SYS(ERROR_FILE_OPERATION, "Cannot open %s for writing", file_path);
    }
    if (fputs(toml_content, fp) < 0) {
      fclose(fp);
      return SET_ERRNO_SYS(ERROR_FILE_OPERATION, "Failed to write to %s", file_path);
    }
    fclose(fp);
  } else {
    /* Write to stdout */
    if (fputs(toml_content, stdout) < 0) {
      return SET_ERRNO_SYS(ERROR_FILE_OPERATION, "Failed to write to stdout");
    }
    /* Flush to ensure piped output is written immediately */
    (void)fflush(stdout);
  }

  return ASCIICHAT_OK;
}

/* ============================================================================
 * Terminal Background Detection
 * ============================================================================ */

terminal_background_t detect_terminal_background(void) {
  /* Method 1: Check environment variable override */
  const char *term_bg = SAFE_GETENV("TERM_BACKGROUND");
  if (term_bg) {
    if (strcasecmp(term_bg, "light") == 0) {
      return TERM_BACKGROUND_LIGHT;
    }
    if (strcasecmp(term_bg, "dark") == 0) {
      return TERM_BACKGROUND_DARK;
    }
  }

  /* Method 2: Parse COLORFGBG (format: "15;0" = light fg, dark bg) */
  const char *colorfgbg = SAFE_GETENV("COLORFGBG");
  if (colorfgbg) {
    int fg = -1, bg = -1;
    if (SAFE_SSCANF(colorfgbg, "%d;%d", &fg, &bg) == 2) {
      /* bg < 8 = dark colors, bg >= 8 = light colors */
      if (bg >= 0 && bg < 8) {
        return TERM_BACKGROUND_DARK;
      }
      if (bg >= 8 && bg < 16) {
        return TERM_BACKGROUND_LIGHT;
      }
    }
  }

  /* Default: Dark (most terminals use dark backgrounds) */
  return TERM_BACKGROUND_DARK;
}

/* ============================================================================
 * Early Color Scheme Loading (called from main() before log_init)
 * ============================================================================ */

/**
 * @brief Scan argv for --color-scheme option (quick parse, no validation)
 * @param argc Argument count
 * @param argv Argument array
 * @return Color scheme name if found, NULL otherwise
 *
 * This is a simple linear scan that doesn't do full option parsing.
 * It's only used to find --color-scheme before logging is initialized.
 */
static const char *find_cli_color_scheme(int argc, const char *const argv[]) {
  for (int i = 1; i < argc - 1; i++) {
    if (strcmp(argv[i], "--color-scheme") == 0) {
      return argv[i + 1];
    }
  }
  return NULL;
}

/**
 * @brief Load color scheme from config files (checks multiple locations)
 * @param scheme Pointer to store loaded scheme
 * @return ASCIICHAT_OK if loaded from any location, ERROR_NOT_FOUND if none found
 *
 * Attempts to load user's custom color scheme from TOML config files using the
 * unified platform config search API. Searches standard locations in priority order:
 * 1. ~/.config/ascii-chat/colors.toml (highest priority, user config)
 * 2. /opt/homebrew/etc/ascii-chat/colors.toml (macOS Homebrew)
 * 3. /usr/local/etc/ascii-chat/colors.toml (Unix/Linux local)
 * 4. /etc/ascii-chat/colors.toml (system-wide, lowest priority)
 *
 * Uses override semantics: returns first match (highest priority).
 * Built-in defaults are used if no config file found.
 */
static asciichat_error_t load_config_color_scheme(color_scheme_t *scheme) {
  if (!scheme) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "scheme pointer is NULL");
  }

  /* Use platform abstraction to find colors.toml across standard locations */
  config_file_list_t config_files = {0};
  asciichat_error_t search_result = platform_find_config_file("colors.toml", &config_files);

  if (search_result != ASCIICHAT_OK) {
    /* Platform search failed - non-fatal, will use built-in defaults */
    return ERROR_NOT_FOUND;
  }

  /* Use first match (highest priority) - override semantics */
  asciichat_error_t load_result = ERROR_NOT_FOUND;
  if (config_files.count > 0) {
    load_result = colorscheme_load_from_file(config_files.files[0].path, scheme);
  }

  /* Clean up search results */
  config_file_list_free(&config_files);

  return load_result;
}

/**
 * @brief Initialize color scheme early (before logging)
 * @param argc Argument count
 * @param argv Argument array
 * @return ASCIICHAT_OK on success, error code on failure (non-fatal)
 *
 * This function is called from main() BEFORE log_init() to apply color scheme
 * to logging before any log messages are printed.
 *
 * Priority order:
 * 1. --color-scheme CLI argument (highest priority)
 * 2. ~/.config/ascii-chat/colors.toml config file
 * 3. Built-in "pastel" default scheme (lowest priority)
 */
asciichat_error_t options_colorscheme_init_early(int argc, const char *const argv[]) {
  /* Initialize the color system with defaults */
  asciichat_error_t result = colorscheme_init();
  if (result != ASCIICHAT_OK) {
    /* Non-fatal: use built-in defaults */
    return ASCIICHAT_OK;
  }

  /* Step 1: Try to load from config file (~/.config/ascii-chat/colors.toml) */
  color_scheme_t config_scheme = {0};
  asciichat_error_t config_result = load_config_color_scheme(&config_scheme);
  if (config_result == ASCIICHAT_OK) {
    /* Config file loaded successfully, apply it */
    colorscheme_set_active_scheme(config_scheme.name);
  }

  /* Step 2: CLI --color-scheme overrides config file */
  const char *cli_scheme = find_cli_color_scheme(argc, argv);
  if (cli_scheme) {
    asciichat_error_t cli_result = colorscheme_set_active_scheme(cli_scheme);
    if (cli_result != ASCIICHAT_OK) {
      /* Invalid scheme name from CLI, continue with current scheme */
      return cli_result;
    }
  }

  return ASCIICHAT_OK;
}
