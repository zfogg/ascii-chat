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

char opt_address[OPTIONS_BUFF_SIZE] = "0.0.0.0", opt_port[OPTIONS_BUFF_SIZE] = "90001";

unsigned short int opt_webcam_index = 0;

unsigned short int opt_webcam_flip = 1;

unsigned short int opt_color_output = 0;

// Terminal color mode and capability options
terminal_color_mode_t opt_color_mode = COLOR_MODE_AUTO;       // Auto-detect by default
background_mode_t opt_background_mode = BACKGROUND_MODE_AUTO; // Auto/foreground by default
unsigned short int opt_show_capabilities = 0;                 // Don't show capabilities by default
unsigned short int opt_force_utf8 = 0;                        // Don't force UTF-8 by default

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

static struct option long_options[] = {{"address", required_argument, NULL, 'a'},
                                       {"port", required_argument, NULL, 'p'},
                                       {"width", optional_argument, NULL, 'x'},
                                       {"height", optional_argument, NULL, 'y'},
                                       {"webcam-index", required_argument, NULL, 'c'},
                                       {"webcam-flip", optional_argument, NULL, 'f'},
                                       {"color-mode", required_argument, NULL, 1000},     // New option
                                       {"show-capabilities", no_argument, NULL, 1001},    // New option
                                       {"utf8", no_argument, NULL, 1002},                 // UTF-8 override
                                       {"background-mode", required_argument, NULL, 'M'}, // Background mode
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
  // Get current terminal size
  if (get_terminal_size(&term_width, &term_height) == 0) {
    log_debug("Initial terminal size: %dx%d", term_width, term_height);
    if (auto_width) {
      opt_width = term_width;
    }
    if (auto_height) {
      opt_height = term_height;
    }
    log_debug("After initial update: opt_width=%d, opt_height=%d", opt_width, opt_height);
  } else {
    log_debug("Failed to get initial terminal size");
  }
}

void options_init(int argc, char **argv) {
  update_dimensions_to_terminal_size();

  while (1) {
    int index = 0, c = getopt_long(argc, argv, "a:p:x:y:c:f::AsqSD:L:EK:F:h", long_options, &index);
    if (c == -1)
      break;

    char argbuf[1024];
    switch (c) {
    case 0:
      break;

    case 'a':
      snprintf(opt_address, OPTIONS_BUFF_SIZE, "%s", optarg);
      break;

    case 'p':
      snprintf(opt_port, OPTIONS_BUFF_SIZE, "%s", optarg);
      break;

    case 'x':
      snprintf(argbuf, OPTIONS_BUFF_SIZE, "%s", optarg);
      opt_width = strtoint(argbuf);
      auto_width = 0; // Mark as manually set
      break;

    case 'y':
      snprintf(argbuf, OPTIONS_BUFF_SIZE, "%s", optarg);
      opt_height = strtoint(argbuf);
      auto_height = 0; // Mark as manually set
      break;

    case 'c':
      snprintf(argbuf, OPTIONS_BUFF_SIZE, "%s", optarg);
      opt_webcam_index = strtoint(argbuf);
      break;

    case 'f':
      snprintf(argbuf, OPTIONS_BUFF_SIZE, "%s", optarg);
      opt_webcam_flip = strtoint(argbuf);
      break;

    case 1000: // --color-mode
      if (strcmp(optarg, "auto") == 0) {
        opt_color_mode = COLOR_MODE_AUTO;
      } else if (strcmp(optarg, "mono") == 0 || strcmp(optarg, "monochrome") == 0) {
        opt_color_mode = COLOR_MODE_MONO;
      } else if (strcmp(optarg, "16") == 0 || strcmp(optarg, "16color") == 0) {
        opt_color_mode = COLOR_MODE_16_COLOR;
      } else if (strcmp(optarg, "256") == 0 || strcmp(optarg, "256color") == 0) {
        opt_color_mode = COLOR_MODE_256_COLOR;
      } else if (strcmp(optarg, "truecolor") == 0 || strcmp(optarg, "24bit") == 0) {
        opt_color_mode = COLOR_MODE_TRUECOLOR;
      } else {
        log_error("Error: Invalid color mode '%s'. Valid modes: auto, mono, 16, 256, truecolor", optarg);
        exit(1);
      }
      break;

    case 1001: // --show-capabilities
      opt_show_capabilities = 1;
      break;
    case 1002: // --utf8
      opt_force_utf8 = 1;
      break;

    case 'M': // --background-mode
      if (strcmp(optarg, "auto") == 0) {
        opt_background_mode = BACKGROUND_MODE_AUTO;
      } else if (strcmp(optarg, "foreground") == 0 || strcmp(optarg, "fg") == 0) {
        opt_background_mode = BACKGROUND_MODE_FOREGROUND;
      } else if (strcmp(optarg, "background") == 0 || strcmp(optarg, "bg") == 0) {
        opt_background_mode = BACKGROUND_MODE_BACKGROUND;
      } else {
        log_error("Error: Invalid background mode '%s'. Valid modes: auto, foreground, background", optarg);
        exit(1);
      }
      break;

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

    case 'D':
      opt_snapshot_delay = atof(optarg);
      if (opt_snapshot_delay < 0.0f) {
        log_error("Snapshot delay must be non-negative (got %.2f)", opt_snapshot_delay);
        exit(EXIT_FAILURE);
      }
      break;

    case 'L':
      snprintf(opt_log_file, OPTIONS_BUFF_SIZE, "%s", optarg);
      break;

    case 'E':
      opt_encrypt_enabled = 1;
      break;

    case 'K':
      snprintf(opt_encrypt_key, OPTIONS_BUFF_SIZE, "%s", optarg);
      opt_encrypt_enabled = 1; // Auto-enable encryption when key provided
      break;

    case 'F':
      snprintf(opt_encrypt_keyfile, OPTIONS_BUFF_SIZE, "%s", optarg);
      opt_encrypt_enabled = 1; // Auto-enable encryption when keyfile provided
      break;

    case '?':
      log_error("Unknown option %c", optopt);
      usage(stderr);
      exit(EXIT_FAILURE);
      break;

    case 'h':
      usage(stdout);
      exit(EXIT_SUCCESS);
      break;

    default:
      abort();
    }
  }

  // After parsing command line options, update dimensions for full terminal
  // usage
  update_dimensions_for_full_height();
}

void usage(FILE *desc /* stdout|stderr*/) {
  fprintf(desc, "ascii-chat\n");
  fprintf(desc, "\toptions:\n");
  fprintf(desc, "\t\t -a --address                 (server|client) \t IPv4 address\n");
  fprintf(desc, "\t\t -p --port                    (server|client) \t TCP port\n");
  fprintf(desc, "\t\t -x --width                   (client) \t     render width\n");
  fprintf(desc, "\t\t -y --height                  (client) \t     render height\n");
  fprintf(desc, "\t\t -c --webcam-index            (server) \t     webcam device index (0-based)\n");
  fprintf(desc, "\t\t -f --webcam-flip             (server) \t     horizontally flip the "
                "image (usually desirable)\n");
  fprintf(
      desc,
      "\t\t    --color-mode              (client) \t     color mode: auto, mono, 16, 256, truecolor (default: auto)\n");
  fprintf(desc, "\t\t    --show-capabilities       (client) \t show detected terminal capabilities and exit\n");
  fprintf(desc, "\t\t    --utf8                    (client) \t     force enable UTF-8/Unicode support\n");
  fprintf(desc, "\t\t -M --background-mode                   (client) \t     background rendering: auto, foreground, "
                "background (default: auto)\n");
  fprintf(desc, "\t\t -A --audio                   (server|client) \t enable audio capture and playbook\n");
  fprintf(
      desc,
      "\t\t -s --stretch                 (server|client) \t allow stretching and shrinking (ignore aspect ratio)\n");
  fprintf(desc, "\t\t -q --quiet                   (client) \t     disable console logging (logs only to file)\n");
  fprintf(desc, "\t\t -S --snapshot                (client) \t     capture single frame and exit\n");
  fprintf(desc, "\t\t -D --snapshot-delay SECONDS  (client) \t delay before snapshot (default: 3.0)\n");
  fprintf(desc, "\t\t -L --log-file                (server|client) \t redirect logs to file\n");
  fprintf(desc, "\t\t -E --encrypt                 (server|client) \t enable AES packet encryption\n");
  fprintf(desc, "\t\t -K --key PASSWORD            (server|client) \t encryption passphrase (implies --encrypt)\n");
  fprintf(desc,
          "\t\t -F --keyfile FILE            (server|client) \t read encryption key from file (implies --encrypt)\n");
  fprintf(desc, "\t\t -h --help                    (server|client) \t print this help\n");
}
