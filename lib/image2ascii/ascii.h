#pragma once

#ifdef _WIN32
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#endif

#include <stdio.h>
#include <time.h>
#include "simd/common.h"

// Include platform abstraction for write function mapping and deprecation suppression
#ifndef PLATFORM_ABSTRACTION_H
#include "platform/abstraction.h"
#endif

extern char ascii_palette[];

// Forward declaration
typedef struct image_t image_t;

asciichat_error_status_t ascii_read_init(unsigned short int webcam_index);
asciichat_error_status_t ascii_write_init(int fd, bool reset_terminal);

char *ascii_convert(image_t *original, const ssize_t width, const ssize_t height, const bool color,
                    const bool aspect_ratio, const bool stretch, const char *palette_chars,
                    const char luminance_palette[256]);

// Capability-aware ASCII conversion using terminal detection
#include "platform/terminal.h"
char *ascii_convert_with_capabilities(image_t *original, const ssize_t width, const ssize_t height,
                                      const terminal_capabilities_t *caps, const bool use_aspect_ratio,
                                      const bool stretch, const char *palette_chars, const char luminance_palette[256]);
asciichat_error_status_t ascii_write(const char *frame);

void ascii_read_destroy(void);
void ascii_write_destroy(int fd, bool reset_terminal);

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
// #define ANSI_RESET "\033[0m"
#define ANSI_FG_PREFIX "\033[38;2;"
#define ANSI_BG_PREFIX "\033[48;2;"
#define ANSI_COLOR_SUFFIX "m"

#define print(s) fwrite(s, 1, sizeof(s) / sizeof((s)[0]), stdout)

// Terminal operations using platform abstraction layer functions
#define console_clear(fd) (terminal_clear_screen(), terminal_cursor_home(fd))
#define cursor_reset(fd) terminal_cursor_home(fd)
#define ascii_clear_screen(fd) terminal_clear_screen()
#define cursor_hide(fd) terminal_hide_cursor(fd, true)
#define cursor_show(fd) terminal_hide_cursor(fd, false)

// Note: terminal_reset() and terminal_clear_scrollback(fd) are available as functions

static const struct timespec ASCII_SLEEP_START = {.tv_sec = 0, .tv_nsec = 500},
                             ASCII_SLEEP_STOP = {.tv_sec = 0, .tv_nsec = 0};

#define ascii_zzz() nanosleep((struct timespec *)&ASCII_SLEEP_START, (struct timespec *)&ASCII_SLEEP_STOP)
