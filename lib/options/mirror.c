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
  const char *optstring = ":x:y:c:fM:P:C:sSD:h";

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
      if (parse_width_option(value_str) != ASCIICHAT_OK)
        return option_error_invalid();
      break;
    }

    case 'y': {
      char *value_str = validate_required_argument(optarg, argbuf, sizeof(argbuf), "height", MODE_MIRROR);
      if (!value_str)
        return option_error_invalid();
      if (parse_height_option(value_str) != ASCIICHAT_OK)
        return option_error_invalid();
      break;
    }

    case 'c': {
      char *value_str = validate_required_argument(optarg, argbuf, sizeof(argbuf), "webcam-index", MODE_MIRROR);
      if (!value_str)
        return option_error_invalid();
      if (parse_webcam_index_option(value_str) != ASCIICHAT_OK)
        return option_error_invalid();
      break;
    }

    case 'f':
      opt_webcam_flip = !opt_webcam_flip;
      break;

    case 1000: { // --color-mode
      char *value_str = get_required_argument(optarg, argbuf, sizeof(argbuf), "color-mode", MODE_MIRROR);
      if (!value_str)
        return option_error_invalid();
      if (parse_color_mode_option(value_str) != ASCIICHAT_OK)
        return option_error_invalid();
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
      if (parse_render_mode_option(value_str) != ASCIICHAT_OK)
        return option_error_invalid();
      break;
    }

    case 'P': { // --palette
      char *value_str = get_required_argument(optarg, argbuf, sizeof(argbuf), "palette", MODE_MIRROR);
      if (!value_str)
        return option_error_invalid();
      if (parse_palette_option(value_str) != ASCIICHAT_OK)
        return option_error_invalid();
      break;
    }

    case 'C': { // --palette-chars
      char *value_str = get_required_argument(optarg, argbuf, sizeof(argbuf), "palette-chars", MODE_MIRROR);
      if (!value_str)
        return option_error_invalid();
      if (parse_palette_chars_option(value_str) != ASCIICHAT_OK)
        return option_error_invalid();
      break;
    }

    case 's': // --stretch
      opt_stretch = 1;
      break;

    case 'S': // --snapshot
      opt_snapshot_mode = 1;
      break;

    case 'D': { // --snapshot-delay
      char *value_str = validate_required_argument(optarg, argbuf, sizeof(argbuf), "snapshot-delay", MODE_MIRROR);
      if (!value_str)
        return option_error_invalid();
      if (parse_snapshot_delay_option(value_str) != ASCIICHAT_OK)
        return option_error_invalid();
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

void usage_mirror(FILE *desc) {
  (void)fprintf(desc, "ascii-chat - mirror options\n\n");
  (void)fprintf(desc, "USAGE:\n");
  (void)fprintf(desc, "  ascii-chat mirror [options...]\n\n");
  (void)fprintf(desc, "DESCRIPTION:\n");
  (void)fprintf(desc, "  View your local webcam as ASCII art without connecting to a server.\n\n");
  (void)fprintf(desc, "OPTIONS:\n");
  (void)fprintf(desc, USAGE_HELP_LINE);
  (void)fprintf(desc, USAGE_WIDTH_LINE);
  (void)fprintf(desc, USAGE_HEIGHT_LINE);
  (void)fprintf(desc, USAGE_WEBCAM_INDEX_LINE);
  (void)fprintf(desc, USAGE_LIST_WEBCAMS_LINE);
  (void)fprintf(desc, USAGE_WEBCAM_FLIP_LINE);
  (void)fprintf(desc, USAGE_TEST_PATTERN_MIRROR_LINE);
#ifdef _WIN32
  (void)fprintf(desc, USAGE_FPS_WIN_LINE);
#else
  (void)fprintf(desc, USAGE_FPS_UNIX_LINE);
#endif
  (void)fprintf(desc, USAGE_COLOR_MODE_LINE);
  (void)fprintf(desc, USAGE_SHOW_CAPABILITIES_LINE);
  (void)fprintf(desc, USAGE_UTF8_LINE);
  (void)fprintf(desc, USAGE_RENDER_MODE_LINE);
  (void)fprintf(desc, USAGE_PALETTE_LINE);
  (void)fprintf(desc, USAGE_PALETTE_CHARS_LINE);
  (void)fprintf(desc, USAGE_STRETCH_LINE);
  (void)fprintf(desc, USAGE_SNAPSHOT_LINE);
  (void)fprintf(desc, USAGE_SNAPSHOT_DELAY_LINE, (double)SNAPSHOT_DELAY_DEFAULT);
  (void)fprintf(desc, USAGE_STRIP_ANSI_LINE);
}
