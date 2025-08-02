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

static const unsigned short default_width = 110, default_height = 70;
unsigned short int opt_width = default_width, opt_height = default_height,

                   auto_width = 1, auto_height = 1;

char opt_address[OPTIONS_BUFF_SIZE] = "0.0.0.0", opt_port[OPTIONS_BUFF_SIZE] = "90001";

unsigned short int opt_webcam_index = 0;

unsigned short int opt_webcam_flip = 1;

unsigned short int opt_color_output = 0;

unsigned short int opt_background_color = 0;

unsigned short int opt_audio_enabled = 0;

// Allow stretching/shrinking without preserving aspect ratio when set via -s/--stretch
unsigned short int opt_stretch = 0;

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
                                       {"color", no_argument, NULL, 'C'},
                                       {"background-color", no_argument, NULL, 'b'},
                                       {"audio", no_argument, NULL, 'A'},
                                       {"stretch", no_argument, NULL, 's'},
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

void update_dimensions_to_terminal_size(void) {
  unsigned short int term_width, term_height;
  // Get current terminal size
  if (get_terminal_size(&term_width, &term_height) == 0) {
    if (auto_width) {
      opt_width = term_width;
    }
    if (auto_height) {
      opt_height = term_height;
    }
  }
}

void options_init(int argc, char **argv) {
  update_dimensions_to_terminal_size();

  while (1) {
    int index = 0, c = getopt_long(argc, argv, "a:p:x:y:c:f::CbAsh", long_options, &index);
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

    case 's':
      opt_stretch = 1;
      break;

    case 'b':
      opt_background_color = 1;
      break;

    case 'A':
      opt_audio_enabled = 1;
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
  fprintf(desc, "\t\t -b --background-color (server|client) \t enable background color for ASCII output\n");
  fprintf(desc, "\t\t -A --audio        (server|client) \t enable audio capture and playback\n");
  fprintf(desc, "\t\t -s --stretch          (server|client) \t allow stretching and shrinking (ignore aspect ratio)\n");
  fprintf(desc, "\t\t -h --help         (server|client) \t print this help\n");
}
