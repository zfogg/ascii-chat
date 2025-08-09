#include "common.h"
#include <stdio.h>
#include <time.h>

// Forward declaration
typedef struct image_t image_t;

asciichat_error_t ascii_read_init(unsigned short int webcam_index);
asciichat_error_t ascii_write_init(void);

char *ascii_convert(image_t *original, const ssize_t width, const ssize_t height, const bool color, const bool stretch);
asciichat_error_t ascii_write(const char *);

void ascii_read_destroy(void);
void ascii_write_destroy(void);

// Utility to add leading spaces (left-padding) to each line of a frame.
// Caller owns the returned buffer and must free it with free().
char *ascii_pad_frame_width(const char *frame, size_t pad);

// Utility to add blank lines (vertical padding) to center a frame vertically.
// Caller owns the returned buffer and must free it with free().
char *ascii_pad_frame_height(const char *frame, size_t pad_top);

// Grid layout utility for combining multiple ASCII frames into a single grid
typedef struct {
  const char *frame_data;
  size_t frame_size;
} ascii_frame_source_t;

char *ascii_create_grid(ascii_frame_source_t *sources, int source_count, int width, int height, size_t *out_size);

char *get_lum_palette(void);

#define ASCII_LUMINANCE_LEVELS 256

#define ASCII_SLEEP_NS 50000L

// ANSI color codes
#define ANSI_RESET "\033[0m"
#define ANSI_FG_PREFIX "\033[38;2;"
#define ANSI_BG_PREFIX "\033[48;2;"
#define ANSI_COLOR_SUFFIX "m"

#define print(s) fwrite(s, 1, sizeof(s) / sizeof(s[0]), stdout)

#define console_clear() print("\e[1;1H\e[2J")

// #define cursor_reset() print("\033[0;0H")
#define cursor_reset() print("\033[H")

#define cursor_hide() print("\e[?25l")

#define cursor_show() print("\e[?25h")

static const struct timespec ASCII_SLEEP_START = {.tv_sec = 0, .tv_nsec = 500},
                             ASCII_SLEEP_STOP = {.tv_sec = 0, .tv_nsec = 0};

#define ascii_zzz() nanosleep((struct timespec *)&ASCII_SLEEP_START, (struct timespec *)&ASCII_SLEEP_STOP)
