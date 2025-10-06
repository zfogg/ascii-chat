#pragma once

#include <stdbool.h>
#include <stdio.h>
#include "platform/terminal.h"
#include "palette.h"

#define OPTIONS_BUFF_SIZE 256

// Default "terminal dimensions"
#define OPT_WIDTH_DEFAULT 110
#define OPT_HEIGHT_DEFAULT 70

// Safely parse string to integer with validation
// Returns the integer value, or INT_MIN on error
int strtoint_safe(const char *str);

extern unsigned short int opt_width, opt_height;
extern bool auto_width, auto_height;

extern char opt_address[], opt_port[];

extern unsigned short int opt_webcam_index;

extern bool opt_webcam_flip;

extern bool opt_test_pattern; // Use test pattern instead of real webcam

// Terminal color mode override (client only)
typedef enum {
  COLOR_MODE_AUTO = 0,      // Auto-detect terminal capabilities (default)
  COLOR_MODE_MONO = 1,      // Force monochrome/grayscale
  COLOR_MODE_16_COLOR = 2,  // Force 16-color ANSI
  COLOR_MODE_256_COLOR = 3, // Force 256-color palette
  COLOR_MODE_TRUECOLOR = 4  // Force 24-bit truecolor
} terminal_color_mode_t;

// Render mode is now defined in platform/terminal.h

extern terminal_color_mode_t opt_color_mode;     // Color mode override
extern render_mode_t opt_render_mode;            // Render mode override
extern unsigned short int opt_show_capabilities; // Show detected capabilities and exit
extern unsigned short int opt_force_utf8;        // Force enable UTF-8 support via --utf8

extern unsigned short int opt_audio_enabled;

// If non-zero, allow image to stretch or shrink without preserving aspect ratio
extern unsigned short int opt_stretch;

// If non-zero, disable console logging (quiet mode)
extern unsigned short int opt_quiet;

// If non-zero, enable snapshot mode (client only - capture one frame and exit)
extern unsigned short int opt_snapshot_mode;

// Snapshot delay in seconds (float) - default 3.0 for webcam warmup
extern float opt_snapshot_delay;

// Log file path for file logging (empty string means no file logging)
extern char opt_log_file[OPTIONS_BUFF_SIZE];

// Encryption options
extern unsigned short int opt_encrypt_enabled;      // Enable AES encryption
extern char opt_encrypt_key[OPTIONS_BUFF_SIZE];     // Encryption key from --key
extern char opt_encrypt_keyfile[OPTIONS_BUFF_SIZE]; // Key file path from --keyfile

// Palette options
extern palette_type_t opt_palette_type; // Selected palette type
extern char opt_palette_custom[256];    // Custom palette characters
extern bool opt_palette_custom_set;     // True if custom palette was set
// Default weights; must add up to 1.0
extern const float weight_red;
extern const float weight_green;
extern const float weight_blue;

extern unsigned short int RED[], GREEN[], BLUE[], GRAY[];

void options_init(int argc, char **argv, bool is_client);

void usage(FILE *desc, bool is_client);
void usage_client(FILE *desc);
void usage_server(FILE *desc);

// Terminal size detection functions (get_terminal_size moved to platform/terminal.h)
void update_dimensions_for_full_height(void);
void update_dimensions_to_terminal_size(void);
