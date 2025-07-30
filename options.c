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

// Default weights; must add up to 1.0
static const float weight_red = 0.2989f;
static const float weight_green = 0.5866f;
static const float weight_blue = 0.1145f;

static const unsigned short default_width = 110, default_height = 70;
unsigned short int opt_width = default_width, opt_height = default_height,

                   auto_width = 1, auto_height = 1;

char opt_address[OPTIONS_BUFF_SIZE] = "0.0.0.0", opt_port[OPTIONS_BUFF_SIZE] = "90001";

unsigned short int opt_webcam_index = 0;

unsigned short int opt_webcam_flip = 1;

unsigned short int opt_color_output = 0;

unsigned short int opt_background_color = 0;

// Global variables to store last known image dimensions for aspect ratio
// recalculation
unsigned short int last_image_width = 0, last_image_height = 0;

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
                                       {"color", no_argument, NULL, 'C'},
                                       {"background-color", no_argument, NULL, 'b'},
                                       {"help", optional_argument, NULL, 'h'},
                                       {0, 0, 0, 0}};

// Terminal size detection functions
int get_terminal_size(unsigned short int *width, unsigned short int *height) {
  struct winsize w;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == -1) {
    // Fallback to environment variables
    char *cols_str = getenv("COLUMNS");
    char *lines_str = getenv("LINES");

    if (cols_str && lines_str) {
      *width = strtoint(cols_str);
      *height = strtoint(lines_str);
      return 0;
    }

    // Final fallback to default values
    *width = default_width;
    *height = default_height;
    return -1;
  }

  *width = w.ws_col;
  *height = w.ws_row;
  return 0;
}

void update_dimensions_for_full_height(void) {
  unsigned short int term_width, term_height;

  if (get_terminal_size(&term_width, &term_height) == 0) {
    // If both dimensions are auto, set height to terminal height and let
    // aspect_ratio calculate width
    if (auto_height && auto_width) {
      opt_height = term_height;
      // Note: width will be calculated by aspect_ratio() when next image is
      // loaded
    }
    // If only height is auto, use full terminal height
    else if (auto_height) {
      opt_height = term_height;
    }
    // If only width is auto, use full terminal width
    else if (auto_width) {
      opt_width = term_width;
    }
  }
}

void recalculate_aspect_ratio_on_resize(void) {
  unsigned short int term_width, term_height;

  // Get current terminal size
  if (get_terminal_size(&term_width, &term_height) == 0) {
    // If both dimensions are auto and we have image dimensions, calculate
    // aspect ratio
    if (last_image_width > 0 && last_image_height > 0) {
      // Calculate both possible approaches and choose the one that fits
      unsigned short int width_based_height, height_based_width;

      // Approach 1: Set height to terminal height, calculate width
      opt_height = term_height;
      aspect_ratio(last_image_width, last_image_height);
      height_based_width = opt_width;

      // Approach 2: Set width to terminal width, calculate height
      opt_width = term_width;
      aspect_ratio(last_image_width, last_image_height);
      width_based_height = opt_height;

      // Choose the approach that fits better (prioritize height fitting)
      if (height_based_width <= term_width) {
        // Height-based approach fits
        opt_height = term_height;
        opt_width = height_based_width;
      } else {
        // Width-based approach fits
        opt_width = term_width;
        opt_height = width_based_height;
      }
    } else if (auto_width && auto_height) {
      // If no image loaded but both dimensions are auto, use terminal
      // dimensions
      opt_height = term_height;
      opt_width = term_width;
    }
  }
}

void options_init(int argc, char **argv) {
  precalc_rgb(weight_red, weight_green, weight_blue);
  recalculate_aspect_ratio_on_resize();

  while (1) {
    int index = 0, c = getopt_long(argc, argv, "a:p:x:y:c:f:Cbh", long_options, &index);
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

    case 'C':
      opt_color_output = 1;
      break;

    case 'b':
      opt_background_color = 1;
      break;

    case '?':
      fprintf(stderr, "Unknown option %c\n", optopt);
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
  fprintf(desc, "\t\t -a --address      (server|client) \t IPv4 address\n");
  fprintf(desc, "\t\t -p --port         (server|client) \t TCP port\n");
  fprintf(desc, "\t\t -x --width        (client) \t     render width\n");
  fprintf(desc, "\t\t -y --height       (client) \t     render height\n");
  fprintf(desc, "\t\t -c --webcam-index (server) \t     webcam device index (0-based)\n");
  fprintf(desc, "\t\t -f --webcam-flip  (server) \t     horizontally flip the "
                "image (usually desirable)\n");
  fprintf(desc, "\t\t -C --color        (server|client) \t enable colored "
                "ASCII output\n");
  fprintf(desc, "\t\t -h --help         (server|client) \t print this help\n");
}

void precalc_rgb(const float red, const float green, const float blue) {
  for (int n = 0; n < ASCII_LUMINANCE_LEVELS; ++n) {
    RED[n] = ((float)n) * red;
    GREEN[n] = ((float)n) * green;
    BLUE[n] = ((float)n) * blue;
    GRAY[n] = ((float)n);
  }
}

/* Frame buffer size calculation */
size_t get_frame_buffer_size(void) {
  return opt_color_output ? FRAME_BUFFER_SIZE_COLOR : FRAME_BUFFER_SIZE;
}
