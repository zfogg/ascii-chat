#include "aspect_ratio.h"
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include "ascii.h"
#include "options.h"
#include "common.h"
#include "terminal_detect.h"

static const unsigned short default_width = 110, default_height = 70;
unsigned short int opt_width = default_width, opt_height = default_height,

                   auto_width = 1, auto_height = 1;

char opt_address[OPTIONS_BUFF_SIZE] = "0.0.0.0", opt_port[OPTIONS_BUFF_SIZE] = "27224";

unsigned short int opt_webcam_index = 0;

unsigned short int opt_webcam_flip = 1;

unsigned short int opt_color_output = 0;

// Terminal color mode and capability options
terminal_color_mode_t opt_color_mode = COLOR_MODE_AUTO;             // Auto-detect by default
background_mode_t opt_background_mode = BACKGROUND_MODE_FOREGROUND; // Foreground by default
unsigned short int opt_show_capabilities = 0;                       // Don't show capabilities by default
unsigned short int opt_force_utf8 = 0;                              // Don't force UTF-8 by default

unsigned short int opt_audio_enabled = 0;

// Allow stretching/shrinking without preserving aspect ratio when set via -s/--stretch
unsigned short int opt_stretch = 0;

// Disable console logging when set via -q/--quiet (logs only to file)
unsigned short int opt_quiet = 0;

// Enable snapshot mode when set via --snapshot (client only - capture one frame and exit)
unsigned short int opt_snapshot_mode = 0;

// Snapshot delay in seconds (float) - default 3.0 for webcam warmup
#if defined(__APPLE__)
// their macbook webcams shows pure black first then fade up into a real color image over a few seconds
#define SNAPSHOT_DELAY_DEFAULT 5.0f
#else
#define SNAPSHOT_DELAY_DEFAULT 3.0f
#endif
float opt_snapshot_delay = SNAPSHOT_DELAY_DEFAULT;

// Log file path for file logging (empty string means no file logging)
char opt_log_file[OPTIONS_BUFF_SIZE] = "";

// Encryption options
unsigned short int opt_encrypt_enabled = 0;       // Enable AES encryption via --encrypt
char opt_encrypt_key[OPTIONS_BUFF_SIZE] = "";     // Encryption key from --key
char opt_encrypt_keyfile[OPTIONS_BUFF_SIZE] = ""; // Key file path from --keyfile

// Global variables to store last known image dimensions for aspect ratio
// recalculation
unsigned short int last_image_width = 0, last_image_height = 0;

// Default weights; must add up to 1.0
const float weight_red = 0.2989f;
const float weight_green = 0.5866f;
const float weight_blue = 0.1145f;

/*
Analysis of Your Current Palette
Your palette " ...',;:clodxkO0KXNWM" represents luminance from dark to light:
    (spaces) = darkest/black areas
    ...,' = very dark details
    ;:cl = mid-dark tones
    odxk = medium tones
    O0KX = bright areas
    NWM = brightest/white areas
*/
// ASCII palette for image-to-text conversion
char ascii_palette[] = "   ...',;:clodxkO0KXNWM";

unsigned short int RED[ASCII_LUMINANCE_LEVELS], GREEN[ASCII_LUMINANCE_LEVELS], BLUE[ASCII_LUMINANCE_LEVELS],
    GRAY[ASCII_LUMINANCE_LEVELS];

// Client-only options
static struct option client_options[] = {{"address", required_argument, NULL, 'a'},
                                         {"port", required_argument, NULL, 'p'},
                                         {"width", required_argument, NULL, 'x'},
                                         {"height", required_argument, NULL, 'y'},
                                         {"webcam-index", required_argument, NULL, 'c'},
                                         {"webcam-flip", optional_argument, NULL, 'f'},
                                         {"color-mode", required_argument, NULL, 1000},
                                         {"show-capabilities", no_argument, NULL, 1001},
                                         {"utf8", no_argument, NULL, 1002},
                                         {"background-mode", required_argument, NULL, 'M'},
                                         {"audio", no_argument, NULL, 'A'},
                                         {"stretch", no_argument, NULL, 's'},
                                         {"quiet", no_argument, NULL, 'q'},
                                         {"snapshot", no_argument, NULL, 'S'},
                                         {"snapshot-delay", required_argument, NULL, 'D'},
                                         {"log-file", required_argument, NULL, 'L'},
                                         {"encrypt", no_argument, NULL, 'E'},
                                         {"key", required_argument, NULL, 'K'},
                                         {"keyfile", required_argument, NULL, 'F'},
                                         {"help", optional_argument, NULL, 'h'},
                                         {0, 0, 0, 0}};

// Server-only options
static struct option server_options[] = {{"address", required_argument, NULL, 'a'},
                                         {"port", required_argument, NULL, 'p'},
                                         {"audio", no_argument, NULL, 'A'},
                                         {"log-file", required_argument, NULL, 'L'},
                                         {"encrypt", no_argument, NULL, 'E'},
                                         {"key", required_argument, NULL, 'K'},
                                         {"keyfile", required_argument, NULL, 'F'},
                                         {"help", optional_argument, NULL, 'h'},
                                         {0, 0, 0, 0}};

// Terminal size detection functions moved to terminal_detect.c

void update_dimensions_for_full_height(void) {
  unsigned short int term_width, term_height;

  if (get_terminal_size(&term_width, &term_height) == 0) {
    log_debug("Terminal size detected: %dx%d, auto_width=%d, auto_height=%d", term_width, term_height, auto_width,
              auto_height);
    // If both dimensions are auto, set height to terminal height and let
    // aspect_ratio calculate width
    if (auto_height && auto_width) {
      opt_height = term_height;
      opt_width = term_width; // Also set width when both are auto
      log_debug("Both auto: set to %dx%d", opt_width, opt_height);
    }
    // If only height is auto, use full terminal height
    else if (auto_height) {
      opt_height = term_height;
      log_debug("Height auto: set height to %d", opt_height);
    }
    // If only width is auto, use full terminal width
    else if (auto_width) {
      opt_width = term_width;
      log_debug("Width auto: set width to %d", opt_width);
    }
  } else {
    log_debug("Failed to get terminal size, using defaults: %dx%d", opt_width, opt_height);
  }
}

void update_dimensions_to_terminal_size(void) {
  unsigned short int term_width, term_height;
  // Get current terminal size (get_terminal_size already handles ioctl first, then $COLUMNS/$LINES fallback)
  int terminal_result = get_terminal_size(&term_width, &term_height);
  if (terminal_result == 0) {
    log_debug("Terminal size detected: %dx%d", term_width, term_height);
    if (auto_width) {
      log_debug("Setting opt_width from %u to %u", opt_width, term_width);
      opt_width = term_width;
    }
    if (auto_height) {
      log_debug("Setting opt_height from %u to %u", opt_height, term_height);
      opt_height = term_height;
    }
    log_debug("After update_dimensions_to_terminal_size: opt_width=%d, opt_height=%d", opt_width, opt_height);
  } else {
    log_debug("Failed to get terminal size in update_dimensions_to_terminal_size");
  }
}

// Helper function to strip equals sign from optarg if present
static char *strip_equals_prefix(const char *optarg, char *buffer, size_t buffer_size) {
  if (!optarg)
    return NULL;

  snprintf(buffer, buffer_size, "%s", optarg);
  char *value_str = buffer;
  if (value_str[0] == '=') {
    value_str++; // Skip the equals sign
  }
  return value_str;
}

// Helper function to validate IPv4 address format
static int is_valid_ipv4(const char *ip) {
  if (!ip)
    return 0;

  // int octets[4];
  int count = 0;
  char temp[15 + 1]; // Maximum IPv4 length is 15 characters + null terminator

  // Copy to temp buffer to avoid modifying original
  if (strlen(ip) >= sizeof(temp))
    return 0;
  strncpy(temp, ip, sizeof(temp) - 1);
  temp[sizeof(temp) - 1] = '\0';

  char *saveptr;
  char *token = strtok_r(temp, ".", &saveptr);
  while (token != NULL && count < 4) {
    char *endptr;
    long octet = strtol(token, &endptr, 10);

    // Check if conversion was successful and entire token was consumed
    if (*endptr != '\0' || token == endptr)
      return 0;

    // Check octet range (0-255)
    if (octet < 0 || octet > 255)
      return 0;

    // octets[count] = (int)octet;
    token = strtok_r(NULL, ".", &saveptr);
    count++; // Increment count for each valid octet
  }

  // Must have exactly 4 octets and no remaining tokens
  return (count == 4 && token == NULL);
}

void options_init(int argc, char **argv, bool is_client) {
  // Parse arguments first, then update dimensions (moved below)

  // Use different option sets for client vs server
  const char *optstring;
  struct option *options;

  if (is_client) {
    optstring = "a:p:x:y:c:f::M:AsqSD:L:EK:F:h";
    options = client_options;
  } else {
    optstring = "a:p:AL:EK:F:h";
    options = server_options;
  }

  while (1) {
    int index = 0;
    int c = getopt_long(argc, argv, optstring, options, &index);
    if (c == -1)
      break;

    char argbuf[1024];
    switch (c) {
    case 0:
      break;

    case 'a': {
      char *value_str = strip_equals_prefix(optarg, argbuf, sizeof(argbuf));
      if (!is_valid_ipv4(value_str)) {
        log_error("Invalid IPv4 address '%s'. Address must be in format X.X.X.X where X is 0-255.", value_str);
        exit(EXIT_FAILURE);
      }
      snprintf(opt_address, OPTIONS_BUFF_SIZE, "%s", value_str);
      break;
    }

    case 'p': {
      char *value_str = strip_equals_prefix(optarg, argbuf, sizeof(argbuf));
      // Validate port is a number between 1 and 65535
      char *endptr;
      long port_num = strtol(value_str, &endptr, 10);
      if (*endptr != '\0' || value_str == endptr || port_num < 1 || port_num > 65535) {
        log_error("Invalid port value '%s'. Port must be a number between 1 and 65535.", value_str);
        exit(EXIT_FAILURE);
      }
      snprintf(opt_port, OPTIONS_BUFF_SIZE, "%s", value_str);
      break;
    }

    case 'x': {
      char *value_str = strip_equals_prefix(optarg, argbuf, sizeof(argbuf));
      if (value_str) {
        opt_width = strtoint(value_str);
        if (opt_width == 0) {
          log_error("Invalid width value '%s'. Width must be a positive integer.", value_str);
          exit(EXIT_FAILURE);
        }
        auto_width = 0; // Mark as manually set
      }
      break;
    }

    case 'y': {
      char *value_str = strip_equals_prefix(optarg, argbuf, sizeof(argbuf));
      if (value_str) {
        opt_height = strtoint(value_str);
        if (opt_height == 0) {
          log_error("Invalid height value '%s'. Height must be a positive integer.", value_str);
          exit(EXIT_FAILURE);
        }
        auto_height = 0; // Mark as manually set
      }
      break;
    }

    case 'c': {
      char *value_str = strip_equals_prefix(optarg, argbuf, sizeof(argbuf));
      int parsed_index = strtoint(value_str);
      if (parsed_index < 0) {
        log_error("Invalid webcam index value '%s'. Webcam index must be a non-negative integer.", value_str);
        exit(EXIT_FAILURE);
      }
      opt_webcam_index = (unsigned short int)parsed_index;
      break;
    }

    case 'f': {
      char *value_str = strip_equals_prefix(optarg, argbuf, sizeof(argbuf));
      int parsed_flip = strtoint(value_str);
      if (parsed_flip < 0 || parsed_flip > 1) {
        log_error("Invalid webcam flip value '%s'. Webcam flip must be 0 or 1.", value_str);
        exit(EXIT_FAILURE);
      }
      opt_webcam_flip = (unsigned short int)parsed_flip;
      break;
    }

    case 1000: { // --color-mode
      char *value_str = strip_equals_prefix(optarg, argbuf, sizeof(argbuf));
      if (strcmp(value_str, "auto") == 0) {
        opt_color_mode = COLOR_MODE_AUTO;
      } else if (strcmp(value_str, "mono") == 0 || strcmp(value_str, "monochrome") == 0) {
        opt_color_mode = COLOR_MODE_MONO;
      } else if (strcmp(value_str, "16") == 0 || strcmp(value_str, "16color") == 0) {
        opt_color_mode = COLOR_MODE_16_COLOR;
      } else if (strcmp(value_str, "256") == 0 || strcmp(value_str, "256color") == 0) {
        opt_color_mode = COLOR_MODE_256_COLOR;
      } else if (strcmp(value_str, "truecolor") == 0 || strcmp(value_str, "24bit") == 0) {
        opt_color_mode = COLOR_MODE_TRUECOLOR;
      } else {
        log_error("Error: Invalid color mode '%s'. Valid modes: auto, mono, 16, 256, truecolor", value_str);
        exit(1);
      }
      break;
    }

    case 1001: // --show-capabilities
      opt_show_capabilities = 1;
      break;
    case 1002: // --utf8
      opt_force_utf8 = 1;
      break;

    case 'M': { // --background-mode
      char *value_str = strip_equals_prefix(optarg, argbuf, sizeof(argbuf));
      if (strcmp(value_str, "foreground") == 0 || strcmp(value_str, "fg") == 0) {
        opt_background_mode = BACKGROUND_MODE_FOREGROUND;
      } else if (strcmp(value_str, "background") == 0 || strcmp(value_str, "bg") == 0) {
        opt_background_mode = BACKGROUND_MODE_BACKGROUND;
      } else {
        log_error("Error: Invalid background mode '%s'. Valid modes: foreground, background", value_str);
        exit(1);
      }
      break;
    }

    case 's':
      opt_stretch = 1;
      break;

    case 'A':
      opt_audio_enabled = 1;
      break;

    case 'q':
      opt_quiet = 1;
      break;

    case 'S':
      opt_snapshot_mode = 1;
      break;

    case 'D': {
      char *value_str = strip_equals_prefix(optarg, argbuf, sizeof(argbuf));
      char *endptr;
      opt_snapshot_delay = strtof(value_str, &endptr);
      if (*endptr != '\0' || value_str == endptr) {
        log_error("Invalid snapshot delay value '%s'. Snapshot delay must be a number.", value_str);
        exit(EXIT_FAILURE);
      }
      if (opt_snapshot_delay < 0.0f) {
        log_error("Snapshot delay must be non-negative (got %.2f)", opt_snapshot_delay);
        exit(EXIT_FAILURE);
      }
      break;
    }

    case 'L': {
      char *value_str = strip_equals_prefix(optarg, argbuf, sizeof(argbuf));
      if (strlen(value_str) == 0) {
        log_error("Invalid log file value '%s'. Log file path cannot be empty.", value_str);
        exit(EXIT_FAILURE);
      }
      snprintf(opt_log_file, OPTIONS_BUFF_SIZE, "%s", value_str);
      break;
    }

    case 'E':
      opt_encrypt_enabled = 1;
      break;

    case 'K': {
      char *value_str = strip_equals_prefix(optarg, argbuf, sizeof(argbuf));
      if (strlen(value_str) == 0) {
        log_error("Invalid encryption key value '%s'. Encryption key cannot be empty.", value_str);
        exit(EXIT_FAILURE);
      }
      snprintf(opt_encrypt_key, OPTIONS_BUFF_SIZE, "%s", value_str);
      opt_encrypt_enabled = 1; // Auto-enable encryption when key provided
      break;
    }

    case 'F': {
      char *value_str = strip_equals_prefix(optarg, argbuf, sizeof(argbuf));
      if (strlen(value_str) == 0) {
        log_error("Invalid keyfile value '%s'. Keyfile path cannot be empty.", value_str);
        exit(EXIT_FAILURE);
      }
      snprintf(opt_encrypt_keyfile, OPTIONS_BUFF_SIZE, "%s", value_str);
      opt_encrypt_enabled = 1; // Auto-enable encryption when keyfile provided
      break;
    }

    case '?':
      log_error("Unknown option %c", optopt);
      usage(stderr, is_client);
      exit(EXIT_FAILURE);
      break;

    case 'h':
      usage(stdout, is_client);
      exit(EXIT_SUCCESS);
      break;

    default:
      abort();
    }
  }

  // After parsing command line options, update dimensions
  // First set any auto dimensions to terminal size, then apply full height logic
  update_dimensions_to_terminal_size();
  update_dimensions_for_full_height();

  // Auto-enable color output based on terminal capabilities (unless explicitly disabled)
  if (!opt_color_output && opt_color_mode == COLOR_MODE_AUTO) {
    terminal_capabilities_t caps = detect_terminal_capabilities();
    if (caps.color_level > TERM_COLOR_NONE) {
      opt_color_output = 1;
      log_debug("Auto-enabled color output based on terminal capabilities: %s",
                terminal_color_level_name(caps.color_level));
    }
  }
}

#define USAGE_INDENT "    "

void usage_client(FILE *desc /* stdout|stderr*/) {
  fprintf(desc, "ascii-chat - client options\n");
  fprintf(desc, USAGE_INDENT "-h --help                    " USAGE_INDENT "print this help\n");
  fprintf(desc, USAGE_INDENT "-a --address ADDRESS         " USAGE_INDENT "IPv4 address (default: 0.0.0.0)\n");
  fprintf(desc, USAGE_INDENT "-p --port PORT               " USAGE_INDENT "TCP port (default: 27224)\n");
  fprintf(desc, USAGE_INDENT "-x --width WIDTH             " USAGE_INDENT "render width (default: [auto-set])\n");
  fprintf(desc, USAGE_INDENT "-y --height HEIGHT           " USAGE_INDENT "render height (default: [auto-set])\n");
  fprintf(desc,
          USAGE_INDENT "-c --webcam-index CAMERA     " USAGE_INDENT "webcam device index (0-based) (default: 0)\n");
  fprintf(desc, USAGE_INDENT "-f --webcam-flip             " USAGE_INDENT "horizontally flip the webcam "
                             "image (default: [unset])\n");
  fprintf(desc, USAGE_INDENT "   --color-mode MODE         " USAGE_INDENT "color modes: auto, mono, 16, 256, truecolor "
                             "(default: auto)\n");
  fprintf(desc,
          USAGE_INDENT "   --show-capabilities       " USAGE_INDENT "show detected terminal capabilities and exit\n");
  fprintf(desc, USAGE_INDENT "   --utf8                    " USAGE_INDENT "force enable UTF-8/Unicode support\n");
  fprintf(desc, USAGE_INDENT "-M --background-mode         " USAGE_INDENT "Render colors for glyphs or cells: "
                             "foreground, background (default: foreground)\n");
  fprintf(desc, USAGE_INDENT "-A --audio                   " USAGE_INDENT
                             "enable audio capture and playback (default: [unset])\n");
  fprintf(desc, USAGE_INDENT "-s --stretch                 " USAGE_INDENT "stretch or shrink video to fit "
                             "(ignore aspect ratio) (default: [unset])\n");
  fprintf(desc, USAGE_INDENT "-q --quiet                   " USAGE_INDENT
                             "disable console logging (log only to file) (default: [unset])\n");
  fprintf(desc, USAGE_INDENT "-S --snapshot                " USAGE_INDENT
                             "capture single frame and exit (default: [unset])\n");
  fprintf(desc,
          USAGE_INDENT "-D --snapshot-delay SECONDS  " USAGE_INDENT "delay SECONDS before snapshot (default: %.1f)\n",
          SNAPSHOT_DELAY_DEFAULT);
  fprintf(desc, USAGE_INDENT "-L --log-file FILE           " USAGE_INDENT "redirect logs to FILE (default: [unset])\n");
  fprintf(desc,
          USAGE_INDENT "-E --encrypt                 " USAGE_INDENT "enable packet encryption (default: [unset])\n");
  fprintf(desc, USAGE_INDENT "-K --key PASSWORD            " USAGE_INDENT
                             "encryption passphrase (implies --encrypt) (default: [unset])\n");
  fprintf(desc, USAGE_INDENT "-F --keyfile FILE            " USAGE_INDENT "read encryption key from FILE "
                             "(implies --encrypt) (default: [unset])\n");
}

void usage_server(FILE *desc /* stdout|stderr*/) {
  fprintf(desc, "ascii-chat - server options\n");
  fprintf(desc, USAGE_INDENT "-h --help            " USAGE_INDENT "print this help\n");
  fprintf(desc, USAGE_INDENT "-a --address ADDRESS " USAGE_INDENT "IPv4 address to bind to (default: 0.0.0.0)\n");
  fprintf(desc, USAGE_INDENT "-p --port PORT       " USAGE_INDENT "TCP port to listen on (default: 27224)\n");
  fprintf(desc,
          USAGE_INDENT "-A --audio           " USAGE_INDENT "enable audio streaming to clients (default: [unset])\n");
  fprintf(desc, USAGE_INDENT "-L --log-file FILE   " USAGE_INDENT "redirect logs to file (default: [unset])\n");
  fprintf(desc, USAGE_INDENT "-E --encrypt         " USAGE_INDENT "enable packet encryption (default: [unset])\n");
  fprintf(desc, USAGE_INDENT "-K --key PASSWORD    " USAGE_INDENT
                             "encryption passphrase (implies --encrypt) (default: [unset])\n");
  fprintf(desc, USAGE_INDENT "-F --keyfile FILE    " USAGE_INDENT "read encryption key from file "
                             "(implies --encrypt) (default: [unset])\n");
}

void usage(FILE *desc /* stdout|stderr*/, bool is_client) {
  if (is_client) {
    usage_client(desc);
  } else {
    usage_server(desc);
  }
}
