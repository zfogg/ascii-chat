#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "common.h"

// Terminal color capability levels
typedef enum {
  TERM_COLOR_NONE = 0,     // Monochrome only
  TERM_COLOR_16 = 1,       // Basic ANSI colors (16 colors)
  TERM_COLOR_256 = 2,      // Extended color palette (256 colors)
  TERM_COLOR_TRUECOLOR = 3 // 24-bit RGB support (16.7 million colors)
} terminal_color_level_t;

// Terminal capability flags (bitmask)
#define TERM_CAP_COLOR_16 0x01   // Basic ANSI 16-color support
#define TERM_CAP_COLOR_256 0x02  // 256-color palette support
#define TERM_CAP_COLOR_TRUE 0x04 // 24-bit RGB truecolor support
#define TERM_CAP_UTF8 0x08       // UTF-8/Unicode support
#define TERM_CAP_BACKGROUND 0x10 // Background color support

// Terminal capability detection results
typedef struct {
  uint32_t capabilities;              // Bitmask of TERM_CAP_* flags
  terminal_color_level_t color_level; // Highest supported color level
  uint32_t color_count;               // Actual color count (16, 256, 16777216)
  bool utf8_support;                  // UTF-8/Unicode character support
  char term_type[32];                 // $TERM value for debugging
  char colorterm[32];                 // $COLORTERM value for debugging
  bool detection_reliable;            // True if detection methods were reliable
} terminal_capabilities_t;

// Terminal size detection functions (moved from options.c)
int get_terminal_size(unsigned short int *width, unsigned short int *height);

// Terminal capability detection functions
terminal_capabilities_t detect_terminal_capabilities(void);
terminal_color_level_t detect_color_support(void);
bool detect_truecolor_support(void);
bool detect_256color_support(void);
bool detect_16color_support(void);
bool detect_utf8_support(void);
bool terminal_supports_unicode_blocks(void);

// Capability query functions
const char *terminal_color_level_name(terminal_color_level_t level);
const char *terminal_capabilities_summary(const terminal_capabilities_t *caps);

// Environment-based detection helpers
bool check_colorterm_variable(void);
bool check_term_variable_for_colors(void);
int get_terminfo_color_count(void);

// Debugging and testing functions
void print_terminal_capabilities(const terminal_capabilities_t *caps);
void test_terminal_output_modes(void);

// Color mode override function for terminal capabilities
terminal_capabilities_t apply_color_mode_override(terminal_capabilities_t caps);
