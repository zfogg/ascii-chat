/**
 * @file mirror.c
 * @ingroup options
 * @brief Mirror mode option parsing and help text
 *
 * Mirror-specific command-line argument parsing for standalone local webcam display.
 * This is a subset of client options with no networking, audio, or encryption.
 *
 * Supported features:
 * - Local webcam capture
 * - ASCII art rendering
 * - Terminal color modes
 * - Palette customization
 * - Snapshot mode
 */

#include "options/mirror.h"
#include "options/common.h"

#include "asciichat_errno.h"
#include "common.h"
#include "log/logging.h"
#include "options/options.h"
#include "util/parsing.h"
#include "video/ascii.h"
#include "video/webcam/webcam.h"

#ifdef _WIN32
#include "platform/windows/getopt.h"
#else
#include <getopt.h>
#endif

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ============================================================================
// Mirror Options Array
// ============================================================================

static struct option mirror_options[] = {{"width", required_argument, NULL, 'x'},
                                         {"height", required_argument, NULL, 'y'},
                                         {"webcam-index", required_argument, NULL, 'c'},
                                         {"webcam-flip", no_argument, NULL, 'f'},
                                         {"test-pattern", no_argument, NULL, 1004},
                                         {"fps", required_argument, NULL, 1003},
                                         {"color-mode", required_argument, NULL, 1000},
                                         {"show-capabilities", no_argument, NULL, 1001},
                                         {"utf8", no_argument, NULL, 1002},
                                         {"render-mode", required_argument, NULL, 'M'},
                                         {"palette", required_argument, NULL, 'P'},
                                         {"palette-chars", required_argument, NULL, 'C'},
                                         {"stretch", no_argument, NULL, 's'},
                                         {"quiet", no_argument, NULL, 'q'},
                                         {"snapshot", no_argument, NULL, 'S'},
                                         {"snapshot-delay", required_argument, NULL, 'D'},
                                         {"strip-ansi", no_argument, NULL, 1017},
                                         {"list-webcams", no_argument, NULL, 1013},
                                         {"help", optional_argument, NULL, 'h'},
                                         {0, 0, 0, 0}};

// ============================================================================
// Mirror Option Parsing
// ============================================================================

asciichat_error_t parse_mirror_options(int argc, char **argv) {
  const char *optstring = ":x:y:c:fM:P:C:sqSD:h";

  // Pre-pass: Check for --help first
  for (int i = 1; i < argc; i++) {
    if (argv[i] == NULL) {
      break;
    }
    if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
      usage_mirror(stdout);
      (void)fflush(stdout);
      _exit(0);
    }
  }

  // Main parsing loop
  int longindex = 0;
  while (1) {
    longindex = 0;
    int c = getopt_long(argc, argv, optstring, mirror_options, &longindex);
    if (c == -1)
      break;

    char argbuf[1024];
    switch (c) {
    case 0:
      // Long-only options
      break;

    case 'x': {
      char *value_str = validate_required_argument(optarg, argbuf, sizeof(argbuf), "width", MODE_MIRROR);
      if (!value_str)
        return option_error_invalid();
      int width_val;
      if (!validate_positive_int_opt(value_str, &width_val, "width"))
        return option_error_invalid();
      opt_width = (unsigned short int)width_val;
      extern bool auto_width;
      auto_width = false;
      break;
    }

    case 'y': {
      char *value_str = validate_required_argument(optarg, argbuf, sizeof(argbuf), "height", MODE_MIRROR);
      if (!value_str)
        return option_error_invalid();
      int height_val;
      if (!validate_positive_int_opt(value_str, &height_val, "height"))
        return option_error_invalid();
      opt_height = (unsigned short int)height_val;
      extern bool auto_height;
      auto_height = false;
      break;
    }

    case 'c': {
      char *value_str = validate_required_argument(optarg, argbuf, sizeof(argbuf), "webcam-index", MODE_MIRROR);
      if (!value_str)
        return option_error_invalid();
      unsigned short int index_val;
      if (!validate_webcam_index(value_str, &index_val))
        return option_error_invalid();
      opt_webcam_index = index_val;
      break;
    }

    case 'f':
      opt_webcam_flip = !opt_webcam_flip;
      break;

    case 1000: { // --color-mode
      char *value_str = get_required_argument(optarg, argbuf, sizeof(argbuf), "color-mode", MODE_MIRROR);
      if (!value_str)
        return option_error_invalid();
      if (strcmp(value_str, "auto") == 0) {
        opt_color_mode = COLOR_MODE_AUTO;
      } else if (strcmp(value_str, "none") == 0 || strcmp(value_str, "mono") == 0) {
        opt_color_mode = COLOR_MODE_NONE;
      } else if (strcmp(value_str, "16") == 0 || strcmp(value_str, "16color") == 0) {
        opt_color_mode = COLOR_MODE_16_COLOR;
      } else if (strcmp(value_str, "256") == 0 || strcmp(value_str, "256color") == 0) {
        opt_color_mode = COLOR_MODE_256_COLOR;
      } else if (strcmp(value_str, "truecolor") == 0 || strcmp(value_str, "24bit") == 0) {
        opt_color_mode = COLOR_MODE_TRUECOLOR;
      } else {
        (void)fprintf(stderr, "Error: Invalid color mode '%s'. Valid modes: auto, none, 16, 256, truecolor\n",
                      value_str);
        return option_error_invalid();
      }
      break;
    }

    case 1001: // --show-capabilities
      opt_show_capabilities = 1;
      break;

    case 1002: // --utf8
      opt_force_utf8 = 1;
      break;

    case 1003: { // --fps
      extern int g_max_fps;
      char *value_str = validate_required_argument(optarg, argbuf, sizeof(argbuf), "fps", MODE_MIRROR);
      if (!value_str)
        return option_error_invalid();
      int fps_val;
      if (!validate_fps_opt(value_str, &fps_val))
        return option_error_invalid();
      g_max_fps = fps_val;
      break;
    }

    case 1004: // --test-pattern
      opt_test_pattern = true;
      log_info("Using test pattern mode - webcam will not be opened");
      break;

    case 1013: { // --list-webcams
      webcam_device_info_t *devices = NULL;
      unsigned int device_count = 0;
      asciichat_error_t list_result = webcam_list_devices(&devices, &device_count);
      if (list_result != ASCIICHAT_OK) {
        (void)fprintf(stderr, "Error: Failed to enumerate webcam devices\n");
        _exit(1);
      }
      if (device_count == 0) {
        (void)fprintf(stdout, "No webcam devices found.\n");
      } else {
        (void)fprintf(stdout, "Available webcam devices:\n");
        for (unsigned int i = 0; i < device_count; i++) {
          (void)fprintf(stdout, "  %u: %s\n", devices[i].index, devices[i].name);
        }
      }
      webcam_free_device_list(devices);
      (void)fflush(stdout);
      _exit(0);
    }

    case 'M': { // --render-mode
      char *value_str = get_required_argument(optarg, argbuf, sizeof(argbuf), "render-mode", MODE_MIRROR);
      if (!value_str)
        return option_error_invalid();
      if (strcmp(value_str, "foreground") == 0 || strcmp(value_str, "fg") == 0) {
        opt_render_mode = RENDER_MODE_FOREGROUND;
      } else if (strcmp(value_str, "background") == 0 || strcmp(value_str, "bg") == 0) {
        opt_render_mode = RENDER_MODE_BACKGROUND;
      } else if (strcmp(value_str, "half-block") == 0 || strcmp(value_str, "halfblock") == 0) {
        opt_render_mode = RENDER_MODE_HALF_BLOCK;
      } else {
        (void)fprintf(stderr, "Error: Invalid render mode '%s'. Valid modes: foreground, background, half-block\n",
                      value_str);
        return option_error_invalid();
      }
      break;
    }

    case 'P': { // --palette
      char *value_str = get_required_argument(optarg, argbuf, sizeof(argbuf), "palette", MODE_MIRROR);
      if (!value_str)
        return option_error_invalid();
      if (strcmp(value_str, "standard") == 0) {
        opt_palette_type = PALETTE_STANDARD;
      } else if (strcmp(value_str, "blocks") == 0) {
        opt_palette_type = PALETTE_BLOCKS;
      } else if (strcmp(value_str, "digital") == 0) {
        opt_palette_type = PALETTE_DIGITAL;
      } else if (strcmp(value_str, "minimal") == 0) {
        opt_palette_type = PALETTE_MINIMAL;
      } else if (strcmp(value_str, "cool") == 0) {
        opt_palette_type = PALETTE_COOL;
      } else if (strcmp(value_str, "custom") == 0) {
        opt_palette_type = PALETTE_CUSTOM;
      } else {
        (void)fprintf(stderr,
                      "Invalid palette '%s'. Valid palettes: standard, blocks, digital, minimal, cool, custom\n",
                      value_str);
        return option_error_invalid();
      }
      break;
    }

    case 'C': { // --palette-chars
      char *value_str = get_required_argument(optarg, argbuf, sizeof(argbuf), "palette-chars", MODE_MIRROR);
      if (!value_str)
        return option_error_invalid();
      if (strlen(value_str) >= sizeof(opt_palette_custom)) {
        (void)fprintf(stderr, "Invalid palette-chars: too long (%zu chars, max %zu)\n", strlen(value_str),
                      sizeof(opt_palette_custom) - 1);
        return option_error_invalid();
      }
      SAFE_STRNCPY(opt_palette_custom, value_str, sizeof(opt_palette_custom));
      opt_palette_custom[sizeof(opt_palette_custom) - 1] = '\0';
      opt_palette_custom_set = true;
      opt_palette_type = PALETTE_CUSTOM;
      break;
    }

    case 's': // --stretch
      opt_stretch = 1;
      break;

    case 'q': // --quiet
      opt_quiet = 1;
      break;

    case 'S': // --snapshot
      opt_snapshot_mode = 1;
      break;

    case 'D': { // --snapshot-delay
      char *value_str = validate_required_argument(optarg, argbuf, sizeof(argbuf), "snapshot-delay", MODE_MIRROR);
      if (!value_str)
        return option_error_invalid();
      char *endptr;
      float delay = strtof(value_str, &endptr);
      if (endptr == value_str || *endptr != '\0' || delay < 0.0f) {
        (void)fprintf(stderr, "Invalid snapshot delay '%s'. Must be a non-negative number.\n", value_str);
        return option_error_invalid();
      }
      opt_snapshot_delay = delay;
      break;
    }

    case 1017: // --strip-ansi
      opt_strip_ansi = 1;
      break;

    case 'h': // --help
      usage_mirror(stdout);
      (void)fflush(stdout);
      _exit(0);

    case ':': {
      const char *option_name = mirror_options[longindex].name;
      (void)fprintf(stderr, "mirror: option '--%s' requires an argument\n", option_name);
      return option_error_invalid();
    }

    case '?':
    default: {
      const char *unknown = argv[optind - 1];
      if (unknown && unknown[0] == '-' && unknown[1] == '-') {
        const char *suggestion = find_similar_option(unknown + 2, mirror_options);
        if (suggestion) {
          (void)fprintf(stderr, "mirror: unknown option '%s'. Did you mean '--%s'?\n", unknown, suggestion);
        } else {
          (void)fprintf(stderr, "mirror: unknown option '%s'\n", unknown);
        }
      } else {
        (void)fprintf(stderr, "mirror: invalid option\n");
      }
      return option_error_invalid();
    }
    }
  }

  // Mirror mode has no positional arguments
  return ASCIICHAT_OK;
}

// ============================================================================
// Mirror Usage Text
// ============================================================================

#define USAGE_INDENT "        "

void usage_mirror(FILE *desc) {
  (void)fprintf(desc, "ascii-chat - mirror options\n\n");
  (void)fprintf(desc, "USAGE:\n");
  (void)fprintf(desc, "  ascii-chat mirror [options...]\n\n");
  (void)fprintf(desc, "DESCRIPTION:\n");
  (void)fprintf(desc, "  View your local webcam as ASCII art without connecting to a server.\n\n");
  (void)fprintf(desc, "OPTIONS:\n");
  (void)fprintf(desc, USAGE_INDENT "-h --help                    " USAGE_INDENT "print this help\n");
  (void)fprintf(desc, USAGE_INDENT "-x --width WIDTH             " USAGE_INDENT "render width (default: [auto-set])\n");
  (void)fprintf(desc,
                USAGE_INDENT "-y --height HEIGHT           " USAGE_INDENT "render height (default: [auto-set])\n");
  (void)fprintf(desc, USAGE_INDENT "-c --webcam-index CAMERA     " USAGE_INDENT
                                   "webcam device index (0-based) (default: 0)\n");
  (void)fprintf(desc,
                USAGE_INDENT "   --list-webcams            " USAGE_INDENT "list available webcam devices and exit\n");
  (void)fprintf(desc, USAGE_INDENT "-f --webcam-flip             " USAGE_INDENT "toggle horizontal flip of webcam "
                                   "image (default: flipped)\n");
  (void)fprintf(desc, USAGE_INDENT "   --test-pattern            " USAGE_INDENT "use test pattern instead of webcam "
                                   "(for testing)\n");
  (void)fprintf(desc, USAGE_INDENT "   --fps FPS                 " USAGE_INDENT "desired frame rate 1-144 "
#ifdef _WIN32
                                   "(default: 30 for Windows)\n");
#else
                                   "(default: 60 for Unix)\n");
#endif
  (void)fprintf(desc,
                USAGE_INDENT "   --color-mode MODE         " USAGE_INDENT "color modes: auto, none, 16, 256, truecolor "
                             "(default: auto)\n");
  (void)fprintf(desc, USAGE_INDENT "   --show-capabilities       " USAGE_INDENT
                                   "show detected terminal capabilities and exit\n");
  (void)fprintf(desc, USAGE_INDENT "   --utf8                    " USAGE_INDENT
                                   "force enable UTF-8/Unicode support (default: [unset])\n");
  (void)fprintf(desc, USAGE_INDENT "-M --render-mode MODE        " USAGE_INDENT "Rendering modes: "
                                   "foreground, background, half-block (default: foreground)\n");
  (void)fprintf(desc, USAGE_INDENT "-P --palette PALETTE         " USAGE_INDENT "ASCII character palette: "
                                   "standard, blocks, digital, minimal, cool, custom (default: standard)\n");
  (void)fprintf(desc, USAGE_INDENT "-C --palette-chars CHARS     " USAGE_INDENT
                                   "Custom palette characters (implies --palette=custom) (default: [unset])\n");
  (void)fprintf(desc, USAGE_INDENT "-s --stretch                 " USAGE_INDENT "stretch or shrink video to fit "
                                   "(ignore aspect ratio) (default: [unset])\n");
  (void)fprintf(desc, USAGE_INDENT "-q --quiet                   " USAGE_INDENT
                                   "disable console logging (log only to file) (default: [unset])\n");
  (void)fprintf(desc, USAGE_INDENT "-S --snapshot                " USAGE_INDENT
                                   "capture single frame and exit (default: [unset])\n");
  (void)fprintf(
      desc, USAGE_INDENT "-D --snapshot-delay SECONDS  " USAGE_INDENT "delay SECONDS before snapshot (default: %.1f)\n",
      (double)SNAPSHOT_DELAY_DEFAULT);
  (void)fprintf(desc, USAGE_INDENT "   --strip-ansi              " USAGE_INDENT
                                   "remove all ANSI escape codes from output (default: [unset])\n");
}
