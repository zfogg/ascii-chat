/**
 * @file lib/ui/colors.c
 * @brief Color scheme management implementation
 * @ingroup ui
 */

#include "colors.h"
#include "../common.h"
#include "../video/ansi_fast.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>

/* ============================================================================
 * Global State
 * ============================================================================ */

static color_scheme_t g_active_scheme = {0};
static bool g_colors_initialized = false;

/* Mutex for color scheme compilation - used by both colors.c and logging.c */
/* Must be statically initialized with PTHREAD_MUTEX_INITIALIZER to avoid deadlock */
#ifdef _WIN32
mutex_t g_colors_mutex = {0}; /* Windows CRITICAL_SECTION */
#else
mutex_t g_colors_mutex = PTHREAD_MUTEX_INITIALIZER; /* POSIX pthread_mutex_t */
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
                                                     {107, 127, 255}, /* DEV: Blue */
                                                     {101, 172, 225}, /* DEBUG: Cyan */
                                                     {240, 204, 145}, /* WARN: Yellow */
                                                     {144, 224, 112}, /* INFO: Green */
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
                                                     {180, 140, 0},  /* WARN: Darker yellow */
                                                     {34, 139, 34},  /* INFO: Darker green */
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
                                                   {136, 192, 208}, /* DEV: Nord blue */
                                                   {143, 188, 187}, /* DEBUG: Nord frost */
                                                   {235, 203, 139}, /* WARN: Nord sun */
                                                   {163, 190, 140}, /* INFO: Nord green */
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
                                                   {191, 144, 0},  /* WARN: Darker yellow */
                                                   {89, 131, 52},  /* INFO: Darker green */
                                                   {129, 30, 44},  /* ERROR: Darker red */
                                                   {110, 76, 101}, /* FATAL: Darker purple */
                                                   {76, 86, 106},  /* GREY */
                                                   {0, 0, 0}       /* RESET */
                                               },
                                           .is_builtin = true};

/**
 * @brief Solarized color scheme
 * Precision colors for machines and people
 */
static const color_scheme_t SOLARIZED_SCHEME = {.name = "solarized-dark",
                                                .description = "Solarized dark theme - precision colors",
                                                .log_colors_dark =
                                                    {
                                                        {38, 139, 210},  /* DEV: Blue */
                                                        {42, 161, 152},  /* DEBUG: Cyan */
                                                        {181, 137, 0},   /* WARN: Yellow */
                                                        {133, 153, 0},   /* INFO: Green */
                                                        {220, 50, 47},   /* ERROR: Red */
                                                        {108, 113, 196}, /* FATAL: Violet */
                                                        {101, 123, 142}, /* GREY: Base0 */
                                                        {255, 255, 255}  /* RESET */
                                                    },
                                                .has_light_variant = true,
                                                .log_colors_light =
                                                    {
                                                        {22, 82, 144},  /* DEV: Darker blue */
                                                        {20, 110, 101}, /* DEBUG: Darker cyan */
                                                        {101, 76, 0},   /* WARN: Darker yellow */
                                                        {89, 100, 0},   /* INFO: Darker green */
                                                        {153, 0, 0},    /* ERROR: Darker red */
                                                        {68, 68, 153},  /* FATAL: Darker violet */
                                                        {42, 61, 76},   /* GREY */
                                                        {0, 0, 0}       /* RESET */
                                                    },
                                                .is_builtin = true};

/**
 * @brief Dracula color scheme
 * Dark vampiric theme with vibrant colors
 */
static const color_scheme_t DRACULA_SCHEME = {.name = "dracula",
                                              .description = "Dracula dark theme - vampiric colors",
                                              .log_colors_dark =
                                                  {
                                                      {189, 147, 249}, /* DEV: Purple */
                                                      {139, 233, 253}, /* DEBUG: Cyan */
                                                      {241, 250, 140}, /* WARN: Yellow */
                                                      {80, 250, 123},  /* INFO: Green */
                                                      {255, 121, 198}, /* ERROR: Pink */
                                                      {189, 147, 249}, /* FATAL: Purple */
                                                      {98, 114, 164},  /* GREY: Comment */
                                                      {255, 255, 255}  /* RESET */
                                                  },
                                              .has_light_variant = false,
                                              .is_builtin = true};

/**
 * @brief Gruvbox color scheme
 * Retro warm colors with high contrast
 */
static const color_scheme_t GRUVBOX_SCHEME = {.name = "gruvbox-dark",
                                              .description = "Gruvbox dark theme - retro warm colors",
                                              .log_colors_dark =
                                                  {
                                                      {131, 165, 152}, /* DEV: Aqua */
                                                      {142, 192, 124}, /* DEBUG: Green */
                                                      {250, 189, 47},  /* WARN: Yellow */
                                                      {142, 192, 124}, /* INFO: Green */
                                                      {251, 73, 52},   /* ERROR: Red */
                                                      {215, 95, 0},    /* FATAL: Orange */
                                                      {168, 153, 132}, /* GREY: Gray */
                                                      {255, 255, 255}  /* RESET */
                                                  },
                                              .has_light_variant = true,
                                              .log_colors_light =
                                                  {
                                                      {105, 104, 98}, /* DEV: Darker aqua */
                                                      {79, 91, 59},   /* DEBUG: Darker green */
                                                      {181, 137, 0},  /* WARN: Darker yellow */
                                                      {79, 91, 59},   /* INFO: Darker green */
                                                      {157, 0, 6},    /* ERROR: Darker red */
                                                      {166, 39, 0},   /* FATAL: Darker orange */
                                                      {105, 104, 98}, /* GREY */
                                                      {0, 0, 0}       /* RESET */
                                                  },
                                              .is_builtin = true};

/**
 * @brief Monokai color scheme
 * Vibrant coding theme
 */
static const color_scheme_t MONOKAI_SCHEME = {.name = "monokai",
                                              .description = "Monokai theme - vibrant coding colors",
                                              .log_colors_dark =
                                                  {
                                                      {102, 217, 239}, /* DEV: Blue */
                                                      {166, 226, 46},  /* DEBUG: Green */
                                                      {253, 151, 31},  /* WARN: Orange */
                                                      {174, 213, 129}, /* INFO: Light green */
                                                      {249, 38, 114},  /* ERROR: Magenta */
                                                      {174, 129, 255}, /* FATAL: Purple */
                                                      {117, 113, 94},  /* GREY: Comment */
                                                      {255, 255, 255}  /* RESET */
                                                  },
                                              .has_light_variant = false,
                                              .is_builtin = true};

/**
 * @brief Array of all built-in color schemes
 */
static const color_scheme_t *BUILTIN_SCHEMES[] = {&PASTEL_SCHEME,  &NORD_SCHEME,    &SOLARIZED_SCHEME,
                                                  &DRACULA_SCHEME, &GRUVBOX_SCHEME, &MONOKAI_SCHEME};

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

asciichat_error_t colors_init(void) {
  if (g_colors_initialized) {
    return ASCIICHAT_OK;
  }

  /* NOTE: Mutex is already statically initialized on POSIX with PTHREAD_MUTEX_INITIALIZER.
   * On Windows, we initialize it here. Do NOT call mutex_init() on POSIX because
   * double-initialization of pthread_mutex_t causes undefined behavior and deadlocks. */
#ifdef _WIN32
  static bool mutex_initialized = false;
  if (!mutex_initialized) {
    mutex_init(&g_colors_mutex);
    mutex_initialized = true;
  }
#endif

  /* Load default scheme */
  const color_scheme_t *pastel = find_builtin_scheme("pastel");
  if (!pastel) {
    return SET_ERRNO(ERROR_CONFIG, "Failed to load default pastel scheme");
  }

  memcpy(&g_active_scheme, pastel, sizeof(color_scheme_t));
  g_colors_initialized = true;

  return ASCIICHAT_OK;
}

void colors_shutdown(void) {
  if (!g_colors_initialized) {
    return;
  }

  mutex_lock(&g_colors_mutex);
  memset(&g_active_scheme, 0, sizeof(color_scheme_t));
  g_colors_initialized = false;
  mutex_unlock(&g_colors_mutex);

  /* NOTE: Do NOT call mutex_destroy() on POSIX because the mutex is statically
   * initialized with PTHREAD_MUTEX_INITIALIZER. Destroying a statically-initialized
   * mutex is undefined behavior. On Windows, we must destroy the CRITICAL_SECTION. */
#ifdef _WIN32
  mutex_destroy(&g_colors_mutex);
#endif
}

/* ============================================================================
 * Public API: Scheme Management
 * ============================================================================ */

const color_scheme_t *colors_get_active_scheme(void) {
  if (!g_colors_initialized) {
    /* Lazy initialization of color system */
    colors_init();
  }

  return &g_active_scheme;
}

asciichat_error_t colors_set_active_scheme(const char *name) {
  if (!name) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Scheme name is NULL");
  }

  /* Ensure color system is initialized */
  if (!g_colors_initialized) {
    colors_init();
  }

  const color_scheme_t *scheme = find_builtin_scheme(name);
  if (!scheme) {
    return SET_ERRNO(ERROR_CONFIG, "Unknown color scheme: %s", name);
  }

  mutex_lock(&g_colors_mutex);
  memcpy(&g_active_scheme, scheme, sizeof(color_scheme_t));
  mutex_unlock(&g_colors_mutex);

  log_debug("Switched to color scheme: %s", name);
  return ASCIICHAT_OK;
}

asciichat_error_t colors_load_builtin(const char *name, color_scheme_t *scheme) {
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

asciichat_error_t colors_load_from_file(const char *path, color_scheme_t *scheme) {
  if (!path || !scheme) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "NULL path or scheme pointer");
  }

  /* TODO: Implement TOML parsing */
  return SET_ERRNO(ERROR_NOT_SUPPORTED, "TOML color file loading not yet implemented");
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

asciichat_error_t colors_compile_scheme(const color_scheme_t *scheme, terminal_color_mode_t mode,
                                        terminal_background_t background, compiled_color_scheme_t *compiled) {
  if (!scheme || !compiled) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "NULL scheme or compiled pointer");
  }

  /* Note: mode parameter reserved for future use */
  (void)mode;

  /* Select color array based on background */
  const rgb_color_t *colors = (background == TERM_BACKGROUND_LIGHT && scheme->has_light_variant)
                                  ? scheme->log_colors_light
                                  : scheme->log_colors_dark;

  /* Compile for 16-color mode */
  for (int i = 0; i < 8; i++) {
    if (i == 7) {
      /* RESET */
      SAFE_STRNCPY(compiled->codes_16[i], "\x1b[0m", sizeof(compiled->codes_16[i]));
    } else {
      uint8_t color_idx = rgb_to_16color(colors[i].r, colors[i].g, colors[i].b);
      /* ANSI color codes: 30-37 for normal, 90-97 for bright */
      if (color_idx < 8) {
        safe_snprintf(compiled->codes_16[i], sizeof(compiled->codes_16[i]), "\x1b[%dm", 30 + color_idx);
      } else {
        safe_snprintf(compiled->codes_16[i], sizeof(compiled->codes_16[i]), "\x1b[%dm", 90 + (color_idx - 8));
      }
    }
  }

  /* Compile for 256-color mode */
  for (int i = 0; i < 8; i++) {
    if (i == 7) {
      SAFE_STRNCPY(compiled->codes_256[i], "\x1b[0m", sizeof(compiled->codes_256[i]));
    } else {
      uint8_t color_idx = rgb_to_256color(colors[i].r, colors[i].g, colors[i].b);
      safe_snprintf(compiled->codes_256[i], sizeof(compiled->codes_256[i]), "\x1b[38;5;%dm", color_idx);
    }
  }

  /* Compile for truecolor mode */
  for (int i = 0; i < 8; i++) {
    if (i == 7) {
      SAFE_STRNCPY(compiled->codes_truecolor[i], "\x1b[0m", sizeof(compiled->codes_truecolor[i]));
    } else {
      rgb_to_truecolor_ansi(colors[i].r, colors[i].g, colors[i].b, compiled->codes_truecolor[i],
                            sizeof(compiled->codes_truecolor[i]));
    }
  }

  return ASCIICHAT_OK;
}

/* ============================================================================
 * Public API: Export
 * ============================================================================ */

asciichat_error_t colors_export_scheme(const char *scheme_name, const char *file_path) {
  if (!scheme_name) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Scheme name is NULL");
  }

  /* Note: file_path parameter reserved for future use */
  (void)file_path;

  color_scheme_t scheme = {0};
  asciichat_error_t result = colors_load_builtin(scheme_name, &scheme);
  if (result != ASCIICHAT_OK) {
    return result;
  }

  /* TODO: Generate TOML content and write to file or stdout */
  return SET_ERRNO(ERROR_NOT_SUPPORTED, "Color scheme export not yet implemented");
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
